#ifndef ANIMEDIT_H
#define ANIMEDIT_H

/*
 * Changing the shape of a rig, as the Dev Kit's character creator does:
 * renaming, removing and moving parts. Parts come before the parts attached
 * to them (validRig), and the order of parts of the same parent is the order
 * that they are drawn in when their depths are the same; these keep both
 * right, whatever order the parts were added in.
 *
 * Moving a part changes the indices of parts: a part is found again by its
 * ID (partWithId), which never changes. On failure, the functions change
 * nothing and return false, with why.
 */

#include <string>
#include <vector>

#include "animmodel.h"
#include "animpose.h"

// The index of the part of an ID, or -1.
int partWithId (const RigDefinition & rig, PartId id);

// The parts attached to part, in their order.
std::vector<int> partChildren (const RigDefinition & rig, int part);

// Whether part is root, or attached to it, however far down.
bool partIsUnder (const RigDefinition & rig, int part, int root);

// The world transform of a part in the rig's rest pose, with the root at the
// origin, unscaled.
Transform2D restWorld (const RigDefinition & rig, int part);

// The place, turn, scales and flip of a transform of them (as
// localTransform makes): false if it has none, as when it is scaled to 0.
// A skew, which no pose has, is left out.
bool poseOfTransform (const Transform2D & t, PartPose & out);

// Rename a part (not the root), to a name no other part has.
bool renamePart (RigDefinition & rig, int part, const std::string & name, std::string & why);

// Remove a part (not the root), and every part attached to it.
bool removePart (RigDefinition & rig, int part, std::string & why);

// Put a part (not the root), and what is attached to it, under parent, at
// position among parent's other children (0 first; their number, or more,
// last). Under another parent, its rest pose changes so that it stays where
// it is; a part can't go under itself or what is attached to it.
bool placePart (RigDefinition & rig, int part, int parent, int position, std::string & why);

#endif
