#include <math.h>
#include <stdio.h>

#include "imgui.h"

#include "draw2d.h"
#include "draw2d_gfx.h"
#include "sprites.h"

#include "imgui_impl_gfx.h"
#include "Platform.h"
#include "SpriteBankView.h"
#include "WorkspaceActions.h"

namespace devkit {

// How long a notice stays, in seconds.
static const double NOTICE_SECONDS = 5.0;
// The merge keys of the commands: a drag on the canvas, and the fields.
static const int DRAG_KEY = 1, FIELD_KEY = 2;

#if defined(__APPLE__)
static const char * const CTRL_CLICK = "Cmd-click";
#else
static const char * const CTRL_CLICK = "Ctrl-click";
#endif

SpriteBankView::SpriteBankView (SpriteBankDocument * b, WorkspaceActions & a, const CanvasGraphics & g)
	: bank (b), pictures (&b->bank ()), actions (a), graphics (g), canvas (g), anchor (0), current (0), scrollToCurrent (false),
	  showBox (false), spaceWidth (10), dragging (false), dragX (0), dragY (0),
	  pending (new Pending), noticeUntil (0) {
	canvas.setSpriteView (true);
}

SpriteBankView::~SpriteBankView () {
	graphics.draw->forgetPictures (pictures);
}

bool SpriteBankView::animating () const {
	return dragging || canvas.animating ();
}

void SpriteBankView::say (const std::string & text) {
	notice = text;
	noticeUntil = ImGui::GetTime () + NOTICE_SECONDS;
}

void SpriteBankView::select (int index, bool extend) {
	if (index < 0) index = 0;
	if (index >= bank->count ()) index = bank->count () - 1;
	if (index < 0) index = 0;
	current = index;
	if (! extend) anchor = index;
	scrollToCurrent = true;
	bank->commands ().stopMerging ();
}

void SpriteBankView::insert (const std::vector<std::string> & files) {
	int at = bank->count () ? first () + howMany () : 0;
	int inserted = bank->insertFiles (at, files);
	if (inserted) {
		anchor = at;
		current = at + inserted - 1;
		scrollToCurrent = true;
	}
}

bool SpriteBankView::takeFile (const std::string & path) {
	std::string extension = extensionOf (path);
	if (extension != "png" && extension != "tga") return false;
	insert (std::vector<std::string> (1, path));
	return true;
}

void SpriteBankView::deleteSelected () {
	if (! bank->count ()) return;
	int from = first ();
	bank->deleteSprites (from, howMany ());
	anchor = current = from;
	select (from, false);
}

void SpriteBankView::askForFiles (Pending::What what) {
	if (pending->what != Pending::NONE) return;
	pending->what = what;
	pending->answered = false;
	pending->files.clear ();
	std::shared_ptr<Pending> p = pending;
	Platform::FilesChosen done = [p] (const std::vector<std::string> & files) {
		p->files = files;
		p->answered = true;
	};
	std::vector<FileFilter> filters;
	FileFilter pictures = { "Pictures", "png;tga" };
	FileFilter png = { "PNG pictures", "png" };
	std::string folder = folderOf (bank->path ());
	if (what == Pending::EXPORT) {
		filters.push_back (png);
		std::string name = bank->displayName ();
		size_t dot = name.rfind ('.');
		char number[32];
		snprintf (number, sizeof (number), " %d.png", current);
		actions.platform ().saveFileDialog ("Export the sprite", filters, folder + name.substr (0, dot) + number, done);
	} else {
		filters.push_back (pictures);
		const char * title = what == Pending::INSERT ? "Insert pictures as sprites"
			: what == Pending::REPLACE ? "Replace the picture of the sprite" : "Make a font from a picture";
		actions.platform ().openFileDialog (title, filters, folder, what == Pending::INSERT, done);
	}
}

void SpriteBankView::handlePending () {
	if (pending->what == Pending::NONE || ! pending->answered) return;
	Pending::What what = pending->what;
	std::vector<std::string> files = pending->files;
	pending->what = Pending::NONE;
	pending->answered = false;
	if (files.empty ()) return;
	switch (what) {
		case Pending::INSERT:
			insert (files);
			break;
		case Pending::REPLACE:
			bank->replaceSprite (current, files[0]);
			break;
		case Pending::EXPORT: {
			std::string error;
			if (! bank->exportSprite (current, files[0], &error)) {
				actions.platform ().showMessage (Platform::MESSAGE_ERROR, "Can't export the sprite", error);
			}
			break;
		}
		case Pending::FONT: {
			int at = bank->count () ? first () + howMany () : 0;
			int letters = bank->makeFont (at, files[0], spaceWidth);
			if (letters) {
				anchor = at;
				current = at + letters - 1;
				scrollToCurrent = true;
			}
			break;
		}
		default:
			break;
	}
}

void SpriteBankView::drawToolbar () {
	bool empty = ! bank->count ();
	if (ImGui::Button ("Insert...")) askForFiles (Pending::INSERT);
	ImGui::SetItemTooltip ("Insert PNG or TGA pictures as sprites, after the selected ones");
	ImGui::SameLine ();
	ImGui::BeginDisabled (empty);
	if (ImGui::Button ("Replace...")) askForFiles (Pending::REPLACE);
	ImGui::SetItemTooltip ("Replace the picture of the sprite, keeping its hotspot");
	ImGui::SameLine ();
	if (ImGui::Button ("Delete")) deleteSelected ();
	ImGui::SetItemTooltip ("Delete the selected sprites (Delete)");
	ImGui::SameLine ();
	if (ImGui::Button ("Export...")) askForFiles (Pending::EXPORT);
	ImGui::SetItemTooltip ("Save the sprite as a PNG picture");
	ImGui::EndDisabled ();
	ImGui::SameLine ();
	ImGui::TextDisabled ("|");
	ImGui::SameLine ();
	canvas.drawZoomButtons ();
	ImGui::SameLine ();
	ImGui::Checkbox ("Box", &showBox);
	ImGui::SetItemTooltip ("Show the size of the sprite");
}

void SpriteBankView::drawThumbnails () {
	const spriteBank & sprites = bank->bank ();
	ImGuiStyle & style = ImGui::GetStyle ();
	float row = ImGui::GetFontSize () * 3.5f;
	float picture = row - style.FramePadding.y * 2;
	ImGuiListClipper clipper;
	clipper.Begin (sprites.total, row + style.ItemSpacing.y);
	if (scrollToCurrent) clipper.IncludeItemByIndex (current);
	while (clipper.Step ()) {
		for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i ++) {
			ImGui::PushID (i);
			bool selected = i >= first () && i < first () + howMany ();
			if (ImGui::Selectable ("##sprite", selected, ImGuiSelectableFlags_None, ImVec2 (0, row))) {
				select (i, ImGui::GetIO ().KeyShift);
			}
			if (scrollToCurrent && i == current) {
				ImGui::SetScrollHereY (0.5f);
				scrollToCurrent = false;
			}
			ImVec2 at = ImGui::GetItemRectMin ();
			ImDrawList * list = ImGui::GetWindowDrawList ();
			const sprite & single = sprites.sprites[i];
			gfx::Texture * texture = gfxPictureTexture (graphics.draw, &sprites, i, false);
			if (texture && single.width > 0 && single.height > 0) {
				// As large as fits, but no more than twice its size.
				float zx = picture / single.width, zy = picture / single.height;
				float z = zx < zy ? zx : zy;
				if (z > 2.0f) z = 2.0f;
				float w = single.width * z, h = single.height * z;
				ImVec2 p (at.x + style.FramePadding.x + (picture - w) * 0.5f, at.y + style.FramePadding.y + (picture - h) * 0.5f);
				list->AddImage (ImTextureRef (ImGui_ImplGfx_TextureID (texture)), p, ImVec2 (p.x + w, p.y + h));
			}
			char text[64];
			snprintf (text, sizeof (text), "%d", i);
			ImVec2 textAt (at.x + picture + style.FramePadding.x * 3, at.y + style.FramePadding.y);
			list->AddText (textAt, ImGui::GetColorU32 (ImGuiCol_Text), text);
			snprintf (text, sizeof (text), "%d \xC3\x97 %d", single.width, single.height);
			list->AddText (ImVec2 (textAt.x, textAt.y + ImGui::GetTextLineHeight ()), ImGui::GetColorU32 (ImGuiCol_TextDisabled), text);
			ImGui::PopID ();
		}
	}
}

void SpriteBankView::handleMouse () {
	float mx = canvas.mouseX (), my = canvas.mouseY ();
	if (canvas.pressed ()) {
		Hotspot h = bank->hotspot (current);
		if (ImGui::GetIO ().KeyCtrl) {
			// The pixel under the cursor becomes the hotspot.
			bank->setHotspot (current, Hotspot ((int) floorf (mx) + h.x, (int) floorf (my) + h.y));
		} else {
			dragging = true;
			dragHotspot = h;
			dragX = mx;
			dragY = my;
		}
	}
	if (dragging && canvas.held ()) {
		// The sprite follows the mouse, so its hotspot goes the other way.
		Hotspot h (dragHotspot.x + (int) floorf (dragX) - (int) floorf (mx), dragHotspot.y + (int) floorf (dragY) - (int) floorf (my));
		bank->setHotspot (current, h, DRAG_KEY);
	}
	if (dragging && canvas.released ()) {
		dragging = false;
		bank->commands ().stopMerging ();
	}
}

void SpriteBankView::handleKeys () {
	if (! ImGui::IsWindowFocused (ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO ().WantTextInput) return;
	bool shift = ImGui::GetIO ().KeyShift;
	const ImGuiInputFlags repeat = ImGuiInputFlags_Repeat;
	if (ImGui::Shortcut (ImGuiKey_DownArrow, repeat) || ImGui::Shortcut (ImGuiMod_Shift | ImGuiKey_DownArrow, repeat)) select (current + 1, shift);
	if (ImGui::Shortcut (ImGuiKey_UpArrow, repeat) || ImGui::Shortcut (ImGuiMod_Shift | ImGuiKey_UpArrow, repeat)) select (current - 1, shift);
	if (ImGui::Shortcut (ImGuiKey_Home) || ImGui::Shortcut (ImGuiMod_Shift | ImGuiKey_Home)) select (0, shift);
	if (ImGui::Shortcut (ImGuiKey_End) || ImGui::Shortcut (ImGuiMod_Shift | ImGuiKey_End)) select (bank->count () - 1, shift);
	if (ImGui::Shortcut (ImGuiKey_Delete) || ImGui::Shortcut (ImGuiKey_Backspace)) deleteSelected ();
}

void SpriteBankView::drawCanvas () {
	ImVec2 size = ImGui::GetContentRegionAvail ();
	size.y -= ImGui::GetTextLineHeightWithSpacing ();
	if (! bank->count ()) {
		ImGui::BeginChild ("empty", size);
		ImVec2 avail = ImGui::GetContentRegionAvail ();
		float width = avail.x < ImGui::GetFontSize () * 26 ? avail.x : ImGui::GetFontSize () * 26;
		ImGui::SetCursorPos (ImVec2 ((avail.x - width) * 0.5f, avail.y * 0.35f));
		ImGui::PushTextWrapPos (ImGui::GetCursorPosX () + width);
		ImGui::TextDisabled ("The bank has no sprites yet. Insert PNG or TGA pictures, or drop them here, "
			"or make a font from a picture of its letters in the Inspector.");
		ImGui::PopTextWrapPos ();
		ImGui::SetCursorPosX ((avail.x - width) * 0.5f);
		if (ImGui::Button ("Insert Pictures...")) askForFiles (Pending::INSERT);
		ImGui::EndChild ();
		ImGui::TextDisabled (" ");
		return;
	}
	const sprite & single = bank->bank ().sprites[current];
	canvas.setPictureBounds (- single.xhot, - single.yhot, single.width, single.height);
	const spriteBank * sprites = &bank->bank ();
	int index = current;
	bool box = showBox;
	canvas.draw (size, [sprites, index, box] (Draw2D & draw) { drawSpriteBankView (draw, sprites, index, box); });
	handleMouse ();

	ImGui::TextDisabled ("%s", canvas.status ().c_str ());
	if (! notice.empty () && ImGui::GetTime () < noticeUntil) {
		ImGui::SameLine (0, ImGui::GetFontSize () * 2);
		ImGui::TextColored (ImVec4 (1.0f, 0.8f, 0.3f, 1.0f), "%s", notice.c_str ());
	}
}

void SpriteBankView::draw () {
	handlePending ();
	// Undo may have taken sprites away.
	if (current >= bank->count ()) current = bank->count () - 1;
	if (anchor >= bank->count ()) anchor = bank->count () - 1;
	if (current < 0) current = 0;
	if (anchor < 0) anchor = 0;

	drawToolbar ();
	handleKeys ();
	float height = ImGui::GetContentRegionAvail ().y;
	ImGui::BeginChild ("sprites", ImVec2 (ImGui::GetFontSize () * 9, height), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
	drawThumbnails ();
	ImGui::EndChild ();
	ImGui::SameLine ();
	ImGui::BeginGroup ();
	drawCanvas ();
	ImGui::EndGroup ();
}

static void hintText (const char * text) {
	ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
	ImGui::TextWrapped ("%s", text);
	ImGui::PopStyleColor ();
}

void SpriteBankView::drawInspector () {
	ImGui::SeparatorText ("Sprite Bank");
	ImGui::Text ("%d sprite%s", bank->count (), bank->count () == 1 ? "" : "s");
	static const char * const modes[] = { "Open palette", "Locked palette", "32-bit" };
	static const char * const tips[] = {
		"New pictures add their colours to the palette, up to 255",
		"New pictures get the closest colours of the palette",
		"Every sprite has its own colours and transparency"
	};
	for (int m = 0; m < 3; m ++) {
		SpriteBankDocument::Mode mode = (SpriteBankDocument::Mode) m;
		bool convert = mode == SpriteBankDocument::MODE_32BIT && bank->mode () < SpriteBankDocument::MODE_32BIT && bank->count ();
		ImGui::BeginDisabled (! bank->canSetMode (mode) && ! convert);
		if (ImGui::RadioButton (modes[m], bank->mode () == mode)) {
			if (convert) {
				std::vector<std::string> buttons;
				buttons.push_back ("Convert");
				buttons.push_back ("Cancel");
				if (actions.platform ().ask ("Convert to 32-bit?", "Every sprite gets its colours from the palette, and "
						"the bank has no palette any more. Undo can't take that back.", buttons) == 0) bank->convertTo32 ();
			} else {
				bank->setMode (mode);
			}
		}
		ImGui::SetItemTooltip ("%s", tips[m]);
		ImGui::EndDisabled ();
	}

	if (bank->count ()) {
		const sprite & single = bank->bank ().sprites[current];
		char title[64];
		if (howMany () > 1) snprintf (title, sizeof (title), "Sprites %d to %d", first (), first () + howMany () - 1);
		else snprintf (title, sizeof (title), "Sprite %d", current);
		ImGui::SeparatorText (title);
		ImGui::Text ("%d \xC3\x97 %d pixels", single.width, single.height);
		Hotspot h = bank->hotspot (current);
		ImGui::SetNextItemWidth (ImGui::GetFontSize () * 5);
		if (ImGui::DragInt ("##x", &h.x, 0.25f)) bank->setHotspot (current, h, FIELD_KEY);
		if (ImGui::IsItemDeactivated ()) bank->commands ().stopMerging ();
		ImGui::SameLine ();
		ImGui::SetNextItemWidth (ImGui::GetFontSize () * 5);
		if (ImGui::DragInt ("Hotspot##y", &h.y, 0.25f)) bank->setHotspot (current, h, FIELD_KEY);
		if (ImGui::IsItemDeactivated ()) bank->commands ().stopMerging ();
		if (ImGui::Button ("Centre")) bank->centreHotspots (first (), howMany ());
		ImGui::SetItemTooltip ("Put the hotspot in the middle of the selected sprites");
		ImGui::SameLine ();
		if (ImGui::Button ("Base")) bank->baseHotspots (first (), howMany ());
		ImGui::SetItemTooltip ("Put the hotspot in the middle of the bottom row of the selected sprites");
		if (howMany () > 1) {
			char label[64];
			snprintf (label, sizeof (label), "Use the Hotspot of %d for All", current);
			if (ImGui::Button (label)) bank->setHotspots (first (), howMany (), h);
		}
		char hint[200];
		snprintf (hint, sizeof (hint), "Drag the sprite on the canvas to move it around its hotspot, or %s "
			"a pixel to put the hotspot there. Shift-click sprites to select a range.", CTRL_CLICK);
		hintText (hint);
	}

	ImGui::SeparatorText ("Font");
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 6);
	if (ImGui::InputInt ("Width of a space", &spaceWidth)) {
		if (spaceWidth < 1) spaceWidth = 1;
		if (spaceWidth > 1000) spaceWidth = 1000;
	}
	ImGui::BeginDisabled (bank->mode () != SpriteBankDocument::MODE_32BIT);
	if (ImGui::Button ("Make Font from Picture...")) askForFiles (Pending::FONT);
	ImGui::EndDisabled ();
	hintText ("Cuts a picture of letters, side by side with empty columns between them, into a sprite for "
		"each letter, followed by a space. Needs 32-bit.");
}

} // namespace devkit
