// Outlines around characters and the mouse cursor: see outline.h.

#include <math.h>
#include <string.h>

#include <algorithm>

#include "allfiles.h"
#include "graphics.h"
#include "outline.h"
#include "sludger.h"

extern unsigned int winWidth, winHeight;
extern float cameraZoom;

std::vector<ShapeQuad> * shapeSink = NULL;

bool operator== (const ShapeQuad & a, const ShapeQuad & b) {
	return a.texture == b.texture && a.alpha == b.alpha
		&& memcmp (a.corners, b.corners, sizeof (a.corners)) == 0
		&& memcmp (a.texCoords, b.texCoords, sizeof (a.texCoords)) == 0;
}

ShapeQuad shapeQuad (gfx::Texture * texture, const float * vertices, const float * texCoords, float alpha) {
	ShapeQuad q;
	q.texture = texture;
	for (int c = 0; c < 4; c ++) {
		q.corners[c * 2] = vertices[c * 3];
		q.corners[c * 2 + 1] = vertices[c * 3 + 1];
	}
	memcpy (q.texCoords, texCoords, sizeof (q.texCoords));
	q.alpha = alpha;
	return q;
}

namespace {

// The seeds code positions in eighths of a pixel in 16 bits, and all ones
// stands for no seed.
const int maxFieldSize = 8000;

struct Field {
	const void * key;
	std::vector<ShapeQuad> shape;
	int thickness;
	float pixelsPerUnit;	// Pixels of the field for one unit of the projection
	float box[4];			// x1, y1, x2, y2 in the projection
	int width, height;		// Of the field and its textures
	// The silhouette, and two textures for the seeds, which take turns.
	gfx::Texture * texture[3];
	gfx::RenderTarget * target[3];
	int seeds;				// 1 or 2: which has the nearest seeds
	bool valid, used;
};

std::vector<Field> fields;

Field * findField (const void * key) {
	for (size_t i = 0; i < fields.size (); i ++) {
		if (fields[i].key == key) return & fields[i];
	}
	return NULL;
}

void releaseTextures (Field & f) {
	for (int i = 0; i < 3; i ++) {
		if (f.target[i]) gfx::renderer->releaseTarget (f.target[i]);
		if (f.texture[i]) gfx::renderer->releaseTexture (f.texture[i]);
		f.target[i] = NULL;
		f.texture[i] = NULL;
	}
	f.width = f.height = 0;
}

// Textures of at least width by height, but not much larger, so that a
// character that animates keeps its textures.
void sizeTextures (Field & f, int width, int height) {
	if (f.texture[0] && f.width >= width && f.height >= height && f.width <= width + 128 && f.height <= height + 128) return;
	releaseTextures (f);
	f.width = (width + 63) & ~63;
	f.height = (height + 63) & ~63;
	for (int i = 0; i < 3; i ++) {
		f.texture[i] = gfx::renderer->createTexture (f.width, f.height, gfx::TEX_RGBA8, NULL);
		gfx::renderer->setTextureFilter (f.texture[i], gfx::FILTER_NEAREST, gfx::FILTER_NEAREST);
		gfx::renderer->setTextureWrap (f.texture[i], gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
		f.target[i] = gfx::renderer->createTarget (f.texture[i], false);
	}
}

// A pass over the whole field, from the texture on unit 0 into target.
void fieldPass (const Field & f, gfx::Program program, gfx::Texture * from, int to, float step) {
	static const float vertices[] = {
		-1, 1, 0,
		1, 1, 0,
		-1, -1, 0,
		1, -1, 0
	};
	static const float texCoords[] = {
		0, 1,
		1, 1,
		0, 0,
		1, 0
	};
	gfx::renderer->setTarget (f.target[to]);
	gfx::DrawUniforms u;
	u.outline[0] = 0;
	u.outline[1] = step;
	u.outline[2] = (float) f.width;
	u.outline[3] = (float) f.height;
	gfx::draw (gfx::PipelineState (program), u, from, vertices, texCoords);
}

// The thickness in pixels of the field.
float fieldThickness (int thickness) {
	return thickness * (float) viewportWidth / winWidth;
}

} // namespace

OutlineField makeOutlineField (const void * key, const std::vector<ShapeQuad> & shape, int thickness, bool clip) {
	Field * f = findField (key);
	if (! f) {
		Field n;
		memset (n.texture, 0, sizeof (n.texture));
		memset (n.target, 0, sizeof (n.target));
		n.key = key;
		n.width = n.height = 0;
		n.thickness = 0;
		n.pixelsPerUnit = 0;
		n.seeds = 1;
		n.valid = false;
		fields.push_back (n);
		f = & fields.back ();
	}
	f->used = true;

	float pixelsPerUnit = viewportWidth * cameraZoom / winWidth;
	if (f->valid && f->shape == shape && f->thickness == thickness && f->pixelsPerUnit == pixelsPerUnit) return OUTLINE_KEPT;
	f->valid = false;
	f->shape = shape;
	f->thickness = thickness;
	f->pixelsPerUnit = pixelsPerUnit;
	if (shape.empty () || thickness <= 0 || pixelsPerUnit <= 0) return OUTLINE_NONE;

	// The box around the quads, and the outline, and a pixel for the seeds,
	// on the grid of the window's pixels.
	float x1 = shape[0].corners[0], x2 = x1, y1 = shape[0].corners[1], y2 = y1;
	for (size_t q = 0; q < shape.size (); q ++) {
		for (int c = 0; c < 4; c ++) {
			x1 = std::min (x1, shape[q].corners[c * 2]);
			x2 = std::max (x2, shape[q].corners[c * 2]);
			y1 = std::min (y1, shape[q].corners[c * 2 + 1]);
			y2 = std::max (y2, shape[q].corners[c * 2 + 1]);
		}
	}
	float t = fieldThickness (thickness);
	int pad = (int) ceilf (t) + 2;
	int px1 = (int) floorf (x1 * pixelsPerUnit) - pad, px2 = (int) ceilf (x2 * pixelsPerUnit) + pad;
	int py1 = (int) floorf (y1 * pixelsPerUnit) - pad, py2 = (int) ceilf (y2 * pixelsPerUnit) + pad;
	if (clip) {
		px1 = std::max (px1, - pad);
		py1 = std::max (py1, - pad);
		px2 = std::min (px2, viewportWidth + pad);
		py2 = std::min (py2, viewportHeight + pad);
	}
	if (px2 <= px1 || py2 <= py1 || px2 - px1 > maxFieldSize || py2 - py1 > maxFieldSize) return OUTLINE_NONE;

	sizeTextures (* f, px2 - px1, py2 - py1);
	f->box[0] = px1 / pixelsPerUnit;
	f->box[1] = py1 / pixelsPerUnit;
	f->box[2] = (px1 + f->width) / pixelsPerUnit;
	f->box[3] = (py1 + f->height) / pixelsPerUnit;

	gfx::renderer->setScissor (0, 0, 0, 0);
	gfx::renderer->setTarget (f->target[0]);
	gfx::renderer->setViewport (0, 0, f->width, f->height);
	gfx::renderer->clear (true, false);

	// 1. The silhouette: the box in the projection onto the field, the top
	// at y = 1, as the game's projection has it.
	gfx::DrawUniforms u;
	float w = f->box[2] - f->box[0], h = f->box[3] - f->box[1];
	u.matrix[0] = 2.0f / w;
	u.matrix[5] = -2.0f / h;
	u.matrix[12] = -1.0f - 2.0f * f->box[0] / w;
	u.matrix[13] = 1.0f + 2.0f * f->box[1] / h;
	u.antialias = gameSettings.antiAlias == 1;
	for (size_t q = 0; q < shape.size (); q ++) {
		const ShapeQuad & s = shape[q];
		float vertices[12];
		for (int c = 0; c < 4; c ++) {
			vertices[c * 3] = s.corners[c * 2];
			vertices[c * 3 + 1] = s.corners[c * 2 + 1];
			vertices[c * 3 + 2] = 0;
		}
		u.color[3] = s.alpha;
		gfx::draw (gfx::PipelineState (gfx::PROG_SMART_SCALER, gfx::BLEND_ALPHA), u, s.texture, vertices, s.texCoords);
	}

	// 2. The seeds, and 3. the jump flood, from a step at least as long as
	// the thickness down to 1, and 1 again, which mends the few pixels that
	// the flood gets wrong.
	fieldPass (* f, gfx::PROG_OUTLINE_SEED, f->texture[0], 1, 0);
	int step = 1;
	while (step < t + 2) step *= 2;
	int from = 1;
	for (bool again = true; step >= 1; ) {
		fieldPass (* f, gfx::PROG_OUTLINE_JUMP, f->texture[from], 3 - from, (float) step);
		from = 3 - from;
		if (step == 1 && again) again = false;
		else step /= 2;
	}
	f->seeds = from;
	f->valid = true;
	return OUTLINE_MADE;
}

void drawOutline (const void * key, const std::vector<ShapeQuad> & shape, const OutlineStyle & style, float opacity,
		float z, bool depthTest, float dx, float dy) {
	Field * f = findField (key);
	if (! f || ! f->valid || f->thickness != style.thickness || ! (f->shape == shape)) return;
	f->used = true;
	if (opacity <= 0 || ! style.colour[3]) return;

	gfx::DrawUniforms u = engineUniforms ();
	for (int c = 0; c < 4; c ++) u.color[c] = style.colour[c] / 255.0f;
	u.color[3] *= opacity;
	u.outline[0] = fieldThickness (style.thickness);
	u.outline[1] = opacity >= 1 ? 1.0f : 0.0f;
	u.outline[2] = (float) f->width;
	u.outline[3] = (float) f->height;

	float x1 = f->box[0] + dx, y1 = f->box[1] + dy, x2 = f->box[2] + dx, y2 = f->box[3] + dy;
	const float vertices[] = {
		x1, y1, z,
		x2, y1, z,
		x1, y2, z,
		x2, y2, z
	};
	const float texCoords[] = {
		0, 1,
		1, 1,
		0, 0,
		1, 0
	};
	gfx::Texture * const textures[3] = { f->texture[f->seeds], f->texture[0], NULL };
	gfx::Quad quad = { vertices, { texCoords, NULL, NULL } };
	gfx::renderer->draw (gfx::PipelineState (gfx::PROG_OUTLINE, gfx::BLEND_ALPHA, depthTest ? gfx::DEPTH_TEST : gfx::DEPTH_OFF), u, textures, quad);
}

void releaseUnusedOutlines () {
	for (size_t i = fields.size (); i -- > 0;) {
		if (fields[i].used) {
			fields[i].used = false;
		} else {
			releaseTextures (fields[i]);
			fields.erase (fields.begin () + i);
		}
	}
}
