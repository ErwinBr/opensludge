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
struct ClipItem;
class Document;
class Platform;
class SoundPreview;

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
	// The animation editor's Auto key, a preference: whether a change to a
	// part keys it at the playhead at once.
	virtual bool autoKey () const = 0;
	// What the animation editor copied, to paste into any clip.
	virtual std::vector<ClipItem> & clipClipboard () = 0;
	virtual const std::vector<ClipItem> & clipClipboard () const = 0;
	virtual void setAutoKey (bool on) = 0;
	// How much of the animation editor's height the picture takes; saved
	// with the preferences.
	virtual float clipSplit () const = 0;
	virtual void setClipSplit (float fraction) = 0;
	// The sounds that clips' events play, for all of them.
	virtual SoundPreview & sounds () = 0;
};

} // namespace devkit

#endif
