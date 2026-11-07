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
 * about, is its sprite's hotspot. A part's x, y is where its origin sits,
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
 *    closes a loop or ends a clip that doesn't loop; events lie in
 *    [0, length), or at 0 when length is 0.
 *
 * Parts, tracks and events have IDs that are unique within their
 * definition and never used again, so that an editor can refer to them
 * whatever is added, removed or reordered. The order of the vectors means
 * nothing else.
 */

#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

#include "sprbanks.h"

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
	SpriteBankRef bank;		// The bank, once loaded (resolveBanks)
	int number;				// The sprite of the bank
	bool mirror;			// The sprite's mirror image (only this sprite)

	SpriteRef () : kind (DEFAULT), bankFile (-1), number (0), mirror (false) {}
	static SpriteRef none ();
	static SpriteRef sprite (const SpriteBankRef & bank, int bankFile, int number, bool mirror);
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

	RigPart () : id (0), parent (-1), depth (0) {}
};

struct RigDefinition {
	std::vector<RigPart> parts;		// The root first; parents before children
	PartId nextPartId;
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
	SpriteBankRef spriteBank;
	std::vector<AnimationFrame> frames;

	LifetimeCounter<ClipDefinition> counted;

	// An empty TIMELINE clip.
	ClipDefinition ();

	const Track * track (const std::string & part, Channel channel) const;
};

typedef std::shared_ptr<const ClipDefinition> ClipDefinitionRef;

// A LEGACY clip of anim ()'s frames.
ClipDefinitionRef makeLegacyClip (const SpriteBankRef & bank, const std::vector<AnimationFrame> & frames);

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
typedef SpriteBankRef (* BankLoader) (int bankFile);
bool resolveBanks (RigDefinition & rig, BankLoader load);
bool resolveBanks (ClipDefinition & clip, BankLoader load);

#endif
