/*
 * Tests of the core of the Dev Kit (source/DevKit/Core): undo and redo,
 * documents, opening, saving and closing them, recent files and the
 * preferences. They need no display: a fake Platform answers the questions
 * and dialogs.
 *
 * Usage: core_test <folder for files> <folder of the example projects>
 */

#include <stdio.h>
#include <string.h>

#include <chrono>
#include <thread>

#include <deque>
#include <string>
#include <vector>

#include "CommandStack.h"
#include "CompileJob.h"
#include "Launch.h"
#include "ProjectDocument.h"
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

static std::string folder, examples;

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

int main (int argc, char ** argv) {
	if (argc != 3) {
		fprintf (stderr, "usage: core_test <folder for files> <folder of the example projects>\n");
		return 2;
	}
	folder = std::string (argv[1]) + "/";
	examples = std::string (argv[2]) + "/";

	testCommandStack ();
	testPaths ();
	testDocuments ();
	testPreferences ();
	testProjects ();
	testLaunch ();
	testCompileJob ();

	if (failures) {
		printf ("%d checks failed\n", failures);
		return 1;
	}
	printf ("core_test: all good\n");
	return 0;
}
