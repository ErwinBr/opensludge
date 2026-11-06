#include "animation.h"
#include "ownedanimation.h"

void OwnedAnimation::adopt (AnimationHandle h) {
	handle = h.release ();
}

AnimationHandle OwnedAnimation::release () {
	AnimationHandle h (handle);
	handle = NULL;
	return h;
}

FrameEvent FrameEvent::fromNoise (int noise) {
	FrameEvent e;
	if (noise > 0) {
		e.kind = SOUND;
		e.number = noise;
	} else if (noise < 0) {
		e.kind = FUNCTION;
		e.number = - noise;
	}
	return e;
}

int FrameEvent::toNoise () const {
	switch (kind) {
		case SOUND: return number;
		case FUNCTION: return - number;
		default: return 0;
	}
}

AnimationDefinitionRef makeAnimation (const SpriteBankRef & bank, const std::vector<AnimationFrame> & frames) {
	std::shared_ptr<AnimationDefinition> d = std::make_shared<AnimationDefinition> ();
	d -> spriteBank = bank;
	d -> frames = frames;
	return d;
}

namespace {

// Move on to the next frame, and show it for its ticks.
void nextFrame (AnimationPlayback & p, const personaAnimation & anim) {
	p.frame = (p.frame + 1) % anim.numFrames ();
	p.ticksLeft = anim.frame (p.frame).ticks;
}

// Entering a frame with an event fires it, and passes the frame at once.
void enterFrame (AnimationPlayback & p, const personaAnimation & anim, FrameEventSink & events) {
	const FrameEvent & e = anim.frame (p.frame).event;
	if (e.kind == FrameEvent::NONE) return;
	events.frameEvent (e);
	nextFrame (p, anim);
}

// One tick of the countdown, as the engine always did it: the frame's ticks
// count down, and at 0 the next frame starts. A frame of 0 ticks counts past
// 0 and so holds for good.
void tick (AnimationPlayback & p, const personaAnimation & anim, bool shown, FrameEventSink & events) {
	if (-- p.ticksLeft) return;
	nextFrame (p, anim);
	if (shown) enterFrame (p, anim, events);
}

int tickRate = 50;

}

void updateAnimation (AnimationPlayback & p, const personaAnimation * anim, int ticks, bool shown, FrameEventSink & events) {
	// The engine never animated a character without frames (it crashed).
	if (! anim || ! anim -> numFrames ()) return;
	if (shown && anim != p.playing) {
		p.playing = anim;
		p.frame = 0;
		p.ticksLeft = anim -> frame (0).ticks;
		enterFrame (p, * anim, events);
	}
	p.displayFrame = p.frame;
	p.subTicks += ticks * p.speed;
	int whole = p.subTicks / subTicksPerTick;
	p.subTicks %= subTicksPerTick;
	while (whole --) tick (p, * anim, shown, events);
}

void restartAnimationQuietly (AnimationPlayback & p, const personaAnimation * anim) {
	p.playing = anim;
	p.frame = 0;
	p.ticksLeft = anim && anim -> numFrames () ? anim -> frame (0).ticks : 0;
	p.displayFrame = 0;
}

void updateCursorAnimation (AnimationPlayback & p, const personaAnimation * anim, int ticks) {
	if (! anim || ! anim -> numFrames ()) return;
	p.displayFrame = p.frame;
	p.subTicks += ticks * p.speed;
	int whole = p.subTicks / subTicksPerTick;
	p.subTicks %= subTicksPerTick;
	while (whole --) {
		if (++ p.ticksLeft >= anim -> frame (p.frame).ticks) {
			p.ticksLeft = 0;
			p.frame = (p.frame + 1) % anim -> numFrames ();
		}
	}
}

void setGameTickRate (int ticksPerSecond) {
	if (ticksPerSecond > 0) tickRate = ticksPerSecond;
}

int gameTickRate () {
	return tickRate;
}

int ticksForSeconds (double seconds) {
	return (int) (seconds * tickRate + 0.5);
}

double secondsForTicks (int ticks) {
	return (double) ticks / tickRate;
}
