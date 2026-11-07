#include "animformat.h"

#include <stdint.h>
#include <string.h>

namespace animformat {

namespace {

const char magic[4] = { 'S', 'L', 'A', 'N' };
const char rigTag[4] = { 'R', 'I', 'G', ' ' };
const char clipTag[4] = { 'C', 'L', 'I', 'P' };

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

void writeRig (Writer & w, const RigDefinition & rig) {
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
		w.f32 (p.rest.scale);
		w.u8 ((p.rest.flip ? 1 : 0) | (p.rest.visible ? 2 : 0));
		w.i32 (p.depth);
		w.sprite (p.sprite);
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

bool readRig (Reader & r, RigDefinition & rig, std::string & why) {
	size_t n;
	if (! r.u32 (rig.nextPartId) || ! r.count (n, minPart)) return fail (why, "The parts of a rig are cut off");
	rig.parts.assign (n, RigPart ());
	for (size_t i = 0; i < n; i ++) {
		RigPart & p = rig.parts[i];
		int flags;
		if (! r.u32 (p.id) || ! r.str (p.name) || ! r.i32 (p.parent)
			|| ! r.f32 (p.rest.x) || ! r.f32 (p.rest.y) || ! r.f32 (p.rest.rotation) || ! r.f32 (p.rest.scale)
			|| ! r.u8 (flags) || flags > 3 || ! r.i32 (p.depth) || ! r.sprite (p.sprite))
			return fail (why, "A part of a rig is damaged");
		p.rest.flip = (flags & 1) != 0;
		p.rest.visible = (flags & 2) != 0;
	}
	return validRig (rig, why);
}

bool readClip (Reader & r, ClipDefinition & clip, std::string & why) {
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
		if (! r.u32 (track.id) || ! r.str (track.part) || ! r.u8 (channel) || channel >= NUM_CHANNELS || ! r.count (keys, minKey))
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
	out.clear ();
	Writer w (out);
	w.tag (magic);
	w.u16 (version);
	for (size_t i = 0; i < doc.rigs.size (); i ++) writeRig (w, doc.rigs[i]);
	for (size_t i = 0; i < doc.clips.size (); i ++) writeClip (w, doc.clips[i]);
	return true;
}

bool decode (const unsigned char * data, size_t size, Document & doc, std::string & why) {
	doc = Document ();
	Reader r (data, size);
	char m[4];
	int v;
	if (! r.bytes (m, 4) || memcmp (m, magic, 4) || ! r.u16 (v)) return fail (why, "Not a file of rigs and clips");
	if (v != version) return fail (why, "A file of rigs and clips of a version that this engine doesn't know");
	while (r.remaining ()) {
		char tag[4];
		uint32_t chunkSize;
		if (! r.bytes (tag, 4) || ! r.u32 (chunkSize) || chunkSize > r.remaining ()) return fail (why, "A chunk is cut off");
		Reader c (r.here (), chunkSize);
		bool known = true;
		if (! memcmp (tag, rigTag, 4)) {
			doc.rigs.push_back (RigDefinition ());
			if (! readRig (c, doc.rigs.back (), why)) return false;
		} else if (! memcmp (tag, clipTag, 4)) {
			doc.clips.push_back (ClipDefinition ());
			if (! readClip (c, doc.clips.back (), why)) return false;
		} else {
			known = false;
		}
		if (known && c.remaining ()) return fail (why, "A chunk is longer than what it holds");
		r.skip (chunkSize);
	}
	return true;
}

}
