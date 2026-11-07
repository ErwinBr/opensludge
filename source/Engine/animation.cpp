#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <set>
#include <utility>

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
	p.position = 0;
	p.displayPosition = 0;
}

bool isTimeline (const personaAnimation * anim) {
	return anim && anim -> definition && anim -> definition -> stepping == STEPPING_TIMELINE;
}

void updateTimeline (AnimationPlayback & p, const personaAnimation * anim, std::vector<TimelineEvent> & out) {
	if (! isTimeline (anim)) return;
	const ClipDefinition & clip = * anim -> definition;
	std::vector<CrossedEvent> crossed;
	if (anim != p.playing) {
		p.playing = anim;
		p.position = 0;
		eventsAtStart (clip, crossed);
	}
	p.displayPosition = p.position;
	AnimationTime from = p.position;
	AnimationTime to = from + (p.speed > 0 ? p.speed : 0);
	eventsCrossed (clip, from, to, crossed);
	p.position = clipTime (clip, to);
	for (size_t i = 0; i < crossed.size (); i ++) {
		TimelineEvent e = { crossed[i].position, crossed[i].event -> id, crossed[i].event -> what };
		out.push_back (e);
	}
}

AnimationTime shownTime (const AnimationPlayback & p, const personaAnimation * anim) {
	return anim && p.playing == anim ? p.displayPosition : 0;
}

AnimationTime currentTime (const AnimationPlayback & p, const personaAnimation * anim) {
	return anim && p.playing == anim ? p.position : 0;
}

bool timelineFinished (const AnimationPlayback & p, const personaAnimation * anim) {
	if (! isTimeline (anim)) return false;
	return p.playing == anim && ! clipLoops (* anim -> definition) && p.position >= ticksToTime (anim -> definition -> length);
}

// The missing sprites that resolveSprite has reported. Not a static of the
// function: one that needs constructing gets a thread-safe guard, which
// MinGW's libstdc++ makes of condition variables, which Windows XP lacks.
static std::set<std::pair<int, int> > reportedMissingSprites;

const sprite * resolveSprite (const SpriteRef * r, const spriteBank * & bank) {
	bank = NULL;
	if (! r || r -> kind != SpriteRef::SPRITE || ! r -> bank) return NULL;
	const spriteBank & b = r -> bank -> bank;
	if (r -> number < 0 || r -> number >= b.total) {
		static int log = -1;
		if (log < 0) log = getenv ("SLUDGE_ANIM_LOG") != NULL;
		if (log && reportedMissingSprites.insert (std::make_pair (r -> bankFile, r -> number)).second) {
			fprintf (stderr, "Animation: sprite %d of bank %d doesn't exist; drawing nothing\n", r -> number, r -> bankFile);
		}
		return NULL;
	}
	bank = & b;
	return & b.sprites[r -> number];
}

ClipDefinition * editableClip (const personaAnimation * anim, std::string & why) {
	if (! isTimeline (anim)) {
		why = "Only clips of newClip () can be changed";
		return NULL;
	}
	if (anim -> definition -> sealed) {
		why = "This clip is already used by a character, costume or cursor, so it can't change; make another with newClip ()";
		return NULL;
	}
	return const_cast<ClipDefinition *> (anim -> definition.get ());
}

void sealClip (const personaAnimation * anim) {
	if (anim && anim -> definition) anim -> definition -> sealed = true;
}

void updateCursorAnimation (AnimationPlayback & p, const personaAnimation * anim, int ticks) {
	// A TIMELINE clip plays as for characters, without its events.
	if (isTimeline (anim)) {
		std::vector<TimelineEvent> ignored;
		while (ticks -- > 0) updateTimeline (p, anim, ignored);
		return;
	}
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
		why = "clipFrames needs an animation of anim (), with frames";
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
	std::vector<FrameEvent> startEvents;
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
		// As a loop wraps, it fires its events at 0 again: what the update at
		// its length fires is only at length when it isn't those.
		size_t first = 0;
		if (k == 1 && anim.frame (0).event.kind != FrameEvent::NONE && ! fired.events.empty ()) {
			startEvents.push_back (fired.events[0]);
			if (! addEvent (built, 0, fired.events[0], why)) return false;
			first = 1;
		}
		std::vector<FrameEvent> ticked (fired.events.begin () + first, fired.events.end ());
		if (clip.loop && k == clip.length && ticked == startEvents) continue;
		for (size_t e = 0; e < ticked.size () && k <= clip.length; e ++) {
			if (! addEvent (built, k, ticked[e], why)) return false;
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
