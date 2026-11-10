#ifndef SLUDGE_DEVKIT_CLIP_DOCUMENT_H
#define SLUDGE_DEVKIT_CLIP_DOCUMENT_H

/*
 * A clip (.clp): an animation of the parts of a rig, as the animation
 * editor edits it. The clip is the very ClipDefinition that the engine
 * plays (source/Anim/animmodel.h), in a project asset (animformat.h): with
 * an identity, the paths of its sprite banks, the identity of the rig it is
 * made for, and the sound or function of each event, by path or name.
 *
 * A clip names the parts it moves; it holds nothing of its rig but its
 * identity, so it plays on any rig with parts of those names.
 *
 * Every change is a command that undoes, and the steps of a drag merge into
 * one. Keys and events lie from tick 0 to the clip's length:
 *  - a key moved or copied onto a tick where its track has a key replaces
 *    that key (undo brings it back); it keeps its value and easing;
 *  - the clip doesn't get shorter than its keys and events, unless the
 *    caller says to remove those past its new end;
 *  - keying a part (autoKey, as a drag on the canvas does) makes its track
 *    if it has none, and a key at the tick, or changes the key there.
 * The functions that can fail change nothing then, and say why.
 */

#include <stdint.h>

#include <string>
#include <vector>

#include "Document.h"
#include "animformat.h"

namespace devkit {

// A key of a track: its track and its tick.
struct KeyRef {
	TrackId track;
	int time;
	KeyRef () : track (0), time (0) {}
	KeyRef (TrackId t, int at) : track (t), time (at) {}
	bool operator== (const KeyRef & o) const { return track == o.track && time == o.time; }
	bool operator< (const KeyRef & o) const { return track < o.track || (track == o.track && time < o.time); }
};

// A key or an event, as copied from a clip to be pasted: with the paths and
// names of what it refers to, so that it pastes into any clip.
struct ClipItem {
	bool isEvent;
	int time;
	// A key: of a channel of a part, with its value and easing, or for a
	// sprite its kind, and the path of its bank, its number and mirror.
	std::string part;
	Channel channel;
	float value;
	Easing easing;
	SpriteRef::Kind sprite;
	std::string bank;
	int number;
	bool mirror;
	// An event: a sound (its path) or a function (its name).
	FrameEvent::Kind event;
	std::string name;

	ClipItem () : isEvent (false), time (0), channel (CHANNEL_X), value (0), easing (EASING_LINEAR),
		sprite (SpriteRef::DEFAULT), number (0), mirror (false), event (FrameEvent::NONE) {}
};

class ClipDocument : public Document {
public:
	static DocumentType type ();
	// A new clip of 48 ticks that loops, of no rig, with a new identity.
	ClipDocument ();

	const char * kind () const { return "Clip"; }
	std::string untitledName () const { return "Untitled Clip.clp"; }

	const animformat::Asset & asset () const { return state; }
	const ClipDefinition & clip () const { return state.clip; }
	const Track * track (TrackId id) const;
	const Track * track (const std::string & part, Channel channel) const;
	// The key of a track at a tick, or NULL.
	const Key * key (const KeyRef & key) const;
	// The path of the bank of a sprite, or empty.
	std::string bankOf (const SpriteRef & sprite) const;
	// The sound's path or the function's name of an event, or empty.
	std::string eventName (EventId id) const;
	const ClipEvent * event (EventId id) const;

	// The rig it is made for: its identity and path, or none.
	void bindToRig (const animformat::AssetId & rig, const std::string & path);
	bool isBoundTo (const animformat::AssetId & rig) const { return ! rig.isNull () && state.rigId == rig; }
	// The parts that its tracks name and rig hasn't.
	std::vector<std::string> missingParts (const RigDefinition & rig) const;

	void setLoop (bool loop);
	// How many keys and events lie after a length.
	int itemsPast (int length) const;
	// A length of 0 or more. When it would leave keys or events past its
	// end, it fails, unless removePast says to remove them.
	bool setLength (int length, bool removePast, std::string & why);

	bool addTrack (const std::string & part, Channel channel, TrackId * added, std::string & why);
	void removeTrack (TrackId id);
	// Set or replace the key of a track at a tick: a value of a continuous or
	// discrete channel (the model's units: a factor for SCALE, 0 or 1 for
	// VISIBLE and FLIP), with an easing towards the next key (always STEP
	// for discrete channels).
	bool setKey (TrackId id, int time, float value, Easing easing, std::string & why, int mergeKey = 0);
	// The key of a SPRITE track: a sprite of the bank at path, or with an
	// empty path NONE (an empty frame) or, with useDefault, the part's own.
	bool setSpriteKey (TrackId id, int time, const std::string & bank, int number, bool mirror, bool useDefault, std::string & why);
	void setEasing (const KeyRef & key, Easing easing);
	void removeKeys (const std::vector<KeyRef> & keys);
	// Move, or copy, keys by ticks: false if one would leave the clip.
	bool moveKeys (const std::vector<KeyRef> & keys, int by, bool copy, std::string & why, int mergeKey = 0);
	// Key a channel of a part at a tick, as a drag on the canvas does.
	bool autoKey (const std::string & part, Channel channel, int time, float value, std::string & why, int mergeKey = 0);

	// The selected keys and events, to copy.
	std::vector<ClipItem> copy (const std::vector<KeyRef> & keys, const std::vector<EventId> & events) const;
	// Paste them as one change, the first of them at tick at and the others
	// as far from it as they were: onto the part onto, if it isn't NULL, or
	// their own parts. A key on a tick of a key replaces it; the clip grows
	// to hold what lands past its end. pastedKeys and pastedEvents get what
	// was pasted.
	bool paste (const std::vector<ClipItem> & items, int at, const std::string * onto, std::vector<KeyRef> * pastedKeys,
		std::vector<EventId> * pastedEvents, std::string & why);

	// An event that plays a sound (its path) or calls a function (its name).
	bool addEvent (int time, FrameEvent::Kind kind, const std::string & name, EventId * added, std::string & why);
	bool setEvent (EventId id, int time, FrameEvent::Kind kind, const std::string & name, std::string & why, int mergeKey = 0);
	void removeEvent (EventId id);

	// The tracks of a part are of another name now, as when its rig renames
	// it: false if the clip has tracks of both names for a channel. With
	// serial, the serial of the command (see CommandStack::undoIfLast), or 0
	// when there were none of that name.
	bool renamePart (const std::string & from, const std::string & to, uint64_t * serial, std::string & why);

	// How the editor shows a part (by name), saved with the clip and undone
	// like any change: locked, it can't be selected or keyed on the canvas,
	// nor its keys moved on the timeline. Its scaling is that of its rig's
	// part until the clip sets its own.
	bool isLocked (const std::string & part) const { return state.editorPart (part).locked; }
	animformat::EditorPart::Scaling scaling (const std::string & part) const { return state.editorPart (part).scaling; }
	void setLocked (const std::string & part, bool locked);
	void setScaling (const std::string & part, animformat::EditorPart::Scaling scaling);

protected:
	bool read (const std::string & path, std::string * error);
	bool write (const std::string & path, std::string * error);

private:
	void change (const std::string & label, const animformat::Asset & after, int mergeKey = 0);
	bool check (const animformat::Asset & after, std::string & why) const;

	animformat::Asset state;
};

// The files of an extension ("rig", without the dot) in a folder and the
// folders in it, in order of their paths.
std::vector<std::string> filesWithExtension (const std::string & folder, const std::string & extension);
// Of the .clp files in a folder and the folders in it, those made for a rig.
std::vector<std::string> clipsOfRig (const std::string & folder, const animformat::AssetId & rig);

} // namespace devkit

#endif
