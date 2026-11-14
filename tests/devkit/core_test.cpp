/*
 * Tests of the core of the Dev Kit (source/DevKit/Core): undo and redo,
 * documents, opening, saving and closing them, recent files, the
 * preferences, projects, floors, z-buffers, sprite banks and translations. They need no display: a fake Platform answers the questions
 * and dialogs.
 *
 * Usage: core_test <folder for files> <folder of the example projects> <fixtures>
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <png.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#endif

#include <chrono>
#include <thread>

#include <algorithm>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "CommandStack.h"
#include "CompileJob.h"
#include "Launch.h"
#include "ProjectCheck.h"
#include "ProjectSearch.h"
#include "ClipDocument.h"
#include "PreviewPlayer.h"
#include "ProjectDocument.h"
#include "RigDocument.h"
#include "RoomScript.h"
#include "animedit.h"
#include "Document.h"
#include "DocumentManager.h"
#include "FileMemory.h"
#include "FloorDocument.h"
#include "SpriteBankDocument.h"
#include "TranslationDocument.h"
#include "ZBufferDocument.h"
#include "Preferences.h"
#include "RecentFiles.h"
#include "ScriptAnalysis.h"
#include "ScriptAssist.h"
#include "ScriptCatalog.h"
#include "ScriptIndex.h"
#include "ScriptDocument.h"
#include "ScriptLexer.h"
#include "TextBuffer.h"
#include "TextEditing.h"

using namespace devkit;

static int failures = 0;

#define CHECK(condition) do { \
	if (! (condition)) { \
		fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		failures ++; \
	} \
} while (0)

static std::string folder, examples, fixtures;

static void writeFile (const std::string & path, const std::string & text) {
	FILE * fp = fopen (path.c_str (), "wb");
	fwrite (text.data (), 1, text.size (), fp);
	fclose (fp);
}

static void mkdirs (const std::string & path) {
	std::string at;
	for (size_t i = 0; i <= path.size (); i ++) {
		if (i == path.size () || path[i] == '/') {
			if (! at.empty ()) {
#if defined(_WIN32)
				_mkdir (at.c_str ());
#else
				mkdir (at.c_str (), 0777);
#endif
			}
		}
		if (i < path.size ()) at += path[i];
	}
}

static std::string readFile (const std::string & path) {
	std::string text;
	FILE * fp = fopen (path.c_str (), "rb");
	if (! fp) return text;
	char buffer[4096];
	size_t n;
	while ((n = fread (buffer, 1, sizeof (buffer), fp)) > 0) text.append (buffer, n);
	fclose (fp);
	return text;
}

// A program that writes the given output, in pieces, and ends.
class FakeProcess : public Platform::Process {
public:
	FakeProcess (const std::string & text, int code) : output (text), at (0), exitCode (code) {}
	size_t read (char * buffer, size_t size) {
		// A few bytes at a time, so that lines come in pieces.
		size_t n = output.size () - at;
		if (n > 7) n = 7;
		if (n > size) n = size;
		memcpy (buffer, output.data () + at, n);
		at += n;
		return n;
	}
	int wait () { return exitCode; }
	void kill () { at = output.size (); exitCode = -9; }

private:
	std::string output;
	size_t at;
	int exitCode;
};

// A Platform that answers from lists, and keeps what it was asked.
class FakePlatform : public Platform {
public:
	std::string processOutput;
	int processExitCode;
	std::vector<std::string> ranArguments;
	std::string ranFolder;

	FakePlatform () : processExitCode (0) {}
	Process * runProgram (const std::vector<std::string> & arguments, const std::string & workingFolder, std::string *) {
		ranArguments = arguments;
		ranFolder = workingFolder;
		return new FakeProcess (processOutput, processExitCode);
	}
	std::deque<int> answers;					// For ask
	std::deque<std::vector<std::string> > chosen;	// For the file dialogs
	std::vector<std::string> messages, questions;

	void openFileDialog (const std::string &, const std::vector<FileFilter> &, const std::string &, bool, FilesChosen done) {
		choose (done);
	}
	void saveFileDialog (const std::string &, const std::vector<FileFilter> &, const std::string &, FilesChosen done) {
		choose (done);
	}
	void openFolderDialog (const std::string &, const std::string &, FilesChosen done) {
		choose (done);
	}
	void showMessage (MessageKind, const std::string & title, const std::string &) {
		messages.push_back (title);
	}
	int ask (const std::string & title, const std::string &, const std::vector<std::string> & buttons) {
		questions.push_back (title);
		if (answers.empty ()) return (int) buttons.size () - 1;
		int answer = answers.front ();
		answers.pop_front ();
		return answer;
	}
	bool startProgram (const std::vector<std::string> &, const std::string &, std::string *) { return true; }
	bool openWithSystem (const std::string &) { return true; }
	std::string settingsFolder () { return folder; }
	std::string cacheFolder () { return folder; }
	std::string programFolder () { return folder; }

private:
	void choose (FilesChosen done) {
		std::vector<std::string> files;
		if (! chosen.empty ()) {
			files = chosen.front ();
			chosen.pop_front ();
		}
		done (files);
	}
};

// A document of one line of text, which a command changes.
class TextDocument : public Document {
public:
	std::string text;

	const char * kind () const { return "Text"; }
	std::string untitledName () const { return "Untitled Text.txt"; }

	void setText (const std::string & newText) {
		commands ().run (new ValueCommand<std::string> ("Typing", text, newText,
			[this] (const std::string & t) { text = t; }, 1));
	}

protected:
	bool read (const std::string & path, std::string * error) {
		FILE * fp = fopen (path.c_str (), "rb");
		if (! fp) {
			*error = "No such file";
			return false;
		}
		fclose (fp);
		text = readFile (path);
		return true;
	}
	bool write (const std::string & path, std::string *) {
		writeFile (path, text);
		return true;
	}
};

static DocumentType textType () {
	DocumentType type;
	type.name = "Text";
	type.extensions.push_back ("txt");
	type.create = [] () -> Document * { return new TextDocument; };
	return type;
}

static void testCommandStack () {
	int value = 0;
	std::function<void (const int &)> set = [&value] (const int & v) { value = v; };
	CommandStack stack;
	CHECK (! stack.isModified ());
	CHECK (! stack.canUndo ());

	stack.run (new ValueCommand<int> ("Set to 1", 0, 1, set));
	stack.run (new ValueCommand<int> ("Set to 2", 1, 2, set));
	CHECK (value == 2);
	CHECK (stack.isModified ());
	CHECK (stack.undoLabel () == "Set to 2");

	stack.undo ();
	CHECK (value == 1);
	CHECK (stack.redoLabel () == "Set to 2");
	stack.undo ();
	CHECK (value == 0);
	CHECK (! stack.isModified ());
	stack.redo ();
	CHECK (value == 1);

	// A new change drops what was undone.
	stack.run (new ValueCommand<int> ("Set to 5", 1, 5, set));
	CHECK (! stack.canRedo ());

	// Saved, then changed and undone: not modified.
	stack.markSaved ();
	stack.run (new ValueCommand<int> ("Set to 6", 5, 6, set));
	CHECK (stack.isModified ());
	stack.undo ();
	CHECK (! stack.isModified ());

	// Undone past the saved state, then changed: we can't get back to it.
	stack.undo ();
	stack.run (new ValueCommand<int> ("Set to 7", 1, 7, set));
	CHECK (stack.isModified ());
	stack.undo ();
	CHECK (stack.isModified ());

	// The steps of a drag merge into one, until we stop merging.
	CommandStack drag;
	value = 0;
	drag.run (new ValueCommand<int> ("Drag", 0, 1, set, 42));
	drag.run (new ValueCommand<int> ("Drag", 1, 2, set, 42));
	drag.run (new ValueCommand<int> ("Drag", 2, 3, set, 42));
	drag.stopMerging ();
	drag.run (new ValueCommand<int> ("Drag", 3, 4, set, 42));
	CHECK (value == 4);
	drag.undo ();
	CHECK (value == 3);
	drag.undo ();
	CHECK (value == 0);
	CHECK (! drag.canUndo ());

	// After a change that we can't undo, a changed document stays changed.
	drag.redo ();
	drag.clear ();
	CHECK (! drag.canUndo ());
	CHECK (drag.isModified ());
}

static void testPaths () {
	CHECK (extensionOf ("/a/b/Floor.FLO") == "flo");
	CHECK (extensionOf ("/a.b/noextension") == "");
	CHECK (fileNameOf ("/a/b/c.slp") == "c.slp");
	CHECK (folderOf ("/a/b/c.slp") == "/a/b/");
	CHECK (folderOf ("c.slp") == "");
}

static void testDocuments () {
	FakePlatform platform;
	RecentFiles recent (folder + "recent.txt");
	recent.clear ();
	DocumentManager manager (platform, recent);
	manager.registerType (textType ());
	std::vector<Document *> opened, closed;
	manager.opened = [&opened] (Document * d) { opened.push_back (d); };
	manager.closing = [&closed] (Document * d) { closed.push_back (d); };

	std::string a = folder + "a.txt", b = folder + "B.TXT";
	writeFile (a, "first");
	writeFile (b, "second");

	// Opening by extension, in any case, and only once.
	TextDocument * docA = (TextDocument *) manager.open (a);
	CHECK (docA && docA->text == "first");
	CHECK (manager.open (a) == docA);
	TextDocument * docB = (TextDocument *) manager.open (b);
	CHECK (docB && manager.active () == docB);
	CHECK (opened.size () == 2);
	CHECK (recent.files ().size () == 2 && recent.files ()[0] == b);

	// What we can't open.
	CHECK (manager.open (folder + "c.flo") == NULL);
	CHECK (manager.open (folder + "missing.txt") == NULL);
	CHECK (platform.messages.size () == 2);

	// Closing without changes asks nothing.
	manager.close (docB);
	CHECK (platform.questions.empty ());
	CHECK (closed.size () == 1 && manager.documents ().size () == 1);

	// With changes: cancel keeps it, don't save closes it.
	docA->setText ("changed");
	CHECK (docA->isModified ());
	bool result = true;
	platform.answers.push_back (2);
	manager.close (docA, [&result] (bool c) { result = c; });
	CHECK (! result && manager.documents ().size () == 1);
	platform.answers.push_back (1);
	manager.close (docA, [&result] (bool c) { result = c; });
	CHECK (result && manager.documents ().empty ());
	CHECK (readFile (a) == "first");

	// A new document saves in the file that we choose, when we close it.
	TextDocument * docC = (TextDocument *) manager.create (manager.types ()[0]);
	CHECK (docC->displayName () == "Untitled Text.txt");
	docC->setText ("new");
	std::string c = folder + "c.txt";
	platform.answers.push_back (0);
	platform.chosen.push_back (std::vector<std::string> (1, c));
	manager.close (docC, [&result] (bool closed) { result = closed; });
	CHECK (result && readFile (c) == "new");
	CHECK (recent.files ()[0] == c);

	// A save dialog that is cancelled keeps the document.
	TextDocument * docD = (TextDocument *) manager.create (manager.types ()[0]);
	docD->setText ("d");
	platform.answers.push_back (0);
	manager.close (docD, [&result] (bool closed) { result = closed; });
	CHECK (! result && manager.documents ().size () == 1);

	// Closing all stops at the first one that the user keeps.
	TextDocument * docE = (TextDocument *) manager.open (a);
	docE->setText ("e");
	platform.answers.push_back (1);		// Don't save the last one, docE
	platform.answers.push_back (2);		// Cancel docD
	manager.closeAll ([&result] (bool closed) { result = closed; });
	CHECK (! result && manager.documents ().size () == 1 && manager.documents ()[0] == docD);
	platform.answers.push_back (1);
	manager.closeAll ([&result] (bool closed) { result = closed; });
	CHECK (result && manager.documents ().empty ());

	// The recent files are kept on disk, newest first.
	RecentFiles again (folder + "recent.txt");
	CHECK (again.files () == recent.files ());
	CHECK (again.files ()[0] == a);
}

static void testPreferences () {
	// A file of the GTK Project Manager, with a key that we don't know.
	std::string file = folder + "SLUDGE.ini";
	writeFile (file, "KillImages=1\r\nWriteStrings=1\r\nVerbose=0\r\nSearchSensitive=0\r\n"
		"Editor=gedit\r\nImageViewer=loupe\r\nAudioPlayer=totem\r\nModPlayer=totem\r\nFuture=yes\r\n");
	Preferences preferences;
	CHECK (preferences.read (file));
	CHECK (! preferences.keepImages && preferences.writeStrings && ! preferences.verbose);
	CHECK (preferences.imageViewer == "loupe" && preferences.modPlayer == "totem");

	// Scripts open in the Dev Kit now, but the GTK Project Manager's
	// editor stays in the file, as the keys that we don't know do.
	preferences.imageViewer = "eog {file}";
	CHECK (preferences.write (file));
	Preferences again;
	CHECK (again.read (file));
	CHECK (again.imageViewer == "eog {file}");
	CHECK (readFile (file).find ("Future=yes\r\n") != std::string::npos);
	CHECK (readFile (file).find ("Editor=gedit\r\n") != std::string::npos);

	Preferences defaults;
	CHECK (! defaults.read (folder + "missing.ini"));
	CHECK (defaults.keepImages && defaults.verbose && defaults.imageViewer.empty ());
}

// The text of a script: lines, their ends and the byte order mark, kept
// as they were.
static void testTextBuffer () {
	TextBuffer text;
	CHECK (text.lineCount () == 1 && text.fileText ().empty ());
	text.setText ("one\r\ntwo\r\n");
	CHECK (text.lineCount () == 3 && text.line (1) == "two" && text.line (2).empty () && text.crlf);
	CHECK (text.fileText () == "one\r\ntwo\r\n");
	text.setText ("\xEF\xBB\xBFno end");
	CHECK (text.bom && ! text.crlf && text.lineCount () == 1 && text.line (0) == "no end");
	CHECK (text.fileText () == "\xEF\xBB\xBFno end");
	text.setText ("\n");
	CHECK (text.lineCount () == 2 && text.fileText () == "\n");
	// Mixed ends stay; a new line ends as most do, and a joined line as
	// the last of its lines did.
	text.setText ("a\r\nb\nc\r\nd");
	CHECK (text.crlf && text.mixedLineEnds ());
	text.insert (TextPos (1, 1), "\nx");
	CHECK (text.fileText () == "a\r\nb\r\nx\nc\r\nd");
	text.erase (TextPos (0, 1), TextPos (2, 1));
	CHECK (text.fileText () == "a\nc\r\nd");

	text.setText ("abc\ndef");
	uint64_t stamp0 = text.lineStamp (0), stamp1 = text.lineStamp (1);
	TextPos end = text.insert (TextPos (0, 1), "X\nY");
	CHECK (end == TextPos (1, 1) && text.text () == "aX\nYbc\ndef");
	CHECK (text.lineStamp (0) != stamp0 && text.lineStamp (2) == stamp1);
	text.erase (TextPos (0, 1), TextPos (1, 1));
	CHECK (text.text () == "abc\ndef");
	CHECK (text.textIn (TextPos (0, 2), TextPos (1, 1)) == "c\nd");
	CHECK (TextBuffer::endOf (TextPos (3, 4), "ab\ncde") == TextPos (4, 3));

	// Characters, words and columns, with UTF-8 and tabs.
	text.setText ("\tsay (ego, \"\xC3\xA9t\xC3\xA9\");");
	CHECK (text.next (TextPos (0, 12)) == TextPos (0, 14));
	CHECK (text.previous (TextPos (0, 14)) == TextPos (0, 12));
	CHECK (text.clamp (TextPos (0, 13)) == TextPos (0, 12));
	CHECK (text.wordRight (TextPos (0, 0)) == TextPos (0, 4));
	CHECK (text.wordLeft (TextPos (0, 4)) == TextPos (0, 1));
	CHECK (text.wordAt (TextPos (0, 2)) == TextSelection (TextPos (0, 1), TextPos (0, 4)));
	CHECK (displayColumn (text.line (0), 1, 4) == 4 && displayColumn (text.line (0), 15, 4) == 17);
	CHECK (byteAtDisplayColumn (text.line (0), 4, 4) == 1 && byteAtDisplayColumn (text.line (0), 1, 4) == 0);
}

// Every script of the examples reads and saves as it was.
static void testScriptFiles () {
	const char * projects[] = { "Welcome/Welcome.slp", "VerbCoin/VerbCoin.slp" };
	int scripts = 0;
	for (int p = 0; p < 2; p ++) {
		ProjectDocument project;
		std::string error;
		CHECK (project.load (examples + projects[p], &error));
		for (size_t f = 0; f < project.files ().size (); f ++) {
			std::string name = project.files ()[f];
			if (extensionOf (name) != "slu" && extensionOf (name) != "sld") continue;
			std::string path = folderOf (examples + projects[p]) + name;
			ScriptDocument script;
			CHECK (script.load (path, &error));
			CHECK (script.save (folder + "copy.slu", &error));
			CHECK (readFile (folder + "copy.slu") == readFile (path));
			CHECK (! script.isModified ());
			scripts ++;
		}
	}
	CHECK (scripts >= 10);
	ScriptDocument script;
	std::string error;
	CHECK (script.load (fixtures + "init-utf8.slu", &error));
	CHECK (script.save (folder + "copy.slu", &error) && readFile (folder + "copy.slu") == readFile (fixtures + "init-utf8.slu"));
	// The old project's text is Windows-1252: Check Files converts it.
	CHECK (! script.load (fixtures + "OldProject/init.slu", &error));
	CHECK (error.find ("isn't UTF-8") != std::string::npos && error.find ("Check Files") != std::string::npos);
	CHECK (script.path () == folder + "copy.slu");

	// The indent of a file.
	writeFile (folder + "spaces.slu", "sub a () {\n   b ();\n   if (c) {\n      d ();\n   }\n}\n");
	CHECK (script.load (folder + "spaces.slu", &error) && script.indentUnit () == "   ");
	CHECK (script.load (examples + "VerbCoin/init.slu", &error) && script.indentUnit () == "\t");

	// A change on disk, which we read again.
	writeFile (folder + "disk.slu", "one\n");
	CHECK (script.load (folder + "disk.slu", &error) && ! script.changedOnDisk ());
	writeFile (folder + "disk.slu", "one\ntwo\n");
	CHECK (script.changedOnDisk ());
	CHECK (script.reload (&error) && script.buffer ().text () == "one\ntwo\n" && ! script.isModified () && ! script.changedOnDisk ());
	script.commands ().undo ();
	CHECK (script.buffer ().text () == "one\n" && script.isModified ());
}

static ScriptDocument * scriptOf (const std::string & text, TextPos cursor) {
	ScriptDocument * script = new ScriptDocument;
	script->setText (text, "Test");
	script->commands ().clear ();
	script->commands ().markSaved ();
	script->setSelection (TextSelection (cursor));
	return script;
}

static void typeEach (ScriptDocument & script, const std::string & text) {
	for (size_t i = 0; i < text.size (); i ++) editing::type (script, std::string (1, text[i]));
}

// What the keys of the script editor do, and how they undo.
static void testTextEditing () {
	std::unique_ptr<ScriptDocument> script (scriptOf ("", TextPos ()));
	// Typing undoes a word at a time.
	typeEach (*script, "abc def");
	CHECK (script->buffer ().text () == "abc def" && script->selection ().cursor == TextPos (0, 7));
	script->commands ().undo ();
	CHECK (script->buffer ().text () == "abc " && script->selection ().cursor == TextPos (0, 4));
	script->commands ().undo ();
	CHECK (script->buffer ().text () == "" && ! script->isModified ());
	script->commands ().redo ();
	script->commands ().redo ();
	CHECK (script->buffer ().text () == "abc def");
	// Backspaces together, until the cursor moves.
	editing::backspace (*script, false);
	editing::backspace (*script, false);
	script->setSelection (TextSelection (TextPos (0, 2)));
	editing::backspace (*script, false);
	CHECK (script->buffer ().text () == "ac d");
	script->commands ().undo ();
	CHECK (script->buffer ().text () == "abc d");
	script->commands ().undo ();
	CHECK (script->buffer ().text () == "abc def");
	editing::backspace (*script, true);
	CHECK (script->buffer ().text () == "abc ");
	script->setSelection (TextSelection (TextPos (0, 0)));
	editing::deleteForward (*script, false);
	editing::deleteForward (*script, false);
	CHECK (script->buffer ().text () == "c ");
	script->commands ().undo ();
	CHECK (script->buffer ().text () == "abc ");

	// Brackets and quotes close themselves, and typing the closing one
	// moves over it.
	script.reset (scriptOf ("sub init () {\n}", TextPos (0, 13)));
	editing::newLine (*script);
	CHECK (script->buffer ().text () == "sub init () {\n\t\n}" && script->selection ().cursor == TextPos (1, 1));
	typeEach (*script, "say (ego, \"Hi\");");
	CHECK (script->buffer ().line (1) == "\tsay (ego, \"Hi\");" && script->selection ().cursor == TextPos (1, 17));
	editing::newLine (*script);
	typeEach (*script, "if (x) {");
	CHECK (script->buffer ().line (2) == "\tif (x) {}");
	editing::newLine (*script);
	CHECK (script->buffer ().line (2) == "\tif (x) {" && script->buffer ().line (3) == "\t\t" && script->buffer ().line (4) == "\t}");
	CHECK (script->selection ().cursor == TextPos (3, 2));
	// A } on a line of its own goes to the indent of its {.
	editing::newLine (*script);
	script->setSelection (TextSelection (TextPos (4, 2)));
	typeEach (*script, "}");
	CHECK (script->buffer ().line (4) == "\t}");
	// An empty pair goes with one backspace.
	script.reset (scriptOf ("x = ", TextPos (0, 4)));
	typeEach (*script, "(");
	CHECK (script->buffer ().text () == "x = ()");
	editing::backspace (*script, false);
	CHECK (script->buffer ().text () == "x = ");
	// No partner before a word, nor a quote after one, and a selection
	// gets wrapped.
	script.reset (scriptOf ("call", TextPos (0, 0)));
	typeEach (*script, "(");
	CHECK (script->buffer ().text () == "(call");
	script.reset (scriptOf ("it", TextPos (0, 2)));
	typeEach (*script, "'");
	CHECK (script->buffer ().text () == "it'");
	script->setSelection (TextSelection (TextPos (0, 0), TextPos (0, 2)));
	typeEach (*script, "\"");
	CHECK (script->buffer ().text () == "\"it\"'" && script->selection () == TextSelection (TextPos (0, 1), TextPos (0, 3)));

	// Tab and Shift+Tab, on lines and on a selection of them.
	script.reset (scriptOf ("a\nb\nc", TextPos (0, 0)));
	script->setSelection (TextSelection (TextPos (0, 1), TextPos (2, 0)));
	editing::tab (*script);
	CHECK (script->buffer ().text () == "\ta\n\tb\nc");
	CHECK (script->selection () == TextSelection (TextPos (0, 2), TextPos (2, 0)));
	editing::indentLines (*script, false);
	CHECK (script->buffer ().text () == "a\nb\nc");
	script->commands ().undo ();
	script->commands ().undo ();
	CHECK (script->buffer ().text () == "a\nb\nc");
	script->setSelection (TextSelection (TextPos (1, 1)));
	editing::tab (*script);
	CHECK (script->buffer ().text () == "a\nb\t\nc");

	// Comments, on and off, at the indent of the lines.
	script.reset (scriptOf ("\tone ();\n\t\ttwo ();\n", TextPos (0, 2)));
	script->setSelection (TextSelection (TextPos (0, 2), TextPos (1, 3)));
	editing::toggleComment (*script);
	CHECK (script->buffer ().text () == "\t# one ();\n\t# \ttwo ();\n");
	CHECK (script->selection () == TextSelection (TextPos (0, 4), TextPos (1, 5)));
	editing::toggleComment (*script);
	CHECK (script->buffer ().text () == "\tone ();\n\t\ttwo ();\n");

	// Moving, copying and deleting lines.
	script.reset (scriptOf ("a\nb\nc", TextPos (1, 1)));
	editing::moveLines (*script, -1);
	CHECK (script->buffer ().text () == "b\na\nc" && script->selection ().cursor == TextPos (0, 1));
	editing::moveLines (*script, -1);
	CHECK (script->buffer ().text () == "b\na\nc");
	editing::moveLines (*script, 1);
	editing::moveLines (*script, 1);
	CHECK (script->buffer ().text () == "a\nc\nb" && script->selection ().cursor == TextPos (2, 1));
	editing::duplicateLines (*script, 1);
	CHECK (script->buffer ().text () == "a\nc\nb\nb" && script->selection ().cursor == TextPos (3, 1));
	editing::deleteLines (*script);
	editing::deleteLines (*script);
	CHECK (script->buffer ().text () == "a\nc");
	script->commands ().undo ();
	script->commands ().undo ();
	script->commands ().undo ();
	script->commands ().undo ();
	script->commands ().undo ();
	script->commands ().undo ();
	CHECK (script->buffer ().text () == "a\nb\nc" && ! script->isModified ());

	// Brackets, but not those in strings and comments.
	TextBuffer text;
	text.setText ("sub a (\"(\") {\n\t# }\n\tb [1];\n}");
	CHECK (editing::matchingBracket (text, TextPos (0, 12)) == TextPos (3, 0));
	CHECK (editing::matchingBracket (text, TextPos (3, 0)) == TextPos (0, 12));
	CHECK (editing::matchingBracket (text, TextPos (0, 6)) == TextPos (0, 10));
	CHECK (editing::matchingBracket (text, TextPos (0, 8)) == TextPos (0, 8));

	// Finding and replacing.
	script.reset (scriptOf ("say (ego, \"Ego\");\nsayEgo ();\n", TextPos (0, 0)));
	editing::FindOptions options;
	CHECK (editing::findAll (script->buffer (), "ego", options).size () == 3);
	options.matchCase = true;
	CHECK (editing::findAll (script->buffer (), "Ego", options).size () == 2);
	options.wholeWord = true;
	std::vector<TextSelection> found = editing::findAll (script->buffer (), "Ego", options);
	CHECK (found.size () == 1 && found[0].start () == TextPos (0, 11));
	options = editing::FindOptions ();
	CHECK (editing::findNext (script->buffer (), TextPos (0, 6), "ego", options, false).start () == TextPos (0, 11));
	CHECK (editing::findNext (script->buffer (), TextPos (1, 9), "ego", options, false).start () == TextPos (0, 5));
	CHECK (editing::findNext (script->buffer (), TextPos (0, 5), "ego", options, true).start () == TextPos (1, 3));
	script->setSelection (TextSelection (TextPos (0, 5), TextPos (0, 8)));
	CHECK (editing::replaceNext (*script, "ego", "hero", options));
	CHECK (script->buffer ().line (0) == "say (hero, \"Ego\");" && script->selection ().start () == TextPos (0, 12));
	CHECK (editing::replaceAll (*script, "ego", "x", options) == 2);
	CHECK (script->buffer ().text () == "say (hero, \"x\");\nsayx ();\n");
	script->commands ().undo ();
	CHECK (script->buffer ().text () == "say (hero, \"Ego\");\nsayEgo ();\n");
}

// The words of a line, and what the built-ins are.
static void testScriptLexer () {
	std::vector<Token> tokens = lexLine ("\tsay (ego, \"a \\\"b\\\"\"); # done", NULL);
	TokenKind kinds[] = { TOKEN_SPACE, TOKEN_FUNCTION, TOKEN_SPACE, TOKEN_BRACKET, TOKEN_IDENTIFIER, TOKEN_PUNCTUATION, TOKEN_SPACE,
		TOKEN_STRING, TOKEN_ESCAPE, TOKEN_STRING, TOKEN_ESCAPE, TOKEN_STRING, TOKEN_BRACKET, TOKEN_PUNCTUATION, TOKEN_SPACE, TOKEN_COMMENT };
	CHECK (tokens.size () == sizeof (kinds) / sizeof (kinds[0]));
	for (size_t i = 0; i < tokens.size () && i < sizeof (kinds) / sizeof (kinds[0]); i ++) CHECK (tokens[i].kind == kinds[i]);
	CHECK (tokens[1].start == 1 && tokens[1].end == 4);
	tokens = lexLine ("if (x >= 10 && y != NORTH) addOverlay ('room.tga', 0, 0);", NULL);
	CHECK (tokens[0].kind == TOKEN_KEYWORD && tokens[5].kind == TOKEN_OPERATOR && tokens[5].end - tokens[5].start == 2);
	CHECK (tokens[7].kind == TOKEN_NUMBER);
	CHECK (tokens[tokenAt (tokens, 21)].kind == TOKEN_CONSTANT);
	CHECK (tokens[tokenAt (tokens, 41)].kind == TOKEN_FILE);
	// A string with no end is wrong.
	tokens = lexLine ("say (ego, \"oops);", NULL);
	CHECK (tokens.back ().kind == TOKEN_ERROR && tokens.back ().start == 10);
	CHECK (kindOfName ("walkSpeed", NULL) == TOKEN_ELEMENT && kindOfName ("objectType", NULL) == TOKEN_KEYWORD);
	CHECK (kindOfName ("setStatusColor", NULL) == TOKEN_FUNCTION && kindOfName ("TRUE", NULL) == TOKEN_CONSTANT);
	CHECK (kindOfName ("ego", NULL) == TOKEN_IDENTIFIER);

	const ScriptCatalog & catalog = ScriptCatalog::get ();
	int functions = 0;
#define FUNC(s, n) functions ++; CHECK (catalog.function (#n) != NULL);
#include "functionlist.h"
#undef FUNC
	CHECK ((int) catalog.functions ().size () == functions);
	// The engine's numbers of arguments, one for each function.
	static const int counts[] = {
#include "functionparams.h"
	};
	CHECK ((int) (sizeof (counts) / sizeof (counts[0])) == functions);
	CHECK (catalog.constant ("NORTHEAST") && catalog.constant ("NORTHEAST")->value == "45");
	CHECK (catalog.constant ("dequeue") && catalog.constant ("dequeue")->isFunctionName ());
	CHECK (! catalog.function ("nothing") && ! catalog.constant ("nothing"));
}

// The docs of the built-ins, from the help.
static void testScriptCatalog () {
	const ScriptCatalog & catalog = ScriptCatalog::get ();
	const BuiltinFunction * f = catalog.function ("addCharacter");
	CHECK (f && f->page == "addCharacter.html" && f->signatures.size () == 1);
	CHECK (f && f->signatures[0].parameters.size () == 4 && f->signatures[0].parameters[3] == "thisCostume");
	CHECK (f && f->accepts (4) && ! f->accepts (3) && f->purpose.compare (0, 12, "Adds a chara") == 0 && f->returns == "No return value.");
	f = catalog.function ("anim");
	CHECK (f && f->signatures[0].minArguments == 2 && f->signatures[0].maxArguments == -1 && f->arguments == -1 && f->accepts (9));
	// The numbers of the engine, where the help is wrong.
	f = catalog.function ("howFrozen");
	CHECK (f && f->arguments == 0 && f->accepts (0) && ! f->accepts (1));
	f = catalog.function ("loadClip");
	CHECK (f && f->arguments == 1);
	f = catalog.function ("moveCharacter");
	CHECK (f && f->signatures.size () == 2 && f->arguments == -1);
	CHECK (catalog.function ("setCharacterColourise") && ! catalog.function ("setCharacterColourise")->purpose.empty ());
	CHECK (catalog.functionCalled ("setStatusColor") == catalog.function ("setStatusColour"));
	const BuiltinConstant * c = catalog.constant ("NORTH");
	CHECK (c && c->group == "Directions" && c->about.find ("degrees") != std::string::npos);
	CHECK (catalog.constant ("PART_MODULATE_R") && catalog.constant ("PART_MODULATE_R")->group == "Parts of characters");
	Signature s = Signature::parse ("newClip (length [, loop])");
	CHECK (s.minArguments == 1 && s.maxArguments == 2 && s.parameters.size () == 1 && s.parameters[0] == "length [, loop]");
	s = Signature::parse ("onKeyboard ()");
	CHECK (s.parameters.empty () && s.accepts (0) && ! s.accepts (1));
	CHECK (ScriptCatalog::keywordDoc ("walkSpeed") && ScriptCatalog::keywordDoc ("sub"));
}

// The names of a project, read from its scripts.
static void testScriptIndex () {
	ScriptIndex index;
	ProjectDocument project;
	std::string error;
	CHECK (project.load (examples + "VerbCoin/VerbCoin.slp", &error));
	for (size_t f = 0; f < project.files ().size (); f ++) {
		std::string name = project.files ()[f];
		if (extensionOf (name) == "slu" || extensionOf (name) == "sld") CHECK (index.updateFromDisk (project.fullPath (name)));
	}
	std::string ego = project.fullPath ("ego/ego.slu"), inventory = project.fullPath ("iface/inventory.slu");
	CHECK (index.has (ego) && index.has (inventory));
	CHECK (ScriptIndex::key ("/a/./b/../c\\d.slu") == "/a/c/d.slu");
	const ScriptSymbol * type = index.objectType ("ego");
	CHECK (type && type->detail == "Egor the ego" && type->at == TextPos (2, 11));
	CHECK (type && type->doc.find ("The main character.") == 0);
	std::vector<const ScriptSymbol *> found = index.find ("makeEgoAnim");
	CHECK (found.size () == 1 && found[0]->kind == SYMBOL_SUB && found[0]->at.line == 21);
	found = index.find ("currentInvItem");
	CHECK (found.size () == 1 && found[0]->kind == SYMBOL_VARIABLE && found[0]->detail == "NULL");
	CHECK (index.find ("icon").size () == 1);
	// Events, and where the code of one is.
	std::vector<const ScriptSymbol *> members = index.membersOf ("inventoryIcon");
	CHECK (members.size () == 2 && members[0]->kind == SYMBOL_EVENT && members[0]->detail == "arrowTarget");
	const ScriptScope * scope = index.scopeAt (inventory, TextPos (9, 4));
	CHECK (scope && scope->name == "event onlyAction" && scope->owner == "inventoryIcon");
	CHECK (scope && scope->locals.size () >= 4 && scope->locals[0].name == "rowsNeeded" && scope->locals[2].name == "y");
	CHECK (index.placeAt (inventory, TextPos (9, 4)) == PLACE_CODE);
	CHECK (index.placeAt (inventory, TextPos (6, 0)) == PLACE_OBJECT_TYPE);
	CHECK (index.placeAt (inventory, TextPos (4, 0)) == PLACE_TOP);
	CHECK (index.ownerAt (inventory, TextPos (6, 0)) == "inventoryIcon");
	CHECK (index.kindOf ("ego") == TOKEN_OBJECT_TYPE && index.kindOf ("invGo") == TOKEN_SUB && index.kindOf ("inventory") == TOKEN_VARIABLE);
	CHECK (index.kindOf ("nothing") == TOKEN_IDENTIFIER);

	// A script that is being typed: members, flags, definitions, and
	// what changes the names.
	TextBuffer text;
	text.setText ("flag HEAVY;\nobjectType coins (\"bag of \\\"coins\\\"\") {\n\tvar total = 10;\n\tflags SHINY, ROUND;\n"
		"\t# Adds coins.\n\tsub collect (num) {\n\t\ttotal += num;\n\t}\n}\nsub broken ( {\nsub after () {}\n");
	uint64_t before = index.version ();
	CHECK (index.update ("/tmp/coins.slu", text, 1) && ! index.update ("/tmp/coins.slu", text, 1));
	CHECK (index.version () != before);
	type = index.objectType ("coins");
	CHECK (type && type->detail == "bag of \\\"coins\\\"");
	members = index.membersOf ("coins");
	CHECK (members.size () == 2 && members[0]->name == "total" && members[1]->name == "collect" && members[1]->doc == "Adds coins.");
	CHECK (members.size () == 2 && members[1]->parameters.size () == 1 && members[1]->parameters[0] == "num");
	CHECK (index.kindOf ("HEAVY") == TOKEN_FLAG && index.kindOf ("SHINY") == TOKEN_FLAG && index.kindOf ("collect") == TOKEN_SUB);
	CHECK (index.find ("broken").size () == 1);
	text.setText ("ANSWER = 42\n# Not this = one\n");
	CHECK (index.update ("/tmp/answers.sld", text, 1));
	found = index.find ("ANSWER");
	CHECK (found.size () == 1 && found[0]->kind == SYMBOL_DEFINITION && found[0]->detail == "42" && index.find ("Not").empty ());
	before = index.version ();
	index.forget ("/tmp/answers.sld");
	CHECK (index.version () != before && index.kindOf ("ANSWER") == TOKEN_IDENTIFIER);
}

// Completion, signatures, hover and definitions.
static void testScriptAssist () {
	CHECK (assist::matchScore ("addC", "addCharacter") > assist::matchScore ("addc", "addCharacter"));
	CHECK (assist::matchScore ("aC", "addCharacter") > 0 && assist::matchScore ("aC", "addCharacter") < assist::matchScore ("ad", "addCharacter"));
	CHECK (assist::matchScore ("ss", "setScale") > 0 && assist::matchScore ("sxx", "setScale") == 0);
	CHECK (assist::matchScore ("adr", "addCharacter") > 0 && assist::matchScore ("dd", "addCharacter") == 0);

	ScriptIndex index;
	TextBuffer text;
	std::string file = "/tmp/game.slu";
	text.setText ("var score = 0;\n"
		"objectType ego (\"Ego\") {\n"
		"\tvar money = 5;\n"
		"\t# Walks somewhere.\n"
		"\tsub walk (x, y) {\n"
		"\t\tvar steps = x + y;\n"
		"\t\tst\n"
		"\t}\n"
		"\t\n"
		"}\n"
		"sub init () {\n"
		"\taddCharacter (ego, 10, \n"
		"\tego.\n"
		"\tego.walk (1, \n"
		"}\n"
		"\n");
	index.update (file, text, 1);

	CompletionList list = assist::complete (text, TextPos (6, 4), index, file, false);
	CHECK (list.from == TextPos (6, 2) && list.typed == "st");
	CHECK (! list.items.empty () && list.items[0].label == "steps");
	bool hasBuiltin = false, hasGlobal = false;
	for (size_t i = 0; i < list.items.size (); i ++) {
		if (list.items[i].label == "stopMusic") hasBuiltin = true;
		if (list.items[i].label == "score") hasGlobal = true;
	}
	CHECK (hasBuiltin && ! hasGlobal);
	list = assist::complete (text, TextPos (6, 3), index, file, false);
	for (size_t i = 0; i < list.items.size (); i ++) if (list.items[i].label == "score") hasGlobal = true;
	CHECK (hasGlobal && list.items[0].label == "steps");
	// After a dot, the members.
	list = assist::complete (text, TextPos (12, 5), index, file, false);
	CHECK (list.items.size () == 2 && list.items[0].label == "walk" && list.items[1].label == "money");
	// In an objectType, its elements; at the top, the declarations.
	list = assist::complete (text, TextPos (8, 1), index, file, true);
	bool hasWalkSpeed = false;
	for (size_t i = 0; i < list.items.size (); i ++) if (list.items[i].label == "walkSpeed") hasWalkSpeed = true;
	CHECK (hasWalkSpeed);
	list = assist::complete (text, TextPos (15, 0), index, file, true);
	CHECK (list.items.size () == 5);
	// Nothing in a string, nor before a word is started.
	CHECK (assist::complete (text, TextPos (1, 18), index, file, false).items.empty ());
	CHECK (assist::complete (text, TextPos (11, 1), index, file, false).items.empty ());

	SignatureHelp help;
	CHECK (assist::signatureAt (text, TextPos (11, 25), index, file, &help));
	CHECK (help.name == "addCharacter" && help.argument == 2 && help.signatures.size () == 1);
	CHECK (assist::signatureAt (text, TextPos (13, 14), index, file, &help));
	CHECK (help.name == "ego.walk" && help.argument == 1 && help.signatures[0].parameters.size () == 2 && help.doc == "Walks somewhere.");
	CHECK (! assist::signatureAt (text, TextPos (6, 4), index, file, &help));

	HoverInfo info;
	CHECK (assist::hoverAt (text, TextPos (11, 3), index, file, &info));
	CHECK (info.title == "addCharacter (thisObject, atX, atY, thisCostume)" && info.page == "addCharacter.html");
	CHECK (assist::hoverAt (text, TextPos (13, 7), index, file, &info));
	CHECK (info.title == "sub ego.walk (x, y)" && info.body == "Walks somewhere." && info.where == "game.slu, line 5");
	CHECK (info.range == TextSelection (TextPos (13, 5), TextPos (13, 9)));
	CHECK (assist::hoverAt (text, TextPos (5, 14), index, file, &info) && info.title == "x" && info.body == "A parameter of walk.");
	CHECK (assist::hoverAt (text, TextPos (0, 1), index, file, &info) && info.title == "var name = value;");
	CHECK (! assist::hoverAt (text, TextPos (1, 18), index, file, &info));

	Definition definition;
	CHECK (assist::definitionAt (text, TextPos (13, 6), index, file, &definition));
	CHECK (definition.file == file && definition.at == TextPos (4, 5));
	CHECK (assist::definitionAt (text, TextPos (11, 15), index, file, &definition) && definition.at == TextPos (1, 11));
	CHECK (! assist::definitionAt (text, TextPos (11, 3), index, file, &definition));
}

// The live checks say nothing of scripts that compile: those of the
// examples and of the old project.
static void testScriptAnalysisQuiet () {
	const char * projects[] = { "Welcome/Welcome.slp", "VerbCoin/VerbCoin.slp" };
	for (int p = 0; p < 2; p ++) {
		ProjectDocument project;
		std::string error;
		CHECK (project.load (examples + projects[p], &error));
		ScriptIndex index;
		std::vector<std::string> scripts;
		for (size_t f = 0; f < project.files ().size (); f ++) {
			std::string name = project.files ()[f];
			if (extensionOf (name) != "slu" && extensionOf (name) != "sld") continue;
			CHECK (index.updateFromDisk (project.fullPath (name)));
			scripts.push_back (project.fullPath (name));
		}
		for (size_t s = 0; s < scripts.size (); s ++) {
			std::string fileText;
			CHECK (ScriptDocument::readScript (scripts[s], &fileText, &error));
			TextBuffer text;
			text.setText (fileText);
			std::vector<Diagnostic> found = analyseScript (text, index, scripts[s], true);
			for (size_t d = 0; d < found.size (); d ++) {
				fprintf (stderr, "%s:%d:%d: %s\n", scripts[s].c_str (), found[d].start.line + 1, found[d].start.column + 1, found[d].message.c_str ());
			}
			CHECK (found.empty ());
		}
	}
	// The old project, in UTF-8.
	TextBuffer text;
	text.setText (toUTF8 (readFile (fixtures + "OldProject/init.slu"), CODEPAGE_WINDOWS_1252));
	ScriptIndex index;
	index.update (fixtures + "OldProject/init.slu", text, 1);
	std::vector<Diagnostic> found = analyseScript (text, index, fixtures + "OldProject/init.slu", true);
	for (size_t d = 0; d < found.size (); d ++) fprintf (stderr, "OldProject/init.slu:%d:%d: %s\n", found[d].start.line + 1, found[d].start.column + 1, found[d].message.c_str ());
	CHECK (found.empty ());
}

// An event with code is a sub of its objectType, objectType.event (), as
// the compiler makes it; an event = sub is none.
static void testScriptEventMembers () {
	TextBuffer text;
	ScriptIndex index;
	std::string file = "/tmp/events.slu";
	text.setText ("objectType select (\"\") {}\n"
		"objectType deselect (\"\") {}\n"
		"objectType pickUp (\"\") {\n"
		"\tevent deselect { }\n"
		"\tevent select = helper;\n"
		"}\n"
		"sub helper () {}\n"
		"sub init () {\n"
		"\tpickUp.deselect ();\n"
		"\tpickUp.select ();\n"
		"\tpickUp.deselect (1);\n"
		"\tpickUp.\n"
		"}\n");
	index.update (file, text, 1);
	std::vector<Diagnostic> found = analyseScript (text, index, file, true);
	for (size_t d = 0; d < found.size (); d ++) fprintf (stderr, "events.slu:%d:%d: %s\n", found[d].start.line + 1, found[d].start.column + 1, found[d].message.c_str ());
	bool select = false, count = false;
	for (size_t d = 0; d < found.size (); d ++) {
		// Nothing on the call of the event with code.
		CHECK (found[d].start.line != 8);
		if (found[d].start.line == 9 && found[d].message == "The objectType pickUp has no member 'select'.") select = true;
		if (found[d].start.line == 10 && found[d].message == "pickUp.deselect takes 0 arguments, not 1.") count = true;
	}
	CHECK (select && count);

	Definition definition;
	CHECK (assist::definitionAt (text, TextPos (8, 10), index, file, &definition) && definition.at.line == 3);
	CHECK (! assist::definitionAt (text, TextPos (9, 10), index, file, &definition));
	SignatureHelp help;
	CHECK (assist::signatureAt (text, TextPos (8, 18), index, file, &help) && help.name == "pickUp.deselect");

	CompletionList list = assist::complete (text, TextPos (11, 8), index, file, true);
	bool hasDeselect = false, hasSelect = false;
	for (size_t i = 0; i < list.items.size (); i ++) {
		if (list.items[i].label == "deselect") hasDeselect = true;
		if (list.items[i].label == "select") hasSelect = true;
	}
	CHECK (hasDeselect && ! hasSelect);
}

// Each mistake of tests/devkit/fixtures/mistakes.slu, where it is.
static void testScriptAnalysisMistakes () {
	std::string file = fixtures + "mistakes.slu", fileText, error;
	CHECK (ScriptDocument::readScript (file, &fileText, &error));
	TextBuffer text;
	text.setText (fileText);
	ScriptIndex index;
	index.update (file, text, 1);
	std::vector<Diagnostic> found = analyseScript (text, index, file, true);
	struct Expected { int line, column; Diagnostic::Severity severity; const char * message; };
	const Expected expected[] = {
		{ 3, 5, Diagnostic::ERROR, "'lives' is a variable already, on line 2." },
		{ 4, 5, Diagnostic::ERROR, "'pause' is a built-in function of SLUDGE: a project can't make it again." },
		{ 10, 8, Diagnostic::ERROR, "There is no objectType 'nobody' for this event." },
		{ 12, 15, Diagnostic::ERROR, "'nothing' isn't a sub, which the event would call." },
		{ 16, 6, Diagnostic::ERROR, "'a' is made twice in init, first on line 15." },
		{ 17, 6, Diagnostic::ERROR, "This ( has no )." },
		{ 17, 13, Diagnostic::ERROR, "This string has no end on its line." },
		{ 18, 2, Diagnostic::WARNING, "howFrozen takes 0 arguments, not 1." },
		{ 19, 7, Diagnostic::WARNING, "hero.spend takes 1 argument, not 0." },
		{ 20, 7, Diagnostic::ERROR, "The objectType hero has no member 'steal'." },
		{ 21, 2, Diagnostic::ERROR, "'jump' isn't a variable, sub, objectType, flag or built-in." },
		{ 22, 19, Diagnostic::ERROR, "'nobody' isn't a variable, sub, objectType, flag or built-in." },
		{ 23, 5, Diagnostic::ERROR, "This ( has no )." },
		{ 27, 18, Diagnostic::WARNING, "A ; is missing after this, so the compiler joins it with what comes next." },
		{ 29, 20, Diagnostic::WARNING, "A ; is missing after this, so the compiler joins it with what comes next." },
		{ 35, 12, Diagnostic::WARNING, "A ; is missing after this, so the compiler joins it with what comes next." },
	};
	size_t count = sizeof (expected) / sizeof (expected[0]);
	for (size_t d = 0; d < found.size (); d ++) {
		fprintf (stderr, "mistakes.slu:%d:%d: %s: %s\n", found[d].start.line + 1, found[d].start.column + 1,
			found[d].severity == Diagnostic::ERROR ? "error" : "warning", found[d].message.c_str ());
	}
	CHECK (found.size () == count);
	for (size_t e = 0; e < count && e < found.size (); e ++) {
		CHECK (found[e].start == TextPos (expected[e].line - 1, expected[e].column - 1));
		CHECK (found[e].severity == expected[e].severity);
		CHECK (found[e].message == expected[e].message);
	}
	// Without all of the project, the names of the project go unchecked.
	found = analyseScript (text, index, file, false);
	for (size_t d = 0; d < found.size (); d ++) CHECK (found[d].message.find ("isn't a variable") == std::string::npos);
}

// Find in Project: the scripts on disk, and an open one as it is typed.
static void testProjectSearch () {
	ProjectDocument project;
	std::string error;
	CHECK (project.load (examples + "VerbCoin/VerbCoin.slp", &error));
	std::vector<std::string> files;
	for (size_t f = 0; f < project.files ().size (); f ++) {
		if (extensionOf (project.files ()[f]) == "slu") files.push_back (project.fullPath (project.files ()[f]));
	}
	bool more;
	std::vector<std::string> unread;
	editing::FindOptions options;
	std::vector<SearchHit> hits = searchFiles (files, OpenText (), "makeEgoAnim", options, 100, &more, &unread);
	CHECK (hits.size () == 4 && ! more && unread.empty ());
	CHECK (hits.size () == 4 && fileNameOf (hits[0].file) == "ego.slu" && hits[0].at == TextPos (21, 4) && hits[0].length == 11);
	CHECK (hits.size () == 4 && hits[0].text == "sub makeEgoAnim () {");
	// The open ego.slu has no makeEgoAnim any more.
	TextBuffer typed;
	typed.setText ("sub makeHeroAnim () {}\n");
	std::string ego = project.fullPath ("ego/ego.slu");
	OpenText open = [&] (const std::string & path) -> const TextBuffer * { return path == ego ? &typed : NULL; };
	hits = searchFiles (files, open, "makeEgoAnim", options, 100, &more, &unread);
	CHECK (hits.size () == 3);
	hits = searchFiles (files, open, "makeEgoAnim", options, 2, &more, &unread);
	CHECK (hits.size () == 2 && more);
	// A script that isn't UTF-8 can't be read.
	files.push_back (fixtures + "OldProject/init.slu");
	hits = searchFiles (files, OpenText (), "zzz", options, 100, &more, &unread);
	CHECK (hits.empty () && unread.size () == 1);
}

static void testProjects () {
	// Both example projects read back as they were written. VerbCoin has
	// every setting, and comes out byte for byte; Welcome is older and
	// lacks some, which we write.
	const char * projects[] = { "Welcome/Welcome.slp", "VerbCoin/VerbCoin.slp" };
	for (int i = 0; i < 2; i ++) {
		std::string original = examples + projects[i];
		ProjectSettings settings, again;
		std::vector<std::string> files, filesAgain;
		std::string error;
		CHECK (ProjectDocument::readProject (original, &settings, &files, &error));
		std::string copy = folder + "copy.slp";
		CHECK (ProjectDocument::writeProject (copy, settings, files, &error));
		CHECK (ProjectDocument::readProject (copy, &again, &filesAgain, &error));
		CHECK (again == settings && filesAgain == files);
		if (i == 1) CHECK (readFile (copy) == readFile (original));
	}

	ProjectDocument project;
	std::string error;
	CHECK (project.load (examples + "VerbCoin/VerbCoin.slp", &error));
	CHECK (project.settings ().windowName == "SLUDGE verb coin example");
	CHECK (project.settings ().width == 640 && project.settings ().speed == 50 && ! project.settings ().fullScreen);
	CHECK (project.files ().size () == 12);
	CHECK (project.files ()[1] == "iface/arrow_icons.slu");
	CHECK (project.fullPath ("init.slu") == examples + "VerbCoin/init.slu");

	// Files outside, inside and next to the folder of the project.
	CHECK (project.relativePath (examples + "VerbCoin/ego/new.slu") == "ego/new.slu");
	CHECK (project.relativePath (examples + "Welcome/script.slu") == "../Welcome/script.slu");
	CHECK (project.relativePath (examples + "VerbCoin/init.slu") == "init.slu");

	// Changes, which undo.
	std::vector<std::string> add;
	add.push_back (examples + "VerbCoin/extra.slu");
	add.push_back (examples + "VerbCoin/init.slu");
	CHECK (project.addFiles (add) == 1);
	CHECK (project.files ().size () == 13 && project.files ()[12] == "extra.slu");
	project.moveFile (12, 0);
	CHECK (project.files ()[0] == "extra.slu" && project.files ()[1] == "init.slu");
	std::vector<int> remove;
	remove.push_back (0);
	remove.push_back (2);
	project.removeFiles (remove);
	CHECK (project.files ().size () == 11 && project.files ()[0] == "init.slu" && project.files ()[1] == "iface/coin_interface.slu");
	ProjectSettings changed = project.settings ();
	changed.windowName = "Changed";
	project.setSettings (changed);
	CHECK (project.isModified ());
	for (int i = 0; i < 4; i ++) project.commands ().undo ();
	CHECK (! project.isModified ());
	CHECK (project.files ().size () == 12 && project.settings ().windowName == "SLUDGE verb coin example");

	// The resources of the script of Welcome, which isn't in this project
	// but is next to it.
	std::vector<std::string> resources = project.resourcesOf ("../Welcome/script.slu");
	CHECK (resources.size () == 2 && resources[0] == "image.tga" && resources[1] == "tada.wav");
	CHECK (project.resourcesOf ("german.tra").empty ());
}

static void testLaunch () {
	std::vector<std::string> args = commandLine ("code --goto {file}:{line}", "/a b/c.slu", 12);
	CHECK (args.size () == 3 && args[0] == "code" && args[2] == "/a b/c.slu:12");
	args = commandLine ("\"/Applications/My Editor\" -n", "/x.slu", 0);
	CHECK (args.size () == 3 && args[0] == "/Applications/My Editor" && args[1] == "-n" && args[2] == "/x.slu");
	args = commandLine ("gedit +{line} {file}", "f.slu", 0);
	CHECK (args.size () == 3 && args[1] == "+1" && args[2] == "f.slu");
	CHECK (fileKind ("a/B.SLU") == FILE_SCRIPT && fileKind ("x.tga") == FILE_IMAGE);
	CHECK (fileKind ("x.ogg") == FILE_AUDIO && fileKind ("x.xm") == FILE_MODULE && fileKind ("x.flo") == FILE_DOCUMENT);
}

// Let a compile job read its fake compiler, for up to 5 seconds.
static void finish (CompileJob & job) {
	for (int i = 0; i < 500 && job.running (); i ++) {
		job.update ();
		if (job.running ()) std::this_thread::sleep_for (std::chrono::milliseconds (10));
	}
}

static void testCompileJob () {
	std::vector<std::string> fields = CompileJob::splitFields ("message\terror\ta\\tb.slu\t3\tline\\none\tback\\\\slash");
	CHECK (fields.size () == 6 && fields[2] == "a\tb.slu" && fields[4] == "line\none" && fields[5] == "back\\slash");

	CompileOptions options;
	options.verbose = false;
	std::vector<std::string> args = CompileJob::arguments ("sludge-compiler", "/p/x.slp", options);
	CHECK (args.size () == 5 && args[1] == "--machine" && args[2] == "--keep-slx" && args[3] == "--strip-debug-info" && args[4] == "/p/x.slp");

	// A compile with a warning and an error, from a fake compiler.
	FakePlatform platform;
	platform.processOutput =
		"task\tParsing\nprogress\t1\t0.5000\nfile\tscript.slu\n"
		"stats\t1\t2\t3\t4\t5\n"
		"message\twarning\tscript.slu\t3\tUnused\tWARNING: Unused (in script.slu: line 3)\n"
		"Something on stderr\n"
		"message\terror\tscript.slu\t7\tBroken\tERROR: Broken (in script.slu: line 7)\n"
		"done\t0\t\n";
	CompileJob job (platform);
	CHECK (job.start ("sludge-compiler", "/p/x.slp", CompileOptions ()));
	CHECK (platform.ranFolder == "/p/");
	finish (job);
	CHECK (job.finished && ! job.succeeded && ! job.cancelled);
	CHECK (job.task == "Parsing" && job.file == "script.slu" && job.progress[0] == 0.5);
	CHECK (job.functions == 1 && job.resources == 5);
	CHECK (job.messages.size () == 3 && job.errorCount == 1 && job.warningCount == 1);
	CHECK (job.messages[0].type == BuildMessage::WARNING && job.messages[0].line == 3 && job.messages[0].text == "Unused");
	CHECK (job.messages[1].type == BuildMessage::OUTPUT && job.messages[1].text == "Something on stderr");
	CHECK (job.messages[2].type == BuildMessage::ERROR && job.messages[2].file == "script.slu");

	// A compiler that crashes before it is done.
	platform.processOutput = "task\tParsing\n";
	platform.processExitCode = 139;
	CompileJob crash (platform);
	crash.start ("sludge-compiler", "/p/x.slp", CompileOptions ());
	finish (crash);
	CHECK (crash.finished && ! crash.succeeded);
	CHECK (! crash.messages.empty () && crash.messages.back ().type == BuildMessage::INTERNAL);

	// A compile that succeeds.
	platform.processOutput = "done\t1\t/p/Game.slg\n";
	platform.processExitCode = 0;
	CompileJob good (platform);
	good.start ("sludge-compiler", "/p/x.slp", CompileOptions ());
	finish (good);
	CHECK (good.succeeded && good.gameFile == "/p/Game.slg" && good.messages.empty ());
}

static void testCodePages () {
	// "Café" and "Łódź" in their code pages.
	CHECK (toUTF8 ("Caf\xE9", CODEPAGE_WINDOWS_1252) == "Caf\xC3\xA9");
	CHECK (toUTF8 ("\xA3\xF3" "d\x9F", CODEPAGE_WINDOWS_1250) == "\xC5\x81\xC3\xB3" "d\xC5\xBA");
	CHECK (toUTF8 ("\x80", CODEPAGE_WINDOWS_1252) == "\xE2\x82\xAC");
	// UTF-8 stays as it is.
	CHECK (toUTF8 ("Caf\xC3\xA9", CODEPAGE_WINDOWS_1252) == "Caf\xC3\xA9");
	CHECK (invalidUTF8Offset ("abc") == -1);
	CHECK (invalidUTF8Offset ("ab\xC3\xA9\xE9") == 4);
	CHECK (invalidUTF8Offset ("\xC3") == 0);
	CHECK (invalidUTF8Offset ("\xC0\x80") == 0);
}

// The project of the old Windows versions of SLUDGE: what we find, and
// that we can fix it.
static void testProjectCheck () {
	std::string copy = folder + "OldProject/";
	mkdirs (copy + "Pics");
	writeFile (copy + "Old.SLP", readFile (fixtures + "OldProject/Old.SLP"));
	writeFile (copy + "init.slu", readFile (fixtures + "OldProject/init.slu"));
	writeFile (copy + "Pics/IMAGE.TGA", readFile (fixtures + "OldProject/Pics/IMAGE.TGA"));

	bool found, caseDiffers;
	CHECK (nameOnDisk (copy, "pics\\image.tga", &found, &caseDiffers) == "Pics/IMAGE.TGA" && found && caseDiffers);
	CHECK (nameOnDisk (copy, "Pics/IMAGE.TGA", &found, &caseDiffers) == "Pics/IMAGE.TGA" && found && ! caseDiffers);
	nameOnDisk (copy, "Pics/missing.tga", &found, &caseDiffers);
	CHECK (! found);

	ProjectDocument project;
	std::string error;
	CHECK (project.load (copy + "Old.SLP", &error));
	std::vector<FileProblem> problems = checkProject (project);
	CHECK (problems.size () == 3);
	if (problems.size () == 3) {
		CHECK (problems[0].kind == FileProblem::WRONG_CASE && problems[0].inProject && problems[0].name == "INIT.SLU" && problems[0].onDisk == "init.slu");
		CHECK (problems[1].kind == FileProblem::NOT_UTF8 && problems[1].file == "init.slu" && problems[1].line == 3);
		CHECK (problems[2].kind == FileProblem::WRONG_CASE && problems[2].file == "init.slu" && problems[2].line == 2
			&& problems[2].name == "pics\\image.tga" && problems[2].onDisk == "Pics/IMAGE.TGA");
	}

	// The fixes: the case in the project and in the script, and the text.
	project.renameFile (0, "init.slu");
	CHECK (convertFileToUTF8 (copy + "init.slu", CODEPAGE_WINDOWS_1252, &error));
	CHECK (readFile (copy + "init.slu.orig") == readFile (fixtures + "OldProject/init.slu"));
	CHECK (fixNamesInScript (copy + "init.slu", problems, &error));
	std::string fixed = readFile (copy + "init.slu");
	CHECK (fixed.find ("'Pics\\IMAGE.TGA'") != std::string::npos);
	CHECK (fixed.find ("\"Caf\xC3\xA9\"") != std::string::npos);
	CHECK (checkProject (project).empty ());
}

static std::string withoutCR (const std::string & text) {
	std::string result;
	for (size_t i = 0; i < text.size (); i ++) {
		if (text[i] != '\r') result += text[i];
	}
	return result;
}

static FloorPolygon polygon (const int * xy, int count) {
	FloorPolygon p;
	for (int i = 0; i < count; i ++) p.push_back (FloorPoint (xy[i * 2], xy[i * 2 + 1]));
	return p;
}

static void testFloors () {
	// Loading reverses the polygons and their vertices, as it always did,
	// so a floor comes back as it was after two saves.
	Floor loaded;
	std::string error;
	CHECK (FloorDocument::readFloor (fixtures + "floor.flo", &loaded, &error));
	CHECK (loaded.polygons.size () == 3);
	CHECK (loaded.vertexCount () == 13);
	CHECK (loaded.concaveCount () == 1);		// The last one has a notch
	CHECK (FloorDocument::writeFloor (folder + "floor1.flo", loaded, &error));
	Floor again;
	CHECK (FloorDocument::readFloor (folder + "floor1.flo", &again, &error));
	CHECK (FloorDocument::writeFloor (folder + "floor2.flo", again, &error));
	// On Windows, floors have \r\n at the ends of lines, as they always had.
	CHECK (withoutCR (readFile (folder + "floor2.flo")) == withoutCR (readFile (fixtures + "floor.flo")));
	CHECK (! FloorDocument::readFloor (folder + "no such floor.flo", &again, &error));

	// Drawing a triangle, which closes on its first vertex.
	FakePlatform platform;
	FloorDocument floor (&platform);
	CHECK (! floor.drawing ());
	CHECK (floor.addVertex (10, 10) == FloorDocument::ADDED);
	CHECK (floor.drawing ());
	floor.addVertex (100, 10);
	floor.addVertex (100, 100);
	floor.addVertex (10, 10);
	CHECK (! floor.drawing ());
	CHECK (floor.floor ().polygons.size () == 1);
	CHECK (floor.floor ().vertexCount () == 3);
	CHECK (floor.commands ().undoLabel () == "Add Vertex");
	CHECK (floor.isModified ());

	// Snapping to a vertex near the cursor.
	int x = 98, y = 12;
	CHECK (floor.snap (&x, &y, 6) && x == 100 && y == 10);
	x = 90;
	y = 12;
	CHECK (! floor.snap (&x, &y, 6) && x == 90);

	// After a complete polygon, a click starts a new one; a vertex that it
	// has already, other than its first, isn't allowed.
	floor.addVertex (100, 10);
	CHECK (floor.floor ().polygons.size () == 2);
	floor.addVertex (200, 10);
	CHECK (floor.addVertex (200, 10) == FloorDocument::ALREADY_USED);
	floor.addVertex (200, 100);
	floor.addVertex (100, 100);
	floor.addVertex (100, 10);
	CHECK (floor.floor ().vertexCount () == 7);

	// Moving a vertex moves it in both polygons, and undoes as one.
	CHECK (floor.moveVertex (100, 10, 110, 20));
	const Floor & moved = floor.floor ();
	int found = 0;
	for (size_t p = 0; p < moved.polygons.size (); p ++) {
		for (size_t v = 0; v < moved.polygons[p].size (); v ++) {
			if (moved.polygons[p][v] == FloorPoint (110, 20)) found ++;
		}
	}
	CHECK (found == 3);		// Twice in the second polygon, where it starts and ends
	CHECK (floor.commands ().undoLabel () == "Move Vertex");
	floor.commands ().undo ();
	CHECK (floor.floor ().polygons[0][0] == FloorPoint (100, 10));
	floor.commands ().redo ();
	// A polygon that has both places can't take the move.
	CHECK (! floor.moveVertex (110, 20, 100, 100));

	// Splitting a line adds a vertex in its middle, on a straight line,
	// which leaves the polygon convex.
	int before = floor.floor ().vertexCount ();
	CHECK (floor.floor ().concaveCount () == 0);
	CHECK (floor.splitLine (10, 10, 110, 20));
	CHECK (floor.floor ().vertexCount () == before + 1);
	CHECK (floor.floor ().concaveCount () == 0);
	CHECK (! floor.needsFixing ());
	CHECK (! floor.splitLine (10, 10, 200, 100));
	CHECK (floor.deleteVertex (60, 15));
	CHECK (floor.floor ().vertexCount () == before);

	// Splitting a polygon along a line between two of its vertices.
	size_t polygons = floor.floor ().polygons.size ();
	CHECK (floor.splitFloor (200, 10, 100, 100));
	CHECK (floor.floor ().polygons.size () == polygons + 1);
	CHECK (floor.commands ().undoLabel () == "Split Floor");

	// While we draw a polygon, Backspace takes back its last vertex and Esc
	// the whole polygon, each as a command; neither touches a complete one.
	CHECK (! floor.removeLastVertex ());
	CHECK (! floor.cancelDrawing ());
	Floor complete = floor.floor ();
	floor.addVertex (300, 300);
	floor.addVertex (400, 300);
	floor.addVertex (400, 400);
	CHECK (floor.removeLastVertex ());
	CHECK (floor.floor ().polygons[0].size () == 2);
	CHECK (floor.floor ().polygons[0][0] == FloorPoint (400, 300));
	CHECK (floor.drawing ());
	CHECK (floor.commands ().undoLabel () == "Remove Vertex");
	CHECK (floor.cancelDrawing ());
	CHECK (floor.floor ().polygons[0].empty ());
	CHECK (! floor.drawing ());
	CHECK (floor.floor ().polygons.size () == complete.polygons.size () + 1);
	CHECK (floor.floor ().vertexCount () == complete.vertexCount ());
	floor.commands ().undo ();
	CHECK (floor.floor ().polygons[0].size () == 2);
	floor.commands ().redo ();
	// Taking back the only vertex leaves nothing drawn.
	floor.addVertex (300, 300);
	CHECK (floor.removeLastVertex ());
	CHECK (! floor.drawing ());

	// Undo takes it all back to an empty floor, and redo brings it again.
	Floor drawn = floor.floor ();
	while (floor.commands ().canUndo ()) floor.commands ().undo ();
	CHECK (floor.floor () == Floor ());
	CHECK (! floor.isModified ());
	while (floor.commands ().canRedo ()) floor.commands ().redo ();
	CHECK (floor.floor () == drawn);

	// Moving several vertices by an offset: in every polygon that has them,
	// as one command, unless one would land on another of its polygon.
	FloorDocument squares (&platform);
	const int left[] = { 0, 0, 100, 0, 100, 100, 0, 100, 0, 0 };
	const int right[] = { 100, 0, 200, 0, 200, 100, 100, 100, 100, 0 };
	Floor two;
	two.polygons[0] = polygon (left, 5);
	two.polygons.push_back (polygon (right, 5));
	squares.setFloor (two, "Draw");
	std::vector<FloorPoint> edge;
	edge.push_back (FloorPoint (100, 0));
	edge.push_back (FloorPoint (100, 100));
	CHECK (squares.moveVertices (edge, 10, 0));
	CHECK (squares.commands ().undoLabel () == "Move Vertices");
	CHECK (squares.floor ().polygons[0][1] == FloorPoint (110, 0));
	CHECK (squares.floor ().polygons[1][0] == FloorPoint (110, 0));
	CHECK (squares.floor ().polygons[1][4] == FloorPoint (110, 0));
	CHECK (squares.floor ().polygons[1][3] == FloorPoint (110, 100));
	CHECK (squares.floor ().vertexCount () == two.vertexCount ());
	CHECK (squares.movedFloor (edge, 0, 0) == squares.floor ());
	squares.commands ().undo ();
	CHECK (squares.floor () == two);
	std::vector<FloorPoint> corner (1, FloorPoint (200, 0));
	CHECK (! squares.moveVertices (corner, -100, 0));
	CHECK (squares.movedFloor (corner, -100, 0) == two);
	CHECK (squares.floor () == two);
	CHECK (! squares.moveVertices (edge, 0, 0));
	// All of the floor.
	std::vector<FloorPoint> all;
	for (int i = 0; i < 4; i ++) all.push_back (FloorPoint (left[i * 2], left[i * 2 + 1]));
	all.push_back (FloorPoint (200, 0));
	all.push_back (FloorPoint (200, 100));
	CHECK (squares.moveVertices (all, 5, 7));
	CHECK (squares.floor ().polygons[0][0] == FloorPoint (5, 7));
	CHECK (squares.floor ().polygons[1][2] == FloorPoint (205, 107));
	squares.commands ().undo ();
	CHECK (squares.floor () == two);
	// Deleting a corner of each square leaves two triangles, as one command.
	std::vector<FloorPoint> corners;
	corners.push_back (FloorPoint (0, 0));
	corners.push_back (FloorPoint (200, 100));
	CHECK (squares.deleteVertices (corners));
	CHECK (squares.commands ().undoLabel () == "Delete Vertices");
	CHECK (squares.floor ().polygons.size () == 2);
	CHECK (squares.floor ().vertexCount () == 6);
	squares.commands ().undo ();
	CHECK (squares.floor () == two);

	// A polygon that isn't convex: saving asks to fix it, and the fix is a
	// command.
	FloorDocument concave (&platform);
	const int notch[] = { 0, 0, 0, 100, 50, 50, 100, 100, 100, 0, 0, 0 };
	Floor f;
	f.polygons[0] = polygon (notch, 6);
	concave.setFloor (f, "Draw");
	CHECK (concave.floor ().concaveCount () == 1);
	CHECK (concave.needsFixing ());
	platform.answers.push_back (0);
	platform.questions.clear ();
	CHECK (concave.save (folder + "concave.flo", &error));
	CHECK (platform.questions.size () == 1);
	CHECK (concave.floor ().concaveCount () == 0);
	CHECK (concave.floor ().polygons.size () == 2);
	CHECK (concave.commands ().undoLabel () == "Fix Floor");
	CHECK (! concave.isModified ());
	Floor saved;
	CHECK (FloorDocument::readFloor (folder + "concave.flo", &saved, &error));
	CHECK (saved.polygons.size () == 2 && saved.concaveCount () == 0);
	// Undo brings the notch back, and saving it as it is asks again.
	concave.commands ().undo ();
	CHECK (concave.floor ().concaveCount () == 1);
	platform.answers.push_back (1);
	CHECK (concave.save (folder + "concave.flo", &error));
	CHECK (platform.questions.size () == 2);
	CHECK (concave.floor ().concaveCount () == 1);
	// A polygon that isn't complete yet isn't for fixing.
	FloorDocument unfinished (&platform);
	unfinished.addVertex (0, 0);
	unfinished.addVertex (100, 0);
	CHECK (! unfinished.needsFixing ());
	CHECK (unfinished.fixConcave ());
}

static void testZBuffers () {
	// Made from a picture, named after it, and changed, without undo.
	ZBufferDocument made;
	CHECK (made.empty ());
	std::string error;
	CHECK (! made.save (folder + "empty.zbu", &error) && ! error.empty ());
	CHECK (made.makeFromImage (fixtures + "zbuffer.tga"));
	CHECK (! made.empty ());
	CHECK (made.layers () > 1);
	CHECK (made.untitledName () == "zbuffer.zbu");
	CHECK (made.startFolder () == fixtures);
	CHECK (made.isModified ());
	CHECK (! made.commands ().canUndo ());
	int layers = made.layers ();
	int w = made.width (), h = made.height ();
	CHECK (w > 0 && h > 0);
	// Each layer starts at its lowest row, which is in it.
	for (int l = 1; l <= layers; l ++) {
		int y = made.layerY (l) - 1;
		bool found = false;
		for (int x = 0; x < w; x ++) found = found || made.layerAt (x, y) == l;
		CHECK (found);
	}
	CHECK (made.layerAt (-1, 0) == 0 && made.layerAt (w, 0) == 0);

	// The y of a layer is a command; the steps of a drag merge.
	int y1 = made.layerY (1);
	made.setLayerY (1, 10, 1);
	made.setLayerY (1, 20, 1);
	made.setLayerY (1, 30, 1);
	made.commands ().stopMerging ();
	CHECK (made.layerY (1) == 30);
	CHECK (made.commands ().undoLabel () == "Y of Layer 1");
	made.commands ().undo ();
	CHECK (made.layerY (1) == y1);
	CHECK (! made.commands ().canUndo ());
	made.commands ().redo ();
	CHECK (made.layerY (1) == 30);
	// Below the picture too, but not further.
	made.setLayerY (2, h * 5);
	CHECK (made.layerY (2) == h * 2);
	made.setLayerY (2, -4);
	CHECK (made.layerY (2) == 0);

	// Saved and opened again, as it was.
	CHECK (made.save (folder + "made.zbu", &error));
	CHECK (! made.isModified ());
	ZBufferDocument opened;
	CHECK (opened.load (folder + "made.zbu", &error));
	CHECK (opened.layers () == layers && opened.width () == w && opened.height () == h);
	for (int l = 1; l <= layers; l ++) CHECK (opened.layerY (l) == made.layerY (l));
	bool same = true;
	for (int y = 0; y < h; y ++) {
		for (int x = 0; x < w; x ++) same = same && opened.layerAt (x, y) == made.layerAt (x, y);
	}
	CHECK (same);
	CHECK (opened.untitledName () == "Untitled Z-Buffer.zbu");
	CHECK (! opened.load (fixtures + "floor.flo", &error));

	// The z-buffers of VerbCoin come back byte for byte, after the header:
	// theirs is the first version, of a 640 x 480 room, and we write the
	// second, which has the size.
	const char * rooms[] = { "VerbCoin/outside/room.zbu", "VerbCoin/inside/room.zbu" };
	for (int r = 0; r < 2; r ++) {
		ZBufferDocument room;
		CHECK (room.load (examples + rooms[r], &error));
		CHECK (room.width () == 640 && room.height () == 480);
		CHECK (room.save (folder + "room.zbu", &error));
		std::string saved = readFile (folder + "room.zbu"), original = readFile (examples + rooms[r]);
		CHECK (original.compare (0, 4, std::string ("Szb\0", 4)) == 0);
		CHECK (saved.compare (0, 8, std::string ("Szb\1\2\x80\1\xE0", 8)) == 0);
		CHECK (saved.substr (8) == original.substr (4));
	}

	// From a PNG: transparent and black pixels are the background, and
	// each other colour is a layer whose y is one past its lowest row.
	{
		const int pw = 8, ph = 6;
		std::vector<unsigned char> rgba (pw * ph * 4, 0);
		for (int y = 0; y < ph; y ++) {
			for (int x = 0; x < pw; x ++) {
				unsigned char * p = &rgba[(y * pw + x) * 4];
				if (y < 2 && x < 4) { p[0] = 255; p[3] = 255; }
				else if (y >= 2 && y < 5 && x >= 4) { p[2] = 255; p[3] = 255; }
				else if (y == 5) { p[3] = 255; }
			}
		}
		png_image png;
		memset (&png, 0, sizeof (png));
		png.version = PNG_IMAGE_VERSION;
		png.width = pw;
		png.height = ph;
		png.format = PNG_FORMAT_RGBA;
		CHECK (png_image_write_to_file (&png, (folder + "zbuffer.png").c_str (), 0, rgba.data (), 0, NULL));
		ZBufferDocument fromPNG;
		CHECK (fromPNG.makeFromImage (folder + "zbuffer.png"));
		CHECK (fromPNG.layers () == 2 && fromPNG.width () == pw && fromPNG.height () == ph);
		CHECK (fromPNG.layerY (1) == 2 && fromPNG.layerY (2) == 5);
		CHECK (fromPNG.layerAt (0, 0) == 1 && fromPNG.layerAt (7, 4) == 2);
		CHECK (fromPNG.layerAt (7, 0) == 0 && fromPNG.layerAt (0, 5) == 0);
		CHECK (fromPNG.untitledName () == "zbuffer.zbu");

		// Too many colours.
		std::vector<unsigned char> many (16 * 2 * 4, 0);
		for (int x = 0; x < 16; x ++) {
			many[x * 4] = (unsigned char) (x * 16 + 15);
			many[x * 4 + 3] = 255;
		}
		png.width = 16;
		png.height = 2;
		CHECK (png_image_write_to_file (&png, (folder + "many.png").c_str (), 0, many.data (), 0, NULL));
		ZBufferDocument tooMany;
		CHECK (! tooMany.makeFromImage (folder + "many.png"));
		CHECK (tooMany.empty ());
	}
}

// The pixels of a sprite, as RGBA.
static std::vector<unsigned char> pixelsOf (const SpriteBankDocument & bank, int index) {
	const sprite & single = bank.bank ().sprites[index];
	unsigned char * rgba = spritePixels (&bank.bank (), index);
	std::vector<unsigned char> pixels (rgba, rgba + (size_t) single.width * single.height * 4);
	delete [] rgba;
	return pixels;
}

// A picture of three letters, 4, 2 and 3 pixels wide, with empty columns
// between them, 6 high.
static void writeFontPicture (const std::string & file) {
	const int w = 12, h = 6;
	std::vector<unsigned char> rgba (w * h * 4, 0);
	const int columns[] = { 0, 1, 2, 3, 5, 6, 8, 9, 10 };
	for (int c = 0; c < 9; c ++) {
		for (int y = 0; y < h; y ++) {
			unsigned char * p = &rgba[(y * w + columns[c]) * 4];
			p[0] = 255;
			p[3] = 255;
		}
	}
	png_image png;
	memset (&png, 0, sizeof (png));
	png.version = PNG_IMAGE_VERSION;
	png.width = w;
	png.height = h;
	png.format = PNG_FORMAT_RGBA;
	png_image_write_to_file (&png, file.c_str (), 0, rgba.data (), 0, NULL);
}

// A sprite sheet of 3 x 2 cells of 4 x 5 pixels: in each cell but the
// last, a block of colour c + 1 from (1, 1) to (2, 3); the last cell is
// empty.
static void writeSheetPicture (const std::string & file) {
	const int w = 12, h = 10;
	std::vector<unsigned char> rgba (w * h * 4, 0);
	for (int c = 0; c < 5; c ++) {
		int cx = (c % 3) * 4, cy = (c / 3) * 5;
		for (int y = 1; y <= 3; y ++) {
			for (int x = 1; x <= 2; x ++) {
				unsigned char * p = &rgba[((cy + y) * w + cx + x) * 4];
				p[0] = (unsigned char) (40 * (c + 1));
				p[3] = 255;
			}
		}
	}
	png_image png;
	memset (&png, 0, sizeof (png));
	png.version = PNG_IMAGE_VERSION;
	png.width = w;
	png.height = h;
	png.format = PNG_FORMAT_RGBA;
	png_image_write_to_file (&png, file.c_str (), 0, rgba.data (), 0, NULL);
}

// The places from..from+howMany-1.
static std::vector<int> run (int from, int howMany) {
	std::vector<int> indices;
	for (int i = from; i < from + howMany; i ++) indices.push_back (i);
	return indices;
}

static void testSpriteBanks () {
	std::string error;
	// VerbCoin's banks, with palettes and without, come back byte for byte.
	const char * banks[] = { "VerbCoin/demofont.duc", "VerbCoin/nicupfontsmall.duc", "VerbCoin/ego/alien.duc",
		"VerbCoin/outside/duck.duc", "VerbCoin/iface/mushroom.duc", "VerbCoin/iface/mouse.duc" };
	for (int b = 0; b < 6; b ++) {
		SpriteBankDocument bank;
		CHECK (bank.load (examples + banks[b], &error));
		CHECK (bank.count () > 0);
		CHECK (bank.save (folder + "bank.duc", &error));
		CHECK (readFile (folder + "bank.duc") == readFile (examples + banks[b]));
	}

	// A new bank is 32-bit, and can't be saved empty.
	SpriteBankDocument bank;
	CHECK (bank.mode () == SpriteBankDocument::MODE_32BIT);
	CHECK (! bank.save (folder + "empty.duc", &error));

	// Inserting pictures, with the hotspot in the middle of the bottom row.
	std::vector<std::string> files (1, fixtures + "sprite.png");
	CHECK (bank.insertFiles (0, files) == 1);
	// (Copies: inserting moves the sprites.)
	const sprite first = bank.bank ().sprites[0];
	CHECK (bank.hotspot (0) == Hotspot (first.width / 2, first.height - 1));
	std::vector<unsigned char> picture = pixelsOf (bank, 0);
	files.push_back (fixtures + "sprite.png");
	CHECK (bank.insertFiles (0, files) == 2);
	CHECK (bank.count () == 3);
	CHECK (bank.commands ().undoLabel () == "Insert Sprites");
	bank.commands ().undo ();
	CHECK (bank.count () == 1);
	bank.commands ().redo ();
	CHECK (bank.count () == 3);
	CHECK (pixelsOf (bank, 2) == picture);
	// A file that isn't a picture inserts nothing.
	CHECK (bank.insertFiles (1, std::vector<std::string> (1, fixtures + "floor.flo")) == 0);
	CHECK (bank.count () == 3);

	// Hotspots: one, a drag that merges, a range, centred and at the base.
	bank.setHotspot (1, Hotspot (5, 6));
	CHECK (bank.hotspot (1) == Hotspot (5, 6));
	bank.setHotspot (1, Hotspot (7, 8), 1);
	bank.setHotspot (1, Hotspot (9, 10), 1);
	bank.commands ().stopMerging ();
	bank.commands ().undo ();
	CHECK (bank.hotspot (1) == Hotspot (5, 6));
	bank.setHotspots (run (0, 3), Hotspot (1, 2));
	CHECK (bank.hotspot (0) == Hotspot (1, 2) && bank.hotspot (2) == Hotspot (1, 2));
	bank.centreHotspots (run (0, 2));
	CHECK (bank.hotspot (0) == Hotspot (first.width / 2, first.height / 2));
	CHECK (bank.hotspot (2) == Hotspot (1, 2));
	bank.baseHotspots (run (2, 1));
	CHECK (bank.hotspot (2) == Hotspot (first.width / 2, first.height - 1));

	// Export and insert again, as it was.
	CHECK (bank.exportSprite (0, folder + "exported.png", &error));
	CHECK (bank.insertFiles (3, std::vector<std::string> (1, folder + "exported.png")) == 1);
	CHECK (pixelsOf (bank, 3) == picture);

	// Replacing keeps the hotspot, and undoes.
	Hotspot kept = bank.hotspot (1);
	CHECK (bank.replaceSprite (1, fixtures + "zbuffer.tga"));
	CHECK (bank.hotspot (1) == kept);
	CHECK (pixelsOf (bank, 1) != picture);
	bank.commands ().undo ();
	CHECK (pixelsOf (bank, 1) == picture);

	// Deleting a range, which undoes with the pictures and hotspots.
	Hotspot second = bank.hotspot (2);
	bank.deleteSprites (1, 2);
	CHECK (bank.count () == 2);
	CHECK (bank.commands ().undoLabel () == "Delete Sprites");
	bank.commands ().undo ();
	CHECK (bank.count () == 4);
	CHECK (bank.hotspot (2) == second);
	CHECK (pixelsOf (bank, 2) == picture);

	// Deleting sprites that aren't next to each other, which undoes too.
	std::vector<Hotspot> spots;
	for (int i = 0; i < 4; i ++) spots.push_back (bank.hotspot (i));
	std::vector<unsigned char> replaced = pixelsOf (bank, 1);
	std::vector<int> some;
	some.push_back (0);
	some.push_back (2);
	bank.deleteSprites (some);
	CHECK (bank.count () == 2);
	CHECK (bank.hotspot (0) == spots[1] && bank.hotspot (1) == spots[3]);
	CHECK (bank.commands ().undoLabel () == "Delete Sprites");
	bank.commands ().undo ();
	CHECK (bank.count () == 4);
	bool back = true;
	for (int i = 0; i < 4; i ++) back = back && bank.hotspot (i) == spots[i];
	CHECK (back);
	CHECK (pixelsOf (bank, 1) == replaced);

	// Moving sprites, down and up, and back with undo.
	for (int i = 0; i < 4; i ++) bank.setHotspot (i, Hotspot (i, i));
	CHECK (bank.moveSprites (some, 4) == 2);
	CHECK (bank.hotspot (0) == Hotspot (1, 1) && bank.hotspot (1) == Hotspot (3, 3));
	CHECK (bank.hotspot (2) == Hotspot (0, 0) && bank.hotspot (3) == Hotspot (2, 2));
	CHECK (bank.commands ().undoLabel () == "Move Sprites");
	CHECK (pixelsOf (bank, 0) == replaced);
	bank.commands ().undo ();
	bool order = true;
	for (int i = 0; i < 4; i ++) order = order && bank.hotspot (i) == Hotspot (i, i);
	CHECK (order);
	bank.commands ().redo ();
	CHECK (bank.hotspot (0) == Hotspot (1, 1));
	bank.commands ().undo ();
	CHECK (bank.moveSprites (std::vector<int> (1, 3), 0) == 0);
	CHECK (bank.hotspot (0) == Hotspot (3, 3) && bank.hotspot (1) == Hotspot (0, 0));
	CHECK (bank.commands ().undoLabel () == "Move Sprite");
	bank.commands ().undo ();
	// A move to where they are is no change, so it keeps the redo.
	CHECK (bank.commands ().canRedo ());
	CHECK (bank.moveSprites (run (1, 2), 1) == 1);
	CHECK (bank.moveSprites (run (1, 2), 3) == 1);
	CHECK (bank.commands ().canRedo ());
	CHECK (bank.hotspot (1) == Hotspot (1, 1));

	// A font: three letters and a space, after the other sprites.
	writeFontPicture (folder + "font.png");
	CHECK (bank.makeFont (4, folder + "font.png", 5) == 4);
	CHECK (bank.count () == 8);
	CHECK (bank.bank ().sprites[4].width == 4 && bank.bank ().sprites[5].width == 2 && bank.bank ().sprites[6].width == 3);
	CHECK (bank.bank ().sprites[7].width == 5 && bank.bank ().sprites[7].height == 1);
	CHECK (bank.commands ().undoLabel () == "Make Font");

	// A sprite sheet, cut into frames with their hotspots at the base of
	// their cells, the empty cell skipped or kept, and trimmed.
	writeSheetPicture (folder + "sheet.png");
	{
		SpriteBankDocument sheet;
		SheetLayout layout;
		layout.columns = 3;
		layout.rows = 2;
		layout.count = 6;
		CHECK (sheet.insertSheet (0, folder + "sheet.png", layout) == 5);
		CHECK (sheet.bank ().sprites[4].width == 4 && sheet.bank ().sprites[4].height == 5);
		CHECK (sheet.hotspot (0) == Hotspot (2, 4));
		CHECK (sheet.commands ().undoLabel () == "Import Sheet");
		sheet.commands ().undo ();
		CHECK (sheet.count () == 0);
		layout.skipEmpty = false;
		CHECK (sheet.insertSheet (0, folder + "sheet.png", layout) == 6);
		layout.count = 2;
		CHECK (sheet.insertSheet (6, folder + "sheet.png", layout) == 2);
		CHECK (sheet.count () == 8);
		// Trimmed to the block, which keeps its place around the hotspot.
		layout.count = 6;
		layout.trim = true;
		layout.skipEmpty = true;
		CHECK (sheet.insertSheet (0, folder + "sheet.png", layout) == 5);
		CHECK (sheet.bank ().sprites[1].width == 2 && sheet.bank ().sprites[1].height == 3);
		CHECK (sheet.hotspot (1) == Hotspot (1, 3));
		std::vector<unsigned char> block = pixelsOf (sheet, 1);
		bool solid = block.size () == 2 * 3 * 4;
		for (size_t p = 0; p + 3 < block.size (); p += 4) solid = solid && block[p] == 80 && block[p + 3] == 255;
		CHECK (solid);
		// Cells that aren't all the same size.
		int before = sheet.count ();
		layout.columns = 5;
		CHECK (sheet.insertSheet (0, folder + "sheet.png", layout) == 0);
		CHECK (sheet.count () == before);
	}

	// Saved and opened again, as it was.
	CHECK (bank.save (folder + "made.duc", &error));
	SpriteBankDocument opened;
	CHECK (opened.load (folder + "made.duc", &error));
	CHECK (opened.count () == bank.count ());
	bool same = true;
	for (int i = 0; i < bank.count (); i ++) {
		same = same && opened.hotspot (i) == bank.hotspot (i) && pixelsOf (opened, i) == pixelsOf (bank, i);
	}
	CHECK (same);

	// Undo everything, and the bank is empty again.
	while (bank.commands ().canUndo ()) bank.commands ().undo ();
	CHECK (bank.count () == 0);
	while (bank.commands ().canRedo ()) bank.commands ().redo ();
	CHECK (bank.count () == 8);

	// A palette bank: an open palette takes the colours of a TGA, which undo
	// takes back; it can't take PNG pictures or make fonts.
	SpriteBankDocument palette;
	CHECK (palette.canSetMode (SpriteBankDocument::MODE_OPEN_PALETTE));
	palette.setMode (SpriteBankDocument::MODE_OPEN_PALETTE);
	CHECK (palette.mode () == SpriteBankDocument::MODE_OPEN_PALETTE);
	CHECK (palette.insertFiles (0, std::vector<std::string> (1, fixtures + "zbuffer.tga")) == 1);
	int colours = palette.bank ().myPalette.total;
	CHECK (colours > 1);
	CHECK (palette.insertFiles (1, std::vector<std::string> (1, fixtures + "sprite.png")) == 0);
	CHECK (palette.makeFont (0, folder + "font.png", 5) == 0);
	CHECK (! palette.canSetMode (SpriteBankDocument::MODE_32BIT));
	CHECK (palette.canSetMode (SpriteBankDocument::MODE_LOCKED_PALETTE));
	std::vector<unsigned char> tga = pixelsOf (palette, 0);
	// Saved with its palette, and opened locked.
	CHECK (palette.save (folder + "palette.duc", &error));
	SpriteBankDocument reopened;
	CHECK (reopened.load (folder + "palette.duc", &error));
	CHECK (reopened.mode () == SpriteBankDocument::MODE_LOCKED_PALETTE);
	CHECK (pixelsOf (reopened, 0) == tga);
	palette.commands ().undo ();
	CHECK (palette.count () == 0);
	CHECK (palette.bank ().myPalette.total == 1);
	palette.commands ().redo ();
	CHECK (palette.bank ().myPalette.total == colours);

	// Converting to 32-bit keeps the pictures, and can't be undone.
	CHECK (palette.convertTo32 ());
	CHECK (palette.mode () == SpriteBankDocument::MODE_32BIT);
	CHECK (! palette.commands ().canUndo ());
	CHECK (palette.isModified ());
	CHECK (pixelsOf (palette, 0) == tga);
}

static void testTranslations () {
	std::string error;
	// VerbCoin's German comes back as it was (with \r\n on Windows).
	TranslationDocument german;
	CHECK (german.load (examples + "VerbCoin/german.tra", &error));
	CHECK (german.language ().id == 1 && german.language ().name == "Deutsch");
	CHECK (! german.lines ().empty ());
	CHECK (german.lines ()[0].original == "SLUDGE verb coin example");
	CHECK (german.lines ()[0].type == TranslationLine::TRANSLATED);
	CHECK (german.save (folder + "german.tra", &error));
	CHECK (withoutCR (readFile (folder + "german.tra")) == withoutCR (readFile (examples + "VerbCoin/german.tra")));

	// The three kinds of line, and what isn't a translation.
	writeFile (folder + "kinds.tra", "### SLUDGE Translation File ###\n\n[ID]\n7\n\n[NAME]\nNederlands\nnot this\n\n[DATA]\n"
		"Hello  \t  Hallo \nCaptain Kirk\nGoodbye\t*\t\n");
	TranslationDocument kinds;
	CHECK (kinds.load (folder + "kinds.tra", &error));
	CHECK (kinds.language ().id == 7 && kinds.language ().name == "Nederlands");
	CHECK (kinds.lines ().size () == 3);
	CHECK (kinds.lines ()[0].original == "Hello" && kinds.lines ()[0].translation == "Hallo");
	CHECK (kinds.lines ()[1].type == TranslationLine::NOT_NEEDED);
	CHECK (kinds.lines ()[2].type == TranslationLine::MISSING);
	CHECK (kinds.count (TranslationLine::MISSING) == 1);
	writeFile (folder + "wrong.tra", "Something else\n");
	CHECK (! kinds.load (folder + "wrong.tra", &error) && ! error.empty ());
	writeFile (folder + "latin1.tra", "### SLUDGE Translation File ###\n\n[DATA]\nCaf\xE9\n");
	CHECK (! kinds.load (folder + "latin1.tra", &error));
	CHECK (error.find ("Line 4") != std::string::npos);
	CHECK (kinds.lines ().size () == 3);

	// Translating, which undoes, and lines that need none.
	kinds.setTranslation (2, "Tot\tziens\n");
	CHECK (kinds.lines ()[2].type == TranslationLine::TRANSLATED && kinds.lines ()[2].translation == "Tot ziens");
	kinds.setTranslation (2, "");
	CHECK (kinds.lines ()[2].type == TranslationLine::MISSING);
	kinds.setNeedsTranslation (0, false);
	CHECK (kinds.lines ()[0].type == TranslationLine::NOT_NEEDED && kinds.lines ()[0].translation.empty ());
	kinds.commands ().undo ();
	CHECK (kinds.lines ()[0].translation == "Hallo");
	kinds.setNeedsTranslation (1, true);
	CHECK (kinds.lines ()[1].type == TranslationLine::MISSING);
	kinds.setTranslation (1, "Kapitein Kirk");
	Language dutch = kinds.language ();
	dutch.name = "Vlaams";
	kinds.setLanguage (dutch, 1);
	dutch.id = 8;
	kinds.setLanguage (dutch, 1);
	kinds.commands ().undo ();
	CHECK (kinds.language ().id == 7 && kinds.language ().name == "Nederlands");
	CHECK (kinds.isModified ());

	// What a view shows.
	TranslationLine shown;
	shown.original = "Open the Door";
	shown.translation = "Open de deur";
	shown.type = TranslationLine::TRANSLATED;
	CHECK (translationLineShown (shown, SHOW_ALL, ""));
	CHECK (translationLineShown (shown, SHOW_TRANSLATED, "door"));
	CHECK (translationLineShown (shown, SHOW_ALL, "DE DEUR"));
	CHECK (! translationLineShown (shown, SHOW_MISSING, ""));
	CHECK (! translationLineShown (shown, SHOW_ALL, "window"));

	// The strings of a script.
	std::vector<std::string> strings = TranslationDocument::stringsOfScript (
		"say (ego, \"  Hello there \");  # \"not this\"\n"
		"say (ego, \"A \\\"quote\\\" and a \\\\ backslash\");\r\n"
		"var a = \"\"; var b = \"   \"; # nothing\n"
		"say (ego, \"two\") + \"strings\";\n");
	CHECK (strings.size () == 4);
	if (strings.size () == 4) {
		CHECK (strings[0] == "Hello there");
		CHECK (strings[1] == "A \"quote\" and a \\ backslash");
		CHECK (strings[2] == "two" && strings[3] == "strings");
	}

	// Loading the strings of a project: the window name, the quit message
	// and the scripts, in any case of their names; the translations stay,
	// and strings that the project no longer has go.
	std::string project = folder + "Translated/";
	mkdirs (project + "Scripts");
	writeFile (project + "Scripts/Init.slu", "sub init () {\n\tsay (ego, \"Hello\");\n\tsay (ego, \"New one\");\n}\n");
	ProjectSettings settings;
	settings.windowName = "My Game";
	settings.quitMessage = "Quit?";
	CHECK (ProjectDocument::writeProject (project + "Game.slp", settings, std::vector<std::string> (1, "scripts/init.slu"), &error));
	TranslationDocument loaded;
	CHECK (loaded.load (folder + "kinds.tra", &error));
	StringsUpdate update = loaded.loadStrings (project + "Game.slp");
	CHECK (update.loaded && update.problems.empty ());
	CHECK (update.added == 3);		// My Game, Quit? and New one, but not Hello
	CHECK (update.removed == 2);		// Captain Kirk and Goodbye
	CHECK (loaded.lines ().size () == 4);
	CHECK (loaded.lines ()[0].original == "Hello" && loaded.lines ()[0].translation == "Hallo");
	CHECK (loaded.lines ()[1].original == "My Game" && loaded.lines ()[1].type == TranslationLine::MISSING);
	CHECK (loaded.lines ()[3].original == "New one");
	CHECK (loaded.commands ().undoLabel () == "Load Strings");
	update = loaded.loadStrings (project + "Game.slp");
	CHECK (update.added == 0 && update.removed == 0);
	loaded.commands ().undo ();
	CHECK (loaded.lines ().size () == 3);
	update = loaded.loadStrings (folder + "no such project.slp");
	CHECK (! update.loaded && ! update.problems.empty ());
}

static void testFileMemory () {
	std::string file = folder + "files.txt";
	remove (file.c_str ());
	{
		FileMemory memory (file);
		CHECK (memory.get ("/a/room.flo", "floor.backdrop").empty ());
		memory.set ("/a/room.flo", "floor.backdrop", "/a/room.png");
		memory.set ("/a/room.flo", "floor.colour", "1 0.5 0");
		memory.set ("/b/hall.flo", "floor.backdrop", "/b/hall\tpicture.tga");
		memory.set ("/a/room.flo", "floor.colour", "0 1 0");
	}
	FileMemory memory (file);
	CHECK (memory.get ("/a/room.flo", "floor.backdrop") == "/a/room.png");
	CHECK (memory.get ("/a/room.flo", "floor.colour") == "0 1 0");
	CHECK (memory.get ("/b/hall.flo", "floor.backdrop") == "/b/hallpicture.tga");
	memory.set ("/a/room.flo", "floor.backdrop", "");
	CHECK (memory.get ("/a/room.flo", "floor.backdrop").empty ());
	CHECK (memory.get ("/a/room.flo", "floor.colour") == "0 1 0");
}

static bool samePlace (const Transform2D & a, const Transform2D & b) {
	const float e = 1e-3f;
	return fabsf (a.a - b.a) < e && fabsf (a.b - b.b) < e && fabsf (a.c - b.c) < e && fabsf (a.d - b.d) < e
		&& fabsf (a.tx - b.tx) < e && fabsf (a.ty - b.ty) < e;
}

static void testRigs () {
	// A new rig is of the root only, with an identity of its own.
	RigDocument rig, other;
	CHECK (rig.rig ().parts.size () == 1 && rig.rig ().parts[0].name.empty ());
	CHECK (! rig.asset ().id.isNull () && rig.asset ().id != other.asset ().id);
	CHECK (rig.untitledName () == "Untitled Rig.rig" && ! rig.isModified ());
	CHECK (fileKind ("ego.RIG") == FILE_DOCUMENT);

	// Parts, by their IDs; each change undoes.
	std::string why;
	PartId root = rig.rig ().parts[0].id, body, arm, hand;
	CHECK (rig.addPart (root, "body", &body, why));
	CHECK (rig.addPart (body, "arm", &arm, why));
	CHECK (rig.addPart (arm, rig.freeName ("arm"), &hand, why));
	CHECK (rig.part (hand) && rig.part (hand)->name == "arm 2");
	CHECK (rig.isModified () && rig.commands ().undoLabel () == "Add Part");
	CHECK (! rig.addPart (body, "arm", NULL, why) && ! why.empty ());
	CHECK (! rig.addPart (12345, "nothing", NULL, why));
	rig.commands ().undo ();
	CHECK (! rig.part (hand) && rig.rig ().parts.size () == 3);
	rig.commands ().redo ();
	CHECK (rig.part (hand) && rig.part (hand)->name == "arm 2");

	CHECK (rig.renamePart (hand, "hand", why) && rig.part (hand)->name == "hand");
	CHECK (rig.commands ().undoLabel () == "Rename Part");
	CHECK (! rig.renamePart (hand, "body", why) && rig.part (hand)->name == "hand");
	CHECK (! rig.renamePart (root, "root", why));

	// The rest pose: the steps of a drag merge into one change.
	PartPose p = rig.part (arm)->rest;
	for (int i = 1; i <= 3; i ++) {
		p.x = 10.0f * i;
		p.rotation = 15.0f * i;
		rig.setRest (arm, p, 7);
	}
	rig.commands ().stopMerging ();
	CHECK (rig.part (arm)->rest.x == 30 && rig.part (arm)->rest.rotation == 45);
	CHECK (rig.commands ().undoLabel () == "Transform of arm");
	rig.commands ().undo ();
	CHECK (rig.part (arm)->rest.x == 0 && rig.part (arm)->rest.rotation == 0);
	rig.commands ().redo ();
	p = rig.part (arm)->rest;
	p.scaleX = p.scaleY = 1.5f;
	p.flip = true;
	rig.setRest (arm, p);
	p.x = NAN;
	rig.setRest (arm, p);
	CHECK (rig.part (arm)->rest.x == 30 && rig.part (arm)->rest.scaleX == 1.5f && rig.part (arm)->rest.flip);

	// The same pose again, as a click without a drag, changes nothing.
	size_t undoable = 0;
	while (rig.commands ().canUndo ()) { rig.commands ().undo (); undoable ++; }
	for (size_t i = 0; i < undoable; i ++) rig.commands ().redo ();
	rig.commands ().markSaved ();
	rig.setRest (arm, rig.part (arm)->rest);
	CHECK (! rig.isModified ());
	rig.setDepth (arm, -1);
	CHECK (rig.part (arm)->depth == -1 && rig.commands ().undoLabel () == "Depth of arm");

	// Sprites name their banks by path; a bank goes when no part uses it.
	rig.setSprite (body, "hd/body.duc", 3, false);
	rig.setSprite (arm, "hd/arms.duc", 1, false);
	rig.setSprite (hand, "hd/arms.duc", 4, true);
	CHECK (rig.asset ().banks.size () == 2);
	CHECK (rig.bankOf (rig.part (hand)->sprite) == "hd/arms.duc" && rig.part (hand)->sprite.number == 4 && rig.part (hand)->sprite.mirror);
	rig.setSprite (arm, "", 0, false);
	CHECK (rig.part (arm)->sprite.kind == SpriteRef::DEFAULT && rig.asset ().banks.size () == 2);
	rig.setSprite (hand, "", 0, false);
	CHECK (rig.asset ().banks.size () == 1 && rig.asset ().banks[0] == "hd/body.duc");
	rig.commands ().undo ();
	rig.commands ().undo ();
	CHECK (rig.asset ().banks.size () == 2 && rig.bankOf (rig.part (arm)->sprite) == "hd/arms.duc");

	// A pivot that keeps the part in place moves its origin, and what is
	// attached to it, so that nothing moves in the world.
	int armIndex = partWithId (rig.rig (), arm), handIndex = partWithId (rig.rig (), hand);
	Transform2D spriteBefore = restWorld (rig.rig (), armIndex) * Transform2D::translation (0, 0);
	Transform2D handBefore = restWorld (rig.rig (), handIndex);
	rig.setPivot (arm, 4, 20, true, 9);
	rig.setPivot (arm, 6, 40, true, 9);
	rig.commands ().stopMerging ();
	armIndex = partWithId (rig.rig (), arm);
	handIndex = partWithId (rig.rig (), hand);
	CHECK (rig.part (arm)->pivotX == 6 && rig.part (arm)->pivotY == 40);
	CHECK (samePlace (spriteBefore, restWorld (rig.rig (), armIndex) * Transform2D::translation (-6, -40)));
	CHECK (samePlace (handBefore, restWorld (rig.rig (), handIndex)));
	CHECK (rig.commands ().undoLabel () == "Pivot of arm");
	rig.commands ().undo ();
	CHECK (rig.part (arm)->pivotX == 0 && rig.part (arm)->rest.x == 30);
	rig.setPivot (arm, 0, 10, false);
	CHECK (rig.part (arm)->pivotY == 10 && rig.part (arm)->rest.x == 30);

	// Moving and removing parts, with what is attached to them.
	CHECK (rig.placePart (hand, body, 0, why));
	CHECK (rig.part (hand)->parent == partWithId (rig.rig (), body));
	CHECK (partChildren (rig.rig (), partWithId (rig.rig (), body))[0] == partWithId (rig.rig (), hand));
	CHECK (! rig.placePart (body, hand, 0, why));
	rig.commands ().undo ();
	CHECK (rig.part (hand)->parent == partWithId (rig.rig (), arm));
	CHECK (rig.removePart (arm, why) && ! rig.part (arm) && ! rig.part (hand) && rig.part (body));
	CHECK (rig.commands ().undoLabel () == "Removal of arm" && rig.asset ().banks.size () == 1);
	rig.commands ().undo ();
	CHECK (rig.part (arm) && rig.part (hand) && rig.asset ().banks.size () == 2);
	CHECK (! rig.removePart (root, why));

	// Saved and opened again, it is the same to the byte, identity and all.
	std::string error;
	CHECK (rig.save (folder + "ego.rig", &error));
	CHECK (! rig.isModified ());
	RigDocument opened;
	CHECK (opened.load (folder + "ego.rig", &error));
	CHECK (opened.asset ().id == rig.asset ().id && ! opened.isModified ());
	CHECK (opened.part (hand) && opened.part (hand)->name == "hand" && opened.part (arm)->pivotY == 10);
	CHECK (opened.save (folder + "again.rig", &error));
	CHECK (readFile (folder + "ego.rig") == readFile (folder + "again.rig"));
	CHECK (opened.asset ().id == rig.asset ().id);

	// What isn't a rig doesn't open, and leaves the document as it was.
	writeFile (folder + "nothing.rig", "not a rig");
	CHECK (! opened.load (folder + "nothing.rig", &error) && ! error.empty ());
	animformat::Asset clip;
	clip.kind = animformat::Asset::CLIP;
	clip.id = animformat::newAssetId ();
	animformat::Bytes bytes;
	CHECK (animformat::encodeAsset (clip, bytes, why));
	writeFile (folder + "clip.rig", std::string (bytes.begin (), bytes.end ()));
	CHECK (! opened.load (folder + "clip.rig", &error) && error.find ("clip") != std::string::npos);
	CHECK (opened.part (hand) && opened.asset ().id == rig.asset ().id);
}

static void testClips () {
	ClipDocument clip, other;
	std::string why, error;
	CHECK (clip.clip ().length == 48 && clip.clip ().loop && clip.clip ().tracks.empty ());
	CHECK (! clip.asset ().id.isNull () && clip.asset ().id != other.asset ().id);
	CHECK (clip.untitledName () == "Untitled Clip.clp" && ! clip.isModified ());
	CHECK (fileKind ("walk.clp") == FILE_DOCUMENT);

	// Tracks and keys.
	TrackId turn, eyes, sprite;
	CHECK (clip.addTrack ("head", CHANNEL_ROTATION, &turn, why));
	CHECK (! clip.addTrack ("head", CHANNEL_ROTATION, NULL, why));
	CHECK (clip.setKey (turn, 0, 10, EASING_INOUT, why));
	CHECK (clip.setKey (turn, 24, -10, EASING_LINEAR, why));
	CHECK (! clip.setKey (turn, 49, 0, EASING_LINEAR, why));
	CHECK (! clip.setKey (turn, 4, NAN, EASING_LINEAR, why));
	CHECK (clip.key (KeyRef (turn, 0)) && clip.key (KeyRef (turn, 0))->easing == EASING_INOUT);
	CHECK (clip.key (KeyRef (turn, 24))->value == -10);
	clip.commands ().undo ();
	CHECK (! clip.key (KeyRef (turn, 24)));
	clip.commands ().redo ();
	CHECK (clip.addTrack ("eyes", CHANNEL_VISIBLE, &eyes, why));
	CHECK (clip.setKey (eyes, 4, 5, EASING_INOUT, why));
	CHECK (clip.key (KeyRef (eyes, 4))->value == 1 && clip.key (KeyRef (eyes, 4))->easing == EASING_STEP);
	clip.setEasing (KeyRef (turn, 24), EASING_STEP);
	CHECK (clip.key (KeyRef (turn, 24))->easing == EASING_STEP && clip.commands ().undoLabel () == "Easing");

	// Sprites name their banks by path, and a bank goes when no key uses it.
	CHECK (clip.addTrack ("eyes", CHANNEL_SPRITE, &sprite, why));
	CHECK (clip.setSpriteKey (sprite, 0, "hd/eyes.duc", 3, false, false, why));
	CHECK (clip.setSpriteKey (sprite, 8, "", 0, false, false, why));
	CHECK (clip.setSpriteKey (sprite, 12, "", 0, false, true, why));
	CHECK (clip.asset ().banks.size () == 1 && clip.bankOf (clip.key (KeyRef (sprite, 0))->sprite) == "hd/eyes.duc");
	CHECK (clip.key (KeyRef (sprite, 8))->sprite.kind == SpriteRef::NONE && clip.key (KeyRef (sprite, 12))->sprite.kind == SpriteRef::DEFAULT);
	CHECK (! clip.setKey (sprite, 4, 1, EASING_STEP, why));
	clip.removeKeys (std::vector<KeyRef> (1, KeyRef (sprite, 0)));
	CHECK (clip.asset ().banks.empty ());
	clip.commands ().undo ();
	CHECK (clip.asset ().banks.size () == 1);

	// Moving keys keeps their values and easings; onto a key of the same
	// track, they replace it, and undo brings it back.
	CHECK (clip.setKey (turn, 40, 99, EASING_LINEAR, why));
	CHECK (clip.moveKeys (std::vector<KeyRef> (1, KeyRef (turn, 24)), 8, false, why));
	CHECK (! clip.key (KeyRef (turn, 24)) && clip.key (KeyRef (turn, 32))->value == -10 && clip.key (KeyRef (turn, 32))->easing == EASING_STEP);
	CHECK (clip.moveKeys (std::vector<KeyRef> (1, KeyRef (turn, 32)), 8, false, why));
	CHECK (clip.key (KeyRef (turn, 40))->value == -10 && clip.track (turn)->keys.size () == 2);
	clip.commands ().undo ();
	CHECK (clip.key (KeyRef (turn, 40))->value == 99 && clip.key (KeyRef (turn, 32))->value == -10);
	CHECK (clip.moveKeys (std::vector<KeyRef> (1, KeyRef (turn, 0)), 4, true, why));
	CHECK (clip.key (KeyRef (turn, 0)) && clip.key (KeyRef (turn, 4))->value == 10 && clip.key (KeyRef (turn, 4))->easing == EASING_INOUT);
	CHECK (clip.commands ().undoLabel () == "Copy Key");
	std::vector<KeyRef> both;
	both.push_back (KeyRef (turn, 0));
	both.push_back (KeyRef (eyes, 4));
	CHECK (! clip.moveKeys (both, -1, false, why) && clip.key (KeyRef (turn, 0)) && clip.key (KeyRef (eyes, 4)));
	CHECK (clip.moveKeys (both, 2, false, why) && clip.key (KeyRef (turn, 2)) && clip.key (KeyRef (eyes, 6)));
	CHECK (clip.commands ().undoLabel () == "Move Keys");
	clip.commands ().undo ();
	clip.removeKeys (both);
	CHECK (! clip.key (KeyRef (turn, 0)) && ! clip.key (KeyRef (eyes, 4)) && clip.commands ().undoLabel () == "Remove Keys");
	clip.commands ().undo ();
	CHECK (clip.key (KeyRef (turn, 0)) && clip.key (KeyRef (eyes, 4)));

	// Events, by the path of a sound or the name of a function.
	EventId step, call;
	CHECK (clip.addEvent (8, FrameEvent::SOUND, "sounds/step.wav", &step, why));
	CHECK (clip.addEvent (30, FrameEvent::FUNCTION, "footstep", &call, why));
	CHECK (! clip.addEvent (60, FrameEvent::SOUND, "step.wav", NULL, why));
	CHECK (! clip.addEvent (4, FrameEvent::FUNCTION, "", NULL, why));
	CHECK (clip.eventName (step) == "sounds/step.wav" && clip.eventName (call) == "footstep");
	CHECK (clip.setEvent (call, 36, FrameEvent::FUNCTION, "jump", why) && clip.event (call)->time == 36 && clip.eventName (call) == "jump");
	CHECK (! clip.setEvent (call, 49, FrameEvent::FUNCTION, "jump", why) && clip.event (call)->time == 36);
	clip.removeEvent (step);
	CHECK (! clip.event (step) && clip.eventName (call) == "jump");
	clip.commands ().undo ();
	CHECK (clip.eventName (step) == "sounds/step.wav");

	// Shorter, it doesn't drop keys and events past its end, unless told to.
	CHECK (clip.itemsPast (32) == 2);
	CHECK (! clip.setLength (32, false, why) && clip.clip ().length == 48 && why.find ("2 keys") == 0);
	CHECK (clip.setLength (32, true, why) && clip.clip ().length == 32 && clip.itemsPast (32) == 0);
	CHECK (! clip.key (KeyRef (turn, 40)) && ! clip.event (call));
	clip.commands ().undo ();
	CHECK (clip.clip ().length == 48 && clip.key (KeyRef (turn, 40)) && clip.event (call));
	CHECK (clip.setLength (96, false, why) && clip.clip ().length == 96);
	clip.setLoop (false);
	CHECK (! clip.clip ().loop);

	// Keying a part, as a drag on the canvas: the track if it has none, the
	// key at the tick, and one undo for the whole drag.
	CHECK (clip.autoKey ("arm", CHANNEL_ROTATION, 10, 5, why, 3));
	CHECK (clip.autoKey ("arm", CHANNEL_ROTATION, 10, 15, why, 3));
	CHECK (clip.autoKey ("arm", CHANNEL_ROTATION, 10, 25, why, 3));
	clip.commands ().stopMerging ();
	const Track * arm = clip.track ("arm", CHANNEL_ROTATION);
	CHECK (arm && arm->keys.size () == 1 && arm->keys[0].value == 25 && arm->keys[0].easing == EASING_LINEAR);
	clip.commands ().undo ();
	CHECK (! clip.track ("arm", CHANNEL_ROTATION));
	clip.commands ().redo ();
	arm = clip.track ("arm", CHANNEL_ROTATION);
	clip.setEasing (KeyRef (arm->id, 10), EASING_INOUT);
	CHECK (clip.autoKey ("arm", CHANNEL_ROTATION, 10, 30, why));
	arm = clip.track ("arm", CHANNEL_ROTATION);
	CHECK (arm->keys[0].value == 30 && arm->keys[0].easing == EASING_INOUT);
	CHECK (clip.autoKey ("arm", CHANNEL_ROTATION, 20, 40, why));
	CHECK (clip.autoKey ("arm", CHANNEL_ROTATION, 5, 0, why));
	arm = clip.track ("arm", CHANNEL_ROTATION);
	CHECK (arm->keys.size () == 3 && arm->keys[2].easing == EASING_INOUT && arm->keys[0].easing == EASING_LINEAR);
	CHECK (clip.track ("arm", CHANNEL_X) == NULL);
	CHECK (! clip.autoKey ("arm", CHANNEL_SPRITE, 5, 0, why));

	// Bound to a rig, it knows which of its parts the rig lacks.
	RigDocument rig;
	PartId head;
	CHECK (rig.addPart (rig.rig ().parts[0].id, "head", &head, why));
	clip.bindToRig (rig.asset ().id, "ego.rig");
	CHECK (clip.isBoundTo (rig.asset ().id) && clip.asset ().rigPath == "ego.rig");
	std::vector<std::string> missing = clip.missingParts (rig.rig ());
	CHECK (missing.size () == 2 && missing[0] == "arm" && missing[1] == "eyes");

	// Saved and opened again, the same to the byte.
	CHECK (clip.save (folder + "walk.clp", &error));
	ClipDocument opened;
	CHECK (opened.load (folder + "walk.clp", &error));
	CHECK (opened.asset ().id == clip.asset ().id && opened.isBoundTo (rig.asset ().id) && opened.eventName (call) == "jump");
	CHECK (opened.save (folder + "again.clp", &error));
	CHECK (readFile (folder + "walk.clp") == readFile (folder + "again.clp"));
	CHECK (rig.save (folder + "ego2.rig", &error));
	CHECK (! opened.load (folder + "ego2.rig", &error) && error.find ("rig") != std::string::npos);
}

// Renaming a part of a rig renames its tracks in the clips made for it, as
// one change.
static void testRenameAcross () {
	std::string why, error;
	RigDocument rig;
	PartId head, eyes;
	CHECK (rig.addPart (rig.rig ().parts[0].id, "head", &head, why));
	CHECK (rig.addPart (head, "eyes", &eyes, why));
	ClipDocument walk, blink;
	TrackId t;
	walk.bindToRig (rig.asset ().id, "ego.rig");
	CHECK (walk.addTrack ("head", CHANNEL_ROTATION, &t, why) && walk.setKey (t, 0, 5, EASING_LINEAR, why));
	CHECK (walk.addTrack ("head", CHANNEL_Y, &t, why));
	blink.bindToRig (rig.asset ().id, "ego.rig");
	CHECK (blink.addTrack ("eyes", CHANNEL_VISIBLE, &t, why));
	CHECK (blink.addTrack ("head", CHANNEL_X, &t, why));
	std::vector<ClipDocument *> clips;
	clips.push_back (&walk);
	clips.push_back (&blink);
	std::vector<ClipDocument *> open = clips;
	RigDocument::StillOpen stillOpen = [&open] (ClipDocument * c) { return std::find (open.begin (), open.end (), c) != open.end (); };

	std::vector<ClipDocument *> renamed;
	CHECK (rig.renamePart (head, "skull", clips, stillOpen, &renamed, why));
	CHECK (rig.part (head)->name == "skull" && renamed.size () == 2);
	CHECK (walk.track ("skull", CHANNEL_ROTATION) && walk.track ("skull", CHANNEL_Y) && ! walk.track ("head", CHANNEL_Y));
	CHECK (blink.track ("skull", CHANNEL_X) && blink.track ("eyes", CHANNEL_VISIBLE));
	CHECK (rig.commands ().undoLabel () == "Rename Part" && walk.commands ().undoLabel () == "Rename Part");

	// One undo, in the rig, takes all back, and redo does it again.
	rig.commands ().undo ();
	CHECK (rig.part (head)->name == "head" && walk.track ("head", CHANNEL_ROTATION) && blink.track ("head", CHANNEL_X));
	rig.commands ().redo ();
	CHECK (rig.part (head)->name == "skull" && walk.track ("skull", CHANNEL_ROTATION) && blink.track ("skull", CHANNEL_X));

	// A clip changed since, or closed, stays as it is.
	walk.setLoop (false);
	open.pop_back ();
	rig.commands ().undo ();
	CHECK (rig.part (head)->name == "head");
	CHECK (walk.track ("skull", CHANNEL_ROTATION) && blink.track ("skull", CHANNEL_X));
	open = clips;
	rig.commands ().redo ();

	// A clip with tracks of both names can't take it: nothing changes.
	CHECK (blink.addTrack ("face", CHANNEL_X, &t, why));
	CHECK (! rig.renamePart (head, "face", clips, stillOpen, &renamed, why) && why.find ("face") != std::string::npos);
	CHECK (rig.part (head)->name == "skull" && walk.track ("skull", CHANNEL_ROTATION));

	// How a part shows goes with its rename, in a clip that only locks it.
	ClipDocument shown;
	shown.bindToRig (rig.asset ().id, "ego.rig");
	shown.setLocked ("skull", true);
	clips.push_back (&shown);
	open = clips;
	CHECK (rig.renamePart (head, "cranium", clips, stillOpen, &renamed, why) && renamed.size () == 3);
	CHECK (shown.isLocked ("cranium") && ! shown.isLocked ("skull"));
	rig.commands ().undo ();
	CHECK (shown.isLocked ("skull") && ! shown.isLocked ("cranium"));
	rig.commands ().redo ();
	clips.pop_back ();
	open = clips;

	// The clips of a rig in a folder, and its folders.
	mkdirs (folder + "clips/more");
	CHECK (walk.save (folder + "clips/walk.clp", &error));
	CHECK (blink.save (folder + "clips/more/blink.clp", &error));
	ClipDocument unbound;
	CHECK (unbound.save (folder + "clips/other.clp", &error));
	std::vector<std::string> found = clipsOfRig (folder + "clips", rig.asset ().id);
	CHECK (found.size () == 2);
	CHECK (found.size () == 2 && found[0].find ("more/blink.clp") != std::string::npos && found[1].find ("walk.clp") != std::string::npos);
}

// The preview of the animation editor: time by the library's rules, events
// when playing, none when scrubbing.
static void testPreviewPlayer () {
	std::shared_ptr<ClipDefinition> clip = std::make_shared<ClipDefinition> ();
	clip->length = 10;
	clip->loop = true;
	std::string why;
	FrameEvent e;
	e.kind = FrameEvent::FUNCTION;
	CHECK (addEvent (* clip, 0, e, why) && addEvent (* clip, 5, e, why));
	PreviewPlayer player;
	player.setClip (0, clip, LAYER_BLEND_OVERRIDE);

	// Paused, time doesn't move.
	CHECK (player.advance (1000, 20).empty () && player.ticks () == 0);
	// Playing at 20 ms a tick: 120 ms is 6 ticks, past the events at 0 and 5.
	player.play ();
	std::vector<const ClipEvent *> passed = player.advance (120, 20);
	CHECK (passed.size () == 2 && player.ticks () == 6);
	// Round the loop: from 6 to 12 passes 10 (the event at 0) only.
	passed = player.advance (120, 20);
	CHECK (passed.size () == 1 && passed[0]->time == 0 && player.ticks () == 2);
	// Half speed: 120 ms is 3 ticks; the keys stay where they are.
	player.setSpeed (50);
	passed = player.advance (120, 20);
	CHECK (player.ticks () == 5 && passed.size () == 1 && passed[0]->time == 5);
	player.setSpeed (100);

	// Scrubbing puts time somewhere, and passes nothing.
	player.pause ();
	player.scrub (8);
	CHECK (player.ticks () == 8 && player.advance (100, 20).empty ());
	// Scrubbed to the end of a loop, the playhead stays there while paused,
	// and goes round from there when it plays.
	player.scrub (10);
	CHECK (player.ticks () == 10);
	player.play ();
	player.advance (40, 20);
	CHECK (player.ticks () == 2);
	player.pause ();

	// A clip that doesn't loop stops at its end, and plays again from 0.
	clip->loop = false;
	player.scrub (0);
	player.play ();
	player.advance (1000, 20);
	CHECK (! player.playing () && player.ticks () == 10);
	player.play ();
	CHECK (player.playing () && player.ticks () == 0);

	// A second clip, which starts again with reset.
	std::shared_ptr<ClipDefinition> second = std::make_shared<ClipDefinition> ();
	second->length = 4;
	second->loop = true;
	player.setClip (1, second, LAYER_BLEND_ADD);
	player.advance (100, 20);
	CHECK (player.time (1) == ticksToTime (1));
	std::vector<LayerSamples> layers;
	std::vector<TrackSamples> samples;
	PoseBinding bindings[2];
	player.layers (layers, samples, bindings);
	CHECK (layers.size () == 2 && layers[1].blend == LAYER_BLEND_ADD);
	player.reset ();
	CHECK (player.ticks () == 0 && player.time (1) == 0);
}

// How the editors show parts: locks and uniform scales, as commands, saved
// with the documents.
static void testEditorParts () {
	std::string why, error;
	RigDocument rig;
	PartId arm;
	CHECK (rig.addPart (rig.rig ().parts[0].id, "arm", &arm, why));
	CHECK (! rig.isLocked (arm) && rig.scalesUniformly (arm));
	rig.setLocked (arm, true);
	CHECK (rig.isLocked (arm) && rig.commands ().undoLabel () == "Lock of arm" && rig.isModified ());
	rig.setLocked (rig.rig ().parts[0].id, true);
	CHECK (rig.isLocked (rig.rig ().parts[0].id) && rig.commands ().undoLabel () == "Lock of the Character");
	rig.commands ().undo ();
	CHECK (! rig.isLocked (rig.rig ().parts[0].id) && rig.isLocked (arm));

	PartPose p = rig.part (arm)->rest;
	p.scaleX = 1.5f;
	rig.setUniformScale (arm, false);
	rig.setRest (arm, p);
	p.scaleY = 0.5f;
	rig.setRest (arm, p);
	CHECK (! rig.scalesUniformly (arm) && rig.part (arm)->rest.scaleY == 0.5f);
	rig.setUniformScale (arm, true);
	CHECK (rig.scalesUniformly (arm) && rig.part (arm)->rest.scaleY == 1.5f);
	rig.commands ().undo ();
	CHECK (! rig.scalesUniformly (arm) && rig.part (arm)->rest.scaleY == 0.5f);

	CHECK (rig.save (folder + "shown.rig", &error));
	RigDocument back;
	CHECK (back.load (folder + "shown.rig", &error));
	CHECK (back.isLocked (arm) && ! back.scalesUniformly (arm) && back.part (arm)->rest.scaleY == 0.5f);

	// Removed, a part's state isn't saved.
	CHECK (back.removePart (arm, why) && back.save (folder + "shown.rig", &error));
	RigDocument plain;
	CHECK (plain.load (folder + "shown.rig", &error) && plain.asset ().editor.empty ());

	ClipDocument clip;
	clip.setLocked ("arm", true);
	clip.setScaling ("arm", animformat::EditorPart::SCALING_FREE);
	CHECK (clip.isLocked ("arm") && clip.scaling ("arm") == animformat::EditorPart::SCALING_FREE && clip.scaling ("leg") == animformat::EditorPart::SCALING_DEFAULT);
	clip.commands ().undo ();
	CHECK (clip.scaling ("arm") == animformat::EditorPart::SCALING_DEFAULT && clip.isLocked ("arm"));
	CHECK (clip.save (folder + "shown.clp", &error));
	ClipDocument clipBack;
	CHECK (clipBack.load (folder + "shown.clp", &error) && clipBack.isLocked ("arm"));
}

// Copy and paste of keys and events: at a tick, onto another part, growing
// the clip, as one change.
static void testClipPaste () {
	std::string why;
	ClipDocument clip;
	CHECK (clip.setLength (24, false, why));
	TrackId rot, sprite;
	CHECK (clip.addTrack ("arm", CHANNEL_ROTATION, &rot, why));
	CHECK (clip.setKey (rot, 2, 10, EASING_INOUT, why) && clip.setKey (rot, 6, 30, EASING_LINEAR, why));
	CHECK (clip.addTrack ("arm", CHANNEL_SPRITE, &sprite, why));
	CHECK (clip.setSpriteKey (sprite, 4, "ego/arm.duc", 3, true, false, why));
	EventId wave;
	CHECK (clip.addEvent (5, FrameEvent::FUNCTION, "wave", &wave, why));
	std::vector<KeyRef> keys;
	keys.push_back (KeyRef (rot, 2));
	keys.push_back (KeyRef (rot, 6));
	keys.push_back (KeyRef (sprite, 4));
	std::vector<ClipItem> items = clip.copy (keys, std::vector<EventId> (1, wave));
	CHECK (items.size () == 4 && items[2].bank == "ego/arm.duc" && items[2].number == 3 && items[2].mirror && items[3].name == "wave");

	// At tick 20: from 20 to 24, and the clip grows to 24 + 4 - ... = 24.
	std::vector<KeyRef> pasted;
	std::vector<EventId> pastedEvents;
	CHECK (clip.paste (items, 20, NULL, &pasted, &pastedEvents, why));
	CHECK (clip.clip ().length == 24 && pasted.size () == 3 && pastedEvents.size () == 1);
	CHECK (clip.key (KeyRef (rot, 20)) && clip.key (KeyRef (rot, 20))->value == 10 && clip.key (KeyRef (rot, 20))->easing == EASING_INOUT);
	CHECK (clip.key (KeyRef (rot, 24)) && clip.key (KeyRef (rot, 24))->value == 30);
	CHECK (clip.key (KeyRef (sprite, 22)) && clip.bankOf (clip.key (KeyRef (sprite, 22))->sprite) == "ego/arm.duc");
	CHECK (clip.event (pastedEvents[0]) && clip.event (pastedEvents[0])->time == 23 && clip.eventName (pastedEvents[0]) == "wave");

	// Past the end, the clip grows; one undo takes all of it back.
	CHECK (clip.paste (items, 30, NULL, &pasted, &pastedEvents, why));
	CHECK (clip.clip ().length == 34 && clip.key (KeyRef (rot, 34)) && clip.commands ().undoLabel () == "Paste 4 Items");
	clip.commands ().undo ();
	CHECK (clip.clip ().length == 24 && ! clip.key (KeyRef (rot, 30)));

	// Onto a key, it replaces it; onto another part, its tracks are made.
	CHECK (clip.setKey (rot, 10, 99, EASING_STEP, why));
	CHECK (clip.paste (items, 10, NULL, &pasted, &pastedEvents, why) && clip.key (KeyRef (rot, 10))->value == 10);
	std::string other = "leg";
	CHECK (clip.paste (items, 0, &other, &pasted, &pastedEvents, why));
	const Track * legRot = clip.track ("leg", CHANNEL_ROTATION);
	CHECK (legRot && clip.key (KeyRef (legRot->id, 0)) && clip.key (KeyRef (legRot->id, 4)) && clip.track ("leg", CHANNEL_SPRITE));
	CHECK (! clip.paste (std::vector<ClipItem> (), 0, NULL, &pasted, &pastedEvents, why));
}

// Compaction keeps each sprite on its own bank, whatever the order the
// banks are used in: here B, A, B, of the banks A, B and C.
static void testBankCompaction () {
	std::string why, error;
	animformat::Bytes bytes;

	RigDocument rig;
	PartId root = rig.rig ().parts[0].id, first, second, third;
	CHECK (rig.addPart (root, "first", &first, why) && rig.addPart (root, "second", &second, why) && rig.addPart (root, "third", &third, why));
	animformat::Asset a = rig.asset ();
	a.banks = std::vector<std::string> { "a.duc", "b.duc", "c.duc" };
	const PartId parts[3] = { first, second, third };
	const int banks[3] = { 1, 0, 1 };
	for (int i = 0; i < 3; i ++) {
		SpriteRef & s = a.rig.parts[partWithId (a.rig, parts[i])].sprite;
		s.kind = SpriteRef::SPRITE;
		s.bankFile = banks[i];
		s.number = i;
	}
	CHECK (animformat::encodeAsset (a, bytes, why));
	writeFile (folder + "order.rig", std::string (bytes.begin (), bytes.end ()));
	RigDocument rigBack;
	CHECK (rigBack.load (folder + "order.rig", &error));
	rigBack.setSprite (second, "a.duc", 5, false);
	CHECK (rigBack.asset ().banks == (std::vector<std::string> { "b.duc", "a.duc" }));
	CHECK (rigBack.bankOf (rigBack.part (first)->sprite) == "b.duc");
	CHECK (rigBack.bankOf (rigBack.part (second)->sprite) == "a.duc" && rigBack.part (second)->sprite.number == 5);
	CHECK (rigBack.bankOf (rigBack.part (third)->sprite) == "b.duc" && rigBack.part (third)->sprite.number == 2);

	ClipDocument clip;
	TrackId sprite;
	CHECK (clip.addTrack ("arm", CHANNEL_SPRITE, &sprite, why));
	CHECK (clip.setSpriteKey (sprite, 0, "b.duc", 0, false, false, why));
	CHECK (clip.setSpriteKey (sprite, 8, "a.duc", 1, false, false, why));
	CHECK (clip.setSpriteKey (sprite, 16, "b.duc", 2, false, false, why));
	a = clip.asset ();
	a.banks = std::vector<std::string> { "a.duc", "b.duc", "c.duc" };
	for (size_t k = 0; k < a.clip.tracks[0].keys.size (); k ++) a.clip.tracks[0].keys[k].sprite.bankFile = banks[k];
	CHECK (animformat::encodeAsset (a, bytes, why));
	writeFile (folder + "order.clp", std::string (bytes.begin (), bytes.end ()));
	ClipDocument clipBack;
	CHECK (clipBack.load (folder + "order.clp", &error));
	CHECK (clipBack.setSpriteKey (sprite, 8, "a.duc", 5, false, false, why));
	CHECK (clipBack.asset ().banks == (std::vector<std::string> { "b.duc", "a.duc" }));
	CHECK (clipBack.bankOf (clipBack.key (KeyRef (sprite, 0))->sprite) == "b.duc");
	CHECK (clipBack.bankOf (clipBack.key (KeyRef (sprite, 8))->sprite) == "a.duc");
	CHECK (clipBack.bankOf (clipBack.key (KeyRef (sprite, 16))->sprite) == "b.duc" && clipBack.key (KeyRef (sprite, 16))->sprite.number == 2);
}

// Colours: their four tracks added and removed as one change, keyed,
// eased, moved and pasted together, and a rig's rest colour saved.
static void testColourKeys () {
	std::string why, error;
	ClipDocument clip;
	CHECK (clip.setLength (24, false, why));
	std::vector<Channel> rgba { CHANNEL_MODULATE_R, CHANNEL_MODULATE_G, CHANNEL_MODULATE_B, CHANNEL_MODULATE_A };
	TrackId first;
	CHECK (clip.addTracks ("arm", rgba, &first, why) && first && clip.clip ().tracks.size () == 4);
	CHECK (clip.commands ().undoLabel () == "Add Track" && ! clip.addTracks ("arm", rgba, NULL, why));
	clip.commands ().undo ();
	CHECK (clip.clip ().tracks.empty ());
	clip.commands ().redo ();

	// Keyed together, as the editor's key button does.
	std::vector<KeyRef> keys;
	const float colour[4] = { 0.5f, 1, 1.5f, 0.25f };
	for (int c = 0; c < 4; c ++) {
		CHECK (clip.autoKey ("arm", rgba[c], 4, colour[c], why, 9));
		keys.push_back (KeyRef (clip.track ("arm", rgba[c])->id, 4));
	}
	clip.commands ().stopMerging ();
	CHECK (clip.key (keys[2])->value == 1.5f);
	CHECK (! clip.autoKey ("arm", CHANNEL_MODULATE_R, 8, 2.5f, why) && ! why.empty ());
	clip.setEasings (keys, EASING_INOUT);
	CHECK (clip.key (keys[0])->easing == EASING_INOUT && clip.key (keys[3])->easing == EASING_INOUT && clip.commands ().undoLabel () == "Easing");
	clip.commands ().undo ();
	CHECK (clip.key (keys[0])->easing == EASING_LINEAR && clip.key (keys[3])->easing == EASING_LINEAR);

	// Moved and pasted as one.
	CHECK (clip.moveKeys (keys, 6, false, why));
	std::vector<KeyRef> moved;
	for (size_t i = 0; i < keys.size (); i ++) moved.push_back (KeyRef (keys[i].track, 10));
	CHECK (clip.key (moved[1]) && ! clip.key (keys[1]));
	std::vector<ClipItem> items = clip.copy (moved, std::vector<EventId> ());
	std::string onto = "leg";
	std::vector<KeyRef> pasted;
	CHECK (clip.paste (items, 20, &onto, &pasted, NULL, why) && pasted.size () == 4);
	CHECK (clip.track ("leg", CHANNEL_MODULATE_A) && clip.key (KeyRef (clip.track ("leg", CHANNEL_MODULATE_A)->id, 20))->value == 0.25f);

	// Removed as one change, and saved as version 4.
	CHECK (clip.save (folder + "colour.clp", &error));
	ClipDocument back;
	CHECK (back.load (folder + "colour.clp", &error) && back.track ("arm", CHANNEL_MODULATE_B));
	std::vector<TrackId> legs;
	for (int c = 0; c < 4; c ++) legs.push_back (clip.track ("leg", rgba[c])->id);
	clip.removeTracks (legs);
	CHECK (! clip.track ("leg", CHANNEL_MODULATE_R) && clip.commands ().undoLabel () == "Remove Tracks");
	clip.commands ().undo ();
	CHECK (clip.track ("leg", CHANNEL_MODULATE_R) && clip.track ("leg", CHANNEL_MODULATE_A));

	RigDocument rig;
	PartId arm;
	CHECK (rig.addPart (rig.rig ().parts[0].id, "arm", &arm, why));
	PartPose rest = rig.part (arm)->rest;
	rest.selfModulate[1] = 0.5f;
	rest.modulate[3] = 2;
	rig.setRest (arm, rest);
	CHECK (rig.save (folder + "colour.rig", &error));
	RigDocument rigBack;
	CHECK (rigBack.load (folder + "colour.rig", &error));
	CHECK (rigBack.part (arm)->rest.selfModulate[1] == 0.5f && rigBack.part (arm)->rest.modulate[3] == 2);
}

static const Room * roomCalled (const std::vector<Room> & rooms, const std::string & name) {
	for (size_t r = 0; r < rooms.size (); r ++) {
		if (rooms[r].name == name) return &rooms[r];
	}
	return NULL;
}

static bool regionIs (const RoomRegion & region, const std::string & objectType, int x1, int y1, int x2, int y2, int sX, int sY, int direction) {
	std::vector<int> v = region.values ();
	return region.objectType == objectType && ! region.shape && region.editable && v.size () == 4
		&& v[0] == x1 && v[1] == y1 && v[2] == x2 && v[3] == y2
		&& region.standX.value == sX && region.standY.value == sY && region.direction.value == direction;
}

// Apply the changes of a tool, and return the text after them.
static std::string applied (ScriptDocument & script, const std::vector<TextChange> & changes) {
	script.edit (changes, script.selection (), EDIT_OTHER, "Test");
	return script.buffer ().text ();
}

// The room editor: what it reads of the rooms of a script, and the text
// that its tools write.
static void testRoomScript () {
	std::string error;
	ScriptDocument outside;
	CHECK (outside.load (examples + "VerbCoin/outside/room.slu", &error));
	std::vector<Room> rooms = readRooms (outside.buffer ());
	CHECK (rooms.size () == 1);
	const Room * room = roomCalled (rooms, "outsideRoom");
	CHECK (room != NULL);
	if (! room) return;
	CHECK (room->overlays.size () == 1 && room->overlays[0].file == "outside/room.tga"
		&& room->overlays[0].x.literal && room->overlays[0].x.value == 0 && room->overlays[0].y.value == 0 && ! room->overlays[0].mix);
	CHECK (room->floor == "outside/room.flo" && room->sceneWidth == 0);
	CHECK (room->regions.size () == 2);
	if (room->regions.size () != 2) return;
	CHECK (regionIs (room->regions[0], "notice", 140, 272, 165, 299, 140, 300, 0));
	CHECK (regionIs (room->regions[1], "exit", 52, 284, 96, 358, 83, 344, 270));
	CHECK (! room->regions[0].conditional && room->regions[0].direction.text == "NORTH");
	CHECK (room->regions[0].contains (140, 272) && room->regions[0].contains (165, 299) && ! room->regions[0].contains (166, 280));
	CHECK (room->regions[1].walks ());

	// Every tool, and undo takes the file back to what it was, byte for byte.
	std::string original = outside.buffer ().fileText ();
	const RoomRegion & notice = room->regions[0];
	CHECK (applied (outside, roomEdit::move (notice, 10, -2)).find ("addScreenRegion (notice, 150, 270, 175, 297, 140, 300, NORTH);") != std::string::npos);
	outside.commands ().undo ();
	CHECK (outside.buffer ().fileText () == original);
	CHECK (applied (outside, roomEdit::setRectangle (notice, 140, 272, 170, 299)).find ("(notice, 140, 272, 170, 299, 140, 300, NORTH)") != std::string::npos);
	outside.commands ().undo ();
	CHECK (applied (outside, roomEdit::setStandPoint (notice, -5, 310)).find ("(notice, 140, 272, 165, 299, -5, 310, NORTH)") != std::string::npos);
	outside.commands ().undo ();
	CHECK (applied (outside, roomEdit::setDirection (notice, -1)).find ("(notice, 140, 272, 165, 299, 140, 300, -1)") != std::string::npos);
	outside.commands ().undo ();
	CHECK (applied (outside, roomEdit::setDirection (notice, 135)).find ("140, 300, SOUTHEAST)") != std::string::npos);
	outside.commands ().undo ();
	CHECK (applied (outside, roomEdit::toShape (notice)).find ("(notice, newStack (140, 272, 165, 272, 165, 299, 140, 299), 140, 300, NORTH)") != std::string::npos);
	outside.commands ().undo ();
	std::string removed = applied (outside, roomEdit::remove (outside.buffer (), notice));
	CHECK (removed.find ("notice, 140") == std::string::npos && removed.find ("\tsetZBuffer ('outside/room.zbu');\n\n\taddScreenRegion (exit,") != std::string::npos);
	outside.commands ().undo ();
	CHECK (outside.buffer ().fileText () == original);

	// A new region goes after the last one, and a new objectType after the sub.
	TextPos where;
	std::vector<int> box;
	box.push_back (300); box.push_back (200); box.push_back (340); box.push_back (260);
	std::string added = applied (outside, roomEdit::add (outside.buffer (), *room, "\t", "bench", true, false, box, 320, 270, 180, &where));
	CHECK (added.find ("\taddScreenRegion (exit, 52, 284, 96, 358, 83, 344, WEST);\n\taddScreenRegion (bench, 300, 200, 340, 260, 320, 270, SOUTH);\n") != std::string::npos);
	CHECK (where.line == room->regions[1].call.line + 1 && where.column == 1);
	CHECK (outside.buffer ().line (where.line).substr (1, 15) == "addScreenRegion");
	CHECK (added.find ("\t}\n}\n\nobjectType bench (\"bench\") {\n}\n") != std::string::npos);
	std::vector<Room> after = readRooms (outside.buffer ());
	CHECK (after.size () == 1 && after[0].regions.size () == 3 && regionIs (after[0].regions[2], "bench", 300, 200, 340, 260, 320, 270, 180));
	outside.commands ().undo ();
	CHECK (outside.buffer ().fileText () == original);

	ScriptDocument inside;
	CHECK (inside.load (examples + "VerbCoin/inside/room.slu", &error));
	rooms = readRooms (inside.buffer ());
	CHECK (rooms.size () == 1 && rooms[0].name == "insideRoom" && rooms[0].regions.size () == 1
		&& regionIs (rooms[0].regions[0], "innerExit", 265, 83, 466, 352, 346, 348, 0));

	// The verb coin: regions of sums, which stay as they are.
	ScriptDocument coin;
	CHECK (coin.load (examples + "VerbCoin/iface/coin_interface.slu", &error));
	rooms = readRooms (coin.buffer ());
	int regions = 0;
	for (size_t r = 0; r < rooms.size (); r ++) {
		for (size_t g = 0; g < rooms[r].regions.size (); g ++) {
			const RoomRegion & region = rooms[r].regions[g];
			regions ++;
			CHECK (! region.editable && region.whyNot == "Not a number" && region.conditional);
			CHECK (roomEdit::move (region, 1, 1).empty ());
		}
	}
	CHECK (regions == 4);

	// Shapes, in a stack and in a fast array, and the init of a test.
	ScriptDocument shapes;
	CHECK (shapes.load (fixtures + "../../render/region.slu", &error));
	rooms = readRooms (shapes.buffer ());
	CHECK (rooms.size () == 1 && rooms[0].name == "init" && rooms[0].regions.size () == 3);
	if (rooms.size () == 1 && rooms[0].regions.size () == 3) {
		const RoomRegion & triangle = rooms[0].regions[0];
		const RoomRegion & ell = rooms[0].regions[1];
		CHECK (triangle.shape && triangle.editable && triangle.values ().size () == 6 && triangle.direction.value == 180);
		CHECK (triangle.contains (200, 150) && ! triangle.contains (120, 280));
		CHECK (ell.shape && ell.editable && ell.values ().size () == 12 && ell.direction.value == -1);
		CHECK (ell.contains (420, 250) && ! ell.contains (500, 200));
		CHECK (regionIs (rooms[0].regions[2], "box", 50, 400, 80, 440, 0, 0, -1) && ! rooms[0].regions[2].walks ());
		CHECK (rooms[0].overlays.size () == 1 && rooms[0].overlays[0].file == "outside/room.tga");
		std::vector<int> square;
		int corners[] = { 10, 10, 50, 10, 50, 50, 10, 50 };
		square.assign (corners, corners + 8);
		CHECK (applied (shapes, roomEdit::setPoints (ell, square)).find ("makeFastArray (newStack (10, 10, 50, 10, 50, 50, 10, 50)), 420, 320, -1)") != std::string::npos);
		shapes.commands ().undo ();
		CHECK (applied (shapes, roomEdit::move (triangle, 1, 2)).find ("newStack (101, 102, 301, 102, 201, 302), 200, 320, SOUTH)") != std::string::npos);
		shapes.commands ().undo ();
	}

	// Rooms that aren't plain: a variable's points, an if, a block, a sub
	// in an objectType, a new region in an empty room, a sub on one line.
	std::unique_ptr<ScriptDocument> script (scriptOf (
		"objectType thing (\"thing\") {\n"
		"\tsub nothing () { addScreenRegion (thing, 1, 2, 3, 4, 0, 0, -1); }\n"
		"}\n"
		"sub roomOne () {\n"
		"    var p = newStack (1, 1, 9, 1, 5, 9);\n"
		"    if (TRUE) addScreenRegion (thing, p, 0, 0, NORTH);\n"
		"    setSceneDimensions (1280, 960);\n"
		"    addOverlay ('a.png', CENTRE, 10);\n"
		"    if (lastRoom) {\n"
		"        addScreenRegion (thing, 1, 2, 3,\n"
		"            4, 0, 0, NORTH);\n"
		"    }\n"
		"    addCharacter (thing, 1, 2, costume ());\n"
		"}\n"
		"sub roomTwo () { setFloor ('b.flo'); }\n", TextPos ()));
	rooms = readRooms (script->buffer ());
	CHECK (rooms.size () == 2 && rooms[0].name == "roomOne" && rooms[1].name == "roomTwo");
	if (rooms.size () == 2) {
		const Room & one = rooms[0];
		CHECK (one.regions.size () == 2 && one.regions[0].shape && ! one.regions[0].editable && one.regions[0].whyNot == "Points from a variable");
		CHECK (one.regions[0].conditional && one.regions[1].conditional && one.regions[1].editable);
		CHECK (one.sceneWidth == 1280 && one.sceneHeight == 960);
		CHECK (one.overlays.size () == 1 && one.overlays[0].file == "a.png" && one.overlays[0].x.value == ROOM_CENTRE && one.overlays[0].y.value == 10);
		std::vector<int> triangle;
		int points[] = { 1, 2, 3, 4, 5, 6 };
		triangle.assign (points, points + 6);
		std::string text = applied (*script, roomEdit::add (script->buffer (), one, "    ", "thing", false, true, triangle, 0, 0, -1, &where));
		CHECK (text.find ("    addOverlay ('a.png', CENTRE, 10);\n    addScreenRegion (thing, newStack (1, 2, 3, 4, 5, 6), 0, 0, -1);\n    if (lastRoom)") != std::string::npos);
		script->commands ().undo ();
		CHECK (applied (*script, roomEdit::move (one.regions[1], 1, 1)).find ("(thing, 2, 3, 4,\n            5, 0, 0, NORTH)") != std::string::npos);
		script->commands ().undo ();
		text = applied (*script, roomEdit::add (script->buffer (), rooms[1], "    ", "door", true, false, box, 0, 0, -1, &where));
		CHECK (text.find ("sub roomTwo () { setFloor ('b.flo');\n    addScreenRegion (door, 300, 200, 340, 260, 0, 0, -1);\n }\n\nobjectType door (\"door\") {\n}") != std::string::npos);
	}
	// Overlays: added after the others, moved, and taken away.
	CHECK (outside.buffer ().fileText () == original);
	rooms = readRooms (outside.buffer ());
	if (rooms.size () == 1 && rooms[0].overlays.size () == 1) {
		const RoomOverlay & backdrop = rooms[0].overlays[0];
		CHECK (rooms[0].hasOverlayAnchor && rooms[0].overlayAnchorStart == backdrop.call && backdrop.movable ());
		std::string text = applied (outside, roomEdit::addOverlay (outside.buffer (), rooms[0], "\t", "iface/verbcoin.tga", 0, 0, &where));
		CHECK (text.find ("\taddOverlay ('outside/room.tga', 0, 0);\n\taddOverlay ('iface/verbcoin.tga', 0, 0);\n\tsetFloor") != std::string::npos);
		CHECK (where == TextPos (backdrop.call.line + 1, 1));
		std::vector<Room> two = readRooms (outside.buffer ());
		CHECK (two.size () == 1 && two[0].overlays.size () == 2 && two[0].overlays[1].file == "iface/verbcoin.tga" && two[0].overlays[1].call == where);
		outside.commands ().undo ();
		CHECK (applied (outside, roomEdit::moveOverlay (backdrop, 10, -20)).find ("addOverlay ('outside/room.tga', 10, -20);") != std::string::npos);
		outside.commands ().undo ();
		CHECK (roomEdit::moveOverlay (backdrop, 0, 0).empty ());
		text = applied (outside, roomEdit::remove (outside.buffer (), backdrop));
		CHECK (text.find ("room.tga") == std::string::npos && text.find ("\tstartMusic ('outside/music.xm', 0, 0);\n\tsetFloor") != std::string::npos);
		outside.commands ().undo ();
		CHECK (outside.buffer ().fileText () == original);
	}
	std::unique_ptr<ScriptDocument> overlays (scriptOf (
		"sub sized () {\n"
		"\tsetSceneDimensions (800, 600);\n"
		"\taddScreenRegion (thing, 1, 2, 3, 4, 0, 0, -1);\n"
		"}\n"
		"sub centred () {\n"
		"\tsetSceneDimensions (800, 600);\n"
		"\tif (x) addOverlay ('b.png', 0, 0);\n"
		"\taddOverlay ('a.png', CENTRE, 10);\n"
		"\tsetSceneDimensions (900, 700);\n"
		"}\n"
		"sub bare () { setFloor ('b.flo'); }\n", TextPos ()));
	rooms = readRooms (overlays->buffer ());
	CHECK (rooms.size () == 3);
	if (rooms.size () == 3) {
		std::string text = applied (*overlays, roomEdit::addOverlay (overlays->buffer (), rooms[0], "\t", "c.png", 5, 6, &where));
		CHECK (text.find ("\tsetSceneDimensions (800, 600);\n\taddOverlay ('c.png', 5, 6);\n\taddScreenRegion") != std::string::npos && where == TextPos (2, 1));
		overlays->commands ().undo ();
		// After the last overlay outside the if, not after the size that
		// comes later.
		CHECK (rooms[1].overlays.size () == 2 && rooms[1].overlays[0].conditional && ! rooms[1].overlays[1].conditional);
		text = applied (*overlays, roomEdit::addOverlay (overlays->buffer (), rooms[1], "\t", "c.png", 0, 0, &where));
		CHECK (text.find ("\taddOverlay ('a.png', CENTRE, 10);\n\taddOverlay ('c.png', 0, 0);\n\tsetSceneDimensions (900, 700);") != std::string::npos);
		overlays->commands ().undo ();
		CHECK (applied (*overlays, roomEdit::moveOverlay (rooms[1].overlays[1], 7, 10)).find ("addOverlay ('a.png', 7, 10);") != std::string::npos);
		overlays->commands ().undo ();
		text = applied (*overlays, roomEdit::addOverlay (overlays->buffer (), rooms[2], "    ", "c.png", 0, 0, &where));
		CHECK (text.find ("sub bare () {\n    addOverlay ('c.png', 0, 0);\n setFloor ('b.flo'); }") != std::string::npos);
		overlays->commands ().undo ();
	}

	// Characters: their places, costumes and scale, and the tools.
	CHECK (outside.buffer ().fileText () == original);
	rooms = readRooms (outside.buffer ());
	if (rooms.size () == 1 && rooms[0].characters.size () == 3) {
		const std::vector<RoomCharacter> & people = rooms[0].characters;
		const RoomCharacter & duck = people[0];
		CHECK (duck.objectType == "duck" && duck.x.value == 200 && duck.y.value == 350 && duck.movable () && ! duck.conditional);
		CHECK (duck.picture.known && duck.picture.bank == "outside/duck.duc" && duck.picture.frame == 0 && ! duck.picture.mirror);
		CHECK (duck.scale.set && duck.scale.horizon == 200 && duck.scale.divide == 150 && duck.scale.map.empty ());
		CHECK (fabsf (duck.scale.at (350) - 1.0f) < 0.001f);
		CHECK (people[1].objectType == "ego" && people[1].conditional && people[1].costumeSub == "makeEgoAnim" && ! people[1].picture.known);
		CHECK (people[2].objectType == "ego" && people[2].conditional && people[2].x.value == 420 && people[2].y.value == 400);
		CHECK (rooms[0].hasCharacterAnchor && rooms[0].characterAnchorStart == duck.call);

		std::string text = applied (outside, roomEdit::moveCharacter (duck, 250, 360));
		CHECK (text.find ("addCharacter (duck, 250, 360, costume (") != std::string::npos);
		outside.commands ().undo ();
		text = applied (outside, roomEdit::remove (outside.buffer (), duck));
		CHECK (text.find ("duck.duc") == std::string::npos && text.find ("WEST);\n\n\n\tif (lastRoom == insideRoom)") != std::string::npos);
		outside.commands ().undo ();
		text = applied (outside, roomEdit::addCharacter (outside.buffer (), rooms[0], "\t", "cat", true, 300, 400, "anim ('ego/alien.duc', 0)", &where));
		CHECK (text.find ("\t));\n\taddCharacter (cat, 300, 400, anim ('ego/alien.duc', 0));\n\n\tif (lastRoom") != std::string::npos);
		CHECK (text.find ("\n\nobjectType cat (\"cat\") {\n}") != std::string::npos);
		std::vector<Room> more = readRooms (outside.buffer ());
		CHECK (more.size () == 1 && more[0].characters.size () == 4 && more[0].characters[1].call == where && more[0].characters[1].picture.frame == 0);
		outside.commands ().undo ();
		CHECK (outside.buffer ().fileText () == original);
	} else {
		CHECK (! "the outside room has the duck and two egos");
	}

	// The costume of a sub: makeEgoAnim stands as sprite 4.
	ScriptDocument ego;
	CHECK (ego.load (examples + "VerbCoin/ego/ego.slu", &error));
	CostumePicture standing;
	CHECK (costumeOfSub (ego.buffer (), "makeEgoAnim", &standing) && standing.known && standing.bank == "ego/alien.duc" && standing.frame == 4);
	CHECK (! costumeOfSub (ego.buffer (), "noSuchSub", &standing));
	std::vector<std::string> scripts;
	scripts.push_back (examples + "VerbCoin/ego/ego.slu");
	scripts.push_back (examples + "VerbCoin/outside/room.slu");
	std::vector<std::string> subs = costumeSubs (scripts, [] (const std::string & file, TextBuffer * text) {
		std::string fileText, problem;
		if (! ScriptDocument::readScript (file, &fileText, &problem)) return false;
		text->setText (fileText);
		return true;
	});
	CHECK (std::find (subs.begin (), subs.end (), "makeEgoAnim") != subs.end () && std::find (subs.begin (), subs.end (), "outsideRoom") == subs.end ());

	// The scale map of the scale map test, and the setScale before it.
	ScriptDocument scaled;
	CHECK (scaled.load (fixtures + "../../render/scalemap.slu", &error));
	rooms = readRooms (scaled.buffer ());
	const Room * initRoom = roomCalled (rooms, "init");
	CHECK (initRoom && initRoom->characters.size () >= 2);
	if (initRoom && initRoom->characters.size () >= 2) {
		const RoomCharacter & first = initRoom->characters[0];
		const RoomCharacter & second = initRoom->characters[1];
		CHECK (first.scale.set && first.scale.horizon == 0 && first.scale.divide == 400 && first.scale.map.empty () && first.costumeSub == "egoCostume");
		CHECK (second.scale.map == "scalemap.png" && second.scale.mapMin == 20 && second.scale.mapMax == 120);
		CHECK (initRoom->scale.map.empty ());
	}

	// Costumes that wait, mirror, or can't be read.
	std::unique_ptr<ScriptDocument> costumes (scriptOf (
		"sub people () {\n"
		"\taddCharacter (a, 1, 2, anim ('a.duc', wait (3, 5), 4));\n"
		"\taddCharacter (b, 1, 2, costume (anim ('b.duc', 'step.wav', -2), NULL, NULL));\n"
		"\taddCharacter (c, 1, 2, loadClip ('c.clp'));\n"
		"\taddCharacter (d, x, 2, someCostume);\n"
		"\taddCharacter (e, 1, 2, costume (NULL, NULL, NULL));\n"
		"}\n", TextPos ()));
	rooms = readRooms (costumes->buffer ());
	CHECK (rooms.size () == 1 && rooms[0].characters.size () == 5);
	if (rooms.size () == 1 && rooms[0].characters.size () == 5) {
		const std::vector<RoomCharacter> & c = rooms[0].characters;
		CHECK (c[0].picture.known && c[0].picture.bank == "a.duc" && c[0].picture.frame == 3 && ! c[0].picture.mirror);
		CHECK (c[1].picture.known && c[1].picture.bank == "b.duc" && c[1].picture.frame == 2 && c[1].picture.mirror);
		CHECK (! c[2].picture.known && c[2].costumeSub.empty ());
		CHECK (! c[3].picture.known && c[3].costumeSub.empty () && ! c[3].movable ());
		CHECK (! c[4].picture.known);
		CHECK (! rooms[0].scale.set && fabsf (rooms[0].scale.at (100) - 1.0f) < 0.001f);
	}

	// Costumes in variables: of the costume sub, as an old example writes
	// it, and of the room.
	std::unique_ptr<ScriptDocument> variables (scriptOf (
		"sub makeEgoAnim () {\n"
		"\tvar standFrames =\tanim ('alien.duc', 4);\n"
		"\tvar walkFrames =\tanim ('alien.duc', wait (0, 2), wait (1, 2));\n"
		"\tvar backStandFrames =\tanim ('alien.duc', 11);\n"
		"\treturn costume (\n"
		"\t\tstandFrames, standFrames, backStandFrames, standFrames,\n"
		"\t\twalkFrames, walkFrames, walkFrames, walkFrames,\n"
		"\t\tstandFrames, standFrames, backStandFrames, standFrames);\n"
		"}\n"
		"sub held () {\n"
		"\tvar c = costume (anim ('a.duc', 1), NULL, NULL);\n"
		"\treturn c;\n"
		"}\n"
		"sub room () {\n"
		"\tvar ego = makeEgoAnim (), s = anim ('b.duc', 3);\n"
		"\taddCharacter (one, 1, 2, ego);\n"
		"\taddCharacter (two, 1, 2, costume (s, s, s));\n"
		"\ts = anim ('b.duc', 5);\n"
		"\taddCharacter (three, 1, 2, costume (s, s, s));\n"
		"\taddCharacter (four, 1, 2, nothing);\n"
		"\tvar s2 = s2;\n"
		"\taddCharacter (five, 1, 2, s2);\n"
		"}\n", TextPos ()));
	CostumePicture held;
	CHECK (costumeOfSub (variables->buffer (), "makeEgoAnim", &held) && held.known && held.bank == "alien.duc" && held.frame == 4);
	CHECK (costumeOfSub (variables->buffer (), "held", &held) && held.known && held.bank == "a.duc" && held.frame == 1);
	rooms = readRooms (variables->buffer ());
	const Room * withVariables = roomCalled (rooms, "room");
	CHECK (withVariables && withVariables->characters.size () == 5);
	if (withVariables && withVariables->characters.size () == 5) {
		const std::vector<RoomCharacter> & c = withVariables->characters;
		CHECK (! c[0].picture.known && c[0].costumeSub == "makeEgoAnim");
		CHECK (c[1].picture.known && c[1].picture.bank == "b.duc" && c[1].picture.frame == 3);
		CHECK (c[2].picture.known && c[2].picture.frame == 5);
		CHECK (! c[3].picture.known && c[3].costumeSub.empty ());
		CHECK (! c[4].picture.known);
	}
	std::vector<std::string> variableFiles (1, "variables.slu");
	std::vector<std::string> held2 = costumeSubs (variableFiles, [&variables] (const std::string &, TextBuffer * text) {
		*text = variables->buffer ();
		return true;
	});
	CHECK (held2.size () == 2 && held2[0] == "held" && held2[1] == "makeEgoAnim");

	// The settings of characters after their addCharacter.
	std::unique_ptr<ScriptDocument> settings (scriptOf (
		"sub room () {\n"
		"\taddCharacter (duck, 1, 2, anim ('d.duc', 0));\n"
		"\tsetCharacterExtra (duck, ICON + NOREMOVE);\n"
		"\tsetCharacterDrawMode (duck, TRANSPARENT);\n"
		"\tturnCharacter (duck, NORTH);\n"
		"\taddCharacter (ego, 3, 4, costume (anim ('e.duc', 1), anim ('e.duc', 2), anim ('e.duc', 3), anim ('e.duc', 4),\n"
		"\t\tNULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL));\n"
		"\tsetCharacterWalkSpeed (ego, speed);\n"
		"\tsetCharacterZ (ego, 40);\n"
		"\tturnCharacter (ego, WEST);\n"
		"\tsetCharacterDrawMode (duck, DARK1);\n"
		"\taddCharacter (duck, 5, 6, anim ('d.duc', 0));\n"
		"\tsetCharacterDrawMode (duck, 7);\n"
		"}\n", TextPos ()));
	std::string settingsText = settings->buffer ().text ();
	rooms = readRooms (settings->buffer ());
	CHECK (rooms.size () == 1 && rooms[0].characters.size () == 3);
	if (rooms.size () == 1 && rooms[0].characters.size () == 3) {
		const RoomCharacter & duck = rooms[0].characters[0];
		const RoomCharacter & ego = rooms[0].characters[1];
		const RoomCharacter & second = rooms[0].characters[2];
		CHECK (duck.extraFlags () == ROOM_ICON + ROOM_NOREMOVE && duck.drawModeValue () == 2 && duck.angle () == 0);
		CHECK (! duck.walkSpeed.present && duck.walkSpeedValue () == -1 && ! duck.hasDepth ());
		// The later DARK1 of the duck is a scene's, not its setting up.
		CHECK (duck.drawMode.value.text == "TRANSPARENT");
		CHECK (ego.walkSpeed.present && ! ego.walkSpeed.value.literal && ego.hasDepth () && ego.depth.value.value == 40 && ego.angle () == 270);
		// Four directions, facing west: the second standing one.
		CHECK (ego.picture.known && ego.picture.bank == "e.duc" && ego.picture.frame == 2);
		CHECK (second.drawModeValue () == 7 && second.call.line == 11);

		std::string text = applied (*settings, roomEdit::setDrawMode (settings->buffer (), rooms[0], duck, 15));
		CHECK (text.find ("setCharacterDrawMode (duck, GLOW1);\n\tturnCharacter (duck, NORTH);") != std::string::npos);
		settings->commands ().undo ();
		text = applied (*settings, roomEdit::setDrawMode (settings->buffer (), rooms[0], duck, 0));
		CHECK (text.find ("TRANSPARENT") == std::string::npos && text.find ("NOREMOVE);\n\tturnCharacter (duck, NORTH);") != std::string::npos);
		settings->commands ().undo ();
		// ICON only with all five of its flags.
		text = applied (*settings, roomEdit::setExtras (settings->buffer (), rooms[0], duck, ROOM_FRONT + ROOM_FIXEDSIZE + ROOM_RECTANGULAR));
		CHECK (text.find ("setCharacterExtra (duck, FRONT + FIXEDSIZE + RECTANGULAR);") != std::string::npos);
		settings->commands ().undo ();
		CHECK (extrasText (ROOM_ICON + ROOM_NOREMOVE) == "ICON + NOREMOVE" && extrasText (0) == "NORMAL" && extrasText (ROOM_ICON - ROOM_FRONT) == "FIXEDSIZE + NOZBUFFER + FIXTOSCREEN + NOLIGHTMAP");
		// A new setting goes after the last one of the character.
		text = applied (*settings, roomEdit::setWalkSpeed (settings->buffer (), rooms[0], duck, 3));
		CHECK (text.find ("\tturnCharacter (duck, NORTH);\n\tsetCharacterWalkSpeed (duck, 3);\n\taddCharacter (ego,") != std::string::npos);
		settings->commands ().undo ();
		text = applied (*settings, roomEdit::setDepth (settings->buffer (), rooms[0], duck, true, -20));
		CHECK (text.find ("\tturnCharacter (duck, NORTH);\n\tsetCharacterZ (duck, -20);\n") != std::string::npos);
		settings->commands ().undo ();
		text = applied (*settings, roomEdit::setFacing (settings->buffer (), rooms[0], duck, -1));
		CHECK (text.find ("turnCharacter (duck") == std::string::npos);
		settings->commands ().undo ();
		CHECK (applied (*settings, roomEdit::setFacing (settings->buffer (), rooms[0], duck, 135)).find ("turnCharacter (duck, SOUTHEAST);") != std::string::npos);
		settings->commands ().undo ();
		// A speed that isn't a number stays as it is.
		CHECK (roomEdit::setWalkSpeed (settings->buffer (), rooms[0], ego, 4).empty () && roomEdit::setWalkSpeed (settings->buffer (), rooms[0], ego, -1).empty ());
		text = applied (*settings, roomEdit::setDepth (settings->buffer (), rooms[0], ego, false, 0));
		CHECK (text.find ("setCharacterZ") == std::string::npos);
		settings->commands ().undo ();
		// After the setting that the second duck has.
		text = applied (*settings, roomEdit::setExtras (settings->buffer (), rooms[0], second, ROOM_ICON));
		CHECK (text.find ("\taddCharacter (duck, 5, 6, anim ('d.duc', 0));\n\tsetCharacterDrawMode (duck, 7);\n\tsetCharacterExtra (duck, ICON);") != std::string::npos);
		settings->commands ().undo ();
		CHECK (settings->buffer ().text () == settingsText);
	}
	CHECK (facingDirection (180, 4) == 0 && facingDirection (270, 4) == 1 && facingDirection (0, 4) == 2 && facingDirection (90, 4) == 3);
	CHECK (facingDirection (0, 2) == 1 && facingDirection (180, 1) == 0 && facingDirection (-90, 4) == 1);
	int constant;
	CHECK (constantValue ("GLOW", &constant) && constant == 16 && constantValue ("ICON", &constant) && constant == 31 && ! constantValue ("nothing", &constant));
	CHECK (std::string (drawModeName (5)) == "DARK2" && std::string (drawModeName (19)) == "INVISIBLE");
	// VerbCoin's ego, facing north: sprite 11.
	CostumePicture north;
	CHECK (costumeOfSub (ego.buffer (), "makeEgoAnim", &north, 0) && north.known && north.frame == 11);

	// The scale of a room: setScale from two points, and a scale map.
	int horizon, divide;
	CHECK (scaleFromPoints (200, 10, 400, 100, &horizon, &divide) && divide == 222 && horizon == 178);
	CHECK (scaleFromPoints (400, 100, 200, 10, &horizon, &divide) && divide == 222 && horizon == 178);
	CHECK (! scaleFromPoints (200, 10, 200, 100, &horizon, &divide) && ! scaleFromPoints (200, 50, 300, 50, &horizon, &divide));
	CHECK (outside.buffer ().fileText () == original);
	rooms = readRooms (outside.buffer ());
	if (rooms.size () == 1) {
		const Room & room = rooms[0];
		CHECK (room.setScale.present && room.setScale.first.value == 200 && room.setScale.second.value == 150 && ! room.scaleMap.present);
		CHECK (room.hasFirstCharacter && room.firstCharacter == room.characters[0].call);
		CHECK (applied (outside, roomEdit::setScale (outside.buffer (), room, "\t", 178, 222)).find ("\tsetScale (178, 222);") != std::string::npos);
		outside.commands ().undo ();
		// A map before the first character, which is after the setting up.
		std::string text = applied (outside, roomEdit::setScaleMap (outside.buffer (), room, "\t", "outside/scale.png", 20, 120));
		CHECK (text.find ("WEST);\n\tsetScaleMap ('outside/scale.png', 20, 120);\n\n\taddCharacter (duck") != std::string::npos);
		std::vector<Room> mapped = readRooms (outside.buffer ());
		CHECK (mapped.size () == 1 && mapped[0].scaleMap.present && mapped[0].scaleMap.file == "outside/scale.png" && mapped[0].scaleMap.second.value == 20);
		if (mapped.size () == 1) {
			text = applied (outside, roomEdit::setScaleMap (outside.buffer (), mapped[0], "\t", "other.png", 30, 120));
			CHECK (text.find ("setScaleMap ('other.png', 30, 120);") != std::string::npos);
			outside.commands ().undo ();
			text = applied (outside, roomEdit::removeScaleMap (outside.buffer (), mapped[0]));
			CHECK (text.find ("setScaleMap") == std::string::npos);
			outside.commands ().undo ();
		}
		outside.commands ().undo ();
		CHECK (outside.buffer ().fileText () == original);
	}
	// Without a setScale: the first character outside an if, when it comes
	// before the setting up ends; else after it.
	std::unique_ptr<ScriptDocument> unscaled (scriptOf (
		"sub one () {\n"
		"    addOverlay ('a.png', 0, 0);\n"
		"    addCharacter (a, 1, 2, anim ('a.duc', 0));\n"
		"    setFloor ('a.flo');\n"
		"}\n"
		"sub two () {\n"
		"    setFloor ('b.flo');\n"
		"    if (x) addCharacter (b, 1, 2, anim ('b.duc', 0));\n"
		"}\n", TextPos ()));
	rooms = readRooms (unscaled->buffer ());
	CHECK (rooms.size () == 2 && ! rooms[0].setScale.present);
	if (rooms.size () == 2) {
		CHECK (applied (*unscaled, roomEdit::setScale (unscaled->buffer (), rooms[0], "    ", 0, 400)).find ("('a.png', 0, 0);\n    setScale (0, 400);\n    addCharacter (a,") != std::string::npos);
		unscaled->commands ().undo ();
		CHECK (! rooms[1].hasFirstCharacter);
		CHECK (applied (*unscaled, roomEdit::setScale (unscaled->buffer (), rooms[1], "    ", 0, 400)).find ("setFloor ('b.flo');\n    setScale (0, 400);\n    if (x)") != std::string::npos);
		unscaled->commands ().undo ();
	}

	CHECK (isIdentifier ("bench") && isIdentifier ("_b2") && ! isIdentifier ("2b") && ! isIdentifier ("sub") && ! isIdentifier ("addOverlay") && ! isIdentifier ("a b"));
}

int main (int argc, char ** argv) {
	if (argc != 4) {
		fprintf (stderr, "usage: core_test <folder for files> <folder of the example projects> <fixtures>\n");
		return 2;
	}
	folder = std::string (argv[1]) + "/";
	examples = std::string (argv[2]) + "/";
	fixtures = std::string (argv[3]) + "/";

	testCommandStack ();
	testPaths ();
	testDocuments ();
	testPreferences ();
	testProjects ();
	testLaunch ();
	testCompileJob ();
	testCodePages ();
	testProjectCheck ();
	testFloors ();
	testZBuffers ();
	testSpriteBanks ();
	testTranslations ();
	testRigs ();
	testClips ();
	testRenameAcross ();
	testEditorParts ();
	testClipPaste ();
	testBankCompaction ();
	testColourKeys ();
	testPreviewPlayer ();
	testFileMemory ();
	testTextBuffer ();
	testScriptFiles ();
	testTextEditing ();
	testScriptLexer ();
	testScriptCatalog ();
	testScriptIndex ();
	testScriptAssist ();
	testScriptAnalysisQuiet ();
	testScriptAnalysisMistakes ();
	testScriptEventMembers ();
	testProjectSearch ();
	testRoomScript ();

	if (failures) {
		printf ("%d checks failed\n", failures);
		return 1;
	}
	printf ("core_test: all good\n");
	return 0;
}
