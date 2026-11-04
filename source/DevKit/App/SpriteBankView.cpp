#include <math.h>
#include <algorithm>
#include <stdio.h>
#include <string.h>

#include "imgui.h"
#include "renderer.h"

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

SpriteBankView::SpriteBankView (SpriteBankDocument * b, WorkspaceActions & a, const CanvasGraphics & g)
	: bank (b), pictures (&b->bank ()), actions (a), graphics (g), canvas (g), anchor (0), current (0), scrollToCurrent (false),
	  showBox (false), spaceWidth (10), dragging (false), dragX (0), dragY (0),
	  sheetLoaded (false), sheetToOpen (false), pending (new Pending), noticeUntil (0) {
	memset (&sheet, 0, sizeof (sheet));
	canvas.setSpriteView (true);
}

SpriteBankView::~SpriteBankView () {
	closeSheet ();
	graphics.draw->forgetPictures (pictures);
}

bool SpriteBankView::animating () const {
	return dragging || canvas.animating ();
}

void SpriteBankView::say (const std::string & text) {
	notice = text;
	noticeUntil = ImGui::GetTime () + NOTICE_SECONDS;
}

std::vector<int> SpriteBankView::selection () const {
	std::vector<int> indices;
	for (size_t i = 0; i < chosen.size (); i ++) if (chosen[i]) indices.push_back ((int) i);
	return indices;
}

int SpriteBankView::selectedCount () const {
	int n = 0;
	for (size_t i = 0; i < chosen.size (); i ++) if (chosen[i]) n ++;
	return n;
}

int SpriteBankView::insertionPoint () const {
	if (! bank->count ()) return 0;
	std::vector<int> indices = selection ();
	return indices.empty () ? bank->count () : indices.back () + 1;
}

void SpriteBankView::keepSelection () {
	int total = bank->count ();
	if ((int) chosen.size () != total) chosen.resize (total, false);
	if (current >= total) current = total - 1;
	if (anchor >= total) anchor = total - 1;
	if (current < 0) current = 0;
	if (anchor < 0) anchor = 0;
	if (total && ! selectedCount ()) chosen[current] = true;
}

void SpriteBankView::select (int index, bool extend) {
	keepSelection ();
	if (index >= bank->count ()) index = bank->count () - 1;
	if (index < 0) index = 0;
	current = index;
	if (! extend) anchor = index;
	std::fill (chosen.begin (), chosen.end (), false);
	int from = anchor < current ? anchor : current, to = anchor < current ? current : anchor;
	for (int i = from; i <= to && i < (int) chosen.size (); i ++) chosen[i] = true;
	scrollToCurrent = true;
	bank->commands ().stopMerging ();
}

void SpriteBankView::selectRun (int from, int howMany, int newCurrent) {
	keepSelection ();
	std::fill (chosen.begin (), chosen.end (), false);
	for (int i = from; i < from + howMany && i < (int) chosen.size (); i ++) if (i >= 0) chosen[i] = true;
	anchor = from;
	current = newCurrent;
	keepSelection ();
	scrollToCurrent = true;
	bank->commands ().stopMerging ();
}

void SpriteBankView::click (int index, bool shift, bool ctrl) {
	keepSelection ();
	if (index < 0 || index >= (int) chosen.size ()) return;
	if (shift) {
		// A range from the anchor, added to the others with Ctrl.
		if (! ctrl) std::fill (chosen.begin (), chosen.end (), false);
		int from = anchor < index ? anchor : index, to = anchor < index ? index : anchor;
		for (int i = from; i <= to; i ++) chosen[i] = true;
		current = index;
	} else if (ctrl) {
		anchor = index;
		if (! chosen[index]) {
			chosen[index] = true;
			current = index;
		} else if (selectedCount () > 1) {
			chosen[index] = false;
			if (current == index) {
				// The nearest one that is still selected.
				for (int d = 1; d < (int) chosen.size (); d ++) {
					if (isSelected (index + d)) { current = index + d; break; }
					if (isSelected (index - d)) { current = index - d; break; }
				}
			}
		}
	} else {
		std::fill (chosen.begin (), chosen.end (), false);
		chosen[index] = true;
		anchor = current = index;
	}
	bank->commands ().stopMerging ();
}

void SpriteBankView::moveDragged (int dragged, int to) {
	std::vector<int> moving = isSelected (dragged) ? selection () : std::vector<int> (1, dragged);
	int at = bank->moveSprites (moving, to);
	int newCurrent = at;
	// The canvas keeps the sprite that it showed, if it moved.
	for (size_t k = 0; k < moving.size (); k ++) if (moving[k] == current) newCurrent = at + (int) k;
	selectRun (at, (int) moving.size (), newCurrent);
}

void SpriteBankView::insert (const std::vector<std::string> & files) {
	int at = insertionPoint ();
	int inserted = bank->insertFiles (at, files);
	if (inserted) selectRun (at, inserted, at + inserted - 1);
}

bool SpriteBankView::takeFile (const std::string & path) {
	std::string extension = extensionOf (path);
	if (extension != "png" && extension != "tga") return false;
	insert (std::vector<std::string> (1, path));
	return true;
}

void SpriteBankView::deleteSelected () {
	if (! bank->count ()) return;
	keepSelection ();
	std::vector<int> indices = selection ();
	if (indices.empty ()) return;
	bank->deleteSprites (indices);
	std::fill (chosen.begin (), chosen.end (), false);
	select (indices[0], false);
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
			: what == Pending::REPLACE ? "Replace the picture of the sprite"
			: what == Pending::SHEET ? "Import a sprite sheet" : "Make a font from a picture";
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
			int at = insertionPoint ();
			int letters = bank->makeFont (at, files[0], spaceWidth);
			if (letters) selectRun (at, letters, at + letters - 1);
			break;
		}
		case Pending::SHEET:
			openSheet (files[0]);
			break;
		default:
			break;
	}
}

void SpriteBankView::openSheet (const std::string & file) {
	closeSheet ();
	// A 32-bit copy to show, whatever the mode of the bank.
	sheet.type = SpriteBankDocument::MODE_32BIT;
	addSprite (0, &sheet);
	std::string extension = extensionOf (file);
	bool loaded = extension == "png" ? loadSpriteFromPNG (file.c_str (), &sheet, 0)
		: extension == "tga" && loadSpriteFromTGA (file.c_str (), &sheet, 0);
	spriteBankChanged (&sheet);
	if (! loaded) {
		closeSheet ();
		return;
	}
	sheetFile = file;
	sheetLoaded = sheetToOpen = true;
	layout = SheetLayout ();
}

void SpriteBankView::closeSheet () {
	graphics.draw->forgetPictures (&sheet);
	forgetSpriteBank (&sheet);
	memset (&sheet, 0, sizeof (sheet));
	sheetLoaded = false;
}

void SpriteBankView::drawSheetDialog () {
	if (sheetToOpen) {
		ImGui::OpenPopup ("Import Sprite Sheet");
		sheetToOpen = false;
	}
	ImGui::SetNextWindowSize (ImVec2 (ImGui::GetFontSize () * 30, 0), ImGuiCond_Appearing);
	if (! ImGui::BeginPopupModal ("Import Sprite Sheet", NULL, ImGuiWindowFlags_NoSavedSettings)) return;
	if (! sheetLoaded) {
		ImGui::CloseCurrentPopup ();
		ImGui::EndPopup ();
		return;
	}
	const sprite & picture = sheet.sprites[0];
	ImGui::TextUnformatted (fileNameOf (sheetFile).c_str ());
	ImGui::SameLine ();
	ImGui::TextDisabled ("%d \xC3\x97 %d pixels", picture.width, picture.height);

	// The sheet, as large as fits, with its grid; the cells that we leave
	// out are darker.
	float most = ImGui::GetContentRegionAvail ().x, high = ImGui::GetFontSize () * 18;
	float z = most / picture.width;
	if (picture.height * z > high) z = high / picture.height;
	if (z > 8.0f) z = 8.0f;
	ImVec2 size (picture.width * z, picture.height * z);
	ImVec2 at = ImGui::GetCursorScreenPos ();
	ImDrawList * list = ImGui::GetWindowDrawList ();
	list->AddRectFilled (at, ImVec2 (at.x + size.x, at.y + size.y), ImGui::GetColorU32 (ImGuiCol_FrameBg));
	gfx::Texture * texture = gfxPictureTexture (graphics.draw, &sheet, 0, false);
	// Pixel for pixel, as the canvas shows them.
	if (texture) graphics.renderer->setTextureFilter (texture, gfx::FILTER_NEAREST, gfx::FILTER_NEAREST);
	if (texture) list->AddImage (ImTextureRef (ImGui_ImplGfx_TextureID (texture)), at, ImVec2 (at.x + size.x, at.y + size.y));
	bool fits = picture.width % layout.columns == 0 && picture.height % layout.rows == 0;
	float cw = size.x / layout.columns, ch = size.y / layout.rows;
	for (int c = layout.count; c < layout.columns * layout.rows; c ++) {
		ImVec2 p (at.x + (c % layout.columns) * cw, at.y + (c / layout.columns) * ch);
		list->AddRectFilled (p, ImVec2 (p.x + cw, p.y + ch), IM_COL32 (0, 0, 0, 160));
	}
	ImU32 line = fits ? IM_COL32 (90, 200, 255, 220) : IM_COL32 (255, 90, 90, 220);
	for (int c = 0; c <= layout.columns; c ++) list->AddLine (ImVec2 (at.x + c * cw, at.y), ImVec2 (at.x + c * cw, at.y + size.y), line);
	for (int r = 0; r <= layout.rows; r ++) list->AddLine (ImVec2 (at.x, at.y + r * ch), ImVec2 (at.x + size.x, at.y + r * ch), line);
	ImGui::Dummy (size);

	int columns = layout.columns, rows = layout.rows;
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 6);
	ImGui::InputInt ("Columns", &columns);
	ImGui::SameLine ();
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 6);
	ImGui::InputInt ("Rows", &rows);
	if (columns < 1) columns = 1;
	if (rows < 1) rows = 1;
	if (columns > picture.width) columns = picture.width;
	if (rows > picture.height) rows = picture.height;
	if (columns != layout.columns || rows != layout.rows) {
		layout.columns = columns;
		layout.rows = rows;
		layout.count = columns * rows;
	}
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 6);
	if (ImGui::InputInt ("Frames", &layout.count)) {
		if (layout.count < 1) layout.count = 1;
		if (layout.count > layout.columns * layout.rows) layout.count = layout.columns * layout.rows;
	}
	ImGui::SetItemTooltip ("How many cells to use, a row at a time from the top left");
	ImGui::SameLine ();
	if (fits) ImGui::TextDisabled ("Each frame is %d \xC3\x97 %d", picture.width / layout.columns, picture.height / layout.rows);
	else ImGui::TextColored (ImVec4 (1.0f, 0.4f, 0.4f, 1.0f), "The cells aren't all the same size");
	ImGui::Checkbox ("Skip empty cells", &layout.skipEmpty);
	ImGui::SetItemTooltip ("Leave out the cells that are all transparent");
	ImGui::SameLine ();
	ImGui::Checkbox ("Trim empty space", &layout.trim);
	ImGui::SetItemTooltip ("Cut each frame down to its pixels that show, keeping its place around the hotspot");

	// As for inserting pictures, a bank with a palette takes only TGA.
	bool canLoad = bank->mode () == SpriteBankDocument::MODE_32BIT || extensionOf (sheetFile) != "png";
	if (! canLoad) ImGui::TextColored (ImVec4 (1.0f, 0.4f, 0.4f, 1.0f), "A PNG picture needs a 32-bit colour bank.");

	ImGui::Separator ();
	ImGui::BeginDisabled (! fits || ! canLoad);
	if (ImGui::Button ("Import")) {
		int at = insertionPoint ();
		int inserted = bank->insertSheet (at, sheetFile, layout);
		if (inserted) selectRun (at, inserted, at);
		closeSheet ();
		ImGui::CloseCurrentPopup ();
	}
	ImGui::EndDisabled ();
	ImGui::SameLine ();
	if (ImGui::Button ("Cancel") || ImGui::Shortcut (ImGuiKey_Escape)) {
		closeSheet ();
		ImGui::CloseCurrentPopup ();
	}
	ImGui::EndPopup ();
}

void SpriteBankView::drawToolbar () {
	bool empty = ! bank->count ();
	if (ImGui::Button ("Insert...")) askForFiles (Pending::INSERT);
	ImGui::SetItemTooltip ("Insert PNG or TGA pictures as sprites, after the selected ones");
	ImGui::SameLine ();
	if (ImGui::Button ("Import Sheet...")) askForFiles (Pending::SHEET);
	ImGui::SetItemTooltip ("Cut a picture into a grid of frames, after the selected sprites");
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
	ImGui::TextDisabled ("|");
	ImGui::SameLine ();
	ImGui::Checkbox ("Show sprite size", &showBox);
	ImGui::SetItemTooltip ("Show the size of the sprite");
}

void SpriteBankView::drawThumbnails () {
	const spriteBank & sprites = bank->bank ();
	ImGuiStyle & style = ImGui::GetStyle ();
	float row = ImGui::GetFontSize () * 3.5f;
	float picture = row - style.FramePadding.y * 2;
	// The release that ends a drag doesn't select.
	bool wasDragging = ImGui::GetDragDropPayload () != NULL;
	int dragged = -1, dropAt = -1;
	ImGuiListClipper clipper;
	clipper.Begin (sprites.total, row + style.ItemSpacing.y);
	if (scrollToCurrent) clipper.IncludeItemByIndex (current);
	while (clipper.Step ()) {
		for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i ++) {
			ImGui::PushID (i);
			if (ImGui::Selectable ("##sprite", isSelected (i), ImGuiSelectableFlags_None, ImVec2 (0, row)) && ! wasDragging) {
				click (i, ImGui::GetIO ().KeyShift, ImGui::GetIO ().KeyCtrl);
			}
			if (ImGui::BeginDragDropSource ()) {
				ImGui::SetDragDropPayload ("SLUDGE_SPRITES", &i, sizeof (i));
				int n = isSelected (i) ? selectedCount () : 1;
				if (n == 1) ImGui::Text ("Sprite %d", i);
				else ImGui::Text ("%d sprites", n);
				ImGui::EndDragDropSource ();
			}
			if (ImGui::BeginDragDropTarget ()) {
				const ImGuiPayload * payload = ImGui::AcceptDragDropPayload ("SLUDGE_SPRITES",
					ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
				if (payload) {
					// Above or below the middle of the row: the gap before or after it.
					ImVec2 min = ImGui::GetItemRectMin (), max = ImGui::GetItemRectMax ();
					bool below = ImGui::GetMousePos ().y > (min.y + max.y) * 0.5f;
					float y = below ? max.y + style.ItemSpacing.y * 0.5f : min.y - style.ItemSpacing.y * 0.5f;
					ImGui::GetWindowDrawList ()->AddLine (ImVec2 (min.x, y), ImVec2 (max.x, y),
						ImGui::GetColorU32 (ImGuiCol_DragDropTarget), 2.0f);
					if (payload->IsDelivery ()) {
						dragged = * (const int *) payload->Data;
						dropAt = i + (below ? 1 : 0);
					}
				}
				ImGui::EndDragDropTarget ();
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
	if (dragged >= 0 && dropAt >= 0) moveDragged (dragged, dropAt);
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
	// Undo may have taken sprites away, or put them back.
	keepSelection ();

	drawToolbar ();
	drawSheetDialog ();
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

void SpriteBankView::drawInspector () {
	ImGui::SeparatorText ("Sprite Bank");
	ImGui::Text ("%d sprite%s", bank->count (), bank->count () == 1 ? "" : "s");
	static const char * const modes[] = { "Open palette", "Locked palette", "32-bit colour" };
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
		std::vector<int> indices = selection ();
		char title[64];
		if (indices.size () <= 1) snprintf (title, sizeof (title), "Sprite %d", current);
		else if (indices.back () - indices.front () + 1 == (int) indices.size ()) {
			snprintf (title, sizeof (title), "Sprites %d to %d", indices.front (), indices.back ());
		} else snprintf (title, sizeof (title), "%d Sprites", (int) indices.size ());
		ImGui::SeparatorText (title);
		Hotspot h = bank->hotspot (current);
		ImGui::AlignTextToFramePadding ();
		ImGui::Text ("Hotspot");
		ImGui::SameLine ();
		ImGui::Text ("X:");
		ImGui::SameLine ();
		ImGui::SetNextItemWidth (ImGui::GetFontSize () * 4);
		if (ImGui::DragInt ("##x", &h.x, 0.25f)) bank->setHotspot (current, h, FIELD_KEY);
		if (ImGui::IsItemDeactivated ()) bank->commands ().stopMerging ();
		ImGui::SameLine ();
		ImGui::Text ("Y:");
		ImGui::SameLine ();
		ImGui::SetNextItemWidth (ImGui::GetFontSize () * 4);
		if (ImGui::DragInt ("##y", &h.y, 0.25f)) bank->setHotspot (current, h, FIELD_KEY);
		if (ImGui::IsItemDeactivated ()) bank->commands ().stopMerging ();
		if (ImGui::Button ("Centre")) bank->centreHotspots (indices);
		ImGui::SetItemTooltip ("Put the hotspot in the middle of the selected sprites");
		ImGui::SameLine ();
		if (ImGui::Button ("Base")) bank->baseHotspots (indices);
		ImGui::SetItemTooltip ("Put the hotspot in the middle of the bottom row of the selected sprites");
		if (indices.size () > 1) {
			char label[64];
			snprintf (label, sizeof (label), "Use the Hotspot of %d for All", current);
			if (ImGui::Button (label)) bank->setHotspots (indices, h);
		}
	}

	ImGui::SeparatorText ("Fontify");
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 6);
	if (ImGui::InputInt ("Width of a space", &spaceWidth)) {
		if (spaceWidth < 1) spaceWidth = 1;
		if (spaceWidth > 1000) spaceWidth = 1000;
	}
	ImGui::BeginDisabled (bank->mode () != SpriteBankDocument::MODE_32BIT);
	if (ImGui::Button ("Make Font from Picture...")) askForFiles (Pending::FONT);
	ImGui::EndDisabled ();
}

} // namespace devkit
