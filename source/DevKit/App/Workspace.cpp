#include "imgui.h"
#include "imgui_internal.h"

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
	: platform (p), rendererName (renderer), quitting (false), resetLayout (false), layoutFrames (0),
	  showProject (true), showBuild (true), showInspector (true), showLog (true),
	  showAbout (false), showDemo (false),
	  logShowDebug (false), logAutoScroll (true), logVersionSeen (0) {}

void Workspace::requestQuit () {
	// Later, we ask to save the documents with changes first.
	quitting = true;
}

void Workspace::openFiles (const std::vector<std::string> & files) {
	for (size_t i = 0; i < files.size (); i ++) {
		Log::get ().add (LogLine::LOG_INFO, "This Dev Kit can't open files yet: %s\n", files[i].c_str ());
	}
}

std::string Workspace::title () const {
	return "SLUDGE Dev Kit";
}

void Workspace::draw () {
	handleShortcuts ();
	drawMenuBar ();
	drawDockSpace ();
	drawStartPage ();
	if (showProject) drawProjectPanel ();
	if (showInspector) drawInspectorPanel ();
	if (showBuild) drawBuildPanel ();
	if (showLog) drawLogPanel ();
	// The frame after we placed the panels, when the tabs exist, we show
	// the Build in front of the Log.
	if (layoutFrames > 0 && -- layoutFrames == 0) ImGui::SetWindowFocus (BUILD_PANEL);
	if (showAbout) drawAboutWindow ();
	if (showDemo) ImGui::ShowDemoWindow (&showDemo);
}

void Workspace::handleShortcuts () {
	if (ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiKey_Q, ImGuiInputFlags_RouteGlobal)) requestQuit ();
}

void Workspace::drawMenuBar () {
	if (! ImGui::BeginMainMenuBar ()) return;

	if (ImGui::BeginMenu ("File")) {
		// The documents come with the editors, in the next steps.
		if (ImGui::BeginMenu ("New", false)) ImGui::EndMenu ();
		ImGui::MenuItem ("Open...", shortcut ("O").c_str (), false, false);
		if (ImGui::BeginMenu ("Open Recent", false)) ImGui::EndMenu ();
		ImGui::Separator ();
		ImGui::MenuItem ("Save", shortcut ("S").c_str (), false, false);
		ImGui::MenuItem ("Save As...", shortcut ("Shift+S").c_str (), false, false);
		ImGui::MenuItem ("Save All", NULL, false, false);
		ImGui::MenuItem ("Close", shortcut ("W").c_str (), false, false);
		ImGui::Separator ();
		if (ImGui::MenuItem ("Quit", shortcut ("Q").c_str ())) requestQuit ();
		ImGui::EndMenu ();
	}
	if (ImGui::BeginMenu ("Edit")) {
		ImGui::MenuItem ("Undo", shortcut ("Z").c_str (), false, false);
		ImGui::MenuItem ("Redo", shortcut ("Shift+Z").c_str (), false, false);
		ImGui::Separator ();
		ImGui::MenuItem ("Preferences...", NULL, false, false);
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
	ImGui::End ();
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
	if (ImGui::Begin (INSPECTOR_PANEL, &showInspector)) hint ("Select something to see its properties.");
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
