#ifndef SLUDGE_DEVKIT_TRANSLATION_DOCUMENT_H
#define SLUDGE_DEVKIT_TRANSLATION_DOCUMENT_H

/*
 * A translation (.tra): the strings of a game in another language. We
 * read and write the files as the GTK Translation Editor did, and find the
 * strings of a project as it did, without its changes of folder: the
 * window name, the quit message, and the strings in quotes of the scripts.
 *
 * Each string needs a translation, has one, or needs none. In a file, a
 * line is the original and the translation separated by a tab; a tab and
 * a * after the original mean that it still needs one, and an original
 * alone that it needs none.
 */

#include <string>
#include <vector>

#include "Document.h"

namespace devkit {

struct TranslationLine {
	enum Type { MISSING, TRANSLATED, NOT_NEEDED };
	std::string original, translation;
	Type type;

	TranslationLine () : type (MISSING) {}
	bool operator == (const TranslationLine & o) const { return original == o.original && translation == o.translation && type == o.type; }
	bool operator != (const TranslationLine & o) const { return ! (*this == o); }
};

struct Language {
	unsigned int id;		// The number that the game chooses it by
	std::string name;
	Language () : id (0) {}
	bool operator == (const Language & o) const { return id == o.id && name == o.name; }
	bool operator != (const Language & o) const { return ! (*this == o); }
};

// Which strings a view shows.
enum TranslationFilter { SHOW_ALL, SHOW_MISSING, SHOW_TRANSLATED, SHOW_NOT_NEEDED };

// Whether a view shows a line: of the filter's type, and with the search
// text in its original or its translation, in any case of ASCII letters.
bool translationLineShown (const TranslationLine & line, TranslationFilter filter, const std::string & search);

// What loading the strings of a project did.
struct StringsUpdate {
	bool loaded;				// False when we couldn't read the project
	int added, removed;			// New strings, and unused ones that went
	std::vector<std::string> problems;	// Scripts that we couldn't read, and the like
	StringsUpdate () : loaded (false), added (0), removed (0) {}
};

class TranslationDocument : public Document {
public:
	static DocumentType type ();

	const char * kind () const { return "Translation"; }
	std::string untitledName () const { return "Untitled Translation.tra"; }

	const Language & language () const { return theLanguage; }
	const std::vector<TranslationLine> & lines () const { return theLines; }
	int count (TranslationLine::Type type) const;

	// Changes, as commands that undo. Commands with the same mergeKey
	// (other than 0) merge, as while typing.
	void setLanguage (const Language & language, int mergeKey = 0);
	// Give a line a translation, which makes it need one; an empty one
	// leaves it missing.
	void setTranslation (int index, const std::string & translation);
	// Whether a line needs a translation. Without, it loses its translation.
	void setNeedsTranslation (int index, bool needs);

	// Find the strings of a project: new ones need a translation, and the
	// ones that the project no longer has go.
	StringsUpdate loadStrings (const std::string & projectFile);

	static bool readTranslation (const std::string & path, Language * language, std::vector<TranslationLine> * lines, std::string * error);
	static bool writeTranslation (const std::string & path, const Language & language, const std::vector<TranslationLine> & lines, std::string * error);
	// The strings of a script, unescaped, as loadStrings finds them.
	static std::vector<std::string> stringsOfScript (const std::string & text);

protected:
	bool read (const std::string & path, std::string * error);
	bool write (const std::string & path, std::string * error);

private:
	void setLine (int index, const TranslationLine & line, const std::string & label);

	Language theLanguage;
	std::vector<TranslationLine> theLines;
};

} // namespace devkit

#endif
