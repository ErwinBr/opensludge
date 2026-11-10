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

Transform2D Transform2D::scaling (float sx, float sy) {
	Transform2D t;
	t.a = sx;
	t.d = sy;
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
	if (p.scaleX != 1 || p.scaleY != 1) t = t * Transform2D::scaling (p.scaleX, p.scaleY);
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
	return s.kind == SpriteRef::SPRITE && s.bank && s.number >= 0 && s.number < s.bank -> animSpriteCount ();
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

// A value of a channel onto a part: added (multiplied for the scales) by
// an additive layer to X, Y, ROTATION and the scales; replaced otherwise.
void apply (PosedPart & p, Channel channel, float value, bool add) {
	switch (channel) {
		case CHANNEL_X: p.local.x = add ? p.local.x + value : value; break;
		case CHANNEL_Y: p.local.y = add ? p.local.y + value : value; break;
		case CHANNEL_ROTATION: p.local.rotation = add ? p.local.rotation + value : value; break;
		case CHANNEL_SCALE:
			p.local.scaleX = add ? p.local.scaleX * value : value;
			p.local.scaleY = add ? p.local.scaleY * value : value;
			break;
		case CHANNEL_SCALE_X: p.local.scaleX = add ? p.local.scaleX * value : value; break;
		case CHANNEL_SCALE_Y: p.local.scaleY = add ? p.local.scaleY * value : value; break;
		case CHANNEL_VISIBLE: p.local.visible = value != 0; break;
		case CHANNEL_DEPTH: p.depth = (int) lrintf (value); break;
		case CHANNEL_FLIP: p.local.flip = value != 0; break;
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
	std::vector<LayerSamples> layers;
	if (clip) {
		LayerSamples l = { clip, & samples, LAYER_BLEND_OVERRIDE, & binding };
		layers.push_back (l);
	}
	evaluatePose (rig, layers, std::vector<PartOverride> (), character, out);
}

void evaluatePose (const RigDefinitionRef & rig, const std::vector<LayerSamples> & layers,
	const std::vector<PartOverride> & overrides, const Transform2D & character, Pose & out) {
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

	for (size_t l = 0; l < layers.size (); l ++) {
		const ClipDefinitionRef & clip = layers[l].clip;
		if (! clip || ! layers[l].samples) continue;
		const TrackSamples & samples = * layers[l].samples;
		bool add = layers[l].blend == LAYER_BLEND_ADD;
		const std::vector<int> & partOf = layers[l].binding -> bind (clip, rig);
		// SCALE first, so that SCALE_X and SCALE_Y of the clip go over it,
		// whatever the order of its tracks.
		for (int pass = 0; pass < 2; pass ++) {
			for (size_t s = 0; s < samples.size (); s ++) {
				const Track * t = samples[s].track;
				if (t < clip -> tracks.data () || t >= clip -> tracks.data () + clip -> tracks.size ()) continue;
				if ((t -> channel == CHANNEL_SCALE) != (pass == 0)) continue;
				int part = partOf[t - clip -> tracks.data ()];
				if (part < 0) continue;
				if (t -> channel == CHANNEL_SPRITE) out.parts[part].sprite = shown (* samples[s].sprite, parts[part].sprite);
				else apply (out.parts[part], t -> channel, samples[s].value, add);
			}
		}
	}
	for (int pass = 0; pass < 2; pass ++) {
		for (size_t o = 0; o < overrides.size (); o ++) {
			if ((overrides[o].channel == CHANNEL_SCALE) != (pass == 0)) continue;
			int part = rig -> findPart (overrides[o].part);
			if (part < 0 || overrides[o].channel == CHANNEL_SPRITE) continue;
			apply (out.parts[part], overrides[o].channel, overrides[o].value, false);
		}
	}

	// Parents come before their children.
	for (size_t i = 0; i < parts.size (); i ++) {
		PosedPart & p = out.parts[i];
		int parent = parts[i].parent;
		const Transform2D & above = parent < 0 ? character : out.parts[parent].world;
		p.world = above * localTransform (p.local);
		p.spriteWorld = parts[i].pivotX == 0 && parts[i].pivotY == 0 ? p.world
			: p.world * Transform2D::translation (- parts[i].pivotX, - parts[i].pivotY);
		p.shown = p.local.visible && (parent < 0 || out.parts[parent].shown);
		if (p.shown && p.sprite.kind == SpriteRef::SPRITE) out.drawOrder.push_back ((int) i);
	}
	std::stable_sort (out.drawOrder.begin (), out.drawOrder.end (),
		[&out] (int x, int y) { return out.parts[x].depth < out.parts[y].depth; });
}

void movePivot (RigDefinition & rig, int part, float pivotX, float pivotY) {
	if (part < 0 || part >= (int) rig.parts.size ()) return;
	RigPart & p = rig.parts[part];
	float dx = pivotX - p.pivotX, dy = pivotY - p.pivotY;
	PartPose turn = p.rest;
	turn.x = turn.y = 0;
	float mx, my;
	localTransform (turn).apply (dx, dy, mx, my);
	p.rest.x += mx;
	p.rest.y += my;
	p.pivotX = pivotX;
	p.pivotY = pivotY;
	for (size_t i = 0; i < rig.parts.size (); i ++) {
		if (rig.parts[i].parent != part) continue;
		rig.parts[i].rest.x -= dx;
		rig.parts[i].rest.y -= dy;
	}
}

void spriteCorners (const Transform2D & m, int w, int h, int xhot, int yhot, bool mirror, float out[8]) {
	Transform2D t = mirror ? m * Transform2D::flipX () : m;
	const float xs[2] = { (float) - xhot, (float) (w - xhot) };
	const float ys[2] = { (float) - yhot, (float) (h - yhot) };
	for (int i = 0; i < 4; i ++) t.apply (xs[i & 1], ys[i >> 1], out[i * 2], out[i * 2 + 1]);
}

bool poseBounds (const Pose & pose, float & x1, float & y1, float & x2, float & y2) {
	bool any = false;
	for (size_t i = 0; i < pose.drawOrder.size (); i ++) {
		const PosedPart & p = pose.parts[pose.drawOrder[i]];
		AnimSpriteSize s = p.sprite.bank -> animSpriteSize (p.sprite.number);
		float c[8];
		spriteCorners (p.spriteWorld, s.width, s.height, s.xhot, s.yhot, p.sprite.mirror, c);
		for (int k = 0; k < 4; k ++) {
			float x = c[k * 2], y = c[k * 2 + 1];
			if (! any) {
				x1 = x2 = x;
				y1 = y2 = y;
				any = true;
			}
			x1 = std::min (x1, x);
			x2 = std::max (x2, x);
			y1 = std::min (y1, y);
			y2 = std::max (y2, y);
		}
	}
	return any;
}
