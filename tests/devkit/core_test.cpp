/*
 * Tests of the core of the Dev Kit (source/DevKit/Core): undo and redo,
 * documents, opening, saving and closing them, recent files, the
 * preferences, projects, floors and z-buffers. They need no display: a fake Platform answers the questions
 * and dialogs.
 *
 * Usage: core_test <folder for files> <folder of the example projects> <fixtures>
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#endif

#include <chrono>
#include <thread>

#include <deque>
#include <string>
#include <vector>

#include "CommandStack.h"
#include "CompileJob.h"
#include "Launch.h"
#include "ProjectCheck.h"
#include "ProjectDocument.h"
#include "Document.h"
#include "DocumentManager.h"
#include "FileMemory.h"
#include "FloorDocument.h"
#include "ZBufferDocument.h"
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

	// Undo takes it all back to an empty floor, and redo brings it again.
	Floor drawn = floor.floor ();
	while (floor.commands ().canUndo ()) floor.commands ().undo ();
	CHECK (floor.floor () == Floor ());
	CHECK (! floor.isModified ());
	while (floor.commands ().canRedo ()) floor.commands ().redo ();
	CHECK (floor.floor () == drawn);

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
	CHECK (made.makeFromTGA (fixtures + "zbuffer.tga"));
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
	testFileMemory ();

	if (failures) {
		printf ("%d checks failed\n", failures);
		return 1;
	}
	printf ("core_test: all good\n");
	return 0;
}
