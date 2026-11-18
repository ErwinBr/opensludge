#include <math.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include "imgui.h"

#include "draw2d.h"

#include "FileMemory.h"
#include "FloorView.h"
#include "Log.h"
#include "Platform.h"
#include "WorkspaceActions.h"

namespace devkit {

// The size of the picture of a floor without a backdrop.
static const int DEFAULT_WIDTH = 640, DEFAULT_HEIGHT = 480;
// How long a notice stays, in seconds.
static const double NOTICE_SECONDS = 5.0;

// What we remember of a floor file.
static const char * const BACKDROP_KEY = "floor.backdrop";
static const char * const COLOUR_KEY = "floor.colour";

FloorView::FloorView (FloorDocument * f, WorkspaceActions & a, const CanvasGraphics & g, FileMemory & m)
	: floor (f), actions (a), graphics (g), memory (m), canvas (g),
	  // A floor that has vertices opens for moving them, a new one for
	  // drawing.
	  tools (f->floor ().vertexCount () ? FloorTools::TOOL_MOVE : FloorTools::TOOL_DRAW,
		[this] (const std::string & text) { say (text); }),
	  chosenBackdrop (new std::string), noticeUntil (0) {
	colour[0] = colour[1] = colour[2] = 1.0f;
	memset (&backdrop, 0, sizeof (backdrop));
	backdrop.type = 2;
	reserveSpritePal (&backdrop.myPalette, 0);
	addSprite (0, &backdrop);
	recall ();
	if (backdropFile.empty ()) clearBackdrop ();
}

FloorView::~FloorView () {
	graphics.draw->forgetPictures (&backdrop);
	forgetSpriteBank (&backdrop);
}

bool FloorView::animating () const {
	return tools.busy () || canvas.animating ();
}

void FloorView::say (const std::string & text) {
	notice = text;
	noticeUntil = ImGui::GetTime () + NOTICE_SECONDS;
}

static bool fileExists (const std::string & path) {
	FILE * fp = fopen (path.c_str (), "rb");
	if (! fp) return false;
	fclose (fp);
	return true;
}

void FloorView::recall () {
	rememberedPath = floor->path ();
	if (rememberedPath.empty ()) return;
	std::string file = memory.get (rememberedPath, BACKDROP_KEY);
	if (! file.empty ()) {
		if (fileExists (file)) loadBackdrop (file);
		else Log::get ().add (LogLine::LOG_WARNING, "The backdrop of %s, %s, is gone\n", floor->displayName ().c_str (), file.c_str ());
	}
	float r, g, b;
	if (sscanf (memory.get (rememberedPath, COLOUR_KEY).c_str (), "%f %f %f", &r, &g, &b) == 3) {
		colour[0] = r;
		colour[1] = g;
		colour[2] = b;
	}
}

void FloorView::remember () {
	rememberedPath = floor->path ();
	if (rememberedPath.empty ()) return;
	memory.set (rememberedPath, BACKDROP_KEY, backdropFile);
	char text[64];
	snprintf (text, sizeof (text), "%.3f %.3f %.3f", colour[0], colour[1], colour[2]);
	memory.set (rememberedPath, COLOUR_KEY, text);
}

bool FloorView::loadBackdrop (const std::string & path) {
	std::string extension = extensionOf (path);
	bool loaded = false;
	if (extension == "png") {
		loaded = loadSpriteFromPNG (path.c_str (), &backdrop, 0);
	} else if (extension == "tga") {
		loaded = loadSpriteFromTGA (path.c_str (), &backdrop, 0);
	} else {
		actions.platform ().showMessage (Platform::MESSAGE_ERROR, "Can't load the backdrop",
			"TGA and PNG are the kinds of picture that a backdrop can be.");
	}
	if (! loaded) return false;
	// As the GTK Floor Maker, we draw the backdrop the other way up, as a
	// z-buffer.
	sprite & picture = backdrop.sprites[0];
	if (picture.height > 0) picture.height = - picture.height;
	backdropFile = path;
	canvas.setPictureSize (picture.width, - picture.height);
	return true;
}

bool FloorView::takeFile (const std::string & path) {
	std::string extension = extensionOf (path);
	if (extension != "png" && extension != "tga") return false;
	if (loadBackdrop (path)) remember ();
	return true;
}

void FloorView::clearBackdrop () {
	if (backdrop.sprites[0].data) {
		delete [] backdrop.sprites[0].data;
		backdrop.sprites[0].data = NULL;
	}
	backdrop.sprites[0].width = backdrop.sprites[0].height = 0;
	spriteBankChanged (&backdrop);
	backdropFile.clear ();
	// Without a backdrop, the picture is large enough for the floor.
	FloorPoint extent = floor->floor ().extent ();
	canvas.setPictureSize (extent.x + 40 > DEFAULT_WIDTH ? extent.x + 40 : DEFAULT_WIDTH,
		extent.y + 40 > DEFAULT_HEIGHT ? extent.y + 40 : DEFAULT_HEIGHT);
}

void FloorView::chooseBackdrop () {
	std::vector<FileFilter> filters;
	FileFilter images = { "Images", "png;tga" };
	filters.push_back (images);
	std::string folder = ! backdropFile.empty () ? folderOf (backdropFile) : folderOf (floor->path ());
	std::shared_ptr<std::string> chosen = chosenBackdrop;
	actions.platform ().openFileDialog ("Choose a backdrop", filters, folder, false,
			[chosen] (const std::vector<std::string> & files) {
		if (! files.empty ()) *chosen = files[0];
	});
}

void FloorView::drawToolbar () {
	tools.drawToolButtons ();
	ImGui::SameLine ();
	ImGui::TextDisabled ("|");
	ImGui::SameLine ();
	canvas.drawZoomButtons ();
	ImGui::SameLine ();
	ImGui::TextDisabled ("|");
	ImGui::SameLine ();
	if (ImGui::Button ("Backdrop...")) chooseBackdrop ();
	ImGui::SetItemTooltip ("Choose the picture of the room to draw the floor on");

	// The keys of the tools, while the floor's tab has the focus.
	tools.handleToolKeys ();
}

void FloorView::draw () {
	if (! chosenBackdrop->empty ()) {
		if (loadBackdrop (*chosenBackdrop)) remember ();
		chosenBackdrop->clear ();
	}
	// After the first save, or Save As, we remember for the new file.
	if (floor->path () != rememberedPath) remember ();

	drawToolbar ();
	float statusHeight = ImGui::GetTextLineHeightWithSpacing ();
	ImVec2 size = ImGui::GetContentRegionAvail ();
	size.y -= statusHeight;
	canvas.draw (size, [this] (Draw2D & draw) { tools.paint (draw, *floor, canvas, &backdrop, colour); });
	tools.handleMouse (*floor, canvas);

	ImGui::TextDisabled ("%s", canvas.status ().c_str ());
	if (! notice.empty () && ImGui::GetTime () < noticeUntil) {
		ImGui::SameLine (0, ImGui::GetFontSize () * 2);
		ImGui::TextColored (ImVec4 (1.0f, 0.8f, 0.3f, 1.0f), "%s", notice.c_str ());
	}
}

void FloorView::drawInspector () {
	tools.drawToolInspector ();

	ImGui::SeparatorText ("Backdrop");
	if (backdropFile.empty ()) {
		ImGui::TextDisabled ("None");
	} else {
		ImGui::TextUnformatted (fileNameOf (backdropFile).c_str ());
		ImGui::SetItemTooltip ("%s", backdropFile.c_str ());
	}
	if (ImGui::Button ("Choose...")) chooseBackdrop ();
	ImGui::SameLine ();
	ImGui::BeginDisabled (backdropFile.empty ());
	if (ImGui::Button ("Clear")) {
		clearBackdrop ();
		remember ();
	}
	ImGui::EndDisabled ();
	ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
	ImGui::TextWrapped ("The picture of the room, to draw the floor on. It isn't saved in the floor; "
		"the Dev Kit remembers it for this file.");
	ImGui::PopStyleColor ();

	ImGui::SeparatorText ("Outline");
	ImGui::ColorEdit3 ("Colour", colour, ImGuiColorEditFlags_NoInputs);
	if (ImGui::IsItemDeactivatedAfterEdit ()) remember ();

	tools.drawFloorInspector (*floor);
}

} // namespace devkit
