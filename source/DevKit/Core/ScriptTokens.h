#ifndef SLUDGE_DEVKIT_SCRIPT_TOKENS_H
#define SLUDGE_DEVKIT_SCRIPT_TOKENS_H

/*
 * The tokens of a whole file, for the readers that look further than a
 * line: the index, the checks and the room editor. The lexer works a line
 * at a time; these put the lines' tokens one after another, with where
 * each is in the file, so that a call may go over several lines.
 */

#include <string>
#include <vector>

#include "ScriptLexer.h"
#include "TextBuffer.h"

namespace devkit {

// A token of a file, other than spaces and comments.
struct FileToken {
	TokenKind kind;
	std::string text;
	TextPos at, end;
};

// Add a token of a line to the tokens of its file. A string, with its
// escapes, is one token.
void appendToken (std::vector<FileToken> & tokens, const std::string & line, int lineNumber, const Token & token);

// The tokens of a file, without spaces, comments and errors.
std::vector<FileToken> fileTokens (const TextBuffer & text);

// The } of a {, the ) of a (, the ] of a [: or tokens.size ().
size_t matchingToken (const std::vector<FileToken> & tokens, size_t open);

// The arguments of a call whose ( is at open and ) at close: the first
// token of each and the one after its last, split at the commas outside
// brackets. No arguments is an empty list.
struct TokenRange {
	size_t from, to;
	TokenRange () : from (0), to (0) {}
	TokenRange (size_t f, size_t t) : from (f), to (t) {}
	bool empty () const { return from >= to; }
};
std::vector<TokenRange> splitArguments (const std::vector<FileToken> & tokens, size_t open, size_t close);

} // namespace devkit

#endif
