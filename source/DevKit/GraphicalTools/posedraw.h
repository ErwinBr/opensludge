/*
 * posedraw.h - Part of the SLUDGE Dev Kit
 *
 * Drawing the pose of a rig (source/Anim/animpose.h) with Draw2D, as the
 * character creator and the animation editor show their characters: each
 * part's sprite turned, scaled and mirrored through its transform, in the
 * order the engine draws them, and over it the bones, the origins and the
 * handles of a part. The picture is in the space of the character (+Y
 * down, the root at 0, 0), drawn with Draw2D::setViewArea.
 */

#ifndef __POSEDRAW_H__
#define __POSEDRAW_H__

#include <functional>
#include <memory>
#include <string>

#include "animbank.h"
#include "animpose.h"
#include "sprites.h"

class Draw2D;

// A sprite bank of the Dev Kit (sprites.h), as the animation library sees
// one. It loads the bank, and forgets it when it goes; whoever draws it
// must forget its pictures first (Draw2D::forgetPictures).
class DevKitAnimBank : public AnimSpriteBank {
public:
	DevKitAnimBank ();
	~DevKitAnimBank ();
	// False if the file isn't a sprite bank.
	bool load (const std::string & file);

	int animSpriteCount () const { return bank.total; }
	AnimSpriteSize animSpriteSize (int number) const;
	const spriteBank & sprites () const { return bank; }

private:
	DevKitAnimBank (const DevKitAnimBank &);
	DevKitAnimBank & operator= (const DevKitAnimBank &);
	spriteBank bank;
};

// The Dev Kit's bank of a bank of the library: every bank that the Dev Kit
// gives the library is a DevKitAnimBank.
inline const spriteBank * devkitBank (const AnimBankRef & bank) {
	return bank ? & static_cast<const DevKitAnimBank *> (bank.get ()) -> sprites () : NULL;
}

// Draw the parts of a pose, back to front.
void drawPose (Draw2D & draw, const Pose & pose);

// The part (an index of the rig) whose sprite shows at x, y, the one on top,
// or -1. With canPick, only of the parts it says yes to (not locked ones).
int partAt (const Pose & pose, float x, float y, const std::function<bool (int)> & canPick = std::function<bool (int)> ());

// A handle that scales a part: at a corner of its sprite's box, it scales
// both of the part's axes; in the middle of a side, the one across it.
struct ScaleHandle {
	enum Axes { X = 1, Y = 2, BOTH = 3 };
	float x, y;
	int axes;
};

// The handles of a part, in the space of the character, for a ring of
// radius around its origin: the knob that turns it, on its x axis, and the
// scale handles, on its sprite's box (or for a joint, which has none, on a
// square around its origin): the 4 corners first, then the middles of the
// sides.
struct PartHandles {
	float originX, originY;
	float turnX, turnY;
	float radius;
	ScaleHandle scale[8];
};
PartHandles partHandles (const Pose & pose, int part, float radius);

// The scales of a part whose handle was dragged from handle to mouse (in the
// space of the character), with parent the world transform of its parent
// and start its pose as the drag began: in whole percents, of 1% at least.
// Uniformly, the axes scale together, by how far the mouse is from the
// part's origin; apart, each of the handle's axes by how far along it.
void scaleFromHandle (const Transform2D & parent, const PartPose & start, const ScaleHandle & handle,
	bool uniform, float mouseX, float mouseY, float & scaleX, float & scaleY);

// The bones from each part's origin to its parent's, the origins (a cross
// for a joint, which has no sprite), and for the selected part (or -1) the
// outline of its sprite and its handles: the scale handles of the sides only
// with sides. pixel is the size of a pixel of the view, in the space of the
// character.
void drawPoseOverlay (Draw2D & draw, const RigDefinition & rig, const Pose & pose, int selected, float pixel, bool sides = false);

#endif
