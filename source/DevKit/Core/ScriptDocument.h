#ifndef SLUDGE_DEVKIT_SCRIPT_DOCUMENT_H
#define SLUDGE_DEVKIT_SCRIPT_DOCUMENT_H

/*
 * A script (.slu) or a file of definitions (.sld): its text, and the
 * selection, which undo puts back where it was. Every change is a list of
 * edits that undo together; typing and deleting merge, so that undo takes
 * back a word at a time.
 *
 * The text must be UTF-8, as the compiler wants; Project > Check Files
 * converts the files of old projects.
 */

#include <stdint.h>

#include <string>
#include <vector>

#include "Document.h"
#include "TextBuffer.h"

namespace devkit {

// A change to make: the text between two places becomes another.
struct TextChange {
	TextPos from, to;
	std::string with;
	TextChange () {}
	TextChange (TextPos f, TextPos t, const std::string & w) : from (f), to (t), with (w) {}
};

// A change that was made: what was removed at a place, and what took its
// place.
struct TextEdit {
	TextPos at;
	std::string removed, inserted;
	TextEdit () {}
	TextEdit (TextPos a, const std::string & r, const std::string & i) : at (a), removed (r), inserted (i) {}
};

// What kind of change an edit is, for merging them into one undo.
enum EditKind {
	EDIT_OTHER,			// Never merges
	EDIT_TYPING,		// Typing characters, one after another
	EDIT_BACKSPACE,		// Deleting the characters before the cursor
	EDIT_DELETE,		// Deleting the characters after it
};

class ScriptDocument : public Document {
public:
	ScriptDocument ();
	static DocumentType type ();

	const char * kind () const { return "Script"; }
	std::string untitledName () const { return "Untitled Script.slu"; }

	const TextBuffer & buffer () const { return text; }
	const TextSelection & selection () const { return theSelection; }
	// Moving the cursor isn't a change; it stops typing from merging with
	// what was typed before.
	void setSelection (const TextSelection & selection);

	// Replace the text between two places, and put the selection after it,
	// or at a place of our own.
	void replace (TextPos from, TextPos to, const std::string & with, EditKind kind, const std::string & label);
	void replace (TextPos from, TextPos to, const std::string & with, EditKind kind, const std::string & label, const TextSelection & after);
	// Several changes as one, which undo together. The places of each are
	// in the text as the changes before it left it.
	void edit (const std::vector<TextChange> & changes, const TextSelection & after, EditKind kind, const std::string & label);
	// Replace all of the text, as one change.
	void setText (const std::string & newText, const std::string & label);

	// How the file indents: a tab, or a number of spaces, as most of its
	// lines do; a tab when it doesn't say.
	std::string indentUnit () const;

	// Whether the file changed on disk since we read or saved it.
	bool changedOnDisk () const;
	// Read the file again: the change undoes as any other.
	bool reload (std::string * error);
	// Forget the change on disk, so that we don't ask about it again.
	void acceptDiskState ();

	// Read the text of a file as a script: UTF-8, or the reason why not.
	static bool readScript (const std::string & path, std::string * fileText, std::string * error);

protected:
	bool read (const std::string & path, std::string * error);
	bool write (const std::string & path, std::string * error);

private:
	friend class TextEditCommand;
	void rememberDiskState (const std::string & path);

	TextBuffer text;
	TextSelection theSelection;
	int64_t diskTime, diskSize;		// -1 when there is no file
};

} // namespace devkit

#endif
