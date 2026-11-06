/*
 * Tests of animation playback (source/Engine/animation.cpp): updateAnimation
 * must do exactly what drawPeople did each frame before playback had its own
 * state, which we keep here as it was, run side by side on random scripts.
 * Also the mouse cursor's rule, speed, and the game's tick rate.
 */

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "animation.h"

static int failures = 0;

#define CHECK(cond, what) do { if (! (cond)) { fprintf (stderr, "FAIL: %s (line %d)\n", what, __LINE__); failures ++; } } while (0)

// What the engine started, in order.
class Recorder : public FrameEventSink {
public:
	std::vector<int> events;	// As noise: > 0 a sound, < 0 a function
	void frameEvent (const FrameEvent & e) { events.push_back (e.toNoise ()); }
};

// The engine before, as drawPeople had it, for one character.
struct LegacyPerson {
	const personaAnimation * myAnim;
	const personaAnimation * lastUsedAnim;
	int frameNum, frameTick;
	bool show;
	int drawn;			// The frame it drew, or -1
};

static void legacyFrame (LegacyPerson & p, std::vector<int> & events) {
	// A transcription of drawPeople, with the frames of the definition.
	const personaAnimation * myAnim = NULL;
	p.drawn = -1;
	if (p.show) {
		myAnim = p.myAnim;
		if (myAnim != p.lastUsedAnim) {
			p.lastUsedAnim = myAnim;
			p.frameNum = 0;
			p.frameTick = myAnim -> frame (0).ticks;
			int noise = myAnim -> frame (p.frameNum).event.toNoise ();
			if (noise > 0) {
				events.push_back (noise);
				p.frameNum ++;
				p.frameNum %= p.myAnim -> numFrames ();
				p.frameTick = p.myAnim -> frame (p.frameNum).ticks;
			} else if (noise) {
				events.push_back (noise);
				p.frameNum ++;
				p.frameNum %= p.myAnim -> numFrames ();
				p.frameTick = p.myAnim -> frame (p.frameNum).ticks;
			}
		}
		p.drawn = p.frameNum;
	}
	if (! -- p.frameTick) {
		p.frameNum ++;
		p.frameNum %= p.myAnim -> numFrames ();
		p.frameTick = p.myAnim -> frame (p.frameNum).ticks;
		if (p.show && myAnim) {
			int noise = myAnim -> frame (p.frameNum).event.toNoise ();
			if (noise > 0) {
				events.push_back (noise);
				p.frameNum ++;
				p.frameNum %= p.myAnim -> numFrames ();
				p.frameTick = p.myAnim -> frame (p.frameNum).ticks;
			} else if (noise) {
				events.push_back (noise);
				p.frameNum ++;
				p.frameNum %= p.myAnim -> numFrames ();
				p.frameTick = p.myAnim -> frame (p.frameNum).ticks;
			}
		}
	}
}

static unsigned int seed = 2026;
static int rnd (int n) {
	seed = seed * 1103515245 + 12345;
	return (int) ((seed >> 8) % (unsigned) n);
}

// A random animation: up to 6 frames of 0 to 4 ticks, some with events,
// sometimes on the first frame and sometimes two in a row.
static AnimationDefinitionRef randomAnimation () {
	std::vector<AnimationFrame> frames (1 + rnd (6));
	for (size_t f = 0; f < frames.size (); f ++) {
		frames[f].sprite = rnd (10) - 3;
		frames[f].ticks = rnd (10) ? 1 + rnd (4) : 0;
		int e = rnd (6);
		frames[f].event = FrameEvent::fromNoise (e == 0 ? 1 + rnd (50) : e == 1 ? - (1 + rnd (50)) : 0);
	}
	return makeAnimation (SpriteBankRef (), frames);
}

static void testAgainstLegacy () {
	long ticksCompared = 0;
	bool same = true;
	for (int run = 0; run < 3000 && same; run ++) {
		// Handles: two copies of one definition, and others.
		AnimationDefinitionRef shared = randomAnimation ();
		personaAnimation handles[5];
		handles[0].definition = shared;
		handles[1].definition = shared;
		for (int h = 2; h < 5; h ++) handles[h].definition = randomAnimation ();

		// addPerson never set frameTick; both start from 0.
		LegacyPerson old = { &handles[0], NULL, 0, 0, true, -1 };
		AnimationPlayback now;
		const personaAnimation * current = &handles[0];
		Recorder recorder;
		std::vector<int> oldEvents;

		for (int t = 0; t < 400; t ++) {
			// What scripts do between frames: show or hide, change the
			// animation, sometimes twice, back to where it was.
			if (! rnd (15)) old.show = ! old.show;
			if (! rnd (8)) current = &handles[rnd (5)];
			if (! rnd (30)) {
				const personaAnimation * was = current;
				current = &handles[rnd (5)];
				current = was;
			}
			old.myAnim = current;

			legacyFrame (old, oldEvents);
			updateAnimation (now, current, 1, old.show, recorder);

			ticksCompared ++;
			if (old.frameNum != now.frame || old.frameTick != now.ticksLeft || old.lastUsedAnim != now.playing
					|| (old.show && old.drawn != now.displayFrame) || oldEvents != recorder.events) {
				fprintf (stderr, "Run %d, tick %d: frame %d/%d, ticks %d/%d, drawn %d/%d, events %d/%d\n",
					run, t, old.frameNum, now.frame, old.frameTick, now.ticksLeft, old.drawn, now.displayFrame,
					(int) oldEvents.size (), (int) recorder.events.size ());
				same = false;
				break;
			}
		}
	}
	CHECK (same, "updateAnimation does what drawPeople did, tick for tick, events and all");
	printf ("Compared %ld ticks with the old playback\n", ticksCompared);
}

static void testHandlesAndDefinitions () {
	// Copies share the definition, but each copy is its own handle: a
	// character given the other copy starts again.
	std::vector<AnimationFrame> frames (3);
	for (int f = 0; f < 3; f ++) {
		frames[f].sprite = f;
		frames[f].ticks = 2;
	}
	personaAnimation a, b;
	a.definition = makeAnimation (SpriteBankRef (), frames);
	b.definition = a.definition;
	CHECK (a.definition.get () == b.definition.get () && a.definition.use_count () == 2, "copies share one definition");

	Recorder r;
	AnimationPlayback p;
	for (int t = 0; t < 3; t ++) updateAnimation (p, &a, 1, true, r);
	CHECK (p.frame == 1, "plays on");
	updateAnimation (p, &b, 1, true, r);
	CHECK (p.displayFrame == 0 && p.playing == &b, "another handle of the same definition starts again");
	updateAnimation (p, &b, 1, true, r);
	CHECK (p.displayFrame == 0 && p.frame == 1, "and then plays on");

	// Two characters with handles on one definition play apart.
	personaAnimation c;
	c.definition = a.definition;
	AnimationPlayback q;
	updateAnimation (q, &c, 1, true, r);
	CHECK (q.displayFrame == 0 && p.frame == 1, "each character has its own playback");
}

static void testEventsOnlyWhenAdvancing () {
	std::vector<AnimationFrame> frames (2);
	frames[0].sprite = 0;
	frames[0].ticks = 1;
	frames[0].event = FrameEvent::fromNoise (7);
	frames[1].sprite = 1;
	frames[1].ticks = 3;
	personaAnimation a;
	a.definition = makeAnimation (SpriteBankRef (), frames);
	Recorder r;
	AnimationPlayback p;
	updateAnimation (p, &a, 1, true, r);
	CHECK (r.events.size () == 1 && r.events[0] == 7, "starting fires the first frame's event");
	// Drawing reads the playback, as often as it likes, without an event sink.
	const AnimationPlayback & drawn = p;
	int seen = 0;
	for (int i = 0; i < 100; i ++) seen += a.frame (drawn.displayFrame).sprite;
	CHECK (r.events.size () == 1 && seen == 100, "reading the playback fires nothing");
	// Paused, it doesn't move or fire either.
	p.speed = 0;
	for (int i = 0; i < 20; i ++) updateAnimation (p, &a, 1, true, r);
	CHECK (r.events.size () == 1 && p.frame == 1 && p.ticksLeft == 2, "paused, nothing moves");
}

static void testSpeed () {
	std::vector<AnimationFrame> frames (4);
	for (int f = 0; f < 4; f ++) {
		frames[f].sprite = f;
		frames[f].ticks = 4;
	}
	personaAnimation a;
	a.definition = makeAnimation (SpriteBankRef (), frames);
	Recorder r;

	// Twice as fast: two ticks of the countdown for each game tick.
	AnimationPlayback fast, normal;
	fast.speed = 2 * subTicksPerTick;
	for (int t = 0; t < 10; t ++) {
		updateAnimation (fast, &a, 1, true, r);
		updateAnimation (normal, &a, 1, true, r);
		updateAnimation (normal, &a, 1, true, r);
	}
	CHECK (fast.frame == normal.frame && fast.ticksLeft == normal.ticksLeft, "2x speed is two ticks a tick");

	// Half as fast: a tick of the countdown every second game tick.
	AnimationPlayback slow, normal2;
	slow.speed = subTicksPerTick / 2;
	for (int t = 0; t < 20; t ++) updateAnimation (slow, &a, 1, true, r);
	for (int t = 0; t < 10; t ++) updateAnimation (normal2, &a, 1, true, r);
	CHECK (slow.frame == normal2.frame && slow.ticksLeft == normal2.ticksLeft, "1/2 speed is a tick every second tick");
}

static void testCursor () {
	// The rule of displayCursor: draw, then count up to the frame's ticks.
	std::vector<AnimationFrame> frames (3);
	int ticks[3] = { 2, 0, 3 };
	for (int f = 0; f < 3; f ++) {
		frames[f].sprite = f;
		frames[f].ticks = ticks[f];
	}
	personaAnimation a;
	a.definition = makeAnimation (SpriteBankRef (), frames);
	AnimationPlayback p;
	int oldFrame = 0, oldCount = 0;
	bool same = true;
	for (int t = 0; t < 100; t ++) {
		int drawn = oldFrame;
		if (++ oldCount >= ticks[oldFrame]) {
			oldCount = 0;
			oldFrame = (oldFrame + 1) % 3;
		}
		updateCursorAnimation (p, &a, 1);
		if (p.displayFrame != drawn || p.frame != oldFrame || p.ticksLeft != oldCount) same = false;
	}
	CHECK (same, "the cursor counts as it always did");
}

static void testTickRate () {
	// Seconds go through the game's own tick rate (its frame time), never
	// through how long frames take to draw.
	setGameTickRate (50);		// A game of 20 ms a frame
	CHECK (gameTickRate () == 50 && ticksForSeconds (1.0) == 50 && ticksForSeconds (0.5) == 25, "50 ticks a second");
	setGameTickRate (20);		// A game of 50 ms a frame
	CHECK (ticksForSeconds (1.0) == 20 && ticksForSeconds (2.5) == 50 && secondsForTicks (30) == 1.5, "20 ticks a second");
	setGameTickRate (0);
	CHECK (gameTickRate () == 20, "a bad rate changes nothing");
}

int main () {
	testAgainstLegacy ();
	testHandlesAndDefinitions ();
	testEventsOnlyWhenAdvancing ();
	testSpeed ();
	testCursor ();
	testTickRate ();
	if (failures) {
		fprintf (stderr, "%d failures\n", failures);
		return 1;
	}
	printf ("All animation tests passed\n");
	return 0;
}
