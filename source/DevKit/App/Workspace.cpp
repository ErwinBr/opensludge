#include <algorithm>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL_stdinc.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"

#include <SDL3/SDL_filesystem.h>

#include "DocumentView.h"
#include "FloorDocument.h"
#include "FloorView.h"
#include "Launch.h"
#include "Log.h"
#include "Platform.h"
#include "ProjectView.h"
#include "ClipDocument.h"
#include "ClipView.h"
#include "RigDocument.h"
#include "RigView.h"
#include "ScriptDocument.h"
#include "ScriptView.h"
#include "SpriteBankDocument.h"
#include "SpriteBankView.h"
#include "TranslationDocument.h"
#include "TranslationView.h"
#include "Workspace.h"
#include "ZBufferDocument.h"
#include "ZBufferView.h"
#include "version.h"

namespace devkit {

// The names of the windows, which imgui.ini keeps the places of.
static const char * const START_PAGE = "Start";
static const char * const PROJECT_PANEL = "Project";
static const char * const BUILD_PANEL = "Build";
static const char * const INSPECTOR_PANEL = "Inspector";
static const char * const LOG_PANEL = "Log";
static const char * const FIND_PANEL = "Find in Project";

#if defined(__APPLE__)
static const char * const SHORTCUT_PREFIX = "Cmd+";
#else
static const char * const SHORTCUT_PREFIX = "Ctrl+";
#endif

static std::string shortcut (const char * key) {
	return std::string (SHORTCUT_PREFIX) + key;
}

Workspace::Workspace (Platform & p, const std::string & renderer, const CanvasGraphics & g)
	: thePlatform (p), rendererName (renderer), graphics (g),
	  recent (p.settingsFolder ().empty () ? std::string () : p.settingsFolder () + "recent.txt"),
	  memory (p.settingsFolder ().empty () ? std::string () : p.settingsFolder () + "files.txt"),
	  manager (p, recent), compiles (0), nextDiskIndex (0), projectIndexed (false), looseFontSize (0), buildPanel (*this), findPanel (*this), fileCheck (*this), quitting (false), projectPanelHovered (false), focusInspector (false), resetLayout (false), layoutFrames (0), documentDock (0), focusDocument (NULL), focusLater (NULL), focusLaterFrames (0), closeTab (false),
	  showProject (true), showBuild (true), showInspector (true), showLog (true), showFind (false), focusFind (false),
	  showPreferences (false), showAbout (false), showDemo (false),
	  logShowDebug (false), logAutoScroll (true), logVersionSeen (0) {
	// Without our own preferences yet, we start from those of the GTK
	// Project Manager.
	if (! preferences.read (preferencesFile ())) {
#if defined(_WIN32)
		const char * config = SDL_getenv ("LOCALAPPDATA");
		std::string old = config ? std::string (config) + "\\sludge-devkit\\SLUDGE.ini" : std::string ();
#else
		const char * config = SDL_getenv ("XDG_CONFIG_HOME");
		const char * home = SDL_getenv ("HOME");
		std::string old = config && *config ? std::string (config) + "/sludge-devkit/SLUDGE.ini"
			: home ? std::string (home) + "/.config/sludge-devkit/SLUDGE.ini" : std::string ();
#endif
		if (! old.empty () && preferences.read (old)) {
			Log::get ().add (LogLine::LOG_INFO, "Took the preferences of the GTK Dev Kit from %s\n", old.c_str ());
		}
	}
	manager.registerType (ProjectDocument::type ());
	registerView ("Project", [this] (Document * document) -> DocumentView * {
		return new ProjectView ((ProjectDocument *) document, *this);
	});
	manager.registerType (FloorDocument::type (p));
	registerView ("Floor", [this] (Document * document) -> DocumentView * {
		return new FloorView ((FloorDocument *) document, *this, graphics, memory);
	});
	manager.registerType (ZBufferDocument::type ());
	registerView ("Z-Buffer", [this] (Document * document) -> DocumentView * {
		return new ZBufferView ((ZBufferDocument *) document, *this, graphics);
	});
	manager.registerType (SpriteBankDocument::type ());
	registerView ("Sprite Bank", [this] (Document * document) -> DocumentView * {
		return new SpriteBankView ((SpriteBankDocument *) document, *this, graphics);
	});
	manager.registerType (ClipDocument::type ());
	registerView ("Clip", [this] (Document * document) -> DocumentView * {
		return new ClipView ((ClipDocument *) document, *this, graphics);
	});
	manager.registerType (RigDocument::type ());
	registerView ("Rig", [this] (Document * document) -> DocumentView * {
		return new RigView ((RigDocument *) document, *this, graphics);
	});
	manager.registerType (ScriptDocument::type ());
	registerView ("Script", [this] (Document * document) -> DocumentView * {
		return new ScriptView ((ScriptDocument *) document, *this);
	});
	manager.registerType (TranslationDocument::type ());
	registerView ("Translation", [this] (Document * document) -> DocumentView * {
		return new TranslationView ((TranslationDocument *) document, *this);
	});

	manager.opened = [this] (Document * document) {
		std::map<std::string, ViewMaker>::iterator maker = viewMakers.find (document->kind ());
		if (maker != viewMakers.end ()) views[document].reset (maker->second (document));
		focusDocument = document;
		// A project's scripts go into the index at once.
		nextDiskIndex = 0;
	};
	manager.closing = [this] (Document * document) {
		std::map<Document *, std::unique_ptr<DocumentView> >::iterator view = views.find (document);
		if (view != views.end ()) {
			closedViews.push_back (std::move (view->second));
			views.erase (view);
		}
		// The build was of this project.
		if (document == project ()) job.reset ();
	};
}

Workspace::~Workspace () {
	closedViews.clear ();
	views.clear ();
}

void Workspace::frameDone () {
	closedViews.clear ();
}

std::string Workspace::preferencesFile () const {
	return thePlatform.settingsFolder () + "SLUDGE.ini";
}

void Workspace::registerView (const std::string & kind, ViewMaker make) {
	viewMakers[kind] = make;
}

void Workspace::requestQuit () {
	manager.closeAll ([this] (bool closed) {
		if (closed) quitting = true;
	});
}

bool Workspace::animating () const {
	if (compiling ()) return true;
	for (std::map<Document *, std::unique_ptr<DocumentView> >::const_iterator i = views.begin (); i != views.end (); ++ i) {
		if (i->second && i->second->animating ()) return true;
	}
	return false;
}

// Files that are dropped on the Project panel go into the project; other
// files open, or go to the active editor, as a backdrop to a floor.
void Workspace::openFiles (const std::vector<std::string> & files) {
	ProjectDocument * p = project ();
	std::vector<std::string> add;
	for (size_t i = 0; i < files.size (); i ++) {
		if (p && projectPanelHovered && fileKind (files[i]) != FILE_DOCUMENT) {
			add.push_back (files[i]);
			continue;
		}
		// The active editor may take a file that the Dev Kit doesn't open.
		Document * active = manager.active ();
		std::map<Document *, std::unique_ptr<DocumentView> >::iterator view = views.find (active);
		if (! manager.typeOf (files[i]) && view != views.end () && view->second && view->second->takeFile (files[i])) continue;
		manager.open (files[i]);
	}
	if (! add.empty ()) p->addFiles (add);
}

void Workspace::dropFiles (const std::vector<std::string> & files) {
	std::vector<std::string> rest;
	for (size_t i = 0; i < files.size (); i ++) {
		DocumentView * view = projectPanelHovered ? NULL : activeView ();
		if (view && manager.typeOf (files[i]) && view->takeFile (files[i])) continue;
		rest.push_back (files[i]);
	}
	openFiles (rest);
}

double Workspace::tickMilliseconds () const {
	// The speed of a project is the milliseconds of a tick.
	ProjectDocument * p = project ();
	int ms = p ? p->settings ().speed : 0;
	return ms > 0 ? ms : 20;
}

bool Workspace::isOpen (const Document * document) const {
	const std::vector<Document *> & all = manager.documents ();
	return std::find (all.begin (), all.end (), document) != all.end ();
}

void Workspace::setAutoKey (bool on) {
	if (preferences.autoKey == on) return;
	preferences.autoKey = on;
	if (! preferences.write (preferencesFile ())) Log::get ().add (LogLine::LOG_WARNING, "Can't save the preferences in %s\n", preferencesFile ().c_str ());
}

void Workspace::setClipSplit (float fraction) {
	if (fraction < 0.1f) fraction = 0.1f;
	if (fraction > 0.9f) fraction = 0.9f;
	if (preferences.clipSplit == fraction) return;
	preferences.clipSplit = fraction;
	if (! preferences.write (preferencesFile ())) Log::get ().add (LogLine::LOG_WARNING, "Can't save the preferences in %s\n", preferencesFile ().c_str ());
}

std::vector<ClipDocument *> Workspace::clipsForRename (const animformat::AssetId & rig, const std::string & part, const std::string & folder) {
	std::vector<ClipDocument *> clips;
	// Those open, saved or not.
	const std::vector<Document *> & all = manager.documents ();
	for (size_t i = 0; i < all.size (); i ++) {
		ClipDocument * c = dynamic_cast<ClipDocument *> (all[i]);
		if (c && c->isBoundTo (rig)) clips.push_back (c);
	}
	// And those of the folder with tracks of the part, which we open.
	Document * active = manager.active ();
	std::vector<std::string> files = folder.empty () ? std::vector<std::string> () : clipsOfRig (folder, rig);
	for (size_t i = 0; i < files.size (); i ++) {
		if (manager.find (files[i])) continue;
		ClipDocument probe;
		std::string error;
		if (! probe.load (files[i], &error)) continue;
		bool hasPart = false;
		for (size_t t = 0; t < probe.clip ().tracks.size (); t ++) hasPart = hasPart || probe.clip ().tracks[t].part == part;
		// Or that shows it in its own way, as locked.
		hasPart = hasPart || ! probe.asset ().editorPart (part).isDefault ();
		if (! hasPart) continue;
		ClipDocument * c = dynamic_cast<ClipDocument *> (manager.open (files[i]));
		if (c) clips.push_back (c);
	}
	// The rig stays in front.
	if (active && manager.active () != active) {
		manager.setActive (active);
		focusDocument = active;
		focusLater = active;
		focusLaterFrames = 3;
	}
	return clips;
}

DocumentView * Workspace::activeView () const {
	std::map<Document *, std::unique_ptr<DocumentView> >::const_iterator view = views.find (manager.active ());
	return view == views.end () ? NULL : view->second.get ();
}

ProjectDocument * Workspace::project () const {
	return (ProjectDocument *) manager.findKind ("Project");
}

static bool fileExists (const std::string & path) {
	SDL_PathInfo info;
	return SDL_GetPathInfo (path.c_str (), &info) && info.type == SDL_PATHTYPE_FILE;
}

std::string Workspace::findProgram (const std::string & name) {
#if defined(_WIN32)
	std::string file = name + ".exe";
#else
	std::string file = name;
#endif
	std::string here = thePlatform.programFolder ();
	std::vector<std::string> places;
	places.push_back (here + file);
	// In a macOS app, the programs are in Contents/MacOS, and the base path
	// of SDL is Contents/Resources.
	places.push_back (here + "../MacOS/" + file);
#if defined(SLUDGE_BUILD_FOLDER)
	// In the build folder, the engine is at the top, and the compiler and
	// the Dev Kit are in source/DevKit.
	places.push_back (std::string (SLUDGE_BUILD_FOLDER) + file);
	places.push_back (std::string (SLUDGE_BUILD_FOLDER) + "source/DevKit/" + file);
#endif
	for (size_t i = 0; i < places.size (); i ++) {
		if (fileExists (places[i])) return places[i];
	}
	return file;
}

std::string Workspace::helpFile (const std::string & page) {
	std::string here = thePlatform.programFolder ();
	std::vector<std::string> places;
	// Next to the Dev Kit on Windows, and in the Resources of the app on
	// macOS, which is the folder of the program for SDL.
	places.push_back (here + "Help/");
	// Installed on Linux, with -DSLUDGE_DOC=ON.
	places.push_back (here + "../share/doc/sludge/");
#if defined(SLUDGE_SOURCE_FOLDER)
	places.push_back (std::string (SLUDGE_SOURCE_FOLDER) + "doc/");
#endif
	for (size_t i = 0; i < places.size (); i ++) {
		if (fileExists (places[i] + page)) return places[i] + page;
	}
	return std::string ();
}

void Workspace::openHelpPage (const std::string & page) {
	std::string file = helpFile ("SLUDGEDevKitHelp/" + page);
	if (file.empty ()) {
		openHelp ();
		return;
	}
	thePlatform.openWithSystem (file);
}

void Workspace::openHelp () {
	std::string file = helpFile ("SLUDGE_Help.html");
	if (! file.empty ()) {
		thePlatform.openWithSystem (file);
		return;
	}
	thePlatform.showMessage (Platform::MESSAGE_INFORMATION, "No help",
		"The help of the Dev Kit isn't installed here. It is in the doc folder of the source of SLUDGE.");
}

void Workspace::compile () {
	ProjectDocument * p = project ();
	if (! p || compiling ()) return;
	showBuild = true;
	ImGui::SetWindowFocus (BUILD_PANEL);
	// The compiler reads the scripts from their files, so we save those
	// with changes first, as other editors save before they build.
	std::vector<Document *> open = manager.documents ();
	for (size_t i = 0; i < open.size (); i ++) {
		if (strcmp (open[i]->kind (), "Script") || ! open[i]->isModified () || open[i]->path ().empty ()) continue;
		std::string error;
		if (open[i]->save (open[i]->path (), &error)) {
			Log::get ().add (LogLine::LOG_INFO, "Saved %s before compiling\n", open[i]->displayName ().c_str ());
		} else {
			thePlatform.showMessage (Platform::MESSAGE_ERROR, "Can't save " + open[i]->displayName (), error);
			return;
		}
	}
	// The compiler reads the project from its file.
	if (p->isModified ()) {
		manager.save (p, [this] (bool saved) {
			if (saved) startCompile ();
		});
	} else {
		startCompile ();
	}
}

void Workspace::startCompile () {
	ProjectDocument * p = project ();
	if (! p) return;
	CompileOptions options;
	options.keepImages = preferences.keepImages;
	options.writeStrings = preferences.writeStrings;
	options.verbose = preferences.verbose;
	job.reset (new CompileJob (thePlatform));
	compiles ++;
	std::string compiler = findProgram ("sludge-compiler");
	Log::get ().add (LogLine::LOG_DEBUG, "The compiler is %s\n", compiler.c_str ());
	job->start (compiler, p->path (), options);
}

void Workspace::runGame () {
	ProjectDocument * p = project ();
	if (! p) return;
	std::string game;
	if (job && job->succeeded) {
		game = job->gameFile;
	} else {
		const ProjectSettings & s = p->settings ();
		game = p->fullPath (s.finalFile + (s.makeSilent ? " (silent).slg" : ".slg"));
	}
	if (! fileExists (game)) {
		thePlatform.showMessage (Platform::MESSAGE_INFORMATION, "No game to run",
			"There is no " + fileNameOf (game) + " yet. Compile the project first.");
		return;
	}
	std::vector<std::string> args;
	args.push_back (findProgram ("sludge-engine"));
	args.push_back (game);
	std::string error;
	if (thePlatform.startProgram (args, p->folder (), &error)) {
		Log::get ().add (LogLine::LOG_INFO, "Running %s\n", game.c_str ());
	} else {
		thePlatform.showMessage (Platform::MESSAGE_ERROR, "Can't run the game", "Can't start " + args[0] + ": " + error);
	}
}

void Workspace::openWith (const std::string & command, const std::string & path, int line) {
	if (command.empty ()) {
		thePlatform.openWithSystem (path);
		return;
	}
	std::vector<std::string> args = commandLine (command, path, line);
	std::string error;
	if (! thePlatform.startProgram (args, folderOf (path), &error)) {
		thePlatform.showMessage (Platform::MESSAGE_ERROR, "Can't open " + fileNameOf (path),
			"Can't start " + args[0] + ": " + error + "\n\nYou can choose another program in the preferences.");
	}
}

void Workspace::openFile (const std::string & path, int line) {
	if (! fileExists (path)) {
		thePlatform.showMessage (Platform::MESSAGE_ERROR, "Can't open " + fileNameOf (path), "There is no file " + path + ".");
		return;
	}
	switch (fileKind (path)) {
		case FILE_SCRIPT:
			// In the script editor, at the line.
			if (line > 0) showInScript (path, line - 1, -1);
			else showInScript (path, -1, -1);
			break;
		case FILE_IMAGE:
			openWith (preferences.imageViewer, path, line);
			break;
		case FILE_AUDIO:
			openWith (preferences.audioPlayer, path, line);
			break;
		case FILE_MODULE:
			openWith (preferences.modPlayer, path, line);
			break;
		default:
			// What the Dev Kit edits opens here, the rest as the system does.
			if (manager.typeOf (path)) manager.open (path);
			else thePlatform.openWithSystem (path);
			break;
	}
}

void Workspace::showInScript (const std::string & path, int line, int column, int length) {
	Document * document = manager.open (path);
	if (! document) return;
	focusDocument = document;
	ScriptView * view = dynamic_cast<ScriptView *> (views[document].get ());
	if (! view || line < 0) return;
	if (column < 0) view->goToLine (line + 1);
	else view->goTo (TextPos (line, column), length);
}

void Workspace::openFindInProject () {
	std::string selected;
	if (ScriptView * view = dynamic_cast<ScriptView *> (activeView ())) selected = view->selectedText ();
	showFind = true;
	focusFind = true;
	findPanel.focus (selected);
}

void Workspace::drawFindPanel () {
	// Where the Build panel is, the first time.
	if (ImGuiWindow * build = ImGui::FindWindowByName (BUILD_PANEL)) {
		if (build->DockId) ImGui::SetNextWindowDockID (build->DockId, ImGuiCond_FirstUseEver);
	}
	if (focusFind) {
		ImGui::SetNextWindowFocus ();
		focusFind = false;
	}
	if (! ImGui::Begin (FIND_PANEL, &showFind)) {
		ImGui::End ();
		return;
	}
	findPanel.draw (project (), [this] (const std::string & path) -> const TextBuffer * {
		std::string key = ScriptIndex::key (path);
		const std::vector<Document *> & open = manager.documents ();
		for (size_t i = 0; i < open.size (); i ++) {
			ScriptDocument * script = dynamic_cast<ScriptDocument *> (open[i]);
			if (script && ScriptIndex::key (script->path ()) == key) return &script->buffer ();
		}
		return NULL;
	});
	ImGui::End ();
}

std::vector<BuildMessage> Workspace::buildMessagesFor (const std::string & path, unsigned int * serial) const {
	std::vector<BuildMessage> list;
	ProjectDocument * p = project ();
	// A new number when a compile starts and when it ends.
	*serial = job ? compiles * 2 + (job->finished ? 1 : 0) : 0;
	if (! job || ! p || ! job->finished) return list;
	std::string wanted = ScriptIndex::key (path);
	for (size_t m = 0; m < job->messages.size (); m ++) {
		const BuildMessage & message = job->messages[m];
		if (message.file.empty () || message.line <= 0) continue;
		if (ScriptIndex::key (p->fullPath (message.file)) == wanted) list.push_back (message);
	}
	return list;
}

// The key of the size of the scripts in what we remember of a project.
static const char * const SCRIPT_FONT_SIZE = "scriptFontSize";

float Workspace::scriptFontSize () const {
	ProjectDocument * p = project ();
	if (! p || p->path ().empty ()) return looseFontSize;
	return (float) atof (memory.get (p->path (), SCRIPT_FONT_SIZE).c_str ());
}

void Workspace::setScriptFontSize (float size) {
	if (size < 0) size = 0;
	ProjectDocument * p = project ();
	if (! p || p->path ().empty ()) {
		looseFontSize = size;
		return;
	}
	char text[32];
	snprintf (text, sizeof (text), "%g", size);
	// The size of the interface is no size of its own.
	memory.set (p->path (), SCRIPT_FONT_SIZE, size > 0 ? text : "");
}

bool Workspace::knowsAllNames (const std::string & path) const {
	if (! projectIndexed || ! project ()) return false;
	std::string key = ScriptIndex::key (path);
	if (! projectScripts.count (key)) return false;
	// Each of them, as one may have become unreadable since.
	for (std::set<std::string>::const_iterator s = projectScripts.begin (); s != projectScripts.end (); ++ s) {
		if (! index.has (*s)) return false;
	}
	return true;
}

void Workspace::updateScriptIndex () {
	// The open scripts as they are, typed but not saved too.
	std::set<std::string> wanted;
	const std::vector<Document *> & open = manager.documents ();
	for (size_t i = 0; i < open.size (); i ++) {
		ScriptDocument * script = dynamic_cast<ScriptDocument *> (open[i]);
		if (! script) continue;
		std::string file = ScriptView::indexName (script);
		index.update (file, script->buffer (), script->buffer ().version ());
		wanted.insert (ScriptIndex::key (file));
	}
	// Now and then, the project's from disk, and we forget the rest.
	double now = ImGui::GetTime ();
	if (now < nextDiskIndex) return;
	nextDiskIndex = now + 2.0;
	projectScripts.clear ();
	projectIndexed = false;
	if (ProjectDocument * p = project ()) {
		projectIndexed = true;
		const std::vector<std::string> & files = p->files ();
		for (size_t f = 0; f < files.size (); f ++) {
			std::string extension = extensionOf (files[f]);
			if (extension != "slu" && extension != "sld") continue;
			std::string full = p->fullPath (files[f]);
			std::string key = ScriptIndex::key (full);
			projectScripts.insert (key);
			if (! wanted.count (key) && ! index.updateFromDisk (full)) projectIndexed = false;
			wanted.insert (key);
		}
	}
	std::vector<std::string> known = index.files ();
	for (size_t k = 0; k < known.size (); k ++) {
		if (! wanted.count (known[k])) index.forget (known[k]);
	}
}

std::string Workspace::title () const {
	Document * document = manager.active ();
	if (! document) return "SLUDGE Dev Kit";
	return document->displayName () + (document->isModified () ? "*" : "") + " \xE2\x80\x94 SLUDGE Dev Kit";
}

void Workspace::draw () {
	if (job) job->update ();
	updateScriptIndex ();
	handleShortcuts ();
	drawMenuBar ();
	drawDockSpace ();
	drawStartPage ();
	drawDocuments ();
	if (showProject) drawProjectPanel ();
	if (showInspector) drawInspectorPanel ();
	if (showBuild) drawBuildPanel ();
	if (showLog) drawLogPanel ();
	if (showFind) drawFindPanel ();
	// The frame after we placed the panels, when the tabs exist, we show
	// the Build in front of the Log, and the document that we opened last
	// in front of the Start page.
	if (layoutFrames > 0 && -- layoutFrames == 0) {
		ImGui::SetWindowFocus (BUILD_PANEL);
		Document * active = manager.active ();
		if (active && active != project ()) focusDocument = active;
	}
	if (focusLater && focusLaterFrames > 0 && -- focusLaterFrames == 0) {
		if (isOpen (focusLater)) focusDocument = focusLater;
		focusLater = NULL;
	}
	fileCheck.draw (project ());
	if (showPreferences) drawPreferencesWindow ();
	if (showAbout) drawAboutWindow ();
	if (showDemo) ImGui::ShowDemoWindow (&showDemo);
}

// ImGuiMod_Ctrl is Cmd on macOS.
void Workspace::handleShortcuts () {
	const ImGuiInputFlags global = ImGuiInputFlags_RouteGlobal;
	Document * document = manager.active ();
	if (ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiKey_Q, global)) requestQuit ();
	if (ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiKey_O, global) && ! manager.types ().empty ()) manager.openWithDialog ();
	if (ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiKey_B, global)) compile ();
	if (ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiKey_R, global)) runGame ();
	if (ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F, global)) openFindInProject ();
	if (! document) return;
	if (ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiKey_S, global)) manager.save (document);
	if (ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, global)) manager.saveAs (document);
	if (ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiKey_W, global)) manager.close (document);
	// Undo and redo are for the text fields while one is active.
	if (! ImGui::GetIO ().WantTextInput) {
		if (ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiKey_Z, global)) document->commands ().undo ();
		if (ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, global)
			|| ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiKey_Y, global)) document->commands ().redo ();
	}
}

void Workspace::drawRecentMenu () {
	const std::vector<std::string> & files = recent.files ();
	if (! ImGui::BeginMenu ("Open Recent", ! files.empty ())) return;
	// We copy the list, which opening changes.
	std::vector<std::string> list = files;
	for (size_t i = 0; i < list.size (); i ++) {
		ImGui::PushID ((int) i);
		if (ImGui::MenuItem (fileNameOf (list[i]).c_str ())) manager.open (list[i]);
		ImGui::SetItemTooltip ("%s", list[i].c_str ());
		ImGui::PopID ();
	}
	ImGui::Separator ();
	if (ImGui::MenuItem ("Clear Menu")) recent.clear ();
	ImGui::EndMenu ();
}

void Workspace::drawMenuBar () {
	if (! ImGui::BeginMainMenuBar ()) return;
	Document * document = manager.active ();
	const std::vector<DocumentType> & types = manager.types ();

	if (ImGui::BeginMenu ("File")) {
		if (ImGui::BeginMenu ("New", ! types.empty ())) {
			for (size_t i = 0; i < types.size (); i ++) {
				if (ImGui::MenuItem (types[i].name.c_str ())) manager.create (types[i]);
			}
			ImGui::EndMenu ();
		}
		if (ImGui::MenuItem ("Open...", shortcut ("O").c_str (), false, ! types.empty ())) manager.openWithDialog ();
		drawRecentMenu ();
		ImGui::Separator ();
		if (ImGui::MenuItem ("Save", shortcut ("S").c_str (), false, document != NULL)) manager.save (document);
		if (ImGui::MenuItem ("Save As...", shortcut ("Shift+S").c_str (), false, document != NULL)) manager.saveAs (document);
		if (ImGui::MenuItem ("Save All", NULL, false, manager.anyModified ())) manager.saveAll ();
		if (ImGui::MenuItem ("Close", shortcut ("W").c_str (), false, document != NULL)) manager.close (document);
		ImGui::Separator ();
		if (ImGui::MenuItem ("Quit", shortcut ("Q").c_str ())) requestQuit ();
		ImGui::EndMenu ();
	}
	if (ImGui::BeginMenu ("Edit")) {
		CommandStack * commands = document ? &document->commands () : NULL;
		std::string undo = commands && commands->canUndo () ? "Undo " + commands->undoLabel () : std::string ("Undo");
		std::string redo = commands && commands->canRedo () ? "Redo " + commands->redoLabel () : std::string ("Redo");
		if (ImGui::MenuItem (undo.c_str (), shortcut ("Z").c_str (), false, commands && commands->canUndo ())) commands->undo ();
		if (ImGui::MenuItem (redo.c_str (), shortcut ("Shift+Z").c_str (), false, commands && commands->canRedo ())) commands->redo ();
		ImGui::Separator ();
		DocumentView * editing = activeView ();
		if (ImGui::MenuItem ("Copy", shortcut ("C").c_str (), false, editing && editing->canCopy ())) editing->copy ();
		if (ImGui::MenuItem ("Paste", shortcut ("V").c_str (), false, editing && editing->canPaste ())) editing->pasteClipboard ();
		if (editing) editing->drawEditMenu ();
		ImGui::Separator ();
		if (ImGui::MenuItem ("Preferences...")) showPreferences = true;
		ImGui::EndMenu ();
	}
	if (ImGui::BeginMenu ("Project")) {
		ProjectDocument * p = project ();
		if (ImGui::MenuItem ("Add Files...", NULL, false, p != NULL)) {
			ProjectView * view = (ProjectView *) views[p].get ();
			if (view) view->addFilesWithDialog ();
		}
		if (ImGui::MenuItem ("Properties", NULL, false, p != NULL)) {
			manager.setActive (p);
			revealInspector ();
		}
		if (ImGui::MenuItem ("Find in Project...", shortcut ("Shift+F").c_str (), false, p != NULL)) openFindInProject ();
		if (ImGui::MenuItem ("Check Files...", NULL, false, p != NULL)) checkFiles ();
		ImGui::SetItemTooltip ("Find what keeps the project from building on every system");
		ImGui::Separator ();
		if (ImGui::MenuItem ("Compile", shortcut ("B").c_str (), false, p && ! compiling ())) compile ();
		if (job && job->running ()) {
			if (ImGui::MenuItem ("Cancel Compile")) job->cancel ();
		}
		if (ImGui::MenuItem ("Run Game", shortcut ("R").c_str (), false, p != NULL)) runGame ();
		ImGui::Separator ();
		if (ImGui::MenuItem ("Close Project", NULL, false, p != NULL)) manager.close (p);
		ImGui::EndMenu ();
	}
	if (ImGui::BeginMenu ("View")) {
		ImGui::MenuItem (PROJECT_PANEL, NULL, &showProject);
		ImGui::MenuItem (BUILD_PANEL, NULL, &showBuild);
		ImGui::MenuItem (INSPECTOR_PANEL, NULL, &showInspector);
		ImGui::MenuItem (LOG_PANEL, NULL, &showLog);
		ImGui::MenuItem (FIND_PANEL, NULL, &showFind);
		ImGui::Separator ();
		if (ImGui::MenuItem ("Reset Layout")) {
			showProject = showBuild = showInspector = showLog = true;
			resetLayout = true;
		}
		ImGui::Separator ();
		ImGui::MenuItem ("Dear ImGui Demo", NULL, &showDemo);
		ImGui::EndMenu ();
	}
	if (ImGui::BeginMenu ("Help")) {
		if (ImGui::MenuItem ("SLUDGE Dev Kit Help")) openHelp ();
		if (ImGui::MenuItem ("SLUDGE Website")) thePlatform.openWithSystem ("https://opensludge.github.io");
		ImGui::Separator ();
		if (ImGui::MenuItem ("About the SLUDGE Dev Kit")) showAbout = true;
		ImGui::EndMenu ();
	}
	ImGui::EndMainMenuBar ();
}

void Workspace::drawDockSpace () {
	ImGuiID dockspace = ImGui::GetID ("Workspace");
	// The first time, or after Reset Layout, we place the panels.
	if (resetLayout || ! ImGui::DockBuilderGetNode (dockspace)) {
		buildDefaultLayout (dockspace);
		resetLayout = false;
		layoutFrames = 2;
	}
	ImGui::DockSpaceOverViewport (dockspace, ImGui::GetMainViewport ());
}

void Workspace::buildDefaultLayout (ImGuiID dockspace) {
	ImGuiViewport * viewport = ImGui::GetMainViewport ();
	ImGui::DockBuilderRemoveNode (dockspace);
	ImGui::DockBuilderAddNode (dockspace, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize (dockspace, viewport->WorkSize);

	// The documents in the middle, the project on the left, the inspector
	// on the right, and the build and the log at the bottom.
	ImGuiID middle = dockspace;
	ImGuiID left = ImGui::DockBuilderSplitNode (middle, ImGuiDir_Left, 0.20f, NULL, &middle);
	ImGuiID right = ImGui::DockBuilderSplitNode (middle, ImGuiDir_Right, 0.25f, NULL, &middle);
	ImGuiID bottom = ImGui::DockBuilderSplitNode (middle, ImGuiDir_Down, 0.28f, NULL, &middle);
	documentDock = middle;

	ImGui::DockBuilderDockWindow (START_PAGE, middle);
	ImGui::DockBuilderDockWindow (PROJECT_PANEL, left);
	ImGui::DockBuilderDockWindow (INSPECTOR_PANEL, right);
	ImGui::DockBuilderDockWindow (BUILD_PANEL, bottom);
	ImGui::DockBuilderDockWindow (LOG_PANEL, bottom);
	ImGui::DockBuilderDockWindow (FIND_PANEL, bottom);
	ImGui::DockBuilderFinish (dockspace);
}

// Text in the middle of the rest of the window, greyed out.
static void hint (const char * text) {
	ImVec2 avail = ImGui::GetContentRegionAvail ();
	ImVec2 size = ImGui::CalcTextSize (text, NULL, false, avail.x);
	ImGui::SetCursorPos (ImVec2 (ImGui::GetCursorPosX () + (avail.x - size.x) * 0.5f,
		ImGui::GetCursorPosY () + (avail.y - size.y) * 0.4f));
	ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
	ImGui::PushTextWrapPos (ImGui::GetCursorPosX () + avail.x);
	ImGui::TextUnformatted (text);
	ImGui::PopTextWrapPos ();
	ImGui::PopStyleColor ();
}

void Workspace::drawStartPage () {
	if (! ImGui::Begin (START_PAGE)) {
		ImGui::End ();
		return;
	}
	ImGui::Dummy (ImVec2 (0, ImGui::GetFontSize ()));
	ImGui::PushFont (NULL, ImGui::GetFontSize () * 1.8f);
	ImGui::TextUnformatted ("SLUDGE Dev Kit");
	ImGui::PopFont ();
	ImGui::TextDisabled ("Version %s", TEXT_VERSION);
	ImGui::Spacing ();
	ImGui::Separator ();
	ImGui::Spacing ();
	ImGui::TextWrapped ("Open a project to manage and compile it, or a floor, a z-buffer, a sprite bank or "
		"a translation to edit it. They open here as tabs; their properties are in the Inspector.");

	const std::vector<std::string> & files = recent.files ();
	if (! files.empty ()) {
		ImGui::Spacing ();
		ImGui::SeparatorText ("Recent files");
		std::vector<std::string> list = files;
		for (size_t i = 0; i < list.size (); i ++) {
			ImGui::PushID ((int) i);
			if (ImGui::Selectable (fileNameOf (list[i]).c_str ())) manager.open (list[i]);
			ImGui::SetItemTooltip ("%s", list[i].c_str ());
			ImGui::PopID ();
		}
	}
	ImGui::End ();
}

// A tab for each document, in the middle of the dock space.
void Workspace::drawDocuments () {
	// We copy the list, which closing a document changes.
	std::vector<Document *> documents = manager.documents ();
	for (size_t i = 0; i < documents.size (); i ++) {
		Document * document = documents[i];
		// The project is in the Project panel.
		if (document == project ()) continue;
		// The ### part keeps the window the same when the name changes. The
		// tab has a dot while the document has changes.
		char name[512];
		SDL_snprintf (name, sizeof (name), "%s###document%u", document->displayName ().c_str (), document->id ());
		if (documentDock) ImGui::SetNextWindowDockID (documentDock, ImGuiCond_FirstUseEver);
		if (focusDocument == document) {
			ImGui::SetNextWindowFocus ();
			focusDocument = NULL;
		}
		bool open = true;
		ImGuiWindowFlags flags = document->isModified () ? ImGuiWindowFlags_UnsavedDocument : 0;
		bool visible = ImGui::Begin (name, &open, flags);
		if (ImGui::IsWindowFocused (ImGuiFocusedFlags_RootAndChildWindows)) manager.setActive (document);
		if (visible) {
			std::map<Document *, std::unique_ptr<DocumentView> >::iterator view = views.find (document);
			if (view != views.end () && view->second) view->second->draw ();
			else hint ("The Dev Kit can't edit this kind of file yet.");
		}
		ImGui::End ();
		if (closeTab && document == manager.active ()) {
			open = false;
			closeTab = false;
		}
		if (! open) manager.close (document);
	}
	closeTab = false;
}

void Workspace::drawProjectPanel () {
	bool visible = ImGui::Begin (PROJECT_PANEL, &showProject);
	projectPanelHovered = ImGui::IsWindowHovered (ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	ProjectDocument * p = project ();
	if (p && ImGui::IsWindowFocused (ImGuiFocusedFlags_RootAndChildWindows)) manager.setActive (p);
	if (visible) {
		if (p && views[p]) {
			views[p]->draw ();
		} else {
			hint ("No project is open.");
			const std::vector<DocumentType> & types = manager.types ();
			ImGui::SetCursorPosX ((ImGui::GetContentRegionAvail ().x - ImGui::GetFontSize () * 12) * 0.5f);
			if (ImGui::Button ("New Project...", ImVec2 (ImGui::GetFontSize () * 6, 0)) && ! types.empty ()) manager.create (types[0]);
			ImGui::SameLine ();
			if (ImGui::Button ("Open...", ImVec2 (ImGui::GetFontSize () * 6, 0))) manager.openWithDialog ();
		}
	}
	ImGui::End ();
}

void Workspace::drawBuildPanel () {
	if (ImGui::Begin (BUILD_PANEL, &showBuild)) buildPanel.draw (job.get (), project ());
	ImGui::End ();
}

void Workspace::drawInspectorPanel () {
	if (focusInspector) {
		ImGui::SetNextWindowFocus ();
		focusInspector = false;
	}
	if (ImGui::Begin (INSPECTOR_PANEL, &showInspector)) {
		Document * document = manager.active ();
		std::map<Document *, std::unique_ptr<DocumentView> >::iterator view = views.find (document);
		if (view != views.end () && view->second) view->second->drawInspector ();
		else hint ("Select something to see its properties.");
	}
	ImGui::End ();
}

void Workspace::drawLogPanel () {
	if (! ImGui::Begin (LOG_PANEL, &showLog)) {
		ImGui::End ();
		return;
	}
	if (ImGui::Button ("Clear")) Log::get ().clear ();
	ImGui::SameLine ();
	ImGui::Checkbox ("Details", &logShowDebug);
	ImGui::SetItemTooltip ("Also show the details that help to find problems, such as those of the renderer.");
	ImGui::SameLine ();
	ImGui::Checkbox ("Follow", &logAutoScroll);
	ImGui::SameLine ();
	bool copy = ImGui::Button ("Copy");

	ImGui::Separator ();
	std::vector<LogLine> lines = Log::get ().lines ();
	unsigned int version = Log::get ().version ();
	std::string copied;
	if (ImGui::BeginChild ("lines", ImVec2 (0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
		for (size_t i = 0; i < lines.size (); i ++) {
			const LogLine & line = lines[i];
			if (line.level == LogLine::LOG_DEBUG && ! logShowDebug) continue;
			if (copy) copied += line.text + "\n";
			if (line.level == LogLine::LOG_ERROR) {
				ImGui::TextColored (ImVec4 (1.0f, 0.45f, 0.4f, 1.0f), "%s", line.text.c_str ());
			} else if (line.level == LogLine::LOG_WARNING) {
				ImGui::TextColored (ImVec4 (1.0f, 0.8f, 0.3f, 1.0f), "%s", line.text.c_str ());
			} else if (line.level == LogLine::LOG_DEBUG) {
				ImGui::TextDisabled ("%s", line.text.c_str ());
			} else {
				ImGui::TextUnformatted (line.text.c_str ());
			}
		}
		if (logAutoScroll && version != logVersionSeen) ImGui::SetScrollHereY (1.0f);
	}
	ImGui::EndChild ();
	logVersionSeen = version;
	if (copy) ImGui::SetClipboardText (copied.c_str ());
	ImGui::End ();
}

// A text field for a program. Returns true when the user is done changing
// it.
static bool programField (const char * label, std::string * value, const char * tooltip) {
	ImGui::InputTextWithHint (label, "The program of the system", value);
	ImGui::SetItemTooltip ("%s", tooltip);
	return ImGui::IsItemDeactivatedAfterEdit ();
}

void Workspace::drawPreferencesWindow () {
	ImGui::SetNextWindowPos (ImGui::GetMainViewport ()->GetCenter (), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));
	ImGui::SetNextWindowSize (ImVec2 (ImGui::GetFontSize () * 36, 0), ImGuiCond_Appearing);
	if (ImGui::Begin ("Preferences", &showPreferences, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize)) {
		bool changed = false;
		ImGui::SeparatorText ("Compiler");
		changed |= ImGui::Checkbox ("Keep the compressed images", &preferences.keepImages);
		changed |= ImGui::Checkbox ("Write the strings to a text file", &preferences.writeStrings);
		changed |= ImGui::Checkbox ("Verbose errors (development mode)", &preferences.verbose);

		ImGui::SeparatorText ("Programs");
		ImGui::TextWrapped ("The programs that open the files of a project; scripts open in the Dev Kit. "
			"Leave one empty for the program that your system opens the file with. Write {file} where the "
			"file goes; without it, the file goes at the end.");
		ImGui::PushItemWidth (ImGui::GetFontSize () * 22);
		changed |= programField ("Image viewer", &preferences.imageViewer, "For PNG and TGA images");
		changed |= programField ("Audio player", &preferences.audioPlayer, "For WAV, OGG and FLAC sounds");
		changed |= programField ("Module player", &preferences.modPlayer, "For MOD, S3M, IT and XM music");
		ImGui::PopItemWidth ();

		ImGui::Spacing ();
		if (ImGui::Button ("Close")) showPreferences = false;
		// We save when a field is done, and when the window closes.
		if (changed || ! showPreferences) {
			if (! preferences.write (preferencesFile ())) {
				Log::get ().add (LogLine::LOG_ERROR, "Can't save the preferences in %s\n", preferencesFile ().c_str ());
			}
		}
	}
	ImGui::End ();
}

void Workspace::drawAboutWindow () {
	ImGui::SetNextWindowPos (ImGui::GetMainViewport ()->GetCenter (), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));
	if (ImGui::Begin ("About the SLUDGE Dev Kit", &showAbout, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {
		ImGui::Text ("SLUDGE Dev Kit %s", TEXT_VERSION);
		ImGui::Spacing ();
		ImGui::TextUnformatted ("By Tim Furnish, Rikard Peterson, Tobias Hansen and contributors.");
		ImGui::TextUnformatted ("\xC2\xA9 2000-2014 Hungry Software and contributors.");
		ImGui::Spacing ();
		ImGui::TextUnformatted ("This program is free software: you can redistribute it and/or modify it\n"
			"under the terms of the GNU General Public License as published by the Free\n"
			"Software Foundation, either version 3 of the License, or (at your option)\n"
			"any later version.");
		ImGui::Spacing ();
		ImGui::Separator ();
		ImGui::Text ("Renderer: %s", rendererName.c_str ());
		ImGui::Text ("Dear ImGui %s", ImGui::GetVersion ());
		ImGui::TextUnformatted ("Scripts in JetBrains Mono, under the SIL Open Font License 1.1.");
		ImGui::Spacing ();
		if (ImGui::Button ("Close")) showAbout = false;
	}
	ImGui::End ();
}

} // namespace devkit
