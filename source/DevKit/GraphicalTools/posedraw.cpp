/*
 * posedraw.cpp - Part of the SLUDGE Dev Kit
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "draw2d.h"
#include "posedraw.h"

DevKitAnimBank::DevKitAnimBank () {
	memset (& bank, 0, sizeof (bank));
}

DevKitAnimBank::~DevKitAnimBank () {
	forgetSpriteBank (& bank);
}

bool DevKitAnimBank::load (const std::string & file) {
	FILE * f = fopen (file.c_str (), "rb");
	if (! f) return false;
	fclose (f);
	forgetSpriteBank (& bank);
	memset (& bank, 0, sizeof (bank));
	return loadSpriteBank (file.c_str (), & bank);
}

AnimSpriteSize DevKitAnimBank::animSpriteSize (int number) const {
	const sprite & s = bank.sprites[number];
	AnimSpriteSize size = { s.width, s.height, s.xhot, s.yhot };
	return size;
}

namespace {

// What a part's sprite is drawn through: its own mirror image about its
// hotspot too, as the engine draws it.
Transform2D spriteTransform (const PosedPart & p) {
	return p.sprite.mirror ? p.spriteWorld * Transform2D::flipX () : p.spriteWorld;
}

const sprite * spriteOf (const PosedPart & p) {
	const spriteBank * bank = devkitBank (p.sprite.bank);
	if (! bank || p.sprite.number < 0 || p.sprite.number >= bank -> total) return NULL;
	return & bank -> sprites[p.sprite.number];
}

void corners (const PosedPart & p, float out[8]) {
	const sprite * s = spriteOf (p);
	if (! s) {
		for (int i = 0; i < 8; i ++) out[i] = 0;
		return;
	}
	spriteCorners (p.spriteWorld, s -> width, s -> height, s -> xhot, s -> yhot, p.sprite.mirror, out);
}

void cross (Draw2D & draw, float x, float y, float size) {
	const float lines[] = { x - size, y, x + size, y, x, y - size, x, y + size };
	draw.drawLines (lines, 4);
}

void circle (Draw2D & draw, float x, float y, float r) {
	float points[64];
	for (int i = 0; i < 32; i ++) {
		double a = i * 2 * 3.14159265358979323846 / 32;
		points[i * 2] = x + (float) (r * cos (a));
		points[i * 2 + 1] = y + (float) (r * sin (a));
	}
	draw.drawLineLoop (points, 32);
}

void square (Draw2D & draw, float x, float y, float half) {
	const float box[] = { x - half, y - half, x + half, y - half, x + half, y + half, x - half, y + half };
	draw.drawPolygon (box, 4);
}

}

void drawPose (Draw2D & draw, const Pose & pose) {
	draw.setDrawingColour (1, 1, 1, 1);
	for (size_t i = 0; i < pose.drawOrder.size (); i ++) {
		const PosedPart & p = pose.parts[pose.drawOrder[i]];
		const sprite * s = spriteOf (p);
		if (! s || ! s -> width || ! s -> height || ! s -> data) continue;
		float c[8];
		corners (p, c);
		draw.setTint (p.colour[0], p.colour[1], p.colour[2], p.colour[3]);
		draw.drawPictureQuad (devkitBank (p.sprite.bank), p.sprite.number, false, c);
	}
	draw.setTint (1, 1, 1, 1);
}

int partAt (const Pose & pose, float x, float y, const std::function<bool (int)> & canPick) {
	for (size_t i = pose.drawOrder.size (); i -- > 0; ) {
		int part = pose.drawOrder[i];
		if (canPick && ! canPick (part)) continue;
		const PosedPart & p = pose.parts[part];
		const sprite * s = spriteOf (p);
		if (! s || ! s -> width || ! s -> height || ! s -> data) continue;
		Transform2D inverse;
		if (! spriteTransform (p).inverse (inverse)) continue;
		float lx, ly;
		inverse.apply (x, y, lx, ly);
		int sx = (int) floorf (lx + s -> xhot), sy = (int) floorf (ly + s -> yhot);
		if (sx < 0 || sy < 0 || sx >= s -> width || sy >= s -> height) continue;
		unsigned char * rgba = spritePixels (devkitBank (p.sprite.bank), p.sprite.number);
		bool shows = rgba && rgba[((size_t) sy * s -> width + sx) * 4 + 3] != 0;
		delete [] rgba;
		if (shows) return part;
	}
	return -1;
}

PartHandles partHandles (const Pose & pose, int part, float radius) {
	PartHandles h;
	memset (& h, 0, sizeof (h));
	if (part < 0 || part >= (int) pose.parts.size ()) return h;
	const PosedPart & p = pose.parts[part];
	const Transform2D & w = p.world;
	h.originX = w.tx;
	h.originY = w.ty;
	h.radius = radius;
	// The directions of the part's axes in the world, whatever its scale.
	float ax = w.a, ay = w.b, bx = w.c, by = w.d;
	float la = sqrtf (ax * ax + ay * ay), lb = sqrtf (bx * bx + by * by);
	if (la > 0) { ax /= la; ay /= la; } else { ax = 1; ay = 0; }
	if (lb > 0) { bx /= lb; by /= lb; } else { bx = 0; by = 1; }
	h.turnX = h.originX + ax * radius;
	h.turnY = h.originY + ay * radius;

	// The box: top left, top right, bottom left, bottom right, along the
	// part's axes (its sprite's, which a mirrored sprite only flips).
	float c[8];
	if (spriteOf (p)) {
		corners (p, c);
	} else {
		float r = radius * 0.7f;
		const float sx[4] = { -1, 1, -1, 1 }, sy[4] = { -1, -1, 1, 1 };
		for (int i = 0; i < 4; i ++) {
			c[i * 2] = h.originX + (ax * sx[i] + bx * sy[i]) * r;
			c[i * 2 + 1] = h.originY + (ay * sx[i] + by * sy[i]) * r;
		}
	}
	for (int i = 0; i < 4; i ++) {
		h.scale[i].x = c[i * 2];
		h.scale[i].y = c[i * 2 + 1];
		h.scale[i].axes = ScaleHandle::BOTH;
	}
	// The middles of the sides: top and bottom scale Y, left and right X.
	const int sides[4][2] = { { 0, 1 }, { 2, 3 }, { 0, 2 }, { 1, 3 } };
	const int axes[4] = { ScaleHandle::Y, ScaleHandle::Y, ScaleHandle::X, ScaleHandle::X };
	for (int i = 0; i < 4; i ++) {
		ScaleHandle & s = h.scale[4 + i];
		s.x = (c[sides[i][0] * 2] + c[sides[i][1] * 2]) * 0.5f;
		s.y = (c[sides[i][0] * 2 + 1] + c[sides[i][1] * 2 + 1]) * 0.5f;
		s.axes = axes[i];
	}
	return h;
}

void scaleFromHandle (const Transform2D & parent, const PartPose & start, const ScaleHandle & handle,
		bool uniform, float mouseX, float mouseY, float & scaleX, float & scaleY) {
	scaleX = start.scaleX;
	scaleY = start.scaleY;
	// The part's frame without its own scale: in it, a point of the part is
	// its scales times where it is in the part.
	PartPose frame = start;
	frame.scaleX = frame.scaleY = 1;
	Transform2D in;
	if (! (parent * localTransform (frame)).inverse (in)) return;
	float hx, hy, mx, my;
	in.apply (handle.x, handle.y, hx, hy);
	in.apply (mouseX, mouseY, mx, my);
	float fx = 1, fy = 1;
	if (uniform) {
		float from = sqrtf (hx * hx + hy * hy), to = sqrtf (mx * mx + my * my);
		if (from > 0) fx = fy = to / from;
	} else {
		// Along each axis, with a mouse on the other side of the origin
		// kept to the smallest scale.
		if ((handle.axes & ScaleHandle::X) && fabsf (hx) > 1e-4f) fx = mx / hx;
		if ((handle.axes & ScaleHandle::Y) && fabsf (hy) > 1e-4f) fy = my / hy;
	}
	float px = floorf (start.scaleX * fx * 100.0f + 0.5f), py = floorf (start.scaleY * fy * 100.0f + 0.5f);
	if (uniform) {
		// The same percent for both, of X when they weren't the same.
		py = floorf (start.scaleY * fx * 100.0f + 0.5f);
		if (start.scaleX == start.scaleY) py = px;
	}
	scaleX = (px < 1 ? 1 : px) / 100.0f;
	scaleY = (py < 1 ? 1 : py) / 100.0f;
}

void drawPoseOverlay (Draw2D & draw, const RigDefinition & rig, const Pose & pose, int selected, float pixel, bool sides) {
	// Bones, from each part's origin to its parent's.
	draw.setDrawingColour (0.35f, 0.8f, 1.0f, 0.6f);
	for (size_t i = 1; i < pose.parts.size () && i < rig.parts.size (); i ++) {
		int parent = rig.parts[i].parent;
		if (parent < 0) continue;
		const Transform2D & a = pose.parts[parent].world, & b = pose.parts[i].world;
		const float line[] = { a.tx, a.ty, b.tx, b.ty };
		draw.drawLines (line, 2);
	}
	// Origins: a dot for a part with a sprite, a cross for a joint.
	for (size_t i = 0; i < pose.parts.size () && i < rig.parts.size (); i ++) {
		const Transform2D & w = pose.parts[i].world;
		bool joint = rig.parts[i].sprite.kind != SpriteRef::SPRITE;
		if (joint) {
			draw.setDrawingColour (1.0f, 0.75f, 0.3f, 0.9f);
			cross (draw, w.tx, w.ty, pixel * 5);
		} else {
			draw.setDrawingColour (0.35f, 0.8f, 1.0f, 0.9f);
			square (draw, w.tx, w.ty, pixel * 2);
		}
	}
	if (selected < 0 || selected >= (int) pose.parts.size ()) return;

	// The selected part: its sprite's outline, and its handles.
	const PosedPart & p = pose.parts[selected];
	if (spriteOf (p)) {
		float c[8];
		corners (p, c);
		const float outline[] = { c[0], c[1], c[2], c[3], c[6], c[7], c[4], c[5] };
		draw.setDrawingColour (1.0f, 0.85f, 0.2f, 1.0f);
		draw.drawLineLoop (outline, 4);
	}
	PartHandles h = partHandles (pose, selected, pixel * 40);
	draw.setDrawingColour (1.0f, 0.85f, 0.2f, 0.8f);
	circle (draw, h.originX, h.originY, h.radius);
	const float axis[] = { h.originX, h.originY, h.turnX, h.turnY };
	draw.drawLines (axis, 2);
	draw.setDrawingColour (1.0f, 0.85f, 0.2f, 1.0f);
	circle (draw, h.turnX, h.turnY, pixel * 5);
	// The scale handles: the corners, and with sides the middles of the
	// sides; a joint's on a faint square of its own.
	if (! spriteOf (p)) {
		draw.setDrawingColour (1.0f, 0.85f, 0.2f, 0.5f);
		const float box[] = { h.scale[0].x, h.scale[0].y, h.scale[1].x, h.scale[1].y, h.scale[3].x, h.scale[3].y, h.scale[2].x, h.scale[2].y };
		draw.drawLineLoop (box, 4);
		draw.setDrawingColour (1.0f, 0.85f, 0.2f, 1.0f);
	}
	for (int i = 0; i < (sides ? 8 : 4); i ++) square (draw, h.scale[i].x, h.scale[i].y, pixel * 3);
	// The pivot: the origin, which a drag moves.
	draw.setDrawingColour (1.0f, 0.3f, 0.3f, 1.0f);
	circle (draw, h.originX, h.originY, pixel * 4);
	cross (draw, h.originX, h.originY, pixel * 7);
}
