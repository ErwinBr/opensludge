#include <sys/stat.h>

#include <algorithm>

#include "Document.h"
#include "ScriptDocument.h"
#include "ScriptIndex.h"

namespace devkit {

std::string ScriptSymbol::declaration () const {
	std::string full = owner.empty () ? name : owner + "." + name;
	switch (kind) {
		case SYMBOL_SUB: {
			std::string s = std::string ("sub ") + (unfreezable ? "unfreezable " : "") + full + " (";
			for (size_t p = 0; p < parameters.size (); p ++) s += (p ? ", " : "") + parameters[p];
			return s + ")";
		}
		case SYMBOL_OBJECT_TYPE: return "objectType " + name + " (\"" + detail + "\")";
		case SYMBOL_VARIABLE: return "var " + full + (detail.empty () ? std::string () : " = " + detail);
		case SYMBOL_EVENT: return "event " + name + " of " + owner + (detail.empty () ? std::string () : " = " + detail);
		case SYMBOL_FLAG: return "flag " + name;
		case SYMBOL_DEFINITION: return name + " = " + detail;
		case SYMBOL_LOCAL: return "var " + name + (detail.empty () ? std::string () : " = " + detail);
		case SYMBOL_PARAMETER: return name;
	}
	return name;
}

namespace {

// A token of a file, other than spaces and comments.
struct Tok {
	TokenKind kind;
	std::string text;
	TextPos at, end;
};

// What a line has, for the comments above a declaration.
enum LineKind { LINE_BLANK, LINE_COMMENT, LINE_CODE };

bool isNameToken (const Tok & t) {
	if (t.kind == TOKEN_KEYWORD || t.text.empty ()) return false;
	unsigned char c = (unsigned char) t.text[0];
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80;
}

// Reads the tokens of a script into an index.
class Reader {
public:
	Reader (const std::vector<Tok> & tokens, const std::vector<LineKind> & lineKinds, const std::vector<std::string> & comments,
			const std::string & file, std::vector<ScriptSymbol> & symbols, std::vector<ScriptScope> & scopes,
			std::vector<std::pair<TextPos, TextPos> > & bodies)
		: t (tokens), kinds (lineKinds), comments (comments), file (file), symbols (symbols), scopes (scopes), bodies (bodies), i (0) {}

	void readAll () {
		while (i < t.size ()) {
			const std::string & word = t[i].text;
			if (word == "sub") readSub (std::string ());
			else if (word == "objectType") readObjectType ();
			else if (word == "var") readVariables (SYMBOL_VARIABLE, std::string (), NULL);
			else if (word == "flag" || word == "flags") readFlags (std::string ());
			else if (word == "{") i = matching (i) + 1;
			else i ++;
		}
	}

private:
	bool is (const char * text) const { return i < t.size () && t[i].text == text; }

	// The } of a {, the ) of a (: or the end.
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

	// To after the next ; or to a } that ends what we are in, over blocks.
	void skipStatement (size_t limit) {
		while (i < limit) {
			const std::string & word = t[i].text;
			if (word == ";") {
				i ++;
				return;
			}
			if (word == "}") return;
			if (word == "{" || word == "(" || word == "[") i = matching (i) + 1;
			else i ++;
		}
	}

	std::string docAbove (int line) const {
		std::vector<std::string> lines;
		for (int l = line - 1; l >= 0 && kinds[l] == LINE_COMMENT; l --) lines.push_back (comments[l]);
		std::string doc;
		for (size_t k = lines.size (); k -- > 0; ) doc += lines[k] + (k ? "\n" : "");
		return doc;
	}

	ScriptSymbol symbol (SymbolKind kind, const Tok & name, const std::string & owner, int docLine) const {
		ScriptSymbol s;
		s.kind = kind;
		s.name = name.text;
		s.owner = owner;
		s.file = file;
		s.at = name.at;
		if (docLine >= 0) s.doc = docAbove (docLine);
		return s;
	}

	// The text of tokens, as it would read, for a value.
	std::string textOf (size_t from, size_t to) const {
		std::string text;
		for (size_t k = from; k < to && k < t.size (); k ++) {
			if (! text.empty () && t[k].at != t[k - 1].end) text += ' ';
			text += t[k].text;
			if (text.size () > 60) return text.substr (0, 60) + "...";
		}
		return text;
	}

	// var a = 1, b; as globals, members or locals.
	void readVariables (SymbolKind kind, const std::string & owner, std::vector<ScriptSymbol> * locals) {
		int docLine = t[i].at.line;
		i ++;
		while (i < t.size ()) {
			if (! isNameToken (t[i])) {
				skipStatement (t.size ());
				return;
			}
			ScriptSymbol s = symbol (kind, t[i], owner, kind == SYMBOL_LOCAL ? -1 : docLine);
			i ++;
			if (is ("=")) {
				// The value, to a , or ; outside brackets.
				size_t from = ++ i;
				while (i < t.size () && ! is (",") && ! is (";") && ! is ("}") && ! is (")")) {
					if (is ("(") || is ("[") || is ("{")) i = matching (i) + 1;
					else i ++;
				}
				s.detail = textOf (from, i);
			}
			if (locals) locals->push_back (s);
			else symbols.push_back (s);
			if (is (",")) {
				i ++;
				continue;
			}
			if (is (";")) i ++;
			return;
		}
	}

	void readFlags (const std::string & owner) {
		int docLine = t[i].at.line;
		i ++;
		while (i < t.size () && isNameToken (t[i])) {
			ScriptSymbol s = symbol (SYMBOL_FLAG, t[i], std::string (), docLine);
			s.detail = owner;
			symbols.push_back (s);
			i ++;
			if (! is (",")) break;
			i ++;
		}
		skipStatement (t.size ());
	}

	// The locals of code from { to }.
	void readCode (ScriptScope & scope, size_t open, size_t close) {
		size_t keep = i;
		for (i = open + 1; i < close; ) {
			if (t[i].text == "var") readVariables (SYMBOL_LOCAL, std::string (), &scope.locals);
			else i ++;
		}
		i = keep;
	}

	void readSub (const std::string & owner) {
		int docLine = t[i].at.line;
		i ++;
		bool unfreezable = false;
		while (is ("unfreezable") || is ("debug")) {
			if (is ("unfreezable")) unfreezable = true;
			i ++;
		}
		if (i >= t.size () || ! isNameToken (t[i])) {
			skipStatement (t.size ());
			return;
		}
		ScriptSymbol s = symbol (SYMBOL_SUB, t[i], owner, docLine);
		s.unfreezable = unfreezable;
		i ++;
		ScriptScope scope;
		scope.name = s.name;
		scope.owner = owner;
		if (is ("(")) {
			size_t close = matching (i);
			for (size_t k = i + 1; k < close; k ++) {
				if (! isNameToken (t[k])) continue;
				s.parameters.push_back (t[k].text);
				ScriptSymbol p = symbol (SYMBOL_PARAMETER, t[k], std::string (), -1);
				p.detail = s.name;
				scope.locals.push_back (p);
			}
			i = close + 1;
		}
		symbols.push_back (s);
		if (is ("{")) {
			size_t close = matching (i);
			scope.start = t[i].at;
			scope.end = close < t.size () ? t[close].at : t.back ().end;
			readCode (scope, i, close);
			scopes.push_back (scope);
			i = close + 1;
		} else {
			skipStatement (t.size ());
		}
	}

	void readEvent (const std::string & owner, size_t limit) {
		int docLine = t[i].at.line;
		i ++;
		if (i >= limit || ! isNameToken (t[i])) {
			skipStatement (limit);
			return;
		}
		ScriptSymbol s = symbol (SYMBOL_EVENT, t[i], owner, docLine);
		i ++;
		if (is ("{")) {
			size_t close = matching (i);
			ScriptScope scope;
			scope.name = "event " + s.name;
			scope.owner = owner;
			scope.start = t[i].at;
			scope.end = close < t.size () ? t[close].at : t.back ().end;
			readCode (scope, i, close);
			scopes.push_back (scope);
			symbols.push_back (s);
			i = close + 1;
			return;
		}
		if (is ("=") && i + 1 < limit && isNameToken (t[i + 1])) s.detail = t[i + 1].text;
		symbols.push_back (s);
		skipStatement (limit);
	}

	void readObjectType () {
		int docLine = t[i].at.line;
		i ++;
		if (i >= t.size () || ! isNameToken (t[i])) {
			skipStatement (t.size ());
			return;
		}
		ScriptSymbol s = symbol (SYMBOL_OBJECT_TYPE, t[i], std::string (), docLine);
		i ++;
		if (is ("(")) {
			size_t close = matching (i);
			// Its name on screen, without the quotes.
			for (size_t k = i + 1; k < close; k ++) {
				if (t[k].kind == TOKEN_STRING || t[k].kind == TOKEN_ESCAPE) s.detail += t[k].kind == TOKEN_ESCAPE ? t[k].text.substr (1) : t[k].text;
			}
			if (! s.detail.empty () && s.detail[0] == '"') s.detail.erase (0, 1);
			if (! s.detail.empty () && s.detail[s.detail.size () - 1] == '"') s.detail.erase (s.detail.size () - 1);
			i = close + 1;
		}
		symbols.push_back (s);
		if (! is ("{")) {
			skipStatement (t.size ());
			return;
		}
		size_t close = matching (i);
		bodies.push_back (std::make_pair (t[i].at, close < t.size () ? t[close].at : t.back ().end));
		i ++;
		while (i < close) {
			const std::string & word = t[i].text;
			if (word == "sub") readSub (s.name);
			else if (word == "var") readVariables (SYMBOL_VARIABLE, s.name, NULL);
			else if (word == "event") readEvent (s.name, close);
			else if (word == "flag" || word == "flags") readFlags (s.name);
			else {
				size_t before = i;
				skipStatement (close);
				if (i == before) i ++;
			}
		}
		i = close + 1;
	}

	const std::vector<Tok> & t;
	const std::vector<LineKind> & kinds;
	const std::vector<std::string> & comments;
	const std::string & file;
	std::vector<ScriptSymbol> & symbols;
	std::vector<ScriptScope> & scopes;
	std::vector<std::pair<TextPos, TextPos> > & bodies;
	size_t i;
};

bool statFile (const std::string & path, int64_t * time, int64_t * size) {
	struct stat info;
	if (stat (path.c_str (), &info) != 0) return false;
	*time = (int64_t) info.st_mtime;
	*size = (int64_t) info.st_size;
	return true;
}

} // namespace

ScriptIndex::ScriptIndex () : namesVersion (1) {}

std::string ScriptIndex::key (const std::string & path) {
	std::string p = path;
	std::replace (p.begin (), p.end (), '\\', '/');
	bool absolute = ! p.empty () && p[0] == '/';
	std::vector<std::string> parts;
	std::string part;
	for (size_t i = 0; i <= p.size (); i ++) {
		if (i == p.size () || p[i] == '/') {
			if (part == "..") {
				if (! parts.empty () && parts.back () != "..") parts.pop_back ();
				else parts.push_back (part);
			} else if (! part.empty () && part != ".") {
				parts.push_back (part);
			}
			part.clear ();
		} else {
			part += p[i];
		}
	}
	std::string k = absolute ? "/" : "";
	for (size_t i = 0; i < parts.size (); i ++) k += (i ? "/" : "") + parts[i];
	return k;
}

void ScriptIndex::read (FileIndex & index, const std::string & file, const TextBuffer & text) {
	index.symbols.clear ();
	index.scopes.clear ();
	index.objectBodies.clear ();
	std::vector<Tok> tokens;
	std::vector<LineKind> kinds (text.lineCount (), LINE_BLANK);
	std::vector<std::string> comments (text.lineCount ());
	bool definitions = extensionOf (file) == "sld";
	for (int l = 0; l < text.lineCount (); l ++) {
		const std::string & line = text.line (l);
		if (definitions) {
			// NAME = value, and # comments.
			size_t start = line.find_first_not_of (" \t");
			if (start == std::string::npos || line[start] == '#') continue;
			size_t equals = line.find ('=');
			if (equals == std::string::npos) continue;
			size_t end = line.find_last_not_of (" \t", equals == 0 ? 0 : equals - 1);
			if (end == std::string::npos || end < start) continue;
			ScriptSymbol s;
			s.kind = SYMBOL_DEFINITION;
			s.name = line.substr (start, end - start + 1);
			s.file = file;
			s.at = TextPos (l, (int) start);
			size_t value = line.find_first_not_of (" \t", equals + 1);
			if (value != std::string::npos) s.detail = line.substr (value, line.find_last_not_of (" \t") - value + 1);
			if (s.name.find_first_of (" \t") == std::string::npos) index.symbols.push_back (s);
			continue;
		}
		std::vector<Token> lineTokens = lexLine (line, NULL);
		for (size_t k = 0; k < lineTokens.size (); k ++) {
			const Token & token = lineTokens[k];
			if (token.kind == TOKEN_SPACE) continue;
			if (token.kind == TOKEN_COMMENT) {
				if (kinds[l] == LINE_BLANK) {
					kinds[l] = LINE_COMMENT;
					std::string c = line.substr (token.start + 1);
					size_t first = c.find_first_not_of (" \t#");
					comments[l] = first == std::string::npos ? std::string () : c.substr (first);
				}
				continue;
			}
			kinds[l] = LINE_CODE;
			Tok t;
			t.kind = token.kind;
			t.text = line.substr (token.start, token.end - token.start);
			t.at = TextPos (l, token.start);
			t.end = TextPos (l, token.end);
			// A string is one token for the reader.
			if ((t.kind == TOKEN_STRING || t.kind == TOKEN_ESCAPE) && ! tokens.empty ()
					&& (tokens.back ().kind == TOKEN_STRING || tokens.back ().kind == TOKEN_ESCAPE) && tokens.back ().end == t.at
					&& ! (tokens.back ().kind == TOKEN_STRING && tokens.back ().text.size () > 1 && tokens.back ().text[tokens.back ().text.size () - 1] == '"' && t.text[0] == '"')) {
				tokens.back ().text += t.text;
				tokens.back ().end = t.end;
				continue;
			}
			tokens.push_back (t);
		}
	}
	if (definitions) return;
	Reader reader (tokens, kinds, comments, file, index.symbols, index.scopes, index.objectBodies);
	reader.readAll ();
}

bool ScriptIndex::update (const std::string & path, const TextBuffer & text, uint64_t stamp) {
	std::string file = key (path);
	std::map<std::string, FileIndex>::iterator found = fileIndex.find (file);
	if (found != fileIndex.end () && found->second.stamp == stamp && found->second.diskTime < 0) return false;
	FileIndex & index = fileIndex[file];
	index.stamp = stamp;
	index.diskTime = index.diskSize = -1;
	read (index, file, text);
	updateNames ();
	return true;
}

bool ScriptIndex::updateFromDisk (const std::string & path) {
	std::string file = key (path);
	int64_t time, size;
	if (! statFile (path, &time, &size)) {
		forget (path);
		return false;
	}
	std::map<std::string, FileIndex>::iterator found = fileIndex.find (file);
	if (found != fileIndex.end () && found->second.diskTime == time && found->second.diskSize == size) return true;
	std::string fileText, error;
	if (! ScriptDocument::readScript (path, &fileText, &error)) {
		forget (path);
		return false;
	}
	TextBuffer text;
	text.setText (fileText);
	FileIndex & index = fileIndex[file];
	index.stamp = 0;
	index.diskTime = time;
	index.diskSize = size;
	read (index, file, text);
	updateNames ();
	return true;
}

void ScriptIndex::forget (const std::string & path) {
	if (fileIndex.erase (key (path))) updateNames ();
}

bool ScriptIndex::has (const std::string & path) const {
	return fileIndex.count (key (path)) != 0;
}

std::vector<std::string> ScriptIndex::files () const {
	std::vector<std::string> list;
	for (std::map<std::string, FileIndex>::const_iterator f = fileIndex.begin (); f != fileIndex.end (); ++ f) list.push_back (f->first);
	return list;
}

void ScriptIndex::updateNames () {
	std::map<std::string, TokenKind> made;
	for (std::map<std::string, FileIndex>::const_iterator f = fileIndex.begin (); f != fileIndex.end (); ++ f) {
		const std::vector<ScriptSymbol> & symbols = f->second.symbols;
		for (size_t s = 0; s < symbols.size (); s ++) {
			TokenKind kind;
			switch (symbols[s].kind) {
				case SYMBOL_SUB: kind = TOKEN_SUB; break;
				case SYMBOL_OBJECT_TYPE: kind = TOKEN_OBJECT_TYPE; break;
				case SYMBOL_VARIABLE: kind = TOKEN_VARIABLE; break;
				case SYMBOL_FLAG: kind = TOKEN_FLAG; break;
				case SYMBOL_DEFINITION: kind = TOKEN_DEFINITION; break;
				default: continue;
			}
			// An objectType wins over the rest of its name, then a sub.
			std::map<std::string, TokenKind>::iterator had = made.find (symbols[s].name);
			if (had != made.end () && (had->second == TOKEN_OBJECT_TYPE || (had->second == TOKEN_SUB && kind != TOKEN_OBJECT_TYPE))) continue;
			made[symbols[s].name] = kind;
		}
	}
	if (made != names) {
		names.swap (made);
		namesVersion ++;
	}
}

TokenKind ScriptIndex::kindOf (const std::string & name) const {
	std::map<std::string, TokenKind>::const_iterator found = names.find (name);
	return found == names.end () ? TOKEN_IDENTIFIER : found->second;
}

const std::vector<ScriptSymbol> & ScriptIndex::symbolsOf (const std::string & path) const {
	static const std::vector<ScriptSymbol> none;
	std::map<std::string, FileIndex>::const_iterator found = fileIndex.find (key (path));
	return found == fileIndex.end () ? none : found->second.symbols;
}

const std::vector<ScriptScope> & ScriptIndex::scopesOf (const std::string & path) const {
	static const std::vector<ScriptScope> none;
	std::map<std::string, FileIndex>::const_iterator found = fileIndex.find (key (path));
	return found == fileIndex.end () ? none : found->second.scopes;
}

std::vector<const ScriptSymbol *> ScriptIndex::allSymbols () const {
	std::vector<const ScriptSymbol *> list;
	for (std::map<std::string, FileIndex>::const_iterator f = fileIndex.begin (); f != fileIndex.end (); ++ f) {
		for (size_t s = 0; s < f->second.symbols.size (); s ++) list.push_back (&f->second.symbols[s]);
	}
	return list;
}

std::vector<const ScriptSymbol *> ScriptIndex::find (const std::string & name) const {
	std::vector<const ScriptSymbol *> list;
	for (std::map<std::string, FileIndex>::const_iterator f = fileIndex.begin (); f != fileIndex.end (); ++ f) {
		for (size_t s = 0; s < f->second.symbols.size (); s ++) {
			if (f->second.symbols[s].name == name) list.push_back (&f->second.symbols[s]);
		}
	}
	return list;
}

const ScriptSymbol * ScriptIndex::objectType (const std::string & name) const {
	std::vector<const ScriptSymbol *> list = find (name);
	for (size_t i = 0; i < list.size (); i ++) {
		if (list[i]->kind == SYMBOL_OBJECT_TYPE) return list[i];
	}
	return NULL;
}

std::vector<const ScriptSymbol *> ScriptIndex::membersOf (const std::string & owner) const {
	std::vector<const ScriptSymbol *> list;
	for (std::map<std::string, FileIndex>::const_iterator f = fileIndex.begin (); f != fileIndex.end (); ++ f) {
		for (size_t s = 0; s < f->second.symbols.size (); s ++) {
			const ScriptSymbol & symbol = f->second.symbols[s];
			if (symbol.owner == owner && symbol.kind != SYMBOL_FLAG) list.push_back (&symbol);
		}
	}
	return list;
}

const ScriptScope * ScriptIndex::scopeAt (const std::string & path, TextPos at) const {
	const std::vector<ScriptScope> & scopes = scopesOf (path);
	for (size_t s = 0; s < scopes.size (); s ++) {
		if (scopes[s].contains (at)) return &scopes[s];
	}
	return NULL;
}

std::string ScriptIndex::ownerAt (const std::string & path, TextPos at) const {
	if (const ScriptScope * scope = scopeAt (path, at)) return scope->owner;
	std::map<std::string, FileIndex>::const_iterator found = fileIndex.find (key (path));
	if (found == fileIndex.end ()) return std::string ();
	const FileIndex & index = found->second;
	for (size_t b = 0; b < index.objectBodies.size (); b ++) {
		if (index.objectBodies[b].first < at && at <= index.objectBodies[b].second) {
			// The objectType whose name is just before its body.
			const ScriptSymbol * best = NULL;
			for (size_t s = 0; s < index.symbols.size (); s ++) {
				const ScriptSymbol & symbol = index.symbols[s];
				if (symbol.kind == SYMBOL_OBJECT_TYPE && symbol.at < index.objectBodies[b].first && (! best || best->at < symbol.at)) best = &symbol;
			}
			return best ? best->name : std::string ();
		}
	}
	return std::string ();
}

ScriptPlace ScriptIndex::placeAt (const std::string & path, TextPos at) const {
	if (scopeAt (path, at)) return PLACE_CODE;
	std::map<std::string, FileIndex>::const_iterator found = fileIndex.find (key (path));
	if (found == fileIndex.end ()) return PLACE_TOP;
	const FileIndex & index = found->second;
	for (size_t b = 0; b < index.objectBodies.size (); b ++) {
		if (index.objectBodies[b].first < at && at <= index.objectBodies[b].second) return PLACE_OBJECT_TYPE;
	}
	return PLACE_TOP;
}

} // namespace devkit
