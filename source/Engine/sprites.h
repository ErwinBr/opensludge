#ifndef SPRITE_H
#define SPRITE_H

#include "sludge_gl.h"
#include "renderer.h"

#include <stddef.h>
#include <vector>

struct onScreenPerson;

struct sprite {
	int width, height, xhot, yhot;
	// Where the sprite is: from (tex_x, tex_y) in texture texNum of its bank.
	int tex_x, tex_y;
	int texNum;
	// The alpha of each pixel, width by height, for finding out which
	// character the mouse is over. Empty in fonts.
	std::vector<unsigned char> alpha;
};

// The colour to draw a font with. Sprites draw in white.
struct spritePalette {
	unsigned char originalRed, originalGreen, originalBlue;
};

// A texture of a bank, and for fonts the texture to burn text with.
struct AtlasPage {
	gfx::UniqueTexture tex, burn;
	int w, h;
	size_t gpuBytes;	// Of both textures
};

// The sprites of a sprite bank or font, and the textures that hold them.
struct spriteBank {
	int total;
	std::vector<sprite> sprites;
	std::vector<AtlasPage> pages;
	spritePalette myPalette;
	bool isFont;
	int paletteSize;	// For banks with a palette, as the file says

	spriteBank () : total (0), isFont (false), paletteSize (0) {
		myPalette.originalRed = myPalette.originalGreen = myPalette.originalBlue = 255;
	}
	spriteBank (spriteBank && other) = default;
	spriteBank & operator= (spriteBank && other) = default;
	spriteBank (const spriteBank &) = delete;
	spriteBank & operator= (const spriteBank &) = delete;

	size_t gpuBytes () const;
};

namespace spritebank { struct Layout; struct Pages; }
// Makes the textures of the pages of a bank.
void uploadPages (gfx::Renderer & renderer, const spritebank::Layout & layout, const spritebank::Pages & pages,
	bool isFont, gfx::Filter filter, std::vector<AtlasPage> & out);

// Replaces what loadhere holds; on failure, calls fatal.
bool loadSpriteBank (int fileNum, spriteBank & loadhere, bool isFont);

// Fonts, and the mouse cursor, draw with the colour of fontPal.
void fontSprite		(int x1, int y1, const sprite & single, const spriteBank & bank, const spritePalette & fontPal);
void flipFontSprite	(int x1, int y1, const sprite & single, const spriteBank & bank, const spritePalette & fontPal);
void pasteSpriteToBackDrop (int x1, int y1, const sprite & single, const spriteBank & bank, const spritePalette & fontPal);
void burnSpriteToBackDrop (int x1, int y1, const sprite & single, const spriteBank & bank);

bool scaleSprite (const sprite & single, const spriteBank & bank, const onScreenPerson * thisPerson, bool mirror);
// A character of parts, from its pose (composite.cpp). Whether the mouse is
// over it.
bool drawRiggedPerson (const onScreenPerson * thisPerson);
// Paste a character of parts into the backdrop, behind the z-buffer, as
// pasteCharacter does.
void pasteRiggedPerson (const onScreenPerson * thisPerson);
void fixScaleSprite (int x1, int y1, const sprite & single, const spriteBank & bank, onScreenPerson * thisPerson, const int camX, const int camY, bool);

#endif
