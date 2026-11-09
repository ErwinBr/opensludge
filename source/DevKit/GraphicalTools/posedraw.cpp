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
		draw.drawPictureQuad (devkitBank (p.sprite.bank), p.sprite.number, false, c);
	}
}

int partAt (const Pose & pose, float x, float y) {
	for (size_t i = pose.drawOrder.size (); i -- > 0; ) {
		int part = pose.drawOrder[i];
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
	const Transform2D & w = pose.parts[part].world;
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
	h.scaleX = h.originX + bx * radius;
	h.scaleY = h.originY + by * radius;
	return h;
}

void drawPoseOverlay (Draw2D & draw, const RigDefinition & rig, const Pose & pose, int selected, float pixel) {
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
	const float axes[] = { h.originX, h.originY, h.turnX, h.turnY, h.originX, h.originY, h.scaleX, h.scaleY };
	draw.drawLines (axes, 4);
	draw.setDrawingColour (1.0f, 0.85f, 0.2f, 1.0f);
	circle (draw, h.turnX, h.turnY, pixel * 5);
	square (draw, h.scaleX, h.scaleY, pixel * 4);
	// The pivot: the origin, which a drag moves.
	draw.setDrawingColour (1.0f, 0.3f, 0.3f, 1.0f);
	circle (draw, h.originX, h.originY, pixel * 4);
	cross (draw, h.originX, h.originY, pixel * 7);
}
