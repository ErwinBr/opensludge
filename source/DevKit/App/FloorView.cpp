#include <math.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include "imgui.h"

#include "draw2d.h"
#include "floormaker.h"

#include "FileMemory.h"
#include "FloorView.h"
#include "Log.h"
#include "Platform.h"
#include "WorkspaceActions.h"

namespace devkit {

// The size of the picture of a floor without a backdrop.
static const int DEFAULT_WIDTH = 640, DEFAULT_HEIGHT = 480;
// How close, in points, the cursor must be to a vertex to take it.
static const float SNAP_POINTS = 7.0f;
// How long a notice stays, in seconds.
static const double NOTICE_SECONDS = 5.0;

// What we remember of a floor file.
static const char * const BACKDROP_KEY = "floor.backdrop";
static const char * const COLOUR_KEY = "floor.colour";

struct ToolInfo {
	const char * name;
	ImGuiKey key;
	const char * hint;
};

static const ToolInfo tools[FloorView::TOOL_COUNT] = {
	{ "Draw", ImGuiKey_1, "Click to add a vertex to the polygon you're drawing. Click its first vertex to close it. "
		"Backspace removes the last vertex, Esc the whole unfinished polygon." },
	{ "Move", ImGuiKey_2, "Click a vertex to select it, Shift-click to add more, or drag a box around several. "
		"Drag a selected vertex to move the selection, in every polygon that has it. Delete deletes the selection." },
	{ "Delete", ImGuiKey_3, "Click a vertex to delete it." },
	{ "Split Line", ImGuiKey_4, "Drag from one end of a line to the other to add a vertex in its middle." },
	{ "Split Floor", ImGuiKey_5, "Drag between two vertices of a polygon to split it in two." },
};

FloorView::FloorView (FloorDocument * f, WorkspaceActions & a, const CanvasGraphics & g, FileMemory & m)
	: floor (f), actions (a), graphics (g), memory (m), canvas (g),
	  // A floor that has vertices opens for moving them, a new one for
	  // drawing.
	  tool (f->floor ().vertexCount () ? TOOL_MOVE : TOOL_DRAW),
	  chosenBackdrop (new std::string), lit (false), litX (0), litY (0),
	  dragging (false), fromX (0), fromY (0), toX (0), toY (0),
	  pressX (0), pressY (0), boxing (false), boxX1 (0), boxY1 (0), boxX2 (0), boxY2 (0), noticeUntil (0) {
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
	return dragging || boxing || canvas.animating ();
}

int FloorView::closeness () const {
	int pixels = (int) ceilf (canvas.pixelsOfPoints (SNAP_POINTS));
	return pixels < 1 ? 1 : pixels;
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
	for (int t = 0; t < TOOL_COUNT; t ++) {
		if (t) ImGui::SameLine ();
		bool current = tool == t;
		if (current) ImGui::PushStyleColor (ImGuiCol_Button, ImGui::GetStyleColorVec4 (ImGuiCol_ButtonActive));
		if (ImGui::Button (tools[t].name)) tool = (Tool) t;
		if (current) ImGui::PopStyleColor ();
		ImGui::SetItemTooltip ("%s (%d)\n%s", tools[t].name, t + 1, tools[t].hint);
	}
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
	if (! ImGui::GetIO ().WantTextInput) {
		for (int t = 0; t < TOOL_COUNT; t ++) {
			if (ImGui::Shortcut (tools[t].key)) tool = (Tool) t;
		}
	}
}

static bool hasPoint (const std::vector<FloorPoint> & points, const FloorPoint & point) {
	for (size_t i = 0; i < points.size (); i ++) {
		if (points[i] == point) return true;
	}
	return false;
}

// What the selection is still on, after an undo or another tool changed
// the floor.
void FloorView::keepSelection () {
	const Floor & f = floor->floor ();
	std::vector<FloorPoint> kept;
	for (size_t i = 0; i < selected.size (); i ++) {
		bool there = false;
		for (size_t p = 0; p < f.polygons.size () && ! there; p ++) {
			there = hasPoint (f.polygons[p], selected[i]);
		}
		if (there) kept.push_back (selected[i]);
	}
	selected.swap (kept);
}

// How far a drag moves several selected vertices: as far as the mouse moved
// from the vertex it took, but no vertex goes left of or above the picture.
void FloorView::selectionOffset (int * dx, int * dy) const {
	*dx = toX - fromX;
	*dy = toY - fromY;
	for (size_t i = 0; i < selected.size (); i ++) {
		if (selected[i].x + *dx < 0) *dx = - selected[i].x;
		if (selected[i].y + *dy < 0) *dy = - selected[i].y;
	}
}

void FloorView::handleMouse () {
	int x = (int) floorf (canvas.mouseX ()), y = (int) floorf (canvas.mouseY ());
	int sx = x, sy = y;
	bool snapped = floor->snap (&sx, &sy, closeness ());
	lit = canvas.hovered () && snapped && ! canvas.animating ();
	litX = sx;
	litY = sy;
	keepSelection ();

	if ((dragging || boxing) && ImGui::IsKeyPressed (ImGuiKey_Escape)) {
		dragging = boxing = false;
	} else if (tool == TOOL_DRAW && floor->drawing () && ! ImGui::GetIO ().WantTextInput) {
		// While we draw a polygon, Backspace takes back its last vertex and
		// Esc all of it.
		if (ImGui::Shortcut (ImGuiKey_Backspace, ImGuiInputFlags_Repeat)) floor->removeLastVertex ();
		else if (ImGui::Shortcut (ImGuiKey_Escape)) floor->cancelDrawing ();
	} else if (tool == TOOL_MOVE && ! ImGui::GetIO ().WantTextInput) {
		// Delete, or the Delete key of a Mac (Backspace), deletes the
		// selected vertices. We ask for the keys in every frame, as a
		// shortcut goes to who asked for it in the frame before.
		bool deleteKey = ImGui::Shortcut (ImGuiKey_Delete);
		bool backspace = ImGui::Shortcut (ImGuiKey_Backspace);
		if ((deleteKey || backspace) && ! dragging && ! boxing && ! selected.empty ()) {
			floor->deleteVertices (selected);
			selected.clear ();
		}
	}

	if (canvas.pressed ()) {
		bool shift = ImGui::GetIO ().KeyShift;
		switch (tool) {
			case TOOL_DRAW: {
				bool wasDrawing = floor->drawing ();
				if (floor->addVertex (sx, sy) == FloorDocument::ALREADY_USED) {
					say ("That vertex is in this polygon already, and isn't its first.");
				} else if (wasDrawing && ! floor->drawing ()) {
					// The polygon is complete.
					tool = TOOL_MOVE;
				}
				break;
			}
			case TOOL_DELETE:
				if (snapped) floor->deleteVertex (sx, sy);
				break;
			case TOOL_MOVE:
				if (snapped) {
					// A vertex joins the selection, or takes its place, and
					// we drag all of the selection from it.
					FloorPoint point (sx, sy);
					if (! hasPoint (selected, point)) {
						if (! shift) selected.clear ();
						selected.push_back (point);
					}
					dragging = true;
					fromX = toX = sx;
					fromY = toY = sy;
					pressX = x;
					pressY = y;
				} else {
					// A box selects the vertices in it.
					if (! shift) selected.clear ();
					boxing = true;
					boxX1 = boxX2 = x;
					boxY1 = boxY2 = y;
				}
				break;
			default:
				// The other tools drag from a vertex.
				if (snapped) {
					dragging = true;
					fromX = toX = sx;
					fromY = toY = sy;
					pressX = x;
					pressY = y;
				}
				break;
		}
	}

	if (boxing) {
		boxX2 = x;
		boxY2 = y;
		if (canvas.released ()) {
			boxing = false;
			int left = boxX1 < boxX2 ? boxX1 : boxX2, right = boxX1 < boxX2 ? boxX2 : boxX1;
			int top = boxY1 < boxY2 ? boxY1 : boxY2, bottom = boxY1 < boxY2 ? boxY2 : boxY1;
			const Floor & f = floor->floor ();
			for (size_t p = 0; p < f.polygons.size (); p ++) {
				for (size_t v = 0; v < f.polygons[p].size (); v ++) {
					const FloorPoint & point = f.polygons[p][v];
					if (point.x >= left && point.x <= right && point.y >= top && point.y <= bottom
							&& ! hasPoint (selected, point)) {
						selected.push_back (point);
					}
				}
			}
		}
	}

	if (dragging) {
		bool several = tool == TOOL_MOVE && selected.size () > 1;
		// The end goes as far as the mouse from where it was pressed, so
		// that a click doesn't move a vertex; it snaps to another vertex,
		// unless we move several.
		if (! several && snapped && (sx != fromX || sy != fromY)) {
			toX = sx;
			toY = sy;
		} else {
			toX = fromX + x - pressX;
			toY = fromY + y - pressY;
			if (! several) {
				if (toX < 0) toX = 0;
				if (toY < 0) toY = 0;
			}
		}
		if (canvas.released ()) {
			dragging = false;
			if (several) {
				int dx, dy;
				selectionOffset (&dx, &dy);
				if (dx || dy) {
					if (floor->moveVertices (selected, dx, dy)) {
						for (size_t i = 0; i < selected.size (); i ++) {
							selected[i].x += dx;
							selected[i].y += dy;
						}
					} else {
						say ("A vertex would land on another vertex of its polygon.");
					}
				}
			} else if (toX != fromX || toY != fromY) {
				switch (tool) {
					case TOOL_MOVE:
						if (floor->moveVertex (fromX, fromY, toX, toY)) {
							selected.assign (1, FloorPoint (toX, toY));
						} else {
							say ("A polygon with this vertex has one there already.");
						}
						break;
					case TOOL_SPLIT_LINE:
						if (! floor->splitLine (fromX, fromY, toX, toY)) {
							say ("Drag from one end of a line to its other end to split it.");
						}
						break;
					case TOOL_SPLIT_FLOOR:
						if (! floor->splitFloor (fromX, fromY, toX, toY)) {
							say ("Drag between two vertices of one polygon, that no line joins, to split it.");
						}
						break;
					default:
						break;
				}
			}
		}
	}

	if (canvas.hovered () && lit && tool != TOOL_DRAW) ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
}

void FloorView::paint (Draw2D & draw) {
	// While the user moves vertices, we show the floor as it will be, and
	// the selection where it goes.
	const Floor & current = floor->floor ();
	Floor shown = current;
	std::vector<FloorPoint> marked;
	if (tool == TOOL_MOVE) marked = selected;
	if (dragging && tool == TOOL_MOVE) {
		int dx = toX - fromX, dy = toY - fromY;
		if (selected.size () > 1) {
			selectionOffset (&dx, &dy);
			shown = floor->movedFloor (selected, dx, dy);
		} else {
			shown = floor->movedFloor (fromX, fromY, toX, toY);
		}
		if (shown != current) {
			for (size_t i = 0; i < marked.size (); i ++) {
				if (selected.size () > 1) {
					marked[i].x += dx;
					marked[i].y += dy;
				} else {
					marked[i] = FloorPoint (toX, toY);
				}
			}
		}
	}
	std::vector<int> markedXY;
	for (size_t i = 0; i < marked.size (); i ++) {
		markedXY.push_back (marked[i].x);
		markedXY.push_back (marked[i].y);
	}
	polyList * list = shown.toPolyList ();
	struct floorMakerView view;
	view.backdrop = &backdrop;
	view.floor = list;
	view.r = colour[0];
	view.g = colour[1];
	view.b = colour[2];
	view.lit = lit;
	view.litX = litX;
	view.litY = litY;
	view.pointPixels = canvas.pixelsOfPoints (1.0f);
	view.selectedXY = markedXY.empty () ? NULL : markedXY.data ();
	view.selectedCount = (int) marked.size ();
	view.boxing = boxing;
	view.boxX1 = boxX1;
	view.boxY1 = boxY1;
	view.boxX2 = boxX2;
	view.boxY2 = boxY2;
	// The line of a drag, or from the last vertex to the cursor while we
	// draw a polygon.
	view.selecting = false;
	if (dragging && tool != TOOL_MOVE) {
		view.selecting = true;
		view.selx1 = fromX;
		view.sely1 = fromY;
		view.selx2 = toX;
		view.sely2 = toY;
	} else if (tool == TOOL_DRAW && canvas.hovered () && floor->drawing ()) {
		const FloorPoint & last = floor->floor ().polygons[0][0];
		view.selecting = true;
		view.selx1 = last.x;
		view.sely1 = last.y;
		view.selx2 = lit ? litX : (int) floorf (canvas.mouseX ());
		view.sely2 = lit ? litY : (int) floorf (canvas.mouseY ());
	}
	drawFloorMakerView (draw, &view);
	Floor::freePolyList (list);
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
	canvas.draw (size, [this] (Draw2D & draw) { paint (draw); });
	handleMouse ();

	ImGui::TextDisabled ("%s", canvas.status ().c_str ());
	if (! notice.empty () && ImGui::GetTime () < noticeUntil) {
		ImGui::SameLine (0, ImGui::GetFontSize () * 2);
		ImGui::TextColored (ImVec4 (1.0f, 0.8f, 0.3f, 1.0f), "%s", notice.c_str ());
	}
}

void FloorView::drawInspector () {
	ImGui::SeparatorText ("Tool");
	for (int t = 0; t < TOOL_COUNT; t ++) {
		char label[64];
		snprintf (label, sizeof (label), "%s  (%d)", tools[t].name, t + 1);
		if (ImGui::RadioButton (label, tool == t)) tool = (Tool) t;
	}
	ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
	ImGui::TextWrapped ("%s", tools[tool].hint);
	ImGui::PopStyleColor ();

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

	ImGui::SeparatorText ("Floor");
	const Floor & f = floor->floor ();
	int polygons = 0;
	for (size_t p = 0; p < f.polygons.size (); p ++) {
		if (! f.polygons[p].empty ()) polygons ++;
	}
	int vertices = f.vertexCount ();
	ImGui::Text ("%d polygon%s, %d vert%s", polygons, polygons == 1 ? "" : "s", vertices, vertices == 1 ? "ex" : "ices");
	int concave = f.concaveCount ();
	if (concave) {
		ImGui::TextColored (ImVec4 (1.0f, 0.45f, 0.4f, 1.0f), concave == 1 ? "1 polygon isn't convex." : "%d polygons aren't convex.", concave);
		ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
		ImGui::TextWrapped ("Characters can't walk well on a polygon that isn't convex. They are red on the canvas.");
		ImGui::PopStyleColor ();
		if (ImGui::Button ("Split into Convex Polygons")) {
			if (! floor->fixConcave ()) say ("A polygon is too complicated to split. Split it by hand with Split Floor.");
		}
	}
}

} // namespace devkit
