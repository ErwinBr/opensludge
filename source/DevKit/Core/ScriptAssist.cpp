#include <ctype.h>
#include <stdio.h>

#include <algorithm>
#include <set>

#include "Document.h"
#include "ScriptAssist.h"

namespace devkit {

namespace assist {

static char lowerOf (char c) {
	return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
}

static bool isNameKind (TokenKind kind) {
	switch (kind) {
		case TOKEN_IDENTIFIER: case TOKEN_FUNCTION: case TOKEN_CONSTANT: case TOKEN_SUB: case TOKEN_OBJECT_TYPE:
		case TOKEN_VARIABLE: case TOKEN_DEFINITION: case TOKEN_FLAG: case TOKEN_KEYWORD: case TOKEN_ELEMENT:
			return true;
		default:
			return false;
	}
}

// Where a part of a name starts: its first letter, a capital after a small
// one, and what comes after _ or a digit.
static bool partStart (const std::string & name, size_t i) {
	if (i == 0) return true;
	char c = name[i], before = name[i - 1];
	if (before == '_') return c != '_';
	if (c >= 'A' && c <= 'Z') return ! (before >= 'A' && before <= 'Z');
	if (c >= '0' && c <= '9') return ! (before >= '0' && before <= '9');
	return false;
}

// The letters of typed at the starts of parts of name, or each right after
// the one before.
static bool partsMatch (const std::string & typed, size_t t, const std::string & name, size_t n, bool follows) {
	if (t == typed.size ()) return true;
	for (size_t i = n; i < name.size (); i ++) {
		if (lowerOf (name[i]) != lowerOf (typed[t])) continue;
		if (! ((follows && i == n) || partStart (name, i))) continue;
		if (partsMatch (typed, t + 1, name, i + 1, true)) return true;
	}
	return false;
}

int matchScore (const std::string & typed, const std::string & name) {
	if (typed.empty ()) return 1;
	if (typed.size () > name.size ()) return 0;
	if (name.compare (0, typed.size (), typed) == 0) return 1000;
	bool prefix = true;
	for (size_t i = 0; i < typed.size () && prefix; i ++) prefix = lowerOf (typed[i]) == lowerOf (name[i]);
	if (prefix) return 900;
	if (lowerOf (typed[0]) == lowerOf (name[0]) && partsMatch (typed, 0, name, 0, false)) return 700;
	// The letters in order, the first at the start.
	if (lowerOf (typed[0]) != lowerOf (name[0])) return 0;
	size_t n = 1;
	for (size_t t = 1; t < typed.size (); t ++) {
		while (n < name.size () && lowerOf (name[n]) != lowerOf (typed[t])) n ++;
		if (n == name.size ()) return 0;
		n ++;
	}
	return 300;
}

// The name at or before a place of a line, and the objectType before its
// dot, as ego in ego.walk.
struct NameAt {
	bool found;
	std::string name, qualifier;
	int start, end;
	NameAt () : found (false), start (0), end (0) {}
};

static NameAt nameAt (const std::string & line, int column, bool toCursor) {
	NameAt at;
	std::vector<Token> tokens = lexLine (line, NULL);
	int t = -1;
	for (size_t k = 0; k < tokens.size (); k ++) {
		if (! isNameKind (tokens[k].kind)) continue;
		if (column >= tokens[k].start && column <= tokens[k].end && (column > tokens[k].start || ! toCursor)) {
			// At the end of one and the start of the next, the one before.
			if (t < 0 || column < tokens[k].end) t = (int) k;
		}
	}
	if (t < 0) return at;
	at.found = true;
	at.start = tokens[t].start;
	at.end = toCursor ? column : tokens[t].end;
	at.name = line.substr (at.start, at.end - at.start);
	if (t >= 2 && line[tokens[t - 1].start] == '.' && tokens[t - 1].end - tokens[t - 1].start == 1 && isNameKind (tokens[t - 2].kind)) {
		at.qualifier = line.substr (tokens[t - 2].start, tokens[t - 2].end - tokens[t - 2].start);
	}
	return at;
}

static bool inStringOrComment (const std::string & line, int column) {
	std::vector<Token> tokens = lexLine (line, NULL);
	for (size_t t = 0; t < tokens.size (); t ++) {
		const Token & k = tokens[t];
		if (k.start >= column) break;
		if (k.kind == TOKEN_COMMENT || k.kind == TOKEN_ERROR) return true;
		if ((k.kind == TOKEN_STRING || k.kind == TOKEN_ESCAPE || k.kind == TOKEN_FILE) && column < k.end) return true;
	}
	return false;
}

static TokenKind tokenOfSymbol (SymbolKind kind) {
	switch (kind) {
		case SYMBOL_SUB: return TOKEN_SUB;
		case SYMBOL_OBJECT_TYPE: return TOKEN_OBJECT_TYPE;
		case SYMBOL_VARIABLE: return TOKEN_VARIABLE;
		case SYMBOL_FLAG: return TOKEN_FLAG;
		case SYMBOL_DEFINITION: return TOKEN_DEFINITION;
		default: return TOKEN_IDENTIFIER;
	}
}

// The first paragraph of some docs.
static std::string firstParagraph (const std::string & text) {
	size_t end = text.find ("\n\n");
	return end == std::string::npos ? text : text.substr (0, end);
}

// What a name is, at a place.
struct Resolved {
	enum What { NONE, LOCAL, PROJECT, FUNCTION, CONSTANT, KEYWORD } what;
	ScriptSymbol symbol;					// LOCAL and PROJECT
	const BuiltinFunction * function;
	const BuiltinConstant * constant;
	const KeywordDoc * keyword;
	Resolved () : what (NONE), function (NULL), constant (NULL), keyword (NULL) {}
};

static int globalRank (SymbolKind kind) {
	switch (kind) {
		case SYMBOL_OBJECT_TYPE: return 0;
		case SYMBOL_SUB: return 1;
		case SYMBOL_VARIABLE: return 2;
		case SYMBOL_DEFINITION: return 3;
		case SYMBOL_FLAG: return 4;
		default: return 9;
	}
}

static Resolved resolve (const std::string & name, const std::string & qualifier, const ScriptIndex & index, const std::string & file, TextPos at) {
	Resolved r;
	if (! qualifier.empty ()) {
		std::vector<const ScriptSymbol *> members = index.membersOf (qualifier);
		for (size_t m = 0; m < members.size (); m ++) {
			// Events with code too, which the compiler makes subs of.
			if (members[m]->name == name && (members[m]->kind != SYMBOL_EVENT || members[m]->callable ())) {
				r.what = Resolved::PROJECT;
				r.symbol = *members[m];
				return r;
			}
		}
		return r;
	}
	const ScriptScope * scope = index.scopeAt (file, at);
	if (scope) {
		for (size_t l = 0; l < scope->locals.size (); l ++) {
			if (scope->locals[l].name == name) {
				r.what = Resolved::LOCAL;
				r.symbol = scope->locals[l];
				r.symbol.file = ScriptIndex::key (file);
				return r;
			}
		}
	}
	std::string owner = scope ? scope->owner : index.ownerAt (file, at);
	std::vector<const ScriptSymbol *> found = index.find (name);
	const ScriptSymbol * best = NULL;
	for (size_t f = 0; f < found.size (); f ++) {
		const ScriptSymbol * s = found[f];
		if (s->kind == SYMBOL_EVENT) continue;
		bool member = ! s->owner.empty ();
		if (member && s->owner != owner) continue;
		// A member of the objectType that we are in hides a global.
		if (! best || (member && best->owner.empty ()) || (member == ! best->owner.empty () && globalRank (s->kind) < globalRank (best->kind))) best = s;
	}
	if (best) {
		r.what = Resolved::PROJECT;
		r.symbol = *best;
		return r;
	}
	const ScriptCatalog & catalog = ScriptCatalog::get ();
	if ((r.function = catalog.functionCalled (name)) != NULL) {
		r.what = Resolved::FUNCTION;
		return r;
	}
	if ((r.constant = catalog.constant (name)) != NULL) {
		r.what = Resolved::CONSTANT;
		return r;
	}
	if ((r.keyword = ScriptCatalog::keywordDoc (name)) != NULL) r.what = Resolved::KEYWORD;
	return r;
}

static std::string whereOf (const ScriptSymbol & s) {
	char line[32];
	snprintf (line, sizeof (line), ", line %d", s.at.line + 1);
	return fileNameOf (s.file) + line;
}

static void addItem (std::vector<Completion> & items, std::set<std::string> & seen, const std::string & typed,
		const std::string & label, TokenKind kind, const std::string & detail, const std::string & doc, int bonus) {
	if (seen.count (label)) return;
	int score = matchScore (typed, label);
	if (! score) return;
	seen.insert (label);
	Completion c;
	c.label = label;
	c.kind = kind;
	c.detail = detail;
	c.doc = doc;
	c.score = score + bonus;
	items.push_back (c);
}

static bool byScore (const Completion & a, const Completion & b) {
	if (a.score != b.score) return a.score > b.score;
	if (a.label.size () != b.label.size ()) return a.label.size () < b.label.size ();
	return a.label < b.label;
}

static void addKeyword (std::vector<Completion> & items, std::set<std::string> & seen, const std::string & typed, const std::string & word, int bonus) {
	const KeywordDoc * doc = ScriptCatalog::keywordDoc (word);
	addItem (items, seen, typed, word, ScriptCatalog::isElement (word) ? TOKEN_ELEMENT : TOKEN_KEYWORD,
		doc ? doc->syntax : std::string (), doc ? doc->about : std::string (), bonus);
}

CompletionList complete (const TextBuffer & text, TextPos at, const ScriptIndex & index, const std::string & file, bool everything) {
	CompletionList list;
	list.from = at;
	if (at.line < 0 || at.line >= text.lineCount ()) return list;
	const std::string & line = text.line (at.line);
	if (inStringOrComment (line, at.column)) return list;
	NameAt word = nameAt (line, at.column, true);
	std::string qualifier;
	int start = at.column;
	if (word.found) {
		start = word.start;
		list.typed = word.name;
		qualifier = word.qualifier;
	} else if (at.column > 0 && line[at.column - 1] == '.') {
		// Just after a dot: the members of what is before it.
		NameAt before = nameAt (line, at.column - 1, true);
		if (before.found) qualifier = before.name;
	}
	list.from = TextPos (at.line, start);
	// Names start with a letter; a number isn't one.
	if (! list.typed.empty () && isdigit ((unsigned char) list.typed[0])) return list;
	if (! everything && list.typed.empty () && qualifier.empty ()) return list;

	// The word before, which may make a new name, where we have none.
	std::string before;
	{
		int k = start - 1;
		while (k >= 0 && (line[k] == ' ' || line[k] == '\t')) k --;
		int end = k + 1;
		while (k >= 0 && charClassOf ((unsigned char) line[k]) == CHAR_WORD) k --;
		before = line.substr (k + 1, end - k - 1);
	}
	if (before == "sub" || before == "objectType" || before == "var" || before == "flag" || before == "flags"
			|| before == "unfreezable" || before == "debug") return list;

	std::vector<Completion> & items = list.items;
	std::set<std::string> seen;
	const std::string & typed = list.typed;

	if (! qualifier.empty ()) {
		std::vector<const ScriptSymbol *> members = index.membersOf (qualifier);
		for (size_t m = 0; m < members.size (); m ++) {
			const ScriptSymbol & s = *members[m];
			if (s.kind == SYMBOL_EVENT && ! s.callable ()) continue;
			addItem (items, seen, typed, s.name, s.kind == SYMBOL_EVENT ? TOKEN_SUB : tokenOfSymbol (s.kind), s.declaration (), s.doc, 50);
		}
		std::sort (items.begin (), items.end (), byScore);
		return list;
	}
	std::vector<const ScriptSymbol *> all = index.allSymbols ();
	if (before == "event") {
		for (size_t s = 0; s < all.size (); s ++) {
			if (all[s]->kind == SYMBOL_OBJECT_TYPE) addItem (items, seen, typed, all[s]->name, TOKEN_OBJECT_TYPE, all[s]->declaration (), all[s]->doc, 0);
		}
		std::sort (items.begin (), items.end (), byScore);
		return list;
	}

	ScriptPlace place = index.placeAt (file, at);
	if (place == PLACE_TOP) {
		const char * words[] = { "sub", "objectType", "var", "flag", "flags" };
		for (size_t w = 0; w < sizeof (words) / sizeof (words[0]); w ++) addKeyword (items, seen, typed, words[w], 0);
		std::sort (items.begin (), items.end (), byScore);
		return list;
	}
	if (place == PLACE_OBJECT_TYPE) {
		const char * words[] = { "sub", "var", "event", "flag", "flags" };
		for (size_t w = 0; w < sizeof (words) / sizeof (words[0]); w ++) addKeyword (items, seen, typed, words[w], 10);
		const std::vector<std::string> & elements = ScriptCatalog::elements ();
		for (size_t e = 0; e < elements.size (); e ++) addKeyword (items, seen, typed, elements[e], 0);
		std::sort (items.begin (), items.end (), byScore);
		return list;
	}

	// In code: its locals, the members of its objectType, the project's
	// names, the built-ins and the words of code.
	const ScriptScope * scope = index.scopeAt (file, at);
	if (scope) {
		for (size_t l = 0; l < scope->locals.size (); l ++) {
			const ScriptSymbol & s = scope->locals[l];
			// Not the word that we are typing, as a local is being named.
			if (s.at.line == at.line && s.at.column == start) continue;
			addItem (items, seen, typed, s.name, TOKEN_IDENTIFIER, s.kind == SYMBOL_PARAMETER ? "parameter of " + scope->name : s.declaration (), std::string (), 60);
		}
		if (! scope->owner.empty ()) {
			std::vector<const ScriptSymbol *> members = index.membersOf (scope->owner);
			for (size_t m = 0; m < members.size (); m ++) {
				if (members[m]->kind == SYMBOL_EVENT) continue;
				addItem (items, seen, typed, members[m]->name, tokenOfSymbol (members[m]->kind), members[m]->declaration (), members[m]->doc, 50);
			}
		}
	}
	for (size_t s = 0; s < all.size (); s ++) {
		const ScriptSymbol & symbol = *all[s];
		if (! symbol.owner.empty () || symbol.kind == SYMBOL_EVENT) continue;
		addItem (items, seen, typed, symbol.name, tokenOfSymbol (symbol.kind), symbol.declaration (), symbol.doc, 40);
	}
	const char * words[] = { "if", "else", "while", "for", "loop", "return", "var" };
	for (size_t w = 0; w < sizeof (words) / sizeof (words[0]); w ++) addKeyword (items, seen, typed, words[w], 30);
	const ScriptCatalog & catalog = ScriptCatalog::get ();
	for (size_t f = 0; f < catalog.functions ().size (); f ++) {
		const BuiltinFunction & function = catalog.functions ()[f];
		if (function.removed ()) continue;
		std::string detail = function.signatures.empty () ? function.name + " (...)" : function.signatures[0].text;
		addItem (items, seen, typed, function.name, TOKEN_FUNCTION, detail, firstParagraph (function.purpose), 0);
	}
	for (size_t c = 0; c < catalog.constants ().size (); c ++) {
		const BuiltinConstant & constant = catalog.constants ()[c];
		if (constant.isFunctionName ()) {
			const BuiltinFunction * f = catalog.function (constant.value);
			addItem (items, seen, typed, constant.name, TOKEN_FUNCTION, "Another name of " + constant.value,
				f ? firstParagraph (f->purpose) : std::string (), -5);
		} else {
			addItem (items, seen, typed, constant.name, TOKEN_CONSTANT, "= " + constant.value, constant.about, 0);
		}
	}
	std::sort (items.begin (), items.end (), byScore);
	if (items.size () > 200) items.resize (200);
	return list;
}

bool signatureAt (const TextBuffer & text, TextPos at, const ScriptIndex & index, const std::string & file, SignatureHelp * help) {
	if (at.line < 0 || at.line >= text.lineCount ()) return false;
	if (inStringOrComment (text.line (at.line), at.column)) return false;
	// Back from the cursor to the ( of the call that we are in, counting
	// the commas of its arguments.
	int depth = 0, commas = 0;
	for (int l = at.line; l >= 0 && at.line - l < 30; l --) {
		const std::string & line = text.line (l);
		std::vector<Token> tokens = lexLine (line, NULL);
		for (size_t k = tokens.size (); k -- > 0; ) {
			const Token & token = tokens[k];
			if (l == at.line && token.start >= at.column) continue;
			if (token.kind != TOKEN_BRACKET && ! (token.kind == TOKEN_PUNCTUATION && (line[token.start] == ',' || line[token.start] == ';'))) continue;
			char c = line[token.start];
			if (c == ')' || c == ']') depth ++;
			else if (c == '[') {
				if (depth == 0) return false;
				depth --;
			} else if (c == '(') {
				if (depth > 0) {
					depth --;
					continue;
				}
				// The name before the (.
				int column = token.start;
				while (column > 0 && (line[column - 1] == ' ' || line[column - 1] == '\t')) column --;
				NameAt name = nameAt (line, column, true);
				if (! name.found || column == 0) return false;
				if (ScriptCatalog::isKeyword (name.name)) return false;
				Resolved r = resolve (name.name, name.qualifier, index, file, TextPos (l, name.start));
				help->name = name.qualifier.empty () ? name.name : name.qualifier + "." + name.name;
				help->argument = commas;
				help->signatures.clear ();
				help->doc.clear ();
				if (r.what == Resolved::PROJECT && r.symbol.callable ()) {
					std::string form = help->name + " (";
					for (size_t p = 0; p < r.symbol.parameters.size (); p ++) form += (p ? ", " : "") + r.symbol.parameters[p];
					help->signatures.push_back (Signature::parse (form + ")"));
					help->doc = r.symbol.doc;
				} else if (r.what == Resolved::FUNCTION) {
					help->signatures = r.function->signatures;
					help->doc = firstParagraph (r.function->purpose);
					if (help->signatures.empty ()) help->signatures.push_back (Signature::parse (r.function->name + " (...)"));
				} else {
					return false;
				}
				help->active = (int) help->signatures.size () - 1;
				for (size_t s = 0; s < help->signatures.size (); s ++) {
					const Signature & signature = help->signatures[s];
					if (signature.maxArguments < 0 || signature.maxArguments > commas) {
						help->active = (int) s;
						break;
					}
				}
				return true;
			} else if (c == '{' || c == '}' || c == ';') {
				if (depth == 0) return false;
			} else if (c == ',' && depth == 0) {
				commas ++;
			}
		}
	}
	return false;
}

bool hoverAt (const TextBuffer & text, TextPos at, const ScriptIndex & index, const std::string & file, HoverInfo * info) {
	if (at.line < 0 || at.line >= text.lineCount ()) return false;
	const std::string & line = text.line (at.line);
	if (inStringOrComment (line, at.column + 1)) return false;
	NameAt name = nameAt (line, at.column, false);
	if (! name.found) return false;
	Resolved r = resolve (name.name, name.qualifier, index, file, at);
	info->range = TextSelection (TextPos (at.line, name.start), TextPos (at.line, name.end));
	info->where.clear ();
	info->page.clear ();
	info->body.clear ();
	switch (r.what) {
		case Resolved::LOCAL:
			info->title = r.symbol.declaration ();
			info->body = r.symbol.kind == SYMBOL_PARAMETER ? "A parameter of " + r.symbol.detail + "." : "A local variable.";
			return true;
		case Resolved::PROJECT:
			info->title = r.symbol.declaration ();
			info->body = r.symbol.doc;
			info->where = whereOf (r.symbol);
			return true;
		case Resolved::FUNCTION: {
			const BuiltinFunction & f = *r.function;
			for (size_t s = 0; s < f.signatures.size (); s ++) info->title += (s ? "\n" : "") + f.signatures[s].text;
			if (info->title.empty ()) info->title = f.name + " (...)";
			if (f.name != name.name) info->body = name.name + " is another name of " + f.name + ".\n\n";
			info->body += firstParagraph (f.purpose);
			if (! f.returns.empty ()) info->body += (info->body.empty () ? "" : "\n\n") + std::string ("Returns: ") + firstParagraph (f.returns);
			info->page = f.page;
			return true;
		}
		case Resolved::CONSTANT:
			info->title = r.constant->name + " = " + r.constant->value;
			info->body = r.constant->group.empty () ? r.constant->about : r.constant->group + ": " + r.constant->about;
			info->page = "Built-in_Constants.html";
			return true;
		case Resolved::KEYWORD:
			info->title = r.keyword->syntax;
			info->body = r.keyword->about;
			return true;
		default:
			return false;
	}
}

bool definitionAt (const TextBuffer & text, TextPos at, const ScriptIndex & index, const std::string & file, Definition * definition) {
	if (at.line < 0 || at.line >= text.lineCount ()) return false;
	const std::string & line = text.line (at.line);
	NameAt name = nameAt (line, at.column, false);
	if (! name.found) return false;
	Resolved r = resolve (name.name, name.qualifier, index, file, at);
	if (r.what != Resolved::LOCAL && r.what != Resolved::PROJECT) return false;
	definition->file = r.symbol.file;
	definition->at = r.symbol.at;
	return true;
}

} // namespace assist

} // namespace devkit
