#include "TextBuffer.h"

namespace devkit {

static const char BOM[] = "\xEF\xBB\xBF";

static bool isContinuation (unsigned char c) {
	return (c & 0xC0) == 0x80;
}

CharClass charClassOf (unsigned char c) {
	if (c == ' ' || c == '\t') return CHAR_SPACE;
	// Letters, digits, _ and every byte of a character beyond ASCII.
	if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c >= 0x80) return CHAR_WORD;
	return CHAR_PUNCTUATION;
}

int previousCharStart (const std::string & line, int column) {
	if (column > (int) line.size ()) column = (int) line.size ();
	if (column <= 0) return 0;
	column --;
	while (column > 0 && isContinuation ((unsigned char) line[column])) column --;
	return column;
}

int nextCharStart (const std::string & line, int column) {
	if (column < 0) return 0;
	if (column >= (int) line.size ()) return (int) line.size ();
	column ++;
	while (column < (int) line.size () && isContinuation ((unsigned char) line[column])) column ++;
	return column;
}

int displayColumn (const std::string & line, int byte, int tabSize) {
	int column = 0;
	for (int i = 0; i < byte && i < (int) line.size (); i ++) {
		unsigned char c = (unsigned char) line[i];
		if (c == '\t') column = (column / tabSize + 1) * tabSize;
		else if (! isContinuation (c)) column ++;
	}
	return column;
}

int byteAtDisplayColumn (const std::string & line, int wanted, int tabSize) {
	int column = 0, i = 0;
	while (i < (int) line.size ()) {
		int next = nextCharStart (line, i);
		int width = line[i] == '\t' ? (column / tabSize + 1) * tabSize - column : 1;
		// The nearer side of the character.
		if (column + width > wanted) return wanted - column > width / 2 ? next : i;
		column += width;
		i = next;
	}
	return (int) line.size ();
}

TextBuffer::TextBuffer () : crlf (false), bom (false), changes (0) {
	lines.push_back (std::string ());
	stamps.push_back (newStamp ());
	crlfEnds.push_back (0);
}

uint64_t TextBuffer::newStamp () {
	static uint64_t next = 0;
	return ++ next;
}

void TextBuffer::setText (const std::string & file) {
	size_t start = 0;
	bom = file.compare (0, 3, BOM) == 0;
	if (bom) start = 3;
	lines.clear ();
	stamps.clear ();
	crlfEnds.clear ();
	int crlfCount = 0, lfCount = 0;
	while (true) {
		size_t end = file.find ('\n', start);
		if (end == std::string::npos) {
			lines.push_back (file.substr (start));
			crlfEnds.push_back (0);
			break;
		}
		size_t lineEnd = end;
		bool isCRLF = lineEnd > start && file[lineEnd - 1] == '\r';
		if (isCRLF) {
			lineEnd --;
			crlfCount ++;
		} else {
			lfCount ++;
		}
		lines.push_back (file.substr (start, lineEnd - start));
		crlfEnds.push_back (isCRLF ? 1 : 0);
		start = end + 1;
	}
	crlf = crlfCount > lfCount;
	for (size_t i = 0; i < lines.size (); i ++) stamps.push_back (newStamp ());
	changes ++;
}

std::string TextBuffer::fileText () const {
	std::string file;
	if (bom) file += BOM;
	for (size_t i = 0; i < lines.size (); i ++) {
		file += lines[i];
		if (i + 1 < lines.size ()) file += crlfEnds[i] ? "\r\n" : "\n";
	}
	return file;
}

bool TextBuffer::mixedLineEnds () const {
	for (size_t i = 1; i + 1 < crlfEnds.size (); i ++) {
		if (crlfEnds[i] != crlfEnds[0]) return true;
	}
	return false;
}

void TextBuffer::copyLineEnds (const TextBuffer & other) {
	if (other.lineCount () == lineCount ()) crlfEnds = other.crlfEnds;
}

std::string TextBuffer::text () const {
	return textIn (begin (), end ());
}

TextPos TextBuffer::clamp (TextPos at) const {
	if (at.line < 0) return begin ();
	if (at.line >= lineCount ()) return end ();
	const std::string & l = lines[at.line];
	if (at.column < 0) at.column = 0;
	if (at.column > (int) l.size ()) at.column = (int) l.size ();
	while (at.column > 0 && at.column < (int) l.size () && isContinuation ((unsigned char) l[at.column])) at.column --;
	return at;
}

std::string TextBuffer::textIn (TextPos from, TextPos to) const {
	from = clamp (from);
	to = clamp (to);
	if (to < from) {
		TextPos t = from;
		from = to;
		to = t;
	}
	if (from.line == to.line) return lines[from.line].substr (from.column, to.column - from.column);
	std::string text = lines[from.line].substr (from.column);
	for (int l = from.line + 1; l < to.line; l ++) {
		text += '\n';
		text += lines[l];
	}
	text += '\n';
	text += lines[to.line].substr (0, to.column);
	return text;
}

TextPos TextBuffer::endOf (TextPos at, const std::string & text) {
	for (size_t i = 0; i < text.size (); i ++) {
		if (text[i] == '\n') {
			at.line ++;
			at.column = 0;
		} else if (text[i] != '\r') {
			at.column ++;
		}
	}
	return at;
}

TextPos TextBuffer::insert (TextPos at, const std::string & text) {
	at = clamp (at);
	if (text.empty ()) return at;
	// The lines of the text.
	std::vector<std::string> parts;
	std::string part;
	for (size_t i = 0; i < text.size (); i ++) {
		if (text[i] == '\n') {
			parts.push_back (part);
			part.clear ();
		} else if (text[i] != '\r') {
			part += text[i];
		}
	}
	parts.push_back (part);

	std::string & first = lines[at.line];
	std::string after = first.substr (at.column);
	first.erase (at.column);
	first += parts[0];
	stamps[at.line] = newStamp ();
	TextPos endAt (at.line, (int) first.size ());
	if (parts.size () > 1) {
		std::vector<std::string> added (parts.begin () + 1, parts.end ());
		std::vector<uint64_t> addedStamps;
		for (size_t i = 0; i < added.size (); i ++) addedStamps.push_back (newStamp ());
		// The end of the line goes to the last new line; the others end as
		// most lines do.
		std::vector<unsigned char> addedEnds (added.size (), crlf ? 1 : 0);
		addedEnds.back () = crlfEnds[at.line];
		crlfEnds[at.line] = crlf ? 1 : 0;
		lines.insert (lines.begin () + at.line + 1, added.begin (), added.end ());
		stamps.insert (stamps.begin () + at.line + 1, addedStamps.begin (), addedStamps.end ());
		crlfEnds.insert (crlfEnds.begin () + at.line + 1, addedEnds.begin (), addedEnds.end ());
		endAt = TextPos (at.line + (int) added.size (), (int) added.back ().size ());
	}
	lines[endAt.line] += after;
	changes ++;
	return endAt;
}

void TextBuffer::erase (TextPos from, TextPos to) {
	from = clamp (from);
	to = clamp (to);
	if (to < from) {
		TextPos t = from;
		from = to;
		to = t;
	}
	if (from == to) return;
	std::string rest = lines[to.line].substr (to.column);
	lines[from.line].erase (from.column);
	lines[from.line] += rest;
	stamps[from.line] = newStamp ();
	if (to.line > from.line) {
		// The line ends as the last one did.
		crlfEnds[from.line] = crlfEnds[to.line];
		lines.erase (lines.begin () + from.line + 1, lines.begin () + to.line + 1);
		stamps.erase (stamps.begin () + from.line + 1, stamps.begin () + to.line + 1);
		crlfEnds.erase (crlfEnds.begin () + from.line + 1, crlfEnds.begin () + to.line + 1);
	}
	changes ++;
}

TextPos TextBuffer::next (TextPos at) const {
	at = clamp (at);
	if (at.column < (int) lines[at.line].size ()) return TextPos (at.line, nextCharStart (lines[at.line], at.column));
	if (at.line + 1 < lineCount ()) return TextPos (at.line + 1, 0);
	return at;
}

TextPos TextBuffer::previous (TextPos at) const {
	at = clamp (at);
	if (at.column > 0) return TextPos (at.line, previousCharStart (lines[at.line], at.column));
	if (at.line > 0) return TextPos (at.line - 1, (int) lines[at.line - 1].size ());
	return at;
}

TextPos TextBuffer::wordRight (TextPos at) const {
	at = clamp (at);
	const std::string & l = lines[at.line];
	if (at.column >= (int) l.size ()) return next (at);
	int c = at.column;
	// Over spaces, then over a run of one class.
	while (c < (int) l.size () && charClassOf ((unsigned char) l[c]) == CHAR_SPACE) c ++;
	if (c < (int) l.size ()) {
		CharClass kind = charClassOf ((unsigned char) l[c]);
		while (c < (int) l.size () && charClassOf ((unsigned char) l[c]) == kind) c ++;
	}
	return TextPos (at.line, c);
}

TextPos TextBuffer::wordLeft (TextPos at) const {
	at = clamp (at);
	if (at.column == 0) return previous (at);
	const std::string & l = lines[at.line];
	int c = at.column;
	while (c > 0 && charClassOf ((unsigned char) l[c - 1]) == CHAR_SPACE) c --;
	if (c > 0) {
		CharClass kind = charClassOf ((unsigned char) l[c - 1]);
		while (c > 0 && charClassOf ((unsigned char) l[c - 1]) == kind) c --;
	}
	return TextPos (at.line, c);
}

TextSelection TextBuffer::wordAt (TextPos at) const {
	at = clamp (at);
	const std::string & l = lines[at.line];
	if (l.empty ()) return TextSelection (at);
	// At the end of a word, the word before.
	int probe = at.column;
	if (probe >= (int) l.size () || (probe > 0 && charClassOf ((unsigned char) l[probe]) != CHAR_WORD
			&& charClassOf ((unsigned char) l[probe - 1]) == CHAR_WORD)) probe --;
	CharClass kind = charClassOf ((unsigned char) l[probe]);
	int start = probe, end = probe;
	while (start > 0 && charClassOf ((unsigned char) l[start - 1]) == kind) start --;
	while (end < (int) l.size () && charClassOf ((unsigned char) l[end]) == kind) end ++;
	return TextSelection (TextPos (at.line, start), TextPos (at.line, end));
}

} // namespace devkit
