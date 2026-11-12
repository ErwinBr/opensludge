#ifndef SLUDGE_DEVKIT_ROOM_VIEW_H
#define SLUDGE_DEVKIT_ROOM_VIEW_H

/*
 * The room editor, the Room mode of a script's tab: a room of the script
 * (Core/RoomScript.h) as the game builds it, its overlays on the scene,
 * its floor, and its screen regions over them, which the user moves,
 * reshapes and draws. Every change is a change to the text of the script,
 * which undoes and saves as typing does; and what the user types in the
 * script shows here at once.
 */

#include <stdint.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "CanvasView.h"
#include "FloorDocument.h"
#include "RoomScript.h"
#include "ScriptDocument.h"
#include "sprites.h"

namespace devkit {

class FileMemory;
class WorkspaceActions;

class RoomView {
public:
	RoomView (ScriptDocument * script, WorkspaceActions & actions, const CanvasGraphics & graphics, FileMemory & memory);
	~RoomView ();

	void draw ();
	void drawInspector ();
	bool animating () const;

	// Show a place of the script in Script mode, with length bytes selected.
	std::function<void (TextPos at, int length)> showInScript;

	enum Tool { TOOL_SELECT, TOOL_RECTANGLE, TOOL_SHAPE, TOOL_COUNT };

private:
	// A picture of an overlay, as the floor editor loads its backdrop.
	struct Picture {
		struct spriteBank bank;
		int64_t time;			// Of the file, or STALE to look again
		bool loaded;
		Picture ();
	};
	struct FloorFile {
		Floor floor;
		int64_t time;			// Of the file, or STALE to look again
		bool loaded;
		FloorFile ();
	};
	// What the mouse drags.
	enum Drag { DRAG_NONE, DRAG_REGION, DRAG_HANDLE, DRAG_STAND, DRAG_RECTANGLE };

	void update ();
	const Room * room () const;
	const RoomRegion * selected () const;
	void chooseRoom (int index);
	std::string pathOf (const std::string & file) const;
	Picture * pictureOf (const std::string & file);
	const Floor * floorOf (const std::string & file);
	void sceneSize (int * width, int * height);
	void overlayPlace (const RoomOverlay & overlay, Picture * picture, int * x, int * y) const;

	void drawToolbar ();
	void handleMouse ();
	void handleKeys ();
	void paint (Draw2D & draw);
	void drawLabels ();
	void drawNewRegion ();

	// The coordinates of a region as they are shown: as a drag leaves them.
	std::vector<int> shownValues (int region) const;
	void standShown (int region, int * x, int * y) const;
	// The handles of a region: each the index of its x and y in its
	// coordinates, or -1 for a coordinate that it doesn't move.
	std::vector<std::pair<int, int> > handlesOf (const RoomRegion & region) const;
	void handlePlace (const std::vector<int> & values, const std::pair<int, int> & handle, float * x, float * y) const;
	float closeness () const;
	int regionAt (float x, float y) const;
	int clampX (int x) const;
	int clampY (int y) const;

	void apply (const std::vector<TextChange> & changes, const std::string & label, TextPos at);
	void finishDrag ();
	void finishShape ();
	void say (const std::string & text);

	ScriptDocument * script;
	WorkspaceActions & actions;
	CanvasGraphics graphics;
	FileMemory & memory;
	CanvasView canvas;

	std::vector<Room> rooms;
	uint64_t roomsVersion;
	bool read;
	std::string roomName;		// The room that we show
	int roomIndex;				// Its index in rooms, or -1

	// The files of the script on disk, by their names in it.
	mutable std::map<std::string, std::string> paths;
	std::map<std::string, std::unique_ptr<Picture> > pictures;
	std::map<std::string, FloorFile> floors;
	double nextFileCheck;
	bool showFloor;
	int sceneWidth, sceneHeight;

	Tool tool;
	int selectedRegion, selectedPoint;	// -1 for none
	// Select the region that starts here when we read the rooms again.
	bool selectPending;
	TextPos selectAt;

	Drag drag;
	std::pair<int, int> dragHandle;
	int pressX, pressY, mouseX, mouseY;

	// A shape that the user draws, and a region drawn that waits for its
	// objectType.
	std::vector<int> drawnPoints;
	bool newPending, newShape, openNewPopup;
	std::vector<int> newCoordinates;
	int newChoice;				// In the objectTypes, or their count for a new one
	std::string newName;
	bool focusNewName;
	std::string lastType;		// That a region was added with

	// The fields of the Inspector, while the user changes them.
	float fieldTopLeft[2], fieldBottomRight[2], fieldStand[2];
	bool fieldsEditing;

	std::string notice;
	double noticeUntil;
};

} // namespace devkit

#endif
