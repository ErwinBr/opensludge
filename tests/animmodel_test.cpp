/*
 * Tests of the animation model (source/Engine/animmodel.cpp) and its file
 * format (animformat.cpp): legacy animations as LEGACY clips, building rigs
 * and clips, their rules, and files that round-trip, while every cut-off or
 * damaged file fails cleanly.
 */

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "animformat.h"
#include "animmodel.h"

static int failures = 0;

#define CHECK(cond, what) do { if (! (cond)) { fprintf (stderr, "FAIL: %s (line %d)\n", what, __LINE__); failures ++; } } while (0)

static std::string why;

static AnimationFrame frame (int sprite, int ticks, int noise = 0) {
	AnimationFrame f;
	f.sprite = sprite;
	f.ticks = ticks;
	f.event = FrameEvent::fromNoise (noise);
	return f;
}

static PartPose at (float x, float y) {
	PartPose p;
	p.x = x;
	p.y = y;
	return p;
}

static SpriteRef sprite (int bankFile, int number, bool mirror = false) {
	return SpriteRef::sprite (SpriteBankRef (), bankFile, number, mirror);
}

static Key key (int time, float value, Easing easing = EASING_LINEAR) {
	Key k;
	k.time = time;
	k.value = value;
	k.easing = easing;
	return k;
}

static Key spriteKey (int time, const SpriteRef & s) {
	Key k;
	k.time = time;
	k.sprite = s;
	return k;
}

// The rig of the plan: torso, arms, head, eyes.
static RigDefinition heroRig () {
	RigDefinition rig;
	addPart (rig, "torso", "", at (0, 0), sprite (3, 0), why);
	addPart (rig, "leftArm", "torso", at (-18, -52), sprite (3, 1), why);
	addPart (rig, "head", "torso", at (0, -70), sprite (3, 2), why);
	addPart (rig, "eyes", "head", at (0, -14), sprite (3, 3), why);
	addPart (rig, "rightArm", "torso", at (18, -52), sprite (3, 4), why);
	rig.parts[rig.findPart ("leftArm")].depth = -1;
	rig.parts[rig.findPart ("head")].rest.rotation = 5.5f;
	rig.parts[rig.findPart ("eyes")].rest.flip = true;
	rig.parts[rig.findPart ("rightArm")].rest.visible = false;
	rig.parts[rig.findPart ("rightArm")].rest.scale = 0.75f;
	return rig;
}

static ClipDefinition walkClip () {
	ClipDefinition c;
	c.length = 16;
	c.loop = true;
	setKey (c, "leftArm", CHANNEL_ROTATION, key (0, 20), why);
	setKey (c, "leftArm", CHANNEL_ROTATION, key (8, -20, EASING_INOUT), why);
	setKey (c, "leftArm", CHANNEL_ROTATION, key (16, 20), why);
	setKey (c, "head", CHANNEL_Y, key (4, -2), why);
	setKey (c, "torso", CHANNEL_SPRITE, spriteKey (0, sprite (7, 0)), why);
	setKey (c, "torso", CHANNEL_SPRITE, spriteKey (4, sprite (7, 1, true)), why);
	setKey (c, "torso", CHANNEL_SPRITE, spriteKey (8, SpriteRef::none ()), why);
	setKey (c, "torso", CHANNEL_SPRITE, spriteKey (12, SpriteRef ()), why);
	setKey (c, "eyes", CHANNEL_VISIBLE, key (0, 1, EASING_STEP), why);
	setKey (c, "", CHANNEL_FLIP, key (0, 0, EASING_STEP), why);
	addEvent (c, 8, FrameEvent::fromNoise (12), why);
	addEvent (c, 0, FrameEvent::fromNoise (-3), why);
	return c;
}

static void testLegacyClips () {
	std::vector<AnimationFrame> frames;
	frames.push_back (frame (2, 5));
	frames.push_back (frame (-4, 3, 9));	// Mirrored, with a sound
	frames.push_back (frame (1, 0, -6));	// Holds, with a function
	frames.push_back (frame (0, 7));
	ClipDefinitionRef c = makeLegacyClip (SpriteBankRef (), frames);

	CHECK (c -> stepping == STEPPING_LEGACY && c -> loop, "anim () makes a looping LEGACY clip");
	CHECK (c -> frames.size () == 4, "it keeps the frames");
	for (size_t i = 0; i < frames.size (); i ++) {
		CHECK (c -> frames[i].sprite == frames[i].sprite && c -> frames[i].ticks == frames[i].ticks
			&& c -> frames[i].event == frames[i].event, "exactly as given, for the legacy stepper");
	}
	CHECK (c -> tracks.size () == 1, "one track");
	const Track * t = c -> track ("", CHANNEL_SPRITE);
	CHECK (t && t -> keys.size () == 4, "a sprite track on the root, a key per frame");
	if (t && t -> keys.size () == 4) {
		CHECK (t -> keys[0].time == 0 && t -> keys[1].time == 5 && t -> keys[2].time == 8 && t -> keys[3].time == 8,
			"keys where frames start; a frame of 0 ticks takes none");
		CHECK (t -> keys[1].sprite.kind == SpriteRef::SPRITE && t -> keys[1].sprite.number == 4 && t -> keys[1].sprite.mirror,
			"a negative sprite is its mirror image");
		CHECK (! t -> keys[0].sprite.mirror && t -> keys[0].sprite.number == 2, "a positive one isn't");
		CHECK (t -> keys[0].sprite.bankFile == -1, "no bank, no file");
	}
	CHECK (c -> length == 15, "as long as the frames that count");
	CHECK (c -> events.size () == 2, "an event per frame with a noise");
	if (c -> events.size () == 2) {
		CHECK (c -> events[0].frame == 1 && c -> events[0].time == 5 && c -> events[0].what.kind == FrameEvent::SOUND
			&& c -> events[0].what.number == 9, "the sound of frame 1");
		CHECK (c -> events[1].frame == 2 && c -> events[1].what.kind == FrameEvent::FUNCTION
			&& c -> events[1].what.number == 6, "the function of frame 2");
		CHECK (c -> events[0].id != c -> events[1].id, "events have their own IDs");
	}
	CHECK (validClip (* c, why), "a legacy clip is valid");

	// Extremes don't overflow.
	std::vector<AnimationFrame> wild;
	wild.push_back (frame (INT_MIN, INT_MAX));
	wild.push_back (frame (5, INT_MAX));
	wild.push_back (frame (6, -9));
	ClipDefinitionRef w = makeLegacyClip (SpriteBankRef (), wild);
	CHECK (w -> length == INT_MAX, "lengths stop at the largest int");
	CHECK (w -> tracks[0].keys[0].sprite.number == INT_MAX && w -> tracks[0].keys[0].sprite.mirror, "the most negative sprite");
	CHECK (validClip (* w, why), "still valid");

	ClipDefinitionRef empty = makeLegacyClip (SpriteBankRef (), std::vector<AnimationFrame> ());
	CHECK (empty -> length == 0 && empty -> tracks[0].keys.empty () && validClip (* empty, why), "anim () of no frames");

	ClipDefinition legacy = * c;
	CHECK (! setKey (legacy, "", CHANNEL_X, key (0, 1), why), "no keys for anim ()'s clips");
	CHECK (! addEvent (legacy, 0, FrameEvent::fromNoise (1), why), "no events either");
}

static void testRigs () {
	RigDefinition rig;
	CHECK (rig.parts.size () == 1 && rig.parts[0].name.empty () && rig.parts[0].parent == -1, "a rig starts with its root");
	CHECK (validRig (rig, why), "which is valid");
	CHECK (addPart (rig, "torso", "", at (0, 0), sprite (1, 0), why), "a part on the root");
	CHECK (addPart (rig, "head", "torso", at (0, -70), sprite (1, 2), why), "a part on a part");
	CHECK (addPart (rig, "hip", "torso", at (0, 10), SpriteRef (), why), "a joint, without a sprite");
	CHECK (! addPart (rig, "head", "torso", at (0, 0), sprite (1, 2), why), "names are unique");
	CHECK (! addPart (rig, "", "torso", at (0, 0), sprite (1, 2), why), "a part has a name");
	CHECK (! addPart (rig, "hat", "neck", at (0, 0), sprite (1, 2), why), "on a part that exists");
	CHECK (! addPart (rig, "hat", "head", at (NAN, 0), sprite (1, 2), why), "at a place that is a number");
	CHECK (! addPart (rig, "hat", "head", at (0, 0), SpriteRef::none (), why), "a part's sprite is one, or none (DEFAULT)");
	CHECK (! addPart (rig, std::string ("h\0t", 3), "head", at (0, 0), sprite (1, 2), why), "no 0 byte in a name");
	CHECK (rig.parts.size () == 4, "failures add nothing");
	CHECK (rig.findPart ("head") == 2 && rig.parts[2].parent == 1, "parts find their parents");
	CHECK (rig.parts[1].id != rig.parts[2].id && rig.parts[2].id < rig.nextPartId, "IDs are unique");
	CHECK (rig.part ("nobody") == NULL, "no such part");
	CHECK (validRig (rig, why), "valid");

	RigDefinition bad = rig;
	bad.parts[1].parent = 2;
	CHECK (! validRig (bad, why), "a parent comes first");
	bad = rig;
	bad.parts[2].name = "torso";
	CHECK (! validRig (bad, why), "names are unique");
	bad = rig;
	bad.parts[2].id = bad.parts[1].id;
	CHECK (! validRig (bad, why), "IDs are unique");
	bad = rig;
	bad.parts[2].id = bad.nextPartId;
	CHECK (! validRig (bad, why), "IDs are below the next");
	bad = rig;
	bad.parts[0].name = "root";
	CHECK (! validRig (bad, why), "the root has no name");
	bad = rig;
	bad.parts[2].rest.scale = INFINITY;
	CHECK (! validRig (bad, why), "numbers are finite");
}

static void testClips () {
	ClipDefinition c;
	CHECK (c.stepping == STEPPING_TIMELINE && c.length == 0 && validClip (c, why), "an empty timeline");
	c.length = 10;
	CHECK (setKey (c, "head", CHANNEL_X, key (5, 1), why), "a key");
	CHECK (setKey (c, "head", CHANNEL_X, key (0, 2), why), "a key before it");
	CHECK (setKey (c, "head", CHANNEL_X, key (10, 3), why), "a key at the end");
	CHECK (setKey (c, "head", CHANNEL_X, key (5, 4), why), "a key at the same time");
	const Track * t = c.track ("head", CHANNEL_X);
	CHECK (c.tracks.size () == 1 && t && t -> keys.size () == 3, "one track, three keys");
	if (t && t -> keys.size () == 3) {
		CHECK (t -> keys[0].time == 0 && t -> keys[1].time == 5 && t -> keys[2].time == 10, "in time order");
		CHECK (t -> keys[1].value == 4, "the same time replaces");
	}
	TrackId first = t ? t -> id : 0;
	CHECK (setKey (c, "head", CHANNEL_Y, key (0, 0), why), "another channel");
	CHECK (setKey (c, "eyes", CHANNEL_X, key (0, 0), why), "another part");
	CHECK (c.tracks.size () == 3 && c.tracks[1].id != first && c.tracks[2].id != c.tracks[1].id, "each its own track, with its own ID");
	CHECK (! setKey (c, "head", CHANNEL_X, key (11, 0), why), "not after the end");
	CHECK (! setKey (c, "head", CHANNEL_X, key (-1, 0), why), "not before the start");
	CHECK (! setKey (c, "head", CHANNEL_DEPTH, key (0, 1, EASING_LINEAR), why), "discrete channels step");
	CHECK (! setKey (c, "head", CHANNEL_X, key (0, NAN), why), "values are numbers");
	CHECK (! setKey (c, "head", (Channel) 99, key (0, 0), why), "channels exist");
	CHECK (! setKey (c, "head", CHANNEL_X, key (0, 0, (Easing) 7), why), "easings exist");
	CHECK (setKey (c, "head", CHANNEL_SPRITE, spriteKey (0, SpriteRef::none ()), why), "an empty frame");
	CHECK (setKey (c, "head", CHANNEL_SPRITE, spriteKey (2, SpriteRef ()), why), "the part's own sprite");
	CHECK (! setKey (c, "head", CHANNEL_SPRITE, spriteKey (3, sprite (1, -1)), why), "sprites are numbered from 0");

	CHECK (addEvent (c, 0, FrameEvent::fromNoise (4), why), "an event at the start");
	CHECK (addEvent (c, 9, FrameEvent::fromNoise (-4), why), "and before the end");
	CHECK (! addEvent (c, 10, FrameEvent::fromNoise (4), why), "not at the end: time runs to before it");
	CHECK (! addEvent (c, -1, FrameEvent::fromNoise (4), why), "not before the start");
	CHECK (! addEvent (c, 1, FrameEvent (), why), "an event is a sound or a function");
	CHECK (c.events.size () == 2 && c.events[0].id != c.events[1].id && c.events[1].id < c.nextEventId, "unique IDs");
	CHECK (validClip (c, why), "valid");

	ClipDefinition still;
	CHECK (addEvent (still, 0, FrameEvent::fromNoise (1), why), "a clip of no length has events at 0");
	CHECK (! addEvent (still, 1, FrameEvent::fromNoise (1), why), "only at 0");

	ClipDefinition bad = c;
	bad.tracks[1].part = "head";
	bad.tracks[1].channel = CHANNEL_X;
	CHECK (! validClip (bad, why), "one track per part and channel");
	bad = c;
	std::swap (bad.tracks[0].keys[0], bad.tracks[0].keys[1]);
	CHECK (! validClip (bad, why), "keys in order");
	bad = c;
	bad.tracks[0].keys[1].time = bad.tracks[0].keys[0].time;
	CHECK (! validClip (bad, why), "two keys can't share a time in a timeline");
	bad = c;
	bad.events[1].time = 10;
	CHECK (! validClip (bad, why), "events before the end");
	bad = c;
	bad.events[0].frame = 0;
	CHECK (! validClip (bad, why), "timeline events are at times");
	bad = c;
	bad.events[1].id = bad.events[0].id;
	CHECK (! validClip (bad, why), "event IDs unique");
	bad = c;
	bad.length = 4;
	CHECK (! validClip (bad, why), "no key after the end");
	bad = c;
	bad.frames.push_back (frame (0, 1));
	CHECK (! validClip (bad, why), "a timeline has no frames");
}

static const SpriteBankRef & fakeBank () {
	static SpriteBankRef b = std::make_shared<loadedSpriteBank> ();
	return b;
}
static int loads = 0;
static SpriteBankRef fakeLoader (int file) {
	loads ++;
	return file == 7 ? fakeBank () : SpriteBankRef ();
}

static void testResolve () {
	ClipDefinition c = walkClip ();
	loads = 0;
	CHECK (resolveBanks (c, fakeLoader), "the clip's banks load");
	const Track * t = c.track ("torso", CHANNEL_SPRITE);
	CHECK (t && t -> keys[0].sprite.bank == fakeBank () && t -> keys[1].sprite.bank == fakeBank (), "and are filled in");
	CHECK (t && ! t -> keys[2].sprite.bank && ! t -> keys[3].sprite.bank, "NONE and DEFAULT have no bank");
	CHECK (loads == 2, "only sprites load");
	RigDefinition rig = heroRig ();
	CHECK (! resolveBanks (rig, fakeLoader), "a bank that doesn't load fails");
}

// Field by field.
static bool samePose (const PartPose & a, const PartPose & b) {
	return a.x == b.x && a.y == b.y && a.rotation == b.rotation && a.scale == b.scale && a.flip == b.flip && a.visible == b.visible;
}

static bool sameRig (const RigDefinition & a, const RigDefinition & b) {
	if (a.nextPartId != b.nextPartId || a.parts.size () != b.parts.size ()) return false;
	for (size_t i = 0; i < a.parts.size (); i ++) {
		const RigPart & p = a.parts[i], & q = b.parts[i];
		if (p.id != q.id || p.name != q.name || p.parent != q.parent || ! samePose (p.rest, q.rest)
			|| ! (p.sprite == q.sprite) || p.depth != q.depth) return false;
	}
	return true;
}

static bool sameClip (const ClipDefinition & a, const ClipDefinition & b) {
	if (a.stepping != b.stepping || a.length != b.length || a.loop != b.loop || a.nextTrackId != b.nextTrackId
		|| a.nextEventId != b.nextEventId || a.tracks.size () != b.tracks.size () || a.events.size () != b.events.size ())
		return false;
	for (size_t t = 0; t < a.tracks.size (); t ++) {
		const Track & s = a.tracks[t], & u = b.tracks[t];
		if (s.id != u.id || s.part != u.part || s.channel != u.channel || s.keys.size () != u.keys.size ()) return false;
		for (size_t k = 0; k < s.keys.size (); k ++) {
			const Key & x = s.keys[k], & y = u.keys[k];
			if (x.time != y.time || x.value != y.value || x.easing != y.easing || ! (x.sprite == y.sprite)) return false;
		}
	}
	for (size_t e = 0; e < a.events.size (); e ++) {
		const ClipEvent & x = a.events[e], & y = b.events[e];
		if (x.id != y.id || x.time != y.time || x.frame != y.frame || ! (x.what == y.what)) return false;
	}
	return true;
}

static animformat::Document document () {
	animformat::Document doc;
	doc.rigs.push_back (heroRig ());
	doc.rigs.push_back (RigDefinition ());
	doc.clips.push_back (walkClip ());
	ClipDefinition still;
	addEvent (still, 0, FrameEvent::fromNoise (2), why);
	doc.clips.push_back (still);
	return doc;
}

static bool decodeBytes (const animformat::Bytes & b, animformat::Document & doc) {
	return animformat::decode (b.data (), b.size (), doc, why);
}

// The ends of the chunks of an encoded file, where a file may stop.
static std::vector<size_t> chunkEnds (const animformat::Bytes & b) {
	std::vector<size_t> ends;
	size_t at = 6;
	ends.push_back (at);
	while (at + 8 <= b.size ()) {
		size_t size = b[at + 4] | (b[at + 5] << 8) | (b[at + 6] << 16) | ((size_t) b[at + 7] << 24);
		at += 8 + size;
		ends.push_back (at);
	}
	return ends;
}

static void testFormat () {
	animformat::Document doc = document (), back;
	animformat::Bytes bytes;
	CHECK (animformat::encode (doc, bytes, why), "a document encodes");
	CHECK (bytes.size () > 6 && ! memcmp (bytes.data (), "SLAN", 4) && bytes[4] == animformat::version && bytes[5] == 0, "magic and version");
	CHECK (decodeBytes (bytes, back), "and decodes");
	CHECK (back.rigs.size () == 2 && back.clips.size () == 2, "all of it");
	if (back.rigs.size () == 2 && back.clips.size () == 2) {
		CHECK (sameRig (doc.rigs[0], back.rigs[0]) && sameRig (doc.rigs[1], back.rigs[1]), "rigs round-trip");
		CHECK (sameClip (doc.clips[0], back.clips[0]) && sameClip (doc.clips[1], back.clips[1]), "clips round-trip");
	}
	animformat::Bytes again;
	animformat::encode (back, again, why);
	CHECK (again == bytes, "and encode to the same bytes");

	// Rotations and scales that aren't whole numbers keep every bit.
	CHECK (back.rigs.size () && back.rigs[0].parts[3].rest.rotation == 5.5f && back.rigs[0].parts[5].rest.scale == 0.75f, "floats exactly");

	// A chunk this engine doesn't know is skipped.
	animformat::Bytes extra = bytes;
	const unsigned char unknown[] = { 'N', 'E', 'W', '!', 3, 0, 0, 0, 1, 2, 3 };
	extra.insert (extra.begin () + 6, unknown, unknown + sizeof (unknown));
	CHECK (decodeBytes (extra, back) && back.rigs.size () == 2 && back.clips.size () == 2, "unknown chunks are skipped");

	// Things that don't encode.
	animformat::Document legacy;
	legacy.clips.push_back (* makeLegacyClip (SpriteBankRef (), std::vector<AnimationFrame> (1, frame (0, 1))));
	CHECK (! animformat::encode (legacy, bytes, why), "anim ()'s clips keep their legacy record");
	animformat::Document invalid = document ();
	invalid.rigs[0].parts[2].parent = 4;
	CHECK (! animformat::encode (invalid, bytes, why), "an invalid rig doesn't encode");
	animformat::encode (doc, bytes, why);

	// Wrong headers.
	animformat::Bytes b = bytes;
	b[0] = 'X';
	CHECK (! decodeBytes (b, back), "the magic");
	b = bytes;
	b[4] = 2;
	CHECK (! decodeBytes (b, back), "a version from the future");

	// Every cut: a file may stop between chunks, with fewer of them, but not
	// within one.
	std::vector<size_t> ends = chunkEnds (bytes);
	CHECK (ends.size () == 5 && ends.back () == bytes.size (), "four chunks");
	int cutFailures = 0, cutWrong = 0;
	for (size_t n = 0; n < bytes.size (); n ++) {
		bool boundary = false;
		for (size_t e = 0; e < ends.size (); e ++) boundary = boundary || ends[e] == n;
		animformat::Bytes cut (bytes.begin (), bytes.begin () + n);
		bool ok = decodeBytes (cut, back);
		if (ok != boundary) cutWrong ++;
		if (! ok) cutFailures ++;
	}
	CHECK (cutWrong == 0, "a cut within a chunk fails; between chunks it doesn't");
	CHECK (cutFailures == (int) bytes.size () - 4, "every other cut fails");

	// Every damaged byte, three ways: whatever decodes is valid.
	int invalidDecodes = 0, decoded = 0;
	const unsigned char flips[] = { 0x01, 0x80, 0xFF };
	for (size_t i = 0; i < bytes.size (); i ++) {
		for (size_t f = 0; f < sizeof (flips); f ++) {
			animformat::Bytes damaged = bytes;
			damaged[i] ^= flips[f];
			if (! decodeBytes (damaged, back)) continue;
			decoded ++;
			for (size_t r = 0; r < back.rigs.size (); r ++) if (! validRig (back.rigs[r], why)) invalidDecodes ++;
			for (size_t c = 0; c < back.clips.size (); c ++) if (! validClip (back.clips[c], why)) invalidDecodes ++;
		}
	}
	CHECK (invalidDecodes == 0, "damage never decodes to something invalid");
	printf ("%d of %d damaged files still decoded (to valid rigs and clips)\n", decoded, (int) bytes.size () * 3);

	// Enormous counts fail without allocating them.
	animformat::Bytes huge;
	const unsigned char hugeRig[] = { 'S', 'L', 'A', 'N', 1, 0, 'R', 'I', 'G', ' ', 8, 0, 0, 0, 1, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF };
	huge.assign (hugeRig, hugeRig + sizeof (hugeRig));
	CHECK (! decodeBytes (huge, back), "a count beyond the bytes");
}

static void testLifetimes () {
	int rigs = LifetimeCounter<RigDefinition>::live (), clips = LifetimeCounter<ClipDefinition>::live ();
	{
		animformat::Document doc = document ();
		RigDefinition copy = doc.rigs[0];
		ClipDefinitionRef legacy = makeLegacyClip (SpriteBankRef (), std::vector<AnimationFrame> (1, frame (0, 1)));
		CHECK (LifetimeCounter<RigDefinition>::live () == rigs + 3, "rigs are counted");
		CHECK (LifetimeCounter<ClipDefinition>::live () == clips + 3, "clips are counted");
	}
	CHECK (LifetimeCounter<RigDefinition>::live () == rigs && LifetimeCounter<ClipDefinition>::live () == clips, "and all go");
}

int main () {
	testLegacyClips ();
	testRigs ();
	testClips ();
	testResolve ();
	testFormat ();
	testLifetimes ();
	if (failures) {
		fprintf (stderr, "%d failures\n", failures);
		return 1;
	}
	printf ("All animation model tests passed\n");
	return 0;
}
