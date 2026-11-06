#include <string>
#include <vector>

#include <string.h>
#include <math.h>
#include <stdlib.h>

#include "allfiles.h"

#include "fileset.h"
#include "people.h"
#include "sprites.h"
#include "moreio.h"
#include "newfatal.h"
#include "colours.h"
#include "backdrop.h"
#include "sludger.h"
#include "zbuffer.h"
#include "debug.h"
#include "graphics.h"
#include "objtypes.h"

#include "shaders.h"
#include "spritebankdecode.h"

extern zBufferData zBuffer;

extern inputType input;
extern int cameraX, cameraY;
extern float cameraZoom;

unsigned char currentBurnR = 0, currentBurnG = 0, currentBurnB = 0;

// Draws of text and the mouse cursor in this frame, for SLUDGE_RENDER_STATS.
unsigned int fontSpriteDraws = 0;


size_t spriteBank::gpuBytes () const {
	size_t n = 0;
	for (size_t i = 0; i < pages.size (); i ++) n += pages[i].gpuBytes;
	return n;
}

// Whether to give the gutters around sprites the colour of their edges: as
// SLUDGE_SPRITE_EDGES says (for tests), else as SPRITEEDGES in the .ini
// says, else as the game says (from its project in the Dev Kit).
extern bool gameExtrudesSpriteEdges;
bool spriteEdgesExtruded () {
	const char * env = getenv ("SLUDGE_SPRITE_EDGES");
	if (env) return atoi (env) != 0;
	if (gameSettings.spriteEdges >= 0) return gameSettings.spriteEdges != 0;
	return gameExtrudesSpriteEdges;
}

// With SLUDGE_SPRITEBANK_LOG, name the sprites that look different from
// before 2.3.2: the first sprite of each texture after the first lost its last
// row and had no gap to its left in the old layout.
static void logOldLayoutChanges (int fileNum, const spritebank::DecodedBank & decoded) {
	if (! getenv ("SLUDGE_SPRITEBANK_LOG")) return;
	spritebank::Layout old;
	std::string error;
	if (! spritebank::layoutOneRow (decoded, false, old, error)) return;
	for (size_t i = 0; i < decoded.sprites.size (); i ++) {
		if (old.texNum[i] > 0 && old.tex_x[i] == 0) {
			fprintf (stderr, "Sprite bank %d: sprite %d (%d x %d) now shows its last row and left gap\n",
				fileNum, (int) i, decoded.sprites[i].width, decoded.sprites[i].height);
		}
	}
}

bool loadSpriteBank (int fileNum, spriteBank & loadhere, bool isFont) {
	std::string error;
	spritebank::DecodedBank decoded;
	spritebank::Layout layout;
	spritebank::Pages pages;

	setResourceForFatal (fileNum);
	unsigned int length = openFileFromNum (fileNum);
	if (! length) return fatal ("Can't open sprite bank / font");

	bool ok;
	{
		// The whole resource, so that the decoder can check every offset.
		std::vector<unsigned char> bytes (length);
		ok = fread (bytes.data (), 1, length, bigDataFile) == length;
		finishAccess ();
		if (! ok) {
			error = "Can't open sprite bank / font - the file ends too soon.";
		} else {
			spritebank::Reader reader (bytes.data (), bytes.size ());
			ok = spritebank::decode (reader, isFont, decoded, error);
		}
	}
	ok = ok && spritebank::layout (decoded, ! NPOT_textures, layout, error);
	ok = ok && spritebank::makePages (decoded, layout, spriteEdgesExtruded (), pages, error);
	if (! ok) return fatal (error.c_str ());
	logOldLayoutChanges (fileNum, decoded);

	int total = (int) decoded.sprites.size ();

	// Whatever the bank held before goes now.
	spriteBank bank;
	bank.isFont = isFont;
	bank.total = total;
	bank.paletteSize = decoded.paletteSize;
	bank.sprites.resize (total);

	for (int i = 0; i < total; i ++) {
		spritebank::DecodedSprite & from = decoded.sprites[i];
		sprite & sp = bank.sprites[i];
		sp.width = from.width;
		sp.height = from.height;
		sp.xhot = from.xhot;
		sp.yhot = from.yhot;
		sp.tex_x = layout.tex_x[i];
		sp.tex_y = layout.tex_y[i];
		sp.texNum = layout.texNum[i];
		// We keep the alpha of the sprites, but not of fonts, to find out
		// which character the mouse is over.
		if (! isFont) {
			size_t size = (size_t) sp.width * sp.height;
			sp.alpha.resize (size);
			for (size_t p = 0; p < size; p ++) sp.alpha[p] = from.rgba[p * 4 + 3];
		}
		std::vector<unsigned char> ().swap (from.rgba);
	}

	gfx::Filter filter = gameSettings.antiAlias < 0 ? gfx::FILTER_LINEAR : gfx::FILTER_NEAREST;
	uploadPages (* gfx::renderer, layout, pages, isFont, filter, bank.pages);

	loadhere = std::move (bank);
	setResourceForFatal (-1);

	return true;
}

void pasteSpriteToBackDrop (int x1, int y1, const sprite & single, const spriteBank & bank, const spritePalette & fontPal) {

	const AtlasPage & page = bank.pages[single.texNum];
	spritebank::TexRect r = spritebank::texRect (single.tex_x, single.tex_y, single.width, single.height, page.w, page.h, spritebank::TEXKIND_PASTE);
	float tx1 = r.u1;
	float ty1 = r.v1;
	float tx2 = r.u2;
	float ty2 = r.v2;

	float btx1;
	float btx2;
	float bty1;
	float bty2;

	int diffX = single.width;
	int diffY = single.height;

	x1 -= single.xhot;
	y1 -= single.yhot;

	if (! NPOT_textures) {
		btx1 = backdropTexW * x1 / sceneWidth;
		btx2 = backdropTexW * (x1+single.width) / sceneWidth;
		bty1 = backdropTexH * y1 / sceneHeight;
		bty2 = backdropTexH * (y1+single.height) / sceneHeight;
	} else {
		btx1 = (float) x1 / sceneWidth;
		btx2 = (float) (x1+single.width) / sceneWidth;
		bty1 = (float) y1 / sceneHeight;
		bty2 = (float) (y1+single.height) / sceneHeight;
	}

	const GLfloat btexCoords[] = { 
		btx1, bty1,
		btx2, bty1,
		btx1, bty2,
		btx2, bty2
	}; 

	if (x1 < 0) diffX += x1;
	if (y1 < 0) diffY += y1;
	if (x1 + diffX > sceneWidth) diffX = sceneWidth - x1;
	if (y1 + diffY > sceneHeight) diffY = sceneHeight - y1;
	if (diffX < 0) return;
	if (diffY < 0) return;

	setPixelCoords (true);

	int xoffset = 0;
	while (xoffset < diffX) {
		int w = (diffX-xoffset < viewportWidth) ? diffX-xoffset : viewportWidth;

		int yoffset = 0;
		while (yoffset < diffY) {
			int h = (diffY-yoffset < viewportHeight) ? diffY-yoffset : viewportHeight;

			// Render the sprite to the backdrop
			// (using mulitexturing, so the backdrop is seen where alpha < 1.0)
			gfx::Texture * copy = copyOfBackdrop ();
			drawIntoTile (backdropTarget, (x1<0) ? xoffset : x1+xoffset, (y1<0) ? yoffset : y1+yoffset, (x1<0) ? -x1 : 0, (y1<0) ? -y1 : 0, w, h);

			setPrimaryColor(fontPal.originalRed/255.f, fontPal.originalGreen/255.f, fontPal.originalBlue/255.f, 1.0f);

			const GLfloat vertices[] = { 
				(GLfloat)-xoffset, (GLfloat)-yoffset, 0.,
				(GLfloat)single.width-xoffset, (GLfloat)-yoffset, 0.,
				(GLfloat)-xoffset, (GLfloat)single.height-yoffset, 0.,
				(GLfloat)single.width-xoffset, (GLfloat)single.height-yoffset, 0.
			};

			const GLfloat texCoords[] = { 
				tx1, ty1,
				tx2, ty1,
				tx1, ty2,
				tx2, ty2
			}; 

			gfx::Texture * const textures[3] = { bank.pages[single.texNum].tex.get (), NULL, copy };
			gfx::Quad quad = { vertices, { texCoords, NULL, btexCoords } };
			gfx::renderer->draw (gfx::PipelineState (gfx::PROG_PASTE), engineUniforms (), textures, quad);

			yoffset += viewportHeight;
		}
		xoffset += viewportWidth;
	}
	drawIntoWindow ();
	setPixelCoords (false);
}

void burnSpriteToBackDrop (int x1, int y1, const sprite & single, const spriteBank & bank) {

	const AtlasPage & page = bank.pages[single.texNum];
	spritebank::TexRect r = spritebank::texRect (single.tex_x, single.tex_y, single.width, single.height, page.w, page.h, spritebank::TEXKIND_BURN);
	float tx1 = r.u1;
	float ty1 = r.v1;
	float tx2 = r.u2;
	float ty2 = r.v2;

	const GLfloat spriteTexCoords[] = { 
		tx1, ty1,
		tx2, ty1,
		tx1, ty2,
		tx2, ty2
	}; 

	x1 -= single.xhot;
	y1 -= single.yhot-1;

	float bx1 = (float)x1*backdropTexW/sceneWidth;
	float by1 = (float)y1*backdropTexH/sceneHeight;
	float bx2 = (float)(x1+single.width-1)*backdropTexW/sceneWidth;
	float by2 = (float)(y1+single.height-1)*backdropTexH/sceneHeight;

	const GLfloat backdropTexCoords[] = { 
		bx1, by1,
		bx2, by1,
		bx1, by2,
		bx2, by2
	};

	const GLfloat backdropTexCoords2[] = { 
		0.0f, 0.0f,
		backdropTexW, 0.0f,
		0.0f, backdropTexH,
		backdropTexW, backdropTexH
	}; 

	int diffX = single.width+1;
	int diffY = single.height+2;

	if (x1 < 0) diffX += x1;
	if (y1 < 0) diffY += y1;
	if (x1 + diffX > sceneWidth) diffX = sceneWidth - x1;
	if (y1 + diffY > sceneHeight) diffY = sceneHeight - y1;
	if (diffX < 0) return;
	if (diffY < 0) return;

	setPixelCoords (true);
	setPrimaryColor(currentBurnR/255.f, currentBurnG/255.f, currentBurnB/255.f, 1.0f);

	GLfloat xoffset = 0.0f;
	while (xoffset < diffX) {
		int w = (diffX-xoffset < viewportWidth) ? diffX-xoffset : viewportWidth;

		GLfloat yoffset = 0.0f;
		while (yoffset < diffY) {
			int h = (diffY-yoffset < viewportHeight) ? diffY-yoffset : viewportHeight;

			const GLfloat backdropVertices[] = { 
				-x1-xoffset, -y1+yoffset, 0.0f, 
				sceneWidth-1.0f-x1-xoffset, -y1+yoffset, 0.0f, 
				-x1-xoffset, sceneHeight-1.0f-y1+yoffset, 0.0f,
				sceneWidth-1.0f-x1-xoffset, sceneHeight-1.0f-y1+yoffset, 0.0f
			};

			const GLfloat spriteVertices[] = { 
				-xoffset, -yoffset, 0.0f, 
				single.width-1-xoffset, -yoffset, 0.0f, 
				-xoffset, single.height-1-yoffset, 0.0f,
				single.width-1-xoffset, single.height-1-yoffset, 0.0f
			};

			gfx::Texture * copy = copyOfBackdrop ();
			drawIntoTile (backdropTarget, (x1<0) ? xoffset : x1+xoffset, (y1<0) ? yoffset : y1+yoffset, 0, 0, w, h);

			gfx::draw (gfx::PipelineState (gfx::PROG_TEXTURE), engineUniforms (), copy, backdropVertices, backdropTexCoords2);

//FIXME: Test this some more. Also pasting the backdrop again is not strictly necessary but allows using the paste shader.
			gfx::Texture * const textures[3] = { bank.pages[single.texNum].burn.get (), NULL, copy };
			gfx::Quad quad = { spriteVertices, { spriteTexCoords, NULL, backdropTexCoords } };
			gfx::renderer->draw (gfx::PipelineState (gfx::PROG_PASTE), engineUniforms (), textures, quad);

			yoffset += viewportHeight;
		}
		xoffset += viewportWidth;
	}
	drawIntoWindow ();
	setPixelCoords (false);
}


static void fontSprite (bool flip, int x, int y, const sprite & single, const spriteBank & bank, const spritePalette & fontPal) {
	fontSpriteDraws ++;

	const AtlasPage & page = bank.pages[single.texNum];
	spritebank::TexRect r = spritebank::texRect (single.tex_x, single.tex_y, single.width, single.height, page.w, page.h, (flip ? spritebank::TEXKIND_FONT_FLIP : spritebank::TEXKIND_FONT));
	float tx1 = r.u1;
	float ty1 = r.v1;
	float tx2 = r.u2;
	float ty2 = r.v2;

	float x1 = (float)x - (float)single.xhot/cameraZoom;
	float y1 = (float)y - (float)single.yhot/cameraZoom;
	float x2 = x1 + (float)single.width/cameraZoom;
	float y2 = y1 + (float)single.height/cameraZoom;

	GLfloat vertices[] = { 
		x1, y1, 0.0f, 
		x2, y1, 0.0f, 
		x1, y2, 0.0f,
		x2, y2, 0.0f
	};
	if (flip) {
		vertices[0] = x2;
		vertices[3] = x1;
		vertices[6] = x2;
		vertices[9] = x1;
	}

	const GLfloat texCoords[] = { 
		tx1, ty1,
		tx2, ty1,
		tx1, ty2,
		tx2, ty2
	}; 

	//glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); // GL_MODULATE instead of decal mixes the colours!
	setPrimaryColor(fontPal.originalRed/255.f, fontPal.originalGreen/255.f, fontPal.originalBlue/255.f, 1.0f);

	gfx::DrawUniforms uniforms = engineUniforms ();
	uniforms.antialias = gameSettings.antiAlias == 1;
	gfx::draw (gfx::PipelineState (gfx::PROG_SMART_SCALER, gfx::BLEND_ALPHA), uniforms, bank.pages[single.texNum].tex.get (), vertices, texCoords);
}

void fontSprite (int x, int y, const sprite & single, const spriteBank & bank, const spritePalette & fontPal) {
	fontSprite (false, x, y, single, bank, fontPal);
}

void flipFontSprite (int x, int y, const sprite & single, const spriteBank & bank, const spritePalette & fontPal) {
	fontSprite (true, x, y, single, bank, fontPal);
}




unsigned char curLight[3];

void setDrawMode (const onScreenPerson * thisPerson) {
	if (thisPerson->colourmix) {
		//glEnable(GL_COLOR_SUM); FIXME: replace line?
		setSecondaryColor(curLight[0]*thisPerson->r*thisPerson->colourmix/65025/255.f, curLight[1]*thisPerson->g*thisPerson->colourmix/65025/255.f, curLight[2]*thisPerson->b*thisPerson->colourmix/65025/255.f, 1.0f);
	}

	//glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	setPrimaryColor(curLight[0]*(255-thisPerson->colourmix)/65025.f, curLight[1]*(255-thisPerson->colourmix)/65025.f, curLight[2]*(255-thisPerson->colourmix)/65025.f, 1.0f - thisPerson->transparency/255.f);
}


// Is the mouse over an opaque pixel of a sprite that we draw from (x1, y1)
// to (x2, y2), and not over a part of the z-buffer in front of it? We take
// the pixel that the centre of the pixel under the mouse samples, as
// nearest filtering does. Characters on z-buffer panel `panel` are behind
// the parts of the z-buffer of higher panels.
static bool mouseOverSprite (const sprite & single, float x1, float y1, float x2, float y2, bool mirror, bool behindZBuffer, int panel) {
	if (single.alpha.empty () || x2 <= x1 || y2 <= y1) return false;
	int sx = (int) floorf ((input.mouseX + 0.5f - x1) / (x2 - x1) * single.width);
	int sy = (int) floorf ((input.mouseY + 0.5f - y1) / (y2 - y1) * single.height);
	if (sx < 0 || sy < 0 || sx >= single.width || sy >= single.height) return false;
	if (mirror) sx = single.width - 1 - sx;
	if (! single.alpha[sy * single.width + sx]) return false;
	if (behindZBuffer) {
		int p = zBufferPanelAt (input.mouseX + cameraX, input.mouseY + cameraY);
		if (p > zBuffer.numPanels - 1) p = zBuffer.numPanels - 1;
		if (p > panel) return false;
	}
	return true;
}

bool scaleSprite (const sprite & single, const spriteBank & bank, const onScreenPerson * thisPerson, bool mirror) {

	float x = thisPerson->x;
	float y = thisPerson->y;

	float scale = thisPerson-> scale;
	bool light = ! (thisPerson->extra & EXTRA_NOLITE);

	if (scale <= 0.05) return false;

	const AtlasPage & page = bank.pages[single.texNum];
	spritebank::TexRect r = spritebank::texRect (single.tex_x, single.tex_y, single.width, single.height, page.w, page.h, spritebank::TEXKIND_SCALE);
	float tx1 = r.u1;
	float ty1 = r.v1;
	float tx2 = r.u2;
	float ty2 = r.v2;

	int diffX = (int)(((float)single.width) * scale);
	int diffY = (int)(((float)single.height) * scale);

	GLfloat x1, y1, x2, y2;

	if (thisPerson -> extra & EXTRA_FIXTOSCREEN) {
		x = x / cameraZoom;
		y = y / cameraZoom;
		if (single.xhot < 0)
			x1 = x - (int)((mirror ? (float) (single.width - single.xhot) : (float)(single.xhot+1) ) * scale/cameraZoom);
		else
			x1 = x - (int)((mirror ? (float) (single.width - (single.xhot+1)) : (float)single.xhot ) * scale / cameraZoom);
		y1 = y - (int)((single.yhot - thisPerson->floaty) * scale / cameraZoom);
		x2 = x1 + (int)(diffX / cameraZoom);
		y2 = y1 + (int)(diffY / cameraZoom);
	} else {
		x -= cameraX;
		y -= cameraY;
		if (single.xhot < 0)
			x1 = x - (int)((mirror ? (float) (single.width - single.xhot) : (float)(single.xhot+1) ) * scale);
		else
			x1 = x - (int)((mirror ? (float) (single.width - (single.xhot+1)) : (float)single.xhot ) * scale);
		y1 = y - (int)((single.yhot - thisPerson->floaty) * scale);
		x2 = x1 + diffX;
		y2 = y1 + diffY;
	}

	GLfloat z;
	bool behindZBuffer = (! (thisPerson->extra & EXTRA_NOZB)) && zBuffer.numPanels;
	int panel = 0;

	if (behindZBuffer) {
		int i;
		for (i = 1; i<zBuffer.numPanels; i++) {
			if (zBuffer.panel[i] >= y + cameraY) {
				i--;
				break;
			}
		}
		panel = i;
		z = 0.999 - (double) i * (1.0 / 128.0);
	} else {
		z = -0.5;
	}

	float ltx1, ltx2, lty1, lty2;
	if (! NPOT_textures) {
		ltx1 = lightMap.texW * (x1+cameraX) / sceneWidth;
		ltx2 = lightMap.texW * (x2+cameraX) / sceneWidth;
		lty1 = lightMap.texH * (y1+cameraY) / sceneHeight;
		lty2 = lightMap.texH * (y2+cameraY) / sceneHeight;
	} else {
		ltx1 = (float) (x1+cameraX) / sceneWidth;
		ltx2 = (float) (x2+cameraX) / sceneWidth;
		lty1 = (float) (y1+cameraY) / sceneHeight;
		lty2 = (float) (y2+cameraY) / sceneHeight;
	}

	const GLfloat ltexCoords[] = { 
		ltx1, lty1,
		ltx2, lty1,
		ltx1, lty2,
		ltx2, lty2
	}; 

	if (light && lightMap.data) {
		if (lightMapMode == LIGHTMAPMODE_HOTSPOT) {
			int lx=(int)(x+cameraX);
			int ly=(int)(y+cameraY);

			if (lx<0) lx = 0;
			else if (lx>=sceneWidth) lx = sceneWidth-1;
			if (ly<0) ly = 0;
			else if (ly>=sceneHeight) ly = sceneHeight-1;

			GLubyte *target;
			if (! NPOT_textures) {
				target = lightMap.data + (ly*getNextPOT(sceneWidth) + lx)*4;
			} else {
				target = lightMap.data + (ly*sceneWidth + lx)*4;
			}
			curLight[0] = target[0];
			curLight[1] = target[1];
			curLight[2] = target[2];
		} else if (lightMapMode == LIGHTMAPMODE_PIXEL) {
			curLight[0] = curLight[1] = curLight[2] = 255;
		}
	} else {
		curLight[0] = curLight[1] = curLight[2] = 255;
	}
	setDrawMode (thisPerson);

	gfx::DrawUniforms uniforms = engineUniforms ();
	uniforms.useLightTexture = light && lightMapMode == LIGHTMAPMODE_PIXEL && lightMap.data;
	uniforms.antialias = gameSettings.antiAlias == 1;

	const GLfloat vertices[] = { 
		x1, y1, z, 
		x2, y1, z, 
		x1, y2, z,
		x2, y2, z
	};

	if (! mirror) {
		GLfloat tx3 = tx1;
		tx1 = tx2;
		tx2 = tx3;
	}
	const GLfloat texCoords[] = { 
		tx2, ty1,
		tx1, ty1,
		tx2, ty2,
		tx1, ty2
	}; 

	// Characters are drawn with the depth test, behind the z-buffer.
	gfx::Texture * const textures[3] = { bank.pages[single.texNum].tex.get (), uniforms.useLightTexture ? lightMap.tex : NULL, NULL };
	gfx::Quad quad = { vertices, { texCoords, ltexCoords, NULL } };
	gfx::renderer->draw (gfx::PipelineState (gfx::PROG_SMART_SCALER, gfx::BLEND_ALPHA, gfx::DEPTH_TEST), uniforms, textures, quad);

	setSecondaryColor(0., 0., 0.,1.);
	//glDisable(GL_COLOR_SUM); FIXME: replace line?

	// Are we pointing at the sprite?
	if (input.mouseX >= x1 && input.mouseX <= x2 && input.mouseY >= y1 && input.mouseY <= y2) {
		if (thisPerson->extra & EXTRA_RECTANGULAR) return true;
		return mouseOverSprite (single, x1, y1, x2, y2, mirror, behindZBuffer, panel);
	}
	return false;
}

// Paste a scaled sprite onto the backdrop
void fixScaleSprite (int x, int y, const sprite & single, const spriteBank & bank, onScreenPerson * thisPerson, int camX, int camY, bool mirror) {

	float scale = thisPerson-> scale;
	bool useZB = ! (thisPerson->extra & EXTRA_NOZB);
	bool light = ! (thisPerson->extra & EXTRA_NOLITE);

	if (scale <= 0.05) return;

	const AtlasPage & page = bank.pages[single.texNum];
	spritebank::TexRect r = spritebank::texRect (single.tex_x, single.tex_y, single.width, single.height, page.w, page.h, spritebank::TEXKIND_SCALE);
	float tx1 = r.u1;
	float ty1 = r.v1;
	float tx2 = r.u2;
	float ty2 = r.v2;

	int diffX = (int)(((float)single.width) * scale);
	int diffY = (int)(((float)single.height) * scale);
	int x1;
	if (single.xhot < 0)
		x1 = x - (int)((mirror ? (float) (single.width - single.xhot) : (float)(single.xhot+1) ) * scale);
	else
		x1 = x - (int)((mirror ? (float) (single.width - (single.xhot+1)) : (float)single.xhot ) * scale);
	int y1 = y - (int)((single.yhot - thisPerson->floaty) * scale);

	float spriteWidth = diffX;
	float spriteHeight = diffY;
	if (x1 < 0) diffX += x1;
	if (y1 < 0) diffY += y1;
	if (x1 + diffX > sceneWidth) diffX = sceneWidth - x1;
	if (y1 + diffY > sceneHeight) diffY = sceneHeight - y1;
	if (diffX < 0) return;
	if (diffY < 0) return;

	GLfloat z;


	if (useZB && zBuffer.numPanels) {
		int i;
		for (i = 1; i<zBuffer.numPanels; i++) {
			if (zBuffer.panel[i] >= y + cameraY) {
				i--;
				break;
			}
		}
		z = 0.999 - (double) i * (1.0 / 128.0);
	} else {
		z = -0.5;
	}

	float ltx1, btx1;
	float ltx2, btx2;
	float lty1, bty1;
	float lty2, bty2;
	if (! NPOT_textures) {
		ltx1 = lightMap.texW * x1 / sceneWidth;
		ltx2 = lightMap.texW * (x1+spriteWidth) / sceneWidth;
		lty1 = lightMap.texH * y1 / sceneHeight;
		lty2 = lightMap.texH * (y1+spriteHeight) / sceneHeight;
		btx1 = backdropTexW * x1 / sceneWidth;
		btx2 = backdropTexW * (x1+spriteWidth) / sceneWidth;
		bty1 = backdropTexH * y1 / sceneHeight;
		bty2 = backdropTexH * (y1+spriteHeight) / sceneHeight;
	} else {
		btx1 = ltx1 = (float) x1 / sceneWidth;
		btx2 = ltx2 = (float) (x1+spriteWidth) / sceneWidth;
		bty1 = lty1 = (float) y1 / sceneHeight;
		bty2 = lty2 = (float) (y1+spriteHeight) / sceneHeight;
	}

	const GLfloat ltexCoords[] = { 
		ltx1, lty1,
		ltx2, lty1,
		ltx1, lty2,
		ltx2, lty2
	}; 

	const GLfloat btexCoords[] = { 
		btx1, bty1,
		btx2, bty1,
		btx1, bty2,
		btx2, bty2
	}; 

	if (light && lightMap.data) {
		if (lightMapMode == LIGHTMAPMODE_HOTSPOT) {
			int lx=x+cameraX;
			int ly=y+cameraY;
			if (lx<0 || ly<0 || lx>=sceneWidth || ly>=sceneHeight) {
				curLight[0] = curLight[1] = curLight[2] = 255;
			} else {
				GLubyte *target = lightMap.data + (ly*sceneWidth + lx)*4;
				curLight[0] = target[0];
				curLight[1] = target[1];
				curLight[2] = target[2];
			}
		} else if (lightMapMode == LIGHTMAPMODE_PIXEL) {
			curLight[0] = curLight[1] = curLight[2] = 255;
		}
	} else {
		curLight[0] = curLight[1] = curLight[2] = 255;
	}

	setPixelCoords (true);
	GLfloat xoffset = 0.0f;
	while (xoffset < diffX) {
		int w = (diffX-xoffset < viewportWidth) ? (int) (diffX-xoffset) : viewportWidth;

		GLfloat yoffset = 0.0f;
		while (yoffset < diffY) {

			int h = (diffY-yoffset< viewportHeight) ? (int) (diffY-yoffset) : viewportHeight;

			// Render the scene - first the old backdrop (so that it'll show through when the z-buffer is active
			gfx::renderer->setTextureFilter (backdropTex, gfx::FILTER_NEAREST, gfx::FILTER_NEAREST);
			gfx::Texture * copy = copyOfBackdrop ();
			drawIntoTile (backdropTarget, (x1<0) ? xoffset : x1+xoffset, (y1<0) ? yoffset : y1+yoffset, (x1<0) ? -x1 : 0, (y1<0) ? -y1 : 0, w, h);

			const GLfloat vertices[] = { 
				-x1-xoffset, -y1-yoffset, 0.0f, 
				sceneWidth-x1-xoffset, -y1-yoffset, 0.0f, 
				-x1-xoffset, sceneHeight-y1-yoffset, 0.0f,
				sceneWidth-x1-xoffset, sceneHeight-y1-yoffset, 0.0f
			};

			const GLfloat texCoords[] = { 
				0.0f, 0.0f,
				backdropTexW, 0.0f,
				0.0f, backdropTexH,
				backdropTexW, backdropTexH
			}; 

			gfx::draw (gfx::PipelineState (gfx::PROG_TEXTURE), engineUniforms (), copy, vertices, texCoords);

			// The z-buffer
			if (useZB) {
				drawZBuffer((int) (x1+xoffset+camX), (int) (y1+yoffset+camY));
			}

			// Then the sprite
			setDrawMode (thisPerson);

			const GLfloat vertices2[] = { 
				-xoffset, -yoffset, z, 
				spriteWidth-xoffset, -yoffset, z, 
				-xoffset, spriteHeight-yoffset, z,
				spriteWidth-xoffset, spriteHeight-yoffset, z
			};

			if (! mirror) {
				GLfloat tx3 = tx1;
				tx1 = tx2;
				tx2 = tx3;
			}
			const GLfloat texCoords2[] = { 
				tx2, ty1,
				tx1, ty1,
				tx2, ty2,
				tx1, ty2
			}; 

			gfx::DrawUniforms uniforms = engineUniforms ();
			uniforms.useLightTexture = light && lightMapMode == LIGHTMAPMODE_PIXEL && lightMap.data;
			gfx::Texture * const textures[3] = { bank.pages[single.texNum].tex.get (), uniforms.useLightTexture ? lightMap.tex : NULL, copy };
			gfx::Quad quad = { vertices2, { texCoords2, ltexCoords, btexCoords } };
			gfx::renderer->draw (gfx::PipelineState (gfx::PROG_PASTE, gfx::BLEND_OFF, useZB ? gfx::DEPTH_TEST : gfx::DEPTH_OFF), uniforms, textures, quad);

			setSecondaryColor(0., 0., 0., 1.);
			//glDisable(GL_COLOR_SUM); FIXME: replace line?

			yoffset += viewportHeight;
		}
		xoffset += viewportWidth;
	}

	drawIntoWindow ();
	setPixelCoords (false);
}

