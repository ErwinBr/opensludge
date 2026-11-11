#ifndef SLUDGE_DEVKIT_SCRIPT_CATALOG_H
#define SLUDGE_DEVKIT_SCRIPT_CATALOG_H

/*
 * What SLUDGE knows before any script: the built-in functions (those of
 * CommonCode/functionlist.h), the names that the compiler knows (those of
 * CommonCode/constantlist.h, which are numbers or other names of
 * functions), the keywords, and the elements of an objectType; with their
 * docs from the help (ScriptCatalogData.inc), for the script editor.
 */

#include <string>
#include <vector>

namespace devkit {

// One form of a call, such as "anim (spriteBank, frame [, frame...])".
struct Signature {
	std::string text;
	// The parameters, split at the commas outside [ ], such as
	// "spriteBank" and "frame [, frame...]".
	std::vector<std::string> parameters;
	int minArguments;
	int maxArguments;		// -1 for any number

	Signature () : minArguments (0), maxArguments (0) {}
	// Parse "name (a, b [, c...])".
	static Signature parse (const std::string & text);
	bool accepts (int arguments) const {
		return arguments >= minArguments && (maxArguments < 0 || arguments <= maxArguments);
	}
};

struct BuiltinFunction {
	std::string name;
	std::string page;					// Of the help, such as "anim.html", or empty
	std::vector<Signature> signatures;	// Empty when the help has none
	// How many arguments the engine takes (CommonCode/functionparams.h),
	// or -1 for any number.
	int arguments;
	std::string purpose;				// Paragraphs between blank lines
	std::string returns;
	// A function of the old engines that is no longer there (_rem_...).
	bool removed () const { return name.compare (0, 5, "_rem_") == 0; }
	BuiltinFunction () : arguments (-1) {}
	// Whether the engine takes a call with a number of arguments.
	bool accepts (int count) const { return arguments < 0 || count == arguments; }
};

struct BuiltinConstant {
	std::string name;
	std::string value;		// A number, or the name of a function
	std::string group;		// Its heading in the help, such as "Directions"
	std::string about;		// What the help says of the group
	// Another name of a built-in function, such as setStatusColor.
	bool isFunctionName () const;
};

struct KeywordDoc {
	std::string word, syntax, about;
};

class ScriptCatalog {
public:
	static const ScriptCatalog & get ();

	const std::vector<BuiltinFunction> & functions () const { return functionList; }
	const std::vector<BuiltinConstant> & constants () const { return constantList; }

	// NULL when there is none of the name.
	const BuiltinFunction * function (const std::string & name) const;
	const BuiltinConstant * constant (const std::string & name) const;
	// The function that a name calls, also through another name of it
	// (setStatusColor calls setStatusColour), or NULL.
	const BuiltinFunction * functionCalled (const std::string & name) const;

	// The words of the language, such as sub and objectType.
	static bool isKeyword (const std::string & word);
	// The elements of an objectType, such as speechColour and walkSpeed.
	static bool isElement (const std::string & word);
	static const std::vector<std::string> & keywords ();
	static const std::vector<std::string> & elements ();
	// What a keyword or an element does, or NULL.
	static const KeywordDoc * keywordDoc (const std::string & word);

private:
	ScriptCatalog ();

	std::vector<BuiltinFunction> functionList;		// Sorted by name
	std::vector<BuiltinConstant> constantList;		// Sorted by name
};

} // namespace devkit

#endif
