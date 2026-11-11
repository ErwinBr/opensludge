#ifndef SLUDGE_DEVKIT_SCRIPT_LEXER_H
#define SLUDGE_DEVKIT_SCRIPT_LEXER_H

/*
 * The words of a line of a script, for its colours and for finding what is
 * at a place. A line stands alone: a comment (#) ends at the end of the
 * line, and so must a "string" or a 'file name' (preproc.cpp).
 *
 * The lexer knows the keywords, the elements of objectTypes and the
 * built-ins; ScriptNames can tell it what the other names of a project
 * are, such as its subs and objectTypes.
 */

#include <string>
#include <vector>

namespace devkit {

enum TokenKind {
	TOKEN_SPACE,
	TOKEN_COMMENT,
	TOKEN_STRING,			// "text", with its quotes
	TOKEN_ESCAPE,			// \" or \\ in a string
	TOKEN_FILE,				// 'file name', with its quotes
	TOKEN_NUMBER,
	TOKEN_KEYWORD,			// sub, if, objectType...
	TOKEN_ELEMENT,			// speechColour, walkSpeed... of an objectType
	TOKEN_OPERATOR,			// + - * / % = ! < > & | and their pairs
	TOKEN_BRACKET,			// ( ) [ ] { }
	TOKEN_PUNCTUATION,		// ; , . and the rest
	TOKEN_IDENTIFIER,		// A name that we know nothing of
	TOKEN_FUNCTION,			// A built-in function
	TOKEN_CONSTANT,			// A name that the compiler knows, such as TRUE
	TOKEN_SUB,				// A sub of the project
	TOKEN_OBJECT_TYPE,		// An objectType of the project
	TOKEN_VARIABLE,			// A global or member variable of the project
	TOKEN_DEFINITION,		// A name of a definition file (.sld)
	TOKEN_FLAG,				// A flag of the objectTypes
	TOKEN_ERROR,			// A string with no end, or a character that doesn't belong
	TOKEN_KIND_COUNT
};

struct Token {
	TokenKind kind;
	int start, end;			// Bytes of the line
	Token () : kind (TOKEN_SPACE), start (0), end (0) {}
	Token (TokenKind k, int s, int e) : kind (k), start (s), end (e) {}
};

// The names of a project, beyond the built-ins.
class ScriptNames {
public:
	virtual ~ScriptNames () {}
	// TOKEN_SUB, TOKEN_OBJECT_TYPE..., or TOKEN_IDENTIFIER when the name is
	// none of the project's.
	virtual TokenKind kindOf (const std::string & name) const = 0;
};

// The tokens of a line, in order, which cover all of it. names may be NULL.
std::vector<Token> lexLine (const std::string & line, const ScriptNames * names);

// The kind of a name, from the built-ins and then names.
TokenKind kindOfName (const std::string & name, const ScriptNames * names);

// The token at a byte of a line: the one that covers it, or the one that
// ends there when the byte is after it. -1 when there is none.
int tokenAt (const std::vector<Token> & tokens, int column);

} // namespace devkit

#endif
