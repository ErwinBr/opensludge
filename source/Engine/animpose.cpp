#include "animpose.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <set>
#include <string>
#include <tuple>

// Not statics of functions: one that needs constructing gets a thread-safe
// guard, which MinGW's libstdc++ makes of condition variables, which
// Windows XP lacks. forgetPoseReports forgets them.
static std::set<std::tuple<const void *, const void *, std::string> > reportedMissingParts;
static std::set<std::pair<int, int> > reportedMissingSprites;

static const double pi = 3.14159265358979323846;

Transform2D Transform2D::translation (float x, float y) {
	Transform2D t;
	t.tx = x;
	t.ty = y;
	return t;
}

Transform2D Transform2D::rotation (float degrees) {
	// With +Y down, the usual matrix turns clockwise on screen: (1, 0), to
	// the right, turns a quarter to (0, 1), down.
	double r = degrees * pi / 180.0;
	Transform2D t;
	t.a = (float) cos (r);
	t.b = (float) sin (r);
	t.c = - t.b;
	t.d = t.a;
	return t;
}

Transform2D Transform2D::scaling (float s) {
	Transform2D t;
	t.a = t.d = s;
	return t;
}

Transform2D Transform2D::flipX () {
	Transform2D t;
	t.a = -1;
	return t;
}

Transform2D Transform2D::operator* (const Transform2D & o) const {
	Transform2D t;
	t.a = a * o.a + c * o.b;
	t.b = b * o.a + d * o.b;
	t.c = a * o.c + c * o.d;
	t.d = b * o.c + d * o.d;
	t.tx = a * o.tx + c * o.ty + tx;
	t.ty = b * o.tx + d * o.ty + ty;
	return t;
}

void Transform2D::apply (float x, float y, float & outX, float & outY) const {
	outX = a * x + c * y + tx;
	outY = b * x + d * y + ty;
}

bool Transform2D::inverse (Transform2D & out) const {
	double det = (double) a * d - (double) b * c;
	if (fabs (det) < 1e-12) return false;
	out.a = (float) (d / det);
	out.b = (float) (- b / det);
	out.c = (float) (- c / det);
	out.d = (float) (a / det);
	out.tx = - (out.a * tx + out.c * ty);
	out.ty = - (out.b * tx + out.d * ty);
	return true;
}

Transform2D localTransform (const PartPose & p) {
	Transform2D t = Transform2D::translation (p.x, p.y);
	if (p.rotation != 0) t = t * Transform2D::rotation (p.rotation);
	if (p.scale != 1) t = t * Transform2D::scaling (p.scale);
	if (p.flip) t = t * Transform2D::flipX ();
	return t;
}

const std::vector<int> & PoseBinding::bind (const ClipDefinitionRef & c, const RigDefinitionRef & r) {
	// The same clip and rig, unless either went and another took its place.
	// (A weak_ptr of one that went locks to NULL, whatever took its address.)
	if (c && clip.lock () == c && rig.lock () == r) return partOfTrack;
	clip = c;
	rig = r;
	partOfTrack.clear ();
	if (! c || ! r) return partOfTrack;
	partOfTrack.resize (c -> tracks.size ());
	for (size_t t = 0; t < c -> tracks.size (); t ++) {
		const std::string & name = c -> tracks[t].part;
		partOfTrack[t] = r -> findPart (name);
		if (partOfTrack[t] < 0 && reportedMissingParts.insert (std::make_tuple ((const void *) c.get (), (const void *) r.get (), name)).second) {
			animDiagnostic ("a clip has a track for the part \"" + name + "\", which the rig doesn't have; leaving it out");
		}
	}
	return partOfTrack;
}

namespace {

bool exists (const SpriteRef & s) {
	return s.kind == SpriteRef::SPRITE && s.bank && s.number >= 0 && s.number < s.bank -> bank.total;
}

void reportMissing (const SpriteRef & s) {
	if (reportedMissingSprites.insert (std::make_pair (s.bankFile, s.number)).second) {
		char text[120];
		snprintf (text, sizeof (text), "sprite %d of bank %d doesn't exist; showing the part's own sprite", s.number, s.bankFile);
		animDiagnostic (text);
	}
}

// What a part shows of sprite s: s, its own sprite, or nothing (NONE).
SpriteRef shown (const SpriteRef & s, const SpriteRef & own) {
	if (s.kind == SpriteRef::NONE) return SpriteRef::none ();
	if (s.kind == SpriteRef::SPRITE) {
		if (exists (s)) return s;
		reportMissing (s);
	}
	// DEFAULT, or missing: the part's own sprite, if it has one.
	if (own.kind != SpriteRef::SPRITE) return SpriteRef::none ();
	if (exists (own)) return own;
	reportMissing (own);
	return SpriteRef::none ();
}

void apply (PosedPart & p, const SpriteRef & own, Channel channel, const TrackSample & s) {
	switch (channel) {
		case CHANNEL_X: p.local.x = s.value; break;
		case CHANNEL_Y: p.local.y = s.value; break;
		case CHANNEL_ROTATION: p.local.rotation = s.value; break;
		case CHANNEL_SCALE: p.local.scale = s.value; break;
		case CHANNEL_SPRITE: p.sprite = shown (* s.sprite, own); break;
		case CHANNEL_VISIBLE: p.local.visible = s.value != 0; break;
		case CHANNEL_DEPTH: p.depth = (int) lrintf (s.value); break;
		case CHANNEL_FLIP: p.local.flip = s.value != 0; break;
		default: break;
	}
}

}

void forgetPoseReports () {
	reportedMissingParts.clear ();
	reportedMissingSprites.clear ();
}

void evaluatePose (const RigDefinitionRef & rig, const ClipDefinitionRef & clip, const TrackSamples & samples,
	const Transform2D & character, PoseBinding & binding, Pose & out) {
	out.parts.clear ();
	out.drawOrder.clear ();
	if (! rig) return;
	const std::vector<RigPart> & parts = rig -> parts;
	out.parts.resize (parts.size ());
	for (size_t i = 0; i < parts.size (); i ++) {
		PosedPart & p = out.parts[i];
		p.local = parts[i].rest;
		p.depth = parts[i].depth;
		p.sprite = shown (SpriteRef (), parts[i].sprite);
	}

	if (clip) {
		const std::vector<int> & partOf = binding.bind (clip, rig);
		for (size_t s = 0; s < samples.size (); s ++) {
			const Track * t = samples[s].track;
			if (t < clip -> tracks.data () || t >= clip -> tracks.data () + clip -> tracks.size ()) continue;
			int part = partOf[t - clip -> tracks.data ()];
			if (part < 0) continue;
			apply (out.parts[part], parts[part].sprite, t -> channel, samples[s]);
		}
	}

	// Parents come before their children.
	for (size_t i = 0; i < parts.size (); i ++) {
		PosedPart & p = out.parts[i];
		int parent = parts[i].parent;
		const Transform2D & above = parent < 0 ? character : out.parts[parent].world;
		p.world = above * localTransform (p.local);
		p.shown = p.local.visible && (parent < 0 || out.parts[parent].shown);
		if (p.shown && p.sprite.kind == SpriteRef::SPRITE) out.drawOrder.push_back ((int) i);
	}
	std::stable_sort (out.drawOrder.begin (), out.drawOrder.end (),
		[&out] (int x, int y) { return out.parts[x].depth < out.parts[y].depth; });
}
