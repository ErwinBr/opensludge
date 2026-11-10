#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <sys/stat.h>

#include <algorithm>
#include <set>

#include "ClipDocument.h"

namespace devkit {

namespace {

bool fail (std::string & why, const std::string & what) {
	why = what;
	return false;
}

int trackIndex (const animformat::Asset & a, TrackId id) {
	for (size_t i = 0; i < a.clip.tracks.size (); i ++) {
		if (a.clip.tracks[i].id == id) return (int) i;
	}
	return -1;
}

int keyIndex (const Track & t, int time) {
	for (size_t i = 0; i < t.keys.size (); i ++) {
		if (t.keys[i].time == time) return (int) i;
	}
	return -1;
}

int eventIndex (const animformat::Asset & a, EventId id) {
	for (size_t i = 0; i < a.clip.events.size (); i ++) {
		if (a.clip.events[i].id == id) return (int) i;
	}
	return -1;
}

// Keep only the banks that sprite keys use, in the order they are first used.
void compactBanks (animformat::Asset & a) {
	std::vector<std::string> used;
	std::vector<int> newIndex (a.banks.size (), -1);
	for (size_t t = 0; t < a.clip.tracks.size (); t ++) {
		for (size_t k = 0; k < a.clip.tracks[t].keys.size (); k ++) {
			SpriteRef & s = a.clip.tracks[t].keys[k].sprite;
			if (s.kind != SpriteRef::SPRITE || s.bankFile < 0 || (size_t) s.bankFile >= a.banks.size ()) continue;
			if (newIndex[s.bankFile] < 0) {
				newIndex[s.bankFile] = (int) used.size ();
				used.push_back (a.banks[s.bankFile]);
			}
			s.bankFile = newIndex[s.bankFile];
		}
	}
	a.banks = used;
}

std::string plural (int n, const char * one, const char * many) {
	char text[64];
	snprintf (text, sizeof (text), "%d %s", n, n == 1 ? one : many);
	return text;
}

}

DocumentType ClipDocument::type () {
	DocumentType type;
	type.name = "Clip";
	type.extensions.push_back ("clp");
	type.create = [] () -> Document * { return new ClipDocument; };
	return type;
}

ClipDocument::ClipDocument () {
	state.kind = animformat::Asset::CLIP;
	state.id = animformat::newAssetId ();
	state.clip.length = 48;
	state.clip.loop = true;
}

const Track * ClipDocument::track (TrackId id) const {
	int i = trackIndex (state, id);
	return i < 0 ? NULL : & state.clip.tracks[i];
}

const Track * ClipDocument::track (const std::string & part, Channel channel) const {
	return state.clip.track (part, channel);
}

const Key * ClipDocument::key (const KeyRef & k) const {
	const Track * t = track (k.track);
	if (! t) return NULL;
	int i = keyIndex (* t, k.time);
	return i < 0 ? NULL : & t->keys[i];
}

std::string ClipDocument::bankOf (const SpriteRef & sprite) const {
	if (sprite.kind != SpriteRef::SPRITE || sprite.bankFile < 0 || (size_t) sprite.bankFile >= state.banks.size ()) return std::string ();
	return state.banks[sprite.bankFile];
}

const ClipEvent * ClipDocument::event (EventId id) const {
	int i = eventIndex (state, id);
	return i < 0 ? NULL : & state.clip.events[i];
}

std::string ClipDocument::eventName (EventId id) const {
	int i = eventIndex (state, id);
	return i < 0 || (size_t) i >= state.eventNames.size () ? std::string () : state.eventNames[i];
}

void ClipDocument::change (const std::string & label, const animformat::Asset & after, int mergeKey) {
	commands ().run (new ValueCommand<animformat::Asset> (label, state, after,
		[this] (const animformat::Asset & a) { state = a; }, mergeKey));
}

bool ClipDocument::check (const animformat::Asset & after, std::string & why) const {
	animformat::Bytes bytes;
	return animformat::encodeAsset (after, bytes, why);
}

void ClipDocument::bindToRig (const animformat::AssetId & rig, const std::string & path) {
	if (state.rigId == rig && state.rigPath == path) return;
	animformat::Asset after = state;
	after.rigId = rig;
	after.rigPath = rig.isNull () ? std::string () : path;
	change ("Rig of the Clip", after);
}

std::vector<std::string> ClipDocument::missingParts (const RigDefinition & rig) const {
	std::set<std::string> missing;
	for (size_t t = 0; t < state.clip.tracks.size (); t ++) {
		if (rig.findPart (state.clip.tracks[t].part) < 0) missing.insert (state.clip.tracks[t].part);
	}
	return std::vector<std::string> (missing.begin (), missing.end ());
}

void ClipDocument::setLoop (bool loop) {
	if (state.clip.loop == loop) return;
	animformat::Asset after = state;
	after.clip.loop = loop;
	change (loop ? "Loop" : "Play Once", after);
}

int ClipDocument::itemsPast (int length) const {
	int n = 0;
	for (size_t t = 0; t < state.clip.tracks.size (); t ++) {
		for (size_t k = 0; k < state.clip.tracks[t].keys.size (); k ++) {
			if (state.clip.tracks[t].keys[k].time > length) n ++;
		}
	}
	for (size_t e = 0; e < state.clip.events.size (); e ++) {
		if (state.clip.events[e].time > length) n ++;
	}
	return n;
}

bool ClipDocument::setLength (int length, bool removePast, std::string & why) {
	if (length < 0) return fail (why, "A clip can't be shorter than 0 ticks");
	if (length == state.clip.length) return true;
	int past = itemsPast (length);
	if (past && ! removePast) return fail (why, plural (past, "key or event lies", "keys and events lie") + " past the new end");
	animformat::Asset after = state;
	after.clip.length = length;
	for (size_t t = 0; t < after.clip.tracks.size (); t ++) {
		std::vector<Key> & keys = after.clip.tracks[t].keys;
		keys.erase (std::remove_if (keys.begin (), keys.end (), [length] (const Key & k) { return k.time > length; }), keys.end ());
	}
	for (size_t e = after.clip.events.size (); e -- > 0; ) {
		if (after.clip.events[e].time > length) {
			after.clip.events.erase (after.clip.events.begin () + e);
			after.eventNames.erase (after.eventNames.begin () + e);
		}
	}
	compactBanks (after);
	if (! check (after, why)) return false;
	change ("Length of the Clip", after);
	return true;
}

bool ClipDocument::addTrack (const std::string & part, Channel channel, TrackId * added, std::string & why) {
	if (channel < 0 || channel >= NUM_CHANNELS) return fail (why, "Not a channel");
	if (track (part, channel)) return fail (why, "The part has a track of that already");
	animformat::Asset after = state;
	Track t;
	t.id = after.clip.nextTrackId ++;
	t.part = part;
	t.channel = channel;
	after.clip.tracks.push_back (t);
	if (! check (after, why)) return false;
	if (added) * added = t.id;
	change ("Add Track", after);
	return true;
}

void ClipDocument::removeTrack (TrackId id) {
	int i = trackIndex (state, id);
	if (i < 0) return;
	animformat::Asset after = state;
	after.clip.tracks.erase (after.clip.tracks.begin () + i);
	compactBanks (after);
	change ("Remove Track", after);
}

bool ClipDocument::setKey (TrackId id, int time, float value, Easing easing, std::string & why, int mergeKey) {
	const Track * t = track (id);
	if (! t) return fail (why, "There is no such track");
	if (t->channel == CHANNEL_SPRITE) return fail (why, "A sprite track has sprites");
	if (! isfinite (value)) return fail (why, "Not a number");
	if (time < 0 || time > state.clip.length) return fail (why, "A key must lie within the clip");
	Key k;
	k.time = time;
	k.value = value;
	if (t->channel == CHANNEL_VISIBLE || t->channel == CHANNEL_FLIP) k.value = value != 0 ? 1.0f : 0.0f;
	k.easing = isDiscrete (t->channel) ? EASING_STEP : easing;
	const Key * now = key (KeyRef (id, time));
	if (now && now->value == k.value && now->easing == k.easing) return true;
	animformat::Asset after = state;
	if (! ::setKey (after.clip, t->part, t->channel, k, why)) return false;
	change ("Key", after, mergeKey);
	return true;
}

bool ClipDocument::setSpriteKey (TrackId id, int time, const std::string & bank, int number, bool mirror, bool useDefault, std::string & why) {
	const Track * t = track (id);
	if (! t) return fail (why, "There is no such track");
	if (t->channel != CHANNEL_SPRITE) return fail (why, "Only a sprite track has sprites");
	if (time < 0 || time > state.clip.length) return fail (why, "A key must lie within the clip");
	if (number < 0) return fail (why, "Not a sprite");
	animformat::Asset after = state;
	Key k;
	k.time = time;
	k.easing = EASING_STEP;
	if (useDefault) {
		k.sprite = SpriteRef ();
	} else if (bank.empty ()) {
		k.sprite = SpriteRef::none ();
	} else {
		int index = -1;
		for (size_t b = 0; b < after.banks.size (); b ++) {
			if (after.banks[b] == bank) index = (int) b;
		}
		if (index < 0) {
			index = (int) after.banks.size ();
			after.banks.push_back (bank);
		}
		k.sprite = SpriteRef::sprite (AnimBankRef (), index, number, mirror);
	}
	if (! ::setKey (after.clip, t->part, t->channel, k, why)) return false;
	compactBanks (after);
	change ("Sprite Key", after);
	return true;
}

void ClipDocument::setEasing (const KeyRef & ref, Easing easing) {
	const Track * t = track (ref.track);
	const Key * k = key (ref);
	if (! t || ! k || isDiscrete (t->channel) || k->easing == easing || easing < 0 || easing >= NUM_EASINGS) return;
	animformat::Asset after = state;
	Track & changed = after.clip.tracks[trackIndex (after, ref.track)];
	changed.keys[keyIndex (changed, ref.time)].easing = easing;
	change ("Easing", after);
}

void ClipDocument::removeKeys (const std::vector<KeyRef> & keys) {
	animformat::Asset after = state;
	int removed = 0;
	for (size_t i = 0; i < keys.size (); i ++) {
		int t = trackIndex (after, keys[i].track);
		if (t < 0) continue;
		int k = keyIndex (after.clip.tracks[t], keys[i].time);
		if (k < 0) continue;
		after.clip.tracks[t].keys.erase (after.clip.tracks[t].keys.begin () + k);
		removed ++;
	}
	if (! removed) return;
	compactBanks (after);
	change (removed == 1 ? "Remove Key" : "Remove Keys", after);
}

bool ClipDocument::moveKeys (const std::vector<KeyRef> & keys, int by, bool copy, std::string & why, int mergeKey) {
	if (keys.empty () || (by == 0 && ! copy)) return true;
	animformat::Asset after = state;
	// What moves, before any of it does, so that keys moved onto each other's
	// ticks don't get in each other's way.
	struct Moving {
		std::string part;
		Channel channel;
		Key key;
	};
	std::vector<Moving> moving;
	std::set<KeyRef> seen;
	for (size_t i = 0; i < keys.size (); i ++) {
		if (! seen.insert (keys[i]).second) continue;
		const Track * t = track (keys[i].track);
		const Key * k = key (keys[i]);
		if (! t || ! k) continue;
		if (k->time + by < 0 || k->time + by > state.clip.length) return fail (why, "Keys must stay within the clip");
		Moving m = { t->part, t->channel, * k };
		m.key.time += by;
		moving.push_back (m);
	}
	if (moving.empty ()) return true;
	if (! copy) {
		for (std::set<KeyRef>::const_iterator i = seen.begin (); i != seen.end (); ++ i) {
			int t = trackIndex (after, i->track);
			if (t < 0) continue;
			int k = keyIndex (after.clip.tracks[t], i->time);
			if (k >= 0) after.clip.tracks[t].keys.erase (after.clip.tracks[t].keys.begin () + k);
		}
	}
	// Onto a tick of a key of the same track, it replaces it.
	for (size_t i = 0; i < moving.size (); i ++) {
		if (! ::setKey (after.clip, moving[i].part, moving[i].channel, moving[i].key, why)) return false;
	}
	if (! check (after, why)) return false;
	change (copy ? (moving.size () == 1 ? "Copy Key" : "Copy Keys") : (moving.size () == 1 ? "Move Key" : "Move Keys"), after, mergeKey);
	return true;
}

bool ClipDocument::autoKey (const std::string & part, Channel channel, int time, float value, std::string & why, int mergeKey) {
	if (channel < 0 || channel >= NUM_CHANNELS || channel == CHANNEL_SPRITE) return fail (why, "Not a channel to key");
	if (time < 0 || time > state.clip.length) return fail (why, "A key must lie within the clip");
	if (! isfinite (value)) return fail (why, "Not a number");
	Key k;
	k.time = time;
	k.value = value;
	k.easing = EASING_LINEAR;
	const Track * t = track (part, channel);
	if (t) {
		int at = keyIndex (* t, time);
		if (at >= 0) {
			// The key there keeps its easing.
			k.easing = t->keys[at].easing;
			if (t->keys[at].value == value) return true;
		} else {
			// A new key moves on as the one before it does.
			for (size_t i = 0; i < t->keys.size () && t->keys[i].time < time; i ++) k.easing = t->keys[i].easing;
		}
	}
	if (isDiscrete (channel)) k.easing = EASING_STEP;
	animformat::Asset after = state;
	if (! ::setKey (after.clip, part, channel, k, why)) return false;
	change ("Key " + (part.empty () ? std::string ("the Character") : part), after, mergeKey);
	return true;
}

bool ClipDocument::addEvent (int time, FrameEvent::Kind kind, const std::string & name, EventId * added, std::string & why) {
	if (kind != FrameEvent::SOUND && kind != FrameEvent::FUNCTION) return fail (why, "An event plays a sound or calls a function");
	if (name.empty ()) return fail (why, kind == FrameEvent::SOUND ? "Choose a sound" : "Name a function");
	animformat::Asset after = state;
	FrameEvent what;
	what.kind = kind;
	if (! ::addEvent (after.clip, time, what, why)) return false;
	after.eventNames.push_back (name);
	if (! check (after, why)) return false;
	if (added) * added = after.clip.events.back ().id;
	change ("Add Event", after);
	return true;
}

bool ClipDocument::setEvent (EventId id, int time, FrameEvent::Kind kind, const std::string & name, std::string & why, int mergeKey) {
	int i = eventIndex (state, id);
	if (i < 0) return fail (why, "There is no such event");
	if (kind != FrameEvent::SOUND && kind != FrameEvent::FUNCTION) return fail (why, "An event plays a sound or calls a function");
	if (time < 0 || time > state.clip.length) return fail (why, "An event must lie within the clip");
	if (name.empty ()) return fail (why, kind == FrameEvent::SOUND ? "Choose a sound" : "Name a function");
	const ClipEvent & now = state.clip.events[i];
	if (now.time == time && now.what.kind == kind && state.eventNames[i] == name) return true;
	animformat::Asset after = state;
	after.clip.events[i].time = time;
	after.clip.events[i].what.kind = kind;
	after.eventNames[i] = name;
	if (! check (after, why)) return false;
	change ("Event", after, mergeKey);
	return true;
}

void ClipDocument::removeEvent (EventId id) {
	int i = eventIndex (state, id);
	if (i < 0) return;
	animformat::Asset after = state;
	after.clip.events.erase (after.clip.events.begin () + i);
	after.eventNames.erase (after.eventNames.begin () + i);
	change ("Remove Event", after);
}

bool ClipDocument::renamePart (const std::string & from, const std::string & to, uint64_t * serial, std::string & why) {
	if (serial) * serial = 0;
	if (from == to) return true;
	animformat::Asset after = state;
	int renamed = 0;
	for (size_t t = 0; t < after.clip.tracks.size (); t ++) {
		Track & track = after.clip.tracks[t];
		if (track.part != from) continue;
		if (state.clip.track (to, track.channel)) return fail (why, "The clip has tracks of \"" + to + "\" already");
		track.part = to;
		renamed ++;
	}
	// How the part shows goes with it, over any of the new name's.
	animformat::EditorPart shows = state.editorPart (from);
	if (! shows.isDefault ()) {
		animformat::EditorPart gone;
		gone.name = from;
		after.setEditorPart (gone);
		shows.name = to;
		after.setEditorPart (shows);
		renamed ++;
	}
	if (! renamed) return true;
	if (! check (after, why)) return false;
	change ("Rename Part", after);
	if (serial) * serial = commands ().lastSerial ();
	return true;
}

void ClipDocument::setLocked (const std::string & part, bool locked) {
	if (isLocked (part) == locked) return;
	animformat::Asset after = state;
	animformat::EditorPart e = after.editorPart (part);
	e.locked = locked;
	after.setEditorPart (e);
	change (std::string (locked ? "Lock " : "Unlock ") + (part.empty () ? std::string ("the Character") : part), after);
}

void ClipDocument::setScaling (const std::string & part, animformat::EditorPart::Scaling s) {
	if (scaling (part) == s) return;
	animformat::Asset after = state;
	animformat::EditorPart e = after.editorPart (part);
	e.scaling = s;
	after.setEditorPart (e);
	change (std::string (s == animformat::EditorPart::SCALING_FREE ? "Separate Scales of " : "Uniform Scale of ")
		+ (part.empty () ? std::string ("the Character") : part), after);
}

bool ClipDocument::read (const std::string & path, std::string * error) {
	FILE * f = fopen (path.c_str (), "rb");
	if (! f) {
		* error = "The file can't be opened.";
		return false;
	}
	animformat::Bytes bytes;
	unsigned char buffer[4096];
	size_t n;
	while ((n = fread (buffer, 1, sizeof (buffer), f)) > 0) bytes.insert (bytes.end (), buffer, buffer + n);
	fclose (f);
	animformat::Asset loaded;
	std::string why;
	if (! animformat::decodeAsset (bytes.data (), bytes.size (), loaded, why)) {
		* error = "It isn't a clip that can be read: " + why + ".";
		return false;
	}
	if (loaded.kind != animformat::Asset::CLIP) {
		* error = "It is a rig, not a clip.";
		return false;
	}
	state = loaded;
	return true;
}

bool ClipDocument::write (const std::string & path, std::string * error) {
	animformat::Bytes bytes;
	std::string why;
	if (! animformat::encodeAsset (state, bytes, why)) {
		* error = "The clip can't be saved: " + why + ".";
		return false;
	}
	FILE * f = fopen (path.c_str (), "wb");
	bool ok = f && fwrite (bytes.data (), 1, bytes.size (), f) == bytes.size ();
	if (f && fclose (f)) ok = false;
	if (! ok) * error = "The file can't be written.";
	return ok;
}

namespace {

void findFiles (const std::string & folder, const std::string & extension, int depth, std::vector<std::string> & out) {
	if (depth > 16) return;
	DIR * dir = opendir (folder.empty () ? "." : folder.c_str ());
	if (! dir) return;
	std::vector<std::string> names;
	while (struct dirent * entry = readdir (dir)) {
		std::string name = entry->d_name;
		if (name.empty () || name[0] == '.') continue;
		names.push_back (name);
	}
	closedir (dir);
	std::sort (names.begin (), names.end ());
	for (size_t i = 0; i < names.size (); i ++) {
		std::string path = folder + names[i];
		struct stat info;
		if (stat (path.c_str (), & info)) continue;
		if (S_ISDIR (info.st_mode)) findFiles (path + "/", extension, depth + 1, out);
		else if (extensionOf (path) == extension) out.push_back (path);
	}
}

bool readAsset (const std::string & path, animformat::Asset & a) {
	FILE * f = fopen (path.c_str (), "rb");
	if (! f) return false;
	animformat::Bytes bytes;
	unsigned char buffer[4096];
	size_t n;
	while ((n = fread (buffer, 1, sizeof (buffer), f)) > 0) bytes.insert (bytes.end (), buffer, buffer + n);
	fclose (f);
	std::string why;
	return animformat::decodeAsset (bytes.data (), bytes.size (), a, why);
}

}

std::vector<std::string> filesWithExtension (const std::string & folder, const std::string & extension) {
	std::vector<std::string> out;
	std::string base = folder;
	if (! base.empty () && base[base.size () - 1] != '/' && base[base.size () - 1] != '\\') base += "/";
	findFiles (base, extension, 0, out);
	return out;
}

std::vector<std::string> clipsOfRig (const std::string & folder, const animformat::AssetId & rig) {
	std::vector<std::string> out;
	if (rig.isNull ()) return out;
	std::vector<std::string> clips = filesWithExtension (folder, "clp");
	for (size_t i = 0; i < clips.size (); i ++) {
		animformat::Asset a;
		if (readAsset (clips[i], a) && a.kind == animformat::Asset::CLIP && a.rigId == rig) out.push_back (clips[i]);
	}
	return out;
}

} // namespace devkit
