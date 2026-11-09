#ifndef ANIMMODEL_H
#define ANIMMODEL_H

/*
 * The animation model: what a character is made of (RigDefinition, the
 * parts and what is attached to what) and what changes over time
 * (ClipDefinition, its tracks and events). The two are apart: a track names
 * the part it drives, and is bound to a rig's part of that name only when a
 * character plays the clip, so one clip can play on any rig.
 *
 * Definitions are data only, shared (shared_ptr<const>) and never changed
 * once shared. Playing them is in animation.h; their file format in
 * animformat.h.
 *
 * Coordinates: +X is right and +Y down, in pixels. Angles are in degrees,
 * clockwise. A part's origin, and the point it turns, scales and flips
 * about, is its sprite's hotspot moved by the part's pivot (pivotX, pivotY,
 * in the sprite's pixels; 0, 0 is the hotspot). A part's x, y is where its origin sits,
 * from its parent's origin, in its parent's space (turned, scaled and
 * flipped as the parent is, in the parent's sprite pixels). Part 0 is the
 * root: the character itself, named "".
 *
 * A track is one channel of one part (its x, its sprite, ...), with keys at
 * times in game ticks. A clip has at most one track per part and channel.
 *
 * Clips are stepped one of two ways:
 *  - LEGACY: animations of anim() and of saved games. They play through the
 *    legacy stepper (updateAnimation), exactly as SLUDGE always has, from
 *    `frames`. Their tracks are a view of the same frames: one SPRITE track
 *    on the root, with a key where each frame starts, and their events,
 *    keyed by frame.
 *  - TIMELINE: clips with tracks and events at times, of length ticks. Time
 *    runs over [0, length); keys lie in [0, length], so that a key at length
 *    closes a loop or ends a clip that doesn't loop. Events lie in
 *    [0, length] too: those at 0 fire as the clip starts, and again each
 *    time a loop goes round; those at length fire as each pass ends (as a
 *    clip that doesn't loop finishes), before the next pass's at 0.
 *
 * Parts, tracks and events have IDs that are unique within their
 * definition and never used again, so that an editor can refer to them
 * whatever is added, removed or reordered. The order of the vectors means
 * nothing else.
 */

#include <stdint.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "animbank.h"

// Counts how many objects of a kind are alive, as a member of them, for the
// lifetime log (SLUDGE_LIFETIME_LOG, see logLifetimes in people.h).
template <class Tag> class LifetimeCounter {
public:
	LifetimeCounter () { ++ live (); }
	LifetimeCounter (const LifetimeCounter &) { ++ live (); }
	LifetimeCounter & operator= (const LifetimeCounter &) { return * this; }
	~LifetimeCounter () { -- live (); }
	static int & live () { static int n = 0; return n; }
};

// What happens at a point of an animation: a sound file, or a script
// function (the noise of SLUDGE's animation frames).
struct FrameEvent {
	enum Kind { NONE, SOUND, FUNCTION };
	Kind kind;
	int number;		// The file, or the function

	FrameEvent () : kind (NONE), number (0) {}
	// From and to the noise of saved games and scripts: > 0 a sound file,
	// < 0 minus a function.
	static FrameEvent fromNoise (int noise);
	int toNoise () const;
	bool operator== (const FrameEvent & o) const { return kind == o.kind && number == o.number; }
};

// A frame of a legacy animation, as anim () makes it.
struct AnimationFrame {
	int sprite;		// The sprite of the bank; negative for the mirror image
	int ticks;		// How many game ticks it shows (howMany); 0 holds it for good
	FrameEvent event;
};

typedef uint32_t PartId;
typedef uint32_t TrackId;
typedef uint32_t EventId;

// What a track changes. X, Y, ROTATION and SCALE are continuous, and ease
// between keys; the others are discrete, and step.
enum Channel : int {
	CHANNEL_X,			// Pixels
	CHANNEL_Y,			// Pixels
	CHANNEL_ROTATION,	// Degrees, clockwise
	CHANNEL_SCALE,		// A factor: 1 is the sprite's size
	CHANNEL_SPRITE,		// Keys hold a SpriteRef
	CHANNEL_VISIBLE,	// 0 hides the part and what is attached to it
	CHANNEL_DEPTH,		// Drawing order within the character
	CHANNEL_FLIP,		// 1 mirrors the part and what is attached to it
	NUM_CHANNELS
};

bool isDiscrete (Channel c);

enum Easing : int {
	EASING_STEP,		// The value of the key, until the next
	EASING_LINEAR,		// Straight towards the next key's value
	EASING_INOUT,		// Smoothstep towards it
	NUM_EASINGS
};

// The sprite of a key or part. DEFAULT is the part's own sprite; NONE is
// deliberately no sprite (an empty frame), which isn't the same as a sprite
// that its bank lacks.
struct SpriteRef {
	enum Kind { DEFAULT, NONE, SPRITE };
	Kind kind;
	int bankFile;			// The bank's file number, for saving; -1 for none
	AnimBankRef bank;		// The bank, once loaded (resolveBanks)
	int number;				// The sprite of the bank
	bool mirror;			// The sprite's mirror image (only this sprite)

	SpriteRef () : kind (DEFAULT), bankFile (-1), number (0), mirror (false) {}
	static SpriteRef none ();
	static SpriteRef sprite (const AnimBankRef & bank, int bankFile, int number, bool mirror);
	bool operator== (const SpriteRef & o) const;
};

// The way a part is placed and shown, relative to its parent.
struct PartPose {
	float x, y;
	float rotation;		// Degrees, clockwise
	float scale;		// Uniform; inherited by what is attached
	bool flip;
	bool visible;

	PartPose () : x (0), y (0), rotation (0), scale (1), flip (false), visible (true) {}
};

struct RigPart {
	PartId id;
	std::string name;
	int parent;			// Index of the parent part; -1 for the root only
	PartPose rest;
	SpriteRef sprite;	// DEFAULT here means none: a joint
	int depth;			// Drawing order; parts of the same depth in rig order
	// The point it turns, scales and flips about, and that x, y place: its
	// sprite's hotspot, moved by this many of the sprite's pixels.
	float pivotX, pivotY;

	RigPart () : id (0), parent (-1), depth (0), pivotX (0), pivotY (0) {}
};

struct RigDefinition {
	std::vector<RigPart> parts;		// The root first; parents before children
	PartId nextPartId;
	// Not part of the definition: whether a script may still change it, as
	// for clips (ClipDefinition::sealed): setCharacterRig seals it.
	mutable bool sealed;
	LifetimeCounter<RigDefinition> counted;

	// A rig of the root only.
	RigDefinition ();

	// The index of the part of that name, or -1.
	int findPart (const std::string & name) const;
	const RigPart * part (const std::string & name) const;
};

typedef std::shared_ptr<const RigDefinition> RigDefinitionRef;

struct Key {
	int time;			// Game ticks from the start of the clip
	float value;		// For all channels but SPRITE
	Easing easing;		// Towards the next key; STEP for discrete channels
	SpriteRef sprite;	// For SPRITE

	Key () : time (0), value (0), easing (EASING_STEP) {}
};

struct Track {
	TrackId id;
	std::string part;	// The name of the part it drives
	Channel channel;
	std::vector<Key> keys;	// In time order

	Track () : id (0), channel (CHANNEL_X) {}
};

struct ClipEvent {
	EventId id;
	int time;			// Game ticks from the start of the clip
	int frame;			// LEGACY: the frame that fires it; -1 in TIMELINE
	FrameEvent what;

	ClipEvent () : id (0), time (0), frame (-1) {}
};

enum ClipStepping { STEPPING_LEGACY, STEPPING_TIMELINE };

struct ClipDefinition {
	ClipStepping stepping;
	int length;			// In ticks
	bool loop;
	std::vector<Track> tracks;
	std::vector<ClipEvent> events;
	TrackId nextTrackId;
	EventId nextEventId;

	// LEGACY only: what the legacy stepper plays, and the tracks show.
	AnimBankRef spriteBank;
	std::vector<AnimationFrame> frames;

	// Not part of the definition: whether a script may still change it.
	// Scripts build a clip in place (newClip, then clipSprite and so on) on
	// a definition that the copies of the clip's variables share; the first
	// use of the clip (by a character, costume or cursor) seals it, so that
	// nothing ever changes under what plays it.
	mutable bool sealed;

	LifetimeCounter<ClipDefinition> counted;

	// An empty TIMELINE clip.
	ClipDefinition ();

	const Track * track (const std::string & part, Channel channel) const;
};

typedef std::shared_ptr<const ClipDefinition> ClipDefinitionRef;

// A LEGACY clip of anim ()'s frames, of the bank of file bankFile (-1 for
// none).
ClipDefinitionRef makeLegacyClip (const AnimBankRef & bank, int bankFile, const std::vector<AnimationFrame> & frames);

// Building definitions, for scripts, the decoder and tests. On failure they
// change nothing and return false, with why.

// Add a part of a unique name under the part named parent.
bool addPart (RigDefinition & rig, const std::string & name, const std::string & parent,
	const PartPose & rest, const SpriteRef & sprite, std::string & why);

// Add or replace (at the same time) a key of the part's track of channel,
// making the track if there is none. For TIMELINE clips.
bool setKey (ClipDefinition & clip, const std::string & part, Channel channel, const Key & key, std::string & why);

// Add an event at time. For TIMELINE clips.
bool addEvent (ClipDefinition & clip, int time, const FrameEvent & what, std::string & why);

// Every rule of the comments above, as the decoder checks them.
bool validRig (const RigDefinition & rig, std::string & why);
bool validClip (const ClipDefinition & clip, std::string & why);

// Fill in the banks of every SpriteRef from its file number. load gives the
// bank of a file, or NULL; then this returns false.
typedef std::function<AnimBankRef (int bankFile)> BankLoader;

// Reports of the animation model, as a track for a part that a rig lacks,
// or a sprite that its bank lacks. With SLUDGE_ANIM_LOG set, they go to
// stderr; a handler (for tests) takes them instead, NULL for the default.
void animDiagnostic (const std::string & what);
typedef void (* AnimDiagnosticHandler) (const std::string & what);
void setAnimDiagnosticHandler (AnimDiagnosticHandler handler);
bool resolveBanks (RigDefinition & rig, const BankLoader & load);
bool resolveBanks (ClipDefinition & clip, const BankLoader & load);

/*
 * Evaluating a TIMELINE clip. Pure functions: no state, no events fired,
 * no rig. The player uses them, and so will the editor to scrub.
 *
 * AnimationTime counts sub-ticks: timeSubTicks to a game tick, so that time
 * moves at any speed without drifting. A playback's position is the time
 * since the clip started, unwrapped: it grows past length as a loop goes
 * round. clipTime turns it into the time within the clip.
 */

typedef int64_t AnimationTime;
const int timeSubTicks = 256;

inline AnimationTime ticksToTime (int ticks) { return (AnimationTime) ticks * timeSubTicks; }

// The time within the clip of a position: a loop wraps over [0, length);
// any other clip stops at length, where it is finished. A loop of length 0
// doesn't go round: it is a clip that holds its first pose.
AnimationTime clipTime (const ClipDefinition & clip, AnimationTime position);
bool clipLoops (const ClipDefinition & clip);
bool clipFinished (const ClipDefinition & clip, AnimationTime position);

// The value of one track at a time. track, and sprite, point into the clip,
// and are valid while it is.
struct TrackSample {
	const Track * track;
	float value;				// For all channels but SPRITE
	const SpriteRef * sprite;	// For SPRITE
};

typedef std::vector<TrackSample> TrackSamples;

// The value of every track with keys at time (within the clip, see
// clipTime), in the order of the tracks. Before its first key a track has
// the first key's value, and after its last key the last's. Between keys,
// the easing of the earlier key: STEP holds its value; LINEAR goes
// straight to the next key's; INOUT eases in and out (smoothstep).
// Discrete channels always step.
void evaluateClip (const ClipDefinition & clip, AnimationTime time, TrackSamples & out);

// The sprite of one track at time, as evaluateClip has it: NULL when the
// clip has no SPRITE track of that part, or it has no keys.
const SpriteRef * sampleSprite (const ClipDefinition & clip, const std::string & part, AnimationTime time);

// An event that a playback passed, at its position.
struct CrossedEvent {
	AnimationTime position;
	const ClipEvent * event;	// Into the clip
};

// The events that fire when a clip starts: those at 0.
void eventsAtStart (const ClipDefinition & clip, std::vector<CrossedEvent> & out);

// The events that fire as a playback moves from position from to position
// to (from <= to), however far: each event at a position p (its time, plus
// a whole number of lengths in a loop) with from < p <= to, in order of
// position (where a pass ends and the next starts, the end's first), then
// of ID. So moving in steps fires exactly what one move fires, and a loop
// fires each event once a pass: as it wraps, those at length, then those
// at 0. A clip that doesn't loop fires none past its length. Appends to out.
void eventsCrossed (const ClipDefinition & clip, AnimationTime from, AnimationTime to, std::vector<CrossedEvent> & out);

#endif
