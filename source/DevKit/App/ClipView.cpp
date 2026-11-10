#include <math.h>
#include <stdio.h>

#include <algorithm>

#include "imgui.h"
#include "imgui_stdlib.h"
#include "renderer.h"

#include "animedit.h"
#include "draw2d.h"
#include "draw2d_gfx.h"

#include "ClipView.h"
#include "imgui_impl_gfx.h"
#include "Platform.h"
#include "Properties.h"
#include "RigDocument.h"
#include "WorkspaceActions.h"

namespace devkit {

namespace {

const double NOTICE_SECONDS = 5.0;
// How long an event's marker flashes as playback passes it.
const double FLASH_SECONDS = 0.3;
// The merge keys: a drag on the canvas, the fields, a drag of an event.
const int DRAG_KEY = 1, FIELD_KEY = 2, EVENT_KEY = 3;
const float RING_POINTS = 40, GRAB_POINTS = 8;
const double pi = 3.14159265358979323846;

// The channels in the order of the panel.
const Channel channelOrder[] = { CHANNEL_X, CHANNEL_Y, CHANNEL_ROTATION, CHANNEL_SCALE, CHANNEL_SPRITE,
	CHANNEL_VISIBLE, CHANNEL_DEPTH, CHANNEL_FLIP };

const char * channelLabel (Channel c) {
	switch (c) {
		case CHANNEL_X: return "Position X";
		case CHANNEL_Y: return "Position Y";
		case CHANNEL_ROTATION: return "Turn";
		case CHANNEL_SCALE: return "Scale";
		case CHANNEL_SPRITE: return "Sprite";
		case CHANNEL_VISIBLE: return "Visible";
		case CHANNEL_DEPTH: return "Depth";
		case CHANNEL_FLIP: return "Flip";
		default: return "?";
	}
}

const char * easingLabels[] = { "Step: hold until the next key", "Linear: at a steady speed", "In and out: slow, fast, slow" };

std::string partLabel (const std::string & part) {
	return part.empty () ? std::string ("Character") : part;
}

// The colours of the timeline.
const ImU32 KEYING = IM_COL32 (255, 80, 80, 255);
const ImU32 SOUND = IM_COL32 (255, 170, 60, 255);
const ImU32 FUNCTION = IM_COL32 (190, 120, 255, 255);

ImU32 easingColour (Easing e, bool selected) {
	ImU32 c = e == EASING_STEP ? IM_COL32 (190, 190, 190, 255) : e == EASING_INOUT ? IM_COL32 (110, 220, 140, 255) : IM_COL32 (100, 170, 255, 255);
	return selected ? IM_COL32 (255, 230, 90, 255) : c;
}

float distance (float x1, float y1, float x2, float y2) {
	return sqrtf ((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
}

Transform2D linear (Transform2D t) {
	t.tx = t.ty = 0;
	return t;
}

float wrapDegrees (float d) {
	d = fmodf (d, 360.0f);
	if (d >= 180.0f) d -= 360.0f;
	if (d < -180.0f) d += 360.0f;
	return d;
}

bool loadAsset (const std::string & path, animformat::Asset & asset) {
	FILE * f = fopen (path.c_str (), "rb");
	if (! f) return false;
	animformat::Bytes bytes;
	unsigned char buffer[4096];
	size_t n;
	while ((n = fread (buffer, 1, sizeof (buffer), f)) > 0) bytes.insert (bytes.end (), buffer, buffer + n);
	fclose (f);
	std::string why;
	return animformat::decodeAsset (bytes.data (), bytes.size (), asset, why);
}

}

ClipView::ClipView (ClipDocument * c, WorkspaceActions & a, const CanvasGraphics & g)
	: clip (c), actions (a), graphics (g), canvas (g), preview (g.draw), lastTime (-1), haveRig (false),
	  haveSecond (false), secondBlend (LAYER_BLEND_OVERRIDE), onlyAnimated (true), selectedEvent (0), selectedPart (0),
	  havePart (false), currentTrack (0), timelineDrag (TL_NONE), dragStartX (0), dragStartY (0), dragTicks (0),
	  dragCopy (false), dragEventTime (0), tickWidth (1), laneLeft (0), drag (DRAG_NONE), dragMoved (false), dragX (0), dragY (0),
	  pendingLength (0), lengthDialog (false), functionDialog (false), pending (new Pending), noticeUntil (0) {
}

ClipView::~ClipView () {
}

bool ClipView::animating () const {
	return player.playing () || drag != DRAG_NONE || timelineDrag != TL_NONE || ! flashes.empty () || canvas.animating ();
}

void ClipView::say (const std::string & text) {
	notice = text;
	noticeUntil = ImGui::GetTime () + NOTICE_SECONDS;
}

std::string ClipView::baseFolder () const {
	std::string project = actions.projectFile ();
	return project.empty () ? folderOf (clip->path ()) : folderOf (project);
}

int ClipView::playhead () const {
	int t = (int) floor (player.ticks () + 0.5);
	return t < 0 ? 0 : t > clip->clip ().length ? clip->clip ().length : t;
}

void ClipView::scrub (int tick) {
	player.pause ();
	player.scrub (tick < 0 ? 0 : tick > clip->clip ().length ? clip->clip ().length : tick);
}

bool ClipView::findRig () {
	const animformat::Asset & a = clip->asset ();
	if (a.rigId.isNull ()) {
		haveRig = false;
		rigFile.clear ();
		return false;
	}
	std::string full = baseFolder () + a.rigPath;
	// Live from the character creator, if the rig is open there.
	RigDocument * open = dynamic_cast<RigDocument *> (actions.findDocument (full));
	if (open && open->asset ().id == a.rigId) {
		rigAsset = open->asset ();
		haveRig = true;
		rigFile = full;
		return true;
	}
	if (full != rigFile) {
		rigFile = full;
		animformat::Asset loaded;
		haveRig = loadAsset (full, loaded) && loaded.kind == animformat::Asset::RIG && loaded.id == a.rigId;
		if (haveRig) rigAsset = loaded;
	}
	return haveRig;
}

void ClipView::findSecond () {
	if (secondFile.empty ()) {
		haveSecond = false;
		return;
	}
	ClipDocument * open = dynamic_cast<ClipDocument *> (actions.findDocument (secondFile));
	if (open) {
		secondAsset = open->asset ();
		haveSecond = true;
	} else if (! haveSecond) {
		haveSecond = loadAsset (secondFile, secondAsset) && secondAsset.kind == animformat::Asset::CLIP;
		if (! haveSecond) secondFile.clear ();
	}
}

void ClipView::pose () {
	std::string base = baseFolder ();
	findRig ();
	findSecond ();
	posedRig = haveRig ? preview.rig (rigAsset, base) : RigDefinitionRef (std::make_shared<RigDefinition> ());
	posedClip = preview.clip (clip->asset (), base);
	posedSecond = haveSecond ? preview.clip (secondAsset, base) : ClipDefinitionRef ();
	player.setClip (0, posedClip, LAYER_BLEND_OVERRIDE);
	player.setClip (1, posedSecond, secondBlend);
	std::vector<LayerSamples> layers;
	std::vector<TrackSamples> samples;
	player.layers (layers, samples, bindings);
	evaluatePose (posedRig, layers, std::vector<PartOverride> (), Transform2D (), shown);
	// The edited clip alone: what keying starts from.
	TrackSamples own;
	evaluateClip (* posedClip, player.time (0), own);
	evaluatePose (posedRig, posedClip, own, Transform2D (), keyBinding, keyed);
}

bool ClipView::partShown (int part) const {
	if (! onlyAnimated) return true;
	const RigDefinition & r = rigAsset.rig;
	const ClipDefinition & c = clip->clip ();
	for (size_t t = 0; t < c.tracks.size (); t ++) {
		int p = r.findPart (c.tracks[t].part);
		if (p >= 0 && partIsUnder (r, p, part)) return true;
	}
	return false;
}

void ClipView::addRows (int part, int depth) {
	const RigDefinition & r = rigAsset.rig;
	if (! partShown (part)) return;
	const std::string & name = r.parts[part].name;
	Row header = { false, name, 0, CHANNEL_X, depth, false };
	rows.push_back (header);
	if (collapsed.count (name)) return;
	for (size_t i = 0; i < sizeof (channelOrder) / sizeof (channelOrder[0]); i ++) {
		const Track * t = clip->track (name, channelOrder[i]);
		if (! t) continue;
		Row row = { true, name, t->id, t->channel, depth + 1, false };
		rows.push_back (row);
	}
	std::vector<int> children = partChildren (r, part);
	for (size_t i = 0; i < children.size (); i ++) addRows (children[i], depth + 1);
}

void ClipView::buildRows () {
	rows.clear ();
	std::vector<std::string> missing;
	if (haveRig) {
		addRows (0, 0);
		missing = clip->missingParts (rigAsset.rig);
	} else {
		std::set<std::string> parts;
		for (size_t t = 0; t < clip->clip ().tracks.size (); t ++) parts.insert (clip->clip ().tracks[t].part);
		missing.assign (parts.begin (), parts.end ());
	}
	// The tracks of parts that the rig hasn't, at the end, so that none is
	// hidden.
	for (size_t m = 0; m < missing.size (); m ++) {
		Row header = { false, missing[m], 0, CHANNEL_X, 0, haveRig };
		rows.push_back (header);
		if (collapsed.count (missing[m])) continue;
		for (size_t i = 0; i < sizeof (channelOrder) / sizeof (channelOrder[0]); i ++) {
			const Track * t = clip->track (missing[m], channelOrder[i]);
			if (! t) continue;
			Row row = { true, missing[m], t->id, t->channel, 1, haveRig };
			rows.push_back (row);
		}
	}
}

void ClipView::selectOnly (const KeyRef & key) {
	selectedKeys.clear ();
	selectedKeys.insert (key);
	selectedEvent = 0;
	currentTrack = key.track;
}

void ClipView::removeSelection () {
	if (selectedEvent) {
		clip->removeEvent (selectedEvent);
		selectedEvent = 0;
	} else if (! selectedKeys.empty ()) {
		clip->removeKeys (std::vector<KeyRef> (selectedKeys.begin (), selectedKeys.end ()));
		selectedKeys.clear ();
	}
}

void ClipView::requestLength (int length) {
	if (length < 0) length = 0;
	if (length == clip->clip ().length) return;
	std::string why;
	if (clip->itemsPast (length)) {
		pendingLength = length;
		lengthDialog = true;
	} else if (! clip->setLength (length, false, why)) {
		say (why);
	}
}

void ClipView::addKeyAtPlayhead (TrackId id) {
	const Track * t = clip->track (id);
	if (! t) return;
	int tick = playhead ();
	std::string why;
	bool ok = true;
	if (t->channel == CHANNEL_SPRITE) {
		const SpriteRef * s = sampleSprite (clip->clip (), t->part, ticksToTime (tick));
		if (s && s->kind == SpriteRef::SPRITE) ok = clip->setSpriteKey (id, tick, clip->bankOf (* s), s->number, s->mirror, false, why);
		else ok = clip->setSpriteKey (id, tick, "", 0, false, ! s || s->kind == SpriteRef::DEFAULT, why);
	} else {
		// What the part has there now, as the clip poses it.
		int p = posedRig ? posedRig->findPart (t->part) : -1;
		float value = 0;
		if (p >= 0 && p < (int) keyed.parts.size ()) {
			const PosedPart & part = keyed.parts[p];
			switch (t->channel) {
				case CHANNEL_X: value = part.local.x; break;
				case CHANNEL_Y: value = part.local.y; break;
				case CHANNEL_ROTATION: value = part.local.rotation; break;
				case CHANNEL_SCALE:
				case CHANNEL_SCALE_X: value = part.local.scaleX; break;
				case CHANNEL_SCALE_Y: value = part.local.scaleY; break;
				case CHANNEL_VISIBLE: value = part.local.visible ? 1 : 0; break;
				case CHANNEL_DEPTH: value = (float) part.depth; break;
				case CHANNEL_FLIP: value = part.local.flip ? 1 : 0; break;
				default: break;
			}
		} else if (isScale (t->channel) || t->channel == CHANNEL_VISIBLE) {
			value = 1;
		}
		ok = clip->autoKey (t->part, t->channel, tick, value, why);
	}
	if (! ok) say (why);
	else selectOnly (KeyRef (id, tick));
}

void ClipView::askForFile (Pending::What what) {
	if (pending->waiting) return;
	std::string base = baseFolder ();
	if (base.empty ()) {
		say ("Save the clip, or open its project, first: paths are from that folder.");
		return;
	}
	pending->waiting = true;
	pending->answered = false;
	pending->what = what;
	std::shared_ptr<Pending> p = pending;
	std::vector<FileFilter> filters;
	FileFilter sounds = { "Sounds", "wav;ogg;xm;mod;s3m;it" };
	FileFilter banks = { "Sprite banks", "duc" };
	filters.push_back (what == Pending::SOUND ? sounds : banks);
	actions.platform ().openFileDialog (what == Pending::SOUND ? "Choose the sound of the event" : "Choose the sprite bank of the key",
		filters, base, false, [p] (const std::vector<std::string> & files) {
			p->files = files;
			p->answered = true;
		});
}

void ClipView::handlePending () {
	if (! pending->waiting || ! pending->answered) return;
	pending->waiting = false;
	if (pending->files.empty ()) return;
	std::string path = relativePathFrom (baseFolder (), pending->files[0]);
	std::string why;
	if (pending->what == Pending::SOUND) {
		if (selectedEvent && clip->event (selectedEvent) && clip->event (selectedEvent)->what.kind == FrameEvent::SOUND) {
			if (! clip->setEvent (selectedEvent, clip->event (selectedEvent)->time, FrameEvent::SOUND, path, why)) say (why);
		} else {
			EventId added;
			if (clip->addEvent (playhead (), FrameEvent::SOUND, path, &added, why)) {
				selectedEvent = added;
				selectedKeys.clear ();
			} else {
				say (why);
			}
		}
	} else if (selectedKeys.size () == 1) {
		KeyRef k = * selectedKeys.begin ();
		const Key * key = clip->key (k);
		int number = key && key->sprite.kind == SpriteRef::SPRITE ? key->sprite.number : 0;
		if (! clip->setSpriteKey (k.track, k.time, path, number, false, false, why)) say (why);
	}
}

bool ClipView::takeFile (const std::string & path) {
	if (extensionOf (path) != "duc" || ! havePart) return false;
	int p = posedRig ? partWithId (* posedRig, selectedPart) : -1;
	if (p < 0) return false;
	std::string part = posedRig->parts[p].name;
	std::string base = baseFolder ();
	if (base.empty ()) {
		say ("Save the clip, or open its project, first: paths are from that folder.");
		return true;
	}
	std::string why;
	const Track * t = clip->track (part, CHANNEL_SPRITE);
	TrackId id = t ? t->id : 0;
	if (! t && ! clip->addTrack (part, CHANNEL_SPRITE, &id, why)) {
		say (why);
		return true;
	}
	int tick = playhead ();
	if (clip->setSpriteKey (id, tick, relativePathFrom (base, path), 0, false, false, why)) selectOnly (KeyRef (id, tick));
	else say (why);
	return true;
}

void ClipView::drawToolbar () {
	std::string base = baseFolder ();
	ImGui::AlignTextToFramePadding ();
	ImGui::TextUnformatted ("Rig");
	ImGui::SameLine ();
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 10);
	std::string current = clip->asset ().rigId.isNull () ? std::string ("None") : clip->asset ().rigPath;
	if (ImGui::BeginCombo ("##rig", current.c_str ())) {
		std::vector<std::string> rigs = base.empty () ? std::vector<std::string> () : filesWithExtension (base, "rig");
		if (rigs.empty ()) ImGui::TextDisabled ("No rigs in the folder of the project");
		for (size_t i = 0; i < rigs.size (); i ++) {
			std::string path = relativePathFrom (base, rigs[i]);
			if (ImGui::Selectable (path.c_str (), path == clip->asset ().rigPath)) {
				animformat::Asset a;
				if (loadAsset (rigs[i], a) && a.kind == animformat::Asset::RIG) {
					clip->bindToRig (a.id, path);
					rigFile.clear ();
				} else {
					say ("That isn't a rig that can be read.");
				}
			}
		}
		ImGui::EndCombo ();
	}
	ImGui::SetItemTooltip ("The rig that the clip is made for, and that the preview shows");
	ImGui::SameLine ();
	if (ImGui::Button (player.playing () ? "Pause" : "Play")) {
		if (player.playing ()) player.pause ();
		else player.play ();
	}
	ImGui::SameLine ();
	if (ImGui::Button ("Restart")) {
		player.reset ();
		player.play ();
	}
	ImGui::SameLine ();
	bool loop = clip->clip ().loop;
	if (ImGui::Checkbox ("Loop", &loop)) clip->setLoop (loop);
	ImGui::SameLine ();
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 5);
	int speed = player.speed ();
	if (ImGui::SliderInt ("##speed", &speed, 10, 400, "%d%%")) player.setSpeed (speed);
	ImGui::SetItemTooltip ("How fast the preview plays, of the game's speed: the keys stay where they are");

	ImGui::AlignTextToFramePadding ();
	ImGui::TextUnformatted ("Over it");
	ImGui::SameLine ();
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 9);
	std::string second = secondFile.empty () ? std::string ("No second clip") : relativePathFrom (base, secondFile);
	if (ImGui::BeginCombo ("##second", second.c_str ())) {
		if (ImGui::Selectable ("No second clip", secondFile.empty ())) {
			secondFile.clear ();
			haveSecond = false;
			player.reset ();
		}
		std::vector<std::string> clips = base.empty () ? std::vector<std::string> () : filesWithExtension (base, "clp");
		for (size_t i = 0; i < clips.size (); i ++) {
			if (clips[i] == clip->path ()) continue;
			std::string path = relativePathFrom (base, clips[i]);
			if (ImGui::Selectable (path.c_str (), clips[i] == secondFile)) {
				secondFile = clips[i];
				haveSecond = false;
				player.reset ();
			}
		}
		ImGui::EndCombo ();
	}
	ImGui::SetItemTooltip ("A clip to play over this one on a layer, as a character in a game would");
	ImGui::SameLine ();
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 6);
	int blend = secondBlend;
	const char * blends[] = { "Override", "Additive" };
	if (ImGui::Combo ("##blend", &blend, blends, 2)) {
		secondBlend = (LayerBlend) blend;
		player.reset ();
	}
	ImGui::SetItemTooltip ("Override: the second clip sets what it keys. Additive: it adds to places and turns, and multiplies scales");
	ImGui::SameLine (0, ImGui::GetFontSize ());
	canvas.drawZoomButtons ();
}

void ClipView::handleCanvasMouse () {
	float mx = canvas.mouseX (), my = canvas.mouseY ();
	if (canvas.pressed ()) {
		player.pause ();
		drag = DRAG_NONE;
		float grab = canvas.pixelsOfPoints (GRAB_POINTS);
		int at = havePart && posedRig ? partWithId (* posedRig, selectedPart) : -1;
		if (at >= 0) {
			PartHandles h = partHandles (shown, at, canvas.pixelsOfPoints (RING_POINTS));
			if (distance (mx, my, h.turnX, h.turnY) < grab) drag = DRAG_TURN;
			for (int i = 0; i < 4 && drag == DRAG_NONE; i ++) {
				if (distance (mx, my, h.scale[i].x, h.scale[i].y) < grab) {
					drag = DRAG_SCALE;
					dragHandle = h.scale[i];
				}
			}
			if (drag == DRAG_NONE && distance (mx, my, h.originX, h.originY) < grab) drag = DRAG_MOVE;
		}
		if (drag == DRAG_NONE) {
			int hit = partAt (shown, mx, my);
			if (hit >= 0) {
				selectedPart = posedRig->parts[hit].id;
				havePart = true;
				drag = DRAG_MOVE;
			} else {
				havePart = false;
			}
		}
		at = havePart && posedRig ? partWithId (* posedRig, selectedPart) : -1;
		if (drag != DRAG_NONE && at >= 0 && at < (int) keyed.parts.size ()) {
			int parent = posedRig->parts[at].parent;
			dragMoved = false;
			dragX = mx;
			dragY = my;
			dragLocal = keyed.parts[at].local;
			dragParent = parent >= 0 ? shown.parts[parent].world : Transform2D ();
			dragWorld = shown.parts[at].world;
		} else {
			drag = DRAG_NONE;
		}
	}

	int at = havePart && posedRig ? partWithId (* posedRig, selectedPart) : -1;
	if (drag != DRAG_NONE && canvas.held () && at >= 0 && ! dragMoved && distance (mx, my, dragX, dragY) >= canvas.pixelsOfPoints (2)) dragMoved = true;
	if (drag != DRAG_NONE && dragMoved && canvas.held () && at >= 0) {
		std::string part = posedRig->parts[at].name;
		int tick = playhead ();
		char where[96];
		snprintf (where, sizeof (where), " at tick %d", tick);
		std::string why;
		bool ok = true;
		if (drag == DRAG_MOVE) {
			Transform2D up;
			if (linear (dragParent).inverse (up)) {
				float lx, ly;
				up.apply (mx - dragX, my - dragY, lx, ly);
				ok = clip->autoKey (part, CHANNEL_X, tick, floorf (dragLocal.x + lx + 0.5f), why, DRAG_KEY)
					&& clip->autoKey (part, CHANNEL_Y, tick, floorf (dragLocal.y + ly + 0.5f), why, DRAG_KEY);
			}
			keying = "Keying " + partLabel (part) + " > position" + where;
		} else if (drag == DRAG_TURN) {
			float ox = dragWorld.tx, oy = dragWorld.ty;
			double turned = (atan2 (my - oy, mx - ox) - atan2 (dragY - oy, dragX - ox)) * 180.0 / pi;
			if (dragParent.mirrors ()) turned = - turned;
			ok = clip->autoKey (part, CHANNEL_ROTATION, tick, wrapDegrees (floorf (dragLocal.rotation + (float) turned + 0.5f)), why, DRAG_KEY);
			keying = "Keying " + partLabel (part) + " > turn" + where;
		} else if (drag == DRAG_SCALE) {
			float sx, sy;
			scaleFromHandle (dragParent, dragLocal, dragHandle, true, dragHandle.x + mx - dragX, dragHandle.y + my - dragY, sx, sy);
			ok = clip->autoKey (part, CHANNEL_SCALE, tick, sx, why, DRAG_KEY);
			keying = "Keying " + partLabel (part) + " > scale" + where;
		}
		if (! ok) say (why);
	}
	if (drag != DRAG_NONE && canvas.released ()) {
		drag = DRAG_NONE;
		keying.clear ();
		clip->commands ().stopMerging ();
	}
}

void ClipView::drawCanvas () {
	ImVec2 size = ImGui::GetContentRegionAvail ();
	size.y = floorf (size.y * 0.55f);
	float x1, y1, x2, y2;
	if (! poseBounds (shown, x1, y1, x2, y2)) {
		x1 = y1 = -100;
		x2 = y2 = 100;
	}
	float margin = ((x2 - x1) > (y2 - y1) ? (x2 - x1) : (y2 - y1)) * 0.1f + 10;
	canvas.setPictureBounds ((int) floorf (x1 - margin), (int) floorf (y1 - margin),
		(int) ceilf (x2 - x1 + margin * 2), (int) ceilf (y2 - y1 + margin * 2));
	const Pose & p = shown;
	const RigDefinition & r = * posedRig;
	int at = havePart ? partWithId (r, selectedPart) : -1;
	float pixel = canvas.pixelsOfPoints (1);
	canvas.draw (size, [&p, &r, at, pixel] (Draw2D & draw) {
		draw.clear ();
		drawPose (draw, p);
		drawPoseOverlay (draw, r, p, at, pixel);
		draw.finish ();
	});
	// Whatever a drag on the canvas does, it keys: the frame says so.
	ImGui::GetWindowDrawList ()->AddRect (ImGui::GetItemRectMin (), ImGui::GetItemRectMax (),
		drag != DRAG_NONE ? KEYING : IM_COL32 (255, 80, 80, 90), 0.0f, drag != DRAG_NONE ? 2.0f : 1.0f);
	handleCanvasMouse ();

	char tick[48];
	snprintf (tick, sizeof (tick), "Tick %d of %d", playhead (), clip->clip ().length);
	ImGui::TextDisabled ("%s    %s", tick, canvas.status ().c_str ());
	if (! keying.empty ()) {
		ImGui::SameLine (0, ImGui::GetFontSize () * 2);
		ImGui::TextColored (ImVec4 (1.0f, 0.35f, 0.35f, 1.0f), "%s", keying.c_str ());
	} else if (! haveRig) {
		ImGui::SameLine (0, ImGui::GetFontSize () * 2);
		ImGui::TextColored (ImVec4 (1.0f, 0.8f, 0.3f, 1.0f), "%s", clip->asset ().rigId.isNull ()
			? "Choose the rig that the clip is made for." : "The rig of the clip isn't there: choose it again.");
	} else if (! notice.empty () && ImGui::GetTime () < noticeUntil) {
		ImGui::SameLine (0, ImGui::GetFontSize () * 2);
		ImGui::TextColored (ImVec4 (1.0f, 0.8f, 0.3f, 1.0f), "%s", notice.c_str ());
	}
}

void ClipView::drawTimeline () {
	ImGui::BeginChild ("timeline", ImVec2 (0, 0), ImGuiChildFlags_Borders);
	ImDrawList * list = ImGui::GetWindowDrawList ();
	ImGuiIO & io = ImGui::GetIO ();
	ImVec2 origin = ImGui::GetCursorScreenPos ();
	float h = ImGui::GetFrameHeight ();
	float labelWidth = ImGui::GetFontSize () * 14;
	float width = ImGui::GetContentRegionAvail ().x;
	int length = clip->clip ().length;
	laneLeft = origin.x + labelWidth + 8;
	float laneRight = origin.x + width - 12;
	tickWidth = (laneRight - laneLeft) / (float) (length > 0 ? length : 1);
	float rowsTop = origin.y + h * 2;
	float bottom = rowsTop + h * rows.size ();
	ImU32 text = ImGui::GetColorU32 (ImGuiCol_Text), faint = ImGui::GetColorU32 (ImGuiCol_TextDisabled);

	// The left column: the options, "Events", and the rows.
	ImGui::SetCursorScreenPos (ImVec2 (origin.x, origin.y));
	ImGui::Checkbox ("Only animated parts", &onlyAnimated);
	ImGui::SetItemTooltip ("Hide the parts that have no tracks, keeping the parts they hang from");
	ImGui::SetCursorScreenPos (ImVec2 (origin.x, origin.y + h));
	ImGui::AlignTextToFramePadding ();
	ImGui::TextUnformatted ("Events");
	ImGui::SameLine ();
	if (ImGui::SmallButton ("+ Sound")) askForFile (Pending::SOUND);
	ImGui::SameLine ();
	if (ImGui::SmallButton ("+ Function")) {
		functionName.clear ();
		functionDialog = true;
	}
	for (size_t r = 0; r < rows.size (); r ++) {
		const Row & row = rows[r];
		float y = rowsTop + h * r;
		ImGui::PushID ((int) r);
		ImGui::SetCursorScreenPos (ImVec2 (origin.x + row.depth * ImGui::GetFontSize (), y));
		if (row.isTrack) {
			bool current = row.track == currentTrack;
			std::string label = std::string (channelLabel (row.channel));
			if (ImGui::Selectable (label.c_str (), current, 0, ImVec2 (labelWidth - row.depth * ImGui::GetFontSize () - h * 1.2f, h))) currentTrack = row.track;
			ImGui::SameLine ();
			if (ImGui::SmallButton ("x")) {
				clip->removeTrack (row.track);
				selectedKeys.clear ();
			}
			ImGui::SetItemTooltip ("Remove the track, and its keys");
		} else {
			bool open = ! collapsed.count (row.part);
			std::string label = std::string (open ? "v  " : ">  ") + partLabel (row.part);
			if (row.missing) label += "  (not in the rig)";
			if (row.missing) ImGui::PushStyleColor (ImGuiCol_Text, ImVec4 (1.0f, 0.6f, 0.3f, 1.0f));
			if (ImGui::Selectable (label.c_str (), false, 0, ImVec2 (labelWidth - row.depth * ImGui::GetFontSize () - h * 1.2f, h))) {
				if (open) collapsed.insert (row.part);
				else collapsed.erase (row.part);
			}
			if (row.missing) ImGui::PopStyleColor ();
			ImGui::SameLine ();
			if (ImGui::SmallButton ("+")) ImGui::OpenPopup ("add");
			ImGui::SetItemTooltip ("Add a track to the part");
			if (ImGui::BeginPopup ("add")) {
				const char * kinds[] = { "Position", "Turn", "Scale", "Sprite", "Visible", "Depth", "Flip" };
				const Channel first[] = { CHANNEL_X, CHANNEL_ROTATION, CHANNEL_SCALE, CHANNEL_SPRITE, CHANNEL_VISIBLE, CHANNEL_DEPTH, CHANNEL_FLIP };
				for (int k = 0; k < 7; k ++) {
					bool has = clip->track (row.part, first[k]) != NULL;
					if (ImGui::MenuItem (kinds[k], NULL, false, ! has)) {
						std::string why;
						TrackId added;
						if (! clip->addTrack (row.part, first[k], &added, why)) say (why);
						else currentTrack = added;
						if (first[k] == CHANNEL_X && ! clip->track (row.part, CHANNEL_Y)) clip->addTrack (row.part, CHANNEL_Y, NULL, why);
						collapsed.erase (row.part);
					}
				}
				ImGui::EndPopup ();
			}
		}
		ImGui::PopID ();
	}

	// The lanes, and the mouse on them.
	ImGui::SetCursorScreenPos (ImVec2 (laneLeft - 8, origin.y));
	ImGui::InvisibleButton ("##lanes", ImVec2 (laneRight - laneLeft + 20, (bottom > origin.y + h * 2 ? bottom : origin.y + h * 2) - origin.y));
	bool hovered = ImGui::IsItemHovered ();
	float mx = io.MousePos.x, my = io.MousePos.y;
	auto xOf = [this] (double t) { return laneLeft + (float) t * tickWidth; };
	auto tickAt = [this, length] (float x) {
		int t = (int) floorf ((x - laneLeft) / tickWidth + 0.5f);
		return t < 0 ? 0 : t > length ? length : t;
	};
	// The key under the mouse, if any.
	KeyRef hitKey;
	bool hit = false;
	for (size_t r = 0; r < rows.size () && ! hit; r ++) {
		if (! rows[r].isTrack) continue;
		float cy = rowsTop + h * r + h * 0.5f;
		if (fabsf (my - cy) > h * 0.5f) continue;
		const Track * t = clip->track (rows[r].track);
		for (size_t k = 0; t && k < t->keys.size (); k ++) {
			if (fabsf (mx - xOf (t->keys[k].time)) <= 5) {
				hitKey = KeyRef (t->id, t->keys[k].time);
				hit = true;
				break;
			}
		}
	}
	EventId hitEvent = 0;
	if (fabsf (my - (origin.y + h * 1.5f)) <= h * 0.5f) {
		for (size_t e = 0; e < clip->clip ().events.size (); e ++) {
			if (fabsf (mx - xOf (clip->clip ().events[e].time)) <= 6) hitEvent = clip->clip ().events[e].id;
		}
	}

	if (ImGui::IsItemActivated ()) {
		dragStartX = mx;
		dragStartY = my;
		dragTicks = 0;
		if (my < origin.y + h) {
			if (fabsf (mx - xOf (length)) <= 6) {
				timelineDrag = TL_LENGTH;
				dragTicks = length;
			} else {
				timelineDrag = TL_SCRUB;
				scrub (tickAt (mx));
			}
		} else if (my < rowsTop) {
			if (hitEvent) {
				selectedEvent = hitEvent;
				selectedKeys.clear ();
				timelineDrag = TL_EVENT;
				dragEventTime = clip->event (hitEvent)->time;
			} else {
				selectedEvent = 0;
				timelineDrag = TL_NONE;
			}
		} else if (hit) {
			if (io.KeyShift) {
				if (selectedKeys.count (hitKey)) selectedKeys.erase (hitKey);
				else selectedKeys.insert (hitKey);
				selectedEvent = 0;
				currentTrack = hitKey.track;
			} else if (! selectedKeys.count (hitKey)) {
				selectOnly (hitKey);
			}
			timelineDrag = selectedKeys.count (hitKey) ? TL_KEYS : TL_NONE;
			dragCopy = io.KeyAlt;
		} else {
			if (! io.KeyShift) selectedKeys.clear ();
			selectedEvent = 0;
			timelineDrag = TL_BOX;
		}
	}
	if (ImGui::IsItemActive ()) {
		if (timelineDrag == TL_SCRUB) scrub (tickAt (mx));
		else if (timelineDrag == TL_LENGTH) {
			int t = (int) floorf ((mx - laneLeft) / tickWidth + 0.5f);
			dragTicks = t < 0 ? 0 : t > 100000 ? 100000 : t;
		} else if (timelineDrag == TL_KEYS) {
			dragTicks = (int) floorf ((mx - dragStartX) / tickWidth + 0.5f);
		} else if (timelineDrag == TL_EVENT) {
			int t = dragEventTime + (int) floorf ((mx - dragStartX) / tickWidth + 0.5f);
			t = t < 0 ? 0 : t > length ? length : t;
			const ClipEvent * e = clip->event (selectedEvent);
			std::string why;
			if (e && e->time != t) clip->setEvent (selectedEvent, t, e->what.kind, clip->eventName (selectedEvent), why, EVENT_KEY);
		}
	}
	if (ImGui::IsItemDeactivated ()) {
		std::string why;
		if (timelineDrag == TL_LENGTH) {
			requestLength (dragTicks);
		} else if (timelineDrag == TL_KEYS && dragTicks != 0) {
			std::vector<KeyRef> keys (selectedKeys.begin (), selectedKeys.end ());
			if (clip->moveKeys (keys, dragTicks, dragCopy, why)) {
				std::set<KeyRef> moved;
				for (size_t i = 0; i < keys.size (); i ++) moved.insert (KeyRef (keys[i].track, keys[i].time + dragTicks));
				selectedKeys = moved;
			} else {
				say (why);
			}
		} else if (timelineDrag == TL_BOX) {
			float bx1 = std::min (dragStartX, mx), bx2 = std::max (dragStartX, mx);
			float by1 = std::min (dragStartY, my), by2 = std::max (dragStartY, my);
			for (size_t r = 0; r < rows.size (); r ++) {
				if (! rows[r].isTrack) continue;
				float cy = rowsTop + h * r + h * 0.5f;
				if (cy < by1 || cy > by2) continue;
				const Track * t = clip->track (rows[r].track);
				for (size_t k = 0; t && k < t->keys.size (); k ++) {
					float kx = xOf (t->keys[k].time);
					if (kx >= bx1 && kx <= bx2) selectedKeys.insert (KeyRef (t->id, t->keys[k].time));
				}
			}
		} else if (timelineDrag == TL_EVENT) {
			clip->commands ().stopMerging ();
		}
		timelineDrag = TL_NONE;
	}

	// The ruler: ticks, numbers a readable distance apart, and the length.
	list->AddRectFilled (ImVec2 (laneLeft - 8, origin.y), ImVec2 (laneRight + 12, origin.y + h), ImGui::GetColorU32 (ImGuiCol_FrameBg));
	int every = 1;
	const int steps[] = { 1, 2, 4, 8, 12, 24, 48, 96, 192, 480, 960 };
	for (size_t i = 0; i < sizeof (steps) / sizeof (steps[0]); i ++) {
		every = steps[i];
		if (every * tickWidth >= ImGui::GetFontSize () * 3) break;
	}
	for (int t = 0; t <= length; t += every) {
		float x = xOf (t);
		list->AddLine (ImVec2 (x, origin.y + h * 0.6f), ImVec2 (x, origin.y + h), faint);
		list->AddLine (ImVec2 (x, rowsTop), ImVec2 (x, bottom), IM_COL32 (255, 255, 255, 18));
		char number[16];
		snprintf (number, sizeof (number), "%d", t);
		list->AddText (ImVec2 (x + 2, origin.y), faint, number);
	}
	int shownLength = timelineDrag == TL_LENGTH ? dragTicks : length;
	float lx = timelineDrag == TL_LENGTH ? laneLeft + dragTicks * tickWidth : xOf (length);
	list->AddTriangleFilled (ImVec2 (lx - 6, origin.y), ImVec2 (lx + 6, origin.y), ImVec2 (lx, origin.y + h * 0.6f), text);
	if (timelineDrag == TL_LENGTH) {
		char tip[48];
		snprintf (tip, sizeof (tip), "Length: %d ticks", shownLength);
		ImGui::SetTooltip ("%s", tip);
	} else if (hovered && my < origin.y + h && fabsf (mx - xOf (length)) <= 6) {
		ImGui::SetTooltip ("Drag to change the length of the clip");
	}

	// The events: sounds as round markers, functions as diamonds.
	double now = ImGui::GetTime ();
	for (size_t e = 0; e < clip->clip ().events.size (); e ++) {
		const ClipEvent & ev = clip->clip ().events[e];
		float x = xOf (ev.time), cy = origin.y + h * 1.5f, r = h * 0.3f;
		bool flashing = false;
		for (size_t f = 0; f < flashes.size (); f ++) flashing = flashing || (flashes[f].first == ev.id && flashes[f].second > now);
		ImU32 c = ev.what.kind == FrameEvent::SOUND ? SOUND : FUNCTION;
		if (flashing) r *= 1.6f;
		if (ev.what.kind == FrameEvent::SOUND) list->AddCircleFilled (ImVec2 (x, cy), r, c);
		else list->AddQuadFilled (ImVec2 (x, cy - r), ImVec2 (x + r, cy), ImVec2 (x, cy + r), ImVec2 (x - r, cy), c);
		if (ev.id == selectedEvent) list->AddCircle (ImVec2 (x, cy), r + 3, IM_COL32 (255, 230, 90, 255), 0, 2);
		if (hovered && hitEvent == ev.id) {
			std::string name = clip->eventName (ev.id);
			ImGui::SetTooltip ("%s at tick %d: %s", ev.what.kind == FrameEvent::SOUND ? "Sound" : "Function", ev.time, name.c_str ());
		}
	}

	// The keys: diamonds for continuous channels, squares for discrete ones,
	// coloured by their easing towards the next key.
	for (size_t r = 0; r < rows.size (); r ++) {
		float y = rowsTop + h * r;
		if (r % 2) list->AddRectFilled (ImVec2 (laneLeft - 8, y), ImVec2 (laneRight + 12, y + h), IM_COL32 (255, 255, 255, 8));
		if (! rows[r].isTrack) continue;
		const Track * t = clip->track (rows[r].track);
		if (! t) continue;
		float cy = y + h * 0.5f, s = h * 0.28f;
		for (size_t k = 0; k + 1 < t->keys.size (); k ++) {
			if (isDiscrete (t->channel) || t->keys[k].easing == EASING_STEP) continue;
			list->AddLine (ImVec2 (xOf (t->keys[k].time), cy), ImVec2 (xOf (t->keys[k + 1].time), cy), easingColour (t->keys[k].easing, false) & 0x60FFFFFF, 2);
		}
		for (size_t k = 0; k < t->keys.size (); k ++) {
			bool sel = selectedKeys.count (KeyRef (t->id, t->keys[k].time)) != 0;
			float offset = sel && timelineDrag == TL_KEYS ? dragTicks * tickWidth : 0;
			float x = xOf (t->keys[k].time) + offset;
			ImU32 c = easingColour (t->keys[k].easing, sel);
			if (isDiscrete (t->channel)) list->AddRectFilled (ImVec2 (x - s, cy - s), ImVec2 (x + s, cy + s), c);
			else list->AddQuadFilled (ImVec2 (x, cy - s * 1.3f), ImVec2 (x + s * 1.3f, cy), ImVec2 (x, cy + s * 1.3f), ImVec2 (x - s * 1.3f, cy), c);
			if (sel && timelineDrag == TL_KEYS && dragCopy && offset != 0) {
				float x0 = xOf (t->keys[k].time);
				list->AddQuad (ImVec2 (x0, cy - s * 1.3f), ImVec2 (x0 + s * 1.3f, cy), ImVec2 (x0, cy + s * 1.3f), ImVec2 (x0 - s * 1.3f, cy), c);
			}
		}
	}
	if (timelineDrag == TL_BOX) {
		list->AddRect (ImVec2 (std::min (dragStartX, mx), std::min (dragStartY, my)), ImVec2 (std::max (dragStartX, mx), std::max (dragStartY, my)),
			IM_COL32 (255, 230, 90, 200));
	}
	// The playhead, in the colour of keying: a drag on the canvas keys here.
	float px = xOf (player.ticks ());
	list->AddLine (ImVec2 (px, origin.y), ImVec2 (px, bottom > rowsTop ? bottom : rowsTop), KEYING, 2);

	ImGui::SetCursorScreenPos (origin);
	ImGui::Dummy (ImVec2 (width, (bottom > rowsTop ? bottom : rowsTop) - origin.y + h * 0.5f));
	ImGui::EndChild ();
}

void ClipView::drawLengthDialog () {
	if (lengthDialog) {
		ImGui::OpenPopup ("Shorten the Clip?");
		lengthDialog = false;
	}
	if (ImGui::BeginPopupModal ("Shorten the Clip?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
		int past = clip->itemsPast (pendingLength);
		ImGui::Text ("%d key%s and event%s lie past tick %d, the new end.", past, past == 1 ? "" : "s", past == 1 ? "" : "s", pendingLength);
		ImGui::TextUnformatted ("A clip can't keep keys past its end: shortening it removes them.");
		if (ImGui::Button ("Remove Them")) {
			std::string why;
			if (! clip->setLength (pendingLength, true, why)) say (why);
			selectedKeys.clear ();
			selectedEvent = 0;
			ImGui::CloseCurrentPopup ();
		}
		ImGui::SameLine ();
		if (ImGui::Button ("Cancel")) ImGui::CloseCurrentPopup ();
		ImGui::EndPopup ();
	}
}

void ClipView::drawFunctionDialog () {
	if (functionDialog) {
		ImGui::OpenPopup ("Add a Function Event");
		functionDialog = false;
	}
	if (ImGui::BeginPopup ("Add a Function Event")) {
		ImGui::Text ("At tick %d, call the function:", playhead ());
		if (ImGui::IsWindowAppearing ()) ImGui::SetKeyboardFocusHere ();
		bool enter = ImGui::InputText ("##function", &functionName, ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SetItemTooltip ("A function of the project's scripts, without parameters");
		if (enter || ImGui::Button ("Add")) {
			std::string why;
			EventId added;
			if (clip->addEvent (playhead (), FrameEvent::FUNCTION, functionName, &added, why)) {
				selectedEvent = added;
				selectedKeys.clear ();
				ImGui::CloseCurrentPopup ();
			} else {
				say (why);
			}
		}
		ImGui::EndPopup ();
	}
}

void ClipView::handleKeys () {
	if (! ImGui::IsWindowFocused (ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO ().WantTextInput) return;
	if (ImGui::Shortcut (ImGuiKey_Delete) || ImGui::Shortcut (ImGuiKey_Backspace)) removeSelection ();
	if (ImGui::Shortcut (ImGuiKey_K) && currentTrack) addKeyAtPlayhead (currentTrack);
	if (ImGui::Shortcut (ImGuiKey_LeftArrow, ImGuiInputFlags_Repeat)) scrub (playhead () - 1);
	if (ImGui::Shortcut (ImGuiKey_RightArrow, ImGuiInputFlags_Repeat)) scrub (playhead () + 1);
}

void ClipView::draw () {
	handlePending ();
	double now = ImGui::GetTime ();
	if (lastTime >= 0) {
		std::vector<const ClipEvent *> passed = player.advance ((now - lastTime) * 1000.0, actions.tickMilliseconds ());
		for (size_t i = 0; i < passed.size (); i ++) flashes.push_back (std::make_pair (passed[i]->id, now + FLASH_SECONDS));
	}
	lastTime = now;
	for (size_t i = flashes.size (); i -- > 0; ) {
		if (flashes[i].second <= now) flashes.erase (flashes.begin () + i);
	}
	// Undo may have taken away what was selected.
	for (std::set<KeyRef>::iterator i = selectedKeys.begin (); i != selectedKeys.end (); ) {
		if (! clip->key (* i)) selectedKeys.erase (i ++);
		else ++ i;
	}
	if (selectedEvent && ! clip->event (selectedEvent)) selectedEvent = 0;
	if (currentTrack && ! clip->track (currentTrack)) currentTrack = 0;
	if (player.ticks () > clip->clip ().length && ! player.playing ()) scrub (clip->clip ().length);

	pose ();
	buildRows ();
	drawToolbar ();
	handleKeys ();
	drawCanvas ();
	drawTimeline ();
	drawLengthDialog ();
	drawFunctionDialog ();
}

void ClipView::drawInspector () {
	std::string why;
	if (selectedEvent && clip->event (selectedEvent)) {
		const ClipEvent ev = * clip->event (selectedEvent);
		std::string name = clip->eventName (selectedEvent);
		if (beginProperties ("Event")) {
			propertyRow ("Tick");
			int time = ev.time;
			if (ImGui::InputInt ("##tick", &time)) {
				if (! clip->setEvent (ev.id, time, ev.what.kind, name, why, FIELD_KEY)) say (why);
			}
			if (ImGui::IsItemDeactivated ()) clip->commands ().stopMerging ();
			if (ev.what.kind == FrameEvent::SOUND) {
				propertyRow ("Plays the sound", "A sound file of the project");
				ImGui::TextUnformatted (name.c_str ());
				propertyRow ("");
				if (ImGui::Button ("Choose...")) askForFile (Pending::SOUND);
			} else {
				propertyRow ("Calls the function", "A function of the project's scripts, without parameters");
				std::string edited = name;
				if (ImGui::InputText ("##function", &edited, ImGuiInputTextFlags_EnterReturnsTrue) && edited != name) {
					if (! clip->setEvent (ev.id, ev.time, FrameEvent::FUNCTION, edited, why)) say (why);
				}
			}
			ImGui::EndTable ();
		}
		if (ImGui::Button ("Remove Event")) removeSelection ();
		return;
	}

	if (selectedKeys.size () == 1) {
		KeyRef k = * selectedKeys.begin ();
		const Track * t = clip->track (k.track);
		const Key * key = clip->key (k);
		if (! t || ! key) return;
		const Track track = * t;
		const Key now = * key;
		std::string title = partLabel (track.part) + " > " + channelLabel (track.channel);
		if (beginProperties (title.c_str ())) {
			propertyRow ("Tick", "Where the key is on the timeline");
			int time = now.time;
			if (ImGui::InputInt ("##tick", &time) && time != now.time) {
				if (clip->moveKeys (std::vector<KeyRef> (1, k), time - now.time, false, why)) selectOnly (KeyRef (k.track, time));
				else say (why);
			}
			float value = now.value;
			bool changed = false, done = false;
			switch (track.channel) {
				case CHANNEL_X:
				case CHANNEL_Y:
					propertyRow ("Place", "Where its origin is, from its parent's, in its parent's pixels");
					changed = ImGui::DragFloat ("##value", &value, 1.0f, 0, 0, "%g");
					done = ImGui::IsItemDeactivated ();
					break;
				case CHANNEL_ROTATION:
					propertyRow ("Turn", "In degrees, clockwise");
					changed = ImGui::DragFloat ("##value", &value, 1.0f, -3600, 3600, "%g\xC2\xB0");
					done = ImGui::IsItemDeactivated ();
					break;
				case CHANNEL_SCALE: {
					float percent = value * 100.0f;
					propertyRow ("Scale", "In percent");
					if (ImGui::DragFloat ("##value", &percent, 1.0f, 1, 10000, "%g%%")) {
						value = (percent < 1 ? 1 : percent) / 100.0f;
						changed = true;
					}
					done = ImGui::IsItemDeactivated ();
					break;
				}
				case CHANNEL_VISIBLE:
				case CHANNEL_FLIP: {
					bool on = value != 0;
					propertyRow (track.channel == CHANNEL_VISIBLE ? "Visible" : "Flipped");
					if (ImGui::Checkbox ("##value", &on)) {
						value = on ? 1.0f : 0.0f;
						changed = true;
					}
					break;
				}
				case CHANNEL_DEPTH: {
					int depth = (int) lrintf (value);
					propertyRow ("Depth", "Higher is in front of the character's other parts");
					if (ImGui::DragInt ("##value", &depth, 0.1f)) {
						value = (float) depth;
						changed = true;
					}
					done = ImGui::IsItemDeactivated ();
					break;
				}
				default:
					break;
			}
			if (changed && ! clip->setKey (k.track, k.time, value, now.easing, why, FIELD_KEY)) say (why);
			if (done) clip->commands ().stopMerging ();
			if (! isDiscrete (track.channel)) {
				propertyRow ("Towards the next key", "How the value moves from this key to the next one; the last key's isn't used");
				int easing = now.easing;
				if (ImGui::Combo ("##easing", &easing, easingLabels, 3)) clip->setEasing (k, (Easing) easing);
			}
			if (track.channel == CHANNEL_SPRITE) {
				std::string bank = clip->bankOf (now.sprite);
				propertyRow ("Shows", "A sprite of a bank, nothing, or the part's own sprite");
				ImGui::TextUnformatted (now.sprite.kind == SpriteRef::NONE ? "Nothing (an empty frame)"
					: now.sprite.kind == SpriteRef::DEFAULT ? "The part's own sprite" : bank.c_str ());
				propertyRow ("");
				if (ImGui::Button ("Choose...")) askForFile (Pending::BANK);
				ImGui::SameLine ();
				if (ImGui::Button ("Nothing")) clip->setSpriteKey (k.track, k.time, "", 0, false, false, why);
				ImGui::SameLine ();
				if (ImGui::Button ("Own")) clip->setSpriteKey (k.track, k.time, "", 0, false, true, why);
				if (now.sprite.kind == SpriteRef::SPRITE) {
					AnimBankRef b = preview.bank (baseFolder (), bank);
					int number = now.sprite.number;
					propertyRow ("Sprite", "Its number in the bank");
					if (ImGui::InputInt ("##sprite", &number)) {
						int count = b ? b->animSpriteCount () : 0;
						if (number >= count) number = count - 1;
						if (number < 0) number = 0;
						clip->setSpriteKey (k.track, k.time, bank, number, now.sprite.mirror, false, why);
					}
					bool mirror = now.sprite.mirror;
					propertyRow ("Mirror image");
					if (ImGui::Checkbox ("##mirror", &mirror)) clip->setSpriteKey (k.track, k.time, bank, now.sprite.number, mirror, false, why);
					if (b && now.sprite.number < b->animSpriteCount ()) {
						const spriteBank * sprites = devkitBank (b);
						gfx::Texture * texture = gfxPictureTexture (graphics.draw, sprites, now.sprite.number, false);
						const sprite & s = sprites->sprites[now.sprite.number];
						float side = ImGui::GetFontSize () * 5;
						float zoom = s.width > s.height ? side / s.width : side / (s.height ? s.height : 1);
						propertyRow ("");
						if (texture) ImGui::Image (ImTextureRef (ImGui_ImplGfx_TextureID (texture)), ImVec2 (s.width * zoom, s.height * zoom));
					}
				}
			}
			ImGui::EndTable ();
		}
		if (ImGui::Button ("Remove Key")) removeSelection ();
		return;
	}

	if (selectedKeys.size () > 1) {
		ImGui::SeparatorText ("Keys");
		ImGui::Text ("%d keys", (int) selectedKeys.size ());
		ImGui::TextUnformatted ("Towards the next key:");
		for (int e = 0; e < 3; e ++) {
			if (ImGui::Button (easingLabels[e])) {
				for (std::set<KeyRef>::const_iterator i = selectedKeys.begin (); i != selectedKeys.end (); ++ i) clip->setEasing (* i, (Easing) e);
			}
		}
		if (ImGui::Button ("Remove Keys")) removeSelection ();
		return;
	}

	if (beginProperties ("Clip")) {
		propertyRow ("Rig", "The rig it is made for: choose it in the toolbar");
		ImGui::TextUnformatted (clip->asset ().rigId.isNull () ? "None" : clip->asset ().rigPath.c_str ());
		propertyRow ("Length", "In ticks; keys and events lie from 0 to the length");
		int length = clip->clip ().length;
		ImGui::InputInt ("##length", &length);
		if (ImGui::IsItemDeactivatedAfterEdit ()) requestLength (length);
		propertyRow ("Tracks");
		ImGui::Text ("%d", (int) clip->clip ().tracks.size ());
		propertyRow ("Events");
		ImGui::Text ("%d", (int) clip->clip ().events.size ());
		ImGui::EndTable ();
	}
	if (haveRig) {
		std::vector<std::string> missing = clip->missingParts (rigAsset.rig);
		if (! missing.empty ()) {
			std::string list;
			for (size_t i = 0; i < missing.size (); i ++) list += (i ? ", " : "") + partLabel (missing[i]);
			ImGui::TextColored (ImVec4 (1.0f, 0.6f, 0.3f, 1.0f), "The rig has no part of tracks of: %s. They do nothing.", list.c_str ());
		}
	}
	ImGui::TextWrapped ("Drag a part on the canvas to key it at the playhead. Click a key to edit it, or the ruler to move the playhead; K keys the selected track there.");
}

} // namespace devkit
