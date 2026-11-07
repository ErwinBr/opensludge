/*
 * Tests of the animation model (source/Engine/animmodel.cpp) and its file
 * format (animformat.cpp): legacy animations as LEGACY clips, building rigs
 * and clips, their rules, and files that round-trip, while every cut-off or
 * damaged file fails cleanly. Then evaluating clips: easing, the ends of
 * clips, events over any steps and across loops, and clipFrames against the
 * legacy stepper.
 */

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "animation.h"
#include "animformat.h"
#include "animpose.h"
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
	CHECK (! addEvent (c, 11, FrameEvent::fromNoise (4), why), "not after the end");
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
	bad.events[1].time = 11;
	CHECK (! validClip (bad, why), "events within the clip");
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


static bool approx (float a, float b) {
	return fabsf (a - b) < 1e-4f;
}

// The sample of a track at time, or NULL.
static const TrackSample * sampleOf (const TrackSamples & s, const std::string & part, Channel c) {
	for (size_t i = 0; i < s.size (); i ++) {
		if (s[i].track -> part == part && s[i].track -> channel == c) return & s[i];
	}
	return NULL;
}

static float valueAt (const ClipDefinition & c, const std::string & part, Channel ch, AnimationTime t) {
	TrackSamples s;
	evaluateClip (c, t, s);
	const TrackSample * x = sampleOf (s, part, ch);
	return x ? x -> value : NAN;
}

static void testEvaluation () {
	ClipDefinition c;
	c.length = 20;
	setKey (c, "a", CHANNEL_X, key (4, 10, EASING_LINEAR), why);
	setKey (c, "a", CHANNEL_X, key (8, 30, EASING_INOUT), why);
	setKey (c, "a", CHANNEL_X, key (12, 70, EASING_STEP), why);
	setKey (c, "a", CHANNEL_X, key (16, 0, EASING_LINEAR), why);
	CHECK (approx (valueAt (c, "a", CHANNEL_X, 0), 10), "before the first key, its value");
	CHECK (approx (valueAt (c, "a", CHANNEL_X, ticksToTime (4)), 10), "at a key, its value");
	CHECK (approx (valueAt (c, "a", CHANNEL_X, ticksToTime (6)), 20), "LINEAR halfway");
	CHECK (approx (valueAt (c, "a", CHANNEL_X, ticksToTime (5)), 15), "LINEAR a quarter of the way");
	CHECK (approx (valueAt (c, "a", CHANNEL_X, ticksToTime (4) + 128), 12.5f), "between ticks: sub-ticks");
	CHECK (approx (valueAt (c, "a", CHANNEL_X, ticksToTime (10)), 50), "INOUT halfway is halfway");
	CHECK (approx (valueAt (c, "a", CHANNEL_X, ticksToTime (9)), 30 + 40 * 0.15625f), "INOUT a quarter of the way: smoothstep");
	CHECK (approx (valueAt (c, "a", CHANNEL_X, ticksToTime (15) + 255), 70), "STEP holds until the next key");
	CHECK (approx (valueAt (c, "a", CHANNEL_X, ticksToTime (16)), 0), "and then has its value");
	CHECK (approx (valueAt (c, "a", CHANNEL_X, ticksToTime (20)), 0), "after the last key, its value");

	// Discrete channels step, whatever is between.
	setKey (c, "a", CHANNEL_DEPTH, key (0, 1, EASING_STEP), why);
	setKey (c, "a", CHANNEL_DEPTH, key (10, 5, EASING_STEP), why);
	CHECK (approx (valueAt (c, "a", CHANNEL_DEPTH, ticksToTime (9) + 255), 1) && approx (valueAt (c, "a", CHANNEL_DEPTH, ticksToTime (10)), 5),
		"depth steps");
	ClipDefinition d = c;
	d.tracks[1].keys[0].easing = EASING_LINEAR;	// Against the rules: still steps
	CHECK (approx (valueAt (d, "a", CHANNEL_DEPTH, ticksToTime (5)), 1), "a discrete channel never eases");

	// Sprites.
	setKey (c, "a", CHANNEL_SPRITE, spriteKey (2, sprite (1, 4)), why);
	setKey (c, "a", CHANNEL_SPRITE, spriteKey (6, SpriteRef::none ()), why);
	TrackSamples s;
	evaluateClip (c, 0, s);
	const TrackSample * sp = sampleOf (s, "a", CHANNEL_SPRITE);
	CHECK (sp && sp -> sprite -> kind == SpriteRef::SPRITE && sp -> sprite -> number == 4, "a sprite before its first key");
	evaluateClip (c, ticksToTime (7), s);
	sp = sampleOf (s, "a", CHANNEL_SPRITE);
	CHECK (sp && sp -> sprite -> kind == SpriteRef::NONE, "an empty frame");
	CHECK (s.size () == 3, "a sample per track with keys");

	ClipDefinition empty;
	empty.tracks.push_back (Track ());
	evaluateClip (empty, 0, s);
	CHECK (s.empty (), "a track without keys has no sample");

	// The closing key of a loop: the last stretch eases towards it.
	ClipDefinition loop = walkClip ();
	CHECK (approx (valueAt (loop, "leftArm", CHANNEL_ROTATION, ticksToTime (12)), 0), "towards the closing key");
	CHECK (approx (valueAt (loop, "leftArm", CHANNEL_ROTATION, clipTime (loop, ticksToTime (16))), 20), "the loop wraps to 0");
}

static void testClipTime () {
	ClipDefinition loop;
	loop.length = 10;
	loop.loop = true;
	CHECK (clipTime (loop, ticksToTime (25)) == ticksToTime (5), "a loop wraps");
	CHECK (clipTime (loop, ticksToTime (10)) == 0, "time runs over [0, length)");
	CHECK (! clipFinished (loop, ticksToTime (1000)), "a loop never finishes");
	ClipDefinition once = loop;
	once.loop = false;
	CHECK (clipTime (once, ticksToTime (25)) == ticksToTime (10), "a clip that doesn't loop stops at length");
	CHECK (! clipFinished (once, ticksToTime (10) - 1) && clipFinished (once, ticksToTime (10)), "and is finished there");
	ClipDefinition still;
	still.loop = true;
	CHECK (! clipLoops (still) && clipTime (still, 99) == 0 && clipFinished (still, 0), "length 0 holds, even if it loops");
	CHECK (clipTime (loop, -5) == 0, "no time before the start");
}

static std::vector<EventId> ids (const std::vector<CrossedEvent> & v) {
	std::vector<EventId> r;
	for (size_t i = 0; i < v.size (); i ++) r.push_back (v[i].event -> id);
	return r;
}

static unsigned seed = 12345;
static int rnd (int n) {
	seed = seed * 1103515245 + 12345;
	return (int) ((seed >> 8) % (unsigned) n);
}

static void testEvents () {
	ClipDefinition c;
	c.length = 10;
	c.loop = true;
	addEvent (c, 0, FrameEvent::fromNoise (1), why);	// id 1
	addEvent (c, 5, FrameEvent::fromNoise (2), why);	// id 2
	addEvent (c, 5, FrameEvent::fromNoise (3), why);	// id 3
	addEvent (c, 9, FrameEvent::fromNoise (4), why);	// id 4

	std::vector<CrossedEvent> v;
	eventsAtStart (c, v);
	CHECK (v.size () == 1 && v[0].event -> id == 1 && v[0].position == 0, "starting fires the events at 0");
	v.clear ();
	eventsCrossed (c, 0, ticksToTime (5), v);
	CHECK (v.size () == 2 && v[0].event -> id == 2 && v[1].event -> id == 3, "(from, to]: at 5, in ID order");
	v.clear ();
	eventsCrossed (c, ticksToTime (5), ticksToTime (10), v);
	CHECK (v.size () == 2 && v[0].event -> id == 4 && v[1].event -> id == 1 && v[1].position == ticksToTime (10),
		"wrapping fires the events at 0 again");
	v.clear ();
	eventsCrossed (c, ticksToTime (3), ticksToTime (33), v);
	CHECK (v.size () == 12, "three passes, each event once a pass");
	bool ordered = true;
	for (size_t i = 1; i < v.size (); i ++) ordered = ordered && v[i - 1].position <= v[i].position;
	CHECK (ordered, "in order of position");
	v.clear ();
	eventsCrossed (c, ticksToTime (4), ticksToTime (4), v);
	CHECK (v.empty (), "no move, no events");

	// Events at the end: as each pass ends, before the next pass's at 0.
	ClipDefinition ends = c;
	addEvent (ends, 10, FrameEvent::fromNoise (5), why);	// id 5
	v.clear ();
	eventsAtStart (ends, v);
	CHECK (v.size () == 1 && v[0].event -> id == 1, "not as the clip starts");
	v.clear ();
	eventsCrossed (ends, ticksToTime (9), ticksToTime (10), v);
	CHECK (v.size () == 2 && v[0].event -> id == 5 && v[1].event -> id == 1, "the end of a pass, then the start of the next");
	v.clear ();
	eventsCrossed (ends, 0, ticksToTime (30), v);
	CHECK (v.size () == 15, "once a pass");
	ClipDefinition endsOnce = ends;
	endsOnce.loop = false;
	v.clear ();
	eventsCrossed (endsOnce, ticksToTime (9), ticksToTime (50), v);
	CHECK (v.size () == 1 && v[0].event -> id == 5, "a clip that doesn't loop fires them as it finishes");

	ClipDefinition once = c;
	once.loop = false;
	v.clear ();
	eventsCrossed (once, 0, ticksToTime (100), v);
	CHECK (v.size () == 3, "a clip that doesn't loop fires each event once");
	v.clear ();
	eventsCrossed (once, ticksToTime (10), ticksToTime (100), v);
	CHECK (v.empty (), "and none once it is finished");

	// Moving in steps of any size fires exactly what one move fires.
	for (int round = 0; round < 2000; round ++) {
		ClipDefinition r;
		r.length = rnd (12);
		r.loop = rnd (3) > 0;
		int n = rnd (5);
		for (int e = 0; e < n; e ++) addEvent (r, rnd (r.length + 1), FrameEvent::fromNoise (1 + e), why);
		AnimationTime end = rnd (5000);
		std::vector<CrossedEvent> whole, steps;
		eventsCrossed (r, 0, end, whole);
		AnimationTime at = 0;
		while (at < end) {
			AnimationTime next = at + 1 + rnd (700);
			if (next > end) next = end;
			eventsCrossed (r, at, next, steps);
			at = next;
		}
		if (ids (whole) != ids (steps)) {
			CHECK (false, "steps fire what one move fires");
			break;
		}
		// And each event once per pass it starts.
		if (clipLoops (r)) {
			AnimationTime period = ticksToTime (r.length);
			size_t expected = 0;
			for (size_t e = 0; e < r.events.size (); e ++) {
				AnimationTime p = ticksToTime (r.events[e].time);
				if (p == 0) p = period;
				if (p <= end) expected += 1 + (size_t) ((end - p) / period);
			}
			if (whole.size () != expected) {
				CHECK (false, "each event once a pass");
				break;
			}
		}
	}
}

// What the legacy stepper does, update by update, against clipFrames.
static ClipDefinitionRef randomLegacy () {
	std::vector<AnimationFrame> frames (1 + rnd (6));
	for (size_t f = 0; f < frames.size (); f ++) {
		frames[f].sprite = rnd (10) - 3;
		frames[f].ticks = rnd (10) ? 1 + rnd (4) : rnd (2) ? 0 : -2;
		int e = rnd (5);
		frames[f].event = FrameEvent::fromNoise (e == 0 ? 1 + rnd (50) : e == 1 ? - (1 + rnd (50)) : 0);
	}
	return makeLegacyClip (SpriteBankRef (), frames);
}

class Recorder : public FrameEventSink {
public:
	std::vector<int> events;
	void frameEvent (const FrameEvent & e) { events.push_back (e.toNoise ()); }
};

static void testLegacyFrames () {
	int compared = 0;
	for (int round = 0; round < 3000; round ++) {
		personaAnimation anim;
		anim.definition = randomLegacy ();
		ClipDefinition c;
		c.length = rnd (60);
		c.loop = rnd (2) == 0;
		if (! addLegacyFrames (c, "body", anim, why)) {
			CHECK (false, "clipFrames converts");
			break;
		}
		if (! validClip (c, why)) {
			CHECK (false, "into a valid clip");
			break;
		}
		AnimationPlayback p;
		Recorder legacy;
		bool same = true;
		for (int k = 1; k <= c.length + 1 && same; k ++) {
			legacy.events.clear ();
			updateAnimation (p, & anim, 1, true, legacy);
			// Update k shows time k - 1.
			TrackSamples s;
			evaluateClip (c, ticksToTime (k - 1), s);
			const TrackSample * x = sampleOf (s, "body", CHANNEL_SPRITE);
			int shown = anim.frame (p.displayFrame).sprite;
			same = x && x -> sprite -> number == (shown < 0 ? - shown : shown) && x -> sprite -> mirror == (shown < 0);
			// And fires what the timeline does from k - 1 to k (and at the
			// start); at the end of a loop, the next pass starts, which an
			// animation of anim () does only when its passes fit the clip.
			if (k <= c.length - (c.loop ? 1 : 0)) {
				std::vector<CrossedEvent> v;
				if (k == 1) eventsAtStart (c, v);
				eventsCrossed (c, ticksToTime (k - 1), ticksToTime (k), v);
				std::vector<int> noise;
				for (size_t i = 0; i < v.size (); i ++) noise.push_back (v[i].event -> what.toNoise ());
				same = same && noise == legacy.events;
			}
			compared ++;
		}
		if (! same) {
			CHECK (false, "clipFrames shows and fires what the legacy stepper does");
			break;
		}
	}
	printf ("Compared %d ticks of clipFrames with the legacy stepper\n", compared);

	// Replacing: the track keeps its ID, and its old keys go.
	ClipDefinition c;
	c.length = 8;
	setKey (c, "body", CHANNEL_SPRITE, spriteKey (3, SpriteRef::none ()), why);
	TrackId id = c.tracks[0].id;
	std::vector<AnimationFrame> frames (1, frame (2, 8));
	personaAnimation anim;
	anim.definition = makeLegacyClip (SpriteBankRef (), frames);
	CHECK (addLegacyFrames (c, "body", anim, why), "frames over a track");
	CHECK (c.tracks.size () == 1 && c.tracks[0].id == id && c.tracks[0].keys.size () == 1
		&& c.tracks[0].keys[0].sprite.number == 2, "replace its keys, and keep its ID");

	ClipDefinition legacyClip = * anim.definition;
	CHECK (! addLegacyFrames (legacyClip, "body", anim, why), "not into an animation of anim ()");
	ClipDefinition tooLong;
	tooLong.length = maxLegacyFramesTicks + 1;
	CHECK (! addLegacyFrames (tooLong, "body", anim, why), "not into an enormous clip");
	personaAnimation none;
	CHECK (! addLegacyFrames (c, "body", none, why), "not from an animation without frames");
}

// The player: updateTimeline against the legacy stepper, on the same
// animation, a tick at a time, both shown.
static void testTimelinePlayer () {
	int compared = 0;
	for (int round = 0; round < 2000; round ++) {
		personaAnimation legacy;
		legacy.definition = randomLegacy ();
		// The stepper's period: the ticks until its state after the first
		// update comes round again. (Some never come back to it, as a
		// frame that holds; those have no whole passes to compare.)
		int period = 0;
		{
			AnimationPlayback s;
			Recorder ignored;
			updateAnimation (s, & legacy, 1, true, ignored);
			int frame = s.frame, left = s.ticksLeft;
			for (int t = 1; t <= 200 && ! period; t ++) {
				updateAnimation (s, & legacy, 1, true, ignored);
				if (s.frame == frame && s.ticksLeft == left) period = t;
			}
		}
		if (period <= 0) continue;
		std::shared_ptr<ClipDefinition> c = std::make_shared<ClipDefinition> ();
		c -> length = period * (1 + rnd (3));
		c -> loop = true;
		if (! addLegacyFrames (* c, "", legacy, why)) continue;
		personaAnimation timeline;
		timeline.definition = c;

		AnimationPlayback lp, tp;
		Recorder le;
		std::vector<TimelineEvent> te;
		bool same = true;
		for (int k = 0; k < 300 && same; k ++) {
			le.events.clear ();
			te.clear ();
			updateAnimation (lp, & legacy, 1, true, le);
			updateTimeline (tp, & timeline, te);
			const SpriteRef * r = sampleSprite (* c, "", shownTime (tp, & timeline));
			int shown = legacy.frame (lp.displayFrame).sprite;
			same = r && r -> number == (shown < 0 ? - shown : shown) && r -> mirror == (shown < 0);
			std::vector<int> noise;
			for (size_t i = 0; i < te.size (); i ++) noise.push_back (te[i].what.toNoise ());
			same = same && noise == le.events;
			compared ++;
		}
		if (! same) {
			// A chain of events over the loop's end is the one thing a
			// timeline can't do (see addLegacyFrames): it plays the first pass.
			const AnimationFrame & a = legacy.frame (0), & z = legacy.frame (legacy.numFrames () - 1);
			if (a.event.kind != FrameEvent::NONE && z.event.kind != FrameEvent::NONE) continue;
			CHECK (false, "a clip of clipFrames plays as its animation does, loop after loop");
			break;
		}
	}
	printf ("Played %d ticks of clips against the legacy stepper\n", compared);

	// Start, speed, finishing, and starting again.
	std::shared_ptr<ClipDefinition> c = std::make_shared<ClipDefinition> ();
	c -> length = 4;
	setKey (* c, "", CHANNEL_SPRITE, spriteKey (0, sprite (1, 0)), why);
	setKey (* c, "", CHANNEL_SPRITE, spriteKey (2, sprite (1, 1)), why);
	addEvent (* c, 0, FrameEvent::fromNoise (1), why);
	addEvent (* c, 3, FrameEvent::fromNoise (2), why);
	personaAnimation a;
	a.definition = c;
	AnimationPlayback p;
	std::vector<TimelineEvent> e;
	updateTimeline (p, & a, e);
	CHECK (e.size () == 1 && e[0].what.number == 1 && shownTime (p, & a) == 0, "starting fires the events at 0, and shows 0");
	CHECK (currentTime (p, & a) == ticksToTime (1), "then moves on a tick");
	e.clear ();
	updateTimeline (p, & a, e);
	updateTimeline (p, & a, e);
	CHECK (e.size () == 1 && e[0].what.number == 2, "the event at 3");
	CHECK (! timelineFinished (p, & a), "not finished before its end");
	updateTimeline (p, & a, e);
	CHECK (timelineFinished (p, & a) && currentTime (p, & a) == ticksToTime (4), "a clip that doesn't loop finishes");
	e.clear ();
	for (int i = 0; i < 10; i ++) updateTimeline (p, & a, e);
	CHECK (e.empty () && shownTime (p, & a) == ticksToTime (4), "and holds its end, without events");

	personaAnimation b;
	b.definition = c;
	updateTimeline (p, & b, e);
	CHECK (e.size () == 1 && shownTime (p, & b) == 0, "another handle starts again");

	AnimationPlayback half;
	half.speed = subTicksPerTick / 2;
	e.clear ();
	for (int i = 0; i < 6; i ++) updateTimeline (half, & a, e);
	CHECK (currentTime (half, & a) == ticksToTime (3) && e.size () == 2, "half speed: 3 ticks in 6");
	AnimationPlayback paused;
	paused.speed = 0;
	e.clear ();
	for (int i = 0; i < 6; i ++) updateTimeline (paused, & a, e);
	CHECK (currentTime (paused, & a) == 0 && e.size () == 1, "paused: only the start");

	personaAnimation l;
	l.definition = randomLegacy ();
	AnimationPlayback lp;
	updateTimeline (lp, & l, e);
	CHECK (lp.playing == NULL, "updateTimeline leaves legacy animations alone");
	CHECK (! isTimeline (& l) && isTimeline (& a), "which are not timelines");

	// Editing and sealing.
	std::string w;
	CHECK (editableClip (& a, w) == c.get (), "a new clip can change");
	sealClip (& a);
	CHECK (editableClip (& b, w) == NULL && ! w.empty (), "once used, no handle of it can change it");
	CHECK (editableClip (& l, w) == NULL, "nor an animation of anim ()");
}

// Poses (animpose.cpp).

static std::vector<std::string> diagnostics;
static void recordDiagnostic (const std::string & what) { diagnostics.push_back (what); }

static bool near2 (float x, float y, float ex, float ey) {
	return fabsf (x - ex) < 1e-3f && fabsf (y - ey) < 1e-3f;
}

// Where a point of a part's own space is in the world.
static void worldOf (const Pose & p, int part, float x, float y, float & wx, float & wy) {
	p.parts[part].world.apply (x, y, wx, wy);
}

// A bank of 4 sprites, file 7.
static SpriteBankRef bankOf4 () {
	static SpriteBankRef b;
	if (! b) {
		b = std::make_shared<loadedSpriteBank> ();
		b -> ID = 7;
		b -> bank.total = 4;
		b -> bank.sprites.resize (4);
	}
	return b;
}

static SpriteRef realSprite (int n, bool mirror = false) {
	return SpriteRef::sprite (bankOf4 (), 7, n, mirror);
}

static void testTransforms () {
	float x, y;
	Transform2D r = Transform2D::rotation (90);
	r.apply (1, 0, x, y);
	CHECK (near2 (x, y, 0, 1), "90 degrees turns right to down: clockwise on screen");
	(Transform2D::translation (5, 6) * Transform2D::scaling (2)).apply (1, 1, x, y);
	CHECK (near2 (x, y, 7, 8), "scale, then move");
	Transform2D::flipX ().apply (3, 4, x, y);
	CHECK (near2 (x, y, -3, 4), "a flip mirrors X");
	CHECK (Transform2D::flipX ().mirrors () && ! Transform2D::rotation (30).mirrors (), "mirroring is told by the determinant");
	Transform2D t = Transform2D::translation (10, -3) * Transform2D::rotation (37) * Transform2D::scaling (1.5f) * Transform2D::flipX (), inv;
	CHECK (t.inverse (inv), "an inverse");
	float bx, by;
	t.apply (2, 5, x, y);
	inv.apply (x, y, bx, by);
	CHECK (near2 (bx, by, 2, 5), "which undoes it");
	CHECK (! Transform2D::scaling (0).inverse (inv), "no inverse when scaled to nothing");

	PartPose p;
	p.x = 3;
	p.y = 4;
	p.rotation = 90;
	p.scale = 2;
	p.flip = true;
	localTransform (p).apply (1, 0, x, y);
	CHECK (near2 (x, y, 3, 2), "local: flip, scale, turn, then move");
}

static void testPoses () {
	diagnostics.clear ();
	setAnimDiagnosticHandler (recordDiagnostic);
	forgetPoseReports ();

	std::shared_ptr<RigDefinition> rig = std::make_shared<RigDefinition> ();
	addPart (* rig, "torso", "", at (0, 0), realSprite (0), why);
	addPart (* rig, "head", "torso", at (0, -70), realSprite (1), why);
	addPart (* rig, "eyes", "head", at (0, -14), realSprite (2), why);
	addPart (* rig, "hand", "torso", at (20, -40), SpriteRef (), why);	// A joint
	addPart (* rig, "glove", "hand", at (5, 0), realSprite (3), why);
	int torso = rig -> findPart ("torso"), head = rig -> findPart ("head"), eyes = rig -> findPart ("eyes"),
		hand = rig -> findPart ("hand"), glove = rig -> findPart ("glove");
	RigDefinitionRef r = rig;
	PoseBinding binding;
	Pose pose;
	float x, y;

	// At rest, where the character is.
	evaluatePose (r, ClipDefinitionRef (), TrackSamples (), Transform2D::translation (100, 200), binding, pose);
	CHECK (pose.parts.size () == rig -> parts.size (), "a posed part per part");
	worldOf (pose, eyes, 0, 0, x, y);
	CHECK (near2 (x, y, 100, 116), "parts are placed from their parents' origins");
	CHECK (pose.parts[hand].sprite.kind == SpriteRef::NONE && pose.parts[0].sprite.kind == SpriteRef::NONE, "a joint and the root show nothing");
	worldOf (pose, glove, 0, 0, x, y);
	CHECK (near2 (x, y, 125, 160), "a joint still places what is attached to it");
	CHECK (pose.drawOrder.size () == 4, "four parts to draw");

	// The convention: a child at (0, -70) of a part turned 90 degrees.
	std::shared_ptr<ClipDefinition> c = std::make_shared<ClipDefinition> ();
	c -> length = 10;
	setKey (* c, "torso", CHANNEL_ROTATION, key (0, 90), why);
	TrackSamples s;
	evaluateClip (* c, 0, s);
	evaluatePose (r, c, s, Transform2D (), binding, pose);
	worldOf (pose, head, 0, 0, x, y);
	CHECK (near2 (x, y, 70, 0), "a child at (0, -70) of a part turned 90 degrees lies at (70, 0)");
	worldOf (pose, eyes, 0, 0, x, y);
	CHECK (near2 (x, y, 84, 0), "and its children turn with it");

	// Scale and flip, inherited.
	c = std::make_shared<ClipDefinition> ();
	c -> length = 10;
	setKey (* c, "torso", CHANNEL_SCALE, key (0, 2), why);
	setKey (* c, "head", CHANNEL_FLIP, key (0, 1, EASING_STEP), why);
	setKey (* c, "eyes", CHANNEL_X, key (0, 3), why);
	evaluateClip (* c, 0, s);
	evaluatePose (r, c, s, Transform2D (), binding, pose);
	worldOf (pose, head, 0, 0, x, y);
	CHECK (near2 (x, y, 0, -140), "a part's scale scales what is attached");
	worldOf (pose, eyes, 0, 0, x, y);
	CHECK (near2 (x, y, -6, -168), "a flipped part mirrors what is attached, scaled");
	CHECK (pose.parts[eyes].world.mirrors () && ! pose.parts[torso].world.mirrors (), "and mirrors it, but not its parent");
	worldOf (pose, glove, 0, 0, x, y);
	CHECK (near2 (x, y, 50, -80), "the other arm is scaled only");

	// The character's flip, scale and place.
	Transform2D character = Transform2D::translation (300, 400) * Transform2D::scaling (0.5f) * Transform2D::flipX ();
	evaluatePose (r, ClipDefinitionRef (), TrackSamples (), character, binding, pose);
	worldOf (pose, glove, 0, 0, x, y);
	CHECK (near2 (x, y, 300 - 12.5f, 380), "the character's transform is the root's parent");

	// Visibility, and empty frames.
	c = std::make_shared<ClipDefinition> ();
	c -> length = 10;
	setKey (* c, "head", CHANNEL_VISIBLE, key (0, 0, EASING_STEP), why);
	setKey (* c, "torso", CHANNEL_SPRITE, spriteKey (0, SpriteRef::none ()), why);
	evaluateClip (* c, 0, s);
	evaluatePose (r, c, s, Transform2D (), binding, pose);
	CHECK (! pose.parts[head].shown && ! pose.parts[eyes].shown, "VISIBLE 0 hides the part and what is attached");
	CHECK (pose.parts[torso].shown && pose.parts[torso].sprite.kind == SpriteRef::NONE, "NONE: the part shows nothing");
	CHECK (pose.parts[glove].shown, "but what is attached still does");
	CHECK (pose.drawOrder.size () == 1 && pose.drawOrder[0] == glove, "only the glove to draw");

	// Sprites: from a key, DEFAULT, and missing ones.
	diagnostics.clear ();
	c = std::make_shared<ClipDefinition> ();
	c -> length = 10;
	setKey (* c, "head", CHANNEL_SPRITE, spriteKey (0, realSprite (3, true)), why);
	setKey (* c, "eyes", CHANNEL_SPRITE, spriteKey (0, realSprite (9)), why);	// Its bank has 4
	setKey (* c, "glove", CHANNEL_SPRITE, spriteKey (0, SpriteRef ()), why);
	setKey (* c, "hand", CHANNEL_SPRITE, spriteKey (0, SpriteRef ()), why);
	evaluateClip (* c, 0, s);
	evaluatePose (r, c, s, Transform2D (), binding, pose);
	CHECK (pose.parts[head].sprite.number == 3 && pose.parts[head].sprite.mirror, "a key's sprite, mirror image and all");
	CHECK (pose.parts[eyes].sprite.kind == SpriteRef::SPRITE && pose.parts[eyes].sprite.number == 2, "a missing sprite: the part's own");
	CHECK (diagnostics.size () == 1 && diagnostics[0].find ("sprite 9 of bank 7") != std::string::npos, "reported");
	CHECK (pose.parts[glove].sprite.number == 3, "DEFAULT: the part's own sprite");
	CHECK (pose.parts[hand].sprite.kind == SpriteRef::NONE, "DEFAULT of a joint: nothing");
	evaluatePose (r, c, s, Transform2D (), binding, pose);
	CHECK (diagnostics.size () == 1, "reported once");

	// Parts the rig doesn't have: left out, reported once for the clip and rig.
	diagnostics.clear ();
	c = std::make_shared<ClipDefinition> ();
	c -> length = 10;
	setKey (* c, "tail", CHANNEL_ROTATION, key (0, 45), why);
	setKey (* c, "head", CHANNEL_Y, key (0, -80), why);
	evaluateClip (* c, 0, s);
	evaluatePose (r, c, s, Transform2D (), binding, pose);
	worldOf (pose, head, 0, 0, x, y);
	CHECK (near2 (x, y, 0, -80), "the tracks of parts it has play");
	CHECK (diagnostics.size () == 1 && diagnostics[0].find ("\"tail\"") != std::string::npos, "a missing part is reported");
	evaluatePose (r, c, s, Transform2D (), binding, pose);
	PoseBinding other;
	evaluatePose (r, c, s, Transform2D (), other, pose);
	CHECK (diagnostics.size () == 1, "once for its clip and rig");

	// The binding follows the clip and rig.
	std::shared_ptr<RigDefinition> rig2 = std::make_shared<RigDefinition> ();
	addPart (* rig2, "tail", "", at (0, 0), realSprite (0), why);
	addPart (* rig2, "head", "tail", at (1, 1), realSprite (1), why);
	RigDefinitionRef r2 = rig2;
	evaluatePose (r2, c, s, Transform2D (), binding, pose);
	CHECK (pose.parts.size () == 3 && pose.parts[1].local.rotation == 45, "another rig: the tail is there now");
	worldOf (pose, 2, 0, 0, x, y);
	float tx, ty;
	pose.parts[1].world.apply (1, -80, tx, ty);	// Its x of the rig, its y of the clip
	CHECK (near2 (x, y, tx, ty) && ! near2 (x, y, 1, -79), "and the head follows the tail's turn");

	// Depth: back to front, then in rig order.
	c = std::make_shared<ClipDefinition> ();
	c -> length = 10;
	setKey (* c, "eyes", CHANNEL_DEPTH, key (0, -1, EASING_STEP), why);
	evaluateClip (* c, 0, s);
	evaluatePose (r, c, s, Transform2D (), binding, pose);
	CHECK (pose.drawOrder.size () == 4 && pose.drawOrder[0] == eyes && pose.drawOrder[1] == torso && pose.drawOrder[2] == head
		&& pose.drawOrder[3] == glove, "by depth, then rig order");

	// A clip of a single part (as anim () on a rig): the root's sprite.
	ClipDefinitionRef legacy = makeLegacyClip (bankOf4 (), std::vector<AnimationFrame> (1, frame (1, 3)));
	evaluateClip (* legacy, 0, s);
	evaluatePose (r, legacy, s, Transform2D (), binding, pose);
	CHECK (pose.parts[0].sprite.kind == SpriteRef::SPRITE && pose.parts[0].sprite.number == 1, "an animation of anim () shows on the root");

	setAnimDiagnosticHandler (NULL);
}

// The quads of sprites: corners about the hotspot, and the box of a pose.
static void testSpriteCorners () {
	float c[8];
	spriteCorners (Transform2D::translation (100, 50), 10, 20, 4, 15, false, c);
	CHECK (near2 (c[0], c[1], 96, 35) && near2 (c[2], c[3], 106, 35) && near2 (c[4], c[5], 96, 55) && near2 (c[6], c[7], 106, 55),
		"the hotspot at the origin: top left, top right, bottom left, bottom right");
	spriteCorners (Transform2D::translation (100, 50), 10, 20, 4, 15, true, c);
	CHECK (near2 (c[0], c[1], 104, 35) && near2 (c[2], c[3], 94, 35), "a mirror image turns about the hotspot");
	spriteCorners (Transform2D::rotation (90) * Transform2D::scaling (2), 10, 20, 0, 0, false, c);
	CHECK (near2 (c[2], c[3], 0, 20) && near2 (c[4], c[5], -40, 0), "turned and scaled");

	// A pose's box: a sprite of 10 by 20 with its hotspot at the bottom middle.
	SpriteBankRef b = std::make_shared<loadedSpriteBank> ();
	b -> bank.total = 1;
	b -> bank.sprites.resize (1);
	b -> bank.sprites[0].width = 10;
	b -> bank.sprites[0].height = 20;
	b -> bank.sprites[0].xhot = 5;
	b -> bank.sprites[0].yhot = 20;
	std::shared_ptr<RigDefinition> rig = std::make_shared<RigDefinition> ();
	addPart (* rig, "body", "", at (0, 0), SpriteRef::sprite (b, 1, 0, false), why);
	addPart (* rig, "hat", "body", at (0, -20), SpriteRef::sprite (b, 1, 0, false), why);
	Pose pose;
	PoseBinding binding;
	evaluatePose (rig, ClipDefinitionRef (), TrackSamples (), Transform2D (), binding, pose);
	float x1, y1, x2, y2;
	CHECK (poseBounds (pose, x1, y1, x2, y2) && near2 (x1, y1, -5, -40) && near2 (x2, y2, 5, 0), "the box of a pose");
	std::shared_ptr<RigDefinition> empty = std::make_shared<RigDefinition> ();
	evaluatePose (empty, ClipDefinitionRef (), TrackSamples (), Transform2D (), binding, pose);
	CHECK (! poseBounds (pose, x1, y1, x2, y2), "a pose that draws nothing has no box");
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
	testEvaluation ();
	testClipTime ();
	testEvents ();
	testLegacyFrames ();
	testTimelinePlayer ();
	testTransforms ();
	testPoses ();
	testSpriteCorners ();
	testLifetimes ();
	if (failures) {
		fprintf (stderr, "%d failures\n", failures);
		return 1;
	}
	printf ("All animation model tests passed\n");
	return 0;
}
