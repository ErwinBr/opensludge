#include <algorithm>
#include <map>

#include "ScriptCatalog.h"

namespace devkit {

static std::string trimmed (const std::string & text) {
	size_t start = text.find_first_not_of (" \t");
	if (start == std::string::npos) return std::string ();
	return text.substr (start, text.find_last_not_of (" \t") - start + 1);
}

Signature Signature::parse (const std::string & text) {
	Signature s;
	s.text = text;
	size_t open = text.find ('('), close = text.rfind (')');
	if (open == std::string::npos || close == std::string::npos || close < open) return s;
	std::string inside = text.substr (open + 1, close - open - 1);
	// The parameters, at the commas outside brackets.
	int depth = 0;
	std::string part;
	for (size_t i = 0; i <= inside.size (); i ++) {
		char c = i < inside.size () ? inside[i] : ',';
		if (c == '[' || c == '(') depth ++;
		if (c == ']' || c == ')') depth --;
		if (c == ',' && depth <= 0) {
			std::string p = trimmed (part);
			if (! p.empty ()) s.parameters.push_back (p);
			part.clear ();
		} else {
			part += c;
		}
	}
	// A parameter counts unless it is all in [ ]; each [ adds one that may
	// be there; ... any number.
	bool any = inside.find ("...") != std::string::npos;
	for (size_t p = 0; p < s.parameters.size (); p ++) {
		const std::string & parameter = s.parameters[p];
		bool optional = parameter[0] == '[';
		if (! optional) s.minArguments ++;
		s.maxArguments += (optional ? 0 : 1) + (int) std::count (parameter.begin (), parameter.end (), '[');
	}
	if (any) s.maxArguments = -1;
	return s;
}

bool BuiltinFunction::accepts (int arguments) const {
	if (signatures.empty ()) return true;
	for (size_t s = 0; s < signatures.size (); s ++) {
		if (signatures[s].accepts (arguments)) return true;
	}
	return false;
}

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

// A name in one spelling, so that setCharacterColourise finds the help of
// setCharacterColorise.
static std::string spelling (const std::string & name) {
	std::string s = name;
	for (size_t i = 0; i < s.size (); i ++) {
		if (s[i] >= 'A' && s[i] <= 'Z') s[i] = (char) (s[i] - 'A' + 'a');
	}
	size_t at;
	while ((at = s.find ("colour")) != std::string::npos) s.replace (at, 6, "color");
	while ((at = s.find ("ise")) != std::string::npos) s.replace (at, 3, "ize");
	return s;
}

struct FunctionDoc {
	const char * name, * page, * syntax, * purpose, * returns;
};

struct ConstantDoc {
	const char * name, * group, * about;
};

static const FunctionDoc functionDocs[] = {
#define FUNCTION_DOC(name, page, syntax, purpose, returns) { name, page, syntax, purpose, returns },
#define CONSTANT_DOC(name, group, about)
#include "ScriptCatalogData.inc"
#undef FUNCTION_DOC
#undef CONSTANT_DOC
};

static const ConstantDoc constantDocs[] = {
#define FUNCTION_DOC(name, page, syntax, purpose, returns)
#define CONSTANT_DOC(name, group, about) { name, group, about },
#include "ScriptCatalogData.inc"
#undef FUNCTION_DOC
#undef CONSTANT_DOC
};

ScriptCatalog::ScriptCatalog () {
#define FUNC(s, n) { BuiltinFunction f; f.name = #n; functionList.push_back (f); }
#include "functionlist.h"
#undef FUNC
#define CONSTANT(n, v) { BuiltinConstant c; c.name = #n; c.value = v; constantList.push_back (c); }
#include "constantlist.h"
#undef CONSTANT
	std::sort (functionList.begin (), functionList.end (), byName<BuiltinFunction>);
	std::sort (constantList.begin (), constantList.end (), byName<BuiltinConstant>);

	// By name, and else by the name in one spelling.
	std::map<std::string, const FunctionDoc *> docs, spelt;
	for (size_t d = 0; d < sizeof (functionDocs) / sizeof (functionDocs[0]); d ++) {
		docs[functionDocs[d].name] = &functionDocs[d];
		spelt[spelling (functionDocs[d].name)] = &functionDocs[d];
	}
	for (size_t f = 0; f < functionList.size (); f ++) {
		BuiltinFunction & function = functionList[f];
		const FunctionDoc * doc = NULL;
		std::map<std::string, const FunctionDoc *>::const_iterator d = docs.find (function.name);
		if (d != docs.end ()) doc = d->second;
		else if ((d = spelt.find (spelling (function.name))) != spelt.end ()) doc = d->second;
		if (! doc) continue;
		function.page = doc->page;
		function.purpose = doc->purpose;
		function.returns = doc->returns;
		std::string syntax = doc->syntax;
		size_t start = 0;
		while (start <= syntax.size ()) {
			size_t end = syntax.find ('\n', start);
			if (end == std::string::npos) end = syntax.size ();
			Signature s = Signature::parse (syntax.substr (start, end - start));
			if (! s.text.empty ()) function.signatures.push_back (s);
			start = end + 1;
		}
	}
	for (size_t d = 0; d < sizeof (constantDocs) / sizeof (constantDocs[0]); d ++) {
		BuiltinConstant * c = const_cast<BuiltinConstant *> (constant (constantDocs[d].name));
		if (! c) continue;
		c->group = constantDocs[d].group;
		c->about = constantDocs[d].about;
	}
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

const BuiltinFunction * ScriptCatalog::functionCalled (const std::string & name) const {
	if (const BuiltinFunction * f = function (name)) return f;
	const BuiltinConstant * c = constant (name);
	return c && c->isFunctionName () ? function (c->value) : NULL;
}

static const KeywordDoc keywordDocs[] = {
	{ "sub", "sub name (parameter, ...) { ... }", "A function: code that runs when it is called, with its parameters as local variables. \"sub unfreezable name\" can't be paused with freeze." },
	{ "var", "var name = value;", "A variable: global outside the subs, a member variable in an objectType, and local in a sub." },
	{ "objectType", "objectType name (\"Its name on screen\") { ... }", "A thing of the game: a character, a screen region, an item or an action, with its events and member variables and subs." },
	{ "if", "if (condition) { ... } else { ... }", "Runs code when the condition is true (not 0)." },
	{ "else", "if (condition) { ... } else { ... }", "The code of an if that runs when the condition is false." },
	{ "return", "return value;", "Leaves the sub, giving back a value." },
	{ "loop", "loop { ... }", "Runs the code again and again, until a return leaves the sub." },
	{ "while", "while (condition) { ... }", "Runs the code again for as long as the condition is true." },
	{ "for", "for (var i = 0; i < 10; i ++) { ... }", "Sets up, then runs the code while the condition is true, with the step after each time." },
	{ "unfreezable", "sub unfreezable name () { ... }", "A sub that freeze doesn't pause." },
	{ "debug", "sub debug name () { ... }", "A sub whose steps the engine writes to its debug file." },
	{ "flag", "flag NAME;", "Gives the objectType a flag, which hasFlag checks. A game has 16 flags at most." },
	{ "flags", "flags NAME, OTHER;", "Gives the objectType flags, which hasFlag checks. A game has 16 flags at most." },
	{ "event", "event otherObject { ... }  or  event otherObject = sub;", "What happens when callEvent combines this objectType with another." },
	{ "speechColour", "speechColour red, green, blue;", "The colour of what the objectType says." },
	{ "speechColor", "speechColor red, green, blue;", "The colour of what the objectType says." },
	{ "speechGap", "speechGap 8;", "The distance in pixels between the objectType and its speech. 8 when not set." },
	{ "walkSpeed", "walkSpeed 5;", "How fast the objectType walks as a character. 5 when not set." },
	{ "spinSpeed", "spinSpeed 45;", "How many degrees a character turns in a frame; 0, the default, works it out from its costume." },
	{ "wrapSpeech", "wrapSpeech 30;", "How many characters of speech fit on a line. 30 when not set." },
	{ "wrapSpeechPixels", "wrapSpeechPixels 300;", "How wide a line of speech is, in pixels." },
	{ "antiAlias", "antiAlias true, blurX, blurY;", "Whether the character is drawn anti-aliased, and how much." },
};

const KeywordDoc * ScriptCatalog::keywordDoc (const std::string & word) {
	for (size_t i = 0; i < sizeof (keywordDocs) / sizeof (keywordDocs[0]); i ++) {
		if (keywordDocs[i].word == word) return &keywordDocs[i];
	}
	return NULL;
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
