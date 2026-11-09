#ifndef ANIMPOSE_H
#define ANIMPOSE_H

/*
 * The pose of a rig: where each part of a character is, and what it shows,
 * from the samples of a clip (evaluateClip, animmodel.h). A pure function
 * of the rig, the samples and where the character is; drawing and hit
 * testing read the pose.
 *
 * The coordinate convention of animmodel.h: +X right, +Y down, angles in
 * degrees clockwise; a part's origin is its sprite's hotspot. A part's
 * local transform is
 *
 *     L = T (x, y) . R (rotation) . S (scale) . F (flip)
 *
 * where F mirrors local X (x -> -x), and its world transform is
 * W (part) = W (parent) . L (part), with W of the root's parent the
 * character's own transform. So turning, scaling or flipping a part does
 * the same to all that is attached to it, and flipping the root mirrors the
 * character.
 *
 * What a part shows:
 *  - VISIBLE 0 hides the part and all that is attached to it.
 *  - Its sprite: from its SPRITE track, or its own (the rig's) sprite. A
 *    key of DEFAULT is the part's own sprite; NONE is no sprite, for that
 *    part only. A sprite that its bank lacks falls back to the part's own
 *    sprite, and is reported once (animDiagnostic, animmodel.h).
 *  - A part without a sprite still has its transform, so that what is
 *    attached to it is placed.
 *
 * Tracks are bound to parts by name. A track whose part the rig lacks is
 * left out, and reported once for its clip and rig.
 *
 * Layers: a character plays clips on layers, 0 (its costume's, or
 * animate's) and up, which apply in ascending order over the rig's rest
 * pose, then the script's overrides (setPartProperty) over all. Per
 * channel:
 *
 *                   override layer   additive layer
 *     X, Y          replace          add
 *     ROTATION      replace          add
 *     SCALE         replace          multiply
 *     SPRITE, VISIBLE, DEPTH, FLIP
 *                   replace          replace
 *
 * Discrete channels have no meaning to add, so every layer replaces them:
 * where two layers give a part a sprite, the higher layer's shows. Script
 * overrides replace.
 */

#include <memory>
#include <string>
#include <vector>

#include "animmodel.h"

// An affine transform: x' = a x + c y + tx, y' = b x + d y + ty.
struct Transform2D {
	float a, b, c, d, tx, ty;

	Transform2D () : a (1), b (0), c (0), d (1), tx (0), ty (0) {}
	static Transform2D translation (float x, float y);
	static Transform2D rotation (float degrees);		// Clockwise on screen
	static Transform2D scaling (float s);
	static Transform2D flipX ();

	// This, after other: (this * other) (p) = this (other (p)).
	Transform2D operator* (const Transform2D & other) const;
	void apply (float x, float y, float & outX, float & outY) const;
	// False when it has no inverse (scaled to 0).
	bool inverse (Transform2D & out) const;
	// Whether it mirrors (an odd number of flips).
	bool mirrors () const { return a * d - b * c < 0; }
};

// The local transform of a pose: T . R . S . F.
Transform2D localTransform (const PartPose & p);

// A part, posed.
struct PosedPart {
	PartPose local;		// After the clip's tracks
	int depth;
	bool shown;			// It and every part it is attached to are visible
	SpriteRef sprite;	// What it shows: kind SPRITE, or NONE for nothing
	Transform2D world;	// Of its origin, which its children are placed from
	// What its sprite is drawn through, with the sprite's hotspot at the
	// origin: world, moved back by the part's pivot.
	Transform2D spriteWorld;
};

struct Pose {
	std::vector<PosedPart> parts;		// As the rig's parts
	// The parts to draw, back to front: shown, with a sprite, by depth, then
	// in rig order.
	std::vector<int> drawOrder;
};

// Which part each track of a clip drives, for a rig; worked out once for a
// clip and rig, and again when either is another. It doesn't keep them.
class PoseBinding {
public:
	PoseBinding () {}
	// The part of each track of clip (-1 for none), bound now if need be.
	const std::vector<int> & bind (const ClipDefinitionRef & clip, const RigDefinitionRef & rig);
private:
	std::weak_ptr<const ClipDefinition> clip;
	std::weak_ptr<const RigDefinition> rig;
	std::vector<int> partOfTrack;
};

enum LayerBlend { LAYER_BLEND_OVERRIDE, LAYER_BLEND_ADD, NUM_LAYER_BLENDS };

// A layer to pose: the samples of its clip (evaluateClip), how it blends,
// and the binding of its tracks to the rig's parts, which it keeps.
struct LayerSamples {
	ClipDefinitionRef clip;
	const TrackSamples * samples;
	LayerBlend blend;
	PoseBinding * binding;
};

// A property of a part that a script sets, over every layer.
struct PartOverride {
	std::string part;
	Channel channel;	// Not SPRITE
	float value;
};

// The pose of rig with layers, in ascending order, then overrides, with the
// character's transform as the root's parent.
void evaluatePose (const RigDefinitionRef & rig, const std::vector<LayerSamples> & layers,
	const std::vector<PartOverride> & overrides, const Transform2D & character, Pose & out);

// One clip (evaluateClip of clip; clip may be NULL, with no samples, for the
// rig's rest pose), as an override layer, and no overrides.
void evaluatePose (const RigDefinitionRef & rig, const ClipDefinitionRef & clip, const TrackSamples & samples,
	const Transform2D & character, PoseBinding & binding, Pose & out);

// The corners of a sprite of width w and height h, with its hotspot at the
// origin of transform m, mirrored (its own mirror image, about its hotspot)
// or not: top left, top right, bottom left, bottom right, as x, y pairs.
void spriteCorners (const Transform2D & m, int w, int h, int xhot, int yhot, bool mirror, float out[8]);

// The box around the sprites that a pose draws, in the space of its
// character's transform. False when it draws none.
bool poseBounds (const Pose & pose, float & x1, float & y1, float & x2, float & y2);

// Move the pivot of part (an index of rig) to pivotX, pivotY, keeping its
// sprite and the parts attached to it where they are in its rest pose. The
// pivot's move d, in the part's sprite pixels, moves its origin by M d in
// its parent's space, where M is the turn, scale and flip of the part's
// rest pose: so its x, y move by M d, and the x, y of its children, which
// are from its origin in its own space, by - d.
void movePivot (RigDefinition & rig, int part, float pivotX, float pivotY);

// Forget what evaluatePose has reported, so that it reports it again (for
// tests; see animDiagnostic in animmodel.h).
void forgetPoseReports ();

#endif
