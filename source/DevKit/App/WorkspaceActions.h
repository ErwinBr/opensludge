#ifndef SLUDGE_DEVKIT_WORKSPACE_ACTIONS_H
#define SLUDGE_DEVKIT_WORKSPACE_ACTIONS_H

/*
 * What the views and panels ask the workspace to do.
 */

#include <string>
#include <vector>

#include "animformat.h"

namespace devkit {

class ClipDocument;
class Document;
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
	// Check the files of the project, in the Check Files window.
	virtual void checkFiles () = 0;
	// The file of the open project, or empty.
	virtual std::string projectFile () const = 0;
	// How long a tick of the game is, in milliseconds: the project's, or
	// SLUDGE's without one.
	virtual double tickMilliseconds () const = 0;
	// The open document of a file, or NULL.
	virtual Document * findDocument (const std::string & path) const = 0;
	virtual bool isOpen (const Document * document) const = 0;
	// The clips of a rig that have tracks of a part, opened as documents
	// (those of the project's folder, and those open already), for a
	// rename of the part.
	virtual std::vector<ClipDocument *> clipsForRename (const animformat::AssetId & rig, const std::string & part, const std::string & folder) = 0;
};

} // namespace devkit

#endif
