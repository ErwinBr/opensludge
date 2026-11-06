#ifndef ANIMATION_H
#define ANIMATION_H

/*
 * Animations: what an animation is (AnimationDefinition, shared and never
 * changed), the handles that variables, costumes, characters and the mouse
 * cursor own (personaAnimation), and the playback of one (AnimationPlayback),
 * which updateAnimation advances, a game tick at a time, apart from drawing.
 * Drawing only reads a playback, and never fires its frame events.
 *
 * Time is in game ticks: the frame time of the game (desiredfps, from its
 * header), at which the main loop runs. ticksForSeconds converts with that
 * rate, never with the time that frames take to draw.
 *
 * Ownership:
 *  - A handle shares its definition (shared_ptr<const>): copying an
 *    animation copies no frames.
 *  - A definition holds its sprite bank (SpriteBankRef, see sprbanks.h).
 *  - Each handle has one owner: a variable (raw pointer in the union of
 *    variable.h, freed by unlinkVar), a costume (unique_ptr), a character's
 *    ownAnim (unique_ptr) or the mouse cursor (unique_ptr).
 *  - A character's myAnim and a playback's `playing` don't own: myAnim
 *    points at a handle of the character's costume or at its ownAnim, and
 *    `playing` is only compared.
 */

#include <memory>
#include <vector>

#include "sprbanks.h"

// What happens when an animation enters a frame (the noise of SLUDGE's
// animation frames: a sound file, or a script function).
struct FrameEvent {
	enum Kind { NONE, SOUND, FUNCTION };
	Kind kind;
	int number;		// The file, or the function

	FrameEvent () : kind (NONE), number (0) {}
	// From and to the noise of saved games and scripts: > 0 a sound file,
	// < 0 minus a function.
	static FrameEvent fromNoise (int noise);
	int toNoise () const;
};

struct AnimationFrame {
	int sprite;		// The sprite of the bank; negative for the mirror image
	int ticks;		// How many game ticks it shows (howMany); 0 holds it for good
	FrameEvent event;
};

struct AnimationDefinition {
	SpriteBankRef spriteBank;
	std::vector<AnimationFrame> frames;
};

typedef std::shared_ptr<const AnimationDefinition> AnimationDefinitionRef;

AnimationDefinitionRef makeAnimation (const SpriteBankRef & bank, const std::vector<AnimationFrame> & frames);

// A handle on an animation, owned by one holder. Each handle is also an
// identity: a character starts its animation again when it is given another
// handle, even of the same definition, as SLUDGE has always done.
struct personaAnimation {
	AnimationDefinitionRef definition;

	int numFrames () const { return definition ? (int) definition -> frames.size () : 0; }
	const AnimationFrame & frame (int i) const { return definition -> frames[i]; }
	const SpriteBankRef & sprites () const { return definition -> spriteBank; }
};

// Where sounds and script functions of frame events go: the engine starts
// them; tests record them.
class FrameEventSink {
public:
	virtual ~FrameEventSink () {}
	virtual void frameEvent (const FrameEvent & event) = 0;
};

const int subTicksPerTick = 256;

struct AnimationPlayback {
	const personaAnimation * playing;	// The handle it plays; compared only, never followed
	int frame;						// The frame now, after this tick's advance
	int ticksLeft;					// Ticks the frame still shows (frameTick)
	int displayFrame;				// The frame to draw this tick
	int subTicks;					// Fractions of a tick, of subTicksPerTick
	int speed;						// subTicksPerTick is normal speed, 0 pauses

	AnimationPlayback () : playing (NULL), frame (0), ticksLeft (0), displayFrame (0), subTicks (0), speed (subTicksPerTick) {}
};

// A character's playback, for ticks game ticks. At normal speed and one
// tick, exactly what the engine always did each frame: when shown and given
// another handle, start it again (firing the event of its first frame);
// remember the frame to draw; count the frame's ticks down and move on,
// firing the event of the frame it enters while shown. A frame with an event
// is passed at once.
void updateAnimation (AnimationPlayback & playback, const personaAnimation * anim, int ticks, bool shown, FrameEventSink & events);

// Start anim from its first frame without its events, as pasteCharacter does.
void restartAnimationQuietly (AnimationPlayback & playback, const personaAnimation * anim);

// The mouse cursor's rule, as it has always been: it remembers the frame to
// draw, then counts up (in ticksLeft) and moves on when the count reaches the
// frame's ticks, so a frame shows for at least one tick; no events.
void updateCursorAnimation (AnimationPlayback & playback, const personaAnimation * anim, int ticks);

// The game's tick rate, from its header.
void setGameTickRate (int ticksPerSecond);
int gameTickRate ();
int ticksForSeconds (double seconds);
double secondsForTicks (int ticks);

#endif
