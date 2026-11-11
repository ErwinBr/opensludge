#ifndef SLUDGE_DEVKIT_SCRIPT_VIEW_H
#define SLUDGE_DEVKIT_SCRIPT_VIEW_H

/*
 * The script editor: the text of a script in a monospaced font with its
 * colours, line numbers, the selection, finding and replacing, and going
 * to a line. What the keys do to the text is in Core/TextEditing.h; this
 * draws it, and turns the mouse and the keys into those changes.
 */

#include <stdint.h>

#include <string>
#include <vector>

#include "DocumentView.h"
#include "ScriptDocument.h"
#include "ScriptLexer.h"
#include "TextEditing.h"

struct ImFont;
struct ImVec2;

namespace devkit {

class WorkspaceActions;

class ScriptView : public DocumentView {
public:
	ScriptView (ScriptDocument * script, WorkspaceActions & actions);

	Document * document () { return script; }
	void draw ();
	void drawInspector ();
	bool animating () const;
	bool canCopy () const { return ! script->selection ().empty (); }
	void copy ();
	bool canPaste () const { return true; }
	void pasteClipboard ();
	void drawEditMenu ();

	// Put the cursor at the start of a line (from 1), in the middle of the
	// view, and give the editor the keyboard.
	void goToLine (int line);

	// The monospaced font of every script editor, or NULL for the font of
	// the interface.
	static void setFont (ImFont * font);

private:
	struct LineTokens {
		uint64_t stamp;
		std::vector<Token> tokens;
		LineTokens () : stamp (0) {}
	};

	void drawFindBar ();
	void drawGoToLine ();
	void drawText ();
	void drawStatus ();
	void handleKeys ();
	void handleMouse (const ImVec2 & origin, bool hovered);
	void checkDisk ();

	const std::vector<Token> & tokensOf (int line);
	// The place of the text nearest a point of the screen, from the origin
	// of the text.
	TextPos placeAt (float x, float y) const;
	float xOf (TextPos at) const;
	// Move the cursor, and keep the anchor when selecting.
	void moveTo (TextPos at, bool select, bool keepColumn = false);
	void moveVertically (int lines, bool select);
	TextPos lineStart (TextPos at) const;
	void openFind (bool replace);
	void findAgain (bool backwards);
	void updateMatches ();
	void say (const std::string & text);

	ScriptDocument * script;
	WorkspaceActions & actions;
	std::vector<LineTokens> lineTokens;

	// The layout of the last frame, in points.
	float charWidth, lineHeight, gutterWidth, viewHeight;
	int widestColumns;			// Of the longest line
	uint64_t widestVersion;		// The version of the text that widestColumns is of

	bool wantFocus;				// Give the editor the keyboard in the next frame
	bool scrollToCursor;
	bool centreCursor;			// And put it in the middle of the view
	int wantColumn;				// Where up and down keep the cursor, or -1
	bool dragging;
	int dragUnit;				// 1 characters, 2 words, 3 lines
	TextSelection dragStart;	// What the click selected

	// Finding.
	bool findOpen, replaceOpen, focusFind;
	std::string findText, replaceText;
	editing::FindOptions findOptions;
	std::vector<TextSelection> matches;
	uint64_t matchesVersion;
	std::string matchesOf;
	bool matchesCase, matchesWord;

	bool goToOpen;
	int goToLineNumber;

	// When the file changed on disk while we had changes.
	bool diskChanged;
	double nextDiskCheck;

	std::string notice;
	double noticeUntil;
};

} // namespace devkit

#endif
