#ifndef SLUDGE_DEVKIT_WORKSPACE_H
#define SLUDGE_DEVKIT_WORKSPACE_H

/*
 * The window of the Dev Kit: the menu bar, the dock space and the panels
 * in it. It shows the state of the editor and passes on what the user
 * does; the state itself lives outside it, in Core.
 */

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "DocumentManager.h"
#include "Preferences.h"
#include "RecentFiles.h"

namespace devkit {

class DocumentView;
class Platform;

class Workspace {
public:
	Workspace (Platform & platform, const std::string & rendererName);
	~Workspace ();

	DocumentManager & documents () { return manager; }

	// Make the editors of a kind of document (Document::kind) with this.
	typedef std::function<DocumentView * (Document *)> ViewMaker;
	void registerView (const std::string & kind, ViewMaker make);

	// Draw the interface for one frame, between ImGui::NewFrame and
	// ImGui::Render.
	void draw ();

	// The user asked to quit, with the window's close button or the menu.
	// We ask about the documents with changes first.
	void requestQuit ();
	bool wantsToQuit () const { return quitting; }
	// Whether a view needs another frame soon.
	bool animating () const;

	// Files that were dropped on the window or named on the command line.
	void openFiles (const std::vector<std::string> & files);

	// The title of the window.
	std::string title () const;

private:
	void drawMenuBar ();
	void drawRecentMenu ();
	void drawDockSpace ();
	void buildDefaultLayout (unsigned int dockspace);
	void drawStartPage ();
	void drawDocuments ();
	void drawProjectPanel ();
	void drawBuildPanel ();
	void drawInspectorPanel ();
	void drawLogPanel ();
	void drawPreferencesWindow ();
	void drawAboutWindow ();
	void handleShortcuts ();
	std::string preferencesFile () const;

	Platform & platform;
	std::string rendererName;
	RecentFiles recent;
	DocumentManager manager;
	Preferences preferences;
	std::map<std::string, ViewMaker> viewMakers;
	std::map<Document *, std::unique_ptr<DocumentView> > views;

	bool quitting;
	bool resetLayout;
	int layoutFrames;			// Frames until the new layout is done
	unsigned int documentDock;	// Where new documents go
	Document * focusDocument;	// A document to bring to the front

	bool showProject, showBuild, showInspector, showLog;
	bool showPreferences, showAbout, showDemo;

	// The Log panel.
	bool logShowDebug;
	bool logAutoScroll;
	unsigned int logVersionSeen;
};

} // namespace devkit

#endif
