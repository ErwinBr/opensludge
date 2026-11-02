#ifndef __SPRITES_H__
#define __SPRITES_H__

class Draw2D;

#ifndef WIN32
#define HWND int
#endif

#define SPRITE_AREA_X	193
#define SPRITE_AREA_Y	100
#define SPRITE_AREA_W	264
#define SPRITE_AREA_H	260

struct sprite {
	int width, height, xhot, yhot;
	int special;
	unsigned char * data;
};

struct spritePalette {
	unsigned short int * pal;
	unsigned char * r;
	unsigned char * g;
	unsigned char * b;
	unsigned char originalRed, originalGreen, originalBlue, total;
};

struct spriteBank {
	int total;
	int type;
	struct sprite * sprites;
	struct spritePalette myPalette;
	bool isFont;
	// A number that changes whenever the pictures change, so that the views
	// know when to make their textures again (draw2d.h).
	unsigned int changes;
};

// Call after changing the pictures of a bank, or its palette.
void spriteBankChanged (struct spriteBank * bank);

// The picture of a sprite as width x height RGBA pixels, the top row first,
// transparent where a palette sprite has colour 0. Free it with delete [].
unsigned char * spritePixels (const struct spriteBank * bank, int index);

bool reserveSpritePal (struct spritePalette * sP, int n);
void forgetSpriteBank (struct spriteBank * forgetme);
bool loadSpriteBank (const char * filename, struct spriteBank * loadhere);
bool saveSpriteBank (const char * filename, struct spriteBank *sprites);

void addSprite (int i, struct spriteBank *sprites);
void deleteSprite (int i, struct spriteBank *sprites);

bool loadSpriteFromPNG (const char * file, struct spriteBank *sprites, int index);
bool loadSpriteFromTGA (const char * file, struct spriteBank *sprites, int index);

void doFontification (struct spriteBank *sprites, unsigned int fontifySpaceWidth);

bool convertSpriteBank8to32 (struct spriteBank *sprites);
bool exportToPNG (const char * file, struct spriteBank *sprites, int index);

void paintPal (HWND h);
void sortColours (HWND h);
void optimizePalette ();
bool saveSpriteToTGA (char * file, int);
bool trimSprite (int);

// Draw a sprite of a bank, or a layer of a z-buffer, at its hotspot, with
// its box when showBox is set.
void pasteSprite (Draw2D & draw, const struct spriteBank * bank, int index, bool zBufferLayer, bool showBox);

// Draw the sprite with the given index, with its box when showBox is set,
// and the axes through its hotspot.
void drawSpriteBankView (Draw2D & draw, const struct spriteBank * sprites, int index, bool showBox);

#endif
