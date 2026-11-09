#include "animedit.h"

#include <math.h>
#include <algorithm>

namespace {

const double pi = 3.14159265358979323846;

bool fail (std::string & why, const char * what) {
	why = what;
	return false;
}

// The rig with its parts in a new order: order[i] is the index, in rig, of
// the part that comes i-th. The parent of each must come before it.
RigDefinition reordered (const RigDefinition & rig, const std::vector<int> & order) {
	std::vector<int> newIndex (rig.parts.size (), -1);
	for (size_t i = 0; i < order.size (); i ++) newIndex[order[i]] = (int) i;
	RigDefinition out = rig;
	out.parts.clear ();
	for (size_t i = 0; i < order.size (); i ++) {
		RigPart p = rig.parts[order[i]];
		if (p.parent >= 0) p.parent = newIndex[p.parent];
		out.parts.push_back (p);
	}
	return out;
}

}

int partWithId (const RigDefinition & rig, PartId id) {
	for (size_t i = 0; i < rig.parts.size (); i ++) {
		if (rig.parts[i].id == id) return (int) i;
	}
	return -1;
}

std::vector<int> partChildren (const RigDefinition & rig, int part) {
	std::vector<int> children;
	for (size_t i = 0; i < rig.parts.size (); i ++) {
		if (rig.parts[i].parent == part && part >= 0) children.push_back ((int) i);
	}
	return children;
}

bool partIsUnder (const RigDefinition & rig, int part, int root) {
	for (int p = part; p >= 0 && p < (int) rig.parts.size (); p = rig.parts[p].parent) {
		if (p == root) return true;
	}
	return false;
}

Transform2D restWorld (const RigDefinition & rig, int part) {
	if (part < 0 || part >= (int) rig.parts.size ()) return Transform2D ();
	const RigPart & p = rig.parts[part];
	Transform2D above = p.parent >= 0 ? restWorld (rig, p.parent) : Transform2D ();
	return above * localTransform (p.rest);
}

bool poseOfTransform (const Transform2D & t, PartPose & out) {
	// T R S F: the first column is s f (cos, sin), the second s (-sin, cos).
	double s = sqrt ((double) t.c * t.c + (double) t.d * t.d);
	if (! (s > 1e-9) || ! isfinite (s)) return false;
	out.x = t.tx;
	out.y = t.ty;
	out.rotation = (float) (atan2 (- (double) t.c, (double) t.d) * 180.0 / pi);
	out.scale = (float) s;
	out.flip = t.mirrors ();
	return true;
}

bool renamePart (RigDefinition & rig, int part, const std::string & name, std::string & why) {
	if (part <= 0 || part >= (int) rig.parts.size ()) return fail (why, "The character itself has no name");
	RigDefinition renamed = rig;
	renamed.parts[part].name = name;
	if (name.empty ()) return fail (why, "A part needs a name");
	if (rig.findPart (name) >= 0 && rig.findPart (name) != part) return fail (why, "Another part has that name");
	if (! validRig (renamed, why)) return false;
	rig = renamed;
	return true;
}

bool removePart (RigDefinition & rig, int part, std::string & why) {
	if (part <= 0 || part >= (int) rig.parts.size ()) return fail (why, "The character itself can't be removed");
	std::vector<int> order;
	for (size_t i = 0; i < rig.parts.size (); i ++) {
		if (! partIsUnder (rig, (int) i, part)) order.push_back ((int) i);
	}
	rig = reordered (rig, order);
	return true;
}

bool placePart (RigDefinition & rig, int part, int parent, int position, std::string & why) {
	int n = (int) rig.parts.size ();
	if (part <= 0 || part >= n) return fail (why, "The character itself can't move");
	if (parent < 0 || parent >= n) return fail (why, "There is no such part to put it under");
	if (partIsUnder (rig, parent, part)) return fail (why, "A part can't go under itself, or what is attached to it");

	RigDefinition moved = rig;
	if (rig.parts[part].parent != parent) {
		// The same place in the world, from the new parent.
		Transform2D up;
		if (! restWorld (rig, parent).inverse (up)) return fail (why, "The part to put it under is scaled to nothing");
		PartPose pose;
		if (! poseOfTransform (up * restWorld (rig, part), pose)) return fail (why, "The part is scaled to nothing");
		pose.visible = rig.parts[part].rest.visible;
		moved.parts[part].rest = pose;
		moved.parts[part].parent = parent;
	}

	// The order without the part and what hangs from it, then those again,
	// before the sibling they go before, or after all of parent's others.
	std::vector<int> rest, subtree;
	for (int i = 0; i < n; i ++) (partIsUnder (moved, i, part) ? subtree : rest).push_back (i);
	std::vector<int> siblings;
	for (size_t i = 0; i < rest.size (); i ++) {
		if (moved.parts[rest[i]].parent == parent) siblings.push_back (rest[i]);
	}
	size_t at;
	if (position >= 0 && position < (int) siblings.size ()) {
		at = std::find (rest.begin (), rest.end (), siblings[position]) - rest.begin ();
	} else {
		// After parent and everything attached to it.
		at = 0;
		for (size_t i = 0; i < rest.size (); i ++) {
			if (partIsUnder (moved, rest[i], parent)) at = i + 1;
		}
	}
	rest.insert (rest.begin () + at, subtree.begin (), subtree.end ());
	RigDefinition out = reordered (moved, rest);
	if (! validRig (out, why)) return false;
	rig = out;
	return true;
}
