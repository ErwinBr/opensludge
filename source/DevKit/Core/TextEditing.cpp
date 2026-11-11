#include <string.h>

#include "ScriptLexer.h"
#include "TextEditing.h"

namespace devkit {

namespace editing {

static const int TAB_SIZE = 4;

static char closerOf (char c) {
	switch (c) {
		case '(': return ')';
		case '[': return ']';
		case '{': return '}';
		case '"': return '"';
		case '\'': return '\'';
		default: return 0;
	}
}

static bool isBlank (const std::string & text) {
	return text.find_first_not_of (" \t") == std::string::npos;
}

std::string indentOf (const std::string & line) {
	size_t n = line.find_first_not_of (" \t");
	return n == std::string::npos ? line : line.substr (0, n);
}

static char charAt (const TextBuffer & text, TextPos at) {
	const std::string & line = text.line (at.line);
	return at.column < (int) line.size () ? line[at.column] : 0;
}

static char charBefore (const TextBuffer & text, TextPos at) {
	const std::string & line = text.line (at.line);
	return at.column > 0 && at.column <= (int) line.size () ? line[at.column - 1] : 0;
}

// Whether a place of a line is in a string, a file name or a comment.
static bool inStringOrComment (const std::string & line, int column) {
	std::vector<Token> tokens = lexLine (line, NULL);
	for (size_t t = 0; t < tokens.size (); t ++) {
		const Token & k = tokens[t];
		if (k.start >= column) break;
		switch (k.kind) {
			case TOKEN_COMMENT:
			case TOKEN_ERROR:
				return true;
			case TOKEN_STRING:
			case TOKEN_ESCAPE:
			case TOKEN_FILE:
				if (column < k.end) return true;
				break;
			default:
				break;
		}
	}
	return false;
}

// One indent less: a tab, or as many spaces as an indent has, from the end.
static std::string outdented (const std::string & indent, const std::string & unit) {
	if (indent.empty ()) return indent;
	if (indent[indent.size () - 1] == '\t') return indent.substr (0, indent.size () - 1);
	size_t spaces = unit == "\t" ? TAB_SIZE : unit.size ();
	size_t keep = indent.size ();
	while (keep > 0 && spaces > 0 && indent[keep - 1] == ' ') {
		keep --;
		spaces --;
	}
	return indent.substr (0, keep);
}

// The { that a } at a place would close, on an earlier line or this one.
static bool findOpening (const TextBuffer & text, TextPos at, TextPos * opening) {
	int depth = 0;
	for (int l = at.line; l >= 0 && at.line - l < 5000; l --) {
		const std::string & line = text.line (l);
		std::vector<Token> tokens = lexLine (line, NULL);
		for (size_t t = tokens.size (); t -- > 0; ) {
			const Token & k = tokens[t];
			if (k.kind != TOKEN_BRACKET || (l == at.line && k.start >= at.column)) continue;
			char c = line[k.start];
			if (c == '}') depth ++;
			else if (c == '{') {
				if (depth == 0) {
					*opening = TextPos (l, k.start);
					return true;
				}
				depth --;
			}
		}
	}
	return false;
}

void insert (ScriptDocument & script, const std::string & text, const std::string & label) {
	TextSelection s = script.selection ();
	script.replace (s.start (), s.end (), text, EDIT_OTHER, label);
}

void type (ScriptDocument & script, const std::string & characters) {
	if (characters.empty ()) return;
	const TextBuffer & text = script.buffer ();
	TextSelection s = script.selection ();
	TextPos start = s.start (), end = s.end ();
	if (characters.size () != 1) {
		script.replace (start, end, characters, EDIT_TYPING, "Typing");
		return;
	}
	char c = characters[0];
	char closer = closerOf (c);
	const std::string & line = text.line (start.line);
	bool quote = c == '"' || c == '\'';

	// Brackets and quotes around a selection.
	if (closer && ! s.empty ()) {
		std::string inside = text.textIn (start, end);
		TextPos anchor (start.line, start.column + 1);
		TextSelection after (anchor, TextBuffer::endOf (anchor, inside));
		script.replace (start, end, std::string (1, c) + inside + closer, EDIT_OTHER, "Typing", after);
		return;
	}
	if (s.empty ()) {
		// Over the closing bracket or quote that is next.
		if ((c == ')' || c == ']' || c == '}' || quote) && charAt (text, start) == c
				&& (! quote || inStringOrComment (line, start.column))) {
			script.setSelection (TextSelection (TextPos (start.line, start.column + 1)));
			return;
		}
		// The partner of an opening bracket or a quote, when what follows is
		// the end of the line, a space or a closing bracket.
		char next = charAt (text, start);
		bool roomAfter = next == 0 || next == ' ' || next == '\t' || strchr (")]};,", next);
		if (closer && roomAfter && ! inStringOrComment (line, start.column)) {
			char previous = charBefore (text, start);
			bool afterWord = previous && charClassOf ((unsigned char) previous) == CHAR_WORD;
			if (! quote || ! afterWord) {
				TextSelection after (TextPos (start.line, start.column + 1));
				script.replace (start, end, std::string (1, c) + closer, EDIT_OTHER, "Typing", after);
				return;
			}
		}
		// A } that starts a line takes the indent of its {.
		if (c == '}' && isBlank (line.substr (0, start.column)) && start.column > 0) {
			TextPos opening;
			std::string indent = findOpening (text, start, &opening) ? indentOf (text.line (opening.line))
				: outdented (line.substr (0, start.column), script.indentUnit ());
			script.replace (TextPos (start.line, 0), start, indent + "}", EDIT_OTHER, "Typing");
			return;
		}
	}
	script.replace (start, end, characters, EDIT_TYPING, "Typing");
}

void newLine (ScriptDocument & script) {
	const TextBuffer & text = script.buffer ();
	TextSelection s = script.selection ();
	TextPos start = s.start (), end = s.end ();
	const std::string & line = text.line (start.line);
	std::string before = line.substr (0, start.column);
	std::string indent = indentOf (before);
	std::string unit = script.indentUnit ();
	// The spaces after the cursor go, as the new line has its own indent.
	const std::string & endLine = text.line (end.line);
	while (end.column < (int) endLine.size () && (endLine[end.column] == ' ' || endLine[end.column] == '\t')) end.column ++;
	std::string trimmed = before.substr (0, before.find_last_not_of (" \t") == std::string::npos ? 0 : before.find_last_not_of (" \t") + 1);
	bool opens = ! trimmed.empty () && trimmed[trimmed.size () - 1] == '{' && ! inStringOrComment (line, (int) trimmed.size () - 1);
	bool closesNext = end.column < (int) endLine.size () && endLine[end.column] == '}';
	std::string inserted = "\n" + indent;
	if (opens) inserted += unit;
	TextPos cursor = TextBuffer::endOf (start, inserted);
	if (opens && closesNext) inserted += "\n" + indent;
	script.replace (start, end, inserted, EDIT_OTHER, "Typing", TextSelection (cursor));
}

void backspace (ScriptDocument & script, bool word) {
	const TextBuffer & text = script.buffer ();
	TextSelection s = script.selection ();
	if (! s.empty ()) {
		deleteSelection (script, "Delete");
		return;
	}
	TextPos at = s.cursor;
	if (at == text.begin ()) return;
	TextPos from = word ? text.wordLeft (at) : text.previous (at);
	TextPos to = at;
	const std::string & line = text.line (at.line);
	if (! word && at.column > 0) {
		char previous = line[at.column - 1];
		char closer = closerOf (previous);
		std::string before = line.substr (0, at.column);
		std::string unit = script.indentUnit ();
		if (closer && charAt (text, at) == closer) {
			// An empty pair goes together.
			to = TextPos (at.line, at.column + 1);
		} else if (unit != "\t" && before.find_first_not_of (' ') == std::string::npos) {
			// In an indent of spaces, back to the indent before.
			int n = at.column % (int) unit.size ();
			from = TextPos (at.line, at.column - (n ? n : (int) unit.size ()));
		}
	}
	script.replace (from, to, std::string (), EDIT_BACKSPACE, "Delete");
}

void deleteForward (ScriptDocument & script, bool word) {
	const TextBuffer & text = script.buffer ();
	TextSelection s = script.selection ();
	if (! s.empty ()) {
		deleteSelection (script, "Delete");
		return;
	}
	TextPos at = s.cursor;
	if (at == text.end ()) return;
	TextPos to = word ? text.wordRight (at) : text.next (at);
	script.replace (at, to, std::string (), EDIT_DELETE, "Delete", TextSelection (at));
}

std::string selectedText (const ScriptDocument & script) {
	const TextSelection & s = script.selection ();
	return script.buffer ().textIn (s.start (), s.end ());
}

void deleteSelection (ScriptDocument & script, const std::string & label) {
	TextSelection s = script.selection ();
	if (s.empty ()) return;
	script.replace (s.start (), s.end (), std::string (), EDIT_OTHER, label);
}

void selectedLines (const ScriptDocument & script, int * first, int * last) {
	const TextSelection & s = script.selection ();
	*first = s.start ().line;
	*last = s.end ().line;
	if (s.end ().column == 0 && *last > *first) (*last) --;
}

// A change of the start of some lines: for each, how many bytes at its
// start go and what comes in their place; and the selection after it.
static void changeLineStarts (ScriptDocument & script, int first, int last,
		const std::vector<int> & removed, const std::vector<std::string> & added, const std::string & label) {
	std::vector<TextChange> changes;
	for (int l = first; l <= last; l ++) {
		int i = l - first;
		if (! removed[i] && added[i].empty ()) continue;
		changes.push_back (TextChange (TextPos (l, 0), TextPos (l, removed[i]), added[i]));
	}
	// Each place of the selection moves with the start of its line.
	TextSelection s = script.selection ();
	TextPos * places[2] = { &s.anchor, &s.cursor };
	for (int p = 0; p < 2; p ++) {
		TextPos & at = *places[p];
		if (at.line < first || at.line > last) continue;
		int i = at.line - first;
		if (at.column == 0 && ! (s.empty ())) continue;
		int column = at.column <= removed[i] ? 0 : at.column - removed[i];
		at.column = column + (int) added[i].size ();
		if (at.column < 0) at.column = 0;
	}
	script.edit (changes, s, EDIT_OTHER, label);
}

void indentLines (ScriptDocument & script, bool more) {
	int first, last;
	selectedLines (script, &first, &last);
	const TextBuffer & text = script.buffer ();
	std::string unit = script.indentUnit ();
	std::vector<int> removed;
	std::vector<std::string> added;
	for (int l = first; l <= last; l ++) {
		const std::string & line = text.line (l);
		if (more) {
			removed.push_back (0);
			// Empty lines stay empty.
			added.push_back (line.empty () ? std::string () : unit);
		} else {
			int n = 0;
			if (! line.empty () && line[0] == '\t') {
				n = 1;
			} else {
				int spaces = unit == "\t" ? TAB_SIZE : (int) unit.size ();
				while (n < spaces && n < (int) line.size () && line[n] == ' ') n ++;
			}
			removed.push_back (n);
			added.push_back (std::string ());
		}
	}
	changeLineStarts (script, first, last, removed, added, more ? "Indent" : "Outdent");
}

void tab (ScriptDocument & script) {
	TextSelection s = script.selection ();
	if (s.start ().line != s.end ().line) {
		indentLines (script, true);
		return;
	}
	std::string unit = script.indentUnit ();
	if (unit != "\t") {
		// Spaces to the next stop.
		int column = displayColumn (script.buffer ().line (s.start ().line), s.start ().column, TAB_SIZE);
		unit = std::string (unit.size () - column % unit.size (), ' ');
	}
	script.replace (s.start (), s.end (), unit, EDIT_TYPING, "Typing");
}

void toggleComment (ScriptDocument & script) {
	int first, last;
	selectedLines (script, &first, &last);
	const TextBuffer & text = script.buffer ();
	bool all = true, any = false;
	size_t indent = std::string::npos;
	for (int l = first; l <= last; l ++) {
		const std::string & line = text.line (l);
		size_t n = line.find_first_not_of (" \t");
		if (n == std::string::npos) continue;
		any = true;
		if (line[n] != '#') all = false;
		if (n < indent) indent = n;
	}
	if (! any) return;
	std::vector<TextChange> changes;
	TextSelection s = script.selection ();
	for (int l = first; l <= last; l ++) {
		const std::string & line = text.line (l);
		size_t n = line.find_first_not_of (" \t");
		if (n == std::string::npos) continue;
		int column, delta;
		if (all) {
			int count = n + 1 < line.size () && line[n + 1] == ' ' ? 2 : 1;
			changes.push_back (TextChange (TextPos (l, (int) n), TextPos (l, (int) n + count), std::string ()));
			column = (int) n;
			delta = - count;
		} else {
			changes.push_back (TextChange (TextPos (l, (int) indent), TextPos (l, (int) indent), "# "));
			column = (int) indent;
			delta = 2;
		}
		TextPos * places[2] = { &s.anchor, &s.cursor };
		for (int p = 0; p < 2; p ++) {
			if (places[p]->line != l || places[p]->column < column) continue;
			places[p]->column += delta;
			if (places[p]->column < column) places[p]->column = column;
		}
	}
	script.edit (changes, s, EDIT_OTHER, all ? "Uncomment" : "Comment");
}

static std::string linesText (const TextBuffer & text, int first, int last) {
	return text.textIn (TextPos (first, 0), TextPos (last, (int) text.line (last).size ()));
}

static TextSelection shifted (TextSelection s, int lines) {
	s.anchor.line += lines;
	s.cursor.line += lines;
	return s;
}

void moveLines (ScriptDocument & script, int direction) {
	int first, last;
	selectedLines (script, &first, &last);
	const TextBuffer & text = script.buffer ();
	if (direction < 0 && first == 0) return;
	if (direction > 0 && last + 1 >= text.lineCount ()) return;
	std::string block = linesText (text, first, last);
	TextSelection after = shifted (script.selection (), direction < 0 ? -1 : 1);
	if (direction < 0) {
		std::string above = text.line (first - 1);
		script.replace (TextPos (first - 1, 0), TextPos (last, (int) text.line (last).size ()), block + "\n" + above, EDIT_OTHER, "Move Lines", after);
	} else {
		std::string below = text.line (last + 1);
		script.replace (TextPos (first, 0), TextPos (last + 1, (int) text.line (last + 1).size ()), below + "\n" + block, EDIT_OTHER, "Move Lines", after);
	}
}

void duplicateLines (ScriptDocument & script, int direction) {
	int first, last;
	selectedLines (script, &first, &last);
	const TextBuffer & text = script.buffer ();
	std::string block = linesText (text, first, last);
	if (direction > 0) {
		TextSelection after = shifted (script.selection (), last - first + 1);
		script.replace (TextPos (last, (int) text.line (last).size ()), TextPos (last, (int) text.line (last).size ()),
			"\n" + block, EDIT_OTHER, "Duplicate Lines", after);
	} else {
		TextSelection after = script.selection ();
		script.replace (TextPos (first, 0), TextPos (first, 0), block + "\n", EDIT_OTHER, "Duplicate Lines", after);
	}
}

void deleteLines (ScriptDocument & script) {
	int first, last;
	selectedLines (script, &first, &last);
	const TextBuffer & text = script.buffer ();
	int column = script.selection ().cursor.column;
	TextPos from, to;
	if (last + 1 < text.lineCount ()) {
		from = TextPos (first, 0);
		to = TextPos (last + 1, 0);
	} else if (first > 0) {
		from = TextPos (first - 1, (int) text.line (first - 1).size ());
		to = TextPos (last, (int) text.line (last).size ());
		first --;
	} else {
		from = TextPos (first, 0);
		to = TextPos (last, (int) text.line (last).size ());
	}
	script.replace (from, to, std::string (), EDIT_OTHER, "Delete Lines", TextSelection (TextPos (first, column)));
}

TextPos matchingBracket (const TextBuffer & text, TextPos at) {
	if (at.line < 0 || at.line >= text.lineCount ()) return at;
	const std::string & line = text.line (at.line);
	std::vector<Token> tokens = lexLine (line, NULL);
	int t = tokenAt (tokens, at.column);
	if (t < 0 || tokens[t].kind != TOKEN_BRACKET || tokens[t].start != at.column) return at;
	char c = line[at.column];
	const char * opens = "([{", * closes = ")]}";
	const char * o = strchr (opens, c), * k = strchr (closes, c);
	bool forward = o != NULL;
	char open = forward ? c : opens[k - closes];
	char close = forward ? closes[o - opens] : c;
	int depth = 0;
	for (int l = at.line; l >= 0 && l < text.lineCount () && abs (l - at.line) < 5000; l += forward ? 1 : -1) {
		const std::string & here = text.line (l);
		std::vector<Token> lineTokens = l == at.line ? tokens : lexLine (here, NULL);
		int count = (int) lineTokens.size ();
		for (int i = forward ? 0 : count - 1; forward ? i < count : i >= 0; i += forward ? 1 : -1) {
			const Token & k = lineTokens[i];
			if (k.kind != TOKEN_BRACKET) continue;
			if (l == at.line && (forward ? k.start <= at.column : k.start >= at.column)) continue;
			char b = here[k.start];
			if (b == (forward ? open : close)) depth ++;
			else if (b == (forward ? close : open)) {
				if (depth == 0) return TextPos (l, k.start);
				depth --;
			}
		}
	}
	return at;
}

static char lowerASCII (char c) {
	return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
}

static bool matchesAt (const std::string & line, size_t at, const std::string & wanted, const FindOptions & options) {
	if (at + wanted.size () > line.size ()) return false;
	for (size_t i = 0; i < wanted.size (); i ++) {
		char a = line[at + i], b = wanted[i];
		if (options.matchCase ? a != b : lowerASCII (a) != lowerASCII (b)) return false;
	}
	if (options.wholeWord) {
		if (at > 0 && charClassOf ((unsigned char) line[at - 1]) == CHAR_WORD && charClassOf ((unsigned char) wanted[0]) == CHAR_WORD) return false;
		size_t after = at + wanted.size ();
		if (after < line.size () && charClassOf ((unsigned char) line[after]) == CHAR_WORD
				&& charClassOf ((unsigned char) wanted[wanted.size () - 1]) == CHAR_WORD) return false;
	}
	return true;
}

std::vector<TextSelection> findAll (const TextBuffer & text, const std::string & wanted, const FindOptions & options) {
	std::vector<TextSelection> found;
	if (wanted.empty () || wanted.find ('\n') != std::string::npos) return found;
	for (int l = 0; l < text.lineCount (); l ++) {
		const std::string & line = text.line (l);
		for (size_t at = 0; at + wanted.size () <= line.size (); ) {
			if (matchesAt (line, at, wanted, options)) {
				found.push_back (TextSelection (TextPos (l, (int) at), TextPos (l, (int) (at + wanted.size ()))));
				at += wanted.size ();
			} else {
				at ++;
			}
		}
	}
	return found;
}

TextSelection findNext (const TextBuffer & text, TextPos from, const std::string & wanted, const FindOptions & options, bool backwards) {
	std::vector<TextSelection> found = findAll (text, wanted, options);
	if (found.empty ()) return TextSelection ();
	if (backwards) {
		for (size_t i = found.size (); i -- > 0; ) {
			if (found[i].end () <= from && found[i].start () < from) return found[i];
		}
		return found.back ();
	}
	for (size_t i = 0; i < found.size (); i ++) {
		if (found[i].start () >= from) return found[i];
	}
	return found[0];
}

bool replaceNext (ScriptDocument & script, const std::string & wanted, const std::string & with, const FindOptions & options) {
	const TextBuffer & text = script.buffer ();
	TextSelection s = script.selection ();
	bool replaced = false;
	TextPos from = s.start ();
	if (! s.empty () && s.start ().line == s.end ().line
			&& s.end ().column - s.start ().column == (int) wanted.size ()
			&& matchesAt (text.line (s.start ().line), s.start ().column, wanted, options)) {
		script.replace (s.start (), s.end (), with, EDIT_OTHER, "Replace");
		from = script.selection ().cursor;
		replaced = true;
	}
	TextSelection next = findNext (script.buffer (), from, wanted, options, false);
	if (! next.empty ()) script.setSelection (next);
	return replaced;
}

int replaceAll (ScriptDocument & script, const std::string & wanted, const std::string & with, const FindOptions & options) {
	std::vector<TextSelection> found = findAll (script.buffer (), wanted, options);
	if (found.empty ()) return 0;
	// From the end, so that the places of the others stay.
	std::vector<TextChange> changes;
	for (size_t i = found.size (); i -- > 0; ) changes.push_back (TextChange (found[i].start (), found[i].end (), with));
	TextSelection after (script.selection ().cursor);
	script.edit (changes, after, EDIT_OTHER, "Replace All");
	return (int) found.size ();
}

} // namespace editing

} // namespace devkit
