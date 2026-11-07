// Drawing characters of parts (rigs, see animpose.h): each part's sprite as
// a quad through its world transform, back to front. Characters of one
// sprite draw through scaleSprite (sprites.cpp), which this file leaves as
// it was, so that old games draw as they always have. The z-buffer and the
// light map are as scaleSprite has them, for the character as a whole.

#include <math.h>

#include "allfiles.h"
#include "people.h"
#include "sprites.h"
#include "backdrop.h"
#include "zbuffer.h"
#include "graphics.h"
#include "spritebankdecode.h"
#include "language.h"

extern zBufferData zBuffer;
extern int cameraX, cameraY;
extern float cameraZoom;
extern unsigned char curLight[3];
void setDrawMode (const onScreenPerson * thisPerson);	// In sprites.cpp

// Where the character's parts go on screen: its place (less the camera, or
// on the screen for FIXTOSCREEN), its scale, and its float, as scaleSprite
// places a sprite's hotspot.
static Transform2D screenTransform (const onScreenPerson * p) {
	float scale = p -> scale;
	float x = p -> x, y = p -> y;
	if (p -> extra & EXTRA_FIXTOSCREEN) {
		x /= cameraZoom;
		y /= cameraZoom;
		scale /= cameraZoom;
	} else {
		x -= cameraX;
		y -= cameraY;
	}
	return Transform2D::translation (x, y) * Transform2D::scaling (scale) * Transform2D::translation (0, (float) p -> floaty);
}

bool drawRiggedPerson (const onScreenPerson * p) {
	if (p -> scale <= 0.05 || p -> pose.drawOrder.empty ()) return false;
	bool light = ! (p -> extra & EXTRA_NOLITE);

	// The panel of the z-buffer that the character is behind, by its feet.
	GLfloat z;
	bool behindZBuffer = (! (p -> extra & EXTRA_NOZB)) && zBuffer.numPanels;
	if (behindZBuffer) {
		int i;
		for (i = 1; i < zBuffer.numPanels; i ++) {
			if (zBuffer.panel[i] >= p -> y) {
				i --;
				break;
			}
		}
		z = 0.999 - (double) i * (1.0 / 128.0);
	} else {
		z = -0.5;
	}

	// The light of the map at its feet, or on each pixel.
	if (light && lightMap.data) {
		if (lightMapMode == LIGHTMAPMODE_HOTSPOT) {
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
	} else {
		curLight[0] = curLight[1] = curLight[2] = 255;
	}
	setDrawMode (p);

	gfx::DrawUniforms uniforms = engineUniforms ();
	uniforms.useLightTexture = light && lightMapMode == LIGHTMAPMODE_PIXEL && lightMap.data;
	uniforms.antialias = gameSettings.antiAlias == 1;

	Transform2D screen = screenTransform (p);
	for (size_t i = 0; i < p -> pose.drawOrder.size (); i ++) {
		const PosedPart & part = p -> pose.parts[p -> pose.drawOrder[i]];
		const spriteBank & bank = part.sprite.bank -> bank;
		const sprite & s = bank.sprites[part.sprite.number];
		const AtlasPage & page = bank.pages[s.texNum];
		spritebank::TexRect r = spritebank::texRect (s.tex_x, s.tex_y, s.width, s.height, page.w, page.h, spritebank::TEXKIND_SCALE);

		float c[8];
		spriteCorners (screen * part.world, s.width, s.height, s.xhot, s.yhot, part.sprite.mirror, c);
		const GLfloat vertices[] = {
			c[0], c[1], z,
			c[2], c[3], z,
			c[4], c[5], z,
			c[6], c[7], z
		};
		const GLfloat texCoords[] = {
			r.u1, r.v1,
			r.u2, r.v1,
			r.u1, r.v2,
			r.u2, r.v2
		};
		// The light map at each corner, as scaleSprite has it.
		GLfloat ltexCoords[8];
		for (int k = 0; k < 4; k ++) {
			float wx = c[k * 2] + cameraX, wy = c[k * 2 + 1] + cameraY;
			if (! NPOT_textures) {
				ltexCoords[k * 2] = lightMap.texW * wx / sceneWidth;
				ltexCoords[k * 2 + 1] = lightMap.texH * wy / sceneHeight;
			} else {
				ltexCoords[k * 2] = wx / sceneWidth;
				ltexCoords[k * 2 + 1] = wy / sceneHeight;
			}
		}

		gfx::Texture * const textures[3] = { page.tex.get (), uniforms.useLightTexture ? lightMap.tex : NULL, NULL };
		gfx::Quad quad = { vertices, { texCoords, ltexCoords, NULL } };
		gfx::renderer->draw (gfx::PipelineState (gfx::PROG_SMART_SCALER, gfx::BLEND_ALPHA, gfx::DEPTH_TEST), uniforms, textures, quad);
	}
	setSecondaryColor (0., 0., 0., 1.);

	// Characters of parts can't be pointed at yet.
	return false;
}
