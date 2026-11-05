/*
 * Tests of the decoding and layout of sprite banks
 * (source/Engine/spritebankdecode.cpp): banks of each version that the
 * engine reads, the layout of their sprites in textures, and damaged files,
 * which must fail cleanly.
 */

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <png.h>

#include "spritebankdecode.h"

using namespace spritebank;

static int failures = 0;

#define CHECK(cond, what) do { if (! (cond)) { fprintf (stderr, "FAIL: %s (line %d)\n", what, __LINE__); failures ++; } } while (0)

typedef std::vector<unsigned char> Bytes;

static void be16 (Bytes & b, int v) { b.push_back ((v >> 8) & 255); b.push_back (v & 255); }
static void le16 (Bytes & b, int v) { b.push_back (v & 255); b.push_back ((v >> 8) & 255); }

static bool decodeBytes (const Bytes & b, bool isFont, DecodedBank & bank, std::string & error) {
	Reader r (b.data (), b.size ());
	return decode (r, isFont, bank, error);
}

static bool pixelIs (const DecodedSprite & sp, int i, int r, int g, int b, int a) {
	const unsigned char * p = &sp.rgba[i * 4];
	return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
}

// Version 0: two sprites with one-byte sizes and hotspots, then the palette.
static Bytes version0 () {
	Bytes b;
	be16 (b, 2);
	// 3 x 1, hotspot (200, 1): transparent, colour 1, transparent.
	b.push_back (3); b.push_back (1); b.push_back (200); b.push_back (1);
	b.push_back (0); b.push_back (1); b.push_back (0);
	// 2 x 2, all transparent.
	b.push_back (2); b.push_back (2); b.push_back (0); b.push_back (0);
	b.push_back (0); b.push_back (0); b.push_back (0); b.push_back (0);
	// One colour, from index 1.
	b.push_back (1); b.push_back (1);
	b.push_back (10); b.push_back (20); b.push_back (30);
	return b;
}

static void testVersion0 () {
	DecodedBank bank;
	std::string error;
	CHECK (decodeBytes (version0 (), false, bank, error), "version 0 decodes");
	if (bank.sprites.size () != 2) return;
	const DecodedSprite & a = bank.sprites[0];
	CHECK (a.width == 3 && a.height == 1 && a.xhot == 200 && a.yhot == 1, "version 0 has unsigned hotspots");
	// Transparent pixels take the colour of the first and then the last opaque pixel.
	CHECK (pixelIs (a, 0, 10, 20, 30, 0), "version 0 transparent pixel before");
	CHECK (pixelIs (a, 1, 10, 20, 30, 255), "version 0 opaque pixel");
	CHECK (pixelIs (a, 2, 10, 20, 30, 0), "version 0 transparent pixel after");
	CHECK (pixelIs (bank.sprites[1], 0, 0, 0, 0, 0), "version 0 empty sprite is black and transparent");
	CHECK (bank.paletteSize == 2, "version 0 palette size");
}

// Version 2: run-length compressed, with two colours.
static Bytes version2 (bool damagedRun) {
	Bytes b;
	be16 (b, 0); b.push_back (2); be16 (b, 1);
	b.push_back (2);					// Two colours, from index 1
	be16 (b, 4); be16 (b, 2);			// 4 x 2
	le16 (b, -3); le16 (b, 5);			// Hotspot (-3, 5)
	// A run of 5 of colour 2 (2 + 2 + 1 = 5, then 4 = 5 - 1), then 3 single pixels.
	b.push_back (5); b.push_back (damagedRun ? 200 : 4);
	b.push_back (1); b.push_back (0); b.push_back (1);
	if (damagedRun) return b;
	b.push_back (255); b.push_back (0); b.push_back (0);
	b.push_back (0); b.push_back (0); b.push_back (255);
	return b;
}

static void testVersion2 () {
	DecodedBank bank;
	std::string error;
	CHECK (decodeBytes (version2 (false), true, bank, error), "version 2 decodes");
	if (bank.sprites.size () != 1) return;
	const DecodedSprite & s = bank.sprites[0];
	CHECK (s.width == 4 && s.height == 2 && s.xhot == -3 && s.yhot == 5, "version 2 size and signed hotspot");
	CHECK (pixelIs (s, 0, 0, 0, 255, 255) && pixelIs (s, 4, 0, 0, 255, 255), "version 2 run");
	CHECK (pixelIs (s, 5, 255, 0, 0, 255), "version 2 single pixel");
	CHECK (pixelIs (s, 6, 255, 0, 0, 0), "version 2 transparent pixel has the last colour");
	// Fonts burn with white, with the red of the palette as alpha.
	CHECK (s.burn.size () == 32 && s.burn[0] == 255 && s.burn[3] == 0 && s.burn[5 * 4 + 3] == 255 && s.burn[6 * 4 + 3] == 0, "version 2 burn pixels");

	// A run past the end of the sprite stops there, and the palette is missing.
	CHECK (! decodeBytes (version2 (true), false, bank, error), "version 2 with a long run and no palette fails");
}

static Bytes pngWithoutSignature (int width, int height, int colourType, int bitDepth, bool interlace) {
	Bytes out;
	png_structp png = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	png_infop info = png_create_info_struct (png);
	png_set_write_fn (png, &out, [] (png_structp p, png_bytep data, png_size_t n) {
		Bytes * o = (Bytes *) png_get_io_ptr (p);
		o -> insert (o -> end (), data, data + n);
	}, NULL);
	png_set_IHDR (png, info, width, height, bitDepth, colourType,
		interlace ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	if (colourType == PNG_COLOR_TYPE_PALETTE) {
		png_color pal[2] = { {1, 2, 3}, {200, 100, 50} };
		png_set_PLTE (png, info, pal, 2);
	}
	png_write_info (png, info);
	int channels = colourType == PNG_COLOR_TYPE_RGB_ALPHA ? 4 : colourType == PNG_COLOR_TYPE_RGB ? 3
		: colourType == PNG_COLOR_TYPE_GRAY_ALPHA ? 2 : 1;
	int bytes = bitDepth / 8;
	Bytes image (width * height * channels * bytes);
	for (size_t i = 0; i < image.size (); i ++) image[i] = (unsigned char) (colourType == PNG_COLOR_TYPE_PALETTE ? (i & 1) : i * 7);
	std::vector<png_bytep> rows (height);
	for (int y = 0; y < height; y ++) rows[y] = &image[y * width * channels * bytes];
	png_write_image (png, rows.data ());
	png_write_end (png, NULL);
	png_destroy_write_struct (&png, &info);
	return Bytes (out.begin () + 8, out.end ());
}

static Bytes version3 (const Bytes & png, int count) {
	Bytes b;
	be16 (b, 0); b.push_back (3); be16 (b, count);
	for (int i = 0; i < count; i ++) {
		le16 (b, 7); le16 (b, -2);
		b.insert (b.end (), png.begin (), png.end ());
	}
	return b;
}

static void testVersion3 () {
	DecodedBank bank;
	std::string error;

	CHECK (decodeBytes (version3 (pngWithoutSignature (3, 2, PNG_COLOR_TYPE_RGB_ALPHA, 8, false), 2), false, bank, error), "version 3 RGBA decodes");
	if (bank.sprites.size () == 2) {
		const DecodedSprite & s = bank.sprites[1];
		CHECK (s.width == 3 && s.height == 2 && s.xhot == 7 && s.yhot == -2, "version 3 size and hotspot");
		bool same = true;
		for (int i = 0; i < 24; i ++) same = same && s.rgba[i] == (unsigned char) (i * 7);
		CHECK (same, "version 3 RGBA pixels unchanged");
		CHECK (s.burn.empty (), "version 3 has no burn pixels");
	}

	CHECK (decodeBytes (version3 (pngWithoutSignature (2, 2, PNG_COLOR_TYPE_RGB, 8, false), 1), false, bank, error)
		&& bank.sprites[0].rgba[3] == 255 && bank.sprites[0].rgba[0] == 0 && bank.sprites[0].rgba[4] == 21, "version 3 RGB gets opaque alpha");
	CHECK (decodeBytes (version3 (pngWithoutSignature (2, 1, PNG_COLOR_TYPE_PALETTE, 8, false), 1), false, bank, error)
		&& pixelIs (bank.sprites[0], 0, 1, 2, 3, 255) && pixelIs (bank.sprites[0], 1, 200, 100, 50, 255), "version 3 palette PNG");
	CHECK (decodeBytes (version3 (pngWithoutSignature (2, 1, PNG_COLOR_TYPE_GRAY, 8, false), 1), false, bank, error)
		&& pixelIs (bank.sprites[0], 1, 7, 7, 7, 255), "version 3 grey PNG");
	CHECK (decodeBytes (version3 (pngWithoutSignature (2, 2, PNG_COLOR_TYPE_RGB_ALPHA, 16, false), 1), false, bank, error)
		&& bank.sprites[0].rgba.size () == 16, "version 3 16-bit PNG");
	CHECK (decodeBytes (version3 (pngWithoutSignature (9, 9, PNG_COLOR_TYPE_RGB_ALPHA, 8, true), 1), false, bank, error)
		&& bank.sprites[0].rgba[9 * 9 * 4 - 1] == (unsigned char) ((9 * 9 * 4 - 1) * 7), "version 3 interlaced PNG");

	// Damaged PNGs.
	Bytes png = pngWithoutSignature (4, 4, PNG_COLOR_TYPE_RGB_ALPHA, 8, false);
	Bytes cut (png.begin (), png.begin () + png.size () / 2);
	CHECK (! decodeBytes (version3 (cut, 1), false, bank, error), "version 3 truncated PNG fails");
	Bytes broken = png;
	for (size_t i = 30; i < broken.size () - 12; i ++) broken[i] ^= 0x5a;
	CHECK (! decodeBytes (version3 (broken, 1), false, bank, error), "version 3 corrupt PNG fails");
	// Wider than any texture: refused before we allocate its pixels.
	Bytes huge = pngWithoutSignature (20000, 1, PNG_COLOR_TYPE_GRAY, 8, false);
	CHECK (! decodeBytes (version3 (huge, 1), false, bank, error), "version 3 enormous PNG fails");
}

static void testDamagedHeaders () {
	DecodedBank bank;
	std::string error;
	Bytes b;
	CHECK (! decodeBytes (b, false, bank, error), "empty file fails");
	be16 (b, 0); b.push_back (1);
	CHECK (! decodeBytes (b, false, bank, error), "version 1 fails, as it always did");
	b.clear (); be16 (b, 0); b.push_back (4); be16 (b, 1);
	CHECK (! decodeBytes (b, false, bank, error), "version 4 fails");
	b.clear (); be16 (b, 0); b.push_back (2); be16 (b, 0);
	CHECK (! decodeBytes (b, false, bank, error), "no sprites fails");
	// Version 2 claims 65535 x 65535: too big, before we allocate anything.
	b.clear (); be16 (b, 0); b.push_back (2); be16 (b, 1); b.push_back (1);
	be16 (b, 65535); be16 (b, 65535); le16 (b, 0); le16 (b, 0);
	CHECK (! decodeBytes (b, false, bank, error), "huge version 2 sprite fails");
	// 60000 sprites of 16000 x 16000 would be far over the budget.
	b.clear (); be16 (b, 0); b.push_back (2); be16 (b, 60000); b.push_back (1);
	for (int i = 0; i < 100; i ++) { be16 (b, 16000); be16 (b, 16000); le16 (b, 0); le16 (b, 0); b.push_back (2); b.push_back (255); }
	CHECK (! decodeBytes (b, false, bank, error), "bank over the budget fails");
	// Version 0, cut in the middle of the pixels.
	Bytes v0 = version0 ();
	v0.resize (8);
	CHECK (! decodeBytes (v0, false, bank, error), "truncated version 0 fails");
}

static DecodedBank spritesOfWidths (const std::vector<int> & widths, int height) {
	DecodedBank bank;
	bank.version = 3;
	bank.isFont = false;
	bank.paletteSize = 0;
	for (size_t i = 0; i < widths.size (); i ++) {
		DecodedSprite s;
		s.width = widths[i];
		s.height = height;
		s.xhot = s.yhot = 0;
		s.rgba.assign ((size_t) s.width * height * 4, (unsigned char) (i + 1));
		bank.sprites.push_back (s);
	}
	return bank;
}

static void testLayout () {
	std::string error;
	Layout l;

	// Two sprites that fit in one 511-wide row, then one that doesn't.
	DecodedBank bank = spritesOfWidths (std::vector<int> {100, 200, 300}, 10);
	CHECK (layout (bank, false, l, error), "layout");
	CHECK (l.tex_x[0] == 1 && l.tex_x[1] == 102 && l.texNum[1] == 0, "first texture starts after a gap");
	CHECK (l.texNum[2] == 1 && l.tex_x[2] == 0, "a new texture starts without a gap");
	CHECK (l.pageWidth[0] == 303 && l.pageHeight[0] == 12, "first texture size");
	CHECK (l.pageWidth[1] == 300 && l.pageHeight[1] == 10, "later texture size, as always");

	// A sprite exactly 511 wide always starts a new texture.
	bank = spritesOfWidths (std::vector<int> {511}, 4);
	CHECK (layout (bank, false, l, error) && l.pageWidth.size () == 2 && l.pageWidth[0] == 1 && l.texNum[0] == 1, "511 wide starts a new texture");

	// Wide sprites share textures up to 2047.
	bank = spritesOfWidths (std::vector<int> {1000, 1000, 1000}, 4);
	CHECK (layout (bank, false, l, error) && l.texNum[1] == 0 && l.texNum[2] == 1, "wide sprites share up to 2047");

	CHECK (layout (spritesOfWidths (std::vector<int> {100, 200, 300}, 10), true, l, error)
		&& l.pageWidth[0] == 512 && l.pageHeight[0] == 16 && l.pageWidth[1] == 512 && l.pageHeight[1] == 16, "power of two sizes");

	// More than 256 textures fail.
	CHECK (! layout (spritesOfWidths (std::vector<int> (300, 1500), 1), false, l, error), "too many textures fail");

	// The pages: each sprite from the second row, the first row empty.
	bank = spritesOfWidths (std::vector<int> {2, 3, 511}, 2);
	Pages pages;
	CHECK (layout (bank, false, l, error) && makePages (bank, l, pages, error), "pages");
	const Bytes & p0 = pages.colour[0];
	int w = l.pageWidth[0];
	CHECK (p0[(0 * w + 1) * 4] == 0 && p0[(1 * w + 1) * 4] == 1 && p0[(2 * w + 2) * 4] == 1, "first sprite in the second row");
	CHECK (p0[(1 * w + 3) * 4] == 0 && p0[(1 * w + 4) * 4] == 2, "a gap between sprites");
	// In a later texture, the last row of the first sprite doesn't fit.
	const Bytes & p1 = pages.colour[1];
	CHECK (l.pageHeight[1] == 2 && p1[(1 * 511) * 4] == 3 && pages.colour[1].size () == 511 * 2 * 4, "last row cut off, as always");
	CHECK (pages.burn.empty (), "no burn pages for sprites");
}

int main () {
	testVersion0 ();
	testVersion2 ();
	testVersion3 ();
	testDamagedHeaders ();
	testLayout ();
	if (failures) {
		fprintf (stderr, "%d failures\n", failures);
		return 1;
	}
	printf ("All sprite bank tests passed\n");
	return 0;
}
