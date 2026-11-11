#ifndef SLUDGE_DEVKIT_WORKSPACE_H
#define SLUDGE_DEVKIT_WORKSPACE_H

/*
 * The window of the Dev Kit: the menu bar, the dock space and the panels
 * in it. It shows the state of the editor and passes on what the user
 * does; the state itself lives outside it, in Core.
 */

#include <functional>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

#include "BuildPanel.h"
#include "CanvasView.h"
#include "CompileJob.h"
#include "DocumentManager.h"
#include "FileCheckWindow.h"
#include "FileMemory.h"
#include "FindPanel.h"
#include "Preferences.h"
#include "ProjectDocument.h"
#include "RecentFiles.h"
#include "ScriptIndex.h"
#include "SoundPreview.h"
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
	// Files dropped on the window: as openFiles, but the active editor may
	// take any of them first, as the character creator takes a sprite bank
	// for the selected part.
	void dropFiles (const std::vector<std::string> & files);

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
	double tickMilliseconds () const;
	Document * findDocument (const std::string & path) const { return manager.find (path); }
	bool isOpen (const Document * document) const;
	std::vector<ClipDocument *> clipsForRename (const animformat::AssetId & rig, const std::string & part, const std::string & folder);
	bool autoKey () const { return preferences.autoKey; }
	std::vector<ClipItem> & clipClipboard () { return clipItems; }
	const std::vector<ClipItem> & clipClipboard () const { return clipItems; }
	void setAutoKey (bool on);
	float clipSplit () const { return preferences.clipSplit; }
	void setClipSplit (float fraction);
	SoundPreview & sounds () { return soundPreview; }
	const ScriptIndex & scriptIndex () const { return index; }
	void showInScript (const std::string & path, int line, int column, int length = 0);
	void openHelpPage (const std::string & page);
	std::vector<BuildMessage> buildMessagesFor (const std::string & path, unsigned int * serial) const;
	bool knowsAllNames (const std::string & path) const;
	float scriptFontSize () const;
	void setScriptFontSize (float size);

	// Whether the last compile succeeded, for the tests.
	bool compileSucceeded () const { return job && job->succeeded; }
	// Close the tab of the active document in the next frame, as its X
	// does, for the tests.
	void closeActiveTab () { closeTab = true; }
	// The editor of the active document, if it has one, for the tests.
	DocumentView * activeView () const;

private:
	ProjectDocument * project () const;
	// A program of Open SLUDGE that comes with the Dev Kit, such as
	// sludge-compiler, or its name for the PATH when we can't find it.
	std::string findProgram (const std::string & name);
	void startCompile ();
	// Open the help of the Dev Kit, which comes with it, in the browser.
	void openHelp ();
	// The file of the help, or empty when it isn't installed.
	std::string helpFile (const std::string & page);
	// Keep the index of the scripts as the open ones and the project are.
	void updateScriptIndex ();
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
	void drawFindPanel ();
	// Open Find in Project, with the selection of the script editor.
	void openFindInProject ();
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
	std::vector<ClipItem> clipItems;
	SoundPreview soundPreview;
	std::map<std::string, ViewMaker> viewMakers;
	std::map<Document *, std::unique_ptr<DocumentView> > views;
	// The views of documents that closed in this frame. Their textures may
	// still be in what ImGui has to draw, so they go when it is presented.
	std::vector<std::unique_ptr<DocumentView> > closedViews;
	std::unique_ptr<CompileJob> job;
	unsigned int compiles;		// Started, for buildMessagesFor
	ScriptIndex index;
	double nextDiskIndex;		// When we look at the project's scripts on disk again
	std::set<std::string> projectScripts;	// Their names in the index
	bool projectIndexed;		// The index has all of them
	float looseFontSize;		// The size of the scripts without a project
	BuildPanel buildPanel;
	FindPanel findPanel;
	FileCheckWindow fileCheck;

	bool quitting;
	bool projectPanelHovered;	// In the last frame, for files that the user drops
	bool focusInspector;
	bool resetLayout;
	int layoutFrames;			// Frames until the new layout is done
	unsigned int documentDock;	// Where new documents go
	Document * focusDocument;	// A document to bring to the front
	// A document to bring to the front again a few frames on, after the
	// documents that opened behind it (as a rename opens clips) have
	// appeared and taken the focus.
	Document * focusLater;
	int focusLaterFrames;
	bool closeTab;				// See closeActiveTab

	bool showProject, showBuild, showInspector, showLog, showFind;
	bool focusFind;			// Bring the Find in Project panel to the front
	bool showPreferences, showAbout, showDemo;

	// The Log panel.
	bool logShowDebug;
	bool logAutoScroll;
	unsigned int logVersionSeen;
};

} // namespace devkit

#endif
