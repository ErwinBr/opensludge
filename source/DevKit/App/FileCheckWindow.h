#ifndef SLUDGE_DEVKIT_FILE_CHECK_WINDOW_H
#define SLUDGE_DEVKIT_FILE_CHECK_WINDOW_H

/*
 * The Check Files window: the problems that keep a project from building
 * on every system (ProjectCheck.h), and fixing the ones that the user
 * chooses.
 */

#include <vector>

#include "ProjectCheck.h"

namespace devkit {

class ProjectDocument;
class WorkspaceActions;

class FileCheckWindow {
public:
	explicit FileCheckWindow (WorkspaceActions & actions);

	// Check the project again, and show the window.
	void open (ProjectDocument * project);
	void draw (ProjectDocument * project);
	bool isOpen () const { return visible; }

private:
	void check (ProjectDocument * project);
	void fix (ProjectDocument * project);

	WorkspaceActions & actions;
	bool visible;
	std::vector<FileProblem> problems;
	std::vector<bool> chosen;
	CodePage codePage;
};

} // namespace devkit

#endif
