#ifndef SLUDGE_DEVKIT_ROOM_VIEW_H
#define SLUDGE_DEVKIT_ROOM_VIEW_H

/*
 * The room editor, the Room mode of a script's tab: a room of the script
 * (Core/RoomScript.h) as the game builds it, its overlays on the scene,
 * its floor, and its screen regions over them. Each kind of thing has a
 * tab of its own, with its tools and its part of the Inspector: in the
 * Overlays tab the user adds, moves and removes the pictures, in the
 * Regions tab moves, reshapes and draws the regions, and in the Characters
 * tab adds, moves and removes the characters, drawn standing as the game
 * puts them, at the size it gives them there. The Scaling tab is for
 * setScale and setScaleMap: characters there, and a reference character
 * loaded from disk, are dragged only to try the scale, and two points of
 * a size at a height make the setScale. Every change is a
 * change to the text of the script, which undoes and saves as typing does;
 * and what the user types in the script shows here at once.
 */

#include <stdint.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "CanvasView.h"
#include "FloorDocument.h"
#include "posedraw.h"
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
	// A PNG or TGA dropped on the room becomes a new overlay, a sprite bank
	// a new character.
	bool takeFile (const std::string & path);

	// Show a place of the script in Script mode, with length bytes selected.
	std::function<void (TextPos at, int length)> showInScript;

	enum Tool { TOOL_SELECT, TOOL_RECTANGLE, TOOL_SHAPE, TOOL_COUNT };
	enum Tab { TAB_OVERLAYS, TAB_REGIONS, TAB_CHARACTERS, TAB_SCALING };

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
	// A sprite bank of a costume.
	struct BankFile {
		DevKitAnimBank bank;
		int64_t time;			// Of the file, or STALE to look again
		bool loaded;
		BankFile ();
	};
	// Where a character is drawn: its frame of its bank, and the box of
	// the sprite on the scene.
	struct CharacterShown {
		int x, y;				// Its feet, as a drag leaves them
		float scale;
		const spriteBank * bank;	// NULL for a marker
		int frame;
		bool mirror;
		float x1, y1, x2, y2;
		// What we draw: the frame, or a copy in the colours of its draw mode.
		const spriteBank * picture;
		int pictureIndex;
		int depth;				// The order of drawing, as the game's
	};
	// A frame of a bank in the colours of a draw mode.
	struct TintedFrame {
		struct spriteBank bank;
		TintedFrame ();
	};
	// What the mouse drags.
	enum Drag { DRAG_NONE, DRAG_REGION, DRAG_HANDLE, DRAG_STAND, DRAG_RECTANGLE, DRAG_OVERLAY, DRAG_CHARACTER, DRAG_TRY, DRAG_SIZE };

	void update ();
	const Room * room () const;
	const RoomRegion * selected () const;
	const RoomOverlay * selectedOverlayOf () const;
	const RoomCharacter * selectedCharacterOf () const;
	void setTab (Tab t);
	void chooseRoom (int index);
	std::string pathOf (const std::string & file) const;
	Picture * pictureOf (const std::string & file);
	const Floor * floorOf (const std::string & file);
	void sceneSize (int * width, int * height);
	void overlayPlace (const RoomOverlay & overlay, Picture * picture, int * x, int * y) const;

	void drawToolbar ();
	void handleMouse ();
	void handleOverlayMouse (float x, float y);
	void handleRegionMouse (float x, float y);
	void handleCharacterMouse (float x, float y);
	void handleKeys ();
	void paint (Draw2D & draw);
	void drawLabels ();
	void drawNewRegion ();
	void drawNewCharacter ();
	// The objectType of a new region or character: the project's, or a
	// new name. Sets creating, entered (Enter in the name) and problem.
	std::vector<std::string> objectTypes () const;
	void drawObjectTypeFields (const std::vector<std::string> & types, bool * creating, bool * entered, std::string * problem);
	void startObjectTypeChoice (const std::vector<std::string> & types);
	void drawOverlayInspector ();
	void drawRegionInspector ();
	void drawCharacterInspector ();
	// Its facing, draw mode, extras, walk speed and depth, as rows of the
	// Inspector's table.
	void drawCharacterSettings (const RoomCharacter & character);

	// A frame in the colours of a draw mode, made once.
	const spriteBank * tintedFrame (const std::string & bankFile, const spriteBank * bank, int frame, int mode);
	// The bank of a costume, loaded, or NULL; and of a file on disk.
	const spriteBank * bankOf (const std::string & file);
	const spriteBank * bankAtPath (const std::string & path);
	// The standing picture of a character's costume, looking into a sub
	// when the costume is one.
	CostumePicture pictureOfCharacter (const RoomCharacter & character);
	// The scale of the game at a point, with that setScale or scale map.
	float scaleAt (const RoomScale & scale, float x, float y);
	// How a character is drawn; false when its place isn't numbers.
	bool characterShown (int character, CharacterShown * shown);
	// The character under a point of the scene, the one in front.
	int characterAt (float x, float y);
	// Whether a point is just at a character's feet.
	bool nearFeet (const CharacterShown & shown, float x, float y) const;
	void chooseBankFile ();

	// The Scaling tab.
	void handleScalingMouse (float x, float y);
	void drawScalingInspector ();
	// How the reference character is drawn; false when there is none.
	bool referenceShown (CharacterShown * shown);
	bool referenceAt (float x, float y);
	// The size a handle drag gives the selected character, from its feet.
	float draggedScale (const CharacterShown & shown) const;
	// The corner of the selected character's box under a point, or -1.
	int sizeHandleAt (float x, float y);
	// The points of the setScale from the room's, when it changes.
	void syncScalePoints ();
	// Write the setScale of the two points, when they make one.
	void applyScalePoints ();
	void startNewCharacter (const std::string & bankPath);

	// Where an overlay is on the scene, as a drag leaves it, and its size;
	// false when it has no picture or its place isn't numbers.
	bool overlayShown (int overlay, int * x, int * y, int * width, int * height);
	// The overlay under a point of the scene, where its picture isn't
	// transparent: the one drawn last.
	int overlayAt (float x, float y);
	// Keep an overlay of a size in the scene, as the game must.
	void clampOverlay (int width, int height, int * x, int * y) const;
	void chooseOverlayFile ();
	void addOverlayFile (const std::string & path);
	// A file as the script names it: from the folder of the project.
	std::string scriptName (const std::string & path) const;

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
	std::map<std::string, std::unique_ptr<BankFile> > banks;
	// The pictures of the costume subs, by name and facing, until the files
	// change.
	std::map<std::string, CostumePicture> subPictures;
	std::map<std::string, std::unique_ptr<TintedFrame> > tinted;

	// The Scaling tab: where its drags left the characters (by index), the
	// reference character, which is selected (REFERENCE for it), the two
	// points of the setScale (a size at a height), the sizes of the scale
	// map, and how opaque the map is drawn.
	enum { REFERENCE = -2 };
	std::map<int, std::pair<int, int> > tries;
	// The sizes that its handles gave the characters, and the reference
	// (below 0 for none), until they move; and a drag of a handle: the
	// size and the distance of the handle from the feet when it began.
	std::map<int, float> triedScales;
	float referenceScale;
	float sizeFrom, sizeDistance;
	std::string referenceBank;		// With its folder, or empty
	int referenceX, referenceY;
	int scalingSelected;
	int pointPercent[2], pointY[2];
	bool pointSet[2];
	int syncedHorizon, syncedDivide;	// The setScale that the points are of
	bool syncedScale;
	int mapPercent[2];
	float mapOpacity;
	std::shared_ptr<std::string> chosenReference, chosenScaleMap;
	double nextFileCheck;
	bool showFloor;
	int sceneWidth, sceneHeight;

	Tab tab;
	bool tabPending;			// Make tab the selected one of the tab bar
	Tool tool;
	int selectedRegion, selectedPoint;	// -1 for none
	int selectedOverlay, selectedCharacter;
	// Select the region, overlay or character that starts here when we
	// read the rooms again.
	bool selectPending, selectOverlayPending, selectCharacterPending;
	TextPos selectAt;
	// The answer of the file dialog of Add Overlay, which may come after we
	// are gone.
	std::shared_ptr<std::string> chosenOverlay;
	// The same for the sprite bank of a new character.
	std::shared_ptr<std::string> chosenBank;

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

	// A character that waits for its objectType and costume: the costume
	// subs of the project, which of them (or -1 for the bank), the bank.
	bool newCharacterPending, openCharacterPopup;
	std::vector<std::string> costumeSubList;
	int newCostume;
	std::string newBank;		// With its folder

	// The fields of the Inspector, while the user changes them.
	float fieldTopLeft[2], fieldBottomRight[2], fieldStand[2], fieldOverlay[2], fieldCharacter[2];
	// The walk speed and depth of a character as typed, while typed.
	std::string fieldWalkSpeed, fieldDepth;
	bool textFieldsEditing;
	bool fieldsEditing;

	std::string notice;
	double noticeUntil;
};

} // namespace devkit

#endif
