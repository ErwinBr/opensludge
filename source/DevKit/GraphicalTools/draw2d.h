/*
 * draw2d.h - Part of the SLUDGE Dev Kit
 *
 * 2D drawing for the views of the floor maker, the z-buffer maker and the
 * sprite bank editor. The views draw through this interface, which
 * draw2d_gfx.cpp implements with the renderers of the engine. (The GTK
 * tools drew it with OpenGL 3.2; the references of tests/devkit come from
 * them.)
 *
 * The pictures that we draw are the sprites of a sprite bank and the
 * layers of a z-buffer. The drawing makes a texture for each, the first
 * time that it draws it, and makes it again when the bank has changed
 * (spriteBank::changes).
 */

#ifndef __DRAW2D_H__
#define __DRAW2D_H__

struct spriteBank;

class Draw2D {
public:
	virtual ~Draw2D () {}

	// Clear the view to transparent black.
	virtual void clear () = 0;

	// Set up a view of w by h pixels, scrolled to (x, y), in which each
	// pixel of the view covers zmul pixels of the picture. In setViewArea,
	// y grows downwards, as in a backdrop, and in setSpriteViewArea, y grows
	// upwards from the hotspot of a sprite.
	virtual void setViewArea (int x, int y, int w, int h, float zmul) = 0;
	virtual void setSpriteViewArea (int x, int y, int w, int h, float zmul) = 0;

	// Set the colour of what we draw next. We multiply the colours of a
	// picture by it. We blend what we draw with what is there, by its alpha.
	virtual void setDrawingColour (float r, float g, float b, float a) = 0;

	// Draw count points, given as x, y pairs: as separate lines between pairs
	// of points, as a closed line through all of them, or as a filled convex
	// polygon. Lines are one pixel of the view wide.
	virtual void drawLines (const float * points, int count) = 0;
	virtual void drawLineLoop (const float * points, int count) = 0;
	virtual void drawPolygon (const float * points, int count) = 0;

	// Draw the part from (tx1, ty1) to (tx2, ty2) of a picture, the sprite
	// with the given index of a bank or the layer of a z-buffer (see
	// spritePixels and zBufferLayerPixels), in the rectangle from (x1, y1)
	// to (x2, y2).
	virtual void drawPicture (const spriteBank * bank, int index, bool zBufferLayer,
		float x1, float y1, float x2, float y2,
		float tx1, float ty1, float tx2, float ty2) = 0;

	// Draw what we have batched up, if anything. Call this at the end of a
	// view.
	virtual void finish () {}

	// Forget the textures of a bank, before it goes away.
	virtual void forgetPictures (const spriteBank * bank) = 0;
};

#endif
