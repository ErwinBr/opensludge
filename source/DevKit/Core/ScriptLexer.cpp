#include <string.h>

#include "ScriptCatalog.h"
#include "ScriptLexer.h"

namespace devkit {

static bool isNameStart (unsigned char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80;
}

static bool isNameChar (unsigned char c) {
	return isNameStart (c) || (c >= '0' && c <= '9');
}

static bool isDigit (unsigned char c) {
	return c >= '0' && c <= '9';
}

TokenKind kindOfName (const std::string & name, const ScriptNames * names) {
	if (ScriptCatalog::isKeyword (name)) return TOKEN_KEYWORD;
	if (ScriptCatalog::isElement (name)) return TOKEN_ELEMENT;
	// A project's names come before the built-ins, which they may hide.
	if (names) {
		TokenKind kind = names->kindOf (name);
		if (kind != TOKEN_IDENTIFIER) return kind;
	}
	const ScriptCatalog & catalog = ScriptCatalog::get ();
	if (catalog.function (name)) return TOKEN_FUNCTION;
	if (const BuiltinConstant * c = catalog.constant (name)) return c->isFunctionName () ? TOKEN_FUNCTION : TOKEN_CONSTANT;
	return TOKEN_IDENTIFIER;
}

std::vector<Token> lexLine (const std::string & line, const ScriptNames * names) {
	std::vector<Token> tokens;
	int n = (int) line.size ();
	int i = 0;
	while (i < n) {
		unsigned char c = (unsigned char) line[i];
		int start = i;
		if (c == ' ' || c == '\t' || c == '\r') {
			while (i < n && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) i ++;
			tokens.push_back (Token (TOKEN_SPACE, start, i));
		} else if (c == '#') {
			tokens.push_back (Token (TOKEN_COMMENT, start, n));
			i = n;
		} else if (c == '"') {
			// A string, with \" and \\ in it, which must end on its line.
			i ++;
			int run = start;
			bool ended = false;
			while (i < n) {
				if (line[i] == '\\' && i + 1 < n) {
					if (i > run) tokens.push_back (Token (TOKEN_STRING, run, i));
					tokens.push_back (Token (TOKEN_ESCAPE, i, i + 2));
					i += 2;
					run = i;
					continue;
				}
				if (line[i] == '"') {
					i ++;
					ended = true;
					break;
				}
				i ++;
			}
			if (i > run) tokens.push_back (Token (ended ? TOKEN_STRING : TOKEN_ERROR, run, i));
			// A string with no end is wrong from its start.
			if (! ended) {
				for (size_t t = tokens.size (); t -- > 0 && tokens[t].start >= start; ) tokens[t].kind = TOKEN_ERROR;
			}
		} else if (c == '\'') {
			// A file name has no escapes.
			i ++;
			while (i < n && line[i] != '\'') i ++;
			bool ended = i < n;
			if (ended) i ++;
			tokens.push_back (Token (ended ? TOKEN_FILE : TOKEN_ERROR, start, i));
		} else if (isDigit (c)) {
			while (i < n && isNameChar ((unsigned char) line[i])) i ++;
			tokens.push_back (Token (TOKEN_NUMBER, start, i));
		} else if (isNameStart (c)) {
			while (i < n && isNameChar ((unsigned char) line[i])) i ++;
			tokens.push_back (Token (kindOfName (line.substr (start, i - start), names), start, i));
		} else if (c < 0x20 || c == 0x7F) {
			// A control character, which strchr would find at the end of
			// its strings when it is 0.
			i ++;
			tokens.push_back (Token (TOKEN_ERROR, start, i));
		} else if (strchr ("()[]{}", c)) {
			i ++;
			tokens.push_back (Token (TOKEN_BRACKET, start, i));
		} else if (strchr ("+-*/%=!<>&|", c)) {
			i ++;
			// The pairs of preproc.cpp: && == || != <= >= ++ -- += -= *= /= %=
			if (i < n) {
				char d = line[i];
				if ((c == '&' && d == '&') || (c == '|' && d == '|') || (c == '+' && d == '+') || (c == '-' && d == '-')
						|| (d == '=' && strchr ("=!<>+-*/%", c))) i ++;
			}
			tokens.push_back (Token (TOKEN_OPERATOR, start, i));
		} else if (strchr (";,.:?~^@$`\\", c)) {
			i ++;
			tokens.push_back (Token (TOKEN_PUNCTUATION, start, i));
		} else {
			i ++;
			tokens.push_back (Token (TOKEN_ERROR, start, i));
		}
	}
	return tokens;
}

int tokenAt (const std::vector<Token> & tokens, int column) {
	for (size_t t = 0; t < tokens.size (); t ++) {
		if (column >= tokens[t].start && column < tokens[t].end) return (int) t;
	}
	if (! tokens.empty () && tokens.back ().end == column) return (int) tokens.size () - 1;
	return -1;
}

} // namespace devkit
