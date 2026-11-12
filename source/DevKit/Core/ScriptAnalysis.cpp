#include <stdio.h>

#include <algorithm>
#include <set>
#include <utility>

#include "Document.h"
#include "ScriptAnalysis.h"
#include "ScriptCatalog.h"
#include "ScriptLexer.h"

namespace devkit {

namespace {

struct Tok {
	TokenKind kind;
	std::string text;
	TextPos at, end;
};

bool isName (const Tok & t) {
	if (t.kind == TOKEN_KEYWORD || t.kind == TOKEN_ELEMENT || t.kind == TOKEN_NUMBER || t.text.empty ()) return false;
	unsigned char c = (unsigned char) t.text[0];
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80;
}

std::vector<Tok> tokensOf (const TextBuffer & text, std::vector<Diagnostic> & out) {
	std::vector<Tok> tokens;
	for (int l = 0; l < text.lineCount (); l ++) {
		const std::string & line = text.line (l);
		std::vector<Token> lineTokens = lexLine (line, NULL);
		for (size_t k = 0; k < lineTokens.size (); k ++) {
			const Token & token = lineTokens[k];
			if (token.kind == TOKEN_SPACE || token.kind == TOKEN_COMMENT) continue;
			if (token.kind == TOKEN_ERROR) {
				// A string with no end is its first ERROR token, to the end.
				char c = line[token.start];
				if (c == '"' || c == '\'') {
					out.push_back (Diagnostic (Diagnostic::ERROR, TextPos (l, token.start), TextPos (l, (int) line.size ()),
						c == '"' ? "This string has no end on its line." : "This file name has no end on its line."));
					break;
				}
				if (k > 0 && lineTokens[k - 1].kind == TOKEN_ERROR) continue;
				if ((unsigned char) c < 0x20 || c == 0x7F) {
					out.push_back (Diagnostic (Diagnostic::ERROR, TextPos (l, token.start), TextPos (l, token.end), "A control character, which doesn't belong in a script."));
				}
				continue;
			}
			Tok t;
			t.kind = token.kind;
			t.text = line.substr (token.start, token.end - token.start);
			t.at = TextPos (l, token.start);
			t.end = TextPos (l, token.end);
			// A string, with its escapes, is one token.
			if ((t.kind == TOKEN_STRING || t.kind == TOKEN_ESCAPE) && ! tokens.empty () && tokens.back ().end == t.at
					&& (tokens.back ().kind == TOKEN_STRING || tokens.back ().kind == TOKEN_ESCAPE)
					&& ! (tokens.back ().text.size () > 1 && tokens.back ().text[tokens.back ().text.size () - 1] == '"'
						&& tokens.back ().kind == TOKEN_STRING && t.text[0] == '"')) {
				tokens.back ().text += t.text;
				tokens.back ().end = t.end;
				continue;
			}
			tokens.push_back (t);
		}
	}
	return tokens;
}

std::string lineText (int line) {
	char text[32];
	snprintf (text, sizeof (text), "line %d", line + 1);
	return text;
}

std::string whereOf (const ScriptSymbol & s, const std::string & file) {
	if (ScriptIndex::key (s.file) == ScriptIndex::key (file)) return lineText (s.at.line);
	return fileNameOf (s.file) + ", " + lineText (s.at.line);
}

class Checker {
public:
	Checker (const std::vector<Tok> & t, const ScriptIndex & index, const std::string & file, bool allNames, std::vector<Diagnostic> & out)
		: t (t), index (index), file (file), allNames (allNames), out (out), catalog (ScriptCatalog::get ()) {}

	void brackets () {
		std::vector<size_t> open;
		for (size_t i = 0; i < t.size (); i ++) {
			if (t[i].kind != TOKEN_BRACKET) continue;
			char c = t[i].text[0];
			if (c == '(' || c == '[' || c == '{') {
				open.push_back (i);
				continue;
			}
			char wanted = c == ')' ? '(' : c == ']' ? '[' : '{';
			// It closes the nearest of its kind; those after that are open.
			size_t match = open.size ();
			for (size_t k = open.size (); k -- > 0; ) {
				if (t[open[k]].text[0] == wanted) {
					match = k;
					break;
				}
			}
			if (match == open.size ()) {
				error (t[i], std::string ("This ") + c + " closes nothing.");
				continue;
			}
			for (size_t k = match + 1; k < open.size (); k ++) unclosed (t[open[k]]);
			open.resize (match);
		}
		for (size_t k = 0; k < open.size (); k ++) unclosed (t[open[k]]);
	}

	// Two statements with no ; between them, which the compiler takes for
	// one: it ends them only at a ; or at the } of a block.
	void semicolons () {
		// The open brackets, and whether each ( is that of a header, as
		// of if (...) or sub name (...), after which a statement follows.
		std::vector<std::pair<char, bool> > open;
		std::vector<bool> headerEnd (t.size (), false);
		for (size_t i = 0; i < t.size (); i ++) {
			const Tok & a = t[i];
			if (a.kind == TOKEN_BRACKET) {
				char c = a.text[0];
				if (c == '(' || c == '[' || c == '{') {
					open.push_back (std::make_pair (c, c == '(' && opensHeader (i)));
				} else {
					// It closes the nearest of its kind, and those after it.
					char wanted = c == ')' ? '(' : c == ']' ? '[' : '{';
					size_t k = open.size ();
					while (k > 0 && open[k - 1].first != wanted) k --;
					if (k > 0) {
						headerEnd[i] = open[k - 1].second;
						open.resize (k - 1);
					}
				}
			}
			if (i + 1 >= t.size ()) break;
			if (! open.empty () && open.back ().first != '{') continue;
			const Tok & b = t[i + 1];
			bool ends = isName (a) || a.kind == TOKEN_NUMBER || a.kind == TOKEN_STRING || a.kind == TOKEN_ESCAPE || a.kind == TOKEN_FILE
				|| ((a.text == ")" || a.text == "]") && ! headerEnd[i]) || a.text == "++" || a.text == "--";
			bool starts = isName (b) || b.kind == TOKEN_KEYWORD || b.kind == TOKEN_ELEMENT;
			if (! ends || ! starts) continue;
			out.push_back (Diagnostic (Diagnostic::WARNING, TextPos (a.end.line, a.end.column - 1), a.end,
				"A ; is missing after this, so the compiler joins it with what comes next."));
		}
	}

	// Whether the ( at i is that of if, while, for or loop, or of the
	// parameters of a sub or objectType.
	bool opensHeader (size_t i) const {
		if (i == 0) return false;
		const Tok & before = t[i - 1];
		if (before.kind == TOKEN_KEYWORD) return before.text == "if" || before.text == "while" || before.text == "for" || before.text == "loop";
		// Back over name, or name.name, to sub, objectType or event.
		size_t j = i - 1;
		while (j > 0 && (isName (t[j]) || t[j].text == ".")) j --;
		if (j == i - 1 && ! isName (t[j])) return false;
		return t[j].kind == TOKEN_KEYWORD && (t[j].text == "sub" || t[j].text == "objectType" || t[j].text == "event");
	}

	void unclosed (const Tok & opener) {
		char closer = opener.text[0] == '(' ? ')' : opener.text[0] == '[' ? ']' : '}';
		error (opener, std::string ("This ") + opener.text + " has no " + closer + ".");
	}

	void code () {
		const std::vector<ScriptScope> & scopes = index.scopesOf (file);
		for (size_t s = 0; s < scopes.size (); s ++) {
			const ScriptScope & scope = scopes[s];
			// Names made twice.
			for (size_t a = 0; a < scope.locals.size (); a ++) {
				for (size_t b = 0; b < a; b ++) {
					if (scope.locals[a].name != scope.locals[b].name) continue;
					diagnose (Diagnostic::ERROR, scope.locals[a].at, scope.locals[a].name, "'" + scope.locals[a].name
						+ "' is made twice in " + scope.name + ", first on " + lineText (scope.locals[b].at.line) + ".");
					break;
				}
			}
			namesIn (scope);
		}
	}

	void declarations () {
		const std::vector<ScriptSymbol> & symbols = index.symbolsOf (file);
		std::vector<const ScriptSymbol *> all = index.allSymbols ();
		for (size_t i = 0; i < symbols.size (); i ++) {
			const ScriptSymbol & s = symbols[i];
			if (s.kind == SYMBOL_EVENT) {
				event (s);
				continue;
			}
			if (s.kind != SYMBOL_SUB && s.kind != SYMBOL_OBJECT_TYPE && s.kind != SYMBOL_VARIABLE) continue;
			// A built-in's name can't be another's.
			if (s.owner.empty ()) {
				if (catalog.function (s.name)) {
					diagnose (Diagnostic::ERROR, s.at, s.name, "'" + s.name + "' is a built-in function of SLUDGE: a project can't make it again.");
					continue;
				}
				if (catalog.constant (s.name)) {
					diagnose (Diagnostic::ERROR, s.at, s.name, "'" + s.name + "' is a name that SLUDGE knows already (" + s.name + " = " + catalog.constant (s.name)->value + ").");
					continue;
				}
			}
			// Made before, in this file or, when we have them all, in others.
			for (size_t o = 0; o < all.size (); o ++) {
				const ScriptSymbol & other = *all[o];
				if (&other == &s || other.name != s.name || other.owner != s.owner) continue;
				if (other.kind != SYMBOL_SUB && other.kind != SYMBOL_OBJECT_TYPE && other.kind != SYMBOL_VARIABLE) continue;
				bool sameFile = ScriptIndex::key (other.file) == ScriptIndex::key (file);
				if (sameFile ? ! (other.at < s.at) : ! allNames) continue;
				if (! sameFile && ScriptIndex::key (other.file) > ScriptIndex::key (file)) continue;
				std::string what = other.kind == SYMBOL_SUB ? "a sub" : other.kind == SYMBOL_OBJECT_TYPE ? "an objectType" : "a variable";
				std::string name = s.owner.empty () ? s.name : s.owner + "." + s.name;
				diagnose (Diagnostic::ERROR, s.at, s.name, "'" + name + "' is " + what + " already, on " + whereOf (other, file) + ".");
				break;
			}
		}
	}

private:
	void error (const Tok & at, const std::string & message) {
		out.push_back (Diagnostic (Diagnostic::ERROR, at.at, at.end, message));
	}

	void diagnose (Diagnostic::Severity severity, TextPos at, const std::string & name, const std::string & message) {
		out.push_back (Diagnostic (severity, at, TextPos (at.line, at.column + (int) name.size ()), message));
	}

	// The } of a {, the ) of a (, or the end.
	size_t matching (size_t open) const {
		const std::string & o = t[open].text;
		std::string c = o == "{" ? "}" : o == "(" ? ")" : "]";
		int depth = 0;
		for (size_t k = open; k < t.size (); k ++) {
			if (t[k].kind != TOKEN_BRACKET) continue;
			if (t[k].text == o) depth ++;
			else if (t[k].text == c && -- depth == 0) return k;
		}
		return t.size ();
	}

	bool isLocal (const ScriptScope & scope, const std::string & name) const {
		for (size_t l = 0; l < scope.locals.size (); l ++) {
			if (scope.locals[l].name == name) return true;
		}
		return false;
	}

	const ScriptSymbol * member (const std::string & owner, const std::string & name) const {
		if (owner.empty ()) return NULL;
		std::vector<const ScriptSymbol *> members = index.membersOf (owner);
		for (size_t m = 0; m < members.size (); m ++) {
			if (members[m]->name == name && members[m]->kind != SYMBOL_EVENT) return members[m];
		}
		return NULL;
	}

	const ScriptSymbol * global (const std::string & name) const {
		std::vector<const ScriptSymbol *> found = index.find (name);
		const ScriptSymbol * best = NULL;
		for (size_t f = 0; f < found.size (); f ++) {
			if (! found[f]->owner.empty () || found[f]->kind == SYMBOL_EVENT) continue;
			// As the linker looks: objectTypes, variables, subs, flags.
			if (! best || found[f]->kind == SYMBOL_OBJECT_TYPE || (found[f]->kind == SYMBOL_VARIABLE && best->kind != SYMBOL_OBJECT_TYPE)) best = found[f];
		}
		return best;
	}

	// The number of arguments of the call whose ( is at open.
	int arguments (size_t open, size_t close) const {
		if (close == open + 1) return 0;
		int depth = 0, commas = 0;
		for (size_t k = open + 1; k < close; k ++) {
			if (t[k].kind == TOKEN_BRACKET) {
				char c = t[k].text[0];
				if (c == '(' || c == '[' || c == '{') depth ++;
				else depth --;
			} else if (depth == 0 && t[k].text == ",") {
				commas ++;
			}
		}
		return commas + 1;
	}

	void callOf (const Tok & name, size_t open, const ScriptScope & scope, const std::string & qualifier) {
		size_t close = matching (open);
		if (close >= t.size ()) return;
		int count = arguments (open, close);
		std::string full = qualifier.empty () ? name.text : qualifier + "." + name.text;
		const ScriptSymbol * sub = NULL;
		if (! qualifier.empty ()) {
			sub = member (qualifier, name.text);
		} else {
			if (isLocal (scope, name.text)) return;
			if (const BuiltinFunction * f = catalog.functionCalled (name.text)) {
				if (! f->accepts (count)) {
					char text[96];
					snprintf (text, sizeof (text), " takes %d argument%s, not %d.", f->arguments, f->arguments == 1 ? "" : "s", count);
					diagnose (Diagnostic::WARNING, name.at, name.text, name.text + text);
				}
				return;
			}
			if (index.objectType (name.text)) return;
			const ScriptSymbol * own = member (scope.owner, name.text);
			if (own && own->kind != SYMBOL_SUB) return;
			sub = own ? own : global (name.text);
		}
		if (! sub || sub->kind != SYMBOL_SUB || (int) sub->parameters.size () == count) return;
		char text[96];
		int wanted = (int) sub->parameters.size ();
		snprintf (text, sizeof (text), " takes %d argument%s, not %d.", wanted, wanted == 1 ? "" : "s", count);
		diagnose (Diagnostic::WARNING, name.at, name.text, full + text);
	}

	void namesIn (const ScriptScope & scope) {
		// The tokens of the code, between its { and }.
		size_t first = 0;
		while (first < t.size () && ! (scope.start < t[first].at)) first ++;
		bool inVar = false, varName = false;
		int varDepth = 0, depth = 0;
		for (size_t i = first; i < t.size () && t[i].at < scope.end; i ++) {
			const Tok & k = t[i];
			if (k.kind == TOKEN_BRACKET) {
				char c = k.text[0];
				if (c == '(' || c == '[' || c == '{') depth ++;
				else {
					depth --;
					if (inVar && depth < varDepth) inVar = false;
				}
				continue;
			}
			if (k.text == "var") {
				inVar = varName = true;
				varDepth = depth;
				continue;
			}
			if (inVar) {
				if (k.text == ";") {
					inVar = false;
					continue;
				}
				if (k.text == "," && depth == varDepth) {
					varName = true;
					continue;
				}
				if (k.text == "=") {
					varName = false;
					continue;
				}
				if (varName && isName (k)) {
					varName = false;
					continue;
				}
			}
			if (! isName (k)) continue;
			// The member after a dot goes with the name before it.
			if (i > 0 && t[i - 1].text == "." && t[i - 1].end == k.at) continue;
			std::string qualifier;
			const Tok * nameToken = &k;
			size_t after = i + 1;
			if (i + 2 < t.size () && t[i + 1].text == "." && t[i + 1].at == k.end && isName (t[i + 2]) && t[i + 2].at == t[i + 1].end) {
				qualifier = k.text;
				nameToken = &t[i + 2];
				after = i + 3;
			}
			bool call = after < t.size () && t[after].text == "(";
			if (! qualifier.empty ()) {
				if (! index.objectType (qualifier)) {
					// Not a member of an objectType: the names stand alone.
					if (allNames && ! knownName (qualifier, scope)) {
						error (k, "'" + qualifier + "' isn't a variable, sub, objectType, flag or built-in.");
					}
					continue;
				}
				if (allNames && ! member (qualifier, nameToken->text)) {
					error (*nameToken, "The objectType " + qualifier + " has no member '" + nameToken->text + "'.");
					continue;
				}
				if (call) callOf (*nameToken, after, scope, qualifier);
				continue;
			}
			if (call) {
				callOf (k, after, scope, std::string ());
			}
			if (allNames && ! knownName (k.text, scope)) {
				error (k, "'" + k.text + "' isn't a variable, sub, objectType, flag or built-in.");
			}
		}
	}

	bool knownName (const std::string & name, const ScriptScope & scope) const {
		if (isLocal (scope, name)) return true;
		if (catalog.function (name) || catalog.constant (name)) return true;
		if (ScriptCatalog::isKeyword (name) || ScriptCatalog::isElement (name)) return true;
		if (member (scope.owner, name)) return true;
		std::vector<const ScriptSymbol *> found = index.find (name);
		for (size_t f = 0; f < found.size (); f ++) {
			const ScriptSymbol & s = *found[f];
			if (s.kind == SYMBOL_EVENT) continue;
			if (s.owner.empty () || s.kind == SYMBOL_FLAG || s.kind == SYMBOL_DEFINITION) return true;
		}
		return false;
	}

	void event (const ScriptSymbol & s) {
		if (! allNames) return;
		if (! index.objectType (s.name)) {
			diagnose (Diagnostic::ERROR, s.at, s.name, "There is no objectType '" + s.name + "' for this event.");
			return;
		}
		if (s.detail.empty ()) return;
		const ScriptSymbol * sub = global (s.detail);
		if (! sub || sub->kind != SYMBOL_SUB) {
			// Where the event names it, after the =.
			for (size_t i = 0; i < t.size (); i ++) {
				if (t[i].at.line == s.at.line && t[i].text == s.detail && s.at < t[i].at) {
					error (t[i], "'" + s.detail + "' isn't a sub, which the event would call.");
					return;
				}
			}
		}
	}

	const std::vector<Tok> & t;
	const ScriptIndex & index;
	const std::string & file;
	bool allNames;
	std::vector<Diagnostic> & out;
	const ScriptCatalog & catalog;
};

bool byPlace (const Diagnostic & a, const Diagnostic & b) {
	return a.start < b.start;
}

} // namespace

std::vector<Diagnostic> analyseScript (const TextBuffer & text, const ScriptIndex & index, const std::string & file, bool allNames) {
	std::vector<Diagnostic> out;
	// A file of definitions has only NAME = value.
	if (extensionOf (file) == "sld") return out;
	std::vector<Tok> tokens = tokensOf (text, out);
	Checker checker (tokens, index, file, allNames, out);
	checker.brackets ();
	checker.semicolons ();
	checker.code ();
	checker.declarations ();
	std::stable_sort (out.begin (), out.end (), byPlace);
	return out;
}

} // namespace devkit
