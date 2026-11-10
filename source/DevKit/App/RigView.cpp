#include <math.h>
#include <limits.h>
#include <stdio.h>

#include "imgui.h"
#include "imgui_stdlib.h"
#include "renderer.h"

#include "animedit.h"
#include "draw2d.h"
#include "draw2d_gfx.h"

#include "imgui_impl_gfx.h"
#include "Icons.h"
#include "PartTree.h"
#include "Platform.h"
#include "Properties.h"
#include "ClipDocument.h"
#include "RigView.h"
#include "WorkspaceActions.h"

namespace devkit {

namespace {

// How long a notice stays, in seconds.
const double NOTICE_SECONDS = 5.0;
// The merge keys of the commands: a drag on the canvas, and the fields.
const int DRAG_KEY = 1, FIELD_KEY = 2;
// The radius of the ring around the selected part, and how close to a
// handle a click must be, in points of the window.
const float RING_POINTS = 40, GRAB_POINTS = 8;
const double pi = 3.14159265358979323846;

float distance (float x1, float y1, float x2, float y2) {
	return sqrtf ((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
}

// A transform without its place: how it turns, scales and mirrors.
Transform2D linear (Transform2D t) {
	t.tx = t.ty = 0;
	return t;
}

// From -180 to 180 degrees.
float wrapDegrees (float d) {
	d = fmodf (d, 360.0f);
	if (d >= 180.0f) d -= 360.0f;
	if (d < -180.0f) d += 360.0f;
	return d;
}

}

RigView::RigView (RigDocument * r, WorkspaceActions & a, const CanvasGraphics & g)
	: rig (r), actions (a), graphics (g), canvas (g), selected (0), haveSelection (false), preview (g.draw),
	  drag (DRAG_NONE), dragX (0), dragY (0), dragPivotX (0), dragPivotY (0), nameFieldPart (0), nameEditing (false),
	  keepPivotInPlace (true), pending (new Pending), noticeUntil (0) {
}

RigView::~RigView () {
}

bool RigView::animating () const {
	return drag != DRAG_NONE || canvas.animating ();
}

void RigView::say (const std::string & text) {
	notice = text;
	noticeUntil = ImGui::GetTime () + NOTICE_SECONDS;
}

std::string RigView::baseFolder () const {
	std::string project = actions.projectFile ();
	return project.empty () ? folderOf (rig->path ()) : folderOf (project);
}

int RigView::selectedIndex () const {
	return haveSelection ? partWithId (rig->rig (), selected) : -1;
}

AnimBankRef RigView::bank (const std::string & path) {
	return preview.bank (baseFolder (), path);
}

void RigView::pose () {
	posedRig = preview.rig (rig->asset (), baseFolder ());
	PoseBinding binding;
	evaluatePose (posedRig, ClipDefinitionRef (), TrackSamples (), Transform2D (), binding, restPose);
}

void RigView::addPart () {
	int at = selectedIndex ();
	PartId parent = rig->rig ().parts[at < 0 ? 0 : at].id;
	PartId added;
	std::string why;
	if (rig->addPart (parent, rig->freeName ("part"), &added, why)) {
		selected = added;
		haveSelection = true;
	} else {
		say (why);
	}
}

void RigView::removeSelected () {
	int at = selectedIndex ();
	if (at <= 0) return;
	PartId parent = rig->rig ().parts[rig->rig ().parts[at].parent].id;
	std::string why;
	if (rig->removePart (selected, why)) selected = parent;
	else say (why);
}

void RigView::moveSelected (int by) {
	int at = selectedIndex ();
	if (at <= 0) return;
	int parent = rig->rig ().parts[at].parent;
	std::vector<int> siblings = partChildren (rig->rig (), parent);
	int position = 0;
	for (size_t i = 0; i < siblings.size (); i ++) {
		if (siblings[i] == at) position = (int) i;
	}
	// Among the other parts of the parent, it goes before the one that is now
	// before it, or before the one after the one after it.
	int to = position + by;
	if (to < 0 || to >= (int) siblings.size ()) return;
	std::string why;
	if (! rig->placePart (selected, rig->rig ().parts[parent].id, to, why)) say (why);
}

bool RigView::canPick (int part) const {
	const RigDefinition & r = rig->rig ();
	return part >= 0 && part < (int) r.parts.size () && ! rig->isLocked (r.parts[part].id);
}

void RigView::centrePivot (const RigPart & part) {
	AnimBankRef b = bank (rig->bankOf (part.sprite));
	if (! b || part.sprite.number >= b->animSpriteCount ()) return;
	AnimSpriteSize s = b->animSpriteSize (part.sprite.number);
	// The middle of the sprite, from its hotspot; a mirrored sprite is
	// mirrored about its hotspot.
	float x = s.width * 0.5f - s.xhot, y = s.height * 0.5f - s.yhot;
	if (part.sprite.mirror) x = - x;
	rig->setPivot (part.id, x, y, keepPivotInPlace);
}

void RigView::chooseBank () {
	if (pending->waiting) return;
	std::string base = baseFolder ();
	if (base.empty ()) {
		say ("Save the rig, or open its project, first: the bank's path is from that folder.");
		return;
	}
	pending->waiting = true;
	pending->answered = false;
	std::shared_ptr<Pending> p = pending;
	std::vector<FileFilter> filters;
	FileFilter duc = { "Sprite banks", "duc" };
	filters.push_back (duc);
	actions.platform ().openFileDialog ("Choose the sprite bank of the part", filters, base, false,
		[p] (const std::vector<std::string> & files) {
			p->files = files;
			p->answered = true;
		});
}

void RigView::handlePending () {
	if (! pending->waiting || ! pending->answered) return;
	pending->waiting = false;
	std::vector<std::string> files = pending->files;
	if (! files.empty ()) setBank (files[0]);
}

void RigView::setBank (const std::string & file) {
	int at = selectedIndex ();
	if (at < 0) return;
	std::string base = baseFolder ();
	if (base.empty ()) {
		say ("Save the rig, or open its project, first: the bank's path is from that folder.");
		return;
	}
	const SpriteRef & now = rig->rig ().parts[at].sprite;
	std::string path = relativePathFrom (base, file);
	AnimBankRef b = bank (path);
	if (! b) {
		say ("That isn't a sprite bank that can be read.");
		return;
	}
	int number = now.kind == SpriteRef::SPRITE && now.number < b->animSpriteCount () ? now.number : 0;
	rig->setSprite (selected, path, number, now.kind == SpriteRef::SPRITE && now.mirror);
}

bool RigView::takeFile (const std::string & path) {
	if (extensionOf (path) != "duc" || selectedIndex () < 0) return false;
	setBank (path);
	return true;
}

void RigView::drawToolbar () {
	int at = selectedIndex ();
	if (ImGui::Button ("Add Part")) addPart ();
	ImGui::SetItemTooltip ("A part attached to the selected one");
	ImGui::SameLine ();
	ImGui::BeginDisabled (at <= 0);
	if (ImGui::Button ("Remove")) removeSelected ();
	ImGui::SetItemTooltip ("The selected part, and what is attached to it");
	ImGui::SameLine ();
	// The arrows, if the font has them; ImGui's own (as in smoke tests)
	// hasn't.
	bool arrows = ImGui::GetFont ()->IsGlyphInFont (0x2191) && ImGui::GetFont ()->IsGlyphInFont (0x2193);
	if (arrows ? ImGui::Button ("\xE2\x86\x91##up") : ImGui::ArrowButton ("##up", ImGuiDir_Up)) moveSelected (-1);
	ImGui::SetItemTooltip ("Move Up: before the part above it, under the same part, drawn first at the same depth");
	ImGui::SameLine ();
	if (arrows ? ImGui::Button ("\xE2\x86\x93##down") : ImGui::ArrowButton ("##down", ImGuiDir_Down)) moveSelected (1);
	ImGui::SetItemTooltip ("Move Down: after the part below it, under the same part, drawn later at the same depth");
	ImGui::EndDisabled ();
	ImGui::SameLine (0, ImGui::GetFontSize () * 2);
	canvas.drawZoomButtons ();
}

void RigView::drawTree () {
	PartTree tree;
	RigView * self = this;
	tree.isLocked = [self] (PartId id) { return self->rig->isLocked (id); };
	tree.setLocked = [self] (PartId id, bool locked) {
		self->rig->setLocked (id, locked);
		if (locked && self->haveSelection && self->selected == id) self->haveSelection = false;
	};
	tree.select = [self] (PartId id) {
		self->selected = id;
		self->haveSelection = true;
	};
	tree.drop = [self] (PartId dragged, PartId onto) {
		std::string why;
		if (! self->rig->placePart (dragged, onto, INT_MAX, why)) self->say (why);
	};
	drawPartTree (rig->rig (), haveSelection, selected, tree);
}

void RigView::handleMouse () {
	float mx = canvas.mouseX (), my = canvas.mouseY ();
	if (canvas.pressed ()) {
		drag = DRAG_NONE;
		float grab = canvas.pixelsOfPoints (GRAB_POINTS);
		int at = selectedIndex ();
		if (at >= 0) {
			PartHandles h = partHandles (restPose, at, canvas.pixelsOfPoints (RING_POINTS));
			bool hasSprite = rig->rig ().parts[at].sprite.kind == SpriteRef::SPRITE;
			int handles = rig->scalesUniformly (selected) ? 4 : 8;
			// The nearest handle within reach: the origin, then the knob,
			// win a tie (a scale handle may lie on the origin).
			float nearest = grab;
			float d = distance (mx, my, h.originX, h.originY);
			if (d < nearest) {
				nearest = d;
				drag = hasSprite ? DRAG_PIVOT : DRAG_MOVE;
			}
			d = distance (mx, my, h.turnX, h.turnY);
			if (d < nearest) {
				nearest = d;
				drag = DRAG_TURN;
			}
			for (int i = 0; i < handles; i ++) {
				d = distance (mx, my, h.scale[i].x, h.scale[i].y);
				if (d < nearest) {
					nearest = d;
					drag = DRAG_SCALE;
					dragHandle = h.scale[i];
				}
			}
		}
		if (drag == DRAG_NONE) {
			RigView * self = this;
			int hit = partAt (restPose, mx, my, [self] (int part) { return self->canPick (part); });
			if (hit >= 0) {
				selected = rig->rig ().parts[hit].id;
				haveSelection = true;
				drag = DRAG_MOVE;
			} else {
				haveSelection = false;
			}
		}
		at = selectedIndex ();
		if (drag != DRAG_NONE && at >= 0) {
			const RigPart & part = rig->rig ().parts[at];
			dragX = mx;
			dragY = my;
			dragRest = part.rest;
			dragPivotX = part.pivotX;
			dragPivotY = part.pivotY;
			dragParent = part.parent >= 0 ? restPose.parts[part.parent].world : Transform2D ();
			dragWorld = restPose.parts[at].world;
		} else {
			drag = DRAG_NONE;
		}
	}

	if (drag != DRAG_NONE && canvas.held () && selectedIndex () >= 0) {
		float dx = mx - dragX, dy = my - dragY;
		if (drag == DRAG_MOVE) {
			// Moved in the world; placed against its parent.
			Transform2D up;
			if (linear (dragParent).inverse (up)) {
				float lx, ly;
				up.apply (dx, dy, lx, ly);
				PartPose rest = dragRest;
				rest.x = floorf (dragRest.x + lx + 0.5f);
				rest.y = floorf (dragRest.y + ly + 0.5f);
				rig->setRest (selected, rest, DRAG_KEY);
			}
		} else if (drag == DRAG_TURN) {
			float ox = dragWorld.tx, oy = dragWorld.ty;
			double turned = (atan2 (my - oy, mx - ox) - atan2 (dragY - oy, dragX - ox)) * 180.0 / pi;
			// Under a mirrored parent, the part turns the other way.
			if (dragParent.mirrors ()) turned = - turned;
			PartPose rest = dragRest;
			rest.rotation = wrapDegrees (floorf (dragRest.rotation + (float) turned + 0.5f));
			rig->setRest (selected, rest, DRAG_KEY);
		} else if (drag == DRAG_SCALE) {
			// From where the handle was, by as much as the mouse moved.
			ScaleHandle from = dragHandle;
			PartPose rest = dragRest;
			scaleFromHandle (dragParent, dragRest, from, rig->scalesUniformly (selected),
				from.x + dx, from.y + dy, rest.scaleX, rest.scaleY);
			rig->setRest (selected, rest, DRAG_KEY);
		} else if (drag == DRAG_PIVOT) {
			// The origin follows the mouse: the pivot moves that much in the
			// part's own space, and the sprite stays.
			Transform2D in;
			if (linear (dragWorld).inverse (in)) {
				float lx, ly;
				in.apply (dx, dy, lx, ly);
				rig->setPivot (selected, floorf (dragPivotX + lx + 0.5f), floorf (dragPivotY + ly + 0.5f), true, DRAG_KEY);
			}
		}
	}
	if (drag != DRAG_NONE && canvas.released ()) {
		drag = DRAG_NONE;
		rig->commands ().stopMerging ();
	}
}

void RigView::handleKeys () {
	if (! ImGui::IsWindowFocused (ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO ().WantTextInput) return;
	if (ImGui::Shortcut (ImGuiKey_Delete) || ImGui::Shortcut (ImGuiKey_Backspace)) removeSelected ();
}

void RigView::drawCanvas () {
	ImVec2 size = ImGui::GetContentRegionAvail ();
	size.y -= ImGui::GetTextLineHeightWithSpacing ();
	float x1, y1, x2, y2;
	if (! poseBounds (restPose, x1, y1, x2, y2)) {
		x1 = y1 = -100;
		x2 = y2 = 100;
	}
	// Room around the character for the handles.
	float margin = ((x2 - x1) > (y2 - y1) ? (x2 - x1) : (y2 - y1)) * 0.1f + 10;
	canvas.setPictureBounds ((int) floorf (x1 - margin), (int) floorf (y1 - margin),
		(int) ceilf (x2 - x1 + margin * 2), (int) ceilf (y2 - y1 + margin * 2));
	const Pose & p = restPose;
	const RigDefinition & r = rig->rig ();
	int at = selectedIndex ();
	float pixel = canvas.pixelsOfPoints (1);
	bool sides = at >= 0 && ! rig->scalesUniformly (selected);
	canvas.draw (size, [&p, &r, at, pixel, sides] (Draw2D & draw) {
		draw.clear ();
		drawPose (draw, p);
		drawPoseOverlay (draw, r, p, at, pixel, sides);
		draw.finish ();
	});
	handleMouse ();

	ImGui::TextDisabled ("%s", canvas.status ().c_str ());
	if (! notice.empty () && ImGui::GetTime () < noticeUntil) {
		ImGui::SameLine (0, ImGui::GetFontSize () * 2);
		ImGui::TextColored (ImVec4 (1.0f, 0.8f, 0.3f, 1.0f), "%s", notice.c_str ());
	}
}

void RigView::draw () {
	handlePending ();
	// Undo may have taken the selected part away, or locked it.
	if (selectedIndex () < 0 || (haveSelection && rig->isLocked (selected))) haveSelection = false;
	pose ();

	drawToolbar ();
	handleKeys ();
	float height = ImGui::GetContentRegionAvail ().y;
	ImGui::BeginChild ("parts", ImVec2 (ImGui::GetFontSize () * 12, height), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
	drawTree ();
	ImGui::EndChild ();
	ImGui::SameLine ();
	ImGui::BeginGroup ();
	drawCanvas ();
	ImGui::EndGroup ();
}

void RigView::drawInspector () {
	int at = selectedIndex ();
	if (at < 0) {
		ImGui::SeparatorText ("Rig");
		ImGui::Text ("%d part%s", (int) rig->rig ().parts.size () - 1, rig->rig ().parts.size () == 2 ? "" : "s");
		ImGui::TextWrapped ("Select a part in the tree or on the canvas, or add one.");
		return;
	}
	const RigPart part = rig->rig ().parts[at];
	PartId id = part.id;

	if (beginProperties (at == 0 ? "Character" : "Part")) {
		if (at > 0) {
			propertyRow ("Name");
			// The name, unless the user is typing another.
			if (nameFieldPart != id || ! nameEditing) {
				nameField = part.name;
				nameFieldPart = id;
			}
			ImGui::InputText ("##name", &nameField);
			nameEditing = ImGui::IsItemActive ();
			if (ImGui::IsItemDeactivatedAfterEdit () && nameField != part.name) {
				// The clips made for this rig name the part too.
				std::string why;
				std::vector<ClipDocument *> clips = actions.clipsForRename (rig->asset ().id, part.name, baseFolder ()), renamed;
				WorkspaceActions * a = &actions;
				if (! rig->renamePart (id, nameField, clips, [a] (ClipDocument * c) { return a->isOpen ((Document *) c); }, &renamed, why)) {
					say (why);
					nameField = part.name;
				} else if (! renamed.empty ()) {
					char text[96];
					snprintf (text, sizeof (text), "Renamed in %d clip%s too: save %s with the rig.", (int) renamed.size (),
						renamed.size () == 1 ? "" : "s", renamed.size () == 1 ? "it" : "them");
					say (text);
				}
			}

			propertyRow ("Attached to", "The part it hangs from: it rotates, scales and flips with it");
			const RigDefinition & r = rig->rig ();
			std::string current = part.parent == 0 ? std::string ("Character") : r.parts[part.parent].name;
			if (ImGui::BeginCombo ("##parent", current.c_str ())) {
				for (size_t i = 0; i < r.parts.size (); i ++) {
					if (partIsUnder (r, (int) i, at)) continue;
					std::string name = i == 0 ? std::string ("Character") : r.parts[i].name;
					if (ImGui::Selectable (name.c_str (), (int) i == part.parent) && (int) i != part.parent) {
						std::string why;
						PartId parent = r.parts[i].id;
						if (! rig->placePart (id, parent, INT_MAX, why)) say (why);
						break;
					}
				}
				ImGui::EndCombo ();
			}
		}

		std::string bankPath = rig->bankOf (part.sprite);
		propertyRow ("Sprite bank", "The bank of the part's own sprite; none makes it a joint, which only holds other parts");
		ImGui::TextUnformatted (bankPath.empty () ? "None (a joint)" : bankPath.c_str ());
		propertyRow ("");
		if (ImGui::Button ("Choose...")) chooseBank ();
		ImGui::SameLine ();
		ImGui::BeginDisabled (bankPath.empty ());
		if (ImGui::Button ("None")) rig->setSprite (id, "", 0, false);
		ImGui::EndDisabled ();
		if (! bankPath.empty ()) {
			AnimBankRef b = bank (bankPath);
			int number = part.sprite.number;
			propertyRow ("Sprite", "Its number in the bank");
			if (ImGui::InputInt ("##sprite", &number)) {
				int count = b ? b->animSpriteCount () : 0;
				if (number >= count) number = count - 1;
				if (number < 0) number = 0;
				rig->setSprite (id, bankPath, number, part.sprite.mirror);
			}
			bool mirror = part.sprite.mirror;
			propertyRow ("Mirror image", "Its sprite mirrored, the parts attached to it not");
			if (ImGui::Checkbox ("##mirror", &mirror)) rig->setSprite (id, bankPath, part.sprite.number, mirror);
			if (b && part.sprite.number < b->animSpriteCount ()) {
				const spriteBank * sprites = devkitBank (b);
				gfx::Texture * texture = gfxPictureTexture (graphics.draw, sprites, part.sprite.number, false);
				const sprite & s = sprites->sprites[part.sprite.number];
				float side = ImGui::GetFontSize () * 5;
				float zoom = s.width > s.height ? side / s.width : side / (s.height ? s.height : 1);
				propertyRow ("");
				if (texture) ImGui::Image (ImTextureRef (ImGui_ImplGfx_TextureID (texture)), ImVec2 (s.width * zoom, s.height * zoom));
			} else {
				propertyRow ("");
				ImGui::TextColored (ImVec4 (1.0f, 0.4f, 0.4f, 1.0f), b ? "The bank has no such sprite." : "The bank can't be read.");
			}
		}
		ImGui::EndTable ();
	}

	if (beginProperties ("Transform")) {
		PartPose rest = part.rest;
		bool changed = false, done = false;
		propertyRow ("Position", "Where its origin is, from its parent's, in its parent's pixels; +Y is down");
		changed |= xyFields ("position", &rest.x, &rest.y, 1.0f, "%g", 0, 0, &done);
		propertyRow ("Rotation", "In degrees, clockwise, about its pivot");
		changed |= ImGui::DragFloat ("##rotation", &rest.rotation, 1.0f, -360, 360, "%g\xC2\xB0");
		done |= ImGui::IsItemDeactivated ();

		bool uniform = rig->scalesUniformly (id);
		float sx = rest.scaleX * 100.0f, sy = rest.scaleY * 100.0f;
		propertyRow ("Scale", "In percent, along its own axes; the parts attached to it scale too");
		if (xyFields ("scale", &sx, &sy, 1.0f, "%g%%", 1, 10000, &done, iconButtonWidth ())) {
			float nx = (sx < 1 ? 1 : sx) / 100.0f, ny = (sy < 1 ? 1 : sy) / 100.0f;
			// Together, either one sets both.
			if (uniform) {
				if (nx != rest.scaleX) ny = nx;
				else nx = ny;
			}
			rest.scaleX = nx;
			rest.scaleY = ny;
			changed = true;
		}
		if (iconButton ("uniform", uniform ? ICON_LOCKED : ICON_UNLOCKED,
				uniform ? "X and Y scale together. Click to scale them apart." : "X and Y scale apart. Click to scale them together (Y takes X's).")) {
			rig->setUniformScale (id, ! uniform);
		}
		propertyRow ("Flip", "Mirrors it, and the parts attached to it");
		changed |= ImGui::Checkbox ("##flip", &rest.flip);
		propertyRow ("Visible", "Hidden, it hides the parts attached to it too");
		changed |= ImGui::Checkbox ("##visible", &rest.visible);
		if (changed) rig->setRest (id, rest, FIELD_KEY);
		int depth = part.depth;
		propertyRow ("Depth", "The order of drawing among the character's parts: higher is in front");
		if (ImGui::DragInt ("##depth", &depth, 0.1f)) rig->setDepth (id, depth, FIELD_KEY);
		done |= ImGui::IsItemDeactivated ();
		// The colours, last, as in the animation editor.
		if (const RigPart * now = rig->part (id)) {
			PartPose coloured = now->rest;
			bool recoloured = false;
			propertyRow ("Modulate", "Its colour, and the colour of the parts attached to it: each component a factor, from 0 to 200%");
			recoloured |= colourField ("modulate", coloured.modulate, MAX_MODULATE, &done);
			propertyRow ("Self Modulate", "Its own colour only, over Modulate: each component a factor, from 0 to 200%");
			recoloured |= colourField ("selfmodulate", coloured.selfModulate, MAX_MODULATE, &done);
			if (recoloured) rig->setRest (id, coloured, FIELD_KEY);
		}
		if (done) rig->commands ().stopMerging ();
		ImGui::EndTable ();
	}

	if (part.sprite.kind == SpriteRef::SPRITE && beginProperties ("Pivot")) {
		float px = part.pivotX, py = part.pivotY;
		bool changed = false, done = false;
		propertyRow ("Pivot", "The point it rotates and scales about, from its sprite's hotspot, in the sprite's pixels");
		changed |= xyFields ("pivot", &px, &py, 1.0f, "%g", 0, 0, &done);
		propertyRow ("");
		if (ImGui::Button ("Center")) centrePivot (part);
		ImGui::SetItemTooltip ("The pivot in the middle of the sprite");
		propertyRow ("");
		ImGui::Checkbox ("Lock sprite position", &keepPivotInPlace);
		ImGui::SetItemTooltip ("Moving the pivot leaves the sprite, and the parts attached to it, where they are");
		if (changed) rig->setPivot (id, px, py, keepPivotInPlace, FIELD_KEY);
		if (done) rig->commands ().stopMerging ();
		ImGui::EndTable ();
	}
}

} // namespace devkit
