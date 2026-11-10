#ifndef SLUDGE_DEVKIT_PART_TREE_H
#define SLUDGE_DEVKIT_PART_TREE_H

/*
 * The parts of a rig as a tree, as the character creator and the animation
 * editor show them: the character at the top, each part under the part it
 * is attached to, joints (parts without a sprite) marked, and at the end of
 * each row a lock. A locked part's name is dimmed, and a click on it
 * doesn't select it. Dragging a part onto another attaches it there, when
 * the editor allows it (drop).
 */

#include <functional>

#include "animmodel.h"

namespace devkit {

struct PartTree {
	std::function<bool (PartId)> isLocked;
	std::function<void (PartId, bool)> setLocked;
	std::function<void (PartId)> select;
	// A part dragged onto another; empty, parts can't be dragged.
	std::function<void (PartId dragged, PartId onto)> drop;
};

// The tree of rig, from its root, with the part selected (if any) shown so.
void drawPartTree (const RigDefinition & rig, bool haveSelection, PartId selected, const PartTree & tree);

// The rows of the tree are as high as a field, as the rows of the timeline
// are; this is the open or closed arrow of a row, drawn as the tree draws it,
// at the cursor, as wide as foldWidth () and a field high. True when clicked.
bool foldArrow (const char * id, bool open);
float foldWidth ();

} // namespace devkit

#endif
