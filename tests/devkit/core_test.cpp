/*
 * Tests of the core of the Dev Kit (source/DevKit/Core): undo and redo,
 * documents, opening, saving and closing them, recent files and the
 * preferences. They need no display: a fake Platform answers the questions
 * and dialogs.
 *
 * Usage: core_test <folder for files>
 */

#include <stdio.h>
#include <string.h>

#include <deque>
#include <string>
#include <vector>

#include "CommandStack.h"
#include "Document.h"
#include "DocumentManager.h"
#include "Preferences.h"
#include "RecentFiles.h"

using namespace devkit;

static int failures = 0;

#define CHECK(condition) do { \
	if (! (condition)) { \
		fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		failures ++; \
	} \
} while (0)

static std::string folder;

static void writeFile (const std::string & path, const std::string & text) {
	FILE * fp = fopen (path.c_str (), "wb");
	fwrite (text.data (), 1, text.size (), fp);
	fclose (fp);
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

// A Platform that answers from lists, and keeps what it was asked.
class FakePlatform : public Platform {
public:
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
	CHECK (preferences.editor == "gedit" && preferences.modPlayer == "totem");

	preferences.editor = "code --goto {file}:{line}";
	CHECK (preferences.write (file));
	Preferences again;
	CHECK (again.read (file));
	CHECK (again.editor == "code --goto {file}:{line}");
	CHECK (readFile (file).find ("Future=yes\r\n") != std::string::npos);

	Preferences defaults;
	CHECK (! defaults.read (folder + "missing.ini"));
	CHECK (defaults.keepImages && defaults.verbose && defaults.editor.empty ());
}

int main (int argc, char ** argv) {
	if (argc != 2) {
		fprintf (stderr, "usage: core_test <folder for files>\n");
		return 2;
	}
	folder = std::string (argv[1]) + "/";

	testCommandStack ();
	testPaths ();
	testDocuments ();
	testPreferences ();

	if (failures) {
		printf ("%d checks failed\n", failures);
		return 1;
	}
	printf ("core_test: all good\n");
	return 0;
}
