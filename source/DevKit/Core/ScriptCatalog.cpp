#include <algorithm>

#include "ScriptCatalog.h"

namespace devkit {

bool BuiltinConstant::isFunctionName () const {
	return ! value.empty () && ((value[0] >= 'a' && value[0] <= 'z') || (value[0] >= 'A' && value[0] <= 'Z'));
}

template <typename T>
static bool byName (const T & a, const T & b) {
	return a.name < b.name;
}

template <typename T>
static const T * findByName (const std::vector<T> & list, const std::string & name) {
	T wanted;
	wanted.name = name;
	typename std::vector<T>::const_iterator i = std::lower_bound (list.begin (), list.end (), wanted, byName<T>);
	return i != list.end () && i->name == name ? &*i : NULL;
}

ScriptCatalog::ScriptCatalog () {
#define FUNC(s, n) { BuiltinFunction f; f.name = #n; functionList.push_back (f); }
#include "functionlist.h"
#undef FUNC
#define CONSTANT(n, v) { BuiltinConstant c; c.name = #n; c.value = v; constantList.push_back (c); }
#include "constantlist.h"
#undef CONSTANT
	std::sort (functionList.begin (), functionList.end (), byName<BuiltinFunction>);
	std::sort (constantList.begin (), constantList.end (), byName<BuiltinConstant>);
}

const ScriptCatalog & ScriptCatalog::get () {
	static ScriptCatalog catalog;
	return catalog;
}

const BuiltinFunction * ScriptCatalog::function (const std::string & name) const {
	return findByName (functionList, name);
}

const BuiltinConstant * ScriptCatalog::constant (const std::string & name) const {
	return findByName (constantList, name);
}

const std::vector<std::string> & ScriptCatalog::keywords () {
	// Those of Compiler/tokens.cpp, and event of an objectType.
	static const char * const words[] = {
		"sub", "var", "objectType", "if", "else", "return", "loop", "while", "for",
		"unfreezable", "debug", "flag", "flags", "event",
	};
	static const std::vector<std::string> list (words, words + sizeof (words) / sizeof (words[0]));
	return list;
}

const std::vector<std::string> & ScriptCatalog::elements () {
	// Those of Compiler/objtype.cpp.
	static const char * const words[] = {
		"speechColour", "speechColor", "speechGap", "walkSpeed", "spinSpeed",
		"wrapSpeech", "wrapSpeechPixels", "antiAlias",
	};
	static const std::vector<std::string> list (words, words + sizeof (words) / sizeof (words[0]));
	return list;
}

bool ScriptCatalog::isKeyword (const std::string & word) {
	const std::vector<std::string> & list = keywords ();
	return std::find (list.begin (), list.end (), word) != list.end ();
}

bool ScriptCatalog::isElement (const std::string & word) {
	const std::vector<std::string> & list = elements ();
	return std::find (list.begin (), list.end (), word) != list.end ();
}

} // namespace devkit
