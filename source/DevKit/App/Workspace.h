#ifndef SLUDGE_DEVKIT_WORKSPACE_H
#define SLUDGE_DEVKIT_WORKSPACE_H

/*
 * The window of the Dev Kit: the menu bar, the dock space and the panels
 * in it. It shows the state of the editor and passes on what the user
 * does; the state itself lives outside it, in Core.
 */

#include <string>
#include <vector>

namespace devkit {

class Platform;

class Workspace {
public:
	Workspace (Platform & platform, const std::string & rendererName);

	// Draw the interface for one frame, between ImGui::NewFrame and
	// ImGui::Render.
	void draw ();

	// The user asked to quit, with the window's close button or the menu.
	void requestQuit ();
	bool wantsToQuit () const { return quitting; }

	// Files that were dropped on the window or named on the command line.
	void openFiles (const std::vector<std::string> & files);

	// The title of the window.
	std::string title () const;

private:
	void drawMenuBar ();
	void drawDockSpace ();
	void buildDefaultLayout (unsigned int dockspace);
	void drawStartPage ();
	void drawProjectPanel ();
	void drawBuildPanel ();
	void drawInspectorPanel ();
	void drawLogPanel ();
	void drawAboutWindow ();
	void handleShortcuts ();

	Platform & platform;
	std::string rendererName;
	bool quitting;
	bool resetLayout;
	int layoutFrames;	// Frames until the new layout is done

	bool showProject, showBuild, showInspector, showLog;
	bool showAbout, showDemo;

	// The Log panel.
	bool logShowDebug;
	bool logAutoScroll;
	unsigned int logVersionSeen;
};

} // namespace devkit

#endif
