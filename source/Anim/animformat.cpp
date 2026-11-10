#include "animformat.h"
#include "animedit.h"

#include <stdint.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <random>
#include <set>

namespace animformat {

namespace {

const char magic[4] = { 'S', 'L', 'A', 'N' };
const char rigTag[4] = { 'R', 'I', 'G', ' ' };
const char clipTag[4] = { 'C', 'L', 'I', 'P' };
const char assetIdTag[4] = { 'A', 'S', 'I', 'D' };
const char banksTag[4] = { 'B', 'A', 'N', 'K' };
const char rigRefTag[4] = { 'R', 'R', 'E', 'F' };
const char eventNamesTag[4] = { 'E', 'V', 'N', 'M' };
const char editorTag[4] = { 'E', 'D', 'I', 'T' };

// The fewest bytes that a part, track, key and event take, to check counts.
const size_t minPart = 4 + 2 + 4 + 16 + 1 + 4 + 1;
const size_t minTrack = 4 + 2 + 1 + 4;
const size_t minKey = 4 + 1 + 1;
const size_t minEvent = 4 + 4 + 1 + 4;

class Writer {
public:
	explicit Writer (Bytes & out) : out (out) {}
	void u8 (int v) { out.push_back ((unsigned char) v); }
	void u16 (int v) { u8 (v & 255); u8 ((v >> 8) & 255); }
	void u32 (uint32_t v) { for (int i = 0; i < 4; i ++) u8 ((v >> (8 * i)) & 255); }
	void i32 (int v) { u32 ((uint32_t) v); }
	void f32 (float v) { uint32_t b; memcpy (& b, & v, 4); u32 (b); }
	void tag (const char t[4]) { for (int i = 0; i < 4; i ++) u8 (t[i]); }
	void str (const std::string & s) { u16 ((int) s.size ()); out.insert (out.end (), s.begin (), s.end ()); }
	void sprite (const SpriteRef & s) {
		u8 (s.kind == SpriteRef::DEFAULT ? 0 : s.kind == SpriteRef::NONE ? 1 : 2);
		if (s.kind == SpriteRef::SPRITE) {
			i32 (s.bankFile);
			i32 (s.number);
			u8 (s.mirror ? 1 : 0);
		}
	}
	// A chunk's size goes before it; start leaves room, end fills it in.
	size_t start (const char t[4]) { tag (t); size_t at = out.size (); u32 (0); return at; }
	void end (size_t at) {
		uint32_t size = (uint32_t) (out.size () - at - 4);
		for (int i = 0; i < 4; i ++) out[at + i] = (size >> (8 * i)) & 255;
	}
private:
	Bytes & out;
};

class Reader {
public:
	Reader (const unsigned char * data, size_t size) : data (data), size (size), pos (0) {}
	size_t remaining () const { return size - pos; }
	const unsigned char * here () const { return data + pos; }
	bool skip (size_t n) { if (remaining () < n) return false; pos += n; return true; }
	bool u8 (int & v) { if (remaining () < 1) return false; v = data[pos ++]; return true; }
	bool u16 (int & v) { int a, b; if (! u8 (a) || ! u8 (b)) return false; v = a | (b << 8); return true; }
	bool u32 (uint32_t & v) {
		if (remaining () < 4) return false;
		v = 0;
		for (int i = 0; i < 4; i ++) v |= (uint32_t) data[pos ++] << (8 * i);
		return true;
	}
	bool i32 (int & v) { uint32_t u; if (! u32 (u)) return false; v = (int) u; return true; }
	bool f32 (float & v) { uint32_t b; if (! u32 (b)) return false; memcpy (& v, & b, 4); return true; }
	bool bytes (void * to, size_t n) { if (remaining () < n) return false; memcpy (to, data + pos, n); pos += n; return true; }
	bool str (std::string & s) {
		int n;
		if (! u16 (n) || remaining () < (size_t) n) return false;
		s.assign ((const char *) data + pos, n);
		pos += n;
		return s.find ('\0') == std::string::npos;
	}
	bool sprite (SpriteRef & s) {
		int kind;
		if (! u8 (kind) || kind > 2) return false;
		s = SpriteRef ();
		if (kind == 1) s.kind = SpriteRef::NONE;
		if (kind != 2) return true;
		s.kind = SpriteRef::SPRITE;
		int mirror;
		if (! i32 (s.bankFile) || ! i32 (s.number) || ! u8 (mirror) || mirror > 1) return false;
		s.mirror = mirror == 1;
		return true;
	}
	// A count of items of at least min bytes each, that the bytes left can hold.
	bool count (size_t & n, size_t min) {
		uint32_t c;
		if (! u32 (c) || c > remaining () / min) return false;
		n = c;
		return true;
	}
private:
	const unsigned char * data;
	size_t size, pos;
};

bool fail (std::string & why, const std::string & what) {
	why = what;
	return false;
}

void writeRig (Writer & w, const RigDefinition & rig, int version) {
	size_t at = w.start (rigTag);
	w.u32 (rig.nextPartId);
	w.u32 ((uint32_t) rig.parts.size ());
	for (size_t i = 0; i < rig.parts.size (); i ++) {
		const RigPart & p = rig.parts[i];
		w.u32 (p.id);
		w.str (p.name);
		w.i32 (p.parent);
		w.f32 (p.rest.x);
		w.f32 (p.rest.y);
		w.f32 (p.rest.rotation);
		w.f32 (p.rest.scaleX);
		if (version >= 3) w.f32 (p.rest.scaleY);
		w.u8 ((p.rest.flip ? 1 : 0) | (p.rest.visible ? 2 : 0));
		w.i32 (p.depth);
		w.sprite (p.sprite);
		if (version >= 2) {
			w.f32 (p.pivotX);
			w.f32 (p.pivotY);
		}
		if (version >= 4) {
			for (int c = 0; c < 4; c ++) w.f32 (p.rest.modulate[c]);
			for (int c = 0; c < 4; c ++) w.f32 (p.rest.selfModulate[c]);
		}
	}
	w.end (at);
}

void writeClip (Writer & w, const ClipDefinition & clip) {
	size_t at = w.start (clipTag);
	w.i32 (clip.length);
	w.u8 (clip.loop ? 1 : 0);
	w.u32 (clip.nextTrackId);
	w.u32 (clip.nextEventId);
	w.u32 ((uint32_t) clip.tracks.size ());
	for (size_t t = 0; t < clip.tracks.size (); t ++) {
		const Track & track = clip.tracks[t];
		w.u32 (track.id);
		w.str (track.part);
		w.u8 (track.channel);
		w.u32 ((uint32_t) track.keys.size ());
		for (size_t k = 0; k < track.keys.size (); k ++) {
			const Key & key = track.keys[k];
			w.i32 (key.time);
			w.u8 (key.easing);
			if (track.channel == CHANNEL_SPRITE) w.sprite (key.sprite);
			else w.f32 (key.value);
		}
	}
	w.u32 ((uint32_t) clip.events.size ());
	for (size_t e = 0; e < clip.events.size (); e ++) {
		const ClipEvent & ev = clip.events[e];
		w.u32 (ev.id);
		w.i32 (ev.time);
		w.u8 (ev.what.kind == FrameEvent::SOUND ? 1 : 2);
		w.i32 (ev.what.number);
	}
	w.end (at);
}

// The version a rig needs: 4 with a part of a colour, 3 with a part of
// different X and Y scales, 2 with a pivot, 1 otherwise.
int versionFor (const RigDefinition & rig) {
	int v = 1;
	for (size_t p = 0; p < rig.parts.size (); p ++) {
		if (! rig.parts[p].rest.white ()) return 4;
	}
	for (size_t p = 0; p < rig.parts.size (); p ++) {
		if (! rig.parts[p].rest.uniform ()) return 3;
		if (rig.parts[p].pivotX != 0 || rig.parts[p].pivotY != 0) v = 2;
	}
	return v;
}

// The version a clip needs: 4 with a track of a colour, 3 with a track of
// SCALE_X or SCALE_Y, 1 otherwise.
int versionFor (const ClipDefinition & clip) {
	for (size_t t = 0; t < clip.tracks.size (); t ++) {
		if (isColour (clip.tracks[t].channel)) return 4;
	}
	for (size_t t = 0; t < clip.tracks.size (); t ++) {
		if (clip.tracks[t].channel == CHANNEL_SCALE_X || clip.tracks[t].channel == CHANNEL_SCALE_Y) return 3;
	}
	return 1;
}

// The channels that files of a version have.
int channelsOf (int version) {
	return version >= 4 ? NUM_CHANNELS : version >= 3 ? CHANNEL_SCALE_Y + 1 : CHANNEL_FLIP + 1;
}

bool readRig (Reader & r, RigDefinition & rig, int version, std::string & why) {
	size_t n;
	if (! r.u32 (rig.nextPartId) || ! r.count (n, minPart)) return fail (why, "The parts of a rig are cut off");
	rig.parts.assign (n, RigPart ());
	for (size_t i = 0; i < n; i ++) {
		RigPart & p = rig.parts[i];
		int flags;
		if (! r.u32 (p.id) || ! r.str (p.name) || ! r.i32 (p.parent)
			|| ! r.f32 (p.rest.x) || ! r.f32 (p.rest.y) || ! r.f32 (p.rest.rotation) || ! r.f32 (p.rest.scaleX)
			|| (version >= 3 && ! r.f32 (p.rest.scaleY))
			|| ! r.u8 (flags) || flags > 3 || ! r.i32 (p.depth) || ! r.sprite (p.sprite)
			|| (version >= 2 && (! r.f32 (p.pivotX) || ! r.f32 (p.pivotY))))
			return fail (why, "A part of a rig is damaged");
		if (version >= 4) {
			bool ok = true;
			for (int c = 0; c < 4; c ++) ok = ok && r.f32 (p.rest.modulate[c]);
			for (int c = 0; c < 4; c ++) ok = ok && r.f32 (p.rest.selfModulate[c]);
			if (! ok) return fail (why, "A part of a rig is damaged");
		}
		if (version < 3) p.rest.scaleY = p.rest.scaleX;
		p.rest.flip = (flags & 1) != 0;
		p.rest.visible = (flags & 2) != 0;
	}
	return validRig (rig, why);
}

bool readClip (Reader & r, ClipDefinition & clip, int version, std::string & why) {
	int loop;
	size_t n;
	if (! r.i32 (clip.length) || ! r.u8 (loop) || loop > 1 || ! r.u32 (clip.nextTrackId) || ! r.u32 (clip.nextEventId)
		|| ! r.count (n, minTrack))
		return fail (why, "A clip is damaged");
	clip.stepping = STEPPING_TIMELINE;
	clip.loop = loop == 1;
	clip.tracks.assign (n, Track ());
	for (size_t t = 0; t < n; t ++) {
		Track & track = clip.tracks[t];
		int channel;
		size_t keys;
		if (! r.u32 (track.id) || ! r.str (track.part) || ! r.u8 (channel) || channel >= channelsOf (version) || ! r.count (keys, minKey))
			return fail (why, "A track of a clip is damaged");
		track.channel = (Channel) channel;
		track.keys.assign (keys, Key ());
		for (size_t k = 0; k < keys; k ++) {
			Key & key = track.keys[k];
			int easing;
			if (! r.i32 (key.time) || ! r.u8 (easing) || easing >= NUM_EASINGS
				|| ! (track.channel == CHANNEL_SPRITE ? r.sprite (key.sprite) : r.f32 (key.value)))
				return fail (why, "A key of a clip is damaged");
			key.easing = (Easing) easing;
		}
	}
	if (! r.count (n, minEvent)) return fail (why, "The events of a clip are cut off");
	clip.events.assign (n, ClipEvent ());
	for (size_t e = 0; e < n; e ++) {
		ClipEvent & ev = clip.events[e];
		int kind;
		if (! r.u32 (ev.id) || ! r.i32 (ev.time) || ! r.u8 (kind) || kind < 1 || kind > 2 || ! r.i32 (ev.what.number))
			return fail (why, "An event of a clip is damaged");
		ev.what.kind = kind == 1 ? FrameEvent::SOUND : FrameEvent::FUNCTION;
		ev.frame = -1;
	}
	return validClip (clip, why);
}

}

bool encode (const Document & doc, Bytes & out, std::string & why) {
	for (size_t i = 0; i < doc.rigs.size (); i ++) {
		if (! validRig (doc.rigs[i], why)) return false;
	}
	for (size_t i = 0; i < doc.clips.size (); i ++) {
		if (doc.clips[i].stepping != STEPPING_TIMELINE) return fail (why, "Animations of anim () aren't stored in this format");
		if (! validClip (doc.clips[i], why)) return false;
	}
	int v = 1;
	for (size_t i = 0; i < doc.rigs.size (); i ++) v = std::max (v, versionFor (doc.rigs[i]));
	for (size_t i = 0; i < doc.clips.size (); i ++) v = std::max (v, versionFor (doc.clips[i]));
	out.clear ();
	Writer w (out);
	w.tag (magic);
	w.u16 (v);
	for (size_t i = 0; i < doc.rigs.size (); i ++) writeRig (w, doc.rigs[i], v);
	for (size_t i = 0; i < doc.clips.size (); i ++) writeClip (w, doc.clips[i]);
	return true;
}

bool decode (const unsigned char * data, size_t size, Document & doc, std::string & why) {
	doc = Document ();
	Reader r (data, size);
	char m[4];
	int v;
	if (! r.bytes (m, 4) || memcmp (m, magic, 4) || ! r.u16 (v)) return fail (why, "Not a file of rigs and clips");
	if (v < 1 || v > version) return fail (why, "A file of rigs and clips of a version that this engine doesn't know");
	while (r.remaining ()) {
		char tag[4];
		uint32_t chunkSize;
		if (! r.bytes (tag, 4) || ! r.u32 (chunkSize) || chunkSize > r.remaining ()) return fail (why, "A chunk is cut off");
		Reader c (r.here (), chunkSize);
		bool known = true;
		if (! memcmp (tag, rigTag, 4)) {
			doc.rigs.push_back (RigDefinition ());
			if (! readRig (c, doc.rigs.back (), v, why)) return false;
		} else if (! memcmp (tag, clipTag, 4)) {
			doc.clips.push_back (ClipDefinition ());
			if (! readClip (c, doc.clips.back (), v, why)) return false;
		} else {
			known = false;
		}
		if (known && c.remaining ()) return fail (why, "A chunk is longer than what it holds");
		r.skip (chunkSize);
	}
	return true;
}


bool AssetId::isNull () const {
	for (int i = 0; i < 16; i ++) {
		if (bytes[i]) return false;
	}
	return true;
}

bool AssetId::operator== (const AssetId & o) const {
	return ! memcmp (bytes, o.bytes, 16);
}

std::string AssetId::text () const {
	static const char digits[] = "0123456789abcdef";
	std::string s;
	for (int i = 0; i < 16; i ++) {
		s += digits[bytes[i] >> 4];
		s += digits[bytes[i] & 15];
	}
	return s;
}

// Not a static of newAssetId: one that needs constructing gets a thread-safe
// guard, which MinGW's libstdc++ makes of condition variables, which Windows
// XP lacks, and the engine links this file. Made on first use, by the Dev
// Kit's thread of the interface.
static std::mt19937_64 * idRandom = NULL;

AssetId newAssetId () {
	if (! idRandom) {
		std::random_device device;
		idRandom = new std::mt19937_64 (((uint64_t) device () << 32) ^ device ());
	}
	AssetId id;
	do {
		for (int i = 0; i < 16; i += 8) {
			uint64_t r = (* idRandom) ();
			for (int b = 0; b < 8; b ++) id.bytes[i + b] = (unsigned char) (r >> (8 * b));
		}
	} while (id.isNull ());
	return id;
}

EditorPart Asset::editorPart (PartId id) const {
	for (size_t i = 0; i < editor.size (); i ++) {
		if (editor[i].id == id) return editor[i];
	}
	EditorPart p;
	p.id = id;
	return p;
}

EditorPart Asset::editorPart (const std::string & name) const {
	for (size_t i = 0; i < editor.size (); i ++) {
		if (editor[i].name == name) return editor[i];
	}
	EditorPart p;
	p.name = name;
	return p;
}

void Asset::setEditorPart (const EditorPart & p) {
	// A rig's parts scale uniformly by default.
	EditorPart part = p;
	if (kind == RIG && part.scaling == EditorPart::SCALING_UNIFORM) part.scaling = EditorPart::SCALING_DEFAULT;
	for (size_t i = 0; i < editor.size (); i ++) {
		if (kind == RIG ? editor[i].id == part.id : editor[i].name == part.name) {
			if (part.isDefault ()) editor.erase (editor.begin () + i);
			else editor[i] = part;
			return;
		}
	}
	if (! part.isDefault ()) editor.push_back (part);
}

namespace {

// What a string of the format may be: up to 65535 bytes, without a 0 byte.
bool storable (const std::string & s) {
	return s.size () <= 65535 && s.find ('\0') == std::string::npos;
}

bool bankOfAsset (const SpriteRef & s, size_t banks) {
	return s.kind != SpriteRef::SPRITE || (s.bankFile >= 0 && (size_t) s.bankFile < banks);
}

bool wholeAsset (const Asset & a, std::string & why) {
	if (a.id.isNull ()) return fail (why, "An asset needs an identity");
	for (size_t i = 0; i < a.banks.size (); i ++) {
		if (a.banks[i].empty () || ! storable (a.banks[i])) return fail (why, "Not the path of a sprite bank");
	}
	if (a.kind == Asset::RIG) {
		if (! a.rigId.isNull () || ! a.rigPath.empty () || ! a.eventNames.empty ())
			return fail (why, "Only a clip is bound to a rig, and has events");
		if (! validRig (a.rig, why)) return false;
		for (size_t i = 0; i < a.rig.parts.size (); i ++) {
			if (! bankOfAsset (a.rig.parts[i].sprite, a.banks.size ())) return fail (why, "A sprite of a bank that the asset doesn't list");
		}
		return true;
	}
	if (a.kind != Asset::CLIP) return fail (why, "An asset is of a rig or a clip");
	if (a.clip.stepping != STEPPING_TIMELINE) return fail (why, "Animations of anim () aren't assets");
	if (! validClip (a.clip, why)) return false;
	if (a.rigId.isNull () && ! a.rigPath.empty ()) return fail (why, "The path of a rig, without the rig");
	if (! storable (a.rigPath)) return fail (why, "Not the path of a rig");
	for (size_t t = 0; t < a.clip.tracks.size (); t ++) {
		for (size_t k = 0; k < a.clip.tracks[t].keys.size (); k ++) {
			if (! bankOfAsset (a.clip.tracks[t].keys[k].sprite, a.banks.size ())) return fail (why, "A sprite of a bank that the asset doesn't list");
		}
	}
	if (a.eventNames.size () != a.clip.events.size ()) return fail (why, "Every event of a clip needs a sound or a function");
	for (size_t e = 0; e < a.eventNames.size (); e ++) {
		if (a.eventNames[e].empty () || ! storable (a.eventNames[e])) return fail (why, "Not the sound or function of an event");
	}
	return true;
}

// The parts of "EDIT" that are written: not as by default, and of a rig's
// parts (a clip's may have any name, of parts its rig lacks too).
std::vector<EditorPart> editorPartsToWrite (const Asset & a) {
	std::vector<EditorPart> out;
	for (size_t i = 0; i < a.editor.size (); i ++) {
		const EditorPart & p = a.editor[i];
		if (p.isDefault ()) continue;
		if (a.kind == Asset::RIG && partWithId (a.rig, p.id) < 0) continue;
		if (a.kind == Asset::CLIP && ! storable (p.name)) continue;
		out.push_back (p);
	}
	return out;
}

void writeAssetId (Writer & w, const AssetId & id) {
	for (int i = 0; i < 16; i ++) w.u8 (id.bytes[i]);
}

bool readAssetId (Reader & r, AssetId & id) {
	return r.bytes (id.bytes, 16);
}

}

bool encodeAsset (const Asset & asset, Bytes & out, std::string & why) {
	if (! wholeAsset (asset, why)) return false;
	out.clear ();
	Writer w (out);
	w.tag (magic);
	int v = asset.kind == Asset::RIG ? versionFor (asset.rig) : versionFor (asset.clip);
	w.u16 (v);
	size_t at = w.start (assetIdTag);
	writeAssetId (w, asset.id);
	w.end (at);
	at = w.start (banksTag);
	w.u32 ((uint32_t) asset.banks.size ());
	for (size_t i = 0; i < asset.banks.size (); i ++) w.str (asset.banks[i]);
	w.end (at);
	std::vector<EditorPart> editor = editorPartsToWrite (asset);
	if (! editor.empty ()) {
		at = w.start (editorTag);
		w.u32 ((uint32_t) editor.size ());
		for (size_t i = 0; i < editor.size (); i ++) {
			if (asset.kind == Asset::RIG) w.u32 (editor[i].id);
			else w.str (editor[i].name);
			int scaling = editor[i].scaling == EditorPart::SCALING_FREE ? 2 : editor[i].scaling == EditorPart::SCALING_UNIFORM ? 4 : 0;
			w.u8 ((editor[i].locked ? 1 : 0) | scaling);
		}
		w.end (at);
	}
	if (asset.kind == Asset::RIG) {
		writeRig (w, asset.rig, v);
		return true;
	}
	writeClip (w, asset.clip);
	if (! asset.rigId.isNull ()) {
		at = w.start (rigRefTag);
		writeAssetId (w, asset.rigId);
		w.str (asset.rigPath);
		w.end (at);
	}
	at = w.start (eventNamesTag);
	w.u32 ((uint32_t) asset.eventNames.size ());
	for (size_t e = 0; e < asset.eventNames.size (); e ++) {
		w.u32 (asset.clip.events[e].id);
		w.str (asset.eventNames[e]);
	}
	w.end (at);
	return true;
}

bool decodeAsset (const unsigned char * data, size_t size, Asset & asset, std::string & why) {
	asset = Asset ();
	Reader r (data, size);
	char m[4];
	int v;
	if (! r.bytes (m, 4) || memcmp (m, magic, 4) || ! r.u16 (v)) return fail (why, "Not a file of rigs and clips");
	if (v < 1 || v > version) return fail (why, "A file of rigs and clips of a version that this program doesn't know");
	int definitions = 0;
	bool haveId = false, haveBanks = false, haveRigRef = false, haveNames = false, haveEditor = false;
	std::map<EventId, std::string> names;
	// "EDIT" comes before the rig or clip, which says how to read it.
	Bytes editorChunk;
	while (r.remaining ()) {
		char tag[4];
		uint32_t chunkSize;
		if (! r.bytes (tag, 4) || ! r.u32 (chunkSize) || chunkSize > r.remaining ()) return fail (why, "A chunk is cut off");
		Reader c (r.here (), chunkSize);
		bool known = true;
		if (! memcmp (tag, rigTag, 4) || ! memcmp (tag, clipTag, 4)) {
			if (definitions ++) return fail (why, "An asset is of one rig or one clip");
			if (! memcmp (tag, rigTag, 4)) {
				asset.kind = Asset::RIG;
				if (! readRig (c, asset.rig, v, why)) return false;
			} else {
				asset.kind = Asset::CLIP;
				if (! readClip (c, asset.clip, v, why)) return false;
			}
		} else if (! memcmp (tag, assetIdTag, 4)) {
			if (haveId) return fail (why, "An asset has one identity");
			haveId = true;
			if (! readAssetId (c, asset.id)) return fail (why, "The identity of an asset is cut off");
		} else if (! memcmp (tag, banksTag, 4)) {
			if (haveBanks) return fail (why, "An asset has one list of sprite banks");
			haveBanks = true;
			size_t n;
			if (! c.count (n, 2)) return fail (why, "The sprite banks of an asset are cut off");
			asset.banks.assign (n, std::string ());
			for (size_t i = 0; i < n; i ++) {
				if (! c.str (asset.banks[i])) return fail (why, "The path of a sprite bank is damaged");
			}
		} else if (! memcmp (tag, rigRefTag, 4)) {
			if (haveRigRef) return fail (why, "A clip is bound to one rig");
			haveRigRef = true;
			if (! readAssetId (c, asset.rigId) || ! c.str (asset.rigPath)) return fail (why, "The rig of a clip is damaged");
			if (asset.rigId.isNull ()) return fail (why, "A clip is bound to a rig with no identity");
		} else if (! memcmp (tag, eventNamesTag, 4)) {
			if (haveNames) return fail (why, "A clip has one list of its events' sounds and functions");
			haveNames = true;
			size_t n;
			if (! c.count (n, 4 + 2)) return fail (why, "The sounds and functions of events are cut off");
			for (size_t i = 0; i < n; i ++) {
				uint32_t id;
				std::string name;
				if (! c.u32 (id) || ! c.str (name)) return fail (why, "The sound or function of an event is damaged");
				if (! names.insert (std::make_pair (id, name)).second) return fail (why, "An event has one sound or function");
			}
		} else if (! memcmp (tag, editorTag, 4)) {
			if (haveEditor) return fail (why, "An asset has one list of how its parts show");
			haveEditor = true;
			editorChunk.assign (r.here (), r.here () + chunkSize);
			known = false;
		} else {
			known = false;
		}
		if (known && c.remaining ()) return fail (why, "A chunk is longer than what it holds");
		r.skip (chunkSize);
	}
	if (definitions != 1) return fail (why, "An asset is of one rig or one clip");
	if (! haveId) return fail (why, "An asset needs an identity");
	if (haveEditor) {
		Reader c (editorChunk.data (), editorChunk.size ());
		size_t n;
		if (! c.count (n, asset.kind == Asset::RIG ? 4 + 1 : 2 + 1)) return fail (why, "How the parts show is cut off");
		for (size_t i = 0; i < n; i ++) {
			EditorPart p;
			int flags;
			if (! (asset.kind == Asset::RIG ? c.u32 (p.id) : c.str (p.name)) || ! c.u8 (flags) || flags > 7 || (flags & 6) == 6)
				return fail (why, "How a part shows is damaged");
			p.locked = (flags & 1) != 0;
			p.scaling = (flags & 2) ? EditorPart::SCALING_FREE : (flags & 4) ? EditorPart::SCALING_UNIFORM : EditorPart::SCALING_DEFAULT;
			bool twice = asset.kind == Asset::RIG ? ! asset.editorPart (p.id).isDefault () : ! asset.editorPart (p.name).isDefault ();
			if (twice) return fail (why, "A part shows one way");
			// Parts that the rig no longer has are forgotten.
			if (asset.kind == Asset::RIG && partWithId (asset.rig, p.id) < 0) continue;
			asset.setEditorPart (p);
		}
		if (c.remaining ()) return fail (why, "A chunk is longer than what it holds");
	}
	if (asset.kind == Asset::RIG && (haveRigRef || haveNames)) return fail (why, "Only a clip is bound to a rig, and has events");
	if (asset.kind == Asset::CLIP) {
		if (names.size () != asset.clip.events.size ()) return fail (why, "Every event of a clip needs a sound or a function");
		for (size_t e = 0; e < asset.clip.events.size (); e ++) {
			std::map<EventId, std::string>::const_iterator n = names.find (asset.clip.events[e].id);
			if (n == names.end ()) return fail (why, "Every event of a clip needs a sound or a function");
			asset.eventNames.push_back (n -> second);
		}
	}
	return wholeAsset (asset, why);
}

}
