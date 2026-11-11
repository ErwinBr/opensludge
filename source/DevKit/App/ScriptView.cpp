#include <math.h>
#include <stdio.h>

#include <algorithm>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"

#include "Log.h"
#include "Platform.h"
#include "Properties.h"
#include "ScriptView.h"
#include "WorkspaceActions.h"

namespace devkit {

static const int TAB_SIZE = 4;
// How big the text of the script editors can be, in points.
static const float SMALLEST_SIZE = 8.0f, BIGGEST_SIZE = 40.0f;
// How long a notice stays, in seconds, and how often we look at the file.
static const double NOTICE_SECONDS = 6.0;
static const double DISK_CHECK_SECONDS = 1.0;

static ImFont * codeFont = NULL;

void ScriptView::setFont (ImFont * font) {
	codeFont = font;
}

// The colours of the editor, for a dark or a light interface.
struct ScriptTheme {
	ImU32 token[TOKEN_KIND_COUNT];
	ImU32 background, gutter, lineNumber, currentLineNumber, currentLine;
	ImU32 selection, inactiveSelection, match, bracket, caret, error, warning;
};

// A wavy line under some text.
static void wavyLine (ImDrawList * draw, float x0, float x1, float base, ImU32 colour) {
	for (float x = x0; x < x1; x += 4) {
		draw->AddLine (ImVec2 (x, base), ImVec2 (x + 2 < x1 ? x + 2 : x1, base - 2), colour);
		if (x + 2 < x1) draw->AddLine (ImVec2 (x + 2, base - 2), ImVec2 (x + 4 < x1 ? x + 4 : x1, base), colour);
	}
}

static ImU32 rgb (unsigned int c, int alpha = 255) {
	return IM_COL32 ((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, alpha);
}

static const ScriptTheme & theme () {
	static ScriptTheme dark, light;
	static bool made = false;
	if (! made) {
		made = true;
		// The light ones are the dark ones darker, for at least 4.5:1 on
		// white, and more for the text that most of a script is.
		const unsigned int darkTokens[TOKEN_KIND_COUNT] = {
			0xABC9FF,	// space
			0x7C8EA2,	// comment
			0xFFEDA1,	// string
			0xABC9FF,	// escape
			0x63C259,	// file
			0xA2FFE0,	// number
			0xFF7085,	// keyword
			0x56B0FB,	// element
			0xABC9FF,	// operator
			0xABC9FF,	// bracket
			0xABC9FF,	// punctuation
			0xABC9FF,	// identifier
			0x58B2FF,	// function
			0xFF7085,	// constant
			0x67E5FF,	// sub
			0x4EC9B0,	// object type
			0x56B0FB,	// variable
			0xFF7085,	// definition
			0x43FFC2,	// flag
			0xFF3434,	// error
		};
		const unsigned int lightTokens[TOKEN_KIND_COUNT] = {
			0x24467E,	// space
			0x64778C,	// comment
			0x8A6F00,	// string
			0x24467E,	// escape
			0x3A7F32,	// file
			0x4A7A6C,	// number
			0xC4314B,	// keyword
			0x0A6DC2,	// element
			0x24467E,	// operator
			0x24467E,	// bracket
			0x24467E,	// punctuation
			0x24467E,	// identifier
			0x0A6DC2,	// function
			0xC4314B,	// constant
			0x007A92,	// sub
			0x237A68,	// object type
			0x0A6DC2,	// variable
			0xC4314B,	// definition
			0x00754E,	// flag
			0xD50000,	// error
		};
		for (int k = 0; k < TOKEN_KIND_COUNT; k ++) {
			dark.token[k] = rgb (darkTokens[k]);
			light.token[k] = rgb (lightTokens[k]);
		}
		dark.background = rgb (0x1E1E1E);
		dark.gutter = rgb (0x1E1E1E);
		dark.lineNumber = rgb (0x858585);
		dark.currentLineNumber = rgb (0xC6C6C6);
		dark.currentLine = rgb (0xFFFFFF, 14);
		dark.selection = rgb (0x264F78);
		dark.inactiveSelection = rgb (0x3A3D41);
		dark.match = rgb (0x623315, 200);
		dark.bracket = rgb (0x888888, 70);
		dark.caret = rgb (0xAEAFAD);
		dark.error = rgb (0xFF3434);
		dark.warning = rgb (0xCCA700);
		light.background = rgb (0xFFFFFF);
		light.gutter = rgb (0xFFFFFF);
		light.lineNumber = rgb (0x6E7681);
		light.currentLineNumber = rgb (0x171184);
		light.currentLine = rgb (0x000000, 12);
		light.selection = rgb (0xADD6FF);
		light.inactiveSelection = rgb (0xE5EBF1);
		light.match = rgb (0xF8C9AB);
		light.bracket = rgb (0xB9B9B9, 90);
		light.caret = rgb (0x000000);
		light.error = rgb (0xD50000);
		light.warning = rgb (0xBF8803);
	}
	const ImVec4 & bg = ImGui::GetStyle ().Colors[ImGuiCol_WindowBg];
	float luminance = 0.299f * bg.x + 0.587f * bg.y + 0.114f * bg.z;
	return luminance < 0.5f ? dark : light;
}

ScriptView::ScriptView (ScriptDocument * s, WorkspaceActions & a)
	: script (s), actions (a), charWidth (8), lineHeight (18), gutterWidth (40), viewHeight (0),
	  widestColumns (0), widestVersion (0), wantFocus (true), scrollToCursor (false), centreCursor (false),
	  wantColumn (-1), dragging (false), dragUnit (1),
	  findOpen (false), replaceOpen (false), focusFind (false), matchesVersion (0), matchesCase (false), matchesWord (false),
	  goToOpen (false), goToLineNumber (1), namesVersion (0),
	  completing (false), completeEverything (false), selectedItem (0), firstItem (0),
	  popupX0 (0), popupY0 (0), popupX1 (0), popupY1 (0), completedVersion (0),
	  signatureShown (false), signatureHidden (false), signatureVersion (0), signatureAt (-1, 0), signatureHeight (0),
	  hoverPlace (-1, 0), hoverSince (0), hoverFound (false), hoverVersion (0), markersSerial (0), framesDrawn (0),
	  problemsVersion (0), problemsNames (0), problemsAllNames (false), problemsDue (0),
	  diskChanged (false), nextDiskCheck (0), noticeUntil (0) {}

std::string ScriptView::indexName (const ScriptDocument * script) {
	if (! script->path ().empty ()) return script->path ();
	char name[48];
	snprintf (name, sizeof (name), "Untitled Script %u.slu", script->id ());
	return name;
}

void ScriptView::say (const std::string & text) {
	notice = text;
	noticeUntil = ImGui::GetTime () + NOTICE_SECONDS;
}

bool ScriptView::animating () const {
	// While the mouse rests on a name, until its docs show.
	bool hoverWaits = hoverPlace.line >= 0 && ImGui::GetTime () - hoverSince < 0.6;
	return dragging || hoverWaits || problemsDue > 0;
}

const std::vector<Token> & ScriptView::tokensOf (int line) {
	const TextBuffer & text = script->buffer ();
	const ScriptIndex & index = actions.scriptIndex ();
	// New names of the project colour every line again.
	if (namesVersion != index.version ()) {
		lineTokens.clear ();
		namesVersion = index.version ();
	}
	if ((int) lineTokens.size () != text.lineCount ()) lineTokens.resize (text.lineCount ());
	LineTokens & cached = lineTokens[line];
	if (cached.stamp != text.lineStamp (line)) {
		cached.tokens = lexLine (text.line (line), &index);
		cached.stamp = text.lineStamp (line);
	}
	return cached.tokens;
}

float ScriptView::xOf (TextPos at) const {
	const TextBuffer & text = script->buffer ();
	if (at.line < 0 || at.line >= text.lineCount ()) return 0;
	return displayColumn (text.line (at.line), at.column, TAB_SIZE) * charWidth;
}

TextPos ScriptView::placeAt (float x, float y) const {
	const TextBuffer & text = script->buffer ();
	// Before converting, as the mouse is at -FLT_MAX when it is nowhere.
	if (! (y >= 0)) return TextPos (0, 0);
	if (y >= text.lineCount () * lineHeight) return text.end ();
	int line = (int) floorf (y / lineHeight);
	if (line >= text.lineCount ()) return text.end ();
	const std::string & l = text.line (line);
	// The nearer side of the character under the point.
	int column = 0, i = 0;
	while (i < (int) l.size ()) {
		int next = nextCharStart (l, i);
		int width = l[i] == '\t' ? (column / TAB_SIZE + 1) * TAB_SIZE - column : 1;
		float middle = (column + width * 0.5f) * charWidth;
		if (x < middle) return TextPos (line, i);
		column += width;
		i = next;
	}
	return TextPos (line, (int) l.size ());
}

TextPos ScriptView::lineStart (TextPos at) const {
	// Home goes to the first character that isn't a space, then to the start.
	const std::string & line = script->buffer ().line (at.line);
	int first = (int) editing::indentOf (line).size ();
	return TextPos (at.line, at.column == first ? 0 : first);
}

void ScriptView::moveTo (TextPos at, bool select, bool keepColumn) {
	TextSelection s = script->selection ();
	at = script->buffer ().clamp (at);
	script->setSelection (select ? TextSelection (s.anchor, at) : TextSelection (at));
	if (! keepColumn) wantColumn = -1;
	scrollToCursor = true;
}

void ScriptView::moveVertically (int lines, bool select) {
	const TextBuffer & text = script->buffer ();
	TextPos cursor = script->selection ().cursor;
	if (wantColumn < 0) wantColumn = displayColumn (text.line (cursor.line), cursor.column, TAB_SIZE);
	int line = cursor.line + lines;
	TextPos at;
	if (line < 0) at = text.begin ();
	else if (line >= text.lineCount ()) at = text.end ();
	else at = TextPos (line, byteAtDisplayColumn (text.line (line), wantColumn, TAB_SIZE));
	moveTo (at, select, true);
}

void ScriptView::goToLine (int line) {
	const TextBuffer & text = script->buffer ();
	if (line < 1) line = 1;
	if (line > text.lineCount ()) line = text.lineCount ();
	const std::string & l = text.line (line - 1);
	script->setSelection (TextSelection (TextPos (line - 1, (int) editing::indentOf (l).size ())));
	wantColumn = -1;
	scrollToCursor = centreCursor = true;
	wantFocus = true;
}

std::string ScriptView::selectedText () const {
	TextSelection s = script->selection ();
	if (s.empty () || s.start ().line != s.end ().line) return std::string ();
	return editing::selectedText (*script);
}

void ScriptView::goTo (TextPos at, int length) {
	const TextBuffer & text = script->buffer ();
	TextPos start = text.clamp (at);
	script->setSelection (length > 0 ? TextSelection (start, text.clamp (TextPos (start.line, start.column + length))) : TextSelection (start));
	wantColumn = -1;
	scrollToCursor = centreCursor = true;
	wantFocus = true;
}

void ScriptView::zoom (int step) {
	if (step == 0) {
		actions.setScriptFontSize (0);
	} else {
		float size = actions.scriptFontSize ();
		if (size <= 0) size = ImGui::GetStyle ().FontSizeBase;
		size = floorf (size + 0.5f) + step;
		if (size < SMALLEST_SIZE) size = SMALLEST_SIZE;
		if (size > BIGGEST_SIZE) size = BIGGEST_SIZE;
		// The size of the interface is no size of its own.
		actions.setScriptFontSize (size == ImGui::GetStyle ().FontSizeBase ? 0 : size);
	}
	scrollToCursor = true;
	hoverPlace = TextPos (-1, 0);
}

void ScriptView::copy () {
	std::string text = editing::selectedText (*script);
	if (! text.empty ()) ImGui::SetClipboardText (text.c_str ());
}

void ScriptView::pasteClipboard () {
	const char * text = ImGui::GetClipboardText ();
	if (text && *text) {
		editing::insert (*script, text, "Paste");
		scrollToCursor = true;
	}
	wantFocus = true;
}

#if defined(__APPLE__)
static const char * const CTRL = "Cmd+";
#else
static const char * const CTRL = "Ctrl+";
#endif

static std::string keys (const char * key) {
	return std::string (CTRL) + key;
}

void ScriptView::drawEditMenu () {
	bool osx = ImGui::GetIO ().ConfigMacOSXBehaviors;
	// After a choice, the keyboard goes back to the text.
	bool chose = false;
	if (ImGui::MenuItem ("Cut", keys ("X").c_str (), false, canCopy ())) {
		copy ();
		editing::deleteSelection (*script, "Cut");
		chose = true;
	}
	if (ImGui::MenuItem ("Select All", keys ("A").c_str ())) {
		script->setSelection (TextSelection (script->buffer ().begin (), script->buffer ().end ()));
		chose = true;
	}
	ImGui::Separator ();
	if (ImGui::MenuItem ("Find...", keys ("F").c_str ())) openFind (false);
	if (ImGui::MenuItem ("Replace...", osx ? "Cmd+Alt+F" : "Ctrl+H")) openFind (true);
	if (ImGui::MenuItem ("Find Next", osx ? "Cmd+G" : "F3", false, ! findText.empty ())) {
		findAgain (false);
		chose = true;
	}
	if (ImGui::MenuItem ("Find Previous", osx ? "Cmd+Shift+G" : "Shift+F3", false, ! findText.empty ())) {
		findAgain (true);
		chose = true;
	}
	if (ImGui::MenuItem ("Go to Line...", keys (osx ? "L" : "G").c_str ())) goToOpen = true;
	if (ImGui::MenuItem ("Go to Definition", "F12")) {
		goToDefinition (script->selection ().cursor);
		chose = true;
	}
	if (ImGui::MenuItem ("Show Completions", "Ctrl+Space")) {
		startCompletion (true);
		chose = true;
	}
	if (ImGui::MenuItem ("Help on the Word", "F1")) openHelpAt (script->selection ().cursor);
	if (ImGui::MenuItem ("Next Problem", "F8", false, ! problems.empty ())) {
		nextProblem (false);
		chose = true;
	}
	ImGui::Separator ();
	if (ImGui::MenuItem ("Bigger Text", keys ("+").c_str ())) zoom (1);
	if (ImGui::MenuItem ("Smaller Text", keys ("-").c_str ())) zoom (-1);
	if (ImGui::MenuItem ("Text of the Usual Size", keys ("0").c_str (), false, actions.scriptFontSize () > 0)) zoom (0);
	ImGui::Separator ();
	if (ImGui::MenuItem ("Toggle Comment", keys ("/").c_str ())) {
		editing::toggleComment (*script);
		chose = true;
	}
	if (ImGui::MenuItem ("Indent", "Tab")) {
		editing::indentLines (*script, true);
		chose = true;
	}
	if (ImGui::MenuItem ("Outdent", "Shift+Tab")) {
		editing::indentLines (*script, false);
		chose = true;
	}
	if (ImGui::MenuItem ("Duplicate Lines", "Alt+Shift+Down")) {
		editing::duplicateLines (*script, 1);
		chose = true;
	}
	if (ImGui::MenuItem ("Delete Lines", keys ("Shift+K").c_str ())) {
		editing::deleteLines (*script);
		chose = true;
	}
	if (chose) wantFocus = scrollToCursor = true;
}

void ScriptView::openFind (bool replace) {
	findOpen = true;
	replaceOpen = replace;
	focusFind = true;
	// The selection, when it is on one line, is what we look for.
	TextSelection s = script->selection ();
	if (! s.empty () && s.start ().line == s.end ().line) findText = editing::selectedText (*script);
}

void ScriptView::findAgain (bool backwards) {
	if (findText.empty ()) {
		openFind (false);
		return;
	}
	TextSelection s = script->selection ();
	TextSelection found = editing::findNext (script->buffer (), backwards ? s.start () : s.end (), findText, findOptions, backwards);
	if (found.empty ()) {
		say ("\"" + findText + "\" isn't in the script.");
		return;
	}
	script->setSelection (found);
	scrollToCursor = centreCursor = true;
}

void ScriptView::updateMatches () {
	const TextBuffer & text = script->buffer ();
	if (matchesVersion == text.version () && matchesOf == findText && matchesCase == findOptions.matchCase && matchesWord == findOptions.wholeWord) return;
	matches = findOpen ? editing::findAll (text, findText, findOptions) : std::vector<TextSelection> ();
	matchesVersion = text.version ();
	matchesOf = findText;
	matchesCase = findOptions.matchCase;
	matchesWord = findOptions.wholeWord;
}

void ScriptView::checkDisk () {
	double now = ImGui::GetTime ();
	if (now < nextDiskCheck || diskChanged) return;
	nextDiskCheck = now + DISK_CHECK_SECONDS;
	if (! script->changedOnDisk ()) return;
	if (script->isModified ()) {
		diskChanged = true;
		return;
	}
	// Without changes of ours, we take the file's.
	std::string error;
	if (script->reload (&error)) {
		Log::get ().add (LogLine::LOG_INFO, "%s changed on disk, and was read again\n", script->displayName ().c_str ());
		say ("The file changed on disk, and was read again.");
	} else {
		Log::get ().add (LogLine::LOG_WARNING, "%s changed on disk, but can't be read: %s\n", script->displayName ().c_str (), error.c_str ());
		script->acceptDiskState ();
	}
}

void ScriptView::drawFindBar () {
	ImGuiStyle & style = ImGui::GetStyle ();
	float field = ImGui::GetFontSize () * 16;
	bool closeIt = false;

	ImGui::SetNextItemWidth (field);
	if (focusFind) {
		ImGui::SetKeyboardFocusHere ();
		focusFind = false;
	}
	// Escape takes back what InputText typed since it was active, so we
	// keep what was typed ourselves.
	std::string typed = findText;
	bool enter = ImGui::InputTextWithHint ("##find", "Find", &findText, ImGuiInputTextFlags_EnterReturnsTrue);
	if (ImGui::IsItemActive () && ImGui::IsKeyPressed (ImGuiKey_Escape, false)) {
		findText = typed;
		closeIt = true;
	}
	if (enter) {
		findAgain (ImGui::GetIO ().KeyShift);
		focusFind = true;
	}
	ImGui::SameLine ();
	if (ImGui::Button ("<")) findAgain (true);
	ImGui::SetItemTooltip ("Find the one before (Shift+Enter)");
	ImGui::SameLine (0, style.ItemInnerSpacing.x);
	if (ImGui::Button (">")) findAgain (false);
	ImGui::SetItemTooltip ("Find the next one (Enter)");
	ImGui::SameLine ();
	ImGui::Checkbox ("Match case", &findOptions.matchCase);
	ImGui::SameLine ();
	ImGui::Checkbox ("Whole words", &findOptions.wholeWord);
	ImGui::SameLine ();
	updateMatches ();
	int current = 0;
	for (size_t i = 0; i < matches.size (); i ++) {
		if (matches[i] == TextSelection (script->selection ().start (), script->selection ().end ())) current = (int) i + 1;
	}
	if (findText.empty ()) ImGui::TextDisabled (" ");
	else if (matches.empty ()) ImGui::TextColored (ImVec4 (1.0f, 0.5f, 0.4f, 1.0f), "No results");
	else if (current) ImGui::TextDisabled ("%d of %d", current, (int) matches.size ());
	else ImGui::TextDisabled ("%d found", (int) matches.size ());
	ImGui::SameLine ();
	if (! replaceOpen && ImGui::SmallButton ("Replace...")) replaceOpen = true;
	// The close button at the right.
	ImGui::SameLine ();
	float room = ImGui::GetContentRegionAvail ().x - ImGui::GetFrameHeight ();
	if (room > 0) ImGui::SetCursorPosX (ImGui::GetCursorPosX () + room);
	if (ImGui::Button ("x", ImVec2 (ImGui::GetFrameHeight (), 0))) closeIt = true;
	ImGui::SetItemTooltip ("Close (Escape)");

	if (replaceOpen) {
		ImGui::SetNextItemWidth (field);
		std::string before = replaceText;
		bool replaceEnter = ImGui::InputTextWithHint ("##replace", "Replace with", &replaceText, ImGuiInputTextFlags_EnterReturnsTrue);
		if (ImGui::IsItemActive () && ImGui::IsKeyPressed (ImGuiKey_Escape, false)) {
			replaceText = before;
			closeIt = true;
		}
		ImGui::SameLine ();
		if (ImGui::Button ("Replace") || replaceEnter) {
			editing::replaceNext (*script, findText, replaceText, findOptions);
			scrollToCursor = centreCursor = true;
		}
		ImGui::SameLine ();
		if (ImGui::Button ("Replace All")) {
			int n = editing::replaceAll (*script, findText, replaceText, findOptions);
			char text[80];
			snprintf (text, sizeof (text), "Replaced %d.", n);
			say (n ? text : "Nothing to replace.");
		}
	}
	if (closeIt) {
		findOpen = replaceOpen = false;
		wantFocus = true;
	}
	ImGui::Separator ();
}

void ScriptView::drawGoToLine () {
	if (goToOpen) {
		ImGui::OpenPopup ("Go to Line");
		goToOpen = false;
		goToLineNumber = script->selection ().cursor.line + 1;
	}
	if (! ImGui::BeginPopup ("Go to Line")) return;
	ImGui::TextUnformatted ("Go to line");
	ImGui::SameLine ();
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 6);
	if (ImGui::IsWindowAppearing ()) ImGui::SetKeyboardFocusHere ();
	bool enter = ImGui::InputInt ("##line", &goToLineNumber, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::SameLine ();
	ImGui::TextDisabled ("of %d", script->buffer ().lineCount ());
	if (enter) {
		goToLine (goToLineNumber);
		ImGui::CloseCurrentPopup ();
	}
	ImGui::EndPopup ();
}

void ScriptView::handleKeys () {
	ImGuiIO & io = ImGui::GetIO ();
	const TextBuffer & text = script->buffer ();
	bool osx = io.ConfigMacOSXBehaviors;
	bool shift = io.KeyShift, alt = io.KeyAlt;
	// On macOS, ImGui has Cmd in KeyCtrl.
	bool ctrl = io.KeyCtrl;
	bool word = osx ? alt : ctrl;
	TextSelection s = script->selection ();
	TextPos cursor = s.cursor;

	// While the list of completions is open, it has the keys that move in
	// it and that choose.
	if (completing && handleCompletionKeys ()) {
		io.InputQueueCharacters.resize (0);
		return;
	}
	// Ctrl+Space (Ctrl, not Cmd, on macOS too) shows every name that fits.
	if ((ctrl || io.KeySuper) && ImGui::IsKeyPressed (ImGuiKey_Space, false)) {
		startCompletion (true);
		io.InputQueueCharacters.resize (0);
		return;
	}
	if (ImGui::IsKeyPressed (ImGuiKey_F12, false)) goToDefinition (cursor);
	if (ImGui::IsKeyPressed (ImGuiKey_F1, false)) openHelpAt (cursor);
	if (ImGui::IsKeyPressed (ImGuiKey_F8, false)) nextProblem (shift);

	if (ImGui::IsKeyPressed (ImGuiKey_LeftArrow)) {
		if (osx && ctrl) moveTo (lineStart (cursor), shift);
		else if (word) moveTo (text.wordLeft (cursor), shift);
		else if (! shift && ! s.empty ()) moveTo (s.start (), false);
		else moveTo (text.previous (cursor), shift);
	}
	if (ImGui::IsKeyPressed (ImGuiKey_RightArrow)) {
		if (osx && ctrl) moveTo (TextPos (cursor.line, (int) text.line (cursor.line).size ()), shift);
		else if (word) moveTo (text.wordRight (cursor), shift);
		else if (! shift && ! s.empty ()) moveTo (s.end (), false);
		else moveTo (text.next (cursor), shift);
	}
	if (ImGui::IsKeyPressed (ImGuiKey_UpArrow)) {
		if (alt && shift) editing::duplicateLines (*script, -1);
		else if (alt) editing::moveLines (*script, -1);
		else if (osx && ctrl) moveTo (text.begin (), shift);
		else moveVertically (-1, shift);
		scrollToCursor = true;
	}
	if (ImGui::IsKeyPressed (ImGuiKey_DownArrow)) {
		if (alt && shift) editing::duplicateLines (*script, 1);
		else if (alt) editing::moveLines (*script, 1);
		else if (osx && ctrl) moveTo (text.end (), shift);
		else moveVertically (1, shift);
		scrollToCursor = true;
	}
	int page = viewHeight > lineHeight ? (int) (viewHeight / lineHeight) - 1 : 1;
	if (ImGui::IsKeyPressed (ImGuiKey_PageUp)) moveVertically (- page, shift);
	if (ImGui::IsKeyPressed (ImGuiKey_PageDown)) moveVertically (page, shift);
	if (ImGui::IsKeyPressed (ImGuiKey_Home)) moveTo (ctrl ? text.begin () : lineStart (cursor), shift);
	if (ImGui::IsKeyPressed (ImGuiKey_End)) moveTo (ctrl ? text.end () : TextPos (cursor.line, (int) text.line (cursor.line).size ()), shift);

	if (ImGui::IsKeyPressed (ImGuiKey_Backspace)) {
		if (osx && ctrl && s.empty ()) script->replace (TextPos (cursor.line, 0), cursor, std::string (), EDIT_OTHER, "Delete");
		else editing::backspace (*script, word);
		scrollToCursor = true;
		wantColumn = -1;
	}
	if (ImGui::IsKeyPressed (ImGuiKey_Delete)) {
		editing::deleteForward (*script, word);
		scrollToCursor = true;
		wantColumn = -1;
	}
	if (ImGui::IsKeyPressed (ImGuiKey_Enter) || ImGui::IsKeyPressed (ImGuiKey_KeypadEnter)) {
		editing::newLine (*script);
		scrollToCursor = true;
		wantColumn = -1;
	}
	if (ImGui::IsKeyPressed (ImGuiKey_Tab)) {
		if (shift) editing::indentLines (*script, false);
		else editing::tab (*script);
		scrollToCursor = true;
	}
	if (ImGui::IsKeyPressed (ImGuiKey_Escape, false)) {
		if (signatureShown) signatureHidden = true;
		else if (findOpen) findOpen = replaceOpen = false;
		else if (! s.empty ()) script->setSelection (TextSelection (cursor));
	}
	if (ImGui::IsKeyPressed (ImGuiKey_F3)) findAgain (shift);

	if (ctrl) {
		if (ImGui::IsKeyPressed (ImGuiKey_A, false)) {
			script->setSelection (TextSelection (text.begin (), text.end ()));
		}
		if (ImGui::IsKeyPressed (ImGuiKey_C, false)) copy ();
		if (ImGui::IsKeyPressed (ImGuiKey_X, false) && ! s.empty ()) {
			copy ();
			editing::deleteSelection (*script, "Cut");
		}
		if (ImGui::IsKeyPressed (ImGuiKey_V)) pasteClipboard ();
		if (ImGui::IsKeyPressed (ImGuiKey_Z)) {
			if (shift) script->commands ().redo ();
			else script->commands ().undo ();
			scrollToCursor = true;
		}
		if (! osx && ImGui::IsKeyPressed (ImGuiKey_Y)) {
			script->commands ().redo ();
			scrollToCursor = true;
		}
		// Ctrl+Alt+F replaces everywhere; Ctrl+H too, but not on macOS,
		// where Cmd+H hides the program.
		if (! shift && ImGui::IsKeyPressed (ImGuiKey_F, false)) openFind (alt);
		if (! osx && ImGui::IsKeyPressed (ImGuiKey_H, false)) openFind (true);
		if (osx && ImGui::IsKeyPressed (ImGuiKey_G)) findAgain (shift);
		if (ImGui::IsKeyPressed (osx ? ImGuiKey_L : ImGuiKey_G, false)) goToOpen = true;
		if (ImGui::IsKeyPressed (ImGuiKey_Slash, false) || ImGui::IsKeyPressed (ImGuiKey_KeypadDivide, false)) {
			editing::toggleComment (*script);
		}
		if (shift && ImGui::IsKeyPressed (ImGuiKey_K, false)) editing::deleteLines (*script);
		// Bigger and smaller, as the editors of pictures zoom.
		if (ImGui::IsKeyPressed (ImGuiKey_Equal) || ImGui::IsKeyPressed (ImGuiKey_KeypadAdd)) zoom (1);
		if (ImGui::IsKeyPressed (ImGuiKey_Minus) || ImGui::IsKeyPressed (ImGuiKey_KeypadSubtract)) zoom (-1);
		if (ImGui::IsKeyPressed (ImGuiKey_0, false) || ImGui::IsKeyPressed (ImGuiKey_Keypad0, false)) zoom (0);
	}

	// What was typed. Ctrl (Cmd on macOS) makes shortcuts, but Ctrl+Alt is
	// AltGr on Windows, which types.
	bool shortcut = ctrl && ! (alt && ! osx);
	if (! shortcut && ! (io.KeySuper && ! osx)) {
		for (int i = 0; i < io.InputQueueCharacters.Size; i ++) {
			unsigned int c = io.InputQueueCharacters[i];
			if (c < 32 || c == 127) continue;
			char utf8[8];
			int n = ImTextCharToUtf8 (utf8, c);
			editing::type (*script, std::string (utf8, n));
			scrollToCursor = true;
			wantColumn = -1;
			signatureHidden = false;
			// A dot, or the second letter of a word, shows the names that fit.
			if (! completing) {
				if (c == '.') {
					startCompletion (false);
				} else if (c < 128 && charClassOf ((unsigned char) c) == CHAR_WORD) {
					TextPos at = script->selection ().cursor;
					const std::string & line = script->buffer ().line (at.line);
					int start = at.column;
					while (start > 0 && charClassOf ((unsigned char) line[start - 1]) == CHAR_WORD) start --;
					if (at.column - start >= 2 && ! (line[start] >= '0' && line[start] <= '9')) startCompletion (false);
				}
			}
		}
	}
	io.InputQueueCharacters.resize (0);
}

void ScriptView::handleMouse (const ImVec2 & origin, bool hovered) {
	ImGuiIO & io = ImGui::GetIO ();
	ImGuiWindow * window = ImGui::GetCurrentWindow ();
	const TextBuffer & text = script->buffer ();
	float textLeft = origin.x + gutterWidth;
	ImVec2 mouse = io.MousePos;
	TextPos at = placeAt (mouse.x - textLeft, mouse.y - origin.y);
	if (hovered && mouse.x >= window->InnerRect.Min.x + gutterWidth) ImGui::SetMouseCursor (ImGuiMouseCursor_TextInput);

	// The list of completions, where it was drawn: its wheel and clicks.
	bool inPopup = completing && mouse.x >= popupX0 && mouse.x < popupX1 && mouse.y >= popupY0 && mouse.y < popupY1;
	if (inPopup) {
		ImGui::SetKeyOwner (ImGuiKey_MouseWheelY, window->GetID ("##text"));
		if (io.MouseWheel != 0) {
			firstItem -= (int) (io.MouseWheel * 3);
			int most = (int) completion.items.size () - 10;
			if (firstItem > most) firstItem = most;
			if (firstItem < 0) firstItem = 0;
		}
		if (ImGui::IsMouseClicked (ImGuiMouseButton_Left)) {
			int row = firstItem + (int) ((mouse.y - popupY0) / lineHeight);
			if (row >= 0 && row < (int) completion.items.size ()) {
				selectedItem = row;
				acceptCompletion ();
			}
		}
		return;
	}
	if (hovered && ImGui::IsMouseClicked (ImGuiMouseButton_Left)) {
		int clicks = io.MouseClickedCount[ImGuiMouseButton_Left];
		completing = false;
		bool inGutter = mouse.x < window->InnerRect.Min.x + gutterWidth;
		TextSelection s = script->selection ();
		if (inGutter || clicks >= 3) {
			TextPos next = at.line + 1 < text.lineCount () ? TextPos (at.line + 1, 0) : TextPos (at.line, (int) text.line (at.line).size ());
			dragStart = TextSelection (TextPos (at.line, 0), next);
			dragUnit = 3;
		} else if (clicks == 2) {
			dragStart = text.wordAt (at);
			dragUnit = 2;
		} else {
			dragStart = TextSelection (io.KeyShift ? s.anchor : at, at);
			dragUnit = 1;
		}
		if (io.KeyShift && dragUnit != 1) dragStart = TextSelection (s.anchor, dragStart.end ());
		script->setSelection (dragStart);
		wantColumn = -1;
		dragging = true;
		// Ctrl+click (Cmd+click on macOS) goes to where the name is made.
		if (io.KeyCtrl && clicks == 1 && ! inGutter && ! io.KeyShift) {
			dragging = false;
			goToDefinition (at);
			return;
		}
	}
	if (! dragging) return;
	if (! ImGui::IsMouseDown (ImGuiMouseButton_Left)) {
		dragging = false;
		return;
	}
	if (ImGui::IsMouseDragging (ImGuiMouseButton_Left, 1.0f)) {
		TextSelection s = dragStart;
		if (dragUnit == 1) {
			s.cursor = at;
		} else {
			TextSelection unit = dragUnit == 2 ? text.wordAt (at)
				: TextSelection (TextPos (at.line, 0), at.line + 1 < text.lineCount () ? TextPos (at.line + 1, 0) : TextPos (at.line, (int) text.line (at.line).size ()));
			if (unit.start () < dragStart.start ()) s = TextSelection (dragStart.end (), unit.start ());
			else s = TextSelection (dragStart.start (), unit.end ());
		}
		script->setSelection (s);
		// Past the edges of the view, it scrolls.
		const ImRect & inner = window->InnerRect;
		if (mouse.y < inner.Min.y) ImGui::SetScrollY (window->Scroll.y - lineHeight);
		else if (mouse.y > inner.Max.y) ImGui::SetScrollY (window->Scroll.y + lineHeight);
		if (mouse.x < inner.Min.x + gutterWidth) ImGui::SetScrollX (window->Scroll.x - charWidth * 2);
		else if (mouse.x > inner.Max.x) ImGui::SetScrollX (window->Scroll.x + charWidth * 2);
	}
}

void ScriptView::drawText () {
	ImGuiContext & g = *GImGui;
	ImGuiIO & io = ImGui::GetIO ();
	ImGuiWindow * window = ImGui::GetCurrentWindow ();
	const TextBuffer & text = script->buffer ();
	const ScriptTheme & colours = theme ();
	ImGuiID id = window->GetID ("##text");

	ImFont * font = ImGui::GetFont ();
	float fontSize = ImGui::GetFontSize ();
	// The advance of a character, which CalcTextSize would round up to a
	// whole point; a long line of rounded ones would drift from the text.
	charWidth = font->CalcTextSizeA (fontSize, FLT_MAX, 0.0f, "MMMMMMMMMMMMMMMMMMMM").x / 20.0f;
	lineHeight = floorf (fontSize * 1.35f + 0.5f);
	int digits = 1;
	for (int n = text.lineCount (); n >= 10; n /= 10) digits ++;
	if (digits < 3) digits = 3;
	gutterWidth = (digits + 2.5f) * charWidth;
	if (widestVersion != text.version ()) {
		widestColumns = 0;
		for (int l = 0; l < text.lineCount (); l ++) {
			int columns = displayColumn (text.line (l), (int) text.line (l).size (), TAB_SIZE);
			if (columns > widestColumns) widestColumns = columns;
		}
		widestVersion = text.version ();
	}
	const ImRect & inner = window->InnerRect;
	viewHeight = inner.GetHeight ();

	// The text starts at origin; the content has room to scroll its last
	// line to the top.
	ImVec2 origin = window->DC.CursorStartPos;
	ImGui::Dummy (ImVec2 (gutterWidth + (widestColumns + 4) * charWidth, text.lineCount () * lineHeight + viewHeight - lineHeight));
	ImRect area (inner.Min, inner.Max);
	ImGui::ItemAdd (area, id, NULL, ImGuiItemFlags_NoNav);
	bool hovered = ImGui::ItemHoverable (area, id, ImGuiItemFlags_None);

	if (hovered && ImGui::IsMouseClicked (ImGuiMouseButton_Left)) wantFocus = true;
	// The tab of the script has the focus, as when the user chose it, and
	// nothing in it is active: the keyboard is the editor's.
	if (g.ActiveId == 0 && ImGui::IsWindowFocused (ImGuiFocusedFlags_RootAndChildWindows)) wantFocus = true;
	if (wantFocus) {
		ImGui::SetActiveID (id, window);
		ImGui::FocusWindow (window);
		wantFocus = false;
	}
	// A click elsewhere takes the keyboard away, as it does from InputText,
	// so that what it clicks can be pressed in this frame; but not a click
	// on the list of completions, which is ours.
	ImVec2 mouse = io.MousePos;
	bool inPopup = completing && mouse.x >= popupX0 && mouse.x < popupX1 && mouse.y >= popupY0 && mouse.y < popupY1;
	if (g.ActiveId == id && ImGui::IsMouseClicked (ImGuiMouseButton_Left) && ! hovered && ! inPopup) {
		ImGui::ClearActiveID ();
		completing = false;
	}
	bool active = g.ActiveId == id;
	if (active) {
		// We let the mouse hover the rest, as InputText does.
		g.ActiveIdAllowOverlap = ! io.MouseDown[0];
		const ImGuiKey owned[] = {
			ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_PageUp, ImGuiKey_PageDown,
			ImGuiKey_Home, ImGuiKey_End, ImGuiKey_Delete, ImGuiKey_Backspace, ImGuiKey_Tab, ImGuiKey_Enter, ImGuiKey_KeypadEnter,
			ImGuiKey_Escape, ImGuiKey_Space,
		};
		for (size_t k = 0; k < sizeof (owned) / sizeof (owned[0]); k ++) ImGui::SetKeyOwner (owned[k], id);
		if (io.ConfigMacOSXBehaviors) ImGui::SetKeyOwner (ImGuiMod_Alt, id);
		if (dragging || ImGui::IsMouseClicked (ImGuiMouseButton_Left)) ImGui::SetKeyOwner (ImGuiKey_MouseLeft, id);
		g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right) | (1 << ImGuiDir_Up) | (1 << ImGuiDir_Down);
		handleKeys ();
	}
	handleMouse (origin, hovered);

	updateMarkers ();
	updateProblems ();
	TextSelection s = script->selection ();
	TextPos cursor = s.cursor;
	// Not in the first frame of a view, whose content has no size yet, so
	// that it can't scroll.
	if (framesDrawn ++ < 1) {
		// Keep the request for the next frame.
	} else if (scrollToCursor) {
		float y = cursor.line * lineHeight;
		if (centreCursor && (y < window->Scroll.y || y + lineHeight > window->Scroll.y + viewHeight)) {
			ImGui::SetScrollY (y - viewHeight * 0.4f);
		} else if (y < window->Scroll.y) {
			ImGui::SetScrollY (y);
		} else if (y + lineHeight > window->Scroll.y + viewHeight) {
			ImGui::SetScrollY (y + lineHeight - viewHeight);
		}
		float x = xOf (cursor);
		float width = inner.GetWidth () - gutterWidth - charWidth;
		if (x < window->Scroll.x) ImGui::SetScrollX (x - charWidth * 4 < 0 ? 0 : x - charWidth * 4);
		else if (x > window->Scroll.x + width) ImGui::SetScrollX (x - width + charWidth * 4);
		scrollToCursor = centreCursor = false;
	}

	ImDrawList * draw = window->DrawList;
	int first = (int) (window->Scroll.y / lineHeight);
	int last = (int) ((window->Scroll.y + viewHeight) / lineHeight) + 1;
	if (first < 0) first = 0;
	if (last >= text.lineCount ()) last = text.lineCount () - 1;
	float textLeft = origin.x + gutterWidth;
	float textTop = origin.y;
	float right = inner.Max.x;

	// The bracket next to the cursor, and its partner.
	TextPos bracket = cursor, partner = cursor;
	if (s.empty ()) {
		partner = editing::matchingBracket (text, cursor);
		if (partner == cursor && cursor.column > 0) {
			bracket = TextPos (cursor.line, cursor.column - 1);
			partner = editing::matchingBracket (text, bracket);
		}
		if (partner == bracket) bracket = partner = TextPos (-1, 0);
	} else {
		bracket = partner = TextPos (-1, 0);
	}
	updateMatches ();

	draw->PushClipRect (ImVec2 (inner.Min.x + gutterWidth, inner.Min.y), inner.Max, true);
	for (int l = first; l <= last; l ++) {
		float y = textTop + l * lineHeight;
		const std::string & line = text.line (l);
		if (l == cursor.line && s.empty () && active) draw->AddRectFilled (ImVec2 (inner.Min.x, y), ImVec2 (right, y + lineHeight), colours.currentLine);
		// The matches of Find.
		for (size_t m = 0; m < matches.size (); m ++) {
			if (matches[m].start ().line < l) continue;
			if (matches[m].start ().line > l) break;
			float x0 = textLeft + xOf (matches[m].start ()), x1 = textLeft + xOf (matches[m].end ());
			draw->AddRectFilled (ImVec2 (x0, y), ImVec2 (x1, y + lineHeight), colours.match);
		}
		// The selection, with a little more for the end of a line in it.
		if (! s.empty () && l >= s.start ().line && l <= s.end ().line) {
			float x0 = l == s.start ().line ? xOf (s.start ()) : 0;
			float x1 = l == s.end ().line ? xOf (s.end ()) : displayColumn (line, (int) line.size (), TAB_SIZE) * charWidth + charWidth * 0.5f;
			draw->AddRectFilled (ImVec2 (textLeft + x0, y), ImVec2 (textLeft + x1, y + lineHeight), active ? colours.selection : colours.inactiveSelection);
		}
		if (l == bracket.line || l == partner.line) {
			for (int b = 0; b < 2; b ++) {
				TextPos at = b ? partner : bracket;
				if (at.line != l) continue;
				float x0 = textLeft + xOf (at);
				draw->AddRectFilled (ImVec2 (x0, y), ImVec2 (x0 + charWidth, y + lineHeight), colours.bracket);
			}
		}
		// The words, with tabs as spaces to the next stop.
		const std::vector<Token> & tokens = tokensOf (l);
		float textY = y + (lineHeight - fontSize) * 0.5f;
		for (size_t t = 0; t < tokens.size (); t ++) {
			const Token & k = tokens[t];
			if (k.kind == TOKEN_SPACE) continue;
			ImU32 colour = colours.token[k.kind];
			int start = k.start;
			while (start < k.end) {
				int end = start;
				while (end < k.end && line[end] != '\t') end ++;
				if (end > start) {
					float x = textLeft + displayColumn (line, start, TAB_SIZE) * charWidth;
					draw->AddText (font, fontSize, ImVec2 (x, textY), colour, line.c_str () + start, line.c_str () + end);
				}
				start = end < k.end ? end + 1 : end;
			}
			if (k.kind == TOKEN_ERROR) {
				// A wavy line under what is wrong.
				float x0 = textLeft + displayColumn (line, k.start, TAB_SIZE) * charWidth;
				float x1 = textLeft + displayColumn (line, k.end, TAB_SIZE) * charWidth;
				wavyLine (draw, x0, x1, y + lineHeight - 2, colours.error);
			}
		}
		// What the last compile said of the line, under its text.
		for (size_t m = 0; m < markers.size (); m ++) {
			if (markers[m].stamp != text.lineStamp (l)) continue;
			int from = (int) editing::indentOf (line).size ();
			float x0 = textLeft + displayColumn (line, from, TAB_SIZE) * charWidth;
			float x1 = textLeft + displayColumn (line, (int) line.size (), TAB_SIZE) * charWidth;
			if (x1 < x0 + charWidth * 2) x1 = x0 + charWidth * 2;
			wavyLine (draw, x0, x1, y + lineHeight - 2, markers[m].error ? colours.error : colours.warning);
			break;
		}
		// What the live checks found.
		for (size_t p = 0; p < problems.size (); p ++) {
			if (problems[p].stamp != text.lineStamp (l)) continue;
			const Diagnostic & d = problems[p].diagnostic;
			int end = d.end.line == d.start.line ? d.end.column : (int) line.size ();
			float x0 = textLeft + displayColumn (line, d.start.column, TAB_SIZE) * charWidth;
			float x1 = textLeft + displayColumn (line, end, TAB_SIZE) * charWidth;
			if (x1 < x0 + charWidth) x1 = x0 + charWidth;
			wavyLine (draw, x0, x1, y + lineHeight - 1, d.severity == Diagnostic::ERROR ? colours.error : colours.warning);
		}
	}
	// The cursor.
	if (active) {
		float x = textLeft + xOf (cursor);
		float y = textTop + cursor.line * lineHeight;
		draw->AddRectFilled (ImVec2 (x, y + 1), ImVec2 (x + (fontSize > 20 ? 2.0f : 1.5f), y + lineHeight - 1), colours.caret);
		// Where the system's input method shows what is being composed.
		g.PlatformImeData.WantVisible = true;
		g.PlatformImeData.WantTextInput = true;
		g.PlatformImeData.InputPos = ImVec2 (x - 1, y);
		g.PlatformImeData.InputLineHeight = lineHeight;
		g.PlatformImeData.ViewportId = window->Viewport->ID;
	}
	draw->PopClipRect ();

	// The numbers of the lines, which stay when the text scrolls sideways.
	draw->PushClipRect (inner.Min, ImVec2 (inner.Min.x + gutterWidth, inner.Max.y), true);
	draw->AddRectFilled (inner.Min, ImVec2 (inner.Min.x + gutterWidth, inner.Max.y), colours.gutter);
	for (int l = first; l <= last; l ++) {
		char number[16];
		int n = snprintf (number, sizeof (number), "%d", l + 1);
		float x = inner.Min.x + gutterWidth - (n + 1.5f) * charWidth;
		float y = textTop + l * lineHeight + (lineHeight - fontSize) * 0.5f;
		bool current = l >= s.start ().line && l <= s.end ().line;
		draw->AddText (font, fontSize, ImVec2 (x, y), current ? colours.currentLineNumber : colours.lineNumber, number, number + n);
		// A dot for what the compiler or the live checks said: red for an
		// error, yellow for a warning.
		int said = 0;		// 1 a warning, 2 an error
		for (size_t m = 0; m < markers.size (); m ++) {
			if (markers[m].stamp == text.lineStamp (l)) said = std::max (said, markers[m].error ? 2 : 1);
		}
		for (size_t p = 0; p < problems.size (); p ++) {
			if (problems[p].stamp == text.lineStamp (l)) said = std::max (said, problems[p].diagnostic.severity == Diagnostic::ERROR ? 2 : 1);
		}
		if (said) {
			float r = charWidth * 0.32f;
			draw->AddCircleFilled (ImVec2 (inner.Min.x + charWidth * 0.75f, textTop + l * lineHeight + lineHeight * 0.5f), r,
				said == 2 ? colours.error : colours.warning);
		}
	}
	draw->PopClipRect ();

	if (completing) updateCompletion ();
	drawCompletion (origin);
	drawSignature (origin);
	drawHover (origin, hovered);
}

// A window over the others that takes no input, as a tooltip, at a point
// of the screen. End it with ImGui::End, whatever this returns.
static bool beginFloating (const char * name, const ImVec2 & at, const ImVec2 & pivot) {
	ImGui::SetNextWindowPos (at, ImGuiCond_Always, pivot);
	ImGui::SetNextWindowBgAlpha (1.0f);
	ImGuiWindowFlags flags = ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
		| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
		| ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDocking;
	return ImGui::Begin (name, NULL, flags);
}

// The font of the interface, at its size, for docs, while the code's is
// pushed, perhaps bigger or smaller.
static void pushTextFont () {
	ImGui::PushFont (ImGui::GetIO ().Fonts->Fonts[0], ImGui::GetStyle ().FontSizeBase);
}

// Some text cut to a number of characters, with ... after it.
static std::string shortened (const std::string & text, int characters) {
	int n = 0;
	for (size_t i = 0; i < text.size (); i ++) {
		if (((unsigned char) text[i] & 0xC0) == 0x80) continue;
		if (n ++ == characters) return text.substr (0, i) + "\xE2\x80\xA6";
	}
	return text;
}

static int characterCount (const std::string & text) {
	int n = 0;
	for (size_t i = 0; i < text.size (); i ++) {
		if (((unsigned char) text[i] & 0xC0) != 0x80) n ++;
	}
	return n;
}

static const int COMPLETION_ROWS = 10;

void ScriptView::startCompletion (bool everything) {
	completeEverything = everything;
	completion = assist::complete (script->buffer (), script->selection ().cursor, actions.scriptIndex (), indexName (script), everything);
	completedVersion = script->buffer ().version ();
	completing = ! completion.items.empty ();
	selectedItem = firstItem = 0;
	if (! completing && everything) say ("No names fit here.");
}

void ScriptView::updateCompletion () {
	const TextBuffer & text = script->buffer ();
	TextSelection s = script->selection ();
	TextPos cursor = s.cursor;
	if (! s.empty () || cursor.line != completion.from.line || cursor < completion.from) {
		completing = false;
		return;
	}
	std::string typed = text.line (cursor.line).substr (completion.from.column, cursor.column - completion.from.column);
	if (completedVersion == text.version () && typed == completion.typed) return;
	std::string had = selectedItem < (int) completion.items.size () ? completion.items[selectedItem].label : std::string ();
	CompletionList list = assist::complete (text, cursor, actions.scriptIndex (), indexName (script), completeEverything);
	// Typing what isn't a name ends it.
	if (list.items.empty () || list.from != completion.from) {
		completing = false;
		return;
	}
	completion = list;
	completedVersion = text.version ();
	selectedItem = firstItem = 0;
	for (size_t i = 0; i < completion.items.size (); i ++) {
		if (completion.items[i].label == had && i > 0 && completion.items[0].score <= completion.items[i].score) selectedItem = (int) i;
	}
	if (selectedItem >= COMPLETION_ROWS) firstItem = selectedItem - COMPLETION_ROWS + 1;
}

void ScriptView::acceptCompletion () {
	if (! completing || selectedItem < 0 || selectedItem >= (int) completion.items.size ()) return;
	const Completion & item = completion.items[selectedItem];
	TextPos cursor = script->selection ().cursor;
	// The rest of the word after the cursor goes too.
	const std::string & line = script->buffer ().line (cursor.line);
	int end = cursor.column;
	while (end < (int) line.size () && charClassOf ((unsigned char) line[end]) == CHAR_WORD) end ++;
	script->replace (completion.from, TextPos (cursor.line, end), item.label, EDIT_OTHER, "Complete");
	completing = false;
	scrollToCursor = true;
	wantColumn = -1;
	signatureHidden = false;
}

bool ScriptView::handleCompletionKeys () {
	int n = (int) completion.items.size ();
	if (! n) {
		completing = false;
		return false;
	}
	bool moved = false;
	if (ImGui::IsKeyPressed (ImGuiKey_UpArrow)) {
		selectedItem = (selectedItem + n - 1) % n;
		moved = true;
	} else if (ImGui::IsKeyPressed (ImGuiKey_DownArrow)) {
		selectedItem = (selectedItem + 1) % n;
		moved = true;
	} else if (ImGui::IsKeyPressed (ImGuiKey_PageUp)) {
		selectedItem = selectedItem - (COMPLETION_ROWS - 1) < 0 ? 0 : selectedItem - (COMPLETION_ROWS - 1);
		moved = true;
	} else if (ImGui::IsKeyPressed (ImGuiKey_PageDown)) {
		selectedItem = selectedItem + (COMPLETION_ROWS - 1) >= n ? n - 1 : selectedItem + (COMPLETION_ROWS - 1);
		moved = true;
	} else if (ImGui::IsKeyPressed (ImGuiKey_Enter, false) || ImGui::IsKeyPressed (ImGuiKey_KeypadEnter, false)
			|| ImGui::IsKeyPressed (ImGuiKey_Tab, false)) {
		acceptCompletion ();
		return true;
	} else if (ImGui::IsKeyPressed (ImGuiKey_Escape, false)) {
		completing = false;
		return true;
	}
	if (! moved) return false;
	if (selectedItem < firstItem) firstItem = selectedItem;
	if (selectedItem >= firstItem + COMPLETION_ROWS) firstItem = selectedItem - COMPLETION_ROWS + 1;
	return true;
}

void ScriptView::drawCompletion (const ImVec2 & origin) {
	if (! completing || completion.items.empty ()) {
		popupX0 = popupX1 = popupY0 = popupY1 = 0;
		return;
	}
	ImGuiWindow * window = ImGui::GetCurrentWindow ();
	const ImRect & inner = window->InnerRect;
	const ScriptTheme & colours = theme ();
	int n = (int) completion.items.size ();
	int rows = n < COMPLETION_ROWS ? n : COMPLETION_ROWS;
	if (firstItem > n - rows) firstItem = n - rows;
	if (firstItem < 0) firstItem = 0;
	int labelColumns = 8, detailColumns = 0;
	for (int i = 0; i < n; i ++) {
		int l = characterCount (completion.items[i].label), d = characterCount (completion.items[i].detail);
		if (l > labelColumns) labelColumns = l;
		if (d > detailColumns) detailColumns = d;
	}
	if (labelColumns > 36) labelColumns = 36;
	if (detailColumns > 40) detailColumns = 40;
	float pad = charWidth * 0.5f;
	float width = (labelColumns + (detailColumns ? detailColumns + 3 : 0)) * charWidth + pad * 2;
	float height = rows * lineHeight;
	float x = origin.x + gutterWidth + xOf (completion.from) - pad;
	float y = origin.y + (completion.from.line + 1) * lineHeight;
	// Above the line when it doesn't fit below.
	if (y + height > inner.Max.y && y - lineHeight - height >= inner.Min.y) y -= lineHeight + height;
	if (x + width > inner.Max.x) x = inner.Max.x - width;
	if (x < inner.Min.x + gutterWidth) x = inner.Min.x + gutterWidth;
	popupX0 = x;
	popupY0 = y;
	popupX1 = x + width;
	popupY1 = y + height;

	ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (0, 0));
	ImGui::PushStyleVar (ImGuiStyleVar_WindowBorderSize, 1.0f);
	if (beginFloating ("##completion", ImVec2 (x, y), ImVec2 (0, 0))) {
		ImDrawList * draw = ImGui::GetWindowDrawList ();
		ImVec2 at = ImGui::GetCursorScreenPos ();
		ImGui::Dummy (ImVec2 (width, height));
		ImFont * font = ImGui::GetFont ();
		float fontSize = ImGui::GetFontSize ();
		ImU32 dim = ImGui::GetColorU32 (ImGuiCol_TextDisabled);
		for (int r = 0; r < rows; r ++) {
			int i = firstItem + r;
			const Completion & item = completion.items[i];
			float rowY = at.y + r * lineHeight;
			if (i == selectedItem) draw->AddRectFilled (ImVec2 (at.x, rowY), ImVec2 (at.x + width, rowY + lineHeight), colours.selection);
			float textY = rowY + (lineHeight - fontSize) * 0.5f;
			std::string label = shortened (item.label, labelColumns);
			draw->AddText (font, fontSize, ImVec2 (at.x + pad, textY), colours.token[item.kind], label.c_str ());
			if (detailColumns && ! item.detail.empty ()) {
				std::string detail = shortened (item.detail, detailColumns);
				float dx = at.x + width - pad - characterCount (detail) * charWidth;
				draw->AddText (font, fontSize, ImVec2 (dx, textY), dim, detail.c_str ());
			}
		}
		// Where the list is, when it is longer.
		if (n > rows) {
			float barTop = at.y + height * firstItem / n, barBottom = at.y + height * (firstItem + rows) / n;
			draw->AddRectFilled (ImVec2 (at.x + width - 3, barTop), ImVec2 (at.x + width - 1, barBottom), dim);
		}
	}
	ImGui::End ();
	ImGui::PopStyleVar (2);

	// The docs of the chosen one, beside the list.
	const Completion & item = completion.items[selectedItem];
	if (item.doc.empty ()) return;
	float docWidth = ImGui::GetFontSize () * 26;
	bool right = x + width + docWidth < ImGui::GetMainViewport ()->Size.x;
	if (beginFloating ("##completion-doc", ImVec2 (right ? x + width + 4 : x - 4, y), ImVec2 (right ? 0.0f : 1.0f, 0.0f))) {
		pushTextFont ();
		ImGui::PushTextWrapPos (ImGui::GetCursorPosX () + docWidth);
		ImGui::TextUnformatted (shortened (item.doc, 600).c_str ());
		ImGui::PopTextWrapPos ();
		ImGui::PopFont ();
	}
	ImGui::End ();
}

void ScriptView::drawSignature (const ImVec2 & origin) {
	ImGuiWindow * window = ImGui::GetCurrentWindow ();
	if (ImGui::GetActiveID () != window->GetID ("##text") || signatureHidden || ! script->selection ().empty ()) return;
	const TextBuffer & text = script->buffer ();
	TextPos cursor = script->selection ().cursor;
	if (signatureVersion != text.version () || signatureAt != cursor) {
		signatureShown = assist::signatureAt (text, cursor, actions.scriptIndex (), indexName (script), &signature);
		signatureVersion = text.version ();
		signatureAt = cursor;
	}
	if (! signatureShown) return;
	const ScriptTheme & colours = theme ();
	float x = origin.x + gutterWidth + xOf (cursor) - charWidth * 4;
	float lineTop = origin.y + cursor.line * lineHeight;
	// Above the line, unless the list of completions is there.
	bool below = (completing && popupY1 <= lineTop + 1) || lineTop - signatureHeight < ImGui::GetMainViewport ()->Pos.y + ImGui::GetFrameHeight ();
	if (x < window->InnerRect.Min.x) x = window->InnerRect.Min.x;
	if (beginFloating ("##signature", ImVec2 (x, below ? lineTop + lineHeight : lineTop - 2), ImVec2 (0.0f, below ? 0.0f : 1.0f))) {
		ImVec4 normal = ImGui::GetStyleColorVec4 (ImGuiCol_Text), dim = ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled);
		ImVec4 current = ImGui::ColorConvertU32ToFloat4 (colours.token[TOKEN_SUB]);
		for (size_t f = 0; f < signature.signatures.size (); f ++) {
			const Signature & form = signature.signatures[f];
			bool active = (int) f == signature.active;
			int parameter = -1;
			int count = (int) form.parameters.size ();
			if (signature.argument < count) parameter = signature.argument;
			else if (form.maxArguments < 0 && count) parameter = count - 1;
			ImVec4 colour = active ? normal : dim;
			ImGui::TextColored (colour, "%s (", signature.name.c_str ());
			for (int p = 0; p < count; p ++) {
				ImGui::SameLine (0, 0);
				if (p) {
					ImGui::TextColored (colour, ", ");
					ImGui::SameLine (0, 0);
				}
				ImGui::TextColored (active && p == parameter ? current : colour, "%s", form.parameters[p].c_str ());
			}
			ImGui::SameLine (0, 0);
			ImGui::TextColored (colour, ")");
		}
		if (! signature.doc.empty ()) {
			pushTextFont ();
			ImGui::PushTextWrapPos (ImGui::GetCursorPosX () + ImGui::GetFontSize () * 30);
			ImGui::TextColored (dim, "%s", shortened (signature.doc, 300).c_str ());
			ImGui::PopTextWrapPos ();
			ImGui::PopFont ();
		}
		signatureHeight = ImGui::GetWindowHeight ();
	}
	ImGui::End ();
}

void ScriptView::drawHover (const ImVec2 & origin, bool hovered) {
	ImGuiIO & io = ImGui::GetIO ();
	ImGuiWindow * window = ImGui::GetCurrentWindow ();
	const TextBuffer & text = script->buffer ();
	ImVec2 mouse = io.MousePos;
	bool inGutter = mouse.x < window->InnerRect.Min.x + gutterWidth;
	if (! hovered || dragging || completing || ImGui::IsMouseDown (ImGuiMouseButton_Left) || ! ImGui::IsMousePosValid ()) {
		hoverPlace = TextPos (-1, 0);
		return;
	}
	float textLeft = origin.x + gutterWidth;
	int line = (int) floorf ((mouse.y - origin.y) / lineHeight);
	if (line < 0 || line >= text.lineCount ()) {
		hoverPlace = TextPos (-1, 0);
		return;
	}
	// The character under the mouse; past the end of the line, none.
	TextPos at = inGutter ? TextPos (line, -1) : placeAt (mouse.x - textLeft - charWidth * 0.5f, mouse.y - origin.y);
	if (! inGutter && mouse.x - textLeft > displayColumn (text.line (line), (int) text.line (line).size (), TAB_SIZE) * charWidth) at = TextPos (line, -1);
	double now = ImGui::GetTime ();
	if (at != hoverPlace || hoverVersion != text.version ()) {
		hoverPlace = at;
		hoverSince = now;
		hoverVersion = text.version ();
		hoverFound = false;
		hover = HoverInfo ();
		if (at.column >= 0) hoverFound = assist::hoverAt (text, at, actions.scriptIndex (), indexName (script), &hover);
	}
	if (now - hoverSince < 0.45) return;
	std::vector<const Marker *> said;
	for (size_t m = 0; m < markers.size (); m ++) {
		if (markers[m].stamp == text.lineStamp (line)) said.push_back (&markers[m]);
	}
	// The live checks' words for what is under the mouse, or for the line
	// in the gutter.
	std::vector<const Diagnostic *> found;
	for (size_t p = 0; p < problems.size (); p ++) {
		if (problems[p].stamp != text.lineStamp (line)) continue;
		const Diagnostic & d = problems[p].diagnostic;
		int end = d.end.line == d.start.line ? d.end.column : (int) text.line (line).size ();
		if (inGutter || (at.column >= d.start.column && at.column < (end > d.start.column ? end : d.start.column + 1))) found.push_back (&d);
	}
	if (! hoverFound && said.empty () && found.empty ()) return;
	const ScriptTheme & colours = theme ();
	if (beginFloating ("##hover", ImVec2 (mouse.x, origin.y + (line + 1) * lineHeight + 2), ImVec2 (0, 0))) {
		float wrap = ImGui::GetFontSize () * 32;
		pushTextFont ();
		ImGui::PushTextWrapPos (ImGui::GetCursorPosX () + wrap);
		for (size_t m = 0; m < said.size (); m ++) {
			ImGui::TextColored (ImGui::ColorConvertU32ToFloat4 (said[m]->error ? colours.error : colours.warning), "%s", said[m]->text.c_str ());
		}
		for (size_t f = 0; f < found.size (); f ++) {
			ImGui::TextColored (ImGui::ColorConvertU32ToFloat4 (found[f]->severity == Diagnostic::ERROR ? colours.error : colours.warning), "%s", found[f]->message.c_str ());
		}
		ImGui::PopTextWrapPos ();
		ImGui::PopFont ();
		if (hoverFound && ! inGutter) {
			if (! said.empty () || ! found.empty ()) ImGui::Separator ();
			ImGui::PushTextWrapPos (ImGui::GetCursorPosX () + wrap);
			ImGui::TextUnformatted (hover.title.c_str ());
			ImGui::PopTextWrapPos ();
			pushTextFont ();
			ImGui::PushTextWrapPos (ImGui::GetCursorPosX () + wrap);
			if (! hover.where.empty ()) ImGui::TextDisabled ("%s", hover.where.c_str ());
			if (! hover.body.empty ()) {
				ImGui::Spacing ();
				ImGui::TextUnformatted (shortened (hover.body, 900).c_str ());
			}
			if (! hover.page.empty ()) ImGui::TextDisabled ("F1 with the cursor on it opens its help.");
			else if (! hover.where.empty ()) ImGui::TextDisabled ("F12 or %s+click goes there.", io.ConfigMacOSXBehaviors ? "Cmd" : "Ctrl");
			ImGui::PopTextWrapPos ();
			ImGui::PopFont ();
		}
	}
	ImGui::End ();
}

void ScriptView::updateMarkers () {
	unsigned int serial;
	std::vector<BuildMessage> messages = actions.buildMessagesFor (script->path (), &serial);
	if (serial == markersSerial) return;
	markersSerial = serial;
	markers.clear ();
	const TextBuffer & text = script->buffer ();
	for (size_t m = 0; m < messages.size (); m ++) {
		int line = messages[m].line - 1;
		if (line < 0 || line >= text.lineCount ()) continue;
		Marker marker;
		marker.stamp = text.lineStamp (line);
		marker.error = messages[m].type != BuildMessage::WARNING;
		marker.text = messages[m].text;
		markers.push_back (marker);
	}
}

void ScriptView::goToDefinition (TextPos at) {
	Definition definition;
	if (! assist::definitionAt (script->buffer (), at, actions.scriptIndex (), indexName (script), &definition)) {
		HoverInfo info;
		if (assist::hoverAt (script->buffer (), at, actions.scriptIndex (), indexName (script), &info) && ! info.page.empty ()) {
			say ("That is a built-in of SLUDGE: F1 opens its help.");
		} else {
			say ("There is no definition of that in the project.");
		}
		return;
	}
	if (definition.file == ScriptIndex::key (indexName (script))) {
		goTo (definition.at);
		return;
	}
	actions.showInScript (definition.file, definition.at.line, definition.at.column);
}

void ScriptView::openHelpAt (TextPos at) {
	HoverInfo info;
	if (assist::hoverAt (script->buffer (), at, actions.scriptIndex (), indexName (script), &info) && ! info.page.empty ()) {
		actions.openHelpPage (info.page);
	} else if (at.column > 0 && assist::hoverAt (script->buffer (), TextPos (at.line, at.column - 1), actions.scriptIndex (), indexName (script), &info) && ! info.page.empty ()) {
		actions.openHelpPage (info.page);
	} else {
		actions.openHelpPage ("SLUDGE_Language_Reference_Manual.html");
	}
}

void ScriptView::drawOutline () {
	const std::vector<ScriptSymbol> & symbols = actions.scriptIndex ().symbolsOf (indexName (script));
	ImGui::SeparatorText ("Outline");
	if (symbols.empty ()) {
		ImGui::TextDisabled ("No subs or objectTypes yet.");
		return;
	}
	const ScriptTheme & colours = theme ();
	ImGuiTreeNodeFlags leaf = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
	for (size_t i = 0; i < symbols.size (); i ++) {
		const ScriptSymbol & symbol = symbols[i];
		if (! symbol.owner.empty ()) continue;
		ImGui::PushID ((int) i);
		ImU32 colour = colours.token[symbol.kind == SYMBOL_OBJECT_TYPE ? TOKEN_OBJECT_TYPE : symbol.kind == SYMBOL_SUB ? TOKEN_SUB
			: symbol.kind == SYMBOL_FLAG ? TOKEN_FLAG : symbol.kind == SYMBOL_DEFINITION ? TOKEN_DEFINITION : TOKEN_VARIABLE];
		std::string label = symbol.kind == SYMBOL_SUB ? symbol.declaration ().substr (4) : symbol.name;
		ImGui::PushStyleColor (ImGuiCol_Text, colour);
		if (symbol.kind == SYMBOL_OBJECT_TYPE) {
			bool open = ImGui::TreeNodeEx ("##type", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", label.c_str ());
			if (ImGui::IsItemClicked () && ! ImGui::IsItemToggledOpen ()) goTo (symbol.at);
			ImGui::PopStyleColor ();
			if (open) {
				for (size_t m = 0; m < symbols.size (); m ++) {
					const ScriptSymbol & member = symbols[m];
					if (member.owner != symbol.name) continue;
					ImGui::PushID ((int) m);
					std::string text = member.kind == SYMBOL_EVENT ? "event " + member.name
						: member.kind == SYMBOL_SUB ? member.declaration ().substr (4 + symbol.name.size () + 1) : member.name;
					ImGui::TreeNodeEx ("##member", leaf, "%s", text.c_str ());
					if (ImGui::IsItemClicked ()) goTo (member.at);
					ImGui::PopID ();
				}
				ImGui::TreePop ();
			}
		} else {
			ImGui::TreeNodeEx ("##symbol", leaf, "%s", label.c_str ());
			ImGui::PopStyleColor ();
			if (ImGui::IsItemClicked ()) goTo (symbol.at);
		}
		ImGui::PopID ();
	}
}

int ScriptView::lineWithStamp (uint64_t stamp) const {
	const TextBuffer & text = script->buffer ();
	for (int l = 0; l < text.lineCount (); l ++) {
		if (text.lineStamp (l) == stamp) return l;
	}
	return -1;
}

void ScriptView::updateProblems () {
	const TextBuffer & text = script->buffer ();
	const ScriptIndex & index = actions.scriptIndex ();
	bool all = actions.knowsAllNames (indexName (script));
	if (text.version () == problemsVersion && index.version () == problemsNames && all == problemsAllNames) return;
	double now = ImGui::GetTime ();
	// Not while a word is being typed: half a second after the last change,
	// and at once the first time.
	if (problemsDue == 0) problemsDue = problemsVersion == 0 ? now : now + 0.5;
	if (now < problemsDue) return;
	problemsDue = 0;
	problemsVersion = text.version ();
	problemsNames = index.version ();
	problemsAllNames = all;
	std::vector<Diagnostic> found = analyseScript (text, index, indexName (script), all);
	problems.clear ();
	for (size_t d = 0; d < found.size (); d ++) {
		if (found[d].start.line < 0 || found[d].start.line >= text.lineCount ()) continue;
		Problem p;
		p.diagnostic = found[d];
		p.stamp = text.lineStamp (found[d].start.line);
		problems.push_back (p);
	}
}

void ScriptView::nextProblem (bool backwards) {
	if (problems.empty ()) {
		say ("The live checks found no problems.");
		return;
	}
	TextPos cursor = script->selection ().cursor;
	std::vector<TextPos> places;
	for (size_t p = 0; p < problems.size (); p ++) {
		int line = lineWithStamp (problems[p].stamp);
		if (line >= 0) places.push_back (TextPos (line, problems[p].diagnostic.start.column));
	}
	if (places.empty ()) return;
	std::sort (places.begin (), places.end ());
	TextPos chosen = backwards ? places.back () : places.front ();
	if (backwards) {
		for (size_t i = places.size (); i -- > 0; ) {
			if (places[i] < cursor) {
				chosen = places[i];
				break;
			}
		}
	} else {
		for (size_t i = 0; i < places.size (); i ++) {
			if (cursor < places[i]) {
				chosen = places[i];
				break;
			}
		}
	}
	goTo (chosen);
	hoverPlace = TextPos (-1, 0);
}

void ScriptView::drawProblems () {
	ImGui::SeparatorText ("Problems");
	const ScriptTheme & colours = theme ();
	if (problems.empty ()) {
		ImGui::TextDisabled ("None found.");
	}
	for (size_t p = 0; p < problems.size (); p ++) {
		const Diagnostic & d = problems[p].diagnostic;
		int line = lineWithStamp (problems[p].stamp);
		if (line < 0) continue;
		ImGui::PushID ((int) p);
		char where[32];
		snprintf (where, sizeof (where), "%d:%d", line + 1, d.start.column + 1);
		ImGui::PushStyleColor (ImGuiCol_Text, d.severity == Diagnostic::ERROR ? colours.error : colours.warning);
		bool chosen = ImGui::Selectable (where, false, ImGuiSelectableFlags_AllowOverlap);
		ImGui::PopStyleColor ();
		ImGui::SameLine (ImGui::GetFontSize () * 3.5f);
		ImGui::PushTextWrapPos (0.0f);
		ImGui::TextUnformatted (d.message.c_str ());
		ImGui::PopTextWrapPos ();
		if (chosen) goTo (TextPos (line, d.start.column));
		ImGui::PopID ();
	}
	if (! problemsAllNames) {
		ImGui::Spacing ();
		ImGui::PushTextWrapPos (0.0f);
		ImGui::TextDisabled ("The names of the project are checked in the scripts of the open project, once all of its scripts can be read.");
		ImGui::PopTextWrapPos ();
	}
}

void ScriptView::drawStatus () {
	const TextBuffer & text = script->buffer ();
	TextSelection s = script->selection ();
	if (! notice.empty () && ImGui::GetTime () < noticeUntil) {
		ImGui::TextColored (ImVec4 (1.0f, 0.8f, 0.3f, 1.0f), "%s", notice.c_str ());
	} else {
		int column = displayColumn (text.line (s.cursor.line), s.cursor.column, TAB_SIZE) + 1;
		if (s.empty ()) {
			ImGui::TextDisabled ("Line %d, Column %d", s.cursor.line + 1, column);
		} else {
			int lines = s.end ().line - s.start ().line + 1;
			int characters = 0;
			std::string selected = text.textIn (s.start (), s.end ());
			for (size_t i = 0; i < selected.size (); i ++) {
				if (((unsigned char) selected[i] & 0xC0) != 0x80) characters ++;
			}
			if (lines > 1) ImGui::TextDisabled ("Line %d, Column %d (%d characters, %d lines selected)", s.cursor.line + 1, column, characters, lines);
			else ImGui::TextDisabled ("Line %d, Column %d (%d selected)", s.cursor.line + 1, column, characters);
		}
	}
	int errors = 0, warnings = 0;
	for (size_t p = 0; p < problems.size (); p ++) {
		if (problems[p].diagnostic.severity == Diagnostic::ERROR) errors ++;
		else warnings ++;
	}
	if (errors || warnings) {
		const ScriptTheme & colours = theme ();
		char count[64];
		ImGui::SameLine (0, ImGui::GetFontSize () * 1.5f);
		if (errors) {
			snprintf (count, sizeof (count), "%d error%s", errors, errors == 1 ? "" : "s");
			ImGui::TextColored (ImGui::ColorConvertU32ToFloat4 (colours.error), "%s", count);
			if (ImGui::IsItemClicked ()) nextProblem (false);
			ImGui::SetItemTooltip ("F8 goes to the next problem, Shift+F8 to the one before.");
		}
		if (warnings) {
			if (errors) ImGui::SameLine ();
			snprintf (count, sizeof (count), "%d warning%s", warnings, warnings == 1 ? "" : "s");
			ImGui::TextColored (ImGui::ColorConvertU32ToFloat4 (colours.warning), "%s", count);
			if (ImGui::IsItemClicked ()) nextProblem (false);
			ImGui::SetItemTooltip ("F8 goes to the next problem, Shift+F8 to the one before.");
		}
	}
	std::string unit = script->indentUnit ();
	char indent[32];
	if (unit == "\t") snprintf (indent, sizeof (indent), "Tabs");
	else snprintf (indent, sizeof (indent), "Spaces: %d", (int) unit.size ());
	char size[32] = "";
	if (actions.scriptFontSize () > 0) snprintf (size, sizeof (size), "%g pt    ", actions.scriptFontSize ());
	char right[128];
	snprintf (right, sizeof (right), "%s%s    %s    %s", size, indent, text.mixedLineEnds () ? (text.crlf ? "Mixed, CRLF" : "Mixed, LF") : text.crlf ? "CRLF" : "LF",
		text.bom ? "UTF-8 with BOM" : "UTF-8");
	float width = ImGui::CalcTextSize (right).x;
	ImGui::SameLine ();
	float room = ImGui::GetContentRegionAvail ().x - width;
	if (room > 0) ImGui::SetCursorPosX (ImGui::GetCursorPosX () + room);
	ImGui::TextDisabled ("%s", right);
}

void ScriptView::draw () {
	checkDisk ();
	if (diskChanged) {
		ImGui::TextColored (ImVec4 (1.0f, 0.8f, 0.3f, 1.0f), "The file has changed on disk, and you have changes too.");
		ImGui::SameLine ();
		if (ImGui::SmallButton ("Read the File Again")) {
			std::string error;
			if (! script->reload (&error)) say ("Can't read the file: " + error);
			diskChanged = false;
		}
		ImGui::SameLine ();
		if (ImGui::SmallButton ("Keep My Changes")) {
			script->acceptDiskState ();
			diskChanged = false;
		}
	}
	if (findOpen) drawFindBar ();
	drawGoToLine ();

	float statusHeight = ImGui::GetTextLineHeightWithSpacing ();
	ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (0, 0));
	ImGui::PushStyleColor (ImGuiCol_ChildBg, theme ().background);
	// The project's size, or the interface's.
	ImGui::PushFont (codeFont, actions.scriptFontSize ());
	bool open = ImGui::BeginChild ("##code", ImVec2 (0, - statusHeight), ImGuiChildFlags_None,
		ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavInputs);
	// The child has its padding and colour now; the windows that the text
	// opens, as the list of completions, have the usual ones.
	ImGui::PopStyleColor ();
	ImGui::PopStyleVar ();
	if (open) drawText ();
	ImGui::EndChild ();
	ImGui::PopFont ();
	drawStatus ();
}

void ScriptView::drawInspector () {
	const TextBuffer & text = script->buffer ();
	if (beginProperties ("Script")) {
		propertyRow ("Lines");
		ImGui::Text ("%d", text.lineCount ());
		propertyRow ("Indent");
		std::string unit = script->indentUnit ();
		if (unit == "\t") ImGui::TextUnformatted ("Tabs");
		else ImGui::Text ("%d spaces", (int) unit.size ());
		propertyRow ("Line ends");
		ImGui::TextUnformatted (text.mixedLineEnds () ? (text.crlf ? "Mixed; new lines get CRLF" : "Mixed; new lines get LF")
			: text.crlf ? "CRLF (Windows)" : "LF");
		propertyRow ("Encoding");
		ImGui::TextUnformatted (text.bom ? "UTF-8 with a byte order mark" : "UTF-8");
		ImGui::EndTable ();
	}
	drawOutline ();
	drawProblems ();
}

} // namespace devkit
