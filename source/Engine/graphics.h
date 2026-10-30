/*
 *  Graphics.h
 *  SdlSludge
 *
 *  Created by Rikard Peterson on 2009-05-01.
 *
 */

#ifndef __SLUDGE_GRAPHICS_H__
#define __SLUDGE_GRAPHICS_H__

#include "sludge_gl.h"

#include "renderer.h"

struct texture {
	GLubyte * data;
	gfx::Texture * tex;
	int w, h;
	double texW, texH;
};

// From Backdrop.cpp, but they're here anyway
extern GLubyte * backdropTexture;
extern GLfloat backdropTexW, backdropTexH;


extern unsigned int winWidth, winHeight;
extern int viewportHeight, viewportWidth;
extern int viewportOffsetX, viewportOffsetY;
extern int realWinWidth, realWinHeight;

extern bool NPOT_textures;
extern GLfloat aPMVMatrix[];

void setPrimaryColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void setSecondaryColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);

void setPixelCoords (bool pixels);
// Set the projection for drawing in game coordinates, with every sprite and
// letter at scale times its size. setPixelCoords (false) sets it with
// scale 1.
void setGameMatrix (float scale);
void setGraphicsWindow(bool fullscreen, bool restoreGraphics = true);

// The engine used to draw at the origin of the window, with the projection
// of setPixelCoords (true), and copy a rectangle from there into a
// texture. Instead, we draw into the target of the texture, with the
// viewport moved to where the rectangle came from and only in the
// rectangle, which we clear first. srcX and srcY are where the rectangle
// was in the viewport, and dstX and dstY where it went in the texture.
void drawIntoTile (gfx::RenderTarget * target, int dstX, int dstY, int srcX, int srcY, int width, int height);
// Draw into the window again, in the viewport of the game.
void drawIntoWindow ();

// Draw the frame of the game into the frame texture, which we copy into
// the window with drawFrameIntoWindow. In between, the frame texture is the
// frame without the mouse cursor.
void beginFrame ();
void drawFrameIntoWindow ();

void setupOpenGLStuff();
void updateViewport ();
void windowToGameCoords (float x, float y, int * gameX, int * gameY);
void gameToWindowCoords (float gameX, float gameY, float * x, float * y);
void swapGraphicsBuffers ();

struct SDL_Window;
extern SDL_Window * sludgeWindow;

int getNextPOT(int n);

// Read the area of the game in the frame we are drawing, top row first, as
// RGBA. Free the pixels with delete [].
unsigned char * readFramePixels (int * width, int * height);
// Read the backdrop, top row first, as RGBA. Returns NULL without a
// backdrop. Free the pixels with delete [].
unsigned char * readBackdropPixels (int * width, int * height);

int printOglError (const char *file, int         line);
#define printOpenGLError() printOglError(__FILE__, __LINE__)

#endif
