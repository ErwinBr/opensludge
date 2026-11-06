#ifndef SPRITEBANKDECODE_H
#define SPRITEBANKDECODE_H

/*
 * Decoding of sprite banks and fonts (.duc files, as the compiler stores them
 * in a game) into pixels, and the layout of their sprites in textures. This
 * is CPU work only: loadSpriteBank (sprites.cpp) uploads the pages that we
 * make here. We check every size and offset in the file, so that a damaged
 * file gives an error instead of a crash or an enormous allocation.
 *
 * The file formats, after the 2-byte count of sprites (big-endian):
 *
 *  - Version 0: the count isn't 0. Each sprite has a width, height, x and y
 *    hotspot of one byte each, then width * height palette indices. Then
 *    the number of colours and the first index (a byte each), and the
 *    colours as RGB.
 *  - Version 1: a 0, the version byte, and nothing else that we read: the
 *    engine has always refused these banks.
 *  - Version 2: a 0, the version byte, the count, the number of colours.
 *    Each sprite has a width and height (2 bytes, big-endian), x and y
 *    hotspot (2 bytes, signed, little-endian), then run-length compressed
 *    indices. Then the colours as RGB, from index 1.
 *  - Version 3: a 0, the version byte, the count. Each sprite has an x and y
 *    hotspot (2 bytes, signed, little-endian), then a PNG without its 8-byte
 *    signature.
 */

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace spritebank {

// A bounded view of the bytes of one resource.
class Reader {
public:
	Reader (const unsigned char * data, size_t size) : data (data), size (size), pos (0) {}

	size_t remaining () const { return size - pos; }
	const unsigned char * here () const { return data + pos; }

	bool byte (int & value);
	bool be16 (int & value);			// Unsigned, big-endian, as get2bytes
	bool le16Signed (int & value);		// Signed, little-endian, as getSigned
	bool bytes (unsigned char * to, size_t n);
	bool skip (size_t n);

private:
	const unsigned char * data;
	size_t size, pos;
};

// The largest width or height of a sprite and of a texture page, and the
// most bytes of pixels that one bank may decode to.
const int maxDimension = 16384;
const uint64_t maxBankBytes = (uint64_t) 1 << 30;

struct DecodedSprite {
	int width, height, xhot, yhot;
	// width * height RGBA pixels. For banks with a palette, transparent
	// pixels have the colour of the last opaque pixel before them (or of
	// the first opaque pixel of the sprite), for the linear filter.
	std::vector<unsigned char> rgba;
	// For fonts: the pixels to burn text into the backdrop with. Empty for
	// other banks.
	std::vector<unsigned char> burn;
};

struct DecodedBank {
	int version;
	bool isFont;
	int paletteSize;	// Colours plus the first index; 0 in version 3
	std::vector<DecodedSprite> sprites;
};

// Decodes a whole bank. On failure, returns false with a message for fatal().
bool decode (Reader & in, bool isFont, DecodedBank & bank, std::string & error);

// Where the sprites go in the textures: sprite i is at (tex_x[i], tex_y[i])
// in texture texNum[i].
struct Layout {
	std::vector<int> tex_x, tex_y, texNum;
	std::vector<int> pageWidth, pageHeight;
};

// The layout: shelves in pages of up to pageSize x pageSize, the tallest
// sprites first, each with a gutter of transparent pixels on every side, so
// that filtering and the anti-aliasing of the smart scaler, which samples up
// to about 1 / (4 x scale) texels away, never reach another sprite (down to
// a scale of 0.07). Fonts and pastes sample the gutter next to the sprite.
const int pageSize = 2048;
const int gutter = 4;
bool layout (const DecodedBank & bank, bool powerOfTwo, Layout & out, std::string & error);

// The layout of the engine before 2.3.2: each texture one row of sprites,
// with a 1-pixel gap between them and an empty first row. In each texture
// after the first, the first sprite has no gap to its left and loses its
// last row. For tests, and to log which sprites the new layout changes.
bool layoutOneRow (const DecodedBank & bank, bool powerOfTwo, Layout & out, std::string & error);

// The RGBA pixels of each texture (and, for fonts, of each burn texture),
// pageWidth * pageHeight each.
struct Pages {
	std::vector<std::vector<unsigned char> > colour, burn;
};

// With extrude, the gutter around each sprite gets the colour of the
// sprite's nearest edge pixel, with alpha 0, so that filtering doesn't darken
// the edges. Without it, the gutter is transparent black, as it always was.
bool makePages (const DecodedBank & bank, const Layout & layout, bool extrude, Pages & out, std::string & error);

// How each way of drawing a sprite samples its texture. They are relative
// to the sprite, which starts at (tex_x, tex_y) in a page of pageW x pageH,
// with the empty row above it and the empty columns beside it that some of
// them sample, as the engine always did.
enum TexKind {
	TEXKIND_SCALE,		// Characters (scaleSprite, fixScaleSprite): the sprite exactly
	TEXKIND_PASTE,		// pasteSpriteToBackDrop: from the row above the sprite, h rows
	TEXKIND_FONT,		// fontSprite: half a texel either side, a row above and below
	TEXKIND_FONT_FLIP,	// flipped: a whole texel more on the right
	TEXKIND_BURN,		// burnSpriteToBackDrop: as fontSprite
};

struct TexRect {
	float u1, v1, u2, v2;
};

// Where nearest filtering samples exactly on the edge between two texels,
// which texel it reads depends on rounding: on the GPU, the renderer, the
// sprite's place in its page, and on some GPUs from one run to the next. So
// texRect shifts each rectangle by texelNudge of a texel: far more than any
// rounding, too little to change what filtering reads anywhere else.
const float texelNudge = 1.0f / 256;

TexRect texRect (int tex_x, int tex_y, int width, int height, int pageW, int pageH, TexKind kind);
// The same without the nudge: the formulas of the draw functions before
// sprites had a tex_y (for tests).
TexRect texRectUnnudged (int tex_x, int tex_y, int width, int height, int pageW, int pageH, TexKind kind);

int nextPowerOfTwo (int n);

}

#endif
