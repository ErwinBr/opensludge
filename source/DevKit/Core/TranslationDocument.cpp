#include <stdio.h>
#include <stdlib.h>

#include <map>

#include "ProjectCheck.h"
#include "ProjectDocument.h"
#include "TranslationDocument.h"

namespace devkit {

static const char * const HEADER_LINE = "### SLUDGE Translation File ###";

static std::string lowerASCII (const std::string & text) {
	std::string lower = text;
	for (size_t i = 0; i < lower.size (); i ++) {
		if (lower[i] >= 'A' && lower[i] <= 'Z') lower[i] = (char) (lower[i] - 'A' + 'a');
	}
	return lower;
}

bool translationLineShown (const TranslationLine & line, TranslationFilter filter, const std::string & search) {
	switch (filter) {
		case SHOW_MISSING: if (line.type != TranslationLine::MISSING) return false; break;
		case SHOW_TRANSLATED: if (line.type != TranslationLine::TRANSLATED) return false; break;
		case SHOW_NOT_NEEDED: if (line.type != TranslationLine::NOT_NEEDED) return false; break;
		default: break;
	}
	if (search.empty ()) return true;
	std::string wanted = lowerASCII (search);
	return lowerASCII (line.original).find (wanted) != std::string::npos
		|| lowerASCII (line.translation).find (wanted) != std::string::npos;
}

// Spaces at the ends go, as trimEdgeSpace did; tabs stay.
static std::string trimSpaces (const std::string & text) {
	size_t start = text.find_first_not_of (' ');
	if (start == std::string::npos) return std::string ();
	size_t end = text.find_last_not_of (' ');
	return text.substr (start, end - start + 1);
}

// The lines of a text, without their line ends.
static std::vector<std::string> linesOf (const std::string & text) {
	std::vector<std::string> lines;
	size_t start = 0;
	while (start < text.size ()) {
		size_t end = text.find ('\n', start);
		if (end == std::string::npos) end = text.size ();
		std::string line = text.substr (start, end - start);
		while (! line.empty () && (line[line.size () - 1] == '\r' || line[line.size () - 1] == '\n')) line.erase (line.size () - 1);
		lines.push_back (line);
		start = end + 1;
	}
	return lines;
}

static bool readFile (const std::string & path, std::string * text) {
	FILE * fp = fopen (path.c_str (), "rb");
	if (! fp) return false;
	char buffer[16384];
	size_t got;
	text->clear ();
	while ((got = fread (buffer, 1, sizeof (buffer), fp)) > 0) text->append (buffer, got);
	fclose (fp);
	return true;
}

DocumentType TranslationDocument::type () {
	DocumentType type;
	type.name = "Translation";
	type.extensions.push_back ("tra");
	type.create = [] () -> Document * { return new TranslationDocument; };
	return type;
}

int TranslationDocument::count (TranslationLine::Type type) const {
	int n = 0;
	for (size_t i = 0; i < theLines.size (); i ++) {
		if (theLines[i].type == type) n ++;
	}
	return n;
}

// Read into a language and lines of our own, which become the caller's
// only when the whole file is right.
static bool parseTranslation (const std::string & path, Language * language, std::vector<TranslationLine> * lines, std::string * error);

bool TranslationDocument::readTranslation (const std::string & path, Language * language, std::vector<TranslationLine> * lines, std::string * error) {
	Language l;
	std::vector<TranslationLine> read;
	if (! parseTranslation (path, &l, &read, error)) return false;
	*language = l;
	lines->swap (read);
	return true;
}

static bool parseTranslation (const std::string & path, Language * language, std::vector<TranslationLine> * lines, std::string * error) {
	std::string text;
	if (! readFile (path, &text)) {
		*error = "The file can't be opened.";
		return false;
	}
	std::vector<std::string> fileLines = linesOf (text);
	if (fileLines.empty ()) {
		*error = "The file is empty.";
		return false;
	}
	if (fileLines[0] != HEADER_LINE) {
		*error = "This isn't a SLUDGE translation file: its first line isn't right.";
		return false;
	}
	*language = Language ();
	lines->clear ();
	enum { MODE_UNKNOWN, MODE_ID, MODE_NAME, MODE_STRINGS } mode = MODE_UNKNOWN;
	bool haveName = false;
	for (size_t n = 1; n < fileLines.size (); n ++) {
		const std::string & line = fileLines[n];
		if (line.empty ()) continue;
		if (invalidUTF8Offset (line) >= 0) {
			char where[160];
			snprintf (where, sizeof (where), "Line %d of the file isn't UTF-8 text. ", (int) n + 1);
			*error = std::string (where) + "When the translation is in a project, Project \xE2\x86\x92 Check Files can convert it.";
			return false;
		}
		if (line[0] == '[') {
			mode = line == "[ID]" ? MODE_ID : line == "[DATA]" ? MODE_STRINGS : line == "[NAME]" ? MODE_NAME : MODE_UNKNOWN;
			continue;
		}
		switch (mode) {
			case MODE_ID: {
				long id = strtol (line.c_str (), NULL, 10);
				if (id > 0) language->id = (unsigned int) id;
				break;
			}
			case MODE_NAME:
				if (! haveName) {
					language->name = line;
					haveName = true;
				}
				break;
			case MODE_STRINGS: {
				TranslationLine l;
				size_t tab = line.find ('\t');
				l.original = trimSpaces (line.substr (0, tab));
				if (tab == std::string::npos) {
					l.type = TranslationLine::NOT_NEEDED;
				} else {
					std::string rest = trimSpaces (line.substr (tab + 1));
					if (rest == "*\t") {
						l.type = TranslationLine::MISSING;
					} else {
						l.type = TranslationLine::TRANSLATED;
						l.translation = rest;
					}
				}
				lines->push_back (l);
				break;
			}
			default:
				break;
		}
	}
	return true;
}

bool TranslationDocument::writeTranslation (const std::string & path, const Language & language, const std::vector<TranslationLine> & lines, std::string * error) {
	// Text mode, as the GTK editor wrote it: with \r\n on Windows.
	FILE * fp = fopen (path.c_str (), "wt");
	if (! fp) {
		*error = "The file can't be written.";
		return false;
	}
	fprintf (fp, "%s\n\n[ID]\n%u\n\n[NAME]\n%s\n\n[DATA]\n", HEADER_LINE, language.id, language.name.c_str ());
	for (size_t i = 0; i < lines.size (); i ++) {
		const TranslationLine & l = lines[i];
		switch (l.type) {
			case TranslationLine::MISSING:
				fprintf (fp, "%s\t*\t\n", l.original.c_str ());
				break;
			case TranslationLine::TRANSLATED:
				fprintf (fp, "%s\t%s\n", l.original.c_str (), l.translation.c_str ());
				break;
			default:
				fprintf (fp, "%s\n", l.original.c_str ());
				break;
		}
	}
	if (fclose (fp) != 0) {
		*error = "The file can't be written.";
		return false;
	}
	return true;
}

bool TranslationDocument::read (const std::string & path, std::string * error) {
	return readTranslation (path, &theLanguage, &theLines, error);
}

bool TranslationDocument::write (const std::string & path, std::string * error) {
	return writeTranslation (path, theLanguage, theLines, error);
}

void TranslationDocument::setLanguage (const Language & language, int mergeKey) {
	if (language == theLanguage) return;
	commands ().run (new ValueCommand<Language> ("Language", theLanguage, language,
		[this] (const Language & l) { theLanguage = l; }, mergeKey));
}

void TranslationDocument::setLine (int index, const TranslationLine & line, const std::string & label) {
	if (index < 0 || index >= (int) theLines.size () || theLines[index] == line) return;
	commands ().run (new ValueCommand<TranslationLine> (label, theLines[index], line,
		[this, index] (const TranslationLine & l) { if (index < (int) theLines.size ()) theLines[index] = l; }));
}

void TranslationDocument::setTranslation (int index, const std::string & translation) {
	if (index < 0 || index >= (int) theLines.size ()) return;
	// A line of the file has no tabs or line ends of its own.
	std::string text;
	for (size_t i = 0; i < translation.size (); i ++) {
		char c = translation[i];
		text += c == '\t' || c == '\n' || c == '\r' ? ' ' : c;
	}
	text = trimSpaces (text);
	TranslationLine line = theLines[index];
	if (text.empty () && line.type == TranslationLine::NOT_NEEDED) return;
	line.translation = text;
	line.type = text.empty () ? TranslationLine::MISSING : TranslationLine::TRANSLATED;
	setLine (index, line, "Translation");
}

void TranslationDocument::setNeedsTranslation (int index, bool needs) {
	if (index < 0 || index >= (int) theLines.size ()) return;
	TranslationLine line = theLines[index];
	if (needs) {
		if (line.type != TranslationLine::NOT_NEEDED) return;
		line.type = line.translation.empty () ? TranslationLine::MISSING : TranslationLine::TRANSLATED;
	} else {
		line.type = TranslationLine::NOT_NEEDED;
		line.translation.clear ();
	}
	setLine (index, line, needs ? "Translate" : "Don't Translate");
}

std::vector<std::string> TranslationDocument::stringsOfScript (const std::string & text) {
	std::vector<std::string> found;
	std::vector<std::string> lines = linesOf (text);
	for (size_t n = 0; n < lines.size (); n ++) {
		const std::string & line = lines[n];
		for (size_t a = 0; a < line.size (); a ++) {
			if (line[a] == '#') break;		// A comment
			if (line[a] != '"') continue;
			while (a + 1 < line.size () && line[a + 1] == ' ') a ++;	// No spaces at the start
			bool escape = false;
			std::string string;
			for (size_t b = a + 1; b < line.size (); b ++) {
				char c = line[b];
				if (c == '\\') {
					// The backslash of an escape goes; a backslash after one stays.
					if (escape) string += c;
					escape = ! escape;
				} else if (c == '"' && ! escape) {
					if (b != a + 1) {
						string = trimSpaces (string);
						if (! string.empty ()) found.push_back (string);
					}
					a = b;
					break;
				} else {
					// The GTK editor left tabs out, which would break the file.
					if (c != '\t') string += c;
					escape = false;
				}
			}
		}
	}
	return found;
}

StringsUpdate TranslationDocument::loadStrings (const std::string & projectFile) {
	StringsUpdate update;
	ProjectSettings settings;
	std::vector<std::string> files;
	std::string error;
	if (! ProjectDocument::readProject (projectFile, &settings, &files, &error)) {
		update.problems.push_back (error.empty () ? "The project can't be read." : error);
		return update;
	}
	update.loaded = true;

	std::vector<std::string> strings;
	if (! trimSpaces (settings.windowName).empty ()) strings.push_back (trimSpaces (settings.windowName));
	if (! trimSpaces (settings.quitMessage).empty ()) strings.push_back (trimSpaces (settings.quitMessage));
	std::string folder = folderOf (projectFile);
	for (size_t f = 0; f < files.size (); f ++) {
		if (extensionOf (files[f]) != "slu") continue;
		bool found, caseDiffers;
		std::string name = nameOnDisk (folder, files[f], &found, &caseDiffers);
		std::string text;
		if (! found || ! readFile (folder + name, &text)) {
			update.problems.push_back ("Can't read " + files[f] + ".");
			continue;
		}
		if (invalidUTF8Offset (text) >= 0) {
			update.problems.push_back (files[f] + " isn't UTF-8 text, so we left out its strings. Project \xE2\x86\x92 Check Files can convert it.");
			continue;
		}
		std::vector<std::string> more = stringsOfScript (text);
		strings.insert (strings.end (), more.begin (), more.end ());
	}

	// The lines that we have keep their place and translation; new strings
	// go at the end, and lines whose strings are gone go.
	std::map<std::string, size_t> first;
	for (size_t i = 0; i < theLines.size (); i ++) {
		if (! first.count (theLines[i].original)) first[theLines[i].original] = i;
	}
	std::vector<bool> used (theLines.size (), false);
	std::vector<TranslationLine> added;
	std::map<std::string, bool> seen;
	for (size_t s = 0; s < strings.size (); s ++) {
		std::map<std::string, size_t>::iterator have = first.find (strings[s]);
		if (have != first.end ()) {
			used[have->second] = true;
		} else if (! seen[strings[s]]) {
			seen[strings[s]] = true;
			TranslationLine line;
			line.original = strings[s];
			added.push_back (line);
		}
	}
	std::vector<TranslationLine> after;
	for (size_t i = 0; i < theLines.size (); i ++) {
		if (used[i]) after.push_back (theLines[i]);
		else update.removed ++;
	}
	after.insert (after.end (), added.begin (), added.end ());
	update.added = (int) added.size ();
	if (after != theLines) {
		commands ().run (new ValueCommand<std::vector<TranslationLine> > ("Load Strings", theLines, after,
			[this] (const std::vector<TranslationLine> & l) { theLines = l; }));
	}
	return update;
}

} // namespace devkit
