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

#include "BuildPanel.h"
#include "CanvasView.h"
#include "CompileJob.h"
#include "DocumentManager.h"
#include "FileCheckWindow.h"
#include "FileMemory.h"
#include "Preferences.h"
#include "ProjectDocument.h"
#include "RecentFiles.h"
#include "WorkspaceActions.h"

namespace devkit {

class DocumentView;
class Platform;

class Workspace : public WorkspaceActions {
public:
	// The editors draw their pictures with graphics.
	Workspace (Platform & platform, const std::string & rendererName, const CanvasGraphics & graphics);
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
	// Whether we need another frame soon, as while we compile.
	bool animating () const;
	// The frame has been drawn and presented.
	void frameDone ();

	// Files that were dropped on the window or named on the command line.
	void openFiles (const std::vector<std::string> & files);

	// The title of the window.
	std::string title () const;

	// WorkspaceActions
	Platform & platform () { return thePlatform; }
	void openFile (const std::string & path, int line);
	void compile ();
	bool compiling () const { return job && job->running (); }
	void runGame ();
	void revealInspector () { focusInspector = true; }
	void checkFiles () { fileCheck.open (project ()); }
	std::string projectFile () const { return project () ? project ()->path () : std::string (); }

	// Whether the last compile succeeded, for the tests.
	bool compileSucceeded () const { return job && job->succeeded; }
	// Close the tab of the active document in the next frame, as its X
	// does, for the tests.
	void closeActiveTab () { closeTab = true; }

private:
	ProjectDocument * project () const;
	// A program of Open SLUDGE that comes with the Dev Kit, such as
	// sludge-compiler, or its name for the PATH when we can't find it.
	std::string findProgram (const std::string & name);
	void startCompile ();
	// Open the help of the Dev Kit, which comes with it, in the browser.
	void openHelp ();
	// Start the program of a preference for a file, or the system's.
	void openWith (const std::string & command, const std::string & path, int line);

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

	Platform & thePlatform;
	std::string rendererName;
	CanvasGraphics graphics;
	RecentFiles recent;
	FileMemory memory;
	DocumentManager manager;
	Preferences preferences;
	std::map<std::string, ViewMaker> viewMakers;
	std::map<Document *, std::unique_ptr<DocumentView> > views;
	// The views of documents that closed in this frame. Their textures may
	// still be in what ImGui has to draw, so they go when it is presented.
	std::vector<std::unique_ptr<DocumentView> > closedViews;
	std::unique_ptr<CompileJob> job;
	BuildPanel buildPanel;
	FileCheckWindow fileCheck;

	bool quitting;
	bool projectPanelHovered;	// In the last frame, for files that the user drops
	bool focusInspector;
	bool resetLayout;
	int layoutFrames;			// Frames until the new layout is done
	unsigned int documentDock;	// Where new documents go
	Document * focusDocument;	// A document to bring to the front
	bool closeTab;				// See closeActiveTab

	bool showProject, showBuild, showInspector, showLog;
	bool showPreferences, showAbout, showDemo;

	// The Log panel.
	bool logShowDebug;
	bool logAutoScroll;
	unsigned int logVersionSeen;
};

} // namespace devkit

#endif
