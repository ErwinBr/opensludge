#include "ScriptTokens.h"

namespace devkit {

void appendToken (std::vector<FileToken> & tokens, const std::string & line, int lineNumber, const Token & token) {
	FileToken t;
	t.kind = token.kind;
	t.text = line.substr (token.start, token.end - token.start);
	t.at = TextPos (lineNumber, token.start);
	t.end = TextPos (lineNumber, token.end);
	if ((t.kind == TOKEN_STRING || t.kind == TOKEN_ESCAPE) && ! tokens.empty ()) {
		const FileToken & last = tokens.back ();
		// Two strings that touch, "a""b", stay two.
		bool closed = last.kind == TOKEN_STRING && last.text.size () > 1 && last.text[last.text.size () - 1] == '"' && t.text[0] == '"';
		if ((last.kind == TOKEN_STRING || last.kind == TOKEN_ESCAPE) && last.end == t.at && ! closed) {
			tokens.back ().text += t.text;
			tokens.back ().end = t.end;
			return;
		}
	}
	tokens.push_back (t);
}

std::vector<FileToken> fileTokens (const TextBuffer & text) {
	std::vector<FileToken> tokens;
	for (int l = 0; l < text.lineCount (); l ++) {
		const std::string & line = text.line (l);
		std::vector<Token> lineTokens = lexLine (line, NULL);
		for (size_t k = 0; k < lineTokens.size (); k ++) {
			TokenKind kind = lineTokens[k].kind;
			if (kind == TOKEN_SPACE || kind == TOKEN_COMMENT || kind == TOKEN_ERROR) continue;
			appendToken (tokens, line, l, lineTokens[k]);
		}
	}
	return tokens;
}

size_t matchingToken (const std::vector<FileToken> & tokens, size_t open) {
	const std::string & o = tokens[open].text;
	std::string c = o == "{" ? "}" : o == "(" ? ")" : "]";
	int depth = 0;
	for (size_t k = open; k < tokens.size (); k ++) {
		if (tokens[k].kind != TOKEN_BRACKET) continue;
		if (tokens[k].text == o) depth ++;
		else if (tokens[k].text == c && -- depth == 0) return k;
	}
	return tokens.size ();
}

std::vector<TokenRange> splitArguments (const std::vector<FileToken> & tokens, size_t open, size_t close) {
	std::vector<TokenRange> arguments;
	if (close == open + 1) return arguments;
	int depth = 0;
	size_t from = open + 1;
	for (size_t k = open + 1; k < close; k ++) {
		if (tokens[k].kind == TOKEN_BRACKET) {
			char c = tokens[k].text[0];
			if (c == '(' || c == '[' || c == '{') depth ++;
			else depth --;
		} else if (depth == 0 && tokens[k].text == ",") {
			arguments.push_back (TokenRange (from, k));
			from = k + 1;
		}
	}
	arguments.push_back (TokenRange (from, close));
	return arguments;
}

} // namespace devkit
