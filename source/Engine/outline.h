#ifndef SLUDGE_OUTLINE_H
#define SLUDGE_OUTLINE_H

// Outlines around characters (setCharacterDrawMode OUTLINE) and around the
// mouse cursor (setCursorOutline).
//
// An outline is the part of the screen that lies within its thickness of
// the edge of what the character draws, where its alpha is one half. We
// work it out on the GPU, in a field: a few textures over the character's
// box on screen, at the pixels of the window, so that the thickness is the
// same however the character is scaled or the camera zooms.
//
//  1. The silhouette: we draw the character's quads into the field.
//  2. The seeds (PROG_OUTLINE_SEED): each pixel next to the edge finds the
//     point where the edge crosses the line to a neighbour, a point of the
//     edge to within a fraction of a pixel.
//  3. The jump flood (PROG_OUTLINE_JUMP): in steps of half the length of the
//     step before, each pixel takes the nearest seed that it or the pixels
//     a step away know, until every pixel knows its nearest seed. That takes
//     a few passes, however thick the outline: 4 for 8 pixels.
//  4. The outline (PROG_OUTLINE): where the nearest seed is less than the
//     thickness away, with a pixel of anti-aliasing. We draw it under the
//     character, at its depth, so that the z-buffer hides it as it hides the
//     character.
//
// The first three draw into textures of their own, so we do them before
// the pass that draws the scene, in prepareOutlines, and keep the fields
// while the character's quads stay the same.

#include <vector>

#include "renderer.h"

// How an outline looks.
struct OutlineStyle {
	int thickness;				// In pixels of the screen, at the game's resolution
	unsigned char colour[4];	// Red, green, blue and alpha
	OutlineStyle (int t = 2) : thickness (t) {
		colour[0] = colour[1] = colour[2] = colour[3] = 255;
	}
};

const int maxOutlineThickness = 64;

// A quad of a sprite that a character or the cursor draws: its corners in
// the order of gfx::Quad, in the coordinates of the game's projection, and
// its texture coordinates.
struct ShapeQuad {
	gfx::Texture * texture;
	float corners[8];
	float texCoords[8];
	float alpha;
};
bool operator== (const ShapeQuad & a, const ShapeQuad & b);
// From the 12 floats of a gfx::Quad's vertices (x, y and z) and its 8
// texture coordinates.
ShapeQuad shapeQuad (gfx::Texture * texture, const float * vertices, const float * texCoords, float alpha);

// While it isn't NULL, scaleSprite, drawRiggedPerson and fontSprite add the
// quads that they would draw to it, and draw nothing.
extern std::vector<ShapeQuad> * shapeSink;

enum OutlineField {
	OUTLINE_NONE,		// Nothing to outline, or nothing of it on screen
	OUTLINE_KEPT,		// The field is as it was: we drew nothing
	OUTLINE_MADE,		// We drew a new field: the target and viewport changed
};

// Make the field of key's outline for shape, or keep the one it has if
// shape and thickness are the same. With clip, only what is near the screen.
OutlineField makeOutlineField (const void * key, const std::vector<ShapeQuad> & shape, int thickness, bool clip);

// Draw key's outline under shape, if its field is of this shape, moved by
// dx, dy. opacity, from 0 to 1, multiplies the colour's alpha; below 1, the
// outline leaves out the inside of the shape, which shows through.
void drawOutline (const void * key, const std::vector<ShapeQuad> & shape, const OutlineStyle & style, float opacity,
	float z, bool depthTest, float dx = 0, float dy = 0);

// Release the fields that weren't made or drawn since the last time.
void releaseUnusedOutlines ();

// The fields of the outlined characters, before a pass that draws them
// (people.cpp). The target and viewport may change.
void prepareOutlines ();

#endif
