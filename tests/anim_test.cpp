// Tests of the animation library (source/Anim) on its own: it builds with no
// include folder but its own and no other library, so this test also proves
// that it depends on nothing of the engine, renderer, compiler or Dev Kit.

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <memory>
#include <string>
#include <vector>

#include "animbank.h"
#include "animedit.h"
#include "animformat.h"
#include "animmodel.h"
#include "animpose.h"

static int failures = 0;

#define CHECK(cond, what) do { if (! (cond)) { fprintf (stderr, "FAIL: %s (line %d)\n", what, __LINE__); failures ++; } } while (0)

static std::string why;

// A bank of sprites of one size and hotspot each, as the engine's or Dev
// Kit's would be.
class TestBank : public AnimSpriteBank {
public:
	std::vector<AnimSpriteSize> sizes;
	int animSpriteCount () const { return (int) sizes.size (); }
	AnimSpriteSize animSpriteSize (int number) const { return sizes[number]; }
};

static AnimBankRef testBank () {
	static std::shared_ptr<TestBank> b;
	if (! b) {
		b = std::make_shared<TestBank> ();
		AnimSpriteSize body = { 40, 100, 20, 100 };	// Hotspot at the feet
		AnimSpriteSize arm = { 10, 60, 5, 5 };		// Hotspot at the shoulder
		b -> sizes.push_back (body);
		b -> sizes.push_back (arm);
	}
	return b;
}

static PartPose pose (float x, float y, float rotation = 0, float scale = 1, bool flip = false) {
	PartPose p;
	p.x = x;
	p.y = y;
	p.rotation = rotation;
	p.scaleX = p.scaleY = scale;
	p.flip = flip;
	return p;
}

static bool near (float a, float b) { return fabsf (a - b) < 1e-3f; }

static Pose restPose (const RigDefinition & rig) {
	RigDefinitionRef ref = std::make_shared<RigDefinition> (rig);
	Pose out;
	PoseBinding binding;
	evaluatePose (ref, ClipDefinitionRef (), TrackSamples (), Transform2D (), binding, out);
	return out;
}

// The corners of every part's sprite, in the world, as a pose draws them.
static std::vector<float> spriteCornersOf (const RigDefinition & rig) {
	Pose p = restPose (rig);
	std::vector<float> all;
	for (size_t i = 0; i < p.parts.size (); i ++) {
		const PosedPart & part = p.parts[i];
		if (part.sprite.kind != SpriteRef::SPRITE) continue;
		AnimSpriteSize s = part.sprite.bank -> animSpriteSize (part.sprite.number);
		float c[8];
		spriteCorners (part.spriteWorld, s.width, s.height, s.xhot, s.yhot, part.sprite.mirror, c);
		all.insert (all.end (), c, c + 8);
	}
	return all;
}

// A body with an arm and a hand: the arm turned, scaled and flipped.
static RigDefinition armRig () {
	RigDefinition rig;
	SpriteRef body = SpriteRef::sprite (testBank (), 3, 0, false);
	SpriteRef arm = SpriteRef::sprite (testBank (), 3, 1, false);
	CHECK (addPart (rig, "body", "", pose (100, 200), body, why), "a body");
	CHECK (addPart (rig, "arm", "body", pose (12, -80, 30, 1.5f, true), arm, why), "an arm");
	CHECK (addPart (rig, "hand", "arm", pose (0, 50, -20, 0.8f), arm, why), "a hand");
	return rig;
}

static void testPivotDrawing () {
	RigDefinition rig;
	CHECK (addPart (rig, "arm", "", pose (0, 0), SpriteRef::sprite (testBank (), 3, 1, false), why), "an arm");
	int arm = rig.findPart ("arm");

	Pose p = restPose (rig);
	const Transform2D & w = p.parts[arm].world, & s = p.parts[arm].spriteWorld;
	CHECK (w.a == s.a && w.b == s.b && w.c == s.c && w.d == s.d && w.tx == s.tx && w.ty == s.ty,
		"without a pivot, the sprite is drawn through the part's own transform");

	// The pivot 0, 50 below the shoulder: the part's origin, so where the arm
	// turns about, is at the elbow; the sprite's hotspot sits 50 above it.
	rig.parts[arm].pivotX = 0;
	rig.parts[arm].pivotY = 50;
	p = restPose (rig);
	float x, y;
	p.parts[arm].spriteWorld.apply (0, 0, x, y);
	CHECK (near (x, 0) && near (y, -50), "the sprite's hotspot sits at minus the pivot");

	// Turned 90 degrees clockwise about the elbow, the shoulder (the hotspot,
	// 50 above the elbow) goes to the right of it.
	rig.parts[arm].rest.rotation = 90;
	p = restPose (rig);
	p.parts[arm].spriteWorld.apply (0, 0, x, y);
	CHECK (near (x, 50) && near (y, 0), "a part turns about its pivot");
	p.parts[arm].world.apply (0, 0, x, y);
	CHECK (near (x, 0) && near (y, 0), "the origin stays where x, y put it");

	float x1, y1, x2, y2;
	CHECK (poseBounds (p, x1, y1, x2, y2), "a pose with a sprite has bounds");
	// The arm, 10 wide and 60 high from its hotspot at 5, 5, turned about the
	// elbow: from -5 to 55 across, and -5 to 5 down.
	CHECK (near (x1, -5) && near (x2, 55) && near (y1, -5) && near (y2, 5), "bounds follow the pivot");
}

static void testMovePivot () {
	RigDefinition rig = armRig ();
	int arm = rig.findPart ("arm");
	std::vector<float> before = spriteCornersOf (rig);

	movePivot (rig, arm, 3, 40);
	CHECK (rig.parts[arm].pivotX == 3 && rig.parts[arm].pivotY == 40, "the pivot moves");
	std::vector<float> after = spriteCornersOf (rig);
	bool same = before.size () == after.size ();
	for (size_t i = 0; same && i < before.size (); i ++) same = near (before[i], after[i]);
	CHECK (same, "moving the pivot of a turned, scaled and flipped part leaves every sprite where it was");

	// The origin moved, in the parent's space, by the part's turn, scale and
	// flip of the pivot's move.
	Pose p = restPose (rig);
	float ox, oy;
	p.parts[arm].world.apply (0, 0, ox, oy);
	float hx, hy;
	p.parts[arm].spriteWorld.apply (3, 40, hx, hy);
	CHECK (near (ox, hx) && near (oy, hy), "the origin is the sprite's hotspot moved by the pivot");

	movePivot (rig, arm, 0, 0);
	after = spriteCornersOf (rig);
	same = before.size () == after.size ();
	for (size_t i = 0; same && i < before.size (); i ++) same = near (before[i], after[i]);
	CHECK (same, "and moving it back too");
	CHECK (near (rig.parts[arm].rest.x, 12) && near (rig.parts[arm].rest.y, -80), "back where it was");
}

static void testPivotFormat () {
	RigDefinition rig = armRig ();
	animformat::Document doc;
	doc.rigs.push_back (rig);
	animformat::Bytes v1;
	CHECK (animformat::encode (doc, v1, why), "a rig without pivots encodes");
	CHECK (v1.size () > 6 && v1[4] == 1 && v1[5] == 0, "without pivots, version 1, as before pivots");

	doc.rigs[0].parts[2].pivotX = 2.5f;
	doc.rigs[0].parts[2].pivotY = -7;
	animformat::Bytes v2;
	CHECK (animformat::encode (doc, v2, why), "a rig with a pivot encodes");
	CHECK (v2.size () > 6 && v2[4] == 2 && v2[5] == 0, "with a pivot, version 2");
	CHECK (v2.size () == v1.size () + 4 * 8, "8 bytes more a part, the root's too");

	animformat::Document back;
	CHECK (animformat::decode (v2.data (), v2.size (), back, why), "version 2 decodes");
	CHECK (back.rigs.size () == 1 && back.rigs[0].parts[2].pivotX == 2.5f && back.rigs[0].parts[2].pivotY == -7
		&& back.rigs[0].parts[1].pivotX == 0, "the pivots come back");
	CHECK (animformat::decode (v1.data (), v1.size (), back, why), "version 1 decodes");
	CHECK (back.rigs[0].parts[2].pivotX == 0 && back.rigs[0].parts[2].pivotY == 0, "with pivots of 0");

	// Cut right after its magic and version, a file is whole: it is empty.
	for (size_t n = 0; n < v2.size (); n ++) {
		if (n != 6 && animformat::decode (v2.data (), n, back, why)) {
			CHECK (false, "a cut file of version 2 decodes");
			break;
		}
	}
	animformat::Bytes wrong = v2;
	wrong[4] = 4;
	CHECK (! animformat::decode (wrong.data (), wrong.size (), back, why), "a later version doesn't decode");
	wrong[4] = 0;
	CHECK (! animformat::decode (wrong.data (), wrong.size (), back, why), "nor version 0");

	doc.rigs[0].parts[2].pivotX = NAN;
	CHECK (! animformat::encode (doc, v2, why), "a pivot that isn't a number doesn't encode");
}

static animformat::Asset rigAsset () {
	animformat::Asset a;
	a.kind = animformat::Asset::RIG;
	a.id = animformat::newAssetId ();
	a.banks.push_back ("hd-ego/legs_south.duc");
	a.banks.push_back ("hd-ego/arm1_south.duc");
	a.rig = armRig ();
	// In an asset, a sprite's bank is its place in the list of banks.
	a.rig.parts[1].sprite.bankFile = 0;
	a.rig.parts[2].sprite.bankFile = 1;
	a.rig.parts[3].sprite.bankFile = 1;
	a.rig.parts[2].pivotY = 12;
	return a;
}

static animformat::Asset clipAsset () {
	animformat::Asset a;
	a.kind = animformat::Asset::CLIP;
	a.id = animformat::newAssetId ();
	a.banks.push_back ("hd-ego/head_south.duc");
	a.clip.length = 48;
	a.clip.loop = true;
	Key k;
	k.time = 0;
	k.value = 10;
	k.easing = EASING_INOUT;
	CHECK (setKey (a.clip, "arm", CHANNEL_ROTATION, k, why), "a key");
	k.time = 24;
	k.value = -10;
	CHECK (setKey (a.clip, "arm", CHANNEL_ROTATION, k, why), "another");
	Key s;
	s.time = 40;
	s.sprite = SpriteRef::sprite (AnimBankRef (), 0, 7, false);
	CHECK (setKey (a.clip, "eyes", CHANNEL_SPRITE, s, why), "a sprite key");
	FrameEvent sound, function;
	sound.kind = FrameEvent::SOUND;
	function.kind = FrameEvent::FUNCTION;
	CHECK (addEvent (a.clip, 8, sound, why) && addEvent (a.clip, 32, function, why), "two events");
	a.eventNames.push_back ("sounds/step.wav");
	a.eventNames.push_back ("footstep");
	a.rigId = animformat::newAssetId ();
	a.rigPath = "ego.rig";
	return a;
}

static bool sameAsset (const animformat::Asset & a, const animformat::Asset & b) {
	animformat::Bytes x, y;
	std::string w;
	return animformat::encodeAsset (a, x, w) && animformat::encodeAsset (b, y, w) && x == y;
}

static void testAssetIds () {
	animformat::AssetId a = animformat::newAssetId (), b = animformat::newAssetId ();
	CHECK (! a.isNull () && ! b.isNull () && a != b, "new identities are random");
	CHECK (a.text ().size () == 32, "an identity's text is 32 digits");
	CHECK (animformat::AssetId ().isNull (), "a default identity is null");
}

static void testAssetRoundTrip () {
	animformat::Asset rig = rigAsset (), clip = clipAsset (), back;
	animformat::Bytes bytes;
	CHECK (animformat::encodeAsset (rig, bytes, why), "a rig asset encodes");
	CHECK (bytes[4] == 2, "of version 2, with its pivot");
	CHECK (animformat::decodeAsset (bytes.data (), bytes.size (), back, why), "and decodes");
	CHECK (back.kind == animformat::Asset::RIG && back.id == rig.id && back.banks == rig.banks, "with its identity and banks");
	CHECK (back.rig.parts.size () == 4 && back.rig.parts[2].pivotY == 12 && back.rig.parts[3].sprite.bankFile == 1
		&& back.rig.parts[2].rest.rotation == 30 && back.rig.parts[2].rest.flip, "and its parts");
	CHECK (sameAsset (rig, back), "the same to the byte");

	// The plain decoder, as the engine's, skips what is of assets only.
	animformat::Document doc;
	CHECK (animformat::decode (bytes.data (), bytes.size (), doc, why) && doc.rigs.size () == 1 && doc.clips.empty (),
		"a decoder of games skips the chunks of assets");

	CHECK (animformat::encodeAsset (clip, bytes, why), "a clip asset encodes");
	CHECK (bytes[4] == 1, "of version 1");
	CHECK (animformat::decodeAsset (bytes.data (), bytes.size (), back, why), "and decodes");
	CHECK (back.kind == animformat::Asset::CLIP && back.id == clip.id && back.rigId == clip.rigId && back.rigPath == "ego.rig",
		"with its binding");
	CHECK (back.eventNames.size () == 2 && back.eventNames[0] == "sounds/step.wav" && back.eventNames[1] == "footstep",
		"its events' sounds and functions");
	CHECK (back.clip.tracks.size () == 2 && back.clip.length == 48 && back.clip.loop, "and its tracks");
	CHECK (sameAsset (clip, back), "the same to the byte");

	clip.rigId = animformat::AssetId ();
	clip.rigPath.clear ();
	CHECK (animformat::encodeAsset (clip, bytes, why) && animformat::decodeAsset (bytes.data (), bytes.size (), back, why)
		&& back.rigId.isNull () && back.rigPath.empty (), "a clip needn't be bound to a rig");
}

static void testWholeAssets () {
	animformat::Bytes bytes;
	animformat::Asset a = rigAsset ();
	a.id = animformat::AssetId ();
	CHECK (! animformat::encodeAsset (a, bytes, why), "an asset without an identity");
	a = rigAsset ();
	a.rig.parts[3].sprite.bankFile = 2;
	CHECK (! animformat::encodeAsset (a, bytes, why), "a sprite of a bank the asset doesn't list");
	a = rigAsset ();
	a.eventNames.push_back ("footstep");
	CHECK (! animformat::encodeAsset (a, bytes, why), "a rig with events");
	a = rigAsset ();
	a.rigId = animformat::newAssetId ();
	CHECK (! animformat::encodeAsset (a, bytes, why), "a rig bound to a rig");
	a = rigAsset ();
	a.banks[0].clear ();
	CHECK (! animformat::encodeAsset (a, bytes, why), "a bank without a path");

	a = clipAsset ();
	a.eventNames.pop_back ();
	CHECK (! animformat::encodeAsset (a, bytes, why), "an event without a sound or function");
	a = clipAsset ();
	a.eventNames[1].clear ();
	CHECK (! animformat::encodeAsset (a, bytes, why), "an event with an empty name");
	a = clipAsset ();
	a.rigId = animformat::AssetId ();
	CHECK (! animformat::encodeAsset (a, bytes, why), "the path of a rig without the rig");
	a = clipAsset ();
	a.clip.tracks[1].keys[0].sprite.bankFile = 1;
	CHECK (! animformat::encodeAsset (a, bytes, why), "a sprite key of a bank the asset doesn't list");
	a = clipAsset ();
	a.clip.stepping = STEPPING_LEGACY;
	CHECK (! animformat::encodeAsset (a, bytes, why), "an animation of anim ()");

	// A file of two rigs, as saved games have, isn't an asset.
	animformat::Document doc;
	doc.rigs.push_back (armRig ());
	doc.rigs.push_back (armRig ());
	animformat::Asset back;
	CHECK (animformat::encode (doc, bytes, why) && ! animformat::decodeAsset (bytes.data (), bytes.size (), back, why),
		"a file of rigs isn't an asset");
}

// Every cut and every changed byte of an asset either fails, or decodes to a
// whole asset (one that encodes again); none crashes, under ASan.
static void testDamagedAssets () {
	animformat::Asset assets[2] = { rigAsset (), clipAsset () };
	int decoded = 0, tried = 0;
	for (int i = 0; i < 2; i ++) {
		animformat::Bytes bytes, again;
		CHECK (animformat::encodeAsset (assets[i], bytes, why), "an asset to damage");
		animformat::Asset back;
		for (size_t n = 0; n < bytes.size (); n ++) {
			tried ++;
			if (animformat::decodeAsset (bytes.data (), n, back, why)) {
				decoded ++;
				CHECK (animformat::encodeAsset (back, again, why), "a cut asset that decodes is whole");
			}
		}
		for (size_t at = 0; at < bytes.size (); at ++) {
			const int changes[3] = { 1, 0x80, 0xff };
			for (int c = 0; c < 3; c ++) {
				animformat::Bytes damaged = bytes;
				damaged[at] ^= changes[c];
				tried ++;
				if (animformat::decodeAsset (damaged.data (), damaged.size (), back, why)) {
					decoded ++;
					CHECK (animformat::encodeAsset (back, again, why), "a damaged asset that decodes is whole");
				}
			}
		}
	}
	printf ("%d of %d damaged assets still decoded (to whole assets)\n", decoded, tried);
}

static int part (const RigDefinition & rig, const char * name) { return rig.findPart (name); }

static std::vector<std::string> namesInOrder (const RigDefinition & rig) {
	std::vector<std::string> names;
	for (size_t i = 0; i < rig.parts.size (); i ++) names.push_back (rig.parts[i].name);
	return names;
}

static void testPoseOfTransform () {
	const float angles[] = { 0, 30, -75, 90, 179, -179 };
	const float scales[] = { 1, 0.5f, 2.25f };
	for (int a = 0; a < 6; a ++) {
		for (int s = 0; s < 3; s ++) {
			for (int f = 0; f < 2; f ++) {
				PartPose p = pose (12, -7, angles[a], scales[s], f == 1), back;
				CHECK (poseOfTransform (localTransform (p), back), "a pose of a transform");
				CHECK (near (back.x, 12) && near (back.y, -7) && near (back.rotation, angles[a]) && near (back.scaleX, scales[s]) && near (back.scaleY, scales[s])
					&& back.flip == (f == 1), "is the pose it was made of");
			}
		}
	}
	PartPose out;
	CHECK (! poseOfTransform (Transform2D::scaling (0), out), "a transform scaled to nothing has no pose");
}

static void testRenameAndRemove () {
	RigDefinition rig = armRig ();
	CHECK (renamePart (rig, part (rig, "arm"), "leftArm", why) && part (rig, "leftArm") == 2 && part (rig, "arm") < 0, "a part renames");
	CHECK (! renamePart (rig, part (rig, "hand"), "body", why), "not to another part's name");
	CHECK (! renamePart (rig, part (rig, "hand"), "", why), "nor to nothing");
	CHECK (! renamePart (rig, 0, "root", why), "and the root has no name");
	PartId hand = rig.parts[part (rig, "hand")].id;

	CHECK (removePart (rig, part (rig, "leftArm"), why), "a part removes");
	CHECK (rig.parts.size () == 2 && part (rig, "hand") < 0 && partWithId (rig, hand) < 0, "with what is attached to it");
	CHECK (validRig (rig, why), "leaving a whole rig");
	CHECK (! removePart (rig, 0, why), "but not the root");
}

static void testPlacePart () {
	RigDefinition rig = armRig ();
	CHECK (addPart (rig, "head", "body", pose (0, -100), SpriteRef (), why), "a head");
	CHECK (addPart (rig, "tail", "", pose (0, 0), SpriteRef (), why), "a tail on the root");
	// Out of order: an eye under the head, added after the tail.
	CHECK (addPart (rig, "eye", "head", pose (5, -10, 15, 1.25f), SpriteRef (), why), "an eye");
	std::vector<std::string> before = namesInOrder (rig);
	std::vector<int> bodyChildren = partChildren (rig, part (rig, "body"));
	CHECK (bodyChildren.size () == 2 && bodyChildren[0] == part (rig, "arm") && bodyChildren[1] == part (rig, "head"), "the body's parts in order");

	// The head first among the body's parts: drawn before the arm, at the same depth.
	CHECK (placePart (rig, part (rig, "head"), part (rig, "body"), 0, why), "the head goes first");
	bodyChildren = partChildren (rig, part (rig, "body"));
	CHECK (bodyChildren.size () == 2 && bodyChildren[0] == part (rig, "head") && bodyChildren[1] == part (rig, "arm"), "before the arm");
	CHECK (validRig (rig, why) && partIsUnder (rig, part (rig, "eye"), part (rig, "head")), "with its eye, in a whole rig");
	CHECK (placePart (rig, part (rig, "head"), part (rig, "body"), 5, why), "and back last");
	bodyChildren = partChildren (rig, part (rig, "body"));
	CHECK (bodyChildren[1] == part (rig, "head"), "after the arm");

	// The eye onto the turned, scaled and flipped arm, staying where it is.
	Transform2D eyeBefore = restWorld (rig, part (rig, "eye"));
	CHECK (placePart (rig, part (rig, "eye"), part (rig, "arm"), 0, why), "the eye goes onto the arm");
	CHECK (rig.parts[part (rig, "eye")].parent == part (rig, "arm"), "under it");
	Transform2D eyeAfter = restWorld (rig, part (rig, "eye"));
	CHECK (near (eyeBefore.a, eyeAfter.a) && near (eyeBefore.b, eyeAfter.b) && near (eyeBefore.c, eyeAfter.c)
		&& near (eyeBefore.d, eyeAfter.d) && near (eyeBefore.tx, eyeAfter.tx) && near (eyeBefore.ty, eyeAfter.ty),
		"where it was in the world");
	CHECK (validRig (rig, why), "a whole rig");

	CHECK (! placePart (rig, part (rig, "arm"), part (rig, "eye"), 0, why), "a part can't go under what is attached to it");
	CHECK (! placePart (rig, part (rig, "arm"), part (rig, "arm"), 0, why), "nor under itself");
	CHECK (! placePart (rig, 0, part (rig, "arm"), 0, why), "and the root stays");
	CHECK (rig.parts.size () == before.size (), "nothing is lost");
}

// Separate X and Y scales: along the part's own axes, inherited (so a
// turned part under a stretched one skews), and in files from version 3.
static void testSeparateScales () {
	// An arm, 10 wide and 60 high from its hotspot at 5, 5, twice as wide and
	// half as high: from -10 to 10 across, and -2.5 to 27.5 down.
	RigDefinition rig;
	SpriteRef arm = SpriteRef::sprite (testBank (), 3, 1, false);
	PartPose stretched = pose (0, 0);
	stretched.scaleX = 2;
	stretched.scaleY = 0.5f;
	CHECK (! stretched.uniform () && pose (0, 0, 0, 3).uniform (), "uniform () tells them apart");
	CHECK (addPart (rig, "arm", "", stretched, arm, why), "a stretched arm");
	float x1, y1, x2, y2;
	CHECK (poseBounds (restPose (rig), x1, y1, x2, y2), "has bounds");
	CHECK (near (x1, -10) && near (x2, 10) && near (y1, -2.5f) && near (y2, 27.5f), "stretched along its own axes");

	// Turned a quarter, its own X axis is down the screen.
	rig.parts[1].rest.rotation = 90;
	CHECK (poseBounds (restPose (rig), x1, y1, x2, y2), "turned, has bounds");
	CHECK (near (x1, -27.5f) && near (x2, 2.5f) && near (y1, -10) && near (y2, 10), "its stretch turns with it");

	// A hand under it, turned 45 degrees in its space: skewed by the arm's
	// stretch, as the arm's own pixels are.
	CHECK (addPart (rig, "hand", "arm", pose (0, 40, 45), arm, why), "a hand");
	Pose p = restPose (rig);
	const Transform2D & h = p.parts[2].world;
	float ax = h.a, ay = h.b, bx = h.c, by = h.d;
	CHECK (fabsf (ax * bx + ay * by) > 0.1f, "a turned part under a stretched one is skewed");

	// Moving the pivot of a stretched, turned and flipped part leaves every
	// sprite where it was.
	RigDefinition armed;
	CHECK (addPart (armed, "body", "", pose (100, 200), SpriteRef::sprite (testBank (), 3, 0, false), why), "a body");
	PartPose odd = pose (12, -80, 30, 1, true);
	odd.scaleX = 1.75f;
	odd.scaleY = 0.6f;
	CHECK (addPart (armed, "arm", "body", odd, arm, why), "an odd arm");
	CHECK (addPart (armed, "hand", "arm", pose (0, 50, -20, 0.8f), arm, why), "a hand");
	std::vector<float> before = spriteCornersOf (armed);
	movePivot (armed, armed.findPart ("arm"), 3, 40);
	std::vector<float> after = spriteCornersOf (armed);
	bool same = before.size () == after.size ();
	for (size_t i = 0; same && i < before.size (); i ++) same = near (before[i], after[i]);
	CHECK (same, "moving the pivot of a part of different scales leaves every sprite where it was");

	// A pose of a transform of different scales, without a skew.
	const float angles[] = { 0, 30, -75, 179 };
	for (int a = 0; a < 4; a ++) {
		for (int f = 0; f < 2; f ++) {
			PartPose q = pose (3, 4, angles[a], 1, f == 1), back;
			q.scaleX = 0.4f;
			q.scaleY = 2.5f;
			CHECK (poseOfTransform (localTransform (q), back), "a pose of a transform of two scales");
			CHECK (near (back.rotation, angles[a]) && near (back.scaleX, 0.4f) && near (back.scaleY, 2.5f) && back.flip == (f == 1),
				"is the pose it was made of");
		}
	}
	PartPose whole;
	CHECK (poseOfTransform (Transform2D::rotation (40) * Transform2D::scaling (1.5f), whole) && whole.uniform (), "a uniform scale stays uniform");

	// SCALE sets both; SCALE_X and SCALE_Y one each, over SCALE whatever the
	// order of the tracks; additive layers multiply.
	std::shared_ptr<RigDefinition> r = std::make_shared<RigDefinition> ();
	addPart (* r, "body", "", pose (0, 0, 0, 2), arm, why);
	for (int order = 0; order < 2; order ++) {
		for (int blend = 0; blend < NUM_LAYER_BLENDS; blend ++) {
			std::shared_ptr<ClipDefinition> c = std::make_shared<ClipDefinition> ();
			c -> length = 10;
			Key k;
			k.easing = EASING_LINEAR;
			k.value = 1.5f;
			if (order == 0) setKey (* c, "body", CHANNEL_SCALE, k, why);
			k.value = 3;
			setKey (* c, "body", CHANNEL_SCALE_X, k, why);
			k.value = 1.5f;
			if (order == 1) setKey (* c, "body", CHANNEL_SCALE, k, why);
			TrackSamples samples;
			evaluateClip (* c, 0, samples);
			PoseBinding b;
			std::vector<LayerSamples> layers;
			LayerSamples l = { c, & samples, (LayerBlend) blend, & b };
			layers.push_back (l);
			Pose out;
			evaluatePose (r, layers, std::vector<PartOverride> (), Transform2D (), out);
			bool add = blend == LAYER_BLEND_ADD;
			CHECK (near (out.parts[1].local.scaleY, add ? 3 : 1.5f), "SCALE scales Y");
			CHECK (near (out.parts[1].local.scaleX, add ? 9 : 3), "SCALE_X goes over SCALE, in either order");
		}
	}
	std::vector<PartOverride> overrides;
	PartOverride oy = { "body", CHANNEL_SCALE_Y, 0.5f }, both = { "body", CHANNEL_SCALE, 4 };
	overrides.push_back (oy);
	overrides.push_back (both);
	Pose out;
	evaluatePose (r, std::vector<LayerSamples> (), overrides, Transform2D (), out);
	CHECK (out.parts[1].local.scaleX == 4 && out.parts[1].local.scaleY == 0.5f, "overrides of SCALE_Y go over SCALE too");
	CHECK (! isDiscrete (CHANNEL_SCALE_X) && ! isDiscrete (CHANNEL_SCALE_Y) && isScale (CHANNEL_SCALE_Y) && ! isScale (CHANNEL_X),
		"the new scales are continuous");

	// Files: version 3 for different scales, as before otherwise.
	animformat::Document doc;
	doc.rigs.push_back (armRig ());
	animformat::Bytes before1, v3;
	CHECK (animformat::encode (doc, before1, why) && before1[4] == 1, "uniform scales: still version 1");
	doc.rigs[0].parts[2].rest.scaleY = 0.25f;
	CHECK (animformat::encode (doc, v3, why) && v3[4] == 3 && v3[5] == 0, "different scales: version 3");
	CHECK (v3.size () == before1.size () + 4 * 4 + 4 * 8, "4 bytes more a part for Y, and the pivots of version 2");
	animformat::Document back;
	CHECK (animformat::decode (v3.data (), v3.size (), back, why) && back.rigs.size () == 1, "version 3 decodes");
	CHECK (back.rigs[0].parts[2].rest.scaleX == 1.5f && back.rigs[0].parts[2].rest.scaleY == 0.25f
		&& back.rigs[0].parts[3].rest.scaleY == 0.8f, "with both scales");
	CHECK (animformat::decode (before1.data (), before1.size (), back, why) && back.rigs[0].parts[2].rest.scaleY == 1.5f,
		"before version 3, Y is X");
	for (size_t n = 7; n < v3.size (); n ++) {
		if (animformat::decode (v3.data (), n, back, why)) {
			CHECK (false, "a cut file of version 3 decodes");
			break;
		}
	}

	ClipDefinition clip;
	clip.length = 10;
	Key k;
	k.value = 2;
	k.easing = EASING_LINEAR;
	setKey (clip, "arm", CHANNEL_SCALE, k, why);
	animformat::Document clips;
	clips.clips.push_back (clip);
	animformat::Bytes c1, c3;
	CHECK (animformat::encode (clips, c1, why) && c1[4] == 1, "a clip of SCALE: version 1");
	setKey (clips.clips[0], "arm", CHANNEL_SCALE_Y, k, why);
	CHECK (animformat::encode (clips, c3, why) && c3[4] == 3, "a clip of SCALE_Y: version 3");
	CHECK (animformat::decode (c3.data (), c3.size (), back, why) && back.clips.size () == 1
		&& back.clips[0].track ("arm", CHANNEL_SCALE_Y), "and its track comes back");
	c3[4] = 2;
	CHECK (! animformat::decode (c3.data (), c3.size (), back, why), "a file before version 3 has no SCALE_Y");
}

int main () {
	testPoseOfTransform ();
	testRenameAndRemove ();
	testPlacePart ();
	testPivotDrawing ();
	testMovePivot ();
	testPivotFormat ();
	testSeparateScales ();
	testAssetIds ();
	testAssetRoundTrip ();
	testWholeAssets ();
	testDamagedAssets ();
	if (failures) {
		fprintf (stderr, "%d animation library test(s) failed\n", failures);
		return 1;
	}
	printf ("All animation library tests passed\n");
	return 0;
}
