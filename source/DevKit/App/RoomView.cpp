#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <algorithm>

#include "imgui.h"
#include "imgui_stdlib.h"

#include "draw2d.h"

#include "FileMemory.h"
#include "Platform.h"
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
static const char * const TAB_KEY = "room.tab";
static const char * const OPACITY_KEY = "room.mapOpacity";

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

RoomView::BankFile::BankFile () : time (STALE), loaded (false) {}

RoomView::TintedFrame::TintedFrame () {
	memset (&bank, 0, sizeof (bank));
	bank.type = 2;
	reserveSpritePal (&bank.myPalette, 0);
	addSprite (0, &bank);
}

RoomView::Picture::Picture () : time (STALE), loaded (false) {
	memset (&bank, 0, sizeof (bank));
	bank.type = 2;
	reserveSpritePal (&bank.myPalette, 0);
	addSprite (0, &bank);
}

RoomView::RoomView (ScriptDocument * s, WorkspaceActions & a, const CanvasGraphics & g, FileMemory & m)
	: script (s), actions (a), graphics (g), memory (m), canvas (g), roomsVersion (0), read (false), roomIndex (-1),
	  nextFileCheck (0), showFloor (true), sceneWidth (DEFAULT_WIDTH), sceneHeight (DEFAULT_HEIGHT),
	  tab (TAB_REGIONS), tabPending (true), tool (TOOL_SELECT), selectedRegion (-1), selectedPoint (-1), selectedOverlay (-1),
	  selectedCharacter (-1), selectPending (false), selectOverlayPending (false), selectCharacterPending (false),
	  chosenOverlay (new std::string), chosenBank (new std::string),
	  referenceScale (-1), sizeFrom (1), sizeDistance (1), referenceX (0), referenceY (0), scalingSelected (-1), syncedHorizon (0), syncedDivide (0), syncedScale (false),
	  mapOpacity (0.5f), chosenReference (new std::string), chosenScaleMap (new std::string),
	  drag (DRAG_NONE), dragHandle (-1, -1), pressX (0), pressY (0), mouseX (0), mouseY (0),
	  newPending (false), newShape (false), openNewPopup (false), newChoice (0), focusNewName (false),
	  newCharacterPending (false), openCharacterPopup (false), newCostume (-1),
	  fieldsEditing (false), textFieldsEditing (false), noticeUntil (0) {
	fieldTopLeft[0] = fieldTopLeft[1] = fieldBottomRight[0] = fieldBottomRight[1] = fieldStand[0] = fieldStand[1] = 0;
	fieldOverlay[0] = fieldOverlay[1] = fieldCharacter[0] = fieldCharacter[1] = 0;
	pointPercent[0] = 10;
	pointPercent[1] = 100;
	pointY[0] = pointY[1] = 0;
	pointSet[0] = pointSet[1] = false;
	mapPercent[0] = 20;
	mapPercent[1] = 100;
	if (! script->path ().empty ()) {
		roomName = memory.get (script->path (), ROOM_KEY);
		showFloor = memory.get (script->path (), FLOOR_KEY) != "off";
		std::string remembered = memory.get (script->path (), TAB_KEY);
		if (remembered == "overlays") tab = TAB_OVERLAYS;
		else if (remembered == "characters") tab = TAB_CHARACTERS;
		else if (remembered == "scaling") tab = TAB_SCALING;
		float opacity;
		if (sscanf (memory.get (script->path (), OPACITY_KEY).c_str (), "%f", &opacity) == 1) mapOpacity = opacity;
	}
}

RoomView::~RoomView () {
	for (std::map<std::string, std::unique_ptr<Picture> >::iterator p = pictures.begin (); p != pictures.end (); ++ p) {
		graphics.draw->forgetPictures (&p->second->bank);
		forgetSpriteBank (&p->second->bank);
	}
	for (std::map<std::string, std::unique_ptr<TintedFrame> >::iterator f = tinted.begin (); f != tinted.end (); ++ f) {
		graphics.draw->forgetPictures (&f->second->bank);
		forgetSpriteBank (&f->second->bank);
	}
	// The banks forget themselves; their textures are the drawing's.
	for (std::map<std::string, std::unique_ptr<BankFile> >::iterator b = banks.begin (); b != banks.end (); ++ b) {
		graphics.draw->forgetPictures (&b->second->bank.sprites ());
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
	int overlaysBefore = roomIndex >= 0 ? (int) rooms[roomIndex].overlays.size () : -1;
	int charactersBefore = roomIndex >= 0 ? (int) rooms[roomIndex].characters.size () : -1;
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
	if (selectOverlayPending && shown) {
		selectOverlayPending = false;
		selectedOverlay = -1;
		for (size_t o = 0; o < shown->overlays.size (); o ++) {
			if (shown->overlays[o].call == selectAt) selectedOverlay = (int) o;
		}
	} else if (! shown || (int) shown->overlays.size () != overlaysBefore) {
		selectedOverlay = -1;
	}
	if (selectCharacterPending && shown) {
		selectCharacterPending = false;
		selectedCharacter = -1;
		for (size_t c = 0; c < shown->characters.size (); c ++) {
			if (shown->characters[c].call == selectAt) selectedCharacter = (int) c;
		}
	} else if (! shown || (int) shown->characters.size () != charactersBefore) {
		selectedCharacter = -1;
	}
	if (! shown || (int) shown->characters.size () != charactersBefore) {
		// The tried places are by index.
		tries.clear ();
		triedScales.clear ();
		if (scalingSelected != REFERENCE) scalingSelected = -1;
	}
	syncScalePoints ();
	if (drag == DRAG_OVERLAY ? ! selectedOverlayOf () : drag == DRAG_CHARACTER ? ! selectedCharacterOf ()
			: drag != DRAG_NONE && drag != DRAG_RECTANGLE && ! selected ()) drag = DRAG_NONE;
}

const Room * RoomView::room () const {
	return roomIndex >= 0 && roomIndex < (int) rooms.size () ? &rooms[roomIndex] : NULL;
}

const RoomRegion * RoomView::selected () const {
	const Room * r = room ();
	return r && selectedRegion >= 0 && selectedRegion < (int) r->regions.size () ? &r->regions[selectedRegion] : NULL;
}

const RoomOverlay * RoomView::selectedOverlayOf () const {
	const Room * r = room ();
	return r && selectedOverlay >= 0 && selectedOverlay < (int) r->overlays.size () ? &r->overlays[selectedOverlay] : NULL;
}

const RoomCharacter * RoomView::selectedCharacterOf () const {
	const Room * r = room ();
	return r && selectedCharacter >= 0 && selectedCharacter < (int) r->characters.size () ? &r->characters[selectedCharacter] : NULL;
}

void RoomView::setTab (Tab t) {
	if (t == tab) return;
	tab = t;
	drag = DRAG_NONE;
	drawnPoints.clear ();
	if (! script->path ().empty ()) memory.set (script->path (), TAB_KEY,
		t == TAB_OVERLAYS ? "overlays" : t == TAB_CHARACTERS ? "characters" : t == TAB_SCALING ? "scaling" : "");
}

void RoomView::chooseRoom (int index) {
	if (index < 0 || index >= (int) rooms.size () || index == roomIndex) return;
	tries.clear ();
	triedScales.clear ();
	if (scalingSelected != REFERENCE) scalingSelected = -1;
	syncedScale = false;
	pointSet[0] = pointSet[1] = false;
	roomIndex = index;
	roomName = rooms[index].name;
	selectedRegion = selectedPoint = selectedOverlay = selectedCharacter = -1;
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

void RoomView::clampOverlay (int width, int height, int * x, int * y) const {
	// As loadHSI (Engine/backdrop.cpp), which stops the game otherwise.
	if (*x > sceneWidth - width) *x = sceneWidth - width;
	if (*y > sceneHeight - height) *y = sceneHeight - height;
	if (*x < 0) *x = 0;
	if (*y < 0) *y = 0;
}

bool RoomView::overlayShown (int index, int * x, int * y, int * width, int * height) {
	const RoomOverlay & overlay = room ()->overlays[index];
	if (! overlay.movable ()) return false;
	Picture * picture = pictureOf (overlay.file);
	if (! picture) return false;
	const sprite & s = picture->bank.sprites[0];
	*width = s.width;
	*height = s.height < 0 ? - s.height : s.height;
	overlayPlace (overlay, picture, x, y);
	if (index == selectedOverlay && drag == DRAG_OVERLAY) {
		*x += mouseX - pressX;
		*y += mouseY - pressY;
		clampOverlay (*width, *height, x, y);
	}
	return true;
}

int RoomView::overlayAt (float fx, float fy) {
	const Room * r = room ();
	if (! r) return -1;
	for (int o = (int) r->overlays.size () - 1; o >= 0; o --) {
		int x, y, w, h;
		if (! overlayShown (o, &x, &y, &w, &h)) continue;
		int px = (int) floorf (fx) - x, py = (int) floorf (fy) - y;
		if (px < 0 || py < 0 || px >= w || py >= h) continue;
		// A click on a transparent part goes to what is under it.
		const sprite & s = pictureOf (r->overlays[o].file)->bank.sprites[0];
		if (s.data && ! s.data[((size_t) py * w + px) * 4 + 3]) continue;
		return o;
	}
	return -1;
}

std::string RoomView::scriptName (const std::string & path) const {
	std::string folder = ! actions.projectFile ().empty () ? folderOf (actions.projectFile ()) : folderOf (script->path ());
	std::string name = folder.empty () ? path : relativePathFrom (folder, path);
	std::replace (name.begin (), name.end (), '\\', '/');
	return name;
}

void RoomView::addOverlayFile (const std::string & path) {
	const Room * r = room ();
	if (! r) return;
	TextPos where;
	apply (roomEdit::addOverlay (script->buffer (), *r, script->indentUnit (), scriptName (path), 0, 0, &where), "Add Overlay", where);
	selectOverlayPending = true;
	selectAt = where;
	setTab (TAB_OVERLAYS);
	tabPending = true;
}

void RoomView::chooseOverlayFile () {
	std::vector<FileFilter> filters;
	FileFilter images = { "Images", "png;tga" };
	filters.push_back (images);
	std::string folder = ! actions.projectFile ().empty () ? folderOf (actions.projectFile ()) : folderOf (script->path ());
	std::shared_ptr<std::string> chosen = chosenOverlay;
	actions.platform ().openFileDialog ("Add Overlay", filters, folder, false, [chosen] (const std::vector<std::string> & files) {
		if (! files.empty ()) *chosen = files[0];
	});
}

bool RoomView::takeFile (const std::string & path) {
	std::string extension = extensionOf (path);
	if (extension != "png" && extension != "tga" && extension != "duc") return false;
	update ();
	if (! room ()) return false;
	if (tab == TAB_SCALING) {
		// A reference character, or a scale map.
		if (extension == "duc") *chosenReference = path;
		else *chosenScaleMap = path;
		return true;
	}
	if (extension == "duc") startNewCharacter (path);
	else addOverlayFile (path);
	return true;
}

const spriteBank * RoomView::bankOf (const std::string & file) {
	return bankAtPath (pathOf (file));
}

const spriteBank * RoomView::bankAtPath (const std::string & path) {
	std::unique_ptr<BankFile> & b = banks[path];
	if (! b) b.reset (new BankFile);
	if (b->time == STALE) {
		b->time = fileTime (path);
		if (b->loaded) graphics.draw->forgetPictures (&b->bank.sprites ());
		// load looks for the file first, so that a missing one says nothing.
		b->loaded = b->time >= 0 && b->bank.load (path);
	}
	return b->loaded ? &b->bank.sprites () : NULL;
}

CostumePicture RoomView::pictureOfCharacter (const RoomCharacter & character) {
	if (character.picture.known || character.costumeSub.empty ()) return character.picture;
	char key[32];
	snprintf (key, sizeof (key), "@%d", character.angle ());
	std::map<std::string, CostumePicture>::iterator known = subPictures.find (character.costumeSub + key);
	if (known != subPictures.end ()) return known->second;
	CostumePicture & picture = subPictures[character.costumeSub + key];
	std::vector<const ScriptSymbol *> found = actions.scriptIndex ().find (character.costumeSub);
	for (size_t f = 0; f < found.size (); f ++) {
		if (found[f]->kind != SYMBOL_SUB || ! found[f]->owner.empty ()) continue;
		// The text of an open script, with its changes, else of the file.
		if (ScriptDocument * open = dynamic_cast<ScriptDocument *> (actions.findDocument (found[f]->file))) {
			if (costumeOfSub (open->buffer (), character.costumeSub, &picture, character.angle ())) break;
		} else {
			std::string text, error;
			TextBuffer buffer;
			if (ScriptDocument::readScript (found[f]->file, &text, &error)) {
				buffer.setText (text);
				if (costumeOfSub (buffer, character.costumeSub, &picture, character.angle ())) break;
			}
		}
	}
	return picture;
}

// How each draw mode colours a character, as setMyDrawMode (Engine/
// people.cpp) has it: the colour it mixes in, how much, and how
// transparent it makes it.
static const unsigned char drawModeColours[ROOM_DRAW_MODES][3] = {
	{ 0, 0, 0 }, { 0, 0, 192 }, { 0, 0, 128 }, { 0, 0, 64 },
	{ 0, 192, 0 }, { 0, 128, 0 }, { 0, 64, 0 }, { 0, 255, 0 },
	{ 0, 255, 64 }, { 0, 255, 128 }, { 0, 255, 192 },
	{ 128, 64, 0 }, { 128, 128, 0 }, { 128, 192, 0 }, { 128, 255, 0 },
	{ 255, 64, 0 }, { 255, 128, 0 }, { 255, 192, 0 }, { 255, 255, 0 },
	{ 0, 0, 254 },
};

const spriteBank * RoomView::tintedFrame (const std::string & bankFile, const spriteBank * bank, int frame, int mode) {
	BankFile * file = banks[pathOf (bankFile)].get ();
	char key[64];
	snprintf (key, sizeof (key), "|%lld|%d|%d", file ? (long long) file->time : 0LL, frame, mode);
	std::unique_ptr<TintedFrame> & tint = tinted[pathOf (bankFile) + key];
	if (tint) return &tint->bank;
	tint.reset (new TintedFrame);
	const sprite & from = bank->sprites[frame];
	unsigned char * rgba = spritePixels (bank, frame);
	// As the engine's shader: rgb = rgb (1 - m) + C m, alpha (1 - t).
	float c = drawModeColours[mode][0], m = drawModeColours[mode][1] / 255.0f, keep = 1.0f - drawModeColours[mode][2] / 255.0f;
	for (int p = 0; p < from.width * from.height; p ++) {
		unsigned char * px = rgba + p * 4;
		for (int k = 0; k < 3; k ++) px[k] = (unsigned char) (px[k] * (1 - m) + c * m + 0.5f);
		px[3] = (unsigned char) (px[3] * keep + 0.5f);
	}
	sprite & to = tint->bank.sprites[0];
	to.data = rgba;
	to.width = from.width;
	to.height = from.height;
	to.xhot = from.xhot;
	to.yhot = from.yhot;
	spriteBankChanged (&tint->bank);
	return &tint->bank;
}

float RoomView::scaleAt (const RoomScale & scale, float x, float y) {
	if (! scale.map.empty () && sceneWidth && sceneHeight) {
		// As scaleMapAt (Engine/scalemap.cpp): the grey of the map over
		// the scene, between the centres of its pixels.
		if (Picture * picture = pictureOf (scale.map)) {
			const sprite & s = picture->bank.sprites[0];
			int w = s.width, h = s.height < 0 ? - s.height : s.height;
			if (s.data && w > 0 && h > 0) {
				float u = x * w / sceneWidth - 0.5f, v = y * h / sceneHeight - 0.5f;
				if (! (u > 0)) u = 0;
				if (! (v > 0)) v = 0;
				if (u > w - 1) u = (float) (w - 1);
				if (v > h - 1) v = (float) (h - 1);
				int x0 = (int) u, y0 = (int) v;
				int x1 = x0 + 1 < w ? x0 + 1 : x0, y1 = y0 + 1 < h ? y0 + 1 : y0;
				float fx = u - x0, fy = v - y0;
				const unsigned char * d = s.data;
				#define GREY(px, py) ((d[((py) * w + (px)) * 4] + d[((py) * w + (px)) * 4 + 1] + d[((py) * w + (px)) * 4 + 2] + 1) / 3)
				float top = GREY (x0, y0) * (1 - fx) + GREY (x1, y0) * fx;
				float bottom = GREY (x0, y1) * (1 - fx) + GREY (x1, y1) * fx;
				#undef GREY
				float t = (top * (1 - fy) + bottom * fy) / 255.0f;
				return (scale.mapMin + (scale.mapMax - scale.mapMin) * t) / 100.0f;
			}
		}
	}
	return scale.at (y);
}

bool RoomView::characterShown (int index, CharacterShown * shown) {
	const RoomCharacter & character = room ()->characters[index];
	if (! character.movable ()) return false;
	shown->x = character.x.value;
	shown->y = character.y.value;
	if (index == selectedCharacter && drag == DRAG_CHARACTER) {
		shown->x = clampX (shown->x + mouseX - pressX);
		shown->y = clampY (shown->y + mouseY - pressY);
	}
	if (tab == TAB_SCALING) {
		// Where the Scaling tab's drags left it, which the script doesn't know.
		std::map<int, std::pair<int, int> >::const_iterator tried = tries.find (index);
		if (tried != tries.end ()) {
			shown->x = tried->second.first;
			shown->y = tried->second.second;
		}
		if (index == scalingSelected && drag == DRAG_TRY) {
			shown->x = clampX (shown->x + mouseX - pressX);
			shown->y = clampY (shown->y + mouseY - pressY);
		}
	}
	int extras = character.extraFlags ();
	// FIXEDSIZE keeps it at its size, as moveAndScale does.
	shown->scale = extras & ROOM_FIXEDSIZE ? 1.0f : scaleAt (character.scale, (float) shown->x, (float) shown->y);
	if (tab == TAB_SCALING) {
		// The size that its handles gave it, which the script doesn't know.
		std::map<int, float>::const_iterator sized = triedScales.find (index);
		if (sized != triedScales.end ()) shown->scale = sized->second;
		if (index == scalingSelected && drag == DRAG_SIZE) shown->scale = draggedScale (*shown);
	}
	// As depthOf and shufflePeople: the fixed depth, else y; FRONT in front.
	shown->depth = (character.hasDepth () ? character.depth.value.value : shown->y) + (extras & ROOM_FRONT ? 1000 : 0);
	shown->bank = shown->picture = NULL;
	shown->frame = shown->pictureIndex = 0;
	shown->mirror = false;
	int mode = character.drawModeValue ();
	CostumePicture picture = pictureOfCharacter (character);
	// An INVISIBLE character shows as one without a picture, to be found.
	const spriteBank * bank = picture.known && mode != ROOM_INVISIBLE ? bankOf (picture.bank) : NULL;
	// As drawPeople (Engine/people.cpp): no picture at all below 5%.
	if (bank && picture.frame >= 0 && picture.frame < bank->total && bank->sprites[picture.frame].data && shown->scale > 0.05f) {
		const sprite & sp = bank->sprites[picture.frame];
		float s = shown->scale;
		// As scaleSprite (Engine/sprites.cpp), with the hotspot at x, y.
		shown->x1 = picture.mirror ? shown->x - (sp.width - (sp.xhot + 1)) * s : shown->x - sp.xhot * s;
		shown->y1 = shown->y - sp.yhot * s;
		shown->x2 = shown->x1 + sp.width * s;
		shown->y2 = shown->y1 + sp.height * s;
		shown->bank = bank;
		shown->frame = picture.frame;
		shown->mirror = picture.mirror;
		shown->picture = bank;
		shown->pictureIndex = picture.frame;
		if (mode > 0 && mode < ROOM_DRAW_MODES) {
			shown->picture = tintedFrame (picture.bank, bank, picture.frame, mode);
			shown->pictureIndex = 0;
		}
	} else {
		float r = canvas.pixelsOfPoints (7.0f);
		shown->x1 = shown->x - r;
		shown->y1 = shown->y - r;
		shown->x2 = shown->x + r;
		shown->y2 = shown->y + r;
	}
	return true;
}

// The characters in the order the game draws them: from the back.
static bool fartherBack (const std::pair<int, int> & a, const std::pair<int, int> & b) {
	return a.first < b.first || (a.first == b.first && a.second < b.second);
}

bool RoomView::nearFeet (const CharacterShown & shown, float fx, float fy) const {
	float r = canvas.pixelsOfPoints (HANDLE_POINTS);
	return fabsf (fx - shown.x) <= r && fy >= shown.y - r * 2 && fy <= shown.y + r;
}

int RoomView::characterAt (float fx, float fy) {
	const Room * r = room ();
	if (! r) return -1;
	std::vector<std::pair<int, int> > order;
	for (int c = 0; c < (int) r->characters.size (); c ++) {
		CharacterShown shown;
		if (characterShown (c, &shown)) order.push_back (std::make_pair (shown.depth, c));
	}
	std::sort (order.begin (), order.end (), fartherBack);
	for (int i = (int) order.size () - 1; i >= 0; i --) {
		CharacterShown shown;
		if (! characterShown (order[i].second, &shown)) continue;
		// Near the feet always counts, so that a small one can be taken.
		if (nearFeet (shown, fx, fy)) return order[i].second;
		if (fx < shown.x1 || fx >= shown.x2 || fy < shown.y1 || fy >= shown.y2) continue;
		if (shown.bank) {
			// Not where the sprite is transparent.
			const sprite & sp = shown.bank->sprites[shown.frame];
			int px = (int) ((fx - shown.x1) / shown.scale), py = (int) ((fy - shown.y1) / shown.scale);
			if (shown.mirror) px = sp.width - 1 - px;
			if (px < 0 || py < 0 || px >= sp.width || py >= sp.height) continue;
			bool clear = shown.bank->type >= 2 ? ! sp.data[((size_t) py * sp.width + px) * 4 + 3] : ! sp.data[(size_t) py * sp.width + px];
			if (clear) continue;
		}
		return order[i].second;
	}
	return -1;
}

void RoomView::chooseBankFile () {
	std::vector<FileFilter> filters;
	FileFilter banks = { "Sprite banks", "duc" };
	filters.push_back (banks);
	std::string folder = ! actions.projectFile ().empty () ? folderOf (actions.projectFile ()) : folderOf (script->path ());
	std::shared_ptr<std::string> chosen = chosenBank;
	actions.platform ().openFileDialog ("Sprite Bank", filters, folder, false, [chosen] (const std::vector<std::string> & files) {
		if (! files.empty ()) *chosen = files[0];
	});
}

void RoomView::startNewCharacter (const std::string & bankPath) {
	// The costume subs of the project and of the open scripts, with their
	// changes.
	costumeSubList = costumeSubs (actions.scriptIndex ().files (), [this] (const std::string & file, TextBuffer * text) {
		if (ScriptDocument * open = dynamic_cast<ScriptDocument *> (actions.findDocument (file))) {
			*text = open->buffer ();
			return true;
		}
		std::string fileText, error;
		if (! ScriptDocument::readScript (file, &fileText, &error)) return false;
		text->setText (fileText);
		return true;
	});
	newBank = bankPath;
	newCostume = bankPath.empty () && ! costumeSubList.empty () ? 0 : -1;
	newCharacterPending = openCharacterPopup = true;
	setTab (TAB_CHARACTERS);
	tabPending = true;
}

void RoomView::apply (const std::vector<TextChange> & changes, const std::string & label, TextPos at) {
	if (changes.empty ()) return;
	script->edit (changes, TextSelection (at), EDIT_OTHER, label);
}

void RoomView::drawToolbar () {
	// What both tabs share: the room, the zoom and the floor.
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
	ImGui::SameLine ();
	canvas.drawZoomButtons ();
	ImGui::SameLine ();
	ImGui::TextDisabled ("|");
	ImGui::SameLine ();
	if (ImGui::Checkbox ("Floor", &showFloor) && ! script->path ().empty ()) {
		memory.set (script->path (), FLOOR_KEY, showFloor ? "" : "off");
	}

	// The tabs, each with its tools.
	if (ImGui::BeginTabBar ("##roomTabs")) {
		if (ImGui::BeginTabItem ("Overlays", NULL, tabPending && tab == TAB_OVERLAYS ? ImGuiTabItemFlags_SetSelected : 0)) {
			if (! tabPending) setTab (TAB_OVERLAYS);
			if (ImGui::Button ("Add Overlay...")) chooseOverlayFile ();
			ImGui::EndTabItem ();
		}
		if (ImGui::BeginTabItem ("Regions", NULL, tabPending && tab == TAB_REGIONS ? ImGuiTabItemFlags_SetSelected : 0)) {
			if (! tabPending) setTab (TAB_REGIONS);
			for (int t = 0; t < TOOL_COUNT; t ++) {
				if (t) ImGui::SameLine ();
				bool current = tool == t;
				if (current) ImGui::PushStyleColor (ImGuiCol_Button, ImGui::GetStyleColorVec4 (ImGuiCol_ButtonActive));
				if (ImGui::Button (tools[t].name)) {
					tool = (Tool) t;
					drawnPoints.clear ();
				}
				if (current) ImGui::PopStyleColor ();
				ImGui::SetItemTooltip ("%s (%d)", tools[t].hint, t + 1);
			}
			ImGui::EndTabItem ();
		}
		if (ImGui::BeginTabItem ("Characters", NULL, tabPending && tab == TAB_CHARACTERS ? ImGuiTabItemFlags_SetSelected : 0)) {
			if (! tabPending) setTab (TAB_CHARACTERS);
			if (ImGui::Button ("Add Character...")) startNewCharacter (std::string ());
			ImGui::EndTabItem ();
		}
		if (ImGui::BeginTabItem ("Scaling", NULL, tabPending && tab == TAB_SCALING ? ImGuiTabItemFlags_SetSelected : 0)) {
			if (! tabPending) setTab (TAB_SCALING);
			if (ImGui::Button ("Load Reference...")) {
				std::vector<FileFilter> filters;
				FileFilter banks = { "Sprite banks", "duc" };
				filters.push_back (banks);
				std::shared_ptr<std::string> chosen = chosenReference;
				actions.platform ().openFileDialog ("Reference Character", filters, folderOf (actions.projectFile ().empty () ? script->path () : actions.projectFile ()), false,
					[chosen] (const std::vector<std::string> & files) { if (! files.empty ()) *chosen = files[0]; });
			}
			ImGui::SameLine ();
			if (ImGui::Button ("Load Scale Map...")) {
				std::vector<FileFilter> filters;
				FileFilter images = { "Images", "png;tga" };
				filters.push_back (images);
				std::shared_ptr<std::string> chosen = chosenScaleMap;
				actions.platform ().openFileDialog ("Scale Map", filters, folderOf (actions.projectFile ().empty () ? script->path () : actions.projectFile ()), false,
					[chosen] (const std::vector<std::string> & files) { if (! files.empty ()) *chosen = files[0]; });
			}
			ImGui::EndTabItem ();
		}
		ImGui::EndTabBar ();
	}
	tabPending = false;

	if (tab == TAB_REGIONS && ! ImGui::GetIO ().WantTextInput && ImGui::IsWindowFocused (ImGuiFocusedFlags_RootAndChildWindows)) {
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
	if (ImGui::GetIO ().WantTextInput || newPending || newCharacterPending || ! ImGui::IsWindowFocused (ImGuiFocusedFlags_RootAndChildWindows)) return;
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
	if (tab == TAB_SCALING) {
		if (ImGui::Shortcut (ImGuiKey_Escape)) scalingSelected = -1;
		return;
	}
	if (tab == TAB_CHARACTERS) {
		const RoomCharacter * character = selectedCharacterOf ();
		if (ImGui::Shortcut (ImGuiKey_Escape)) selectedCharacter = -1;
		bool deleteKey = ImGui::Shortcut (ImGuiKey_Delete);
		bool backspace = ImGui::Shortcut (ImGuiKey_Backspace);
		if (character && (deleteKey || backspace)) {
			apply (roomEdit::remove (script->buffer (), *character), "Delete Character", character->call);
			selectedCharacter = -1;
		}
		return;
	}
	if (tab == TAB_OVERLAYS) {
		const RoomOverlay * overlay = selectedOverlayOf ();
		if (ImGui::Shortcut (ImGuiKey_Escape)) selectedOverlay = -1;
		bool deleteKey = ImGui::Shortcut (ImGuiKey_Delete);
		bool backspace = ImGui::Shortcut (ImGuiKey_Backspace);
		if (overlay && (deleteKey || backspace)) {
			apply (roomEdit::remove (script->buffer (), *overlay), "Delete Overlay", overlay->call);
			selectedOverlay = -1;
		}
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
	if (drag == DRAG_SIZE) {
		// Only to try the scale: the script stays as it is.
		CharacterShown shown;
		if (scalingSelected == REFERENCE ? referenceShown (&shown) : scalingSelected >= 0 && characterShown (scalingSelected, &shown)) {
			if (scalingSelected == REFERENCE) referenceScale = shown.scale;
			else triedScales[scalingSelected] = shown.scale;
		}
		drag = DRAG_NONE;
		return;
	}
	if (drag == DRAG_TRY) {
		// Only to try the scale: the script stays as it is. Somewhere else,
		// it takes the size that the room gives it there.
		CharacterShown shown;
		bool moved = mouseX != pressX || mouseY != pressY;
		if (scalingSelected == REFERENCE) {
			if (referenceShown (&shown)) {
				referenceX = shown.x;
				referenceY = shown.y;
			}
			if (moved) referenceScale = -1;
		} else if (scalingSelected >= 0 && characterShown (scalingSelected, &shown)) {
			tries[scalingSelected] = std::make_pair (shown.x, shown.y);
			if (moved) triedScales.erase (scalingSelected);
		}
		drag = DRAG_NONE;
		return;
	}
	if (drag == DRAG_CHARACTER) {
		const RoomCharacter * character = selectedCharacterOf ();
		CharacterShown shown;
		bool moved = character && characterShown (selectedCharacter, &shown);
		drag = DRAG_NONE;
		if (moved && (mouseX != pressX || mouseY != pressY)) apply (roomEdit::moveCharacter (*character, shown.x, shown.y), "Move Character", character->call);
		return;
	}
	if (drag == DRAG_OVERLAY) {
		const RoomOverlay * overlay = selectedOverlayOf ();
		int x, y, w, h;
		bool shown = overlay && overlayShown (selectedOverlay, &x, &y, &w, &h);
		drag = DRAG_NONE;
		if (shown && (mouseX != pressX || mouseY != pressY)) apply (roomEdit::moveOverlay (*overlay, x, y), "Move Overlay", overlay->call);
		return;
	}
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
	if (! room () || newPending || newCharacterPending) return;
	if (tab == TAB_OVERLAYS) handleOverlayMouse (fx, fy);
	else if (tab == TAB_CHARACTERS) handleCharacterMouse (fx, fy);
	else if (tab == TAB_SCALING) handleScalingMouse (fx, fy);
	else handleRegionMouse (fx, fy);
	if (drag != DRAG_NONE && canvas.released ()) finishDrag ();
}

void RoomView::handleOverlayMouse (float fx, float fy) {
	if (canvas.hovered () && drag == DRAG_NONE && ! canvas.animating ()) {
		int over = overlayAt (fx, fy);
		if (over >= 0 && over == selectedOverlay) ImGui::SetMouseCursor (ImGuiMouseCursor_ResizeAll);
	}
	if (! canvas.pressed ()) return;
	pressX = mouseX;
	pressY = mouseY;
	selectedOverlay = overlayAt (fx, fy);
	if (selectedOverlay >= 0) drag = DRAG_OVERLAY;
}

void RoomView::handleCharacterMouse (float fx, float fy) {
	if (canvas.hovered () && drag == DRAG_NONE && ! canvas.animating ()) {
		int over = characterAt (fx, fy);
		if (over >= 0 && over == selectedCharacter) ImGui::SetMouseCursor (ImGuiMouseCursor_ResizeAll);
	}
	if (! canvas.pressed ()) return;
	pressX = mouseX;
	pressY = mouseY;
	selectedCharacter = characterAt (fx, fy);
	if (selectedCharacter >= 0) drag = DRAG_CHARACTER;
}

void RoomView::handleRegionMouse (float fx, float fy) {
	float near = closeness ();
	const Room * r = room ();

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
	for (int o = 0; o < (int) r->overlays.size (); o ++) {
		int x, y, w, h;
		if (! overlayShown (o, &x, &y, &w, &h)) continue;
		Picture * picture = pictureOf (r->overlays[o].file);
		draw.setDrawingColour (1.0f, 1.0f, 1.0f, r->overlays[o].conditional ? 0.6f : 1.0f);
		draw.drawPicture (&picture->bank, 0, false, (float) x, (float) y, (float) (x + w), (float) (y + h), 0, 0, 1, 1);
	}

	// The scale map over the room, as see-through as the Inspector says.
	bool scalingTab = tab == TAB_SCALING;
	if (scalingTab && ! r->scale.map.empty ()) {
		if (Picture * map = pictureOf (r->scale.map)) {
			draw.setDrawingColour (1.0f, 1.0f, 1.0f, mapOpacity);
			draw.drawPicture (&map->bank, 0, false, 0, 0, (float) sceneWidth, (float) sceneHeight, 0, 0, 1, 1);
		}
	}

	// The characters, from the back, as the game draws them.
	bool charactersTab = tab == TAB_CHARACTERS;
	std::vector<std::pair<int, int> > order;
	for (int c = 0; c < (int) r->characters.size (); c ++) {
		CharacterShown shown;
		if (characterShown (c, &shown)) order.push_back (std::make_pair (shown.depth, c));
	}
	std::sort (order.begin (), order.end (), fartherBack);
	for (size_t i = 0; i < order.size (); i ++) {
		int c = order[i].second;
		CharacterShown shown;
		characterShown (c, &shown);
		float alpha = r->characters[c].conditional ? 0.6f : 1.0f;
		if (shown.bank) {
			draw.setDrawingColour (1.0f, 1.0f, 1.0f, alpha);
			draw.drawPicture (shown.picture, shown.pictureIndex, false, shown.x1, shown.y1, shown.x2, shown.y2,
				shown.mirror ? 1.0f : 0.0f, 0, shown.mirror ? 0.0f : 1.0f, 1);
		} else {
			// A marker: a ring with a cross, where the feet are.
			std::vector<float> ring;
			float radius = (shown.x2 - shown.x1) * 0.5f;
			for (int k = 0; k < 16; k ++) {
				float a = k * 3.14159265f / 8;
				ring.push_back (shown.x + cosf (a) * radius);
				ring.push_back (shown.y + sinf (a) * radius);
			}
			draw.setDrawingColour (1.0f, 0.6f, 0.3f, (charactersTab ? 1.0f : 0.5f) * alpha);
			draw.drawLineLoop (ring.data (), 16);
			const float cross[] = { (float) shown.x - radius, (float) shown.y, (float) shown.x + radius, (float) shown.y,
				(float) shown.x, (float) shown.y - radius, (float) shown.x, (float) shown.y + radius };
			draw.drawLines (cross, 4);
		}
		if ((charactersTab && c == selectedCharacter) || (scalingTab && c == scalingSelected)) {
			const float box[] = { shown.x1, shown.y1, shown.x2, shown.y1, shown.x2, shown.y2, shown.x1, shown.y2 };
			draw.setDrawingColour (1.0f, 0.8f, 0.2f, 1.0f);
			draw.drawLineLoop (box, 4);
			// Handles to try other sizes.
			if (scalingTab && shown.bank) {
				float h = canvas.pixelsOfPoints (3.5f);
				for (int k = 0; k < 4; k ++) {
					float hx = box[k * 2], hy = box[k * 2 + 1];
					const float square[] = { hx - h, hy - h, hx + h, hy - h, hx + h, hy + h, hx - h, hy + h };
					draw.drawPolygon (square, 4);
				}
			}
		}
	}
	// The reference character, in front.
	CharacterShown reference;
	if (scalingTab && referenceShown (&reference)) {
		if (reference.bank) {
			draw.setDrawingColour (1.0f, 1.0f, 1.0f, 1.0f);
			draw.drawPicture (reference.picture, reference.pictureIndex, false, reference.x1, reference.y1, reference.x2, reference.y2, 0, 0, 1, 1);
		}
		if (scalingSelected == REFERENCE || ! reference.bank) {
			const float box[] = { reference.x1, reference.y1, reference.x2, reference.y1, reference.x2, reference.y2, reference.x1, reference.y2 };
			draw.setDrawingColour (1.0f, 0.8f, 0.2f, 1.0f);
			draw.drawLineLoop (box, 4);
			if (scalingSelected == REFERENCE && reference.bank) {
				float h = canvas.pixelsOfPoints (3.5f);
				for (int k = 0; k < 4; k ++) {
					float hx = box[k * 2], hy = box[k * 2 + 1];
					const float square[] = { hx - h, hy - h, hx + h, hy - h, hx + h, hy + h, hx - h, hy + h };
					draw.drawPolygon (square, 4);
				}
			}
		}
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
	bool regionsTab = tab == TAB_REGIONS;
	// The Scaling tab shows the overlays and the characters only.
	if (scalingTab) {
		draw.finish ();
		return;
	}
	int hovered = regionsTab && drag == DRAG_NONE && tool == TOOL_SELECT && canvas.hovered () ? regionAt (canvas.mouseX (), canvas.mouseY ()) : -1;
	for (int g = 0; g < (int) r->regions.size (); g ++) {
		const RoomRegion & region = r->regions[g];
		std::vector<int> v = shownValues (g);
		RoomRegion shown = region;
		for (size_t c = 0; c < shown.coordinates.size () && c < v.size (); c ++) shown.coordinates[c].value = v[c];
		std::vector<int> outline = shown.outline ();
		if (outline.size () < 6) continue;
		std::vector<float> points (outline.begin (), outline.end ());
		// In the Overlays tab, the regions are only there to see.
		bool isSelected = regionsTab && g == selectedRegion;
		float alpha = (region.conditional ? 0.6f : 1.0f) * (regionsTab ? 1.0f : 0.35f);
		if (isSelected && ! region.shape) {
			draw.setDrawingColour (1.0f, 0.8f, 0.2f, 0.18f);
			draw.drawPolygon (points.data (), 4);
		}
		if (isSelected) draw.setDrawingColour (1.0f, 0.8f, 0.2f, 1.0f);
		else if (! region.editable || ! regionsTab) draw.setDrawingColour (0.65f, 0.65f, 0.65f, 0.8f * alpha);
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

	// The selected overlay.
	if (! regionsTab && selectedOverlayOf ()) {
		int x, y, w, h;
		if (overlayShown (selectedOverlay, &x, &y, &w, &h)) {
			const float box[] = { (float) x, (float) y, (float) (x + w), (float) y, (float) (x + w), (float) (y + h), (float) x, (float) (y + h) };
			draw.setDrawingColour (1.0f, 0.8f, 0.2f, 1.0f);
			draw.drawLineLoop (box, 4);
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
	for (int g = 0; tab != TAB_SCALING && g < (int) r->regions.size (); g ++) {
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
		ImU32 colour = tab != TAB_REGIONS ? IM_COL32 (170, 170, 170, 110)
			: g == selectedRegion ? IM_COL32 (255, 205, 50, 255) : region.editable ? IM_COL32 (110, 245, 130, 230) : IM_COL32 (170, 170, 170, 220);
		const char * name = region.objectType.c_str ();
		ImVec2 size = ImGui::CalcTextSize (name);
		list->AddRectFilled (ImVec2 (at.x - 2, at.y - 1), ImVec2 (at.x + size.x + 2, at.y + size.y + 1), IM_COL32 (0, 0, 0, tab == TAB_REGIONS ? 150 : 60), 2.0f);
		list->AddText (at, colour, name);
	}
	if (tab == TAB_CHARACTERS || tab == TAB_SCALING) {
		bool scaling = tab == TAB_SCALING;
		CharacterShown reference;
		bool hasReference = scaling && referenceShown (&reference);
		for (int c = hasReference ? -1 : 0; c < (int) r->characters.size (); c ++) {
			CharacterShown shown;
			if (c == -1) shown = reference;
			else if (! characterShown (c, &shown)) continue;
			// In the Scaling tab, with its size.
			std::string label = c == -1 ? std::string ("Reference") : r->characters[c].objectType;
			if (scaling) {
				char percent[16];
				snprintf (percent, sizeof (percent), " %d%%", (int) floorf (shown.scale * 100.0f + 0.5f));
				label += percent;
			}
			const char * name = label.c_str ();
			ImVec2 size = ImGui::CalcTextSize (name);
			ImVec2 feet = canvas.toWindow ((float) shown.x, shown.y2 > shown.y ? shown.y2 : (float) shown.y);
			ImVec2 at (feet.x - size.x * 0.5f, feet.y + 3);
			bool chosen = scaling ? (c == -1 ? scalingSelected == REFERENCE : c == scalingSelected) : c == selectedCharacter;
			ImU32 colour = chosen ? IM_COL32 (255, 205, 50, 255) : IM_COL32 (255, 175, 110, 230);
			list->AddRectFilled (ImVec2 (at.x - 2, at.y - 1), ImVec2 (at.x + size.x + 2, at.y + size.y + 1), IM_COL32 (0, 0, 0, 150), 2.0f);
			list->AddText (at, colour, name);
		}
	}
	list->PopClipRect ();
}

std::vector<std::string> RoomView::objectTypes () const {
	std::vector<std::string> types;
	std::vector<const ScriptSymbol *> symbols = actions.scriptIndex ().allSymbols ();
	for (size_t s = 0; s < symbols.size (); s ++) {
		if (symbols[s]->kind == SYMBOL_OBJECT_TYPE) types.push_back (symbols[s]->name);
	}
	std::sort (types.begin (), types.end ());
	types.erase (std::unique (types.begin (), types.end ()), types.end ());
	return types;
}

void RoomView::startObjectTypeChoice (const std::vector<std::string> & types) {
	newName.clear ();
	// The objectType of the last new region or character, else a new one.
	newChoice = (int) types.size ();
	for (size_t t = 0; t < types.size (); t ++) {
		if (types[t] == lastType) newChoice = (int) t;
	}
	focusNewName = newChoice == (int) types.size ();
}

static const char * const NEW_TYPE = "New Object Type";

void RoomView::drawObjectTypeFields (const std::vector<std::string> & types, bool * creating, bool * entered, std::string * problem) {
	if (newChoice > (int) types.size ()) newChoice = (int) types.size ();
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
	*creating = newChoice == (int) types.size ();
	*entered = false;
	problem->clear ();
	if (*creating) {
		if (focusNewName) {
			ImGui::SetKeyboardFocusHere ();
			focusNewName = false;
		}
		ImGui::SetNextItemWidth (ImGui::GetFontSize () * 14);
		*entered = ImGui::InputText ("Name", &newName, ImGuiInputTextFlags_EnterReturnsTrue);
		if (! newName.empty ()) {
			if (! isIdentifier (newName)) *problem = "Not a valid name";
			else if (! actions.scriptIndex ().find (newName).empty ()) *problem = "Name in use";
		}
		if (! problem->empty ()) ImGui::TextColored (ImVec4 (1.0f, 0.45f, 0.4f, 1.0f), "%s", problem->c_str ());
	}
	// Enter takes the objectType that is chosen.
	if (! *creating && ! ImGui::IsAnyItemActive () && (ImGui::IsKeyPressed (ImGuiKey_Enter) || ImGui::IsKeyPressed (ImGuiKey_KeypadEnter))) *entered = true;
}

// The window that asks which objectType a new region is of.
void RoomView::drawNewRegion () {
	static const char * const TITLE = "New Region";
	if (! newPending) return;
	std::vector<std::string> types = objectTypes ();
	if (openNewPopup) {
		ImGui::OpenPopup (TITLE);
		openNewPopup = false;
		startObjectTypeChoice (types);
	}

	bool open = true;
	ImGui::SetNextWindowPos (ImGui::GetMainViewport ()->GetCenter (), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));
	if (! ImGui::BeginPopupModal (TITLE, &open, ImGuiWindowFlags_AlwaysAutoResize)) {
		newPending = false;
		return;
	}
	bool creating, entered;
	std::string problem;
	drawObjectTypeFields (types, &creating, &entered, &problem);
	bool ready = creating ? ! newName.empty () && problem.empty () : newChoice < (int) types.size ();
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

// The window that asks for the objectType and costume of a new character.
void RoomView::drawNewCharacter () {
	static const char * const TITLE = "New Character";
	if (! newCharacterPending) return;
	std::vector<std::string> types = objectTypes ();
	if (openCharacterPopup) {
		ImGui::OpenPopup (TITLE);
		openCharacterPopup = false;
		startObjectTypeChoice (types);
	}

	bool open = true;
	ImGui::SetNextWindowPos (ImGui::GetMainViewport ()->GetCenter (), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));
	if (! ImGui::BeginPopupModal (TITLE, &open, ImGuiWindowFlags_AlwaysAutoResize)) {
		newCharacterPending = false;
		return;
	}
	bool creating, entered;
	std::string problem;
	drawObjectTypeFields (types, &creating, &entered, &problem);

	// The costume: a sub of the project, or a sprite bank.
	if (newCostume >= (int) costumeSubList.size ()) newCostume = -1;
	std::string bankName = newBank.empty () ? std::string () : scriptName (newBank);
	std::string current = newCostume >= 0 ? costumeSubList[newCostume] + " ()" : bankName.empty () ? std::string ("Sprite Bank...") : bankName;
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 14);
	if (ImGui::BeginCombo ("Costume", current.c_str ())) {
		for (size_t c = 0; c < costumeSubList.size (); c ++) {
			std::string label = costumeSubList[c] + " ()";
			if (ImGui::Selectable (label.c_str (), (int) c == newCostume)) newCostume = (int) c;
		}
		if (! costumeSubList.empty ()) ImGui::Separator ();
		if (! bankName.empty () && ImGui::Selectable (bankName.c_str (), newCostume < 0)) newCostume = -1;
		if (ImGui::Selectable ("Sprite Bank...")) {
			newCostume = -1;
			chooseBankFile ();
		}
		ImGui::EndCombo ();
	}
	bool hasCostume = newCostume >= 0 || ! newBank.empty ();
	bool ready = hasCostume && (creating ? ! newName.empty () && problem.empty () : newChoice < (int) types.size ());
	ImGui::BeginDisabled (! ready);
	bool add = ImGui::Button ("Add Character") || (entered && ready);
	ImGui::EndDisabled ();
	ImGui::SameLine ();
	bool cancel = ImGui::Button ("Cancel") || (ImGui::IsKeyPressed (ImGuiKey_Escape) && ! ImGui::IsAnyItemActive ());
	const Room * r = room ();
	if (add && ready && r) {
		std::string name = creating ? newName : types[newChoice];
		std::string costume = newCostume >= 0 ? costumeSubList[newCostume] + " ()" : "anim ('" + bankName + "', 0)";
		TextPos where;
		// In the middle of the lower half of the scene, to be dragged.
		std::vector<TextChange> changes = roomEdit::addCharacter (script->buffer (), *r, script->indentUnit (), name, creating,
			sceneWidth / 2, sceneHeight * 3 / 4, costume, &where);
		apply (changes, "Add Character", where);
		selectCharacterPending = true;
		selectAt = where;
		lastType = name;
	}
	if (add || cancel || ! open) {
		newCharacterPending = false;
		ImGui::CloseCurrentPopup ();
	}
	ImGui::EndPopup ();
}

void RoomView::draw () {
	update ();
	if (! chosenOverlay->empty ()) {
		addOverlayFile (*chosenOverlay);
		chosenOverlay->clear ();
		update ();
	}
	if (! chosenReference->empty ()) {
		// It stands in the middle of the lower half, to be dragged about.
		referenceBank = *chosenReference;
		referenceX = sceneWidth / 2;
		referenceY = sceneHeight * 3 / 4;
		scalingSelected = REFERENCE;
		chosenReference->clear ();
		setTab (TAB_SCALING);
		tabPending = true;
	}
	if (! chosenScaleMap->empty ()) {
		if (const Room * r = room ()) {
			apply (roomEdit::setScaleMap (script->buffer (), *r, script->indentUnit (), scriptName (*chosenScaleMap), mapPercent[0], mapPercent[1]),
				r->scaleMap.present ? "Change Scale Map" : "Add Scale Map", r->at);
		}
		chosenScaleMap->clear ();
		setTab (TAB_SCALING);
		tabPending = true;
		update ();
	}
	if (! chosenBank->empty ()) {
		newBank = *chosenBank;
		newCostume = -1;
		chosenBank->clear ();
	}
	if (ImGui::GetTime () >= nextFileCheck) {
		// The pictures and floors are looked at again when they are drawn.
		for (std::map<std::string, std::unique_ptr<Picture> >::iterator p = pictures.begin (); p != pictures.end (); ++ p) {
			if (fileTime (p->first) != p->second->time) p->second->time = STALE;
		}
		for (std::map<std::string, FloorFile>::iterator f = floors.begin (); f != floors.end (); ++ f) {
			if (fileTime (f->first) != f->second.time) f->second.time = STALE;
		}
		for (std::map<std::string, std::unique_ptr<BankFile> >::iterator b = banks.begin (); b != banks.end (); ++ b) {
			if (fileTime (b->first) != b->second->time) b->second->time = STALE;
		}
		// The costume subs may have changed.
		subPictures.clear ();
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
	drawNewCharacter ();

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

	if (tab == TAB_OVERLAYS) drawOverlayInspector ();
	else if (tab == TAB_CHARACTERS) drawCharacterInspector ();
	else if (tab == TAB_SCALING) drawScalingInspector ();
	else drawRegionInspector ();
}

void RoomView::drawOverlayInspector () {
	const Room * r = room ();
	ImGui::SeparatorText ("Overlays");
	if (r->overlays.empty ()) ImGui::TextDisabled ("No overlays");
	for (int o = 0; o < (int) r->overlays.size (); o ++) {
		const RoomOverlay & overlay = r->overlays[o];
		ImGui::PushID (o);
		if (ImGui::Selectable (overlay.file.c_str (), o == selectedOverlay)) selectedOverlay = o;
		ImGui::SameLine ();
		int x, y, w, h;
		ImVec4 red (1.0f, 0.45f, 0.4f, 1.0f);
		if (! pictureOf (overlay.file)) {
			ImGui::TextColored (red, "Not found");
		} else if (! overlay.movable ()) {
			ImGui::TextDisabled ("Not a number");
		} else if (overlayShown (o, &x, &y, &w, &h) && (x < 0 || y < 0 || x + w > sceneWidth || y + h > sceneHeight)) {
			ImGui::TextColored (red, "Outside the scene");
		} else {
			std::string detail = overlay.x.text + ", " + overlay.y.text;
			if (overlay.conditional) detail += ", Conditional";
			ImGui::TextDisabled ("%s", detail.c_str ());
		}
		ImGui::PopID ();
	}

	const RoomOverlay * overlay = selectedOverlayOf ();
	if (! overlay) return;
	int x = 0, y = 0, w = 0, h = 0;
	bool shown = overlayShown (selectedOverlay, &x, &y, &w, &h);
	if (! fieldsEditing) {
		fieldOverlay[0] = (float) x;
		fieldOverlay[1] = (float) y;
	}
	if (beginProperties ("Overlay")) {
		propertyRow ("File");
		ImGui::TextUnformatted (overlay->file.c_str ());
		propertyRow ("Function");
		ImGui::TextUnformatted (overlay->mix ? "mixOverlay" : "addOverlay");
		propertyRow ("Position");
		if (shown) {
			bool done = false;
			fieldsEditing |= xyFields ("position", &fieldOverlay[0], &fieldOverlay[1], 1.0f, "%.0f", 0, 0, &done);
			if (done) {
				fieldsEditing = false;
				int nx = (int) fieldOverlay[0], ny = (int) fieldOverlay[1];
				clampOverlay (w, h, &nx, &ny);
				if (nx != x || ny != y) apply (roomEdit::moveOverlay (*overlay, nx, ny), "Move Overlay", overlay->call);
			}
		} else {
			ImGui::TextDisabled (overlay->movable () ? "Not found" : "Not a number");
		}
		ImGui::EndTable ();
	}
	overlay = selectedOverlayOf ();
	if (! overlay) return;
	bool first = true;
	if (wrappedButton ("Show in Script", &first) && showInScript) {
		// addOverlay and mixOverlay are as long.
		showInScript (overlay->call, (int) std::string ("addOverlay").size ());
		return;
	}
	if (wrappedButton ("Delete Overlay", &first)) {
		apply (roomEdit::remove (script->buffer (), *overlay), "Delete Overlay", overlay->call);
		selectedOverlay = -1;
	}
}

// A setting's row: false, with the reason, when the editor can't change it.
static bool settingEditable (const RoomSetting & setting) {
	if (setting.present && ! setting.value.literal) {
		ImGui::TextDisabled ("Not a number");
		return false;
	}
	return true;
}

static void conditionalMark (const RoomSetting & setting) {
	if (! setting.present || ! setting.conditional) return;
	ImGui::SameLine ();
	ImGui::TextDisabled ("Conditional");
}

// A number typed into a field, or none when it is empty.
static bool typedNumber (const std::string & text, int * value, bool * empty) {
	size_t start = text.find_first_not_of (" \t"), end = text.find_last_not_of (" \t");
	*empty = start == std::string::npos;
	if (*empty) return true;
	std::string t = text.substr (start, end - start + 1);
	size_t digits = t[0] == '-' ? 1 : 0;
	if (digits >= t.size () || t.find_first_not_of ("0123456789", digits) != std::string::npos) return false;
	*value = atoi (t.c_str ());
	return true;
}

void RoomView::drawCharacterSettings (const RoomCharacter & character) {
	const Room * r = room ();
	const TextBuffer & text = script->buffer ();

	propertyRow ("Facing");
	if (settingEditable (character.facing)) {
		ImGui::SetNextItemWidth (ImGui::GetFontSize () * 9);
		std::string current = character.facing.present ? directionText (character.angle ()) : std::string ("Default");
		if (ImGui::BeginCombo ("##facing", current.c_str ())) {
			if (ImGui::Selectable ("Default", ! character.facing.present)) apply (roomEdit::setFacing (text, *r, character, -1), "Change Facing", character.call);
			for (int d = 1; d < DIRECTION_COUNT; d ++) {
				std::string name = directionText (directions[d]);
				if (ImGui::Selectable (name.c_str (), character.facing.present && character.angle () == directions[d])) {
					apply (roomEdit::setFacing (text, *r, character, directions[d]), "Change Facing", character.call);
				}
			}
			ImGui::EndCombo ();
		}
		conditionalMark (character.facing);
	}

	propertyRow ("Draw Mode");
	if (settingEditable (character.drawMode)) {
		ImGui::SetNextItemWidth (ImGui::GetFontSize () * 9);
		int mode = character.drawModeValue ();
		if (ImGui::BeginCombo ("##drawMode", drawModeName (mode))) {
			for (int m = 0; m < ROOM_DRAW_MODES; m ++) {
				if (ImGui::Selectable (drawModeName (m), m == mode) && m != mode) {
					apply (roomEdit::setDrawMode (text, *r, character, m), "Change Draw Mode", character.call);
				}
			}
			ImGui::EndCombo ();
		}
		conditionalMark (character.drawMode);
	}

	propertyRow ("Extras");
	if (settingEditable (character.extra)) {
		static const struct { int flag; const char * name; } extras[] = {
			{ ROOM_FRONT, "FRONT" }, { ROOM_FIXEDSIZE, "FIXEDSIZE" }, { ROOM_NOZBUFFER, "NOZBUFFER" },
			{ ROOM_FIXTOSCREEN, "FIXTOSCREEN" }, { ROOM_NOLIGHTMAP, "NOLIGHTMAP" },
			{ ROOM_NOREMOVE, "NOREMOVE" }, { ROOM_RECTANGULAR, "RECTANGULAR" },
		};
		int flags = character.extraFlags ();
		ImGui::BeginGroup ();
		for (int e = 0; e < 7; e ++) {
			bool on = (flags & extras[e].flag) != 0;
			if (ImGui::Checkbox (extras[e].name, &on)) {
				apply (roomEdit::setExtras (text, *r, character, on ? flags | extras[e].flag : flags & ~extras[e].flag), "Change Extras", character.call);
			}
		}
		ImGui::EndGroup ();
		conditionalMark (character.extra);
	}

	// The speed and the depth as typed; written when the field is let go.
	if (! textFieldsEditing) {
		char number[16];
		snprintf (number, sizeof (number), "%d", character.walkSpeedValue ());
		fieldWalkSpeed = character.walkSpeed.present ? std::string (number) : std::string ();
		snprintf (number, sizeof (number), "%d", character.depth.value.value);
		fieldDepth = character.hasDepth () ? std::string (number) : std::string ();
	}
	propertyRow ("Walk Speed");
	if (settingEditable (character.walkSpeed)) {
		ImGui::SetNextItemWidth (ImGui::GetFontSize () * 6);
		ImGui::InputTextWithHint ("##walkSpeed", "Default", &fieldWalkSpeed, ImGuiInputTextFlags_CharsDecimal);
		textFieldsEditing |= ImGui::IsItemActive ();
		if (ImGui::IsItemDeactivated ()) {
			textFieldsEditing = false;
			int speed = 0;
			bool empty;
			if (typedNumber (fieldWalkSpeed, &speed, &empty) && (empty || speed > 0)) {
				apply (roomEdit::setWalkSpeed (text, *r, character, empty ? -1 : speed), "Change Walk Speed", character.call);
			}
		}
		conditionalMark (character.walkSpeed);
	}
	propertyRow ("Depth");
	if (settingEditable (character.depth)) {
		ImGui::SetNextItemWidth (ImGui::GetFontSize () * 6);
		ImGui::InputTextWithHint ("##depth", "None", &fieldDepth, ImGuiInputTextFlags_CharsDecimal);
		textFieldsEditing |= ImGui::IsItemActive ();
		if (ImGui::IsItemDeactivated ()) {
			textFieldsEditing = false;
			int z = 0;
			bool empty;
			if (typedNumber (fieldDepth, &z, &empty)) {
				apply (roomEdit::setDepth (text, *r, character, ! empty, z), "Change Depth", character.call);
			}
		}
		conditionalMark (character.depth);
	}
}

void RoomView::drawCharacterInspector () {
	const Room * r = room ();
	ImGui::SeparatorText ("Characters");
	if (r->characters.empty ()) ImGui::TextDisabled ("No characters");
	for (int c = 0; c < (int) r->characters.size (); c ++) {
		const RoomCharacter & character = r->characters[c];
		ImGui::PushID (c);
		if (ImGui::Selectable (character.objectType.c_str (), c == selectedCharacter)) selectedCharacter = c;
		ImGui::SameLine ();
		CharacterShown shown;
		if (! character.movable ()) {
			ImGui::TextDisabled ("Not a number");
		} else if (characterShown (c, &shown) && ! shown.bank) {
			ImGui::TextDisabled ("No picture");
		} else {
			std::string detail = character.x.text + ", " + character.y.text;
			if (character.conditional) detail += ", Conditional";
			ImGui::TextDisabled ("%s", detail.c_str ());
		}
		ImGui::PopID ();
	}

	const RoomCharacter * character = selectedCharacterOf ();
	if (! character) return;
	CharacterShown shown;
	bool placed = characterShown (selectedCharacter, &shown);
	if (! fieldsEditing && placed) {
		fieldCharacter[0] = (float) shown.x;
		fieldCharacter[1] = (float) shown.y;
	}
	if (beginProperties ("Character")) {
		propertyRow ("Object Type");
		ImGui::TextUnformatted (character->objectType.c_str ());
		propertyRow ("Costume");
		// One line of it; all of it in the tooltip.
		std::string costume;
		for (size_t i = 0; i < character->costume.text.size (); i ++) {
			char ch = character->costume.text[i];
			bool space = ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
			if (! space) costume += ch;
			else if (! costume.empty () && costume[costume.size () - 1] != ' ' && costume[costume.size () - 1] != '(') costume += ' ';
		}
		if (costume.size () > 40) costume = costume.substr (0, 37) + "...";
		ImGui::TextUnformatted (costume.c_str ());
		ImGui::SetItemTooltip ("%s", character->costume.text.c_str ());
		propertyRow ("Position");
		if (placed) {
			bool done = false;
			fieldsEditing |= xyFields ("position", &fieldCharacter[0], &fieldCharacter[1], 1.0f, "%.0f", 0, 0, &done);
			if (done) {
				fieldsEditing = false;
				int nx = (int) fieldCharacter[0], ny = (int) fieldCharacter[1];
				if (nx != shown.x || ny != shown.y) apply (roomEdit::moveCharacter (*character, nx, ny), "Move Character", character->call);
			}
			propertyRow ("Scale");
			ImGui::Text ("%d%%", (int) floorf (shown.scale * 100.0f + 0.5f));
		} else {
			ImGui::TextDisabled ("Not a number");
		}
		drawCharacterSettings (*character);
		ImGui::EndTable ();
	}
	character = selectedCharacterOf ();
	if (! character) return;
	bool first = true;
	if (wrappedButton ("Show in Script", &first) && showInScript) {
		showInScript (character->call, (int) std::string ("addCharacter").size ());
		return;
	}
	if (wrappedButton ("Delete Character", &first)) {
		apply (roomEdit::remove (script->buffer (), *character), "Delete Character", character->call);
		selectedCharacter = -1;
	}
}

void RoomView::drawRegionInspector () {
	const Room * r = room ();
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

bool RoomView::referenceShown (CharacterShown * shown) {
	const Room * r = room ();
	if (referenceBank.empty () || ! r) return false;
	shown->x = referenceX;
	shown->y = referenceY;
	if (scalingSelected == REFERENCE && drag == DRAG_TRY) {
		shown->x = clampX (referenceX + mouseX - pressX);
		shown->y = clampY (referenceY + mouseY - pressY);
	}
	// As a character added at the end of the room would be, or as big as
	// its handles made it.
	shown->scale = referenceScale >= 0 ? referenceScale : scaleAt (r->scale, (float) shown->x, (float) shown->y);
	if (scalingSelected == REFERENCE && drag == DRAG_SIZE) shown->scale = draggedScale (*shown);
	shown->depth = shown->y;
	shown->mirror = false;
	shown->frame = shown->pictureIndex = 0;
	const spriteBank * bank = bankAtPath (referenceBank);
	shown->bank = shown->picture = NULL;
	if (bank && bank->total > 0 && bank->sprites[0].data && shown->scale > 0.05f) {
		const sprite & sp = bank->sprites[0];
		float s = shown->scale;
		shown->x1 = shown->x - sp.xhot * s;
		shown->y1 = shown->y - sp.yhot * s;
		shown->x2 = shown->x1 + sp.width * s;
		shown->y2 = shown->y1 + sp.height * s;
		shown->bank = shown->picture = bank;
	} else {
		float radius = canvas.pixelsOfPoints (7.0f);
		shown->x1 = shown->x - radius;
		shown->y1 = shown->y - radius;
		shown->x2 = shown->x + radius;
		shown->y2 = shown->y + radius;
	}
	return true;
}

bool RoomView::referenceAt (float fx, float fy) {
	CharacterShown shown;
	if (! referenceShown (&shown)) return false;
	if (nearFeet (shown, fx, fy)) return true;
	if (fx < shown.x1 || fx >= shown.x2 || fy < shown.y1 || fy >= shown.y2) return false;
	if (! shown.bank) return true;
	const sprite & sp = shown.bank->sprites[0];
	int px = (int) ((fx - shown.x1) / shown.scale), py = (int) ((fy - shown.y1) / shown.scale);
	if (px < 0 || py < 0 || px >= sp.width || py >= sp.height) return false;
	return shown.bank->type >= 2 ? sp.data[((size_t) py * sp.width + px) * 4 + 3] != 0 : sp.data[(size_t) py * sp.width + px] != 0;
}

float RoomView::draggedScale (const CharacterShown & shown) const {
	// Uniform, from the feet: as far as the handle is from them now.
	float dx = canvas.mouseX () - shown.x, dy = canvas.mouseY () - shown.y;
	float scale = sizeFrom * sqrtf (dx * dx + dy * dy) / sizeDistance;
	return scale < 0.01f ? 0.01f : scale > 10.0f ? 10.0f : scale;
}

int RoomView::sizeHandleAt (float fx, float fy) {
	CharacterShown shown;
	bool has = scalingSelected == REFERENCE ? referenceShown (&shown) : scalingSelected >= 0 && characterShown (scalingSelected, &shown);
	if (! has || ! shown.bank) return -1;
	float near = closeness ();
	const float corners[] = { shown.x1, shown.y1, shown.x2, shown.y1, shown.x2, shown.y2, shown.x1, shown.y2 };
	for (int k = 0; k < 4; k ++) {
		if (fabsf (fx - corners[k * 2]) > near || fabsf (fy - corners[k * 2 + 1]) > near) continue;
		float dx = corners[k * 2] - shown.x, dy = corners[k * 2 + 1] - shown.y;
		sizeDistance = sqrtf (dx * dx + dy * dy);
		// A handle on the feet, as a small character has, moves it instead.
		if (sizeDistance < near * 2) continue;
		sizeFrom = shown.scale;
		return k;
	}
	return -1;
}

void RoomView::handleScalingMouse (float fx, float fy) {
	if (canvas.hovered () && drag == DRAG_NONE && ! canvas.animating ()) {
		float keepFrom = sizeFrom, keepDistance = sizeDistance;
		if (sizeHandleAt (fx, fy) >= 0) ImGui::SetMouseCursor (ImGuiMouseCursor_ResizeNWSE);
		sizeFrom = keepFrom;
		sizeDistance = keepDistance;
	}
	if (! canvas.pressed ()) return;
	pressX = mouseX;
	pressY = mouseY;
	// A handle of the selected character resizes it.
	if (sizeHandleAt (fx, fy) >= 0) {
		drag = DRAG_SIZE;
		return;
	}
	// The reference is in front of the room's characters.
	if (referenceAt (fx, fy)) scalingSelected = REFERENCE;
	else scalingSelected = characterAt (fx, fy);
	if (scalingSelected != -1) drag = DRAG_TRY;
}

// The points of the setScale, from the room's when it changed: the sizes
// stay, and the heights are where the room's setScale gives them.
void RoomView::syncScalePoints () {
	const Room * r = room ();
	if (! r) return;
	const RoomScaleCall & call = r->setScale;
	bool has = call.present && call.first.literal && call.second.literal && call.second.value != 0;
	if (! has) {
		syncedScale = false;
		return;
	}
	if (syncedScale && syncedHorizon == call.first.value && syncedDivide == call.second.value) return;
	syncedScale = true;
	syncedHorizon = call.first.value;
	syncedDivide = call.second.value;
	for (int p = 0; p < 2; p ++) {
		pointY[p] = (int) floorf (syncedHorizon + pointPercent[p] / 100.0f * syncedDivide + 0.5f);
		pointSet[p] = true;
	}
}

void RoomView::applyScalePoints () {
	const Room * r = room ();
	int horizon, divide;
	if (! r || ! pointSet[0] || ! pointSet[1]) return;
	if (! scaleFromPoints (pointY[0], pointPercent[0], pointY[1], pointPercent[1], &horizon, &divide)) {
		say ("Two different sizes at two different heights");
		return;
	}
	if (r->setScale.present && r->setScale.first.literal && r->setScale.second.literal
			&& r->setScale.first.value == horizon && r->setScale.second.value == divide) return;
	apply (roomEdit::setScale (script->buffer (), *r, script->indentUnit (), horizon, divide), "Change Scale", r->at);
	// The points are what the user gave, not the rounded setScale's.
	syncedScale = true;
	syncedHorizon = horizon;
	syncedDivide = divide;
}

void RoomView::drawScalingInspector () {
	const Room * r = room ();
	const TextBuffer & text = script->buffer ();
	CharacterShown chosen;
	bool hasChosen = scalingSelected == REFERENCE ? referenceShown (&chosen)
		: scalingSelected >= 0 && scalingSelected < (int) r->characters.size () && characterShown (scalingSelected, &chosen);

	if (beginProperties ("Selected")) {
		propertyRow ("Character");
		if (! hasChosen) ImGui::TextDisabled ("None");
		else ImGui::TextUnformatted (scalingSelected == REFERENCE ? "Reference" : r->characters[scalingSelected].objectType.c_str ());
		if (hasChosen) {
			propertyRow ("Y");
			ImGui::Text ("%d", chosen.y);
			propertyRow ("Scale");
			ImGui::Text ("%d%%", (int) floorf (chosen.scale * 100.0f + 0.5f));
		}
		ImGui::EndTable ();
	}

	if (r->scaleMap.present) {
		// A scale map: the sizes of its black and white, over the scene.
		if (beginProperties ("Scale Map")) {
			propertyRow ("File");
			ImGui::TextUnformatted (r->scaleMap.file.c_str ());
			if (! pictureOf (r->scaleMap.file)) {
				ImGui::SameLine ();
				ImGui::TextColored (ImVec4 (1.0f, 0.45f, 0.4f, 1.0f), "Not found");
			}
			const RoomArg * limits[2] = { &r->scaleMap.second, &r->scaleMap.third };
			const char * names[2] = { "Minimum", "Maximum" };
			for (int m = 0; m < 2; m ++) {
				propertyRow (names[m]);
				if (! limits[m]->literal) {
					ImGui::TextDisabled ("Not a number");
					continue;
				}
				if (! ImGui::IsAnyItemActive () || ! textFieldsEditing) mapPercent[m] = limits[m]->value;
				ImGui::PushID (m);
				ImGui::SetNextItemWidth (ImGui::GetFontSize () * 5);
				ImGui::InputInt ("##percent", &mapPercent[m], 0, 0);
				textFieldsEditing |= ImGui::IsItemActive ();
				if (ImGui::IsItemDeactivatedAfterEdit ()) {
					textFieldsEditing = false;
					apply (roomEdit::setScaleMap (text, *r, script->indentUnit (), r->scaleMap.file, mapPercent[0], mapPercent[1]), "Change Scale Map", r->at);
				}
				ImGui::SameLine ();
				ImGui::TextUnformatted ("%");
				ImGui::PopID ();
			}
			propertyRow ("Opacity");
			int percent = (int) floorf (mapOpacity * 100.0f + 0.5f);
			if (ImGui::SliderInt ("##opacity", &percent, 0, 100, "%d%%")) mapOpacity = percent / 100.0f;
			if (ImGui::IsItemDeactivatedAfterEdit () && ! script->path ().empty ()) {
				char value[16];
				snprintf (value, sizeof (value), "%.2f", mapOpacity);
				memory.set (script->path (), OPACITY_KEY, value);
			}
			ImGui::EndTable ();
		}
		if (ImGui::Button ("Remove Scale Map")) apply (roomEdit::removeScaleMap (text, *r), "Remove Scale Map", r->at);
		return;
	}

	// setScale, from a size at a height, twice.
	const RoomScaleCall & call = r->setScale;
	bool editable = ! call.present || (call.first.literal && call.second.literal);
	bool changed = false;
	if (beginProperties ("setScale")) {
		const char * sizes[2] = { "Minimum Size", "Maximum Size" };
		const char * heights[2] = { "Minimum Y", "Maximum Y" };
		for (int p = 0; p < 2 && editable; p ++) {
			ImGui::PushID (p);
			propertyRow (sizes[p]);
			ImGui::SetNextItemWidth (ImGui::GetFontSize () * 4);
			ImGui::InputInt ("##percent", &pointPercent[p], 0, 0);
			changed |= ImGui::IsItemDeactivatedAfterEdit ();
			ImGui::SameLine (0, 3);
			ImGui::TextUnformatted ("%");
			propertyRow (heights[p]);
			ImGui::SetNextItemWidth (ImGui::GetFontSize () * 4);
			if (ImGui::InputInt ("##y", &pointY[p], 0, 0)) pointSet[p] = true;
			changed |= ImGui::IsItemDeactivatedAfterEdit ();
			ImGui::PopID ();
		}
		propertyRow ("Script");
		if (! call.present) ImGui::TextDisabled ("None");
		else if (! editable) ImGui::TextDisabled ("Not a number");
		else ImGui::Text ("setScale (%d, %d)", call.first.value, call.second.value);
		ImGui::EndTable ();
	}
	if (! editable) return;
	// The size and the Y of the selected character, for either point.
	bool first = true;
	ImGui::BeginDisabled (! hasChosen);
	const char * buttons[2] = { "Set Minimum Scale", "Set Maximum Scale" };
	for (int p = 0; p < 2; p ++) {
		if (! wrappedButton (buttons[p], &first)) continue;
		pointY[p] = chosen.y;
		pointPercent[p] = (int) floorf (chosen.scale * 100.0f + 0.5f);
		pointSet[p] = changed = true;
		// Once both points make a setScale, the room gives it that size there.
		if (pointSet[0] && pointSet[1]) {
			if (scalingSelected == REFERENCE) referenceScale = -1;
			else triedScales.erase (scalingSelected);
		}
	}
	ImGui::EndDisabled ();
	if (changed) applyScalePoints ();
}

} // namespace devkit
