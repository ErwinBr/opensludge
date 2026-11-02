#ifndef SLUDGE_DEVKIT_PROJECT_VIEW_H
#define SLUDGE_DEVKIT_PROJECT_VIEW_H

/*
 * The project in the Project panel: its files, in the order in which the
 * compiler takes them, and the resources of the selected scripts. Its
 * settings are in the Inspector.
 */

#include <set>
#include <string>
#include <vector>

#include "DocumentView.h"

namespace devkit {

class ProjectDocument;
class WorkspaceActions;

class ProjectView : public DocumentView {
public:
	ProjectView (ProjectDocument * project, WorkspaceActions & actions);

	Document * document ();
	void draw ();
	void drawInspector ();

	// Ask for files and add them.
	void addFilesWithDialog ();

private:
	void drawToolbar ();
	void drawFiles (float height);
	void drawResources ();
	void removeSelected ();
	void select (int index);
	void updateResources ();

	ProjectDocument * project;
	WorkspaceActions & actions;

	// What the user selected, and the files then, so that we can tell when
	// they change, as with undo.
	std::set<int> selected;
	int anchor;
	std::vector<std::string> filesSeen;
	std::vector<std::string> resources;
	bool resourcesStale;
};

} // namespace devkit

#endif
