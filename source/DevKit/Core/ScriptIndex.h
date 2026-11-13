#ifndef SLUDGE_DEVKIT_SCRIPT_INDEX_H
#define SLUDGE_DEVKIT_SCRIPT_INDEX_H

/*
 * The names that the scripts of a project make: their subs, objectTypes
 * with their members and events, global variables, flags, and the
 * constants of definition files (.sld); and where the subs and events have
 * their local variables. The script editor colours, completes, explains
 * and finds them with this.
 *
 * This is a light reading of the scripts, not the compiler: it takes what
 * it understands and goes on after what it doesn't, as the text of a
 * script that is being typed often is wrong.
 */

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include "ScriptLexer.h"
#include "TextBuffer.h"

namespace devkit {

enum SymbolKind {
	SYMBOL_SUB,
	SYMBOL_OBJECT_TYPE,
	SYMBOL_VARIABLE,		// Global, or a member of an objectType
	SYMBOL_EVENT,			// Of an objectType: its name is the other objectType
	SYMBOL_FLAG,
	SYMBOL_DEFINITION,		// A constant of a definition file
	SYMBOL_LOCAL,			// A local variable of a sub or an event
	SYMBOL_PARAMETER,		// A parameter of a sub
};

struct ScriptSymbol {
	SymbolKind kind;
	std::string name;
	std::string owner;		// The objectType of a member or an event, or empty
	std::string file;		// As the index knows it
	TextPos at;				// Of the name
	std::vector<std::string> parameters;	// Of a sub
	// An objectType's name on screen, a definition's value, an event's sub
	// when it calls one, or a global's first value.
	std::string detail;
	std::string doc;		// The # comment lines just above it, without #
	bool unfreezable;

	ScriptSymbol () : kind (SYMBOL_SUB), unfreezable (false) {}
	// "sub name (a, b)", "objectType name (\"shown\")", "var name"...
	std::string declaration () const;
	// Whether name () calls it: a sub, or an event with code of its own,
	// which the compiler makes a sub of, as objectType.event, without
	// parameters (an event = sub makes none).
	bool callable () const { return kind == SYMBOL_SUB || (kind == SYMBOL_EVENT && detail.empty ()); }
};

// The code of a sub or an event, from its { to its }, and its own names.
struct ScriptScope {
	std::string name;		// Of the sub, or "event other"
	std::string owner;		// The objectType, or empty
	TextPos start, end;
	std::vector<ScriptSymbol> locals;	// Its parameters and local variables
	bool contains (TextPos at) const { return start < at && at <= end; }
};

// Where in a file a place is, for what can be typed there.
enum ScriptPlace {
	PLACE_TOP,				// Outside every sub and objectType
	PLACE_OBJECT_TYPE,		// In an objectType, outside its subs and events
	PLACE_CODE,				// In the code of a sub or an event
};

class ScriptIndex : public ScriptNames {
public:
	ScriptIndex ();

	// Read the text of a file again when its stamp (as the version of an
	// open document) isn't what it was. Returns whether it read it.
	bool update (const std::string & file, const TextBuffer & text, uint64_t stamp);
	// The same from the file on disk, which we read again when its time or
	// size change. Returns false when it can't be read.
	bool updateFromDisk (const std::string & file);
	void forget (const std::string & file);
	bool has (const std::string & file) const;
	std::vector<std::string> files () const;

	// The same name for a file, however it was written: with / and without
	// ./ and dir/../.
	static std::string key (const std::string & path);

	// Goes up when the names that the lexer colours change.
	uint64_t version () const { return namesVersion; }
	TokenKind kindOf (const std::string & name) const;

	const std::vector<ScriptSymbol> & symbolsOf (const std::string & file) const;
	const std::vector<ScriptScope> & scopesOf (const std::string & file) const;
	// Every symbol of every file, but the locals.
	std::vector<const ScriptSymbol *> allSymbols () const;
	// Those of a name: globals, and members of any objectType.
	std::vector<const ScriptSymbol *> find (const std::string & name) const;
	const ScriptSymbol * objectType (const std::string & name) const;
	// The members and events of an objectType.
	std::vector<const ScriptSymbol *> membersOf (const std::string & objectType) const;
	// The code that a place is in, or NULL.
	const ScriptScope * scopeAt (const std::string & file, TextPos at) const;
	// The objectType that a place is in, or empty.
	std::string ownerAt (const std::string & file, TextPos at) const;
	ScriptPlace placeAt (const std::string & file, TextPos at) const;

private:
	struct FileIndex {
		uint64_t stamp;
		int64_t diskTime, diskSize;		// When it came from disk, else -1
		std::vector<ScriptSymbol> symbols;
		std::vector<ScriptScope> scopes;
		// The objectTypes' bodies, from { to }.
		std::vector<std::pair<TextPos, TextPos> > objectBodies;
		FileIndex () : stamp (0), diskTime (-1), diskSize (-1) {}
	};

	void read (FileIndex & index, const std::string & file, const TextBuffer & text);
	void updateNames ();

	std::map<std::string, FileIndex> fileIndex;
	std::map<std::string, TokenKind> names;
	uint64_t namesVersion;
};

} // namespace devkit

#endif
