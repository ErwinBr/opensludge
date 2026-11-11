#ifndef SLUDGE_DEVKIT_TEXT_EDITING_H
#define SLUDGE_DEVKIT_TEXT_EDITING_H

/*
 * What the keys of the script editor do to a script: typing with brackets
 * and quotes that close themselves, a new line that keeps the indent,
 * deleting, indenting, comments, moving lines, and finding and replacing.
 * Each is a change of the document that undoes; the view only turns keys
 * into these.
 */

#include <string>
#include <vector>

#include "ScriptDocument.h"

namespace devkit {

namespace editing {

// Type characters in place of the selection. A bracket or a quote gets its
// partner when what follows allows it, and wraps a selection; typing the
// closing one that is already next moves over it; a } on a line of only
// spaces goes back one indent.
void type (ScriptDocument & script, const std::string & characters);
// A new line in place of the selection, with the indent of this line, and
// one more after a {. Between { and } the } goes on a line of its own.
void newLine (ScriptDocument & script);
// Insert text as it is, in place of the selection, as pasting does.
void insert (ScriptDocument & script, const std::string & text, const std::string & label);
// Delete the selection, or the character (or word) before or after the
// cursor. Backspace between an empty pair of brackets deletes both, and
// in the indent of a line it goes back to the indent before.
void backspace (ScriptDocument & script, bool word);
void deleteForward (ScriptDocument & script, bool word);
// The text of the selection, which cut then deletes.
std::string selectedText (const ScriptDocument & script);
void deleteSelection (ScriptDocument & script, const std::string & label);

// Tab: indent the lines of a selection over several lines, or insert an
// indent; Shift+Tab takes one away from each line.
void tab (ScriptDocument & script);
void indentLines (ScriptDocument & script, bool more);
// Put # before the lines of the selection, or take it away when they all
// have it.
void toggleComment (ScriptDocument & script);
// Move the lines of the selection up (-1) or down (1), copy them below or
// above, or delete them.
void moveLines (ScriptDocument & script, int direction);
void duplicateLines (ScriptDocument & script, int direction);
void deleteLines (ScriptDocument & script);

// The first and the last line of the selection. A selection that ends at
// the start of a line leaves that line out.
void selectedLines (const ScriptDocument & script, int * first, int * last);

// The leading spaces and tabs of a line.
std::string indentOf (const std::string & line);
// The matching bracket of the one at a place, or the same place when it is
// none or has no match. Brackets in strings and comments don't count.
TextPos matchingBracket (const TextBuffer & text, TextPos at);

// Finding.
struct FindOptions {
	bool matchCase;
	bool wholeWord;
	FindOptions () : matchCase (false), wholeWord (false) {}
};
// Every place of text, which is on one line, in order.
std::vector<TextSelection> findAll (const TextBuffer & text, const std::string & wanted, const FindOptions & options);
// The next one after a place, or the one before it, round to the start
// or the end. Empty when there is none.
TextSelection findNext (const TextBuffer & text, TextPos from, const std::string & wanted, const FindOptions & options, bool backwards);
// Replace the selection when it is a match and select the next one.
// Returns whether it replaced one.
bool replaceNext (ScriptDocument & script, const std::string & wanted, const std::string & with, const FindOptions & options);
// Replace every one as one change, and return how many.
int replaceAll (ScriptDocument & script, const std::string & wanted, const std::string & with, const FindOptions & options);

} // namespace editing

} // namespace devkit

#endif
