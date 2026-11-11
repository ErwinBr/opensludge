#ifndef SLUDGE_DEVKIT_SCRIPT_ASSIST_H
#define SLUDGE_DEVKIT_SCRIPT_ASSIST_H

/*
 * What the script editor knows at a place of a script: the names that can
 * be typed there (completion), the call that the cursor is in (signature
 * help), what a name is (hover), and where it is made (go to definition);
 * from the built-ins (ScriptCatalog) and the project (ScriptIndex).
 */

#include <string>
#include <vector>

#include "ScriptCatalog.h"
#include "ScriptIndex.h"
#include "ScriptLexer.h"
#include "TextBuffer.h"

namespace devkit {

struct Completion {
	std::string label;		// What it inserts, too
	TokenKind kind;			// For its colour
	std::string detail;		// Such as "sub ego.walk (x, y)" or "= 45"
	std::string doc;
	int score;
	Completion () : kind (TOKEN_IDENTIFIER), score (0) {}
};

struct CompletionList {
	TextPos from;			// Where the word that it replaces starts
	std::string typed;		// That word, to the cursor
	std::vector<Completion> items;
};

struct SignatureHelp {
	std::string name;
	std::vector<Signature> signatures;
	int argument;			// Which one the cursor is in, from 0
	int active;				// The signature that has room for it
	std::string doc;
	SignatureHelp () : argument (0), active (0) {}
};

struct HoverInfo {
	std::string title;		// The declaration or the syntax
	std::string body;		// Its docs
	std::string where;		// "file, line n" of a name of the project
	std::string page;		// Its page of the help, or empty
	TextSelection range;	// Of the name
};

struct Definition {
	std::string file;		// As the index knows it
	TextPos at;
};

namespace assist {

// Whether a word that is being typed matches a name, and how well: 0 when
// it doesn't; more for a prefix, then for the starts of the parts of a
// name (aC for addCharacter), then for its letters in order.
int matchScore (const std::string & typed, const std::string & name);

// The names that can go at a place. With everything, also when nothing
// has been typed yet (Ctrl+Space); otherwise only once a word is started
// or after a dot.
CompletionList complete (const TextBuffer & text, TextPos at, const ScriptIndex & index, const std::string & file, bool everything);
// The call that the cursor is in, between its brackets.
bool signatureAt (const TextBuffer & text, TextPos at, const ScriptIndex & index, const std::string & file, SignatureHelp * help);
bool hoverAt (const TextBuffer & text, TextPos at, const ScriptIndex & index, const std::string & file, HoverInfo * info);
bool definitionAt (const TextBuffer & text, TextPos at, const ScriptIndex & index, const std::string & file, Definition * definition);

} // namespace assist

} // namespace devkit

#endif
