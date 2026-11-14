#include <stdlib.h>

#include <algorithm>

#include "../../Engine/regionshape.h"
#include "RoomScript.h"
#include "ScriptLexer.h"
#include "ScriptTokens.h"

namespace devkit {

namespace {

struct NamedValue {
	const char * name;
	int value;
};

// The constants that a room's numbers may be. CommonCode/constantlist.h
// has them all.
const NamedValue roomConstants[] = {
	{ "NORTH", 0 }, { "NORTHEAST", 45 }, { "EAST", 90 }, { "SOUTHEAST", 135 },
	{ "SOUTH", 180 }, { "SOUTHWEST", 225 }, { "WEST", 270 }, { "NORTHWEST", 315 },
	{ "CENTRE", ROOM_CENTRE }, { "CENTER", ROOM_CENTRE },
};

struct ConstantText {
	const char * name;
	const char * value;
};

const ConstantText compilerConstants[] = {
#define CONSTANT(n, v) { #n, v },
#include "constantlist.h"
#undef CONSTANT
};

const char * const drawModeNames[ROOM_DRAW_MODES] = {
	"NORMAL", "TRANSPARENT3", "TRANSPARENT2", "TRANSPARENT1", "DARK1", "DARK2", "DARK3", "DARK4",
	"SHADOW1", "SHADOW2", "SHADOW3", "FOGGY1", "FOGGY2", "FOGGY3", "FOGGY4",
	"GLOW1", "GLOW2", "GLOW3", "GLOW4", "INVISIBLE"
};

bool allDigits (const std::string & s) {
	if (s.empty ()) return false;
	for (size_t i = 0; i < s.size (); i ++) {
		if (s[i] < '0' || s[i] > '9') return false;
	}
	return true;
}

std::string numberText (int n) {
	char text[16];
	snprintf (text, sizeof (text), "%d", n);
	return text;
}

std::string leadingSpace (const std::string & line) {
	size_t n = line.find_first_not_of (" \t");
	return line.substr (0, n == std::string::npos ? line.size () : n);
}

bool onlySpace (const std::string & s) {
	return s.find_first_not_of (" \t") == std::string::npos;
}

bool byPlaceDescending (const TextChange & a, const TextChange & b) {
	return b.from < a.from;
}

std::vector<TextChange> ordered (std::vector<TextChange> changes) {
	std::stable_sort (changes.begin (), changes.end (), byPlaceDescending);
	return changes;
}

void change (std::vector<TextChange> & changes, const RoomArg & arg, const std::string & with) {
	if (arg.literal && arg.text != with) changes.push_back (TextChange (arg.from, arg.to, with));
}

void change (std::vector<TextChange> & changes, const RoomArg & arg, int value) {
	if (arg.literal && arg.value != value) change (changes, arg, numberText (value));
}

std::string joined (const std::vector<int> & values) {
	std::string s;
	for (size_t i = 0; i < values.size (); i ++) s += (i ? ", " : "") + numberText (values[i]);
	return s;
}

// No sub to look in for variables.
const size_t NO_SCOPE = (size_t) -1;

// The value that a variable got last before a token, in the code of a sub
// whose { is at scope: the expression of var name = ... or name = ..., to
// its ; or the , before the next variable. False when nothing there sets it.
bool valueOf (const std::vector<FileToken> & t, size_t scope, size_t before, const std::string & name, size_t * from, size_t * to) {
	bool found = false;
	for (size_t k = scope + 1; k + 2 < before && k + 1 < t.size (); k ++) {
		if (t[k].text != name || t[k + 1].text != "=") continue;
		// At the start of a statement, or after var or a comma of var.
		const std::string & previous = t[k - 1].text;
		if (previous != "var" && previous != "{" && previous != "}" && previous != ";" && previous != ",") continue;
		size_t e = k + 2;
		int depth = 0;
		while (e < before) {
			const std::string & w = t[e].text;
			if (t[e].kind == TOKEN_BRACKET) depth += (w == "(" || w == "[" || w == "{") ? 1 : -1;
			else if (depth == 0 && (w == ";" || w == ",")) break;
			e ++;
		}
		*from = k + 2;
		*to = e;
		found = true;
	}
	return found;
}

// The picture of a costume written from token from to before to: costume
// (...) takes its first animation; anim ('bank', frame...) its first frame.
// A call of a sub without arguments gives its name in *sub. A variable of
// the sub whose { is at scope is followed to the value it got before.
CostumePicture costumeIn (const std::vector<FileToken> & t, size_t from, size_t to, std::string * sub, size_t scope, int angle = 180, int depth = 0) {
	CostumePicture picture;
	if (to <= from || to > t.size () || depth > 8) return picture;
	if (to - from == 1 && scope != NO_SCOPE && (t[from].kind == TOKEN_IDENTIFIER || t[from].kind == TOKEN_VARIABLE)) {
		size_t valueFrom, valueTo;
		if (valueOf (t, scope, from, t[from].text, &valueFrom, &valueTo)) return costumeIn (t, valueFrom, valueTo, sub, scope, angle, depth + 1);
		return picture;
	}
	size_t last = to - 1;
	bool call = to - from >= 3 && t[from + 1].text == "(" && matchingToken (t, from + 1) == last;
	if (! call) return picture;
	const std::string & name = t[from].text;
	std::vector<TokenRange> args = splitArguments (t, from + 1, last);
	if (name == "costume") {
		if (args.empty ()) return picture;
		// Standing, walking, talking: each in n directions. The standing
		// one that the character faces.
		int directions = args.size () >= 3 ? (int) args.size () / 3 : 1;
		size_t stand = (size_t) facingDirection (angle, directions);
		if (stand >= args.size ()) stand = 0;
		return costumeIn (t, args[stand].from, args[stand].to, NULL, scope, angle, depth + 1);
	}
	if (name == "anim") {
		if (args.size () < 2 || args[0].to - args[0].from != 1 || t[args[0].from].kind != TOKEN_FILE) return picture;
		const std::string & q = t[args[0].from].text;
		picture.bank = q.size () >= 2 ? q.substr (1, q.size () - 2) : std::string ();
		for (size_t a = 1; a < args.size (); a ++) {
			size_t f = args[a].from, n = args[a].to - args[a].from;
			// wait (frame, ticks): its frame.
			if (n >= 3 && t[f].text == "wait" && t[f + 1].text == "(") {
				std::vector<TokenRange> inner = splitArguments (t, f + 1, matchingToken (t, f + 1));
				if (inner.empty ()) return picture;
				f = inner[0].from;
				n = inner[0].to - inner[0].from;
			}
			if (n == 1 && t[f].kind == TOKEN_NUMBER && allDigits (t[f].text)) {
				picture.frame = atoi (t[f].text.c_str ());
				picture.known = true;
				return picture;
			}
			if (n == 2 && t[f].text == "-" && t[f + 1].kind == TOKEN_NUMBER && allDigits (t[f + 1].text)) {
				// As drawPeople (Engine/people.cpp): the sprite mirrored.
				picture.frame = atoi (t[f + 1].text.c_str ());
				picture.mirror = true;
				picture.known = true;
				return picture;
			}
			// A sound or a function, which is an event of the next frame.
		}
		return picture;
	}
	if (sub && args.empty ()) *sub = name;
	return picture;
}

// The first return of the sub of a name, outside objectTypes: the tokens of
// its expression, and the { of the sub. False when there is none.
bool returnOf (const std::vector<FileToken> & t, const std::string & name, size_t * from, size_t * to, size_t * scope = NULL) {
	for (size_t i = 0; i < t.size (); i ++) {
		if (t[i].text == "objectType") {
			while (i < t.size () && t[i].text != "{") i ++;
			if (i < t.size ()) i = matchingToken (t, i);
			continue;
		}
		if (t[i].text != "sub") continue;
		size_t k = i + 1;
		while (k < t.size () && t[k].kind == TOKEN_KEYWORD) k ++;
		if (k + 2 >= t.size () || t[k].text != name || t[k + 1].text != "(") continue;
		size_t close = matchingToken (t, k + 1);
		if (close + 1 >= t.size () || t[close + 1].text != "{") continue;
		size_t end = matchingToken (t, close + 1);
		for (size_t r = close + 2; r < end && r < t.size (); r ++) {
			if (t[r].text != "return") continue;
			size_t e = r + 1;
			int depth = 0;
			while (e < end) {
				const std::string & w = t[e].text;
				if (t[e].kind == TOKEN_BRACKET) depth += (w == "(" || w == "[" || w == "{") ? 1 : -1;
				else if (w == ";" && depth == 0) break;
				e ++;
			}
			*from = r + 1;
			*to = e;
			if (scope) *scope = close + 1;
			return true;
		}
		return false;
	}
	return false;
}

class Reader {
public:
	Reader (const TextBuffer & text) : text (text), t (fileTokens (text)) {}

	std::vector<Room> readAll () {
		std::vector<Room> rooms;
		size_t i = 0;
		while (i < t.size ()) {
			const std::string & word = t[i].text;
			if (word == "objectType") {
				// Its subs and events aren't rooms.
				while (i < t.size () && t[i].text != "{") i ++;
				if (i < t.size ()) i = matchingToken (t, i) + 1;
			} else if (word == "sub") {
				Room room;
				i = readSub (i, room);
				if (! room.overlays.empty () || ! room.regions.empty () || ! room.floor.empty () || ! room.characters.empty ()) rooms.push_back (room);
			} else if (word == "{") {
				i = matchingToken (t, i) + 1;
			} else {
				i ++;
			}
		}
		return rooms;
	}

private:
	bool is (size_t k, const char * word) const { return k < t.size () && t[k].text == word; }

	// The sub that starts at i; returns where it ends.
	size_t readSub (size_t i, Room & room) {
		i ++;
		// unfreezable, debug...
		while (i < t.size () && t[i].kind == TOKEN_KEYWORD) i ++;
		if (i >= t.size ()) return i;
		room.name = t[i].text;
		room.at = t[i].at;
		i ++;
		if (! is (i, "(")) return i;
		i = matchingToken (t, i) + 1;
		if (! is (i, "{")) return i;
		size_t open = i, close = matchingToken (t, i);
		if (close >= t.size ()) return close;
		room.open = t[open].at;
		room.close = t[close].at;
		readBody (open, close, room);
		return close + 1;
	}

	void readBody (size_t open, size_t close, Room & room) {
		int braces = 0, brackets = 0;
		bool guarded = false, atStart = true;
		bool floorTopLevel = false, afterRegion = false, afterOverlay = false;
		RoomScale scale;
		// The costume of each character of the room, to read again once we
		// know which way it faces.
		std::vector<TokenRange> costumes;
		for (size_t k = open + 1; k < close; k ++) {
			const FileToken & token = t[k];
			const std::string & word = token.text;
			bool conditional = braces > 0 || guarded;
			if (token.kind == TOKEN_BRACKET) {
				char c = word[0];
				if (c == '{') {
					braces ++;
					atStart = true;
					continue;
				}
				if (c == '}') {
					if (braces > 0 && -- braces == 0) guarded = false;
					atStart = true;
					continue;
				}
				if (c == '(' || c == '[') brackets ++;
				else brackets --;
			}
			if (word == ";" && brackets == 0) {
				if (braces == 0) guarded = false;
				atStart = true;
				continue;
			}
			if (atStart && brackets == 0 && (word == "if" || word == "else" || word == "for" || word == "while" || word == "loop")) guarded = true;
			atStart = false;

			if (! is (k + 1, "(") || (k > 0 && t[k - 1].text == ".")) continue;
			size_t callOpen = k + 1, callClose = matchingToken (t, callOpen);
			if (callClose >= close) continue;
			std::vector<TokenRange> args = splitArguments (t, callOpen, callClose);
			TextPos end = is (callClose + 1, ";") ? t[callClose + 1].end : t[callClose].end;
			bool setup = false;
			if (word == "addScreenRegion") {
				RoomRegion region;
				if (readRegion (args, region)) {
					region.call = token.at;
					region.open = t[callOpen].at;
					region.close = t[callClose].at;
					region.statementEnd = end;
					region.conditional = conditional;
					room.regions.push_back (region);
					if (! conditional) {
						room.hasAnchor = afterRegion = true;
						room.anchorStart = token.at;
						room.anchorEnd = end;
					}
				}
			} else if ((word == "addOverlay" || word == "mixOverlay") && args.size () == 3) {
				std::string file = fileOf (args[0]);
				if (! file.empty ()) {
					RoomOverlay overlay;
					overlay.file = file;
					overlay.x = argument (args[1]);
					overlay.y = argument (args[2]);
					overlay.mix = word == "mixOverlay";
					overlay.conditional = conditional;
					overlay.call = token.at;
					overlay.statementEnd = end;
					room.overlays.push_back (overlay);
				}
				setup = true;
			} else if (word == "setFloor" && args.size () == 1) {
				std::string file = fileOf (args[0]);
				if (! file.empty () && (room.floor.empty () || (! floorTopLevel && ! conditional))) {
					room.floor = file;
					floorTopLevel = ! conditional;
				}
				setup = true;
			} else if (word == "setSceneDimensions" && args.size () == 2) {
				RoomArg w = argument (args[0]), h = argument (args[1]);
				if (w.literal && h.literal && (room.sceneWidth == 0 || ! conditional)) {
					room.sceneWidth = w.value;
					room.sceneHeight = h.value;
				}
				setup = true;
			} else if (word == "setScale" && args.size () == 2) {
				RoomArg horizon = argument (args[0]), divide = argument (args[1]);
				if (horizon.literal && divide.literal) {
					scale.set = true;
					scale.horizon = horizon.value;
					scale.divide = divide.value;
				}
				setup = true;
			} else if (word == "setScaleMap") {
				std::string file = args.empty () ? std::string () : fileOf (args[0]);
				if (args.size () == 3 && ! file.empty ()) {
					RoomArg low = argument (args[1]), high = argument (args[2]);
					scale.map = file;
					scale.mapMin = low.literal ? low.value : 100;
					scale.mapMax = high.literal ? high.value : 100;
				} else {
					scale.map.clear ();
				}
				setup = true;
			} else if (word == "setZBuffer") {
				setup = true;
			} else if (word == "addCharacter" && args.size () == 4 && ! args[0].empty ()) {
				RoomCharacter character;
				character.objectType = text.textIn (t[args[0].from].at, t[args[0].to - 1].end);
				character.call = token.at;
				character.statementEnd = end;
				character.x = argument (args[1]);
				character.y = argument (args[2]);
				character.costume = argument (args[3]);
				character.picture = costumeIn (t, args[3].from, args[3].to, &character.costumeSub, open);
				costumes.push_back (args[3]);
				character.scale = scale;
				character.conditional = conditional;
				room.characters.push_back (character);
				if (! conditional) {
					room.hasCharacterAnchor = true;
					room.characterAnchorStart = token.at;
					room.characterAnchorEnd = end;
				}
			}
			if ((word == "setCharacterDrawMode" || word == "setCharacterExtra" || word == "setCharacterWalkSpeed"
					|| word == "turnCharacter" || word == "setCharacterZ") && args.size () == 2 && ! args[0].empty ()) {
				// The latest character of that objectType before it.
				std::string objectType = text.textIn (t[args[0].from].at, t[args[0].to - 1].end);
				for (int c = (int) room.characters.size () - 1; c >= 0; c --) {
					RoomCharacter & character = room.characters[c];
					if (character.objectType != objectType) continue;
					RoomSetting & setting = word == "setCharacterDrawMode" ? character.drawMode : word == "setCharacterExtra" ? character.extra
						: word == "setCharacterWalkSpeed" ? character.walkSpeed : word == "turnCharacter" ? character.facing : character.depth;
					if (! setting.present) {
						setting.present = true;
						setting.value = word == "setCharacterExtra" ? flags (args[1]) : argument (args[1]);
						setting.call = token.at;
						setting.statementEnd = end;
						setting.conditional = conditional;
					}
					break;
				}
			}
			// setSceneDimensions takes the scale map away.
			if (word == "setSceneDimensions") scale.map.clear ();
			// A new overlay goes after the overlays, else after the size of
			// the scene.
			if (! conditional && (word == "addOverlay" || word == "mixOverlay" || (word == "setSceneDimensions" && ! afterOverlay))) {
				room.hasOverlayAnchor = true;
				room.overlayAnchorStart = token.at;
				room.overlayAnchorEnd = end;
				if (word != "setSceneDimensions") afterOverlay = true;
			}
			// A new region goes after the regions, else after the setting up.
			if (setup && ! conditional && ! afterRegion) {
				room.hasAnchor = true;
				room.anchorStart = token.at;
				room.anchorEnd = end;
			}
		}
		room.scale = scale;
		// The standing picture that each character faces.
		for (size_t c = 0; c < room.characters.size () && c < costumes.size (); c ++) {
			std::string sub;
			room.characters[c].picture = costumeIn (t, costumes[c].from, costumes[c].to, &sub, open, room.characters[c].angle ());
		}
	}

	std::string fileOf (const TokenRange & r) const {
		if (r.to - r.from != 1 || t[r.from].kind != TOKEN_FILE) return std::string ();
		const std::string & q = t[r.from].text;
		return q.size () >= 2 ? q.substr (1, q.size () - 2) : std::string ();
	}

	// The flags of setCharacterExtra: numbers and constants added up, as
	// ICON + NOREMOVE.
	RoomArg flags (const TokenRange & r) const {
		RoomArg arg = argument (r);
		if (arg.literal || r.empty ()) return arg;
		int sum = 0;
		bool term = true;		// A number or a name comes next
		for (size_t k = r.from; k < r.to; k ++) {
			const std::string & w = t[k].text;
			int value;
			if (term && t[k].kind == TOKEN_NUMBER && allDigits (w)) sum += atoi (w.c_str ());
			else if (term && constantValue (w, &value)) sum += value;
			else if (! term && w == "+") {}
			else return arg;
			term = ! term;
		}
		if (term) return arg;		// Ends with +
		arg.literal = true;
		arg.value = sum;
		return arg;
	}

	RoomArg argument (const TokenRange & r) const {
		RoomArg arg;
		if (r.empty ()) return arg;
		arg.from = t[r.from].at;
		arg.to = t[r.to - 1].end;
		arg.text = text.textIn (arg.from, arg.to);
		size_t n = r.to - r.from;
		if (n == 1 && t[r.from].kind == TOKEN_NUMBER && allDigits (t[r.from].text)) {
			arg.literal = true;
			arg.value = atoi (t[r.from].text.c_str ());
		} else if (n == 2 && t[r.from].text == "-" && t[r.from + 1].kind == TOKEN_NUMBER && allDigits (t[r.from + 1].text)) {
			arg.literal = true;
			arg.value = - atoi (t[r.from + 1].text.c_str ());
		} else if (n == 1) {
			for (size_t c = 0; c < sizeof (roomConstants) / sizeof (roomConstants[0]); c ++) {
				if (t[r.from].text == roomConstants[c].name) {
					arg.literal = true;
					arg.value = roomConstants[c].value;
				}
			}
			int value;
			if (! arg.literal && constantValue (t[r.from].text, &value)) {
				arg.literal = true;
				arg.value = value;
			}
		}
		return arg;
	}

	// newStack ( ... ) from first to last: the arguments' tokens between.
	bool stackAt (size_t first, size_t last, size_t * open) const {
		if (last - first < 2 || t[first].text != "newStack" || t[first + 1].text != "(") return false;
		if (matchingToken (t, first + 1) != last) return false;
		*open = first + 1;
		return true;
	}

	bool readRegion (const std::vector<TokenRange> & args, RoomRegion & region) {
		if (args.size () != 8 && args.size () != 5) return false;
		if (args[0].empty ()) return false;
		region.objectType = text.textIn (t[args[0].from].at, t[args[0].to - 1].end);
		region.editable = true;
		if (args.size () == 8) {
			for (int c = 1; c <= 4; c ++) region.coordinates.push_back (argument (args[c]));
			region.standX = argument (args[5]);
			region.standY = argument (args[6]);
			region.direction = argument (args[7]);
		} else {
			region.shape = true;
			region.pointsArgument = argument (args[1]);
			region.standX = argument (args[2]);
			region.standY = argument (args[3]);
			region.direction = argument (args[4]);
			const TokenRange & r = args[1];
			size_t first = r.from, last = r.to - 1, open = 0;
			if (r.to - r.from >= 3 && t[first].text == "makeFastArray" && t[first + 1].text == "(" && matchingToken (t, first + 1) == last) {
				first += 2;
				last -= 1;
			}
			if (! r.empty () && stackAt (first, last, &open)) {
				region.pointsOpen = t[open].at;
				region.pointsClose = t[last].at;
				std::vector<TokenRange> points = splitArguments (t, open, last);
				for (size_t p = 0; p < points.size (); p ++) region.coordinates.push_back (argument (points[p]));
				if (region.coordinates.size () < 6 || region.coordinates.size () % 2) {
					region.editable = false;
					region.whyNot = "Not enough points";
				}
			} else {
				region.editable = false;
				region.whyNot = "Points from a variable";
			}
		}
		for (size_t c = 0; c < region.coordinates.size () && region.editable; c ++) {
			if (! region.coordinates[c].literal) {
				region.editable = false;
				region.whyNot = "Not a number";
			}
		}
		return true;
	}

	const TextBuffer & text;
	std::vector<FileToken> t;
};

std::string regionCall (const std::string & objectType, bool shape, const std::vector<int> & coordinates, int standX, int standY, int direction) {
	std::string points = shape ? "newStack (" + joined (coordinates) + ")" : joined (coordinates);
	return "addScreenRegion (" + objectType + ", " + points + ", " + numberText (standX) + ", " + numberText (standY) + ", " + directionText (direction) + ");";
}

} // namespace

std::vector<int> RoomRegion::values () const {
	std::vector<int> v;
	for (size_t c = 0; c < coordinates.size (); c ++) v.push_back (coordinates[c].value);
	return v;
}

std::vector<int> RoomRegion::outline () const {
	std::vector<int> v = values ();
	if (shape || v.size () != 4) return v;
	int corners[] = { v[0], v[1], v[2], v[1], v[2], v[3], v[0], v[3] };
	return std::vector<int> (corners, corners + 8);
}

bool RoomRegion::contains (float x, float y) const {
	std::vector<int> v = values ();
	if (! shape) {
		if (v.size () != 4) return false;
		// As pointInRegion (Engine/region.cpp): the edges are in.
		return x >= std::min (v[0], v[2]) && x <= std::max (v[0], v[2]) && y >= std::min (v[1], v[3]) && y <= std::max (v[1], v[3]);
	}
	return v.size () >= 6 && pointInShape (v, x, y);
}

std::vector<Room> readRooms (const TextBuffer & text) {
	Reader reader (text);
	return reader.readAll ();
}

bool costumeOfSub (const TextBuffer & text, const std::string & sub, CostumePicture * picture, int facing) {
	std::vector<FileToken> t = fileTokens (text);
	size_t from, to;
	size_t scope;
	if (! returnOf (t, sub, &from, &to, &scope)) return false;
	*picture = costumeIn (t, from, to, NULL, scope, facing);
	return true;
}

int facingDirection (int angle, int directions) {
	if (directions < 1) return 0;
	int direc = angle + 180 / directions + 180;
	direc %= 360;
	if (direc < 0) direc += 360;
	return direc * directions / 360;
}

bool constantValue (const std::string & name, int * value) {
	for (size_t c = 0; c < sizeof (compilerConstants) / sizeof (compilerConstants[0]); c ++) {
		if (name != compilerConstants[c].name) continue;
		// Some are other names of functions, not numbers.
		if (! allDigits (compilerConstants[c].value)) return false;
		*value = atoi (compilerConstants[c].value);
		return true;
	}
	return false;
}

const char * drawModeName (int mode) {
	return mode >= 0 && mode < ROOM_DRAW_MODES ? drawModeNames[mode] : "NORMAL";
}

std::string extrasText (int flags) {
	static const struct { int value; const char * name; } names[] = {
		{ ROOM_FRONT, "FRONT" }, { ROOM_FIXEDSIZE, "FIXEDSIZE" }, { ROOM_NOZBUFFER, "NOZBUFFER" },
		{ ROOM_FIXTOSCREEN, "FIXTOSCREEN" }, { ROOM_NOLIGHTMAP, "NOLIGHTMAP" },
		{ ROOM_NOREMOVE, "NOREMOVE" }, { ROOM_RECTANGULAR, "RECTANGULAR" },
	};
	std::string text;
	// The game adds the flags up, so ICON only goes with all of its own.
	if ((flags & ROOM_ICON) == ROOM_ICON) {
		text = "ICON";
		flags &= ~ROOM_ICON;
	}
	for (size_t n = 0; n < sizeof (names) / sizeof (names[0]); n ++) {
		if (! (flags & names[n].value)) continue;
		text += (text.empty () ? "" : " + ") + std::string (names[n].name);
		flags &= ~names[n].value;
	}
	if (flags) text += (text.empty () ? "" : " + ") + numberText (flags);
	return text.empty () ? std::string ("NORMAL") : text;
}

std::vector<std::string> costumeSubs (const std::vector<std::string> & files,
		std::function<bool (const std::string & file, TextBuffer * text)> textOf) {
	std::vector<std::string> subs;
	for (size_t f = 0; f < files.size (); f ++) {
		TextBuffer text;
		if (! textOf (files[f], &text)) continue;
		std::vector<FileToken> t = fileTokens (text);
		for (size_t i = 0; i < t.size (); i ++) {
			if (t[i].text == "objectType") {
				while (i < t.size () && t[i].text != "{") i ++;
				if (i < t.size ()) i = matchingToken (t, i);
				continue;
			}
			if (t[i].text != "sub") continue;
			size_t k = i + 1;
			while (k < t.size () && t[k].kind == TOKEN_KEYWORD) k ++;
			// No parameters.
			if (k + 2 >= t.size () || t[k + 1].text != "(" || t[k + 2].text != ")") continue;
			size_t from, to, scope;
			if (! returnOf (t, t[k].text, &from, &to, &scope) || to <= from) continue;
			// return c, where var c = costume (...).
			if (to - from == 1 && ! valueOf (t, scope, from, t[from].text, &from, &to)) continue;
			if (to <= from) continue;
			const std::string & first = t[from].text;
			if (first == "costume" || first == "anim") subs.push_back (t[k].text);
		}
	}
	std::sort (subs.begin (), subs.end ());
	subs.erase (std::unique (subs.begin (), subs.end ()), subs.end ());
	return subs;
}

std::string directionText (int direction) {
	for (size_t c = 0; c < 8; c ++) {
		if (roomConstants[c].value == direction) return roomConstants[c].name;
	}
	return numberText (direction);
}

bool isIdentifier (const std::string & name) {
	if (name.empty ()) return false;
	for (size_t i = 0; i < name.size (); i ++) {
		char c = name[i];
		bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
		if (! letter && ! (i > 0 && c >= '0' && c <= '9')) return false;
	}
	return kindOfName (name, NULL) == TOKEN_IDENTIFIER;
}

namespace roomEdit {

std::vector<TextChange> setRectangle (const RoomRegion & region, int x1, int y1, int x2, int y2) {
	std::vector<TextChange> changes;
	if (region.shape || region.coordinates.size () != 4) return changes;
	int v[] = { x1, y1, x2, y2 };
	for (int c = 0; c < 4; c ++) change (changes, region.coordinates[c], v[c]);
	return ordered (changes);
}

std::vector<TextChange> setPoints (const RoomRegion & region, const std::vector<int> & points) {
	std::vector<TextChange> changes;
	if (! region.shape || ! region.editable || points.size () < 6 || points.size () % 2) return changes;
	if (points.size () == region.coordinates.size ()) {
		for (size_t c = 0; c < points.size (); c ++) change (changes, region.coordinates[c], points[c]);
	} else {
		changes.push_back (TextChange (region.coordinates.front ().from, region.coordinates.back ().to, joined (points)));
	}
	return ordered (changes);
}

std::vector<TextChange> move (const RoomRegion & region, int dx, int dy) {
	if (! region.editable) return std::vector<TextChange> ();
	std::vector<int> v = region.values ();
	for (size_t c = 0; c < v.size (); c ++) v[c] += c % 2 ? dy : dx;
	if (region.shape) return setPoints (region, v);
	return setRectangle (region, v[0], v[1], v[2], v[3]);
}

std::vector<TextChange> setStandPoint (const RoomRegion & region, int x, int y) {
	std::vector<TextChange> changes;
	change (changes, region.standX, x);
	change (changes, region.standY, y);
	return ordered (changes);
}

std::vector<TextChange> setDirection (const RoomRegion & region, int direction) {
	std::vector<TextChange> changes;
	if (region.direction.literal && region.direction.value != direction) change (changes, region.direction, directionText (direction));
	return changes;
}

std::vector<TextChange> toShape (const RoomRegion & region) {
	std::vector<TextChange> changes;
	if (region.shape || ! region.editable || region.coordinates.size () != 4) return changes;
	changes.push_back (TextChange (region.coordinates[0].from, region.coordinates[3].to, "newStack (" + joined (region.outline ()) + ")"));
	return changes;
}

std::vector<TextChange> remove (const TextBuffer & text, const RoomRegion & region) {
	return removeStatement (text, region.call, region.statementEnd);
}

std::vector<TextChange> remove (const TextBuffer & text, const RoomOverlay & overlay) {
	return removeStatement (text, overlay.call, overlay.statementEnd);
}

std::vector<TextChange> removeStatement (const TextBuffer & text, TextPos call, TextPos statementEnd) {
	std::vector<TextChange> changes;
	TextPos from = call, to = statementEnd;
	const std::string & first = text.line (from.line);
	const std::string & last = text.line (to.line);
	if (onlySpace (first.substr (0, from.column)) && onlySpace (last.substr (to.column))) {
		// Its lines, with their ends.
		if (to.line + 1 < text.lineCount ()) {
			from = TextPos (from.line, 0);
			to = TextPos (to.line + 1, 0);
		} else if (from.line > 0) {
			from = TextPos (from.line - 1, (int) text.line (from.line - 1).size ());
			to = TextPos (to.line, (int) last.size ());
		} else {
			from = TextPos (from.line, 0);
			to = TextPos (to.line, (int) last.size ());
		}
	} else {
		// The spaces after it too.
		int c = to.column;
		while (c < (int) last.size () && (last[c] == ' ' || last[c] == '\t')) c ++;
		to.column = c;
	}
	changes.push_back (TextChange (from, to, std::string ()));
	return changes;
}

// Insert a line of code into a room: after the statement from start to end,
// on a line of its own indented as the line where it starts, or, without
// one, at the start of the body.
static void insertLine (const TextBuffer & text, const Room & room, bool hasAnchor, TextPos start, TextPos end,
		const std::string & indentUnit, const std::string & code, std::vector<TextChange> & changes, TextPos * where) {
	if (hasAnchor && end.line == room.close.line) {
		// The } is on the line: the new call goes on a line between.
		std::string indent = leadingSpace (text.line (room.open.line)) + indentUnit;
		changes.push_back (TextChange (end, end, "\n" + indent + code + "\n" + leadingSpace (text.line (room.close.line))));
		if (where) *where = TextPos (end.line + 1, (int) indent.size ());
	} else if (hasAnchor) {
		std::string indent = leadingSpace (text.line (start.line));
		TextPos at (end.line, (int) text.line (end.line).size ());
		changes.push_back (TextChange (at, at, "\n" + indent + code));
		if (where) *where = TextPos (at.line + 1, (int) indent.size ());
	} else {
		std::string braceIndent = leadingSpace (text.line (room.open.line));
		std::string indent = braceIndent + indentUnit;
		TextPos at (room.open.line, room.open.column + 1);
		// A sub with its } on the line of its {: that goes on a line after.
		std::string with = "\n" + indent + code;
		if (room.close.line == room.open.line) with += "\n" + braceIndent;
		changes.push_back (TextChange (at, at, with));
		if (where) *where = TextPos (at.line + 1, (int) indent.size ());
	}
}

std::vector<TextChange> add (const TextBuffer & text, const Room & room, const std::string & indentUnit,
		const std::string & objectType, bool newObjectType, bool shape, const std::vector<int> & coordinates,
		int standX, int standY, int direction, TextPos * where) {
	std::vector<TextChange> changes;
	if (newObjectType) {
		TextPos after (room.close.line, (int) text.line (room.close.line).size ());
		changes.push_back (TextChange (after, after, "\n\nobjectType " + objectType + " (\"" + objectType + "\") {\n}"));
	}
	std::string call = regionCall (objectType, shape, coordinates, standX, standY, direction);
	insertLine (text, room, room.hasAnchor, room.anchorStart, room.anchorEnd, indentUnit, call, changes, where);
	return changes;
}

std::vector<TextChange> moveOverlay (const RoomOverlay & overlay, int x, int y) {
	std::vector<TextChange> changes;
	change (changes, overlay.x, x);
	change (changes, overlay.y, y);
	return ordered (changes);
}

std::vector<TextChange> addOverlay (const TextBuffer & text, const Room & room, const std::string & indentUnit,
		const std::string & file, int x, int y, TextPos * where) {
	std::vector<TextChange> changes;
	std::string call = "addOverlay ('" + file + "', " + numberText (x) + ", " + numberText (y) + ");";
	insertLine (text, room, room.hasOverlayAnchor, room.overlayAnchorStart, room.overlayAnchorEnd, indentUnit, call, changes, where);
	return changes;
}

std::vector<TextChange> moveCharacter (const RoomCharacter & character, int x, int y) {
	std::vector<TextChange> changes;
	change (changes, character.x, x);
	change (changes, character.y, y);
	return ordered (changes);
}

std::vector<TextChange> remove (const TextBuffer & text, const RoomCharacter & character) {
	return removeStatement (text, character.call, character.statementEnd);
}

std::vector<TextChange> addCharacter (const TextBuffer & text, const Room & room, const std::string & indentUnit,
		const std::string & objectType, bool newObjectType, int x, int y, const std::string & costume, TextPos * where) {
	std::vector<TextChange> changes;
	if (newObjectType) {
		TextPos after (room.close.line, (int) text.line (room.close.line).size ());
		changes.push_back (TextChange (after, after, "\n\nobjectType " + objectType + " (\"" + objectType + "\") {\n}"));
	}
	std::string call = "addCharacter (" + objectType + ", " + numberText (x) + ", " + numberText (y) + ", " + costume + ");";
	if (room.hasCharacterAnchor) insertLine (text, room, true, room.characterAnchorStart, room.characterAnchorEnd, indentUnit, call, changes, where);
	else insertLine (text, room, room.hasAnchor, room.anchorStart, room.anchorEnd, indentUnit, call, changes, where);
	return changes;
}

// Change a setting of a character: the call that is there, or a new one
// after its last setting, else after its addCharacter; none for the
// default.
static std::vector<TextChange> setting (const TextBuffer & text, const Room & room, const RoomCharacter & character,
		const RoomSetting & which, const char * function, bool isDefault, const std::string & value) {
	std::vector<TextChange> changes;
	if (which.present) {
		if (! which.value.literal) return changes;
		if (isDefault) return removeStatement (text, which.call, which.statementEnd);
		if (which.value.text != value) changes.push_back (TextChange (which.value.from, which.value.to, value));
		return changes;
	}
	if (isDefault) return changes;
	// After the last setting of the character, else its addCharacter.
	TextPos end = character.statementEnd;
	const RoomSetting * settings[] = { &character.drawMode, &character.extra, &character.walkSpeed, &character.facing, &character.depth };
	for (size_t s = 0; s < 5; s ++) {
		if (settings[s]->present && end < settings[s]->statementEnd) end = settings[s]->statementEnd;
	}
	std::string call = std::string (function) + " (" + character.objectType + ", " + value + ");";
	insertLine (text, room, true, character.call, end, std::string (), call, changes, NULL);
	return changes;
}

std::vector<TextChange> setDrawMode (const TextBuffer & text, const Room & room, const RoomCharacter & character, int mode) {
	return setting (text, room, character, character.drawMode, "setCharacterDrawMode", mode == 0, drawModeName (mode));
}

std::vector<TextChange> setExtras (const TextBuffer & text, const Room & room, const RoomCharacter & character, int flags) {
	return setting (text, room, character, character.extra, "setCharacterExtra", flags == 0, extrasText (flags));
}

std::vector<TextChange> setWalkSpeed (const TextBuffer & text, const Room & room, const RoomCharacter & character, int speed) {
	return setting (text, room, character, character.walkSpeed, "setCharacterWalkSpeed", speed <= 0, numberText (speed));
}

std::vector<TextChange> setFacing (const TextBuffer & text, const Room & room, const RoomCharacter & character, int angle) {
	return setting (text, room, character, character.facing, "turnCharacter", angle < 0, directionText (angle));
}

std::vector<TextChange> setDepth (const TextBuffer & text, const Room & room, const RoomCharacter & character, bool fixed, int z) {
	return setting (text, room, character, character.depth, "setCharacterZ", ! fixed, numberText (z));
}

} // namespace roomEdit

} // namespace devkit
