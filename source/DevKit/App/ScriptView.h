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

#include "CompileJob.h"
#include "DocumentView.h"
#include "ScriptAnalysis.h"
#include "ScriptAssist.h"
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
	double secondsToWake () const;
	bool canCopy () const { return ! script->selection ().empty (); }
	void copy ();
	bool canPaste () const { return true; }
	void pasteClipboard ();
	void drawEditMenu ();

	// Put the cursor at the start of a line (from 1), or at a place, in the
	// middle of the view, and give the editor the keyboard.
	void goToLine (int line);
	// With a length, the bytes after the place are selected.
	void goTo (TextPos at, int length = 0);
	// The selection, when it is on one line, or empty.
	std::string selectedText () const;

	// The name of a script in the index: its file, or a name of its own
	// while it has none.
	static std::string indexName (const ScriptDocument * script);

	// The font of every script editor, JetBrains Mono (which main.cpp builds
	// in), or NULL for the font of the interface.
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
	void drawCompletion (const ImVec2 & origin);
	void drawSignature (const ImVec2 & origin);
	void drawHover (const ImVec2 & origin, bool hovered);
	void drawOutline ();
	void updateMarkers ();
	// The live checks, again a little after the text or the names change.
	void updateProblems ();
	void drawProblems ();
	// Go to the next problem after the cursor, or the one before it.
	void nextProblem (bool backwards);
	// The line that has a stamp now, or -1.
	int lineWithStamp (uint64_t stamp) const;
	// Completion: start it, keep it up to date as the user types, and put
	// in the chosen name.
	void startCompletion (bool everything);
	void updateCompletion ();
	void acceptCompletion ();
	bool handleCompletionKeys ();
	void goToDefinition (TextPos at);
	// Whether Ctrl+click on the token at a place does something: goes to
	// where it is made, or opens its help page.
	bool isLink (int line, const Token & token);
	void openHelpAt (TextPos at);
	// Make the text a point bigger (1) or smaller (-1), or 0 for the size
	// of the interface.
	void zoom (int step);
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

	// The names of the index that the colours of lineTokens are of.
	uint64_t namesVersion;

	// Completion.
	bool completing, completeEverything;
	CompletionList completion;
	int selectedItem, firstItem;
	float popupX0, popupY0, popupX1, popupY1;	// Where it was drawn, on screen
	uint64_t completedVersion;					// Of the text that it is of

	// Signature help, for a version of the text and a place.
	bool signatureShown, signatureHidden;
	SignatureHelp signature;
	uint64_t signatureVersion;
	TextPos signatureAt;
	float signatureHeight;		// Of its window, the last time

	// The name under the mouse while Ctrl (Cmd on macOS) is held, when a
	// click on it goes somewhere: underlined, with the hand pointer.
	struct Link {
		int line;
		Token token;
		Link () : line (-1) {}
	};
	Link link;
	unsigned int outlineHovered;	// The ImGuiID of the outline's row under the mouse, or 0
	// What isLink answered last, for the same token, text and names.
	int linkLine, linkStart;
	uint64_t linkVersion, linkNames;
	bool linkAnswer;

	// Hovering a name.
	TextPos hoverPlace;
	double hoverSince;
	HoverInfo hover;
	bool hoverFound;
	uint64_t hoverVersion;

	// The messages of the last compile, on the lines as they were then:
	// gone once the line changes.
	struct Marker {
		uint64_t stamp;
		bool error;
		std::string text;
	};
	std::vector<Marker> markers;
	unsigned int markersSerial;

	// What the live checks found, each on its line as it was then.
	struct Problem {
		Diagnostic diagnostic;
		uint64_t stamp;
	};
	std::vector<Problem> problems;
	uint64_t problemsVersion, problemsNames;	// Of the text and of the index
	bool problemsAllNames;
	double problemsDue;		// When to check again, or 0
	int framesDrawn;

	// When the file changed on disk while we had changes.
	bool diskChanged;
	double nextDiskCheck;

	std::string notice;
	double noticeUntil;

	// The cursor blinks, as in InputText: on from when it last moved or the
	// text changed, then off and on again.
	double caretSince;
	TextSelection caretSelection;
	uint64_t caretVersion;
	int caretFrame;		// The last frame that drew it, or -1
};

} // namespace devkit

#endif
