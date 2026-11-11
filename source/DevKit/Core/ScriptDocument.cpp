#include <stdio.h>
#include <sys/stat.h>

#include <map>

#include "ProjectCheck.h"
#include "ScriptDocument.h"

namespace devkit {

// The changes of one undo.
class TextEditCommand : public Command {
public:
	TextEditCommand (ScriptDocument * d, const std::vector<TextEdit> & e, const TextSelection & b, const TextSelection & a,
			EditKind k, const std::string & l)
		: document (d), edits (e), before (b), after (a), kind (k), text (l) {}

	std::string label () const { return text; }

	void apply () {
		for (size_t i = 0; i < edits.size (); i ++) {
			const TextEdit & e = edits[i];
			document->text.erase (e.at, TextBuffer::endOf (e.at, e.removed));
			document->text.insert (e.at, e.inserted);
		}
		document->theSelection = after;
	}

	void revert () {
		for (size_t i = edits.size (); i -- > 0; ) {
			const TextEdit & e = edits[i];
			document->text.erase (e.at, TextBuffer::endOf (e.at, e.inserted));
			document->text.insert (e.at, e.removed);
		}
		document->theSelection = before;
	}

	bool mergeWith (const Command & next) {
		const TextEditCommand * other = dynamic_cast<const TextEditCommand *> (&next);
		if (! other || other->document != document || other->kind != kind || kind == EDIT_OTHER) return false;
		if (edits.size () != 1 || other->edits.size () != 1) return false;
		TextEdit & last = edits[0];
		const TextEdit & e = other->edits[0];
		switch (kind) {
			case EDIT_TYPING: {
				if (! e.removed.empty () || e.inserted.empty () || e.inserted.find ('\n') != std::string::npos) return false;
				if (e.at != TextBuffer::endOf (last.at, last.inserted)) return false;
				// A new word starts a new undo.
				unsigned char lastChar = last.inserted.empty () ? ' ' : (unsigned char) last.inserted[last.inserted.size () - 1];
				if (charClassOf (lastChar) == CHAR_SPACE && charClassOf ((unsigned char) e.inserted[0]) != CHAR_SPACE) return false;
				last.inserted += e.inserted;
				break;
			}
			case EDIT_BACKSPACE:
				if (! e.inserted.empty () || ! last.inserted.empty ()) return false;
				if (TextBuffer::endOf (e.at, e.removed) != last.at) return false;
				last.at = e.at;
				last.removed = e.removed + last.removed;
				break;
			case EDIT_DELETE:
				if (! e.inserted.empty () || ! last.inserted.empty () || e.at != last.at) return false;
				last.removed += e.removed;
				break;
			default:
				return false;
		}
		after = other->after;
		return true;
	}

private:
	ScriptDocument * document;
	std::vector<TextEdit> edits;
	TextSelection before, after;
	EditKind kind;
	std::string text;
};

ScriptDocument::ScriptDocument () : diskTime (-1), diskSize (-1) {}

DocumentType ScriptDocument::type () {
	DocumentType type;
	type.name = "Script";
	type.extensions.push_back ("slu");
	type.extensions.push_back ("sld");
	type.create = [] () -> Document * { return new ScriptDocument; };
	return type;
}

void ScriptDocument::setSelection (const TextSelection & selection) {
	TextSelection s (text.clamp (selection.anchor), text.clamp (selection.cursor));
	if (s == theSelection) return;
	theSelection = s;
	commands ().stopMerging ();
}

void ScriptDocument::replace (TextPos from, TextPos to, const std::string & with, EditKind kind, const std::string & label) {
	TextPos start = from < to ? from : to;
	TextPos end = TextBuffer::endOf (text.clamp (start), with);
	replace (from, to, with, kind, label, TextSelection (end));
}

void ScriptDocument::replace (TextPos from, TextPos to, const std::string & with, EditKind kind, const std::string & label, const TextSelection & after) {
	edit (std::vector<TextChange> (1, TextChange (from, to, with)), after, kind, label);
}

void ScriptDocument::edit (const std::vector<TextChange> & changes, const TextSelection & after, EditKind kind, const std::string & label) {
	TextSelection before = theSelection;
	std::vector<TextEdit> edits;
	for (size_t i = 0; i < changes.size (); i ++) {
		TextPos from = text.clamp (changes[i].from), to = text.clamp (changes[i].to);
		if (to < from) {
			TextPos t = from;
			from = to;
			to = t;
		}
		std::string removed = text.textIn (from, to);
		if (removed.empty () && changes[i].with.empty ()) continue;
		// Lines end with \n in the text.
		std::string inserted;
		for (size_t c = 0; c < changes[i].with.size (); c ++) {
			if (changes[i].with[c] != '\r') inserted += changes[i].with[c];
		}
		text.erase (from, to);
		text.insert (from, inserted);
		edits.push_back (TextEdit (from, removed, inserted));
	}
	if (edits.empty ()) {
		setSelection (after);
		return;
	}
	theSelection = TextSelection (text.clamp (after.anchor), text.clamp (after.cursor));
	commands ().add (new TextEditCommand (this, edits, before, theSelection, kind, label));
	if (kind == EDIT_OTHER) commands ().stopMerging ();
}

void ScriptDocument::setText (const std::string & newText, const std::string & label) {
	TextSelection keep = theSelection;
	edit (std::vector<TextChange> (1, TextChange (text.begin (), text.end (), newText)), keep, EDIT_OTHER, label);
}

std::string ScriptDocument::indentUnit () const {
	int tabs = 0;
	std::map<int, int> spaces;		// By the number of spaces
	for (int l = 0; l < text.lineCount (); l ++) {
		const std::string & line = text.line (l);
		if (line.empty ()) continue;
		if (line[0] == '\t') {
			tabs ++;
		} else if (line[0] == ' ') {
			size_t n = line.find_first_not_of (' ');
			// Lines of only spaces, and those that continue a comment of the
			// line before, say nothing.
			if (n == std::string::npos || line[n] == '*') continue;
			spaces[(int) n] ++;
		}
	}
	int spaced = 0;
	for (std::map<int, int>::const_iterator i = spaces.begin (); i != spaces.end (); ++ i) spaced += i->second;
	if (spaced <= tabs) return "\t";
	// The smallest indent that a fair share of the lines have.
	for (std::map<int, int>::const_iterator i = spaces.begin (); i != spaces.end (); ++ i) {
		if (i->first >= 2 && i->first <= 8 && i->second * 10 >= spaced) return std::string (i->first, ' ');
	}
	return "\t";
}

static bool statFile (const std::string & path, int64_t * time, int64_t * size) {
	struct stat info;
	if (stat (path.c_str (), &info) != 0) return false;
	*time = (int64_t) info.st_mtime;
	*size = (int64_t) info.st_size;
	return true;
}

void ScriptDocument::rememberDiskState (const std::string & path) {
	if (! statFile (path, &diskTime, &diskSize)) diskTime = diskSize = -1;
}

bool ScriptDocument::changedOnDisk () const {
	if (path ().empty () || diskTime < 0) return false;
	int64_t time, size;
	if (! statFile (path (), &time, &size)) return false;
	return time != diskTime || size != diskSize;
}

void ScriptDocument::acceptDiskState () {
	if (! path ().empty ()) rememberDiskState (path ());
}

bool ScriptDocument::reload (std::string * error) {
	std::string fileText;
	if (! readScript (path (), &fileText, error)) return false;
	TextBuffer fresh;
	fresh.setText (fileText);
	text.crlf = fresh.crlf;
	text.bom = fresh.bom;
	TextSelection keep = theSelection;
	edit (std::vector<TextChange> (1, TextChange (text.begin (), text.end (), fresh.text ())), keep, EDIT_OTHER, "Reload");
	text.copyLineEnds (fresh);
	rememberDiskState (path ());
	commands ().markSaved ();
	return true;
}

bool ScriptDocument::readScript (const std::string & path, std::string * fileText, std::string * error) {
	FILE * fp = fopen (path.c_str (), "rb");
	if (! fp) {
		*error = "The file can't be opened.";
		return false;
	}
	char buffer[16384];
	size_t got;
	fileText->clear ();
	while ((got = fread (buffer, 1, sizeof (buffer), fp)) > 0) fileText->append (buffer, got);
	fclose (fp);
	long bad = invalidUTF8Offset (*fileText);
	if (bad >= 0) {
		int line = 1;
		for (long i = 0; i < bad; i ++) {
			if ((*fileText)[i] == '\n') line ++;
		}
		char where[160];
		snprintf (where, sizeof (where), "Line %d of the file isn't UTF-8 text. ", line);
		*error = std::string (where) + "When the script is in a project, Project \xE2\x86\x92 Check Files can convert it.";
		return false;
	}
	return true;
}

bool ScriptDocument::read (const std::string & path, std::string * error) {
	std::string fileText;
	if (! readScript (path, &fileText, error)) return false;
	text.setText (fileText);
	theSelection = TextSelection ();
	rememberDiskState (path);
	return true;
}

bool ScriptDocument::write (const std::string & path, std::string * error) {
	std::string fileText = text.fileText ();
	FILE * fp = fopen (path.c_str (), "wb");
	if (! fp) {
		*error = "The file can't be written.";
		return false;
	}
	bool ok = fwrite (fileText.data (), 1, fileText.size (), fp) == fileText.size ();
	if (fclose (fp) != 0) ok = false;
	if (! ok) {
		*error = "The file couldn't be written completely; the disk may be full.";
		return false;
	}
	rememberDiskState (path);
	return true;
}

} // namespace devkit
