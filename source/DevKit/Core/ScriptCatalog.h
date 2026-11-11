#ifndef SLUDGE_DEVKIT_SCRIPT_CATALOG_H
#define SLUDGE_DEVKIT_SCRIPT_CATALOG_H

/*
 * What SLUDGE knows before any script: the built-in functions (those of
 * CommonCode/functionlist.h), the names that the compiler knows (those of
 * CommonCode/constantlist.h, which are numbers or other names of
 * functions), the keywords, and the elements of an objectType.
 */

#include <string>
#include <vector>

namespace devkit {

struct BuiltinFunction {
	std::string name;
};

struct BuiltinConstant {
	std::string name;
	std::string value;		// A number, or the name of a function
	// Another name of a built-in function, such as setStatusColor.
	bool isFunctionName () const;
};

class ScriptCatalog {
public:
	static const ScriptCatalog & get ();

	const std::vector<BuiltinFunction> & functions () const { return functionList; }
	const std::vector<BuiltinConstant> & constants () const { return constantList; }

	// NULL when there is none of the name.
	const BuiltinFunction * function (const std::string & name) const;
	const BuiltinConstant * constant (const std::string & name) const;

	// The words of the language, such as sub and objectType.
	static bool isKeyword (const std::string & word);
	// The elements of an objectType, such as speechColour and walkSpeed.
	static bool isElement (const std::string & word);
	static const std::vector<std::string> & keywords ();
	static const std::vector<std::string> & elements ();

private:
	ScriptCatalog ();

	std::vector<BuiltinFunction> functionList;		// Sorted by name
	std::vector<BuiltinConstant> constantList;		// Sorted by name
};

} // namespace devkit

#endif
