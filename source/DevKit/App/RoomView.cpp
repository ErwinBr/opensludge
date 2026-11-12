#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <algorithm>

#include "imgui.h"
#include "imgui_stdlib.h"

#include "draw2d.h"

#include "FileMemory.h"
#include "ProjectCheck.h"
#include "ProjectDocument.h"
#include "Properties.h"
#include "RoomView.h"
#include "ScriptIndex.h"
#include "WorkspaceActions.h"

namespace devkit {

// The scene without a project or setSceneDimensions to say how big it is.
static const int DEFAULT_WIDTH = 640, DEFAULT_HEIGHT = 480;
// How close, in points, the cursor must be to a handle to take it.
static const float HANDLE_POINTS = 6.0f;
static const double NOTICE_SECONDS = 5.0;
// How often we look whether the pictures and floors changed on disk.
static const double FILE_CHECK_SECONDS = 1.0;

// The time of a file that we must look at again.
static const int64_t STALE = -2;

static const char * const ROOM_KEY = "room.sub";
static const char * const FLOOR_KEY = "room.floor";

struct ToolInfo {
	const char * name;
	ImGuiKey key;
	const char * hint;
};

static const ToolInfo tools[RoomView::TOOL_COUNT] = {
	{ "Select", ImGuiKey_1, "Select and move regions" },
	{ "Rectangle", ImGuiKey_2, "Draw a rectangle" },
	{ "Shape", ImGuiKey_3, "Draw a shape, a point at a time" },
};

// The directions of the Inspector: -1 keeps the way the character faces.
static const int directions[] = { -1, 0, 45, 90, 135, 180, 225, 270, 315 };
static const int DIRECTION_COUNT = sizeof (directions) / sizeof (directions[0]);

static int64_t fileTime (const std::string & path) {
	struct stat info;
	if (stat (path.c_str (), &info) != 0) return -1;
	return (int64_t) info.st_mtime;
}

RoomView::FloorFile::FloorFile () : time (STALE), loaded (false) {}

RoomView::Picture::Picture () : time (STALE), loaded (false) {
	memset (&bank, 0, sizeof (bank));
	bank.type = 2;
	reserveSpritePal (&bank.myPalette, 0);
	addSprite (0, &bank);
}

RoomView::RoomView (ScriptDocument * s, WorkspaceActions & a, const CanvasGraphics & g, FileMemory & m)
	: script (s), actions (a), graphics (g), memory (m), canvas (g), roomsVersion (0), read (false), roomIndex (-1),
	  nextFileCheck (0), showFloor (true), sceneWidth (DEFAULT_WIDTH), sceneHeight (DEFAULT_HEIGHT),
	  tool (TOOL_SELECT), selectedRegion (-1), selectedPoint (-1), selectPending (false),
	  drag (DRAG_NONE), dragHandle (-1, -1), pressX (0), pressY (0), mouseX (0), mouseY (0),
	  newPending (false), newShape (false), openNewPopup (false), newChoice (0), focusNewName (false),
	  fieldsEditing (false), noticeUntil (0) {
	fieldTopLeft[0] = fieldTopLeft[1] = fieldBottomRight[0] = fieldBottomRight[1] = fieldStand[0] = fieldStand[1] = 0;
	if (! script->path ().empty ()) {
		roomName = memory.get (script->path (), ROOM_KEY);
		showFloor = memory.get (script->path (), FLOOR_KEY) != "off";
	}
}

RoomView::~RoomView () {
	for (std::map<std::string, std::unique_ptr<Picture> >::iterator p = pictures.begin (); p != pictures.end (); ++ p) {
		graphics.draw->forgetPictures (&p->second->bank);
		forgetSpriteBank (&p->second->bank);
	}
}

bool RoomView::animating () const {
	return drag != DRAG_NONE || ! drawnPoints.empty () || canvas.animating ();
}

void RoomView::say (const std::string & text) {
	notice = text;
	noticeUntil = ImGui::GetTime () + NOTICE_SECONDS;
}

// Read the rooms again when the text changed.
void RoomView::update () {
	const TextBuffer & text = script->buffer ();
	if (read && roomsVersion == text.version ()) return;
	read = true;
	roomsVersion = text.version ();
	int regionsBefore = roomIndex >= 0 ? (int) rooms[roomIndex].regions.size () : -1;
	int previousIndex = roomIndex;
	rooms = readRooms (text);
	roomIndex = -1;
	for (size_t r = 0; r < rooms.size (); r ++) {
		if (rooms[r].name == roomName) roomIndex = (int) r;
	}
	if (roomIndex < 0 && ! rooms.empty ()) {
		// Its sub has another name, as while the user types it: the room
		// in its place.
		roomIndex = previousIndex >= 0 && previousIndex < (int) rooms.size () ? previousIndex : 0;
		roomName = rooms[roomIndex].name;
	}
	const Room * shown = room ();
	if (selectPending && shown) {
		selectPending = false;
		selectedRegion = -1;
		for (size_t g = 0; g < shown->regions.size (); g ++) {
			if (shown->regions[g].call == selectAt) selectedRegion = (int) g;
		}
		selectedPoint = -1;
	} else if (! shown || (int) shown->regions.size () != regionsBefore) {
		// Undo, or typing, took regions away or added them: we can't tell
		// which the selection was.
		selectedRegion = selectedPoint = -1;
	}
	if (const RoomRegion * region = selected ()) {
		if (selectedPoint >= (int) region->values ().size () / 2 || ! region->shape) selectedPoint = -1;
	}
	if (drag != DRAG_NONE && ! selected () && drag != DRAG_RECTANGLE) drag = DRAG_NONE;
}

const Room * RoomView::room () const {
	return roomIndex >= 0 && roomIndex < (int) rooms.size () ? &rooms[roomIndex] : NULL;
}

const RoomRegion * RoomView::selected () const {
	const Room * r = room ();
	return r && selectedRegion >= 0 && selectedRegion < (int) r->regions.size () ? &r->regions[selectedRegion] : NULL;
}

void RoomView::chooseRoom (int index) {
	if (index < 0 || index >= (int) rooms.size () || index == roomIndex) return;
	roomIndex = index;
	roomName = rooms[index].name;
	selectedRegion = selectedPoint = -1;
	drag = DRAG_NONE;
	drawnPoints.clear ();
	if (! script->path ().empty ()) memory.set (script->path (), ROOM_KEY, roomName);
	canvas.zoomToFit ();
}

// A file of a script, as the compiler finds it: in the folder of the
// project, else in the folder of the script.
std::string RoomView::pathOf (const std::string & file) const {
	std::map<std::string, std::string>::const_iterator known = paths.find (file);
	if (known != paths.end ()) return known->second;
	std::string & path = paths[file];
	std::string name = file;
	std::replace (name.begin (), name.end (), '\\', '/');
	std::vector<std::string> folders;
	if (! actions.projectFile ().empty ()) folders.push_back (folderOf (actions.projectFile ()));
	if (! script->path ().empty ()) folders.push_back (folderOf (script->path ()));
	for (size_t f = 0; f < folders.size (); f ++) {
		bool found = false, caseDiffers = false;
		std::string onDisk = nameOnDisk (folders[f], name, &found, &caseDiffers);
		if (found) return path = folders[f] + onDisk;
	}
	return path = folders.empty () ? name : folders[0] + name;
}

RoomView::Picture * RoomView::pictureOf (const std::string & file) {
	std::string path = pathOf (file);
	std::unique_ptr<Picture> & picture = pictures[path];
	if (! picture) picture.reset (new Picture);
	if (picture->time != STALE) return picture->loaded ? picture.get () : NULL;
	int64_t time = fileTime (path);
	picture->time = time;
	picture->loaded = false;
	if (time < 0) return NULL;
	std::string extension = extensionOf (path);
	if (extension == "png") picture->loaded = loadSpriteFromPNG (path.c_str (), &picture->bank, 0);
	else if (extension == "tga") picture->loaded = loadSpriteFromTGA (path.c_str (), &picture->bank, 0);
	if (! picture->loaded) return NULL;
	// As the floor editor draws its backdrop: the other way up.
	sprite & s = picture->bank.sprites[0];
	if (s.height > 0) s.height = - s.height;
	spriteBankChanged (&picture->bank);
	return picture.get ();
}

const Floor * RoomView::floorOf (const std::string & file) {
	std::string path = pathOf (file);
	FloorFile & f = floors[path];
	if (f.time == STALE) {
		int64_t time = fileTime (path);
		f.time = time;
		std::string error;
		f.loaded = time >= 0 && FloorDocument::readFloor (path, &f.floor, &error);
	}
	return f.loaded ? &f.floor : NULL;
}

void RoomView::overlayPlace (const RoomOverlay & overlay, Picture * picture, int * x, int * y) const {
	const sprite & s = picture->bank.sprites[0];
	int w = s.width, h = s.height < 0 ? - s.height : s.height;
	// As loadBackDrop (Engine/backdrop.cpp): CENTRE centres in the scene.
	*x = overlay.x.value == ROOM_CENTRE ? (sceneWidth - w) >> 1 : overlay.x.value;
	*y = overlay.y.value == ROOM_CENTRE ? (sceneHeight - h) >> 1 : overlay.y.value;
}

void RoomView::sceneSize (int * width, int * height) {
	const Room * r = room ();
	if (r && r->sceneWidth > 0 && r->sceneHeight > 0) {
		*width = r->sceneWidth;
		*height = r->sceneHeight;
		return;
	}
	if (ProjectDocument * project = dynamic_cast<ProjectDocument *> (actions.findDocument (actions.projectFile ()))) {
		if (project->settings ().width > 0 && project->settings ().height > 0) {
			*width = project->settings ().width;
			*height = project->settings ().height;
			return;
		}
	}
	// As big as the pictures that aren't centred.
	*width = *height = 0;
	if (r) {
		for (size_t o = 0; o < r->overlays.size (); o ++) {
			const RoomOverlay & overlay = r->overlays[o];
			Picture * picture = pictureOf (overlay.file);
			if (! picture || ! overlay.x.literal || ! overlay.y.literal) continue;
			const sprite & s = picture->bank.sprites[0];
			int x = overlay.x.value == ROOM_CENTRE ? 0 : overlay.x.value, y = overlay.y.value == ROOM_CENTRE ? 0 : overlay.y.value;
			*width = std::max (*width, x + s.width);
			*height = std::max (*height, y - s.height);
		}
	}
	if (*width <= 0 || *height <= 0) {
		*width = DEFAULT_WIDTH;
		*height = DEFAULT_HEIGHT;
	}
}

int RoomView::clampX (int x) const {
	return x < 0 ? 0 : x > sceneWidth ? sceneWidth : x;
}

int RoomView::clampY (int y) const {
	return y < 0 ? 0 : y > sceneHeight ? sceneHeight : y;
}

float RoomView::closeness () const {
	return canvas.pixelsOfPoints (HANDLE_POINTS);
}

std::vector<std::pair<int, int> > RoomView::handlesOf (const RoomRegion & region) const {
	std::vector<std::pair<int, int> > handles;
	if (region.shape) {
		int n = (int) region.coordinates.size () / 2;
		for (int p = 0; p < n; p ++) handles.push_back (std::make_pair (p * 2, p * 2 + 1));
	} else {
		// The corners, then the middles of the edges.
		int corners[][2] = { { 0, 1 }, { 2, 1 }, { 2, 3 }, { 0, 3 }, { -1, 1 }, { 2, -1 }, { -1, 3 }, { 0, -1 } };
		for (int c = 0; c < 8; c ++) handles.push_back (std::make_pair (corners[c][0], corners[c][1]));
	}
	return handles;
}

void RoomView::handlePlace (const std::vector<int> & v, const std::pair<int, int> & handle, float * x, float * y) const {
	*x = handle.first >= 0 ? (float) v[handle.first] : (v[0] + v[2]) * 0.5f;
	*y = handle.second >= 0 ? (float) v[handle.second] : (v[1] + v[3]) * 0.5f;
}

std::vector<int> RoomView::shownValues (int index) const {
	const Room * r = room ();
	std::vector<int> v = r->regions[index].values ();
	if (index != selectedRegion) return v;
	if (drag == DRAG_REGION) {
		int dx = mouseX - pressX, dy = mouseY - pressY;
		// The whole region stays in the scene.
		int minX = sceneWidth, maxX = 0, minY = sceneHeight, maxY = 0;
		for (size_t c = 0; c + 1 < v.size (); c += 2) {
			minX = std::min (minX, v[c]);
			maxX = std::max (maxX, v[c]);
			minY = std::min (minY, v[c + 1]);
			maxY = std::max (maxY, v[c + 1]);
		}
		if (minX + dx < 0) dx = - minX;
		if (maxX + dx > sceneWidth) dx = sceneWidth - maxX;
		if (minY + dy < 0) dy = - minY;
		if (maxY + dy > sceneHeight) dy = sceneHeight - maxY;
		for (size_t c = 0; c < v.size (); c ++) v[c] += c % 2 ? dy : dx;
	} else if (drag == DRAG_HANDLE) {
		if (dragHandle.first >= 0) v[dragHandle.first] = clampX (mouseX);
		if (dragHandle.second >= 0) v[dragHandle.second] = clampY (mouseY);
	}
	return v;
}

void RoomView::standShown (int index, int * x, int * y) const {
	const RoomRegion & region = room ()->regions[index];
	*x = region.standX.value;
	*y = region.standY.value;
	if (index == selectedRegion && drag == DRAG_STAND) {
		*x = clampX (mouseX);
		*y = clampY (mouseY);
	}
}

// The region under a point: the one added last, as in the game.
int RoomView::regionAt (float x, float y) const {
	const Room * r = room ();
	if (! r) return -1;
	for (int g = (int) r->regions.size () - 1; g >= 0; g --) {
		if (r->regions[g].contains (x, y)) return g;
	}
	return -1;
}

void RoomView::apply (const std::vector<TextChange> & changes, const std::string & label, TextPos at) {
	if (changes.empty ()) return;
	script->edit (changes, TextSelection (at), EDIT_OTHER, label);
}

void RoomView::drawToolbar () {
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 12);
	const Room * r = room ();
	if (ImGui::BeginCombo ("##room", r ? r->name.c_str () : "")) {
		for (size_t i = 0; i < rooms.size (); i ++) {
			ImGui::PushID ((int) i);
			if (ImGui::Selectable (rooms[i].name.c_str (), (int) i == roomIndex)) chooseRoom ((int) i);
			ImGui::PopID ();
		}
		ImGui::EndCombo ();
	}
	ImGui::SetItemTooltip ("Room");
	ImGui::SameLine ();
	ImGui::TextDisabled ("|");
	for (int t = 0; t < TOOL_COUNT; t ++) {
		ImGui::SameLine ();
		bool current = tool == t;
		if (current) ImGui::PushStyleColor (ImGuiCol_Button, ImGui::GetStyleColorVec4 (ImGuiCol_ButtonActive));
		if (ImGui::Button (tools[t].name)) {
			tool = (Tool) t;
			drawnPoints.clear ();
		}
		if (current) ImGui::PopStyleColor ();
		ImGui::SetItemTooltip ("%s (%d)", tools[t].hint, t + 1);
	}
	ImGui::SameLine ();
	ImGui::TextDisabled ("|");
	ImGui::SameLine ();
	canvas.drawZoomButtons ();
	ImGui::SameLine ();
	ImGui::TextDisabled ("|");
	ImGui::SameLine ();
	if (ImGui::Checkbox ("Floor", &showFloor) && ! script->path ().empty ()) {
		memory.set (script->path (), FLOOR_KEY, showFloor ? "" : "off");
	}

	if (! ImGui::GetIO ().WantTextInput && ImGui::IsWindowFocused (ImGuiFocusedFlags_RootAndChildWindows)) {
		for (int t = 0; t < TOOL_COUNT; t ++) {
			if (ImGui::Shortcut (tools[t].key) && tool != t) {
				tool = (Tool) t;
				drawnPoints.clear ();
			}
		}
	}
}

void RoomView::finishShape () {
	if (drawnPoints.size () >= 6) {
		newPending = openNewPopup = true;
		newShape = true;
		newCoordinates = drawnPoints;
	}
	drawnPoints.clear ();
}

void RoomView::handleKeys () {
	if (ImGui::GetIO ().WantTextInput || newPending || ! ImGui::IsWindowFocused (ImGuiFocusedFlags_RootAndChildWindows)) return;
	if (! drawnPoints.empty ()) {
		if (ImGui::Shortcut (ImGuiKey_Escape)) drawnPoints.clear ();
		else if (ImGui::Shortcut (ImGuiKey_Enter) || ImGui::Shortcut (ImGuiKey_KeypadEnter)) finishShape ();
		else if (ImGui::Shortcut (ImGuiKey_Backspace, ImGuiInputFlags_Repeat)) drawnPoints.resize (drawnPoints.size () - 2);
		return;
	}
	if (drag != DRAG_NONE) {
		if (ImGui::Shortcut (ImGuiKey_Escape)) drag = DRAG_NONE;
		return;
	}
	const RoomRegion * region = selected ();
	if (ImGui::Shortcut (ImGuiKey_Escape)) selectedRegion = selectedPoint = -1;
	// Delete, or the Delete key of a Mac (Backspace): the selected point,
	// else the region.
	bool deleteKey = ImGui::Shortcut (ImGuiKey_Delete);
	bool backspace = ImGui::Shortcut (ImGuiKey_Backspace);
	if (! region || ! (deleteKey || backspace)) return;
	if (selectedPoint >= 0 && region->shape && region->editable) {
		std::vector<int> v = region->values ();
		if (v.size () <= 6) {
			say ("A shape needs 3 points");
			return;
		}
		v.erase (v.begin () + selectedPoint * 2, v.begin () + selectedPoint * 2 + 2);
		apply (roomEdit::setPoints (*region, v), "Delete Point", region->call);
		selectedPoint = -1;
	} else {
		apply (roomEdit::remove (script->buffer (), *region), "Delete Region", region->call);
		selectedRegion = selectedPoint = -1;
	}
}

void RoomView::finishDrag () {
	const RoomRegion * region = selected ();
	Drag was = drag;
	// The values as the drag leaves them, before it ends.
	std::vector<int> v = region ? shownValues (selectedRegion) : std::vector<int> ();
	drag = DRAG_NONE;
	if (was == DRAG_RECTANGLE) {
		int x1 = clampX (std::min (pressX, mouseX)), x2 = clampX (std::max (pressX, mouseX));
		int y1 = clampY (std::min (pressY, mouseY)), y2 = clampY (std::max (pressY, mouseY));
		if (x2 - x1 >= 2 && y2 - y1 >= 2) {
			newPending = openNewPopup = true;
			newShape = false;
			newCoordinates.clear ();
			newCoordinates.push_back (x1);
			newCoordinates.push_back (y1);
			newCoordinates.push_back (x2);
			newCoordinates.push_back (y2);
		}
		return;
	}
	if (! region || (mouseX == pressX && mouseY == pressY)) return;
	if (was == DRAG_STAND) {
		apply (roomEdit::setStandPoint (*region, clampX (mouseX), clampY (mouseY)), "Move Walk-To Point", region->call);
		return;
	}
	if (region->shape) {
		apply (roomEdit::setPoints (*region, v), was == DRAG_REGION ? "Move Region" : "Move Point", region->call);
	} else {
		// The left and top stay the smaller, or the game finds nothing in it.
		apply (roomEdit::setRectangle (*region, std::min (v[0], v[2]), std::min (v[1], v[3]), std::max (v[0], v[2]), std::max (v[1], v[3])),
			was == DRAG_REGION ? "Move Region" : "Resize Region", region->call);
	}
}

void RoomView::handleMouse () {
	float fx = canvas.mouseX (), fy = canvas.mouseY ();
	mouseX = (int) floorf (fx);
	mouseY = (int) floorf (fy);
	float near = closeness ();
	const Room * r = room ();
	if (! r || newPending) return;

	if (tool == TOOL_SELECT && canvas.hovered () && drag == DRAG_NONE && ! canvas.animating ()) {
		// The pointer over what a drag would take.
		bool overHandle = false;
		if (const RoomRegion * region = selected ()) {
			if (region->editable) {
				std::vector<int> v = region->values ();
				std::vector<std::pair<int, int> > handles = handlesOf (*region);
				for (size_t h = 0; h < handles.size () && ! overHandle; h ++) {
					float hx, hy;
					handlePlace (v, handles[h], &hx, &hy);
					overHandle = fabsf (fx - hx) <= near && fabsf (fy - hy) <= near;
				}
			}
			if (region->walks () && fabsf (fx - region->standX.value) <= near && fabsf (fy - region->standY.value) <= near) overHandle = true;
		}
		if (overHandle) ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
		else if (regionAt (fx, fy) >= 0) ImGui::SetMouseCursor (ImGuiMouseCursor_ResizeAll);
	}

	if (canvas.pressed ()) {
		pressX = mouseX;
		pressY = mouseY;
		switch (tool) {
			case TOOL_SELECT: {
				const RoomRegion * region = selected ();
				// A double-click on an edge of a shape adds a point there.
				if (region && region->shape && region->editable && ImGui::IsMouseDoubleClicked (ImGuiMouseButton_Left)) {
					std::vector<int> v = region->values ();
					int n = (int) v.size () / 2;
					for (int p = 0; p < n; p ++) {
						float x1 = (float) v[p * 2], y1 = (float) v[p * 2 + 1];
						float x2 = (float) v[(p + 1) % n * 2], y2 = (float) v[(p + 1) % n * 2 + 1];
						float dx = x2 - x1, dy = y2 - y1, length = dx * dx + dy * dy;
						float t = length > 0 ? ((fx - x1) * dx + (fy - y1) * dy) / length : 0;
						if (t <= 0 || t >= 1) continue;
						float px = x1 + t * dx - fx, py = y1 + t * dy - fy;
						if (px * px + py * py > near * near) continue;
						v.insert (v.begin () + (p + 1) * 2, clampY (mouseY));
						v.insert (v.begin () + (p + 1) * 2, clampX (mouseX));
						apply (roomEdit::setPoints (*region, v), "Add Point", region->call);
						selectedPoint = p + 1;
						return;
					}
				}
				if (region) {
					if (region->walks () && region->standX.literal && fabsf (fx - region->standX.value) <= near && fabsf (fy - region->standY.value) <= near) {
						drag = DRAG_STAND;
						return;
					}
					if (region->editable) {
						std::vector<int> v = region->values ();
						std::vector<std::pair<int, int> > handles = handlesOf (*region);
						for (size_t h = 0; h < handles.size (); h ++) {
							float hx, hy;
							handlePlace (v, handles[h], &hx, &hy);
							if (fabsf (fx - hx) <= near && fabsf (fy - hy) <= near) {
								drag = DRAG_HANDLE;
								dragHandle = handles[h];
								selectedPoint = region->shape ? (int) h : -1;
								return;
							}
						}
					}
				}
				int hit = regionAt (fx, fy);
				if (hit != selectedRegion) selectedPoint = -1;
				selectedRegion = hit;
				if (hit >= 0) {
					if (r->regions[hit].editable) drag = DRAG_REGION;
					else say (r->regions[hit].whyNot);
				}
				break;
			}
			case TOOL_RECTANGLE:
				drag = DRAG_RECTANGLE;
				break;
			case TOOL_SHAPE: {
				int x = clampX (mouseX), y = clampY (mouseY);
				// A click on the first point closes the shape.
				if (drawnPoints.size () >= 6 && fabsf (fx - drawnPoints[0]) <= near && fabsf (fy - drawnPoints[1]) <= near) {
					finishShape ();
				} else {
					drawnPoints.push_back (x);
					drawnPoints.push_back (y);
				}
				break;
			}
			default:
				break;
		}
	}
	if (drag != DRAG_NONE && canvas.released ()) finishDrag ();
}

void RoomView::paint (Draw2D & draw) {
	draw.clear ();
	const float scene[] = { 0, 0, (float) sceneWidth, 0, (float) sceneWidth, (float) sceneHeight, 0, (float) sceneHeight };
	draw.setDrawingColour (0.12f, 0.12f, 0.13f, 1.0f);
	draw.drawPolygon (scene, 4);
	const Room * r = room ();
	if (! r) {
		draw.finish ();
		return;
	}
	for (size_t o = 0; o < r->overlays.size (); o ++) {
		const RoomOverlay & overlay = r->overlays[o];
		if (! overlay.x.literal || ! overlay.y.literal) continue;
		Picture * picture = pictureOf (overlay.file);
		if (! picture) continue;
		int x, y;
		overlayPlace (overlay, picture, &x, &y);
		const sprite & s = picture->bank.sprites[0];
		draw.setDrawingColour (1.0f, 1.0f, 1.0f, overlay.conditional ? 0.6f : 1.0f);
		draw.drawPicture (&picture->bank, 0, false, (float) x, (float) y, (float) (x + s.width), (float) (y - s.height), 0, 0, 1, 1);
	}
	draw.setDrawingColour (0.5f, 0.5f, 0.55f, 1.0f);
	draw.drawLineLoop (scene, 4);

	if (showFloor && ! r->floor.empty ()) {
		if (const Floor * floor = floorOf (r->floor)) {
			draw.setDrawingColour (0.45f, 0.75f, 1.0f, 0.55f);
			for (size_t p = 0; p < floor->polygons.size (); p ++) {
				const FloorPolygon & polygon = floor->polygons[p];
				std::vector<float> points;
				for (size_t v = 0; v < polygon.size (); v ++) {
					points.push_back ((float) polygon[v].x);
					points.push_back ((float) polygon[v].y);
				}
				if (points.size () >= 4) draw.drawLineLoop (points.data (), (int) points.size () / 2);
			}
		}
	}

	float handleSize = canvas.pixelsOfPoints (3.5f);
	int hovered = drag == DRAG_NONE && tool == TOOL_SELECT && canvas.hovered () ? regionAt (canvas.mouseX (), canvas.mouseY ()) : -1;
	for (int g = 0; g < (int) r->regions.size (); g ++) {
		const RoomRegion & region = r->regions[g];
		std::vector<int> v = shownValues (g);
		RoomRegion shown = region;
		for (size_t c = 0; c < shown.coordinates.size () && c < v.size (); c ++) shown.coordinates[c].value = v[c];
		std::vector<int> outline = shown.outline ();
		if (outline.size () < 6) continue;
		std::vector<float> points (outline.begin (), outline.end ());
		bool isSelected = g == selectedRegion;
		float alpha = region.conditional ? 0.6f : 1.0f;
		if (isSelected && ! region.shape) {
			draw.setDrawingColour (1.0f, 0.8f, 0.2f, 0.18f);
			draw.drawPolygon (points.data (), 4);
		}
		if (isSelected) draw.setDrawingColour (1.0f, 0.8f, 0.2f, 1.0f);
		else if (! region.editable) draw.setDrawingColour (0.65f, 0.65f, 0.65f, 0.8f * alpha);
		else if (g == hovered) draw.setDrawingColour (0.7f, 1.0f, 0.75f, alpha);
		else draw.setDrawingColour (0.35f, 0.95f, 0.45f, alpha);
		draw.drawLineLoop (points.data (), (int) points.size () / 2);

		if (isSelected && region.editable) {
			std::vector<std::pair<int, int> > handles = handlesOf (region);
			for (size_t h = 0; h < handles.size (); h ++) {
				float hx, hy;
				handlePlace (v, handles[h], &hx, &hy);
				const float square[] = { hx - handleSize, hy - handleSize, hx + handleSize, hy - handleSize, hx + handleSize, hy + handleSize, hx - handleSize, hy + handleSize };
				if (region.shape && (int) h == selectedPoint) draw.setDrawingColour (1.0f, 0.3f, 0.2f, 1.0f);
				else draw.setDrawingColour (1.0f, 0.8f, 0.2f, 1.0f);
				draw.drawPolygon (square, 4);
			}
		}

		// Where a character walks to: a cross, and the way it faces.
		int sx, sy;
		standShown (g, &sx, &sy);
		if ((sx || sy) && region.standX.literal && region.standY.literal) {
			float arm = canvas.pixelsOfPoints (isSelected ? 6.0f : 4.0f);
			draw.setDrawingColour (0.45f, 0.8f, 1.0f, (isSelected ? 1.0f : 0.7f) * alpha);
			const float cross[] = { sx - arm, (float) sy, sx + arm, (float) sy, (float) sx, sy - arm, (float) sx, sy + arm };
			draw.drawLines (cross, 4);
			if (region.direction.literal && region.direction.value >= 0) {
				float angle = region.direction.value * 3.14159265f / 180.0f, length = arm * 2.5f;
				const float facing[] = { (float) sx, (float) sy, sx + sinf (angle) * length, sy - cosf (angle) * length };
				draw.drawLines (facing, 2);
			}
			if (isSelected) {
				// From the middle of the region to its walk-to point.
				float cx = 0, cy = 0;
				int n = (int) outline.size () / 2;
				for (int p = 0; p < n; p ++) {
					cx += outline[p * 2];
					cy += outline[p * 2 + 1];
				}
				draw.setDrawingColour (0.45f, 0.8f, 1.0f, 0.35f);
				const float line[] = { cx / n, cy / n, (float) sx, (float) sy };
				draw.drawLines (line, 2);
			}
		}
	}

	// What the user draws.
	draw.setDrawingColour (1.0f, 0.8f, 0.2f, 1.0f);
	if (drag == DRAG_RECTANGLE) {
		float x1 = (float) clampX (pressX), y1 = (float) clampY (pressY), x2 = (float) clampX (mouseX), y2 = (float) clampY (mouseY);
		const float box[] = { x1, y1, x2, y1, x2, y2, x1, y2 };
		draw.drawLineLoop (box, 4);
	}
	if (! drawnPoints.empty ()) {
		std::vector<float> points (drawnPoints.begin (), drawnPoints.end ());
		if (canvas.hovered ()) {
			points.push_back ((float) clampX (mouseX));
			points.push_back ((float) clampY (mouseY));
		}
		std::vector<float> lines;
		for (size_t p = 0; p + 3 < points.size (); p += 2) {
			lines.insert (lines.end (), points.begin () + p, points.begin () + p + 4);
		}
		if (! lines.empty ()) draw.drawLines (lines.data (), (int) lines.size () / 2);
		const float first[] = { points[0] - handleSize, points[1] - handleSize, points[0] + handleSize, points[1] - handleSize,
			points[0] + handleSize, points[1] + handleSize, points[0] - handleSize, points[1] + handleSize };
		draw.drawPolygon (first, 4);
	}
	draw.finish ();
}

// The names of the regions' objectTypes, over the canvas.
void RoomView::drawLabels () {
	const Room * r = room ();
	if (! r) return;
	ImDrawList * list = ImGui::GetWindowDrawList ();
	list->PushClipRect (canvas.viewMin (), canvas.viewMax (), true);
	for (int g = 0; g < (int) r->regions.size (); g ++) {
		const RoomRegion & region = r->regions[g];
		std::vector<int> v = shownValues (g);
		if (v.size () < 2) continue;
		int minX = v[0], minY = v[1];
		for (size_t c = 0; c + 1 < v.size (); c += 2) {
			minX = std::min (minX, v[c]);
			minY = std::min (minY, v[c + 1]);
		}
		ImVec2 at = canvas.toWindow ((float) minX, (float) minY);
		at.x += 3;
		at.y += 2;
		ImU32 colour = g == selectedRegion ? IM_COL32 (255, 205, 50, 255) : region.editable ? IM_COL32 (110, 245, 130, 230) : IM_COL32 (170, 170, 170, 220);
		const char * name = region.objectType.c_str ();
		ImVec2 size = ImGui::CalcTextSize (name);
		list->AddRectFilled (ImVec2 (at.x - 2, at.y - 1), ImVec2 (at.x + size.x + 2, at.y + size.y + 1), IM_COL32 (0, 0, 0, 150), 2.0f);
		list->AddText (at, colour, name);
	}
	list->PopClipRect ();
}

// The window that asks which objectType a new region is of.
void RoomView::drawNewRegion () {
	static const char * const TITLE = "New Region";
	if (! newPending) return;
	const ScriptIndex & index = actions.scriptIndex ();
	std::vector<std::string> types;
	std::vector<const ScriptSymbol *> symbols = index.allSymbols ();
	for (size_t s = 0; s < symbols.size (); s ++) {
		if (symbols[s]->kind == SYMBOL_OBJECT_TYPE) types.push_back (symbols[s]->name);
	}
	std::sort (types.begin (), types.end ());
	types.erase (std::unique (types.begin (), types.end ()), types.end ());
	if (openNewPopup) {
		ImGui::OpenPopup (TITLE);
		openNewPopup = false;
		newName.clear ();
		// The objectType of the last new region, else a new one.
		newChoice = (int) types.size ();
		for (size_t t = 0; t < types.size (); t ++) {
			if (types[t] == lastType) newChoice = (int) t;
		}
		focusNewName = newChoice == (int) types.size ();
	}
	if (newChoice > (int) types.size ()) newChoice = (int) types.size ();

	bool open = true;
	ImGui::SetNextWindowPos (ImGui::GetMainViewport ()->GetCenter (), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));
	if (! ImGui::BeginPopupModal (TITLE, &open, ImGuiWindowFlags_AlwaysAutoResize)) {
		newPending = false;
		return;
	}
	const char * NEW_TYPE = "New Object Type";
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 14);
	if (ImGui::BeginCombo ("Object Type", newChoice < (int) types.size () ? types[newChoice].c_str () : NEW_TYPE)) {
		for (size_t t = 0; t < types.size (); t ++) {
			if (ImGui::Selectable (types[t].c_str (), (int) t == newChoice)) newChoice = (int) t;
		}
		ImGui::Separator ();
		if (ImGui::Selectable (NEW_TYPE, newChoice == (int) types.size ())) {
			newChoice = (int) types.size ();
			focusNewName = true;
		}
		ImGui::EndCombo ();
	}
	bool creating = newChoice == (int) types.size ();
	bool entered = false;
	std::string problem;
	if (creating) {
		if (focusNewName) {
			ImGui::SetKeyboardFocusHere ();
			focusNewName = false;
		}
		ImGui::SetNextItemWidth (ImGui::GetFontSize () * 14);
		entered = ImGui::InputText ("Name", &newName, ImGuiInputTextFlags_EnterReturnsTrue);
		if (! newName.empty ()) {
			if (! isIdentifier (newName)) problem = "Not a valid name";
			else if (! index.find (newName).empty ()) problem = "Name in use";
		}
		if (! problem.empty ()) ImGui::TextColored (ImVec4 (1.0f, 0.45f, 0.4f, 1.0f), "%s", problem.c_str ());
	}
	bool ready = creating ? ! newName.empty () && problem.empty () : newChoice < (int) types.size ();
	// Enter takes the objectType that is chosen.
	if (! creating && ! ImGui::IsAnyItemActive () && (ImGui::IsKeyPressed (ImGuiKey_Enter) || ImGui::IsKeyPressed (ImGuiKey_KeypadEnter))) entered = true;
	ImGui::BeginDisabled (! ready);
	bool add = ImGui::Button ("Add Region") || (entered && ready);
	ImGui::EndDisabled ();
	ImGui::SameLine ();
	bool cancel = ImGui::Button ("Cancel") || (ImGui::IsKeyPressed (ImGuiKey_Escape) && ! ImGui::IsAnyItemActive ());
	const Room * r = room ();
	if (add && ready && r) {
		std::string name = creating ? newName : types[newChoice];
		TextPos where;
		std::vector<TextChange> changes = roomEdit::add (script->buffer (), *r, script->indentUnit (), name, creating, newShape, newCoordinates, 0, 0, -1, &where);
		apply (changes, "Add Region", where);
		selectPending = true;
		selectAt = where;
		tool = TOOL_SELECT;
		lastType = name;
	}
	if (add || cancel || ! open) {
		newPending = false;
		ImGui::CloseCurrentPopup ();
	}
	ImGui::EndPopup ();
}

void RoomView::draw () {
	update ();
	if (ImGui::GetTime () >= nextFileCheck) {
		// The pictures and floors are looked at again when they are drawn.
		for (std::map<std::string, std::unique_ptr<Picture> >::iterator p = pictures.begin (); p != pictures.end (); ++ p) {
			if (fileTime (p->first) != p->second->time) p->second->time = STALE;
		}
		for (std::map<std::string, FloorFile>::iterator f = floors.begin (); f != floors.end (); ++ f) {
			if (fileTime (f->first) != f->second.time) f->second.time = STALE;
		}
		// A file may have come, or changed its case.
		paths.clear ();
		nextFileCheck = ImGui::GetTime () + FILE_CHECK_SECONDS;
	}
	if (rooms.empty ()) {
		ImVec2 size = ImGui::GetContentRegionAvail ();
		const char * text = "This script has no rooms";
		ImVec2 textSize = ImGui::CalcTextSize (text);
		ImGui::SetCursorPos (ImVec2 (ImGui::GetCursorPosX () + (size.x - textSize.x) * 0.5f, ImGui::GetCursorPosY () + (size.y - textSize.y) * 0.4f));
		ImGui::TextDisabled ("%s", text);
		return;
	}
	sceneSize (&sceneWidth, &sceneHeight);
	canvas.setPictureSize (sceneWidth, sceneHeight);

	drawToolbar ();
	float statusHeight = ImGui::GetTextLineHeightWithSpacing ();
	ImVec2 size = ImGui::GetContentRegionAvail ();
	size.y -= statusHeight;
	canvas.draw (size, [this] (Draw2D & draw) { paint (draw); });
	drawLabels ();
	handleMouse ();
	handleKeys ();
	drawNewRegion ();

	ImGui::TextDisabled ("%s", canvas.status ().c_str ());
	if (! notice.empty () && ImGui::GetTime () < noticeUntil) {
		ImGui::SameLine (0, ImGui::GetFontSize () * 2);
		ImGui::TextColored (ImVec4 (1.0f, 0.8f, 0.3f, 1.0f), "%s", notice.c_str ());
	}
}

// A button after the one before it, or on the next line when it doesn't fit.
static bool wrappedButton (const char * label, bool * first) {
	const ImGuiStyle & style = ImGui::GetStyle ();
	float width = ImGui::CalcTextSize (label).x + style.FramePadding.x * 2;
	if (! *first) {
		ImGui::SameLine ();
		if (ImGui::GetContentRegionAvail ().x < width) ImGui::NewLine ();
	}
	*first = false;
	return ImGui::Button (label);
}

void RoomView::drawInspector () {
	update ();
	const Room * r = room ();
	if (! r) {
		ImGui::TextDisabled ("This script has no rooms");
		return;
	}
	if (beginProperties ("Room")) {
		propertyRow ("Sub");
		ImGui::TextUnformatted (r->name.c_str ());
		propertyRow ("Scene");
		ImGui::Text ("%d \xC3\x97 %d", sceneWidth, sceneHeight);
		propertyRow ("Floor");
		if (r->floor.empty ()) ImGui::TextDisabled ("None");
		else ImGui::TextUnformatted (r->floor.c_str ());
		ImGui::EndTable ();
	}

	ImGui::SeparatorText ("Overlays");
	if (r->overlays.empty ()) ImGui::TextDisabled ("None");
	for (size_t o = 0; o < r->overlays.size (); o ++) {
		const RoomOverlay & overlay = r->overlays[o];
		ImGui::PushID ((int) o);
		if (ImGui::Selectable (overlay.file.c_str ())) {
			if (showInScript) showInScript (overlay.call, 0);
		}
		ImGui::SameLine ();
		if (! pictureOf (overlay.file)) ImGui::TextColored (ImVec4 (1.0f, 0.45f, 0.4f, 1.0f), "Not found");
		else if (! overlay.x.literal || ! overlay.y.literal) ImGui::TextDisabled ("Not a number");
		else ImGui::TextDisabled ("%s, %s", overlay.x.text.c_str (), overlay.y.text.c_str ());
		ImGui::PopID ();
	}

	ImGui::SeparatorText ("Regions");
	if (r->regions.empty ()) ImGui::TextDisabled ("None");
	for (int g = 0; g < (int) r->regions.size (); g ++) {
		const RoomRegion & region = r->regions[g];
		ImGui::PushID (g);
		if (ImGui::Selectable (region.objectType.c_str (), g == selectedRegion)) {
			selectedRegion = g;
			selectedPoint = -1;
		}
		ImGui::SameLine ();
		std::string detail = region.shape ? "Shape" : "Rectangle";
		if (! region.editable) detail += ", " + region.whyNot;
		else if (region.conditional) detail += ", Conditional";
		ImGui::TextDisabled ("%s", detail.c_str ());
		ImGui::PopID ();
	}

	const RoomRegion * region = selected ();
	if (! region) return;
	std::vector<int> v = region->values ();
	if (! fieldsEditing) {
		if (! region->shape && v.size () == 4) {
			fieldTopLeft[0] = (float) v[0];
			fieldTopLeft[1] = (float) v[1];
			fieldBottomRight[0] = (float) v[2];
			fieldBottomRight[1] = (float) v[3];
		}
		fieldStand[0] = (float) region->standX.value;
		fieldStand[1] = (float) region->standY.value;
	}
	if (beginProperties ("Region")) {
		propertyRow ("Object Type");
		ImGui::TextUnformatted (region->objectType.c_str ());
		if (region->shape) {
			propertyRow ("Points");
			if (region->editable) ImGui::Text ("%d", (int) v.size () / 2);
			else ImGui::TextDisabled ("%s", region->whyNot.c_str ());
		} else if (region->editable) {
			bool done = false;
			propertyRow ("Top Left");
			fieldsEditing |= xyFields ("topLeft", &fieldTopLeft[0], &fieldTopLeft[1], 1.0f, "%.0f", 0, 0, &done);
			propertyRow ("Bottom Right");
			fieldsEditing |= xyFields ("bottomRight", &fieldBottomRight[0], &fieldBottomRight[1], 1.0f, "%.0f", 0, 0, &done);
			if (done) {
				fieldsEditing = false;
				int x1 = (int) fieldTopLeft[0], y1 = (int) fieldTopLeft[1], x2 = (int) fieldBottomRight[0], y2 = (int) fieldBottomRight[1];
				apply (roomEdit::setRectangle (*region, std::min (x1, x2), std::min (y1, y2), std::max (x1, x2), std::max (y1, y2)), "Resize Region", region->call);
			}
		} else {
			propertyRow ("Rectangle");
			ImGui::TextDisabled ("%s", region->whyNot.c_str ());
		}
		propertyRow ("Walk To");
		if (region->standX.literal && region->standY.literal) {
			bool done = false;
			fieldsEditing |= xyFields ("stand", &fieldStand[0], &fieldStand[1], 1.0f, "%.0f", 0, 0, &done);
			if (done) {
				fieldsEditing = false;
				apply (roomEdit::setStandPoint (*region, (int) fieldStand[0], (int) fieldStand[1]), "Move Walk-To Point", region->call);
			}
		} else {
			ImGui::TextDisabled ("Not a number");
		}
		propertyRow ("Direction");
		if (region->direction.literal) {
			std::string current = region->direction.value == -1 ? "Keep" : directionText (region->direction.value);
			if (ImGui::BeginCombo ("##direction", current.c_str ())) {
				for (int d = 0; d < DIRECTION_COUNT; d ++) {
					std::string name = directions[d] == -1 ? "Keep" : directionText (directions[d]);
					if (ImGui::Selectable (name.c_str (), directions[d] == region->direction.value)) {
						apply (roomEdit::setDirection (*region, directions[d]), "Change Direction", region->call);
					}
				}
				ImGui::EndCombo ();
			}
		} else {
			ImGui::TextDisabled ("Not a number");
		}
		ImGui::EndTable ();
	}
	// The region may be gone after a change above, until we read again.
	region = selected ();
	if (! region) return;
	bool first = true;
	if (! region->shape) {
		ImGui::BeginDisabled (! region->editable);
		if (wrappedButton ("Convert to Shape", &first)) apply (roomEdit::toShape (*region), "Convert to Shape", region->call);
		ImGui::EndDisabled ();
	}
	if (wrappedButton ("Show in Script", &first) && showInScript) {
		showInScript (region->call, (int) std::string ("addScreenRegion").size ());
		return;
	}
	if (wrappedButton ("Delete Region", &first)) {
		apply (roomEdit::remove (script->buffer (), *region), "Delete Region", region->call);
		selectedRegion = selectedPoint = -1;
	}
}

} // namespace devkit
