#include "animmodel.h"

#include <limits.h>
#include <math.h>
#include <set>
#include <utility>

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

bool isDiscrete (Channel c) {
	return c == CHANNEL_SPRITE || c == CHANNEL_VISIBLE || c == CHANNEL_DEPTH || c == CHANNEL_FLIP;
}

SpriteRef SpriteRef::none () {
	SpriteRef r;
	r.kind = NONE;
	return r;
}

SpriteRef SpriteRef::sprite (const SpriteBankRef & bank, int bankFile, int number, bool mirror) {
	SpriteRef r;
	r.kind = SPRITE;
	r.bank = bank;
	r.bankFile = bankFile;
	r.number = number;
	r.mirror = mirror;
	return r;
}

bool SpriteRef::operator== (const SpriteRef & o) const {
	if (kind != o.kind) return false;
	if (kind != SPRITE) return true;
	return bankFile == o.bankFile && number == o.number && mirror == o.mirror;
}

RigDefinition::RigDefinition () : nextPartId (1) {
	parts.resize (1);
	parts[0].id = 0;
	parts[0].parent = -1;
}

int RigDefinition::findPart (const std::string & name) const {
	for (size_t i = 0; i < parts.size (); i ++) {
		if (parts[i].name == name) return (int) i;
	}
	return -1;
}

const RigPart * RigDefinition::part (const std::string & name) const {
	int i = findPart (name);
	return i < 0 ? NULL : & parts[i];
}

ClipDefinition::ClipDefinition () : stepping (STEPPING_TIMELINE), length (0), loop (false), nextTrackId (1), nextEventId (1) {}

const Track * ClipDefinition::track (const std::string & part, Channel channel) const {
	for (size_t i = 0; i < tracks.size (); i ++) {
		if (tracks[i].channel == channel && tracks[i].part == part) return & tracks[i];
	}
	return NULL;
}

ClipDefinitionRef makeLegacyClip (const SpriteBankRef & bank, const std::vector<AnimationFrame> & frames) {
	std::shared_ptr<ClipDefinition> d = std::make_shared<ClipDefinition> ();
	d -> stepping = STEPPING_LEGACY;
	d -> loop = true;
	d -> spriteBank = bank;
	d -> frames = frames;

	// The view: a key where each frame starts, as long as the frames that
	// count down last (a frame of 0 ticks, or less, holds for good, and so
	// ends the view), and the events of the frames.
	Track sprites;
	sprites.id = d -> nextTrackId ++;
	sprites.part = "";
	sprites.channel = CHANNEL_SPRITE;
	int bankFile = bank ? bank -> ID : -1;
	long long time = 0;
	for (size_t i = 0; i < frames.size (); i ++) {
		int s = frames[i].sprite;
		Key k;
		k.time = (int) time;
		k.easing = EASING_STEP;
		k.sprite = SpriteRef::sprite (bank, bankFile, s >= 0 ? s : s == INT_MIN ? INT_MAX : - s, s < 0);
		sprites.keys.push_back (k);
		if (frames[i].event.kind != FrameEvent::NONE) {
			ClipEvent e;
			e.id = d -> nextEventId ++;
			e.time = (int) time;
			e.frame = (int) i;
			e.what = frames[i].event;
			d -> events.push_back (e);
		}
		if (frames[i].ticks > 0) time += frames[i].ticks;
		if (time > INT_MAX) time = INT_MAX;
	}
	d -> tracks.push_back (sprites);
	d -> length = (int) time;
	return d;
}

namespace {

bool fail (std::string & why, const std::string & what) {
	why = what;
	return false;
}

bool finite (float v) {
	return isfinite (v) != 0;
}

// Names fit the file format: up to 65535 bytes, with no 0 byte.
bool validName (const std::string & n) {
	return n.size () <= 65535 && n.find ('\0') == std::string::npos;
}

bool validPose (const PartPose & p) {
	return finite (p.x) && finite (p.y) && finite (p.rotation) && finite (p.scale);
}

bool validSprite (const SpriteRef & s) {
	switch (s.kind) {
		case SpriteRef::DEFAULT:
		case SpriteRef::NONE:
			return true;
		case SpriteRef::SPRITE:
			return s.number >= 0 && s.bankFile >= -1;
	}
	return false;
}

}

bool addPart (RigDefinition & rig, const std::string & name, const std::string & parent,
	const PartPose & rest, const SpriteRef & sprite, std::string & why) {
	if (name.empty () || ! validName (name)) return fail (why, "A part needs a name");
	if (rig.findPart (name) >= 0) return fail (why, "There is already a part called \"" + name + "\"");
	int p = rig.findPart (parent);
	if (p < 0) return fail (why, "There is no part called \"" + parent + "\" to attach \"" + name + "\" to");
	if (! validPose (rest)) return fail (why, "The position of a part must be a number");
	if (sprite.kind == SpriteRef::NONE || ! validSprite (sprite)) return fail (why, "Not a sprite for a part");
	RigPart part;
	part.id = rig.nextPartId ++;
	part.name = name;
	part.parent = p;
	part.rest = rest;
	part.sprite = sprite;
	rig.parts.push_back (part);
	return true;
}

bool setKey (ClipDefinition & clip, const std::string & part, Channel channel, const Key & key, std::string & why) {
	if (clip.stepping != STEPPING_TIMELINE) return fail (why, "Keys can't be added to an animation of anim ()");
	if (! validName (part)) return fail (why, "Not the name of a part");
	if (channel < 0 || channel >= NUM_CHANNELS) return fail (why, "Not a channel");
	if (key.time < 0 || key.time > clip.length) return fail (why, "A key must lie within the clip");
	if (key.easing < 0 || key.easing >= NUM_EASINGS) return fail (why, "Not an easing");
	if (isDiscrete (channel) && key.easing != EASING_STEP) return fail (why, "Sprites, visibility, depth and flipping can only step");
	if (channel == CHANNEL_SPRITE ? ! validSprite (key.sprite) : ! finite (key.value)) return fail (why, "Not a value for a key");

	Track * t = NULL;
	for (size_t i = 0; i < clip.tracks.size (); i ++) {
		if (clip.tracks[i].channel == channel && clip.tracks[i].part == part) t = & clip.tracks[i];
	}
	if (! t) {
		clip.tracks.push_back (Track ());
		t = & clip.tracks.back ();
		t -> id = clip.nextTrackId ++;
		t -> part = part;
		t -> channel = channel;
	}
	Key k = key;
	if (channel != CHANNEL_SPRITE) k.sprite = SpriteRef ();
	else k.value = 0;
	std::vector<Key>::iterator i = t -> keys.begin ();
	while (i != t -> keys.end () && i -> time < k.time) ++ i;
	if (i != t -> keys.end () && i -> time == k.time) * i = k;
	else t -> keys.insert (i, k);
	return true;
}

bool addEvent (ClipDefinition & clip, int time, const FrameEvent & what, std::string & why) {
	if (clip.stepping != STEPPING_TIMELINE) return fail (why, "Events can't be added to an animation of anim ()");
	if (what.kind == FrameEvent::NONE) return fail (why, "An event must be a sound or a function");
	if (time < 0 || (clip.length > 0 ? time >= clip.length : time != 0)) return fail (why, "An event must lie within the clip, before its end");
	ClipEvent e;
	e.id = clip.nextEventId ++;
	e.time = time;
	e.frame = -1;
	e.what = what;
	clip.events.push_back (e);
	return true;
}

bool validRig (const RigDefinition & rig, std::string & why) {
	if (rig.parts.empty ()) return fail (why, "A rig has a root");
	std::set<PartId> ids;
	std::set<std::string> names;
	for (size_t i = 0; i < rig.parts.size (); i ++) {
		const RigPart & p = rig.parts[i];
		if (i == 0) {
			if (! p.name.empty () || p.parent != -1) return fail (why, "The first part must be the root");
		} else {
			if (p.name.empty () || ! validName (p.name)) return fail (why, "A part needs a name");
			if (p.parent < 0 || p.parent >= (int) i) return fail (why, "A part must come after its parent");
		}
		if (p.id >= rig.nextPartId || ! ids.insert (p.id).second) return fail (why, "The IDs of parts must be unique");
		if (! names.insert (p.name).second) return fail (why, "The names of parts must be unique");
		if (! validPose (p.rest)) return fail (why, "The position of a part must be a number");
		if (p.sprite.kind == SpriteRef::NONE || ! validSprite (p.sprite)) return fail (why, "Not a sprite for a part");
	}
	return true;
}

bool validClip (const ClipDefinition & clip, std::string & why) {
	if (clip.length < 0) return fail (why, "A clip can't be shorter than 0");
	std::set<TrackId> trackIds;
	std::set<std::pair<std::string, int> > channels;
	for (size_t i = 0; i < clip.tracks.size (); i ++) {
		const Track & t = clip.tracks[i];
		if (t.id >= clip.nextTrackId || ! trackIds.insert (t.id).second) return fail (why, "The IDs of tracks must be unique");
		if (t.channel < 0 || t.channel >= NUM_CHANNELS) return fail (why, "Not a channel");
		if (! validName (t.part)) return fail (why, "Not the name of a part");
		if (! channels.insert (std::make_pair (t.part, (int) t.channel)).second) return fail (why, "A clip has one track per part and channel");
		for (size_t k = 0; k < t.keys.size (); k ++) {
			const Key & key = t.keys[k];
			if (key.time < 0 || key.time > clip.length) return fail (why, "A key must lie within the clip");
			// In the view of a legacy animation, frames that take no time
			// start at the same time.
			if (k && (clip.stepping == STEPPING_LEGACY ? key.time < t.keys[k - 1].time : key.time <= t.keys[k - 1].time))
				return fail (why, "Keys must be in time order");
			if (key.easing < 0 || key.easing >= NUM_EASINGS) return fail (why, "Not an easing");
			if (isDiscrete (t.channel) && key.easing != EASING_STEP) return fail (why, "Discrete channels can only step");
			if (t.channel == CHANNEL_SPRITE ? ! validSprite (key.sprite) : ! finite (key.value)) return fail (why, "Not a value for a key");
		}
	}
	std::set<EventId> eventIds;
	for (size_t i = 0; i < clip.events.size (); i ++) {
		const ClipEvent & e = clip.events[i];
		if (e.id >= clip.nextEventId || ! eventIds.insert (e.id).second) return fail (why, "The IDs of events must be unique");
		if (e.what.kind != FrameEvent::SOUND && e.what.kind != FrameEvent::FUNCTION) return fail (why, "An event must be a sound or a function");
		if (clip.stepping == STEPPING_LEGACY) {
			if (e.frame < 0 || e.frame >= (int) clip.frames.size ()) return fail (why, "An event must be of a frame");
		} else {
			if (e.frame != -1) return fail (why, "Events of a timeline are at times, not frames");
			if (e.time < 0 || (clip.length > 0 ? e.time >= clip.length : e.time != 0)) return fail (why, "An event must lie within the clip, before its end");
		}
	}
	if (clip.stepping == STEPPING_LEGACY) {
		if (clip.tracks.size () != 1 || clip.tracks[0].channel != CHANNEL_SPRITE || ! clip.tracks[0].part.empty ()
			|| clip.tracks[0].keys.size () != clip.frames.size ())
			return fail (why, "An animation of anim () has one sprite track on the root, with a key per frame");
	} else if (! clip.frames.empty () || clip.spriteBank) {
		return fail (why, "Only animations of anim () have frames");
	}
	return true;
}

namespace {

bool resolve (SpriteRef & s, BankLoader load) {
	if (s.kind != SpriteRef::SPRITE || s.bank || s.bankFile < 0) return true;
	s.bank = load (s.bankFile);
	return s.bank != NULL;
}

}

bool resolveBanks (RigDefinition & rig, BankLoader load) {
	for (size_t i = 0; i < rig.parts.size (); i ++) {
		if (! resolve (rig.parts[i].sprite, load)) return false;
	}
	return true;
}

bool resolveBanks (ClipDefinition & clip, BankLoader load) {
	for (size_t t = 0; t < clip.tracks.size (); t ++) {
		for (size_t k = 0; k < clip.tracks[t].keys.size (); k ++) {
			if (! resolve (clip.tracks[t].keys[k].sprite, load)) return false;
		}
	}
	return true;
}
