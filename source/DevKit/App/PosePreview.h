#ifndef SLUDGE_DEVKIT_POSE_PREVIEW_H
#define SLUDGE_DEVKIT_POSE_PREVIEW_H

/*
 * What the character creator and the animation editor show of a character:
 * the sprite banks of its rig and clips, loaded once by path, and the rig
 * and clips with their banks in them, ready to pose with the animation
 * library (evaluateClip, evaluatePose) and draw with posedraw.h. The paths
 * are from a folder: the project's, or the document's own.
 */

#include <map>
#include <memory>
#include <string>

#include "animformat.h"
#include "posedraw.h"

class Draw2D;

namespace devkit {

class PosePreview {
public:
	explicit PosePreview (Draw2D * draw) : draw (draw) {}
	// Forgets the pictures of its banks.
	~PosePreview ();

	// The bank of a path from folder, loaded once; NULL if it can't be.
	AnimBankRef bank (const std::string & folder, const std::string & path);
	// A rig or clip of an asset, with the banks of its sprites.
	RigDefinitionRef rig (const animformat::Asset & asset, const std::string & folder);
	ClipDefinitionRef clip (const animformat::Asset & asset, const std::string & folder);

private:
	PosePreview (const PosePreview &);
	PosePreview & operator= (const PosePreview &);

	Draw2D * draw;
	std::map<std::string, std::shared_ptr<DevKitAnimBank> > banks;	// By full path
};

} // namespace devkit

#endif
