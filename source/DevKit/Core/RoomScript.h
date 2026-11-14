#ifndef SLUDGE_DEVKIT_ROOM_SCRIPT_H
#define SLUDGE_DEVKIT_ROOM_SCRIPT_H

/*
 * The rooms of a script, for the room editor: the subs that put pictures on
 * the backdrop (addOverlay, mixOverlay), lay a floor (setFloor) or make
 * screen regions (addScreenRegion). We read what those calls say, and make
 * the changes to their text that the editor's tools stand for, so that the
 * script stays the only place where a room is.
 *
 * Only numbers written as numbers (and the direction constants) can be
 * changed; a region made of sums, or of points in a variable, shows as it
 * is and stays as it is.
 *
 * The positions are in scene coordinates, those of the backdrop, as the
 * engine has them: addOverlay pastes at x, y, and the regions are tested
 * against the mouse plus the camera.
 */

#include <functional>
#include <string>
#include <vector>

#include "ScriptDocument.h"
#include "TextBuffer.h"

namespace devkit {

// An argument of a call, as written.
struct RoomArg {
	TextPos from, to;
	std::string text;
	// Whether it is a whole number (maybe after -), or a constant that
	// stands for one, such as NORTH or CENTRE: one that we can read and
	// write.
	bool literal;
	int value;
	RoomArg () : literal (false), value (0) {}
};

// The value of CENTRE and CENTER, which centres an overlay in the scene.
enum { ROOM_CENTRE = 65535 };

struct RoomOverlay {
	std::string file;		// As written, without the quotes
	RoomArg x, y;
	bool mix;				// mixOverlay
	bool conditional;		// In an if, a loop or a block
	TextPos call;			// The name of the function
	TextPos statementEnd;	// After its ; (or its ) when there is none)
	RoomOverlay () : mix (false), conditional (false) {}
	// Whether its place is numbers (or CENTRE), which we can move.
	bool movable () const { return x.literal && y.literal; }
};

struct RoomRegion {
	std::string objectType;
	TextPos call;					// The name, addScreenRegion
	TextPos open, close;			// The ( and the ) of the call
	TextPos statementEnd;			// After its ; (or its ) when there is none)
	bool shape;						// Made of points, not x1, y1, x2, y2
	// A rectangle: x1, y1, x2, y2. A shape: the x and y of each point.
	std::vector<RoomArg> coordinates;
	// A shape whose points are newStack (...), maybe in makeFastArray (...):
	// the ( and ) of newStack. Otherwise the points are elsewhere.
	TextPos pointsOpen, pointsClose;
	RoomArg pointsArgument;			// The whole argument of the points
	RoomArg standX, standY, direction;
	bool editable;					// Whether all its coordinates are numbers
	std::string whyNot;				// Why not, briefly
	bool conditional;
	RoomRegion () : shape (false), editable (false), conditional (false) {}

	// The values of the coordinates.
	std::vector<int> values () const;
	// The outline, as points: the four corners of a rectangle.
	std::vector<int> outline () const;
	bool walks () const { return standX.literal && standY.literal && (standX.value || standY.value); }
	bool contains (float x, float y) const;
};

// What a costume looks like when its character stands still, facing south,
// as addCharacter puts it: the first frame of its first animation.
struct CostumePicture {
	std::string bank;		// The sprite bank, as written
	int frame;
	bool mirror;			// A negative frame number
	bool known;				// False for NULL, clips, variables...
	CostumePicture () : frame (0), mirror (false), known (false) {}
};

// How the game scales characters at a point of the script: by setScale
// (horizon, divide), or by the picture of setScaleMap, which wins.
struct RoomScale {
	bool set;				// A setScale with numbers
	int horizon, divide;
	std::string map;		// The file of setScaleMap, or empty
	int mapMin, mapMax;		// Its percentages
	RoomScale () : set (false), horizon (0), divide (0), mapMin (100), mapMax (100) {}
	// The scale at a height, without a map: 1 when nothing sets it.
	float at (float y) const { return set && divide ? (y - horizon) / (float) divide : 1.0f; }
};

// A call that sets a character up after addCharacter, as
// setCharacterDrawMode (duck, DARK1): its value, and where it is.
struct RoomSetting {
	bool present;
	RoomArg value;			// literal when we can read it (a sum of flags too)
	TextPos call, statementEnd;
	bool conditional;
	RoomSetting () : present (false), conditional (false) {}
};

// The extra flags of setCharacterExtra, as Engine/people.h has them.
enum {
	ROOM_FRONT = 1, ROOM_FIXEDSIZE = 2, ROOM_NOZBUFFER = 4, ROOM_FIXTOSCREEN = 8, ROOM_NOLIGHTMAP = 16,
	ROOM_ICON = 31, ROOM_NOREMOVE = 32, ROOM_RECTANGULAR = 64
};
// The draw modes of setCharacterDrawMode, 0 (NORMAL) to 19 (INVISIBLE).
enum { ROOM_DRAW_MODES = 20, ROOM_INVISIBLE = 19 };

struct RoomCharacter {
	std::string objectType;
	TextPos call, statementEnd;
	RoomArg x, y;
	RoomArg costume;			// The whole costume argument
	CostumePicture picture;		// Read from it, when it says
	// When the costume is a call of a sub without arguments, as
	// makeEgoAnim (): its name, to look into.
	std::string costumeSub;
	RoomScale scale;			// As the game has it at this call
	bool conditional;
	// The first of each of these after the addCharacter, for this character.
	RoomSetting drawMode, extra, walkSpeed, facing, depth;
	RoomCharacter () : conditional (false) {}
	// What they come to: NORMAL, no extras, the objectType's speed (-1),
	// facing south (180), no fixed depth.
	int drawModeValue () const { return drawMode.present && drawMode.value.literal ? drawMode.value.value : 0; }
	int extraFlags () const { return extra.present && extra.value.literal ? extra.value.value : 0; }
	int walkSpeedValue () const { return walkSpeed.present && walkSpeed.value.literal ? walkSpeed.value.value : -1; }
	int angle () const { return facing.present && facing.value.literal ? facing.value.value : 180; }
	bool hasDepth () const { return depth.present && depth.value.literal; }
	bool movable () const { return x.literal && y.literal; }
};

struct Room {
	std::string name;				// Of the sub
	TextPos at;						// Of its name
	TextPos open, close;			// Its { and }
	std::vector<RoomOverlay> overlays;
	std::vector<RoomRegion> regions;
	std::vector<RoomCharacter> characters;
	RoomScale scale;				// As the game has it at the end of the sub
	std::string floor;				// The file of setFloor, as written
	int sceneWidth, sceneHeight;	// Of setSceneDimensions, or 0
	// Where a new region goes: on a line after the line of this statement,
	// indented as the line where it starts; or, with none, after the {.
	bool hasAnchor;
	TextPos anchorStart, anchorEnd;
	// The same for a new overlay: after the last overlay, else after
	// setSceneDimensions (which wipes the backdrop, so it comes first).
	bool hasOverlayAnchor;
	TextPos overlayAnchorStart, overlayAnchorEnd;
	// The same for a new character: after the last one, else where a new
	// region goes.
	bool hasCharacterAnchor;
	TextPos characterAnchorStart, characterAnchorEnd;
	Room () : sceneWidth (0), sceneHeight (0), hasAnchor (false), hasOverlayAnchor (false), hasCharacterAnchor (false) {}
};

// The rooms of a script, in order.
std::vector<Room> readRooms (const TextBuffer & text);

// The picture of the costume that a sub without parameters returns, from
// the first return of the sub of that name in a script's text.
// facing is the angle that the character faces, which picks the costume's
// standing animation for it.
bool costumeOfSub (const TextBuffer & text, const std::string & sub, CostumePicture * picture, int facing = 180);
// Which of n directions of a costume an angle shows, as turnMeAngle
// (Engine/people.cpp) works it out: 0 is the first, south.
int facingDirection (int angle, int directions);

// The value of a constant of the compiler (CommonCode/constantlist.h).
bool constantValue (const std::string & name, int * value);
// The name of a draw mode to write: the first of its names.
const char * drawModeName (int mode);
// The extra flags as a script writes them: ICON + NOREMOVE, or NORMAL.
std::string extrasText (int flags);
// The subs without parameters, outside objectTypes, of some scripts that
// return a costume or an animation, sorted: for a new character. textOf
// gives the text of a file, or false.
std::vector<std::string> costumeSubs (const std::vector<std::string> & files,
	std::function<bool (const std::string & file, TextBuffer * text)> textOf);

// The name of a direction: NORTH... for 0, 45... 315, else the number.
std::string directionText (int direction);

// The changes that the tools make, for ScriptDocument::edit. Each list is
// in the order that edit wants: from the end of the text to its start, so
// that each change leaves the places of the next ones as they were. A
// change of an argument that isn't literal is left out.
namespace roomEdit {
	std::vector<TextChange> setRectangle (const RoomRegion & region, int x1, int y1, int x2, int y2);
	// Change the points of a shape: one by one when there are as many as
	// before, else all of them.
	std::vector<TextChange> setPoints (const RoomRegion & region, const std::vector<int> & points);
	// Move a rectangle or a shape by dx, dy.
	std::vector<TextChange> move (const RoomRegion & region, int dx, int dy);
	std::vector<TextChange> setStandPoint (const RoomRegion & region, int x, int y);
	std::vector<TextChange> setDirection (const RoomRegion & region, int direction);
	// A rectangle as a shape of its four corners.
	std::vector<TextChange> toShape (const RoomRegion & region);
	// Take away the call, with its lines when it is alone on them.
	std::vector<TextChange> remove (const TextBuffer & text, const RoomRegion & region);
	std::vector<TextChange> remove (const TextBuffer & text, const RoomOverlay & overlay);
	// The same for any statement, from its first token to its end.
	std::vector<TextChange> removeStatement (const TextBuffer & text, TextPos call, TextPos statementEnd);

	// Put an overlay at x, y: numbers, also in the place of CENTRE.
	std::vector<TextChange> moveOverlay (const RoomOverlay & overlay, int x, int y);
	// A new addOverlay of file (as the compiler finds it, from the folder
	// of the project) at x, y: after the room's overlays, else after
	// setSceneDimensions, else at the start of the sub. where is set to
	// the start of the new call.
	std::vector<TextChange> addOverlay (const TextBuffer & text, const Room & room, const std::string & indentUnit,
		const std::string & file, int x, int y, TextPos * where);

	// Put a character at x, y.
	std::vector<TextChange> moveCharacter (const RoomCharacter & character, int x, int y);
	std::vector<TextChange> remove (const TextBuffer & text, const RoomCharacter & character);
	// A new addCharacter of objectType at x, y with a costume (as code):
	// after the room's characters, else where a new region goes. With
	// newObjectType, also an empty objectType after the sub.
	std::vector<TextChange> addCharacter (const TextBuffer & text, const Room & room, const std::string & indentUnit,
		const std::string & objectType, bool newObjectType, int x, int y, const std::string & costume, TextPos * where);

	// The settings of a character after its addCharacter. Each changes the
	// call that is there, takes it away for the default (NORMAL, no
	// extras, the objectType's speed -1, facing -1, no depth), or adds one
	// on a line after the character's last setting, else after its
	// addCharacter. A setting that isn't a number is left alone.
	std::vector<TextChange> setDrawMode (const TextBuffer & text, const Room & room, const RoomCharacter & character, int mode);
	std::vector<TextChange> setExtras (const TextBuffer & text, const Room & room, const RoomCharacter & character, int flags);
	std::vector<TextChange> setWalkSpeed (const TextBuffer & text, const Room & room, const RoomCharacter & character, int speed);
	std::vector<TextChange> setFacing (const TextBuffer & text, const Room & room, const RoomCharacter & character, int angle);
	std::vector<TextChange> setDepth (const TextBuffer & text, const Room & room, const RoomCharacter & character, bool fixed, int z);

	// A new region of objectType, a rectangle (4 numbers) or a shape (6 or
	// more): after the last region of the room that isn't in an if, else
	// after the last call that sets up the room, else at the start of the
	// sub. With newObjectType, also an empty objectType after the sub.
	// where is set to the start of the new call.
	std::vector<TextChange> add (const TextBuffer & text, const Room & room, const std::string & indentUnit,
		const std::string & objectType, bool newObjectType, bool shape, const std::vector<int> & coordinates,
		int standX, int standY, int direction, TextPos * where);
}

// Whether a name can be that of a new objectType.
bool isIdentifier (const std::string & name);

} // namespace devkit

#endif
