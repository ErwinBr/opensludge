#ifndef SLUDGE_DEVKIT_BUILD_PANEL_H
#define SLUDGE_DEVKIT_BUILD_PANEL_H

/*
 * The Build panel: the progress of a compile, and its errors, warnings and
 * other output, which open their file at their line.
 */

namespace devkit {

class CompileJob;
class ProjectDocument;
class WorkspaceActions;

class BuildPanel {
public:
	explicit BuildPanel (WorkspaceActions & actions);

	// Draw the contents of the panel, for the last compile of the project,
	// if any.
	void draw (CompileJob * job, ProjectDocument * project);

private:
	WorkspaceActions & actions;
	bool showErrors, showWarnings, showOutput;
	int selected;
};

} // namespace devkit

#endif
