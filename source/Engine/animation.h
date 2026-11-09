#ifndef ANIMATION_H
#define ANIMATION_H

/*
 * Animations: what an animation is (its definition, a clip of animmodel.h,
 * shared and never changed), the handles that variables, costumes,
 * characters and the mouse cursor own (personaAnimation), and the playback
 * of one (AnimationPlayback), which updateAnimation advances, a game tick at
 * a time, apart from drawing. Drawing only reads a playback, and never fires
 * its frame events.
 *
 * The stepper here plays LEGACY clips, from their frames: numFrames (),
 * frame () and sprites () are of those. A clip of another stepping has no
 * frames, so the stepper and drawing pass it by.
 *
 * Time is in game ticks: the frame time of the game (desiredfps, from its
 * header), at which the main loop runs. ticksForSeconds converts with that
 * rate, never with the time that frames take to draw.
 *
 * Ownership:
 *  - A handle shares its definition (shared_ptr<const>): copying an
 *    animation copies no frames.
 *  - A definition holds its sprite banks (SpriteBankRef, see sprbanks.h).
 *  - Each handle has one owner: a variable (raw pointer in the union of
 *    variable.h, freed by unlinkVar), a costume (unique_ptr), a character's
 *    ownAnim (unique_ptr) or the mouse cursor (unique_ptr).
 *  - A character's myAnim and a playback's `playing` don't own: myAnim
 *    points at a handle of the character's costume or at its ownAnim, and
 *    `playing` is only compared.
 */

#include <memory>
#include <vector>

#include "animmodel.h"
#include "ownedanimation.h"
#include "sprbanks.h"

// The definition of an animation is a clip (animmodel.h). Animations of
// anim () and of saved games are LEGACY clips, which play as below.
typedef ClipDefinition AnimationDefinition;
typedef ClipDefinitionRef AnimationDefinitionRef;

// A LEGACY clip of anim ()'s frames (makeLegacyClip).
AnimationDefinitionRef makeAnimation (const SpriteBankRef & bank, const std::vector<AnimationFrame> & frames);

// A handle on an animation, owned by one holder. Each handle is also an
// identity: a character starts its animation again when it is given another
// handle, even of the same definition, as SLUDGE has always done.
struct personaAnimation {
	AnimationDefinitionRef definition;
	LifetimeCounter<personaAnimation> counted;

	int numFrames () const { return definition ? (int) definition -> frames.size () : 0; }
	const AnimationFrame & frame (int i) const { return definition -> frames[i]; }
	SpriteBankRef sprites () const { return std::static_pointer_cast<loadedSpriteBank> (definition -> spriteBank); }
};

// Where sounds and script functions of frame events go: the engine starts
// them; tests record them.
class FrameEventSink {
public:
	virtual ~FrameEventSink () {}
	virtual void frameEvent (const FrameEvent & event) = 0;
};

const int subTicksPerTick = 256;
// The fastest a playback goes (setClipSpeed: 100000%).
const int maxPlaybackSpeed = 1000 * subTicksPerTick;

struct AnimationPlayback {
	const personaAnimation * playing;	// The handle it plays; compared only, never followed
	int frame;						// The frame now, after this tick's advance
	int ticksLeft;					// Ticks the frame still shows (frameTick)
	int displayFrame;				// The frame to draw this tick
	int subTicks;					// Fractions of a tick, of subTicksPerTick
	int speed;						// subTicksPerTick is normal speed, 0 pauses
	// TIMELINE clips: the position (see animmodel.h) now, after this tick's
	// advance, wrapped into the clip, and the position to draw this tick.
	AnimationTime position;
	AnimationTime displayPosition;

	AnimationPlayback () : playing (NULL), frame (0), ticksLeft (0), displayFrame (0), subTicks (0), speed (subTicksPerTick),
		position (0), displayPosition (0) {}
};

// A character's playback, for ticks game ticks. At normal speed and one
// tick, exactly what the engine always did each frame: when shown and given
// another handle, start it again (firing the event of its first frame);
// remember the frame to draw; count the frame's ticks down and move on,
// firing the event of the frame it enters while shown. A frame with an event
// is passed at once.
void updateAnimation (AnimationPlayback & playback, const personaAnimation * anim, int ticks, bool shown, FrameEventSink & events);

// TIMELINE clips: whether anim is one, and the events it fires as it plays.
bool isTimeline (const personaAnimation * anim);

struct TimelineEvent {
	AnimationTime position;
	EventId id;
	FrameEvent what;
};

// A TIMELINE clip's playback, for one game tick, shown or not: when given
// another handle, start it again (firing its events at 0); remember the
// position to draw; move on by the speed, adding the events it passes to
// out (see eventsCrossed). A clip that doesn't loop stops at its end. As
// with the legacy stepper, an update shows where the clip is, then moves it
// on, so a clip of clipFrames shows what its animation would.
void updateTimeline (AnimationPlayback & playback, const personaAnimation * anim, std::vector<TimelineEvent> & out);

// The time within its clip to draw of a playback of anim: its display
// position, or 0 until it plays anim. And the time now (for pasting).
AnimationTime shownTime (const AnimationPlayback & playback, const personaAnimation * anim);
AnimationTime currentTime (const AnimationPlayback & playback, const personaAnimation * anim);

// Whether a TIMELINE playback of anim has finished: a clip that doesn't
// loop, at its end.
bool timelineFinished (const AnimationPlayback & playback, const personaAnimation * anim);

// The sprite of a bank that r refers to, and its bank: NULL for NONE, for
// DEFAULT (only a rig's parts have one of their own), and for a sprite its
// bank lacks, which SLUDGE_ANIM_LOG reports once.
const struct sprite * resolveSprite (const SpriteRef * r, const struct spriteBank * & bank);

// Scripts build TIMELINE clips in place, until they are sealed (see
// animmodel.h): the definition to change, or NULL, with why.
ClipDefinition * editableClip (const personaAnimation * anim, std::string & why);
// The first use of a clip, by a character, costume or cursor, seals it.
void sealClip (const personaAnimation * anim);

// Start anim from its first frame without its events, as pasteCharacter does.
void restartAnimationQuietly (AnimationPlayback & playback, const personaAnimation * anim);

// The mouse cursor's rule, as it has always been: it remembers the frame to
// draw, then counts up (in ticksLeft) and moves on when the count reaches the
// frame's ticks, so a frame shows for at least one tick; no events. A
// TIMELINE clip plays as updateTimeline has it, without its events.
void updateCursorAnimation (AnimationPlayback & playback, const personaAnimation * anim, int ticks);

// clipFrames: the frames of a legacy animation as a SPRITE track of part in
// a TIMELINE clip, with their events, over the clip's length. It plays the
// animation with the legacy stepper (shown, from its start) and keys what
// that shows at each tick, so the clip shows and fires exactly what the
// animation would over its first length ticks: frames with an event take no
// time and aren't drawn; only the first of a chain of them fires; a frame
// of 0 ticks holds to the end of the clip; the animation loops as anim ()
// does, an event at the end of a pass at length.
// It replaces the keys of the part's SPRITE track (keeping its ID) and adds
// the events. Clips longer than maxLegacyFramesTicks fail.
const int maxLegacyFramesTicks = 1 << 20;
bool addLegacyFrames (ClipDefinition & clip, const std::string & part, const personaAnimation & anim, std::string & why);

// The game's tick rate, from its header.
void setGameTickRate (int ticksPerSecond);
int gameTickRate ();
int ticksForSeconds (double seconds);
double secondsForTicks (int ticks);

#endif
