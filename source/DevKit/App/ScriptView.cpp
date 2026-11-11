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
	ImU32 selection, inactiveSelection, match, bracket, caret, error;
};

static ImU32 rgb (unsigned int c, int alpha = 255) {
	return IM_COL32 ((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, alpha);
}

static const ScriptTheme & theme () {
	static ScriptTheme dark, light;
	static bool made = false;
	if (! made) {
		made = true;
		const unsigned int darkTokens[TOKEN_KIND_COUNT] = {
			0xD4D4D4,	// space
			0x6A9955,	// comment
			0xCE9178,	// string
			0xD7BA7D,	// escape
			0xE2A86B,	// file
			0xB5CEA8,	// number
			0x569CD6,	// keyword
			0x9CDCFE,	// element
			0xD4D4D4,	// operator
			0xC8C8C8,	// bracket
			0xA0A0A0,	// punctuation
			0xD4D4D4,	// identifier
			0xDCDCAA,	// function
			0x4FC1FF,	// constant
			0xF0C674,	// sub
			0x4EC9B0,	// object type
			0x9CDCFE,	// variable
			0x4FC1FF,	// definition
			0xC586C0,	// flag
			0xF44747,	// error
		};
		const unsigned int lightTokens[TOKEN_KIND_COUNT] = {
			0x1F1F1F, 0x008000, 0xA31515, 0xEE0000, 0xB35900, 0x098658, 0x0000FF, 0x0451A5,
			0x1F1F1F, 0x303030, 0x606060, 0x1F1F1F, 0x795E26, 0x0070C1, 0x8A5A00, 0x267F99,
			0x001080, 0x0070C1, 0xAF00DB, 0xE51400,
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
		dark.error = rgb (0xF44747);
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
		light.error = rgb (0xE51400);
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
	  goToOpen (false), goToLineNumber (1), diskChanged (false), nextDiskCheck (0), noticeUntil (0) {}

void ScriptView::say (const std::string & text) {
	notice = text;
	noticeUntil = ImGui::GetTime () + NOTICE_SECONDS;
}

bool ScriptView::animating () const {
	return dragging;
}

const std::vector<Token> & ScriptView::tokensOf (int line) {
	const TextBuffer & text = script->buffer ();
	if ((int) lineTokens.size () != text.lineCount ()) lineTokens.resize (text.lineCount ());
	LineTokens & cached = lineTokens[line];
	if (cached.stamp != text.lineStamp (line)) {
		cached.tokens = lexLine (text.line (line), NULL);
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
	int line = (int) floorf (y / lineHeight);
	if (line < 0) return TextPos (0, 0);
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
		if (findOpen) findOpen = replaceOpen = false;
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
		if (ImGui::IsKeyPressed (ImGuiKey_F, false)) openFind (alt);
		if (! osx && ImGui::IsKeyPressed (ImGuiKey_H, false)) openFind (true);
		if (osx && ImGui::IsKeyPressed (ImGuiKey_G)) findAgain (shift);
		if (ImGui::IsKeyPressed (osx ? ImGuiKey_L : ImGuiKey_G, false)) goToOpen = true;
		if (ImGui::IsKeyPressed (ImGuiKey_Slash, false) || ImGui::IsKeyPressed (ImGuiKey_KeypadDivide, false)) {
			editing::toggleComment (*script);
		}
		if (shift && ImGui::IsKeyPressed (ImGuiKey_K, false)) editing::deleteLines (*script);
	}

	// What was typed. Ctrl (Cmd on macOS) makes shortcuts, but Ctrl+Alt is
	// AltGr on Windows, which types.
	bool shortcut = ctrl && ! (alt && ! osx);
	if (! shortcut) {
		for (int i = 0; i < io.InputQueueCharacters.Size; i ++) {
			unsigned int c = io.InputQueueCharacters[i];
			if (c < 32 || c == 127) continue;
			char utf8[8];
			int n = ImTextCharToUtf8 (utf8, c);
			editing::type (*script, std::string (utf8, n));
			scrollToCursor = true;
			wantColumn = -1;
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

	if (hovered && ImGui::IsMouseClicked (ImGuiMouseButton_Left)) {
		int clicks = io.MouseClickedCount[ImGuiMouseButton_Left];
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

	TextSelection s = script->selection ();
	TextPos cursor = s.cursor;
	if (scrollToCursor) {
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
				float base = y + lineHeight - 2;
				for (float x = x0; x < x1; x += 4) {
					draw->AddLine (ImVec2 (x, base), ImVec2 (x + 2, base - 2), colours.error);
					draw->AddLine (ImVec2 (x + 2, base - 2), ImVec2 (x + 4 < x1 ? x + 4 : x1, base), colours.error);
				}
			}
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
	}
	draw->PopClipRect ();
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
	std::string unit = script->indentUnit ();
	char indent[32];
	if (unit == "\t") snprintf (indent, sizeof (indent), "Tabs");
	else snprintf (indent, sizeof (indent), "Spaces: %d", (int) unit.size ());
	char right[96];
	snprintf (right, sizeof (right), "%s    %s    %s", indent, text.mixedLineEnds () ? (text.crlf ? "Mixed, CRLF" : "Mixed, LF") : text.crlf ? "CRLF" : "LF",
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
	if (codeFont) ImGui::PushFont (codeFont, 0.0f);
	bool open = ImGui::BeginChild ("##code", ImVec2 (0, - statusHeight), ImGuiChildFlags_None,
		ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavInputs);
	if (open) drawText ();
	ImGui::EndChild ();
	if (codeFont) ImGui::PopFont ();
	ImGui::PopStyleColor ();
	ImGui::PopStyleVar ();
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
}

} // namespace devkit
