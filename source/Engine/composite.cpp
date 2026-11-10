// Drawing characters of parts (rigs, see animpose.h): each part's sprite as
// a quad through its world transform, back to front. Characters of one
// sprite draw through scaleSprite and paste through fixScaleSprite
// (sprites.cpp), which this file leaves as they were, so that old games
// draw as they always have. The z-buffer and the light map are as those
// have them, for the character as a whole, and so is the mouse: a character
// of parts is one character, which the mouse is over when it is over a
// pixel of any of its parts.

#include <math.h>
#include <algorithm>

#include "allfiles.h"
#include "people.h"
#include "sprites.h"
#include "sludger.h"
#include "backdrop.h"
#include "zbuffer.h"
#include "graphics.h"
#include "spritebankdecode.h"
#include "language.h"

extern zBufferData zBuffer;
extern inputType input;
extern int cameraX, cameraY;
extern float cameraZoom;
extern unsigned char curLight[3];
void setDrawMode (const onScreenPerson * thisPerson);	// In sprites.cpp

// Where the character's parts go: its place, its scale, and its float, as
// scaleSprite places a sprite's hotspot. On screen, less the camera, or for
// FIXTOSCREEN as the screen is zoomed; in the scene, for pasting.
static Transform2D personTransform (const onScreenPerson * p, bool onScreen) {
	float scale = p -> scale;
	float x = p -> x, y = p -> y;
	if (onScreen && (p -> extra & EXTRA_FIXTOSCREEN)) {
		x /= cameraZoom;
		y /= cameraZoom;
		scale /= cameraZoom;
	} else if (onScreen) {
		x -= cameraX;
		y -= cameraY;
	}
	return Transform2D::translation (x, y) * Transform2D::scaling (scale) * Transform2D::translation (0, (float) p -> floaty);
}

// The panel of the z-buffer that the character is behind, by its feet, and
// its depth; -1 and in front of the z-buffer when it isn't behind one.
static int zPanel (const onScreenPerson * p, GLfloat & z) {
	if ((p -> extra & EXTRA_NOZB) || ! zBuffer.numPanels) {
		z = -0.5;
		return -1;
	}
	int i;
	for (i = 1; i < zBuffer.numPanels; i ++) {
		if (zBuffer.panel[i] >= p -> y) {
			i --;
			break;
		}
	}
	z = 0.999 - (double) i * (1.0 / 128.0);
	return i;
}

// The light of the map at the character's feet (HOTSPOT), or white.
static void lightAtFeet (const onScreenPerson * p) {
	bool light = ! (p -> extra & EXTRA_NOLITE);
	if (light && lightMap.data && lightMapMode == LIGHTMAPMODE_HOTSPOT) {
		int lx = (int) p -> x, ly = (int) p -> y;
		if (lx < 0) lx = 0;
		else if (lx >= (int) sceneWidth) lx = sceneWidth - 1;
		if (ly < 0) ly = 0;
		else if (ly >= (int) sceneHeight) ly = sceneHeight - 1;
		GLubyte * target = lightMap.data + (ly * (NPOT_textures ? sceneWidth : getNextPOT (sceneWidth)) + lx) * 4;
		curLight[0] = target[0];
		curLight[1] = target[1];
		curLight[2] = target[2];
	} else {
		curLight[0] = curLight[1] = curLight[2] = 255;
	}
}

// Coordinates of a point of the scene in a texture of the scene's size (the
// light map, the backdrop), as the engine has them for textures of a power
// of two or not.
static void sceneTexCoords (float x, float y, float texW, float texH, float & u, float & v) {
	if (! NPOT_textures) {
		u = texW * x / sceneWidth;
		v = texH * y / sceneHeight;
	} else {
		u = x / sceneWidth;
		v = y / sceneHeight;
	}
}

// A part's sprite, its quad through transform m, and its texture rectangle.
struct PartQuad {
	const sprite * s;
	const AtlasPage * page;
	float corners[8];
	spritebank::TexRect r;
	float colour[4];	// The part's (PosedPart::colour)
};

static PartQuad partQuad (const PosedPart & part, const Transform2D & m) {
	PartQuad q;
	for (int c = 0; c < 4; c ++) q.colour[c] = part.colour[c];
	const spriteBank & bank = engineBank (part.sprite.bank) -> bank;
	q.s = & bank.sprites[part.sprite.number];
	q.page = & bank.pages[q.s -> texNum];
	q.r = spritebank::texRect (q.s -> tex_x, q.s -> tex_y, q.s -> width, q.s -> height, q.page -> w, q.page -> h, spritebank::TEXKIND_SCALE);
	spriteCorners (m * part.spriteWorld, q.s -> width, q.s -> height, q.s -> xhot, q.s -> yhot, part.sprite.mirror, q.corners);
	return q;
}

// Whether the mouse is over a pixel of the part, not behind the z-buffer.
static bool mouseOverPart (const PosedPart & part, const Transform2D & m, int panel) {
	const sprite & s = engineBank (part.sprite.bank) -> bank.sprites[part.sprite.number];
	if (s.alpha.empty ()) return false;
	Transform2D t = m * part.spriteWorld, inv;
	if (part.sprite.mirror) t = t * Transform2D::flipX ();
	if (! t.inverse (inv)) return false;
	float lx, ly;
	inv.apply (input.mouseX + 0.5f, input.mouseY + 0.5f, lx, ly);
	int sx = (int) floorf (lx + s.xhot), sy = (int) floorf (ly + s.yhot);
	if (sx < 0 || sy < 0 || sx >= s.width || sy >= s.height) return false;
	if (! s.alpha[sy * s.width + sx]) return false;
	if (panel >= 0) {
		int p = zBufferPanelAt (input.mouseX + cameraX, input.mouseY + cameraY);
		if (p > zBuffer.numPanels - 1) p = zBuffer.numPanels - 1;
		if (p > panel) return false;
	}
	return true;
}

bool drawRiggedPerson (const onScreenPerson * p) {
	if (p -> scale <= 0.05 || p -> pose.drawOrder.empty ()) return false;
	bool light = ! (p -> extra & EXTRA_NOLITE);
	GLfloat z;
	int panel = zPanel (p, z);
	lightAtFeet (p);
	setDrawMode (p);

	gfx::DrawUniforms uniforms = engineUniforms ();
	uniforms.useLightTexture = light && lightMapMode == LIGHTMAPMODE_PIXEL && lightMap.data;
	uniforms.antialias = gameSettings.antiAlias == 1;

	Transform2D screen = personTransform (p, true);
	float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
	bool over = false;
	for (size_t i = 0; i < p -> pose.drawOrder.size (); i ++) {
		const PosedPart & part = p -> pose.parts[p -> pose.drawOrder[i]];
		PartQuad q = partQuad (part, screen);
		const float * c = q.corners;
		const GLfloat vertices[] = {
			c[0], c[1], z,
			c[2], c[3], z,
			c[4], c[5], z,
			c[6], c[7], z
		};
		const GLfloat texCoords[] = {
			q.r.u1, q.r.v1,
			q.r.u2, q.r.v1,
			q.r.u1, q.r.v2,
			q.r.u2, q.r.v2
		};
		// The light map at each corner, as scaleSprite has it.
		GLfloat ltexCoords[8];
		for (int k = 0; k < 4; k ++) {
			sceneTexCoords (c[k * 2] + cameraX, c[k * 2 + 1] + cameraY, lightMap.texW, lightMap.texH, ltexCoords[k * 2], ltexCoords[k * 2 + 1]);
			if (i == 0 && k == 0) {
				x1 = x2 = c[0];
				y1 = y2 = c[1];
			}
			x1 = std::min (x1, c[k * 2]);
			x2 = std::max (x2, c[k * 2]);
			y1 = std::min (y1, c[k * 2 + 1]);
			y2 = std::max (y2, c[k * 2 + 1]);
		}

		gfx::Texture * const textures[3] = { q.page -> tex.get (), uniforms.useLightTexture ? lightMap.tex : NULL, NULL };
		gfx::Quad quad = { vertices, { texCoords, ltexCoords, NULL } };
		// The part's own colour, over the character's.
		gfx::DrawUniforms tinted = uniforms;
		for (int c = 0; c < 4; c ++) tinted.tint[c] = q.colour[c];
		gfx::renderer->draw (gfx::PipelineState (gfx::PROG_SMART_SCALER, gfx::BLEND_ALPHA, gfx::DEPTH_TEST), tinted, textures, quad);

		// A part faded away entirely isn't under the mouse.
		if (! over && ! (p -> extra & EXTRA_RECTANGULAR) && q.colour[3] > 0) over = mouseOverPart (part, screen, panel);
	}
	setSecondaryColor (0., 0., 0., 1.);

	// RECTANGULAR: anywhere in the box around all its parts.
	if (p -> extra & EXTRA_RECTANGULAR) {
		over = input.mouseX >= x1 && input.mouseX <= x2 && input.mouseY >= y1 && input.mouseY <= y2;
	}
	return over;
}

// Paste one part into the backdrop, as fixScaleSprite pastes a sprite: tile
// by tile of the box around it, the backdrop as it is, then the z-buffer,
// then the part, blended with the backdrop by the paste program. Each part
// is pasted on its own, so that it blends with the parts before it.
static void pastePart (const onScreenPerson * p, const PartQuad & q, GLfloat z, bool useZB) {
	bool light = ! (p -> extra & EXTRA_NOLITE);
	const float * c = q.corners;
	float bx1 = c[0], bx2 = c[0], by1 = c[1], by2 = c[1];
	for (int k = 1; k < 4; k ++) {
		bx1 = std::min (bx1, c[k * 2]);
		bx2 = std::max (bx2, c[k * 2]);
		by1 = std::min (by1, c[k * 2 + 1]);
		by2 = std::max (by2, c[k * 2 + 1]);
	}
	int x1 = (int) floorf (bx1), y1 = (int) floorf (by1);
	int diffX = (int) ceilf (bx2) - x1, diffY = (int) ceilf (by2) - y1;
	if (x1 < 0) diffX += x1;
	if (y1 < 0) diffY += y1;
	if (x1 + diffX > (int) sceneWidth) diffX = sceneWidth - x1;
	if (y1 + diffY > (int) sceneHeight) diffY = sceneHeight - y1;
	if (diffX <= 0 || diffY <= 0) return;

	GLfloat ltexCoords[8], btexCoords[8];
	for (int k = 0; k < 4; k ++) {
		sceneTexCoords (c[k * 2], c[k * 2 + 1], lightMap.texW, lightMap.texH, ltexCoords[k * 2], ltexCoords[k * 2 + 1]);
		sceneTexCoords (c[k * 2], c[k * 2 + 1], backdropTexW, backdropTexH, btexCoords[k * 2], btexCoords[k * 2 + 1]);
	}
	const GLfloat texCoords[] = {
		q.r.u1, q.r.v1,
		q.r.u2, q.r.v1,
		q.r.u1, q.r.v2,
		q.r.u2, q.r.v2
	};

	setPixelCoords (true);
	for (int xoffset = 0; xoffset < diffX; xoffset += viewportWidth) {
		int w = std::min (diffX - xoffset, viewportWidth);
		for (int yoffset = 0; yoffset < diffY; yoffset += viewportHeight) {
			int h = std::min (diffY - yoffset, viewportHeight);
			// As fixScaleSprite: we draw from the box's corner (ox, oy), which
			// may lie outside the scene, and copy the tile from there.
			int ox = x1 + xoffset, oy = y1 + yoffset;

			// First the old backdrop, so that it shows through where the
			// z-buffer is in front.
			gfx::renderer->setTextureFilter (backdropTex, gfx::FILTER_NEAREST, gfx::FILTER_NEAREST);
			gfx::Texture * copy = copyOfBackdrop ();
			drawIntoTile (backdropTarget, x1 < 0 ? xoffset : ox, y1 < 0 ? yoffset : oy, x1 < 0 ? -x1 : 0, y1 < 0 ? -y1 : 0, w, h);
			const GLfloat vertices[] = {
				(GLfloat) - ox, (GLfloat) - oy, 0.0f,
				(GLfloat) sceneWidth - ox, (GLfloat) - oy, 0.0f,
				(GLfloat) - ox, (GLfloat) sceneHeight - oy, 0.0f,
				(GLfloat) sceneWidth - ox, (GLfloat) sceneHeight - oy, 0.0f
			};
			const GLfloat backTexCoords[] = {
				0.0f, 0.0f,
				backdropTexW, 0.0f,
				0.0f, backdropTexH,
				backdropTexW, backdropTexH
			};
			gfx::draw (gfx::PipelineState (gfx::PROG_TEXTURE), engineUniforms (), copy, vertices, backTexCoords);

			if (useZB) drawZBuffer (ox, oy);

			setDrawMode (p);
			const GLfloat vertices2[] = {
				c[0] - ox, c[1] - oy, z,
				c[2] - ox, c[3] - oy, z,
				c[4] - ox, c[5] - oy, z,
				c[6] - ox, c[7] - oy, z
			};
			gfx::DrawUniforms uniforms = engineUniforms ();
			uniforms.useLightTexture = light && lightMapMode == LIGHTMAPMODE_PIXEL && lightMap.data;
			for (int c = 0; c < 4; c ++) uniforms.tint[c] = q.colour[c];
			gfx::Texture * const textures[3] = { q.page -> tex.get (), uniforms.useLightTexture ? lightMap.tex : NULL, copy };
			gfx::Quad quad = { vertices2, { texCoords, ltexCoords, btexCoords } };
			gfx::renderer->draw (gfx::PipelineState (gfx::PROG_PASTE, gfx::BLEND_OFF, useZB ? gfx::DEPTH_TEST : gfx::DEPTH_OFF), uniforms, textures, quad);
			setSecondaryColor (0., 0., 0., 1.);
		}
	}
	drawIntoWindow ();
	setPixelCoords (false);
}

void pasteRiggedPerson (const onScreenPerson * p) {
	if (p -> scale <= 0.05) return;
	GLfloat z;
	bool useZB = zPanel (p, z) >= 0;
	lightAtFeet (p);
	Transform2D scene = personTransform (p, false);
	for (size_t i = 0; i < p -> pose.drawOrder.size (); i ++) {
		pastePart (p, partQuad (p -> pose.parts[p -> pose.drawOrder[i]], scene), z, useZB);
	}
}
