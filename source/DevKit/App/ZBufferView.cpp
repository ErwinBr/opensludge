#include <math.h>
#include <stdio.h>

#include <vector>

#include "imgui.h"

#include "draw2d.h"
#include "zbuffer.h"

#include "Platform.h"
#include "WorkspaceActions.h"
#include "ZBufferView.h"

namespace devkit {

// How long a notice stays, in seconds.
static const double NOTICE_SECONDS = 5.0;
// The merge keys of the commands: a drag on the canvas, and the field.
static const int DRAG_KEY = 1, FIELD_KEY = 2;

ZBufferView::ZBufferView (ZBufferDocument * z, WorkspaceActions & a, const CanvasGraphics & g)
	: zbuffer (z), actions (a), graphics (g), canvas (g), selected (1), dragging (false),
	  chosenPicture (new std::string), noticeUntil (0) {}

ZBufferView::~ZBufferView () {
	graphics.draw->forgetPictures (&zbuffer->bank ());
}

bool ZBufferView::animating () const {
	return dragging || canvas.animating ();
}

void ZBufferView::say (const std::string & text) {
	notice = text;
	noticeUntil = ImGui::GetTime () + NOTICE_SECONDS;
}

void ZBufferView::choosePicture () {
	std::vector<FileFilter> filters;
	FileFilter tga = { "TGA pictures", "tga" };
	filters.push_back (tga);
	std::string folder = zbuffer->path ().empty () ? zbuffer->startFolder () : folderOf (zbuffer->path ());
	std::shared_ptr<std::string> chosen = chosenPicture;
	actions.platform ().openFileDialog ("Make the z-buffer from a picture", filters, folder, false,
			[chosen] (const std::vector<std::string> & files) {
		if (! files.empty ()) *chosen = files[0];
	});
}

void ZBufferView::makeFrom (const std::string & path) {
	if (! zbuffer->empty ()) {
		std::vector<std::string> buttons;
		buttons.push_back ("Replace");
		buttons.push_back ("Cancel");
		if (actions.platform ().ask ("Replace the layers?", "The layers of " + zbuffer->displayName ()
				+ " and their y become those of " + fileNameOf (path) + ". Undo can't take that back.", buttons) != 0) return;
	}
	if (zbuffer->makeFromTGA (path)) {
		selected = 1;
		dragging = false;
		canvas.setPictureSize (zbuffer->width (), zbuffer->height ());
	}
}

bool ZBufferView::takeFile (const std::string & path) {
	if (extensionOf (path) != "tga") return false;
	makeFrom (path);
	return true;
}

void ZBufferView::handleMouse () {
	int x = (int) floorf (canvas.mouseX ()), y = (int) floorf (canvas.mouseY ());
	if (canvas.pressed ()) {
		if (ImGui::GetIO ().KeyCtrl) {
			int layer = zbuffer->layerAt (x, y);
			if (layer) selected = layer;
			else say ("There is no layer there.");
		} else {
			dragging = true;
		}
	}
	if (dragging && canvas.held ()) zbuffer->setLayerY (selected, y, DRAG_KEY);
	if (dragging && canvas.released ()) {
		dragging = false;
		zbuffer->commands ().stopMerging ();
	}
}

void ZBufferView::draw () {
	if (! chosenPicture->empty ()) {
		std::string path = *chosenPicture;
		chosenPicture->clear ();
		makeFrom (path);
	}
	if (selected > zbuffer->layers ()) selected = zbuffer->layers ();
	if (selected < 1) selected = 1;

	if (ImGui::Button ("Make from TGA...")) choosePicture ();
	ImGui::SetItemTooltip ("Make the layers from a TGA picture, in which each colour is a layer");
	if (zbuffer->empty ()) {
		ImVec2 avail = ImGui::GetContentRegionAvail ();
		const char * text = "Make the z-buffer from a TGA picture of the room, in which each colour "
			"(up to 15) is a layer, on black or transparent. You can drop the picture here too.";
		float width = avail.x < ImGui::GetFontSize () * 30 ? avail.x : ImGui::GetFontSize () * 30;
		ImGui::SetCursorPos (ImVec2 (ImGui::GetCursorPosX () + (avail.x - width) * 0.5f, ImGui::GetCursorPosY () + avail.y * 0.35f));
		ImGui::PushTextWrapPos (ImGui::GetCursorPosX () + width);
		ImGui::TextDisabled ("%s", text);
		ImGui::PopTextWrapPos ();
		return;
	}

	ImGui::SameLine ();
	ImGui::TextDisabled ("|");
	ImGui::SameLine ();
	canvas.drawZoomButtons ();

	canvas.setPictureSize (zbuffer->width (), zbuffer->height ());
	float statusHeight = ImGui::GetTextLineHeightWithSpacing ();
	ImVec2 size = ImGui::GetContentRegionAvail ();
	size.y -= statusHeight;
	int layer = selected;
	const ZBufferDocument * z = zbuffer;
	canvas.draw (size, [z, layer] (Draw2D & draw) { drawZBufferView (draw, &z->bank (), layer); });
	handleMouse ();

	std::string status = canvas.status ();
	if (canvas.hovered ()) {
		int under = zbuffer->layerAt ((int) floorf (canvas.mouseX ()), (int) floorf (canvas.mouseY ()));
		if (under) {
			char text[32];
			snprintf (text, sizeof (text), "    layer %d", under);
			status += text;
		}
	}
	ImGui::TextDisabled ("%s", status.c_str ());
	if (! notice.empty () && ImGui::GetTime () < noticeUntil) {
		ImGui::SameLine (0, ImGui::GetFontSize () * 2);
		ImGui::TextColored (ImVec4 (1.0f, 0.8f, 0.3f, 1.0f), "%s", notice.c_str ());
	}
}

static void hintText (const char * text) {
	ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
	ImGui::TextWrapped ("%s", text);
	ImGui::PopStyleColor ();
}

void ZBufferView::drawInspector () {
	if (zbuffer->empty ()) {
		ImGui::SeparatorText ("Z-Buffer");
		ImGui::TextDisabled ("No layers yet.");
		if (ImGui::Button ("Make from TGA...")) choosePicture ();
		return;
	}

	ImGui::SeparatorText ("Layers");
	ImGui::Text ("%d layer%s, %d \xC3\x97 %d", zbuffer->layers (), zbuffer->layers () == 1 ? "" : "s",
		zbuffer->width (), zbuffer->height ());
	float rows = (float) (zbuffer->layers () < 8 ? zbuffer->layers () : 8);
	if (ImGui::BeginListBox ("##layers", ImVec2 (-1, rows * ImGui::GetTextLineHeightWithSpacing () + ImGui::GetStyle ().FramePadding.y * 2))) {
		for (int l = 1; l <= zbuffer->layers (); l ++) {
			char label[64];
			snprintf (label, sizeof (label), "Layer %d", l);
			ImGui::PushID (l);
			if (ImGui::Selectable (label, selected == l)) {
				selected = l;
				zbuffer->commands ().stopMerging ();
			}
			ImGui::SameLine (ImGui::GetFontSize () * 7);
			ImGui::TextDisabled ("y %d", zbuffer->layerY (l));
			ImGui::PopID ();
		}
		ImGui::EndListBox ();
	}
#if defined(__APPLE__)
	hintText ("Cmd-click a layer on the canvas to select it.");
#else
	hintText ("Ctrl-click a layer on the canvas to select it.");
#endif

	char title[32];
	snprintf (title, sizeof (title), "Layer %d", selected);
	ImGui::SeparatorText (title);
	int y = zbuffer->layerY (selected);
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 8);
	if (ImGui::DragInt ("Y", &y, 1.0f, 0, zbuffer->maxY ())) zbuffer->setLayerY (selected, y, FIELD_KEY);
	if (ImGui::IsItemDeactivated ()) zbuffer->commands ().stopMerging ();
	hintText ("A character whose feet are above this line, further away, is behind the layer. Click or "
		"drag on the canvas to set it. It can be below the picture, for characters that walk out at the bottom.");

	ImGui::SeparatorText ("Picture");
	if (ImGui::Button ("Make from TGA...")) choosePicture ();
	hintText ("Each colour of the picture is a layer. Making the layers sets the y of each to its lowest row.");
}

} // namespace devkit
