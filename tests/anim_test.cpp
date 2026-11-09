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
	p.scale = scale;
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
	wrong[4] = 3;
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

int main () {
	testPivotDrawing ();
	testMovePivot ();
	testPivotFormat ();
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
