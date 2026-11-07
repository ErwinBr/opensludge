#include <limits.h>
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

AnimationDefinitionRef makeAnimation (const SpriteBankRef & bank, const std::vector<AnimationFrame> & frames) {
	return makeLegacyClip (bank, frames);
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

namespace {

class EventList : public FrameEventSink {
public:
	std::vector<FrameEvent> events;
	void frameEvent (const FrameEvent & e) { events.push_back (e); }
};

}

bool addLegacyFrames (ClipDefinition & clip, const std::string & part, const personaAnimation & anim, std::string & why) {
	if (clip.stepping != STEPPING_TIMELINE) {
		why = "Frames can only be added to a clip of newClip ()";
		return false;
	}
	if (clip.length > maxLegacyFramesTicks) {
		why = "A clip with the frames of anim () can't be that long";
		return false;
	}
	if (! anim.numFrames ()) {
		why = "An animation without frames";
		return false;
	}

	// What the stepper shows at each time from 0 to length, and fires at each
	// time before length. Update k (from 1) shows time k - 1, and fires what
	// starts the animation (k = 1, time 0) and what its tick passes (time k).
	ClipDefinition built = clip;
	Track * track = NULL;
	for (size_t i = 0; i < built.tracks.size (); i ++) {
		if (built.tracks[i].part == part && built.tracks[i].channel == CHANNEL_SPRITE) track = & built.tracks[i];
	}
	if (track) track -> keys.clear ();

	AnimationPlayback p;
	EventList fired;
	int lastSprite = 0;
	for (int k = 1; k <= clip.length + 1; k ++) {
		fired.events.clear ();
		updateAnimation (p, & anim, 1, true, fired);
		int time = k - 1;
		int s = anim.frame (p.displayFrame).sprite;
		if (time == 0 || s != lastSprite) {
			Key key;
			key.time = time;
			key.easing = EASING_STEP;
			int bankFile = anim.sprites () ? anim.sprites () -> ID : -1;
			key.sprite = SpriteRef::sprite (anim.sprites (), bankFile, s >= 0 ? s : s == INT_MIN ? INT_MAX : - s, s < 0);
			if (! setKey (built, part, CHANNEL_SPRITE, key, why)) return false;
			lastSprite = s;
		}
		// The events of update k, at their times: the start's at 0, the tick's at k.
		for (size_t e = 0; e < fired.events.size (); e ++) {
			bool atStart = k == 1 && e == 0 && anim.frame (0).event.kind != FrameEvent::NONE;
			int at = atStart ? 0 : k;
			if (at < clip.length || (clip.length == 0 && at == 0)) {
				if (! addEvent (built, at, fired.events[e], why)) return false;
			}
		}
	}
	clip = built;
	return true;
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
