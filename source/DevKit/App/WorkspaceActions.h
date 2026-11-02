#ifndef SLUDGE_DEVKIT_WORKSPACE_ACTIONS_H
#define SLUDGE_DEVKIT_WORKSPACE_ACTIONS_H

/*
 * What the views and panels ask the workspace to do.
 */

#include <string>

namespace devkit {

class Platform;

class WorkspaceActions {
public:
	virtual ~WorkspaceActions () {}

	virtual Platform & platform () = 0;
	// Open a file of the project: in the Dev Kit if it edits it, and
	// otherwise in the program of the preferences or of the system, at the
	// line if we know it (0 if not).
	virtual void openFile (const std::string & path, int line) = 0;
	virtual void compile () = 0;
	virtual bool compiling () const = 0;
	virtual void runGame () = 0;
	// Bring the Inspector to the front.
	virtual void revealInspector () = 0;
};

} // namespace devkit

#endif
