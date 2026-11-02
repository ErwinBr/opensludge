#include <SDL3/SDL_stdinc.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"

#include "DocumentView.h"
#include "Log.h"
#include "Platform.h"
#include "Workspace.h"
#include "version.h"

namespace devkit {

// The names of the windows, which imgui.ini keeps the places of.
static const char * const START_PAGE = "Start";
static const char * const PROJECT_PANEL = "Project";
static const char * const BUILD_PANEL = "Build";
static const char * const INSPECTOR_PANEL = "Inspector";
static const char * const LOG_PANEL = "Log";

#if defined(__APPLE__)
static const char * const SHORTCUT_PREFIX = "Cmd+";
#else
static const char * const SHORTCUT_PREFIX = "Ctrl+";
#endif

static std::string shortcut (const char * key) {
	return std::string (SHORTCUT_PREFIX) + key;
}

Workspace::Workspace (Platform & p, const std::string & renderer)
	: platform (p), rendererName (renderer), recent (p.settingsFolder ().empty () ? std::string () : p.settingsFolder () + "recent.txt"),
	  manager (p, recent), quitting (false), resetLayout (false), layoutFrames (0), documentDock (0), focusDocument (NULL),
	  showProject (true), showBuild (true), showInspector (true), showLog (true),
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
	manager.opened = [this] (Document * document) {
		std::map<std::string, ViewMaker>::iterator maker = viewMakers.find (document->kind ());
		if (maker != viewMakers.end ()) views[document].reset (maker->second (document));
		focusDocument = document;
	};
	manager.closing = [this] (Document * document) {
		views.erase (document);
	};
}

Workspace::~Workspace () {
	views.clear ();
}

std::string Workspace::preferencesFile () const {
	return platform.settingsFolder () + "SLUDGE.ini";
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
	for (std::map<Document *, std::unique_ptr<DocumentView> >::const_iterator i = views.begin (); i != views.end (); ++ i) {
		if (i->second && i->second->animating ()) return true;
	}
	return false;
}

void Workspace::openFiles (const std::vector<std::string> & files) {
	for (size_t i = 0; i < files.size (); i ++) manager.open (files[i]);
}

std::string Workspace::title () const {
	Document * document = manager.active ();
	if (! document) return "SLUDGE Dev Kit";
	return document->displayName () + (document->isModified () ? "*" : "") + " \xE2\x80\x94 SLUDGE Dev Kit";
}

void Workspace::draw () {
	handleShortcuts ();
	drawMenuBar ();
	drawDockSpace ();
	drawStartPage ();
	drawDocuments ();
	if (showProject) drawProjectPanel ();
	if (showInspector) drawInspectorPanel ();
	if (showBuild) drawBuildPanel ();
	if (showLog) drawLogPanel ();
	// The frame after we placed the panels, when the tabs exist, we show
	// the Build in front of the Log.
	if (layoutFrames > 0 && -- layoutFrames == 0) ImGui::SetWindowFocus (BUILD_PANEL);
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
		if (ImGui::MenuItem ("Preferences...")) showPreferences = true;
		ImGui::EndMenu ();
	}
	if (ImGui::BeginMenu ("Project")) {
		ImGui::MenuItem ("Properties...", NULL, false, false);
		ImGui::Separator ();
		ImGui::MenuItem ("Compile", shortcut ("B").c_str (), false, false);
		ImGui::MenuItem ("Run Game", shortcut ("R").c_str (), false, false);
		ImGui::EndMenu ();
	}
	if (ImGui::BeginMenu ("View")) {
		ImGui::MenuItem (PROJECT_PANEL, NULL, &showProject);
		ImGui::MenuItem (BUILD_PANEL, NULL, &showBuild);
		ImGui::MenuItem (INSPECTOR_PANEL, NULL, &showInspector);
		ImGui::MenuItem (LOG_PANEL, NULL, &showLog);
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
		if (ImGui::MenuItem ("SLUDGE Website")) platform.openWithSystem ("https://opensludge.github.io");
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
	ImGui::TextWrapped ("This is the new Dev Kit, which is still being built. The project manager, the floor, "
		"z-buffer and sprite bank editors and the translation editor come in the next steps. Until then, "
		"use the GTK Dev Kit to edit your games.");

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
		if (! open) manager.close (document);
	}
}

void Workspace::drawProjectPanel () {
	if (ImGui::Begin (PROJECT_PANEL, &showProject)) hint ("No project is open.");
	ImGui::End ();
}

void Workspace::drawBuildPanel () {
	if (ImGui::Begin (BUILD_PANEL, &showBuild)) hint ("Compile a project to see its errors and warnings here.");
	ImGui::End ();
}

void Workspace::drawInspectorPanel () {
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
		ImGui::TextWrapped ("The programs that open the files of a project. Leave one empty for the program "
			"that your system opens the file with. Write {file} where the file goes, and {line} where "
			"the line of an error goes; without {file}, the file goes at the end.");
		ImGui::PushItemWidth (ImGui::GetFontSize () * 22);
		changed |= programField ("Script editor", &preferences.editor, "For example: code --goto {file}:{line}");
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
		ImGui::Spacing ();
		if (ImGui::Button ("Close")) showAbout = false;
	}
	ImGui::End ();
}

} // namespace devkit
