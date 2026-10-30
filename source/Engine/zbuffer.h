#ifndef _ZBUFFER_H_
#define _ZBUFFER_H_

#include "sludge_gl.h"
#include "renderer.h"

struct zBufferData {
	int width, height;
//	bool loaded;
	int numPanels;
	int panel[16];
	int originalNum;

	GLubyte * tex;
	gfx::Texture * texture;
};

bool setZBuffer (int y);
void killZBuffer ();
void drawZBuffer(int x, int y);
// The panel of the z-buffer at a point of the scene, or 0 outside it.
int zBufferPanelAt (int x, int y);

#endif
