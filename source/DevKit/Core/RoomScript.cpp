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
				if (! room.overlays.empty () || ! room.regions.empty () || ! room.floor.empty ()) rooms.push_back (room);
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
		bool floorTopLevel = false, afterRegion = false;
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
			} else if (word == "setZBuffer" || word == "setScale" || word == "setScaleMap") {
				setup = true;
			}
			// A new region goes after the regions, else after the setting up.
			if (setup && ! conditional && ! afterRegion) {
				room.hasAnchor = true;
				room.anchorStart = token.at;
				room.anchorEnd = end;
			}
		}
	}

	std::string fileOf (const TokenRange & r) const {
		if (r.to - r.from != 1 || t[r.from].kind != TOKEN_FILE) return std::string ();
		const std::string & q = t[r.from].text;
		return q.size () >= 2 ? q.substr (1, q.size () - 2) : std::string ();
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
	std::vector<TextChange> changes;
	TextPos from = region.call, to = region.statementEnd;
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

std::vector<TextChange> add (const TextBuffer & text, const Room & room, const std::string & indentUnit,
		const std::string & objectType, bool newObjectType, bool shape, const std::vector<int> & coordinates,
		int standX, int standY, int direction, TextPos * where) {
	std::vector<TextChange> changes;
	if (newObjectType) {
		TextPos after (room.close.line, (int) text.line (room.close.line).size ());
		changes.push_back (TextChange (after, after, "\n\nobjectType " + objectType + " (\"" + objectType + "\") {\n}"));
	}
	std::string call = regionCall (objectType, shape, coordinates, standX, standY, direction);
	if (room.hasAnchor && room.anchorEnd.line == room.close.line) {
		// The } is on the line: the new call goes on a line between.
		std::string indent = leadingSpace (text.line (room.open.line)) + indentUnit;
		changes.push_back (TextChange (room.anchorEnd, room.anchorEnd, "\n" + indent + call + "\n" + leadingSpace (text.line (room.close.line))));
		if (where) *where = TextPos (room.anchorEnd.line + 1, (int) indent.size ());
	} else if (room.hasAnchor) {
		std::string indent = leadingSpace (text.line (room.anchorStart.line));
		TextPos at (room.anchorEnd.line, (int) text.line (room.anchorEnd.line).size ());
		changes.push_back (TextChange (at, at, "\n" + indent + call));
		if (where) *where = TextPos (at.line + 1, (int) indent.size ());
	} else {
		std::string braceIndent = leadingSpace (text.line (room.open.line));
		std::string indent = braceIndent + indentUnit;
		TextPos at (room.open.line, room.open.column + 1);
		// A sub with its } on the line of its {: that goes on a line after.
		std::string with = "\n" + indent + call;
		if (room.close.line == room.open.line) with += "\n" + braceIndent;
		changes.push_back (TextChange (at, at, with));
		if (where) *where = TextPos (at.line + 1, (int) indent.size ());
	}
	return changes;
}

} // namespace roomEdit

} // namespace devkit
