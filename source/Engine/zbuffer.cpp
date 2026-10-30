#include <stdint.h>

#include "allfiles.h"
#include "zbuffer.h"
#include "fileset.h"
#include "moreio.h"
#include "newfatal.h"
#include "graphics.h"

zBufferData zBuffer;
extern int sceneWidth, sceneHeight;

void killZBuffer () {
	if (zBuffer.tex) {
		gfx::renderer->destroyTexture (zBuffer.texture);
		zBuffer.texture = NULL;
		delete [] zBuffer.tex;
		zBuffer.tex = NULL;
	}
	zBuffer.numPanels = 0;
	zBuffer.originalNum =0;
}

void sortZPal (int *oldpal, int *newpal, int size) {
	int i, tmp;

	for (i = 0; i < size; i ++) {
		newpal[i] = i;
	}

	if (size < 2) return;		
		
	for (i = 1; i < size; i ++) {
		if (oldpal[newpal[i]] < oldpal[newpal[i-1]]) {
			tmp = newpal[i];
			newpal[i] = newpal[i-1];
			newpal[i-1] = tmp;
			i = 0;
		}
	}
}

bool setZBuffer (int y) {
	int x, n;
	uint32_t stillToGo = 0;
	int yPalette[16], sorted[16], sortback[16];

	killZBuffer ();
	
	setResourceForFatal (y);

	zBuffer.originalNum = y;
	if (! openFileFromNum (y)) return false;
	if (fgetc (bigDataFile) != 'S') return fatal ("Not a Z-buffer file");
	if (fgetc (bigDataFile) != 'z') return fatal ("Not a Z-buffer file");
	if (fgetc (bigDataFile) != 'b') return fatal ("Not a Z-buffer file");

	switch (fgetc (bigDataFile)) {
		case 0:
		zBuffer.width = 640;
		zBuffer.height = 480;
		break;
		
		case 1:
		zBuffer.width = get2bytes (bigDataFile);
		zBuffer.height = get2bytes (bigDataFile);
		break;
		
		default:
		return fatal ("Extended Z-buffer format not supported in this version of the SLUDGE engine");
	}
	if (zBuffer.width != sceneWidth || zBuffer.height != sceneHeight) {
		char tmp[256];
		sprintf (tmp, "Z-w: %d Z-h:%d w: %d, h:%d", zBuffer.width, zBuffer.height, sceneWidth, sceneHeight);
		return fatal ("Z-buffer width and height don't match scene width and height", tmp);
	}
		
	zBuffer.numPanels = fgetc (bigDataFile);
	for (y = 0; y < zBuffer.numPanels; y ++) {
		yPalette[y] = get2bytes (bigDataFile);
	}
	sortZPal (yPalette, sorted, zBuffer.numPanels);
	for (y = 0; y < zBuffer.numPanels; y ++) {
		zBuffer.panel[y] = yPalette[sorted[y]];
		sortback[sorted[y]] = y; 
	}
	
	int picWidth = sceneWidth;
	int picHeight = sceneHeight;
	if (! NPOT_textures) {
		picWidth = getNextPOT(picWidth);
		picHeight = getNextPOT(picHeight);
	}
	zBuffer.tex = new GLubyte [picHeight*picWidth] ();
	if (! checkNew (zBuffer.tex)) return false;

	for (y = 0; y < sceneHeight; y ++) {
		for (x = 0; x < sceneWidth; x ++) {
			if (stillToGo == 0) {
				n = fgetc (bigDataFile);
				stillToGo = n >> 4;
				if (stillToGo == 15) stillToGo = get2bytes (bigDataFile) + 16l;
				else stillToGo ++;
				n &= 15;
			}
			zBuffer.tex[y*picWidth + x] = sortback[n]*16;
			stillToGo --;
		}
	}
	finishAccess ();
	
	setResourceForFatal (-1);
		
	if (zBuffer.texture) {
		gfx::renderer->setTextureImage (zBuffer.texture, picWidth, picHeight, zBuffer.tex);
	} else {
		zBuffer.texture = gfx::renderer->createTexture (picWidth, picHeight, gfx::TEX_R8, zBuffer.tex);
	}
	gfx::renderer->setTextureWrap (zBuffer.texture, gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
	gfx::renderer->setTextureFilter (zBuffer.texture, gfx::FILTER_NEAREST, gfx::FILTER_NEAREST);
	
	
	return true;
}

int zBufferPanelAt (int x, int y) {
	if (! zBuffer.tex || x < 0 || y < 0 || x >= zBuffer.width || y >= zBuffer.height) return 0;
	int stride = NPOT_textures ? sceneWidth : getNextPOT (sceneWidth);
	return zBuffer.tex[y * stride + x] / 16;
}

void drawZBuffer(int x, int y) {
	int i;
	
	if (! zBuffer.tex) return;

	// We draw only into the depth buffer.
	gfx::PipelineState state (gfx::PROG_TEXTURE, gfx::BLEND_ALPHA, gfx::DEPTH_TEST_WRITE, gfx::MASK_NONE);
	setPrimaryColor(1.0, 1.0, 1.0, 1.0);

	for (i = 1; i<zBuffer.numPanels; i++) {
		GLfloat z = 1.0 - (double) i * (1.0 / 128.0);

		GLfloat vy1 = -y, vy2 = zBuffer.height-y;

		const GLfloat vertices[] = { 
			(GLfloat)-x, vy1, z,
			(GLfloat)zBuffer.width-x, vy1, z, 
			(GLfloat)-x, vy2, z,
			(GLfloat)zBuffer.width-x, vy2, z
		};

		const GLfloat texCoords[] = { 
			0.0f, 0.0f,
			backdropTexW, 0.0f,
			0.0f, backdropTexH,
			backdropTexW, backdropTexH
		}; 

		gfx::DrawUniforms uniforms;
		uniforms.zBuffer = true;
		uniforms.zBufferLayer = i;
		gfx::draw (state, uniforms, zBuffer.texture, vertices, texCoords);
	}
}

