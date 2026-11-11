#ifndef SLUDGE_DEVKIT_TEXT_BUFFER_H
#define SLUDGE_DEVKIT_TEXT_BUFFER_H

/*
 * The text of a script: lines of UTF-8, without their line ends. A place
 * in it is a line and a byte in that line. A file whose last line has an
 * end has an empty line after it, as in most editors. We keep how each line
 * ended (\n or \r\n, which old projects mix) and whether the file started
 * with a byte order mark, so that a file that we don't change saves as it
 * was, byte for byte. New lines end as most lines of the file do.
 *
 * Each line has a stamp that changes when the line does, so that a view
 * can keep what it worked out for a line, such as its colours, until then.
 */

#include <stdint.h>

#include <string>
#include <vector>

namespace devkit {

struct TextPos {
	int line, column;		// column is a byte of the line
	TextPos () : line (0), column (0) {}
	TextPos (int l, int c) : line (l), column (c) {}
	bool operator == (const TextPos & o) const { return line == o.line && column == o.column; }
	bool operator != (const TextPos & o) const { return ! (*this == o); }
	bool operator < (const TextPos & o) const { return line < o.line || (line == o.line && column < o.column); }
	bool operator <= (const TextPos & o) const { return ! (o < *this); }
	bool operator > (const TextPos & o) const { return o < *this; }
	bool operator >= (const TextPos & o) const { return ! (*this < o); }
};

// The selection: from the anchor, where it started, to the cursor. When
// they are the same, nothing is selected.
struct TextSelection {
	TextPos anchor, cursor;
	TextSelection () {}
	explicit TextSelection (TextPos at) : anchor (at), cursor (at) {}
	TextSelection (TextPos a, TextPos c) : anchor (a), cursor (c) {}
	bool empty () const { return anchor == cursor; }
	TextPos start () const { return anchor < cursor ? anchor : cursor; }
	TextPos end () const { return anchor < cursor ? cursor : anchor; }
	bool operator == (const TextSelection & o) const { return anchor == o.anchor && cursor == o.cursor; }
	bool operator != (const TextSelection & o) const { return ! (*this == o); }
};

// The kinds of characters, for moving by words and selecting a word.
enum CharClass { CHAR_SPACE, CHAR_WORD, CHAR_PUNCTUATION };
CharClass charClassOf (unsigned char c);

class TextBuffer {
public:
	TextBuffer ();

	// All of the text of a file, and back, with its line ends.
	void setText (const std::string & fileText);
	std::string fileText () const;
	// The text with \n between lines, as an edit sees it.
	std::string text () const;

	int lineCount () const { return (int) lines.size (); }
	const std::string & line (int n) const { return lines[n]; }
	// The stamp of a line, which no other state of any line has had.
	uint64_t lineStamp (int n) const { return stamps[n]; }
	// Goes up with every change.
	uint64_t version () const { return changes; }

	TextPos begin () const { return TextPos (0, 0); }
	TextPos end () const { return TextPos (lineCount () - 1, (int) lines.back ().size ()); }
	// The nearest place that there is, at the start of a character.
	TextPos clamp (TextPos at) const;
	// The text between two places, with \n between lines.
	std::string textIn (TextPos from, TextPos to) const;

	// Insert text, in which lines end with \n (\r\n becomes \n), and return
	// where it ends.
	TextPos insert (TextPos at, const std::string & text);
	void erase (TextPos from, TextPos to);
	// Where text inserted at a place ends.
	static TextPos endOf (TextPos at, const std::string & text);

	// The next and the previous character, over the ends of lines.
	TextPos next (TextPos at) const;
	TextPos previous (TextPos at) const;
	// The start of the next word, or the end of the line; the start of this
	// word or the one before it.
	TextPos wordRight (TextPos at) const;
	TextPos wordLeft (TextPos at) const;
	// The word, or the run of spaces or of punctuation, at a place.
	TextSelection wordAt (TextPos at) const;

	// Whether some lines end with \r\n and others with \n.
	bool mixedLineEnds () const;
	// Take the ends of the lines of another text with as many lines.
	void copyLineEnds (const TextBuffer & other);

	// How the file was.
	bool crlf;			// Most of its lines ended with \r\n, as new ones do
	bool bom;			// It started with a byte order mark

private:
	uint64_t newStamp ();

	std::vector<std::string> lines;
	std::vector<uint64_t> stamps;
	std::vector<unsigned char> crlfEnds;	// Of each line: whether it ends with \r\n
	uint64_t changes;
};

// The byte where the character before or after a column starts.
int previousCharStart (const std::string & line, int column);
int nextCharStart (const std::string & line, int column);
// The column on screen of a byte, with tabs to the next multiple of
// tabSize, and each character one column; and the byte nearest a column.
int displayColumn (const std::string & line, int byte, int tabSize);
int byteAtDisplayColumn (const std::string & line, int column, int tabSize);

} // namespace devkit

#endif
