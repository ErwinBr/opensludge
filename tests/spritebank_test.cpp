/*
 * Tests of the decoding and layout of sprite banks
 * (source/Engine/spritebankdecode.cpp): banks of each version that the
 * engine reads, the layout of their sprites in textures, and damaged files,
 * which must fail cleanly.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
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

// The old layout, which layoutOneRow keeps for comparisons.
static void testOldLayout () {
	std::string error;
	Layout l;

	// Two sprites that fit in one 511-wide row, then one that doesn't.
	DecodedBank bank = spritesOfWidths (std::vector<int> {100, 200, 300}, 10);
	CHECK (layoutOneRow (bank, false, l, error), "layout");
	CHECK (l.tex_x[0] == 1 && l.tex_x[1] == 102 && l.texNum[1] == 0, "first texture starts after a gap");
	CHECK (l.texNum[2] == 1 && l.tex_x[2] == 0, "a new texture starts without a gap");
	CHECK (l.pageWidth[0] == 303 && l.pageHeight[0] == 12, "first texture size");
	CHECK (l.pageWidth[1] == 300 && l.pageHeight[1] == 10, "later texture size, as always");

	// A sprite exactly 511 wide always starts a new texture.
	bank = spritesOfWidths (std::vector<int> {511}, 4);
	CHECK (layoutOneRow (bank, false, l, error) && l.pageWidth.size () == 2 && l.pageWidth[0] == 1 && l.texNum[0] == 1, "511 wide starts a new texture");

	// Wide sprites share textures up to 2047.
	bank = spritesOfWidths (std::vector<int> {1000, 1000, 1000}, 4);
	CHECK (layoutOneRow (bank, false, l, error) && l.texNum[1] == 0 && l.texNum[2] == 1, "wide sprites share up to 2047");

	CHECK (layoutOneRow (spritesOfWidths (std::vector<int> {100, 200, 300}, 10), true, l, error)
		&& l.pageWidth[0] == 512 && l.pageHeight[0] == 16 && l.pageWidth[1] == 512 && l.pageHeight[1] == 16, "power of two sizes");

	// More than 256 textures fail.
	CHECK (! layoutOneRow (spritesOfWidths (std::vector<int> (300, 1500), 1), false, l, error), "too many textures fail");

	// The pages: each sprite from the second row, the first row empty.
	bank = spritesOfWidths (std::vector<int> {2, 3, 511}, 2);
	Pages pages;
	CHECK (layoutOneRow (bank, false, l, error) && makePages (bank, l, false, pages, error), "pages");
	const Bytes & p0 = pages.colour[0];
	int w = l.pageWidth[0];
	CHECK (p0[(0 * w + 1) * 4] == 0 && p0[(1 * w + 1) * 4] == 1 && p0[(2 * w + 2) * 4] == 1, "first sprite in the second row");
	CHECK (p0[(1 * w + 3) * 4] == 0 && p0[(1 * w + 4) * 4] == 2, "a gap between sprites");
	// In a later texture, the last row of the first sprite doesn't fit.
	const Bytes & p1 = pages.colour[1];
	CHECK (l.pageHeight[1] == 2 && p1[(1 * 511) * 4] == 3 && pages.colour[1].size () == 511 * 2 * 4, "last row cut off, as always");
	CHECK (pages.burn.empty (), "no burn pages for sprites");
}


// Sprites of many sizes, from a fixed sequence, each pixel telling which
// sprite and which pixel it is: R = sprite number + 1, G = x, B = y.
static DecodedBank mixedSprites (int count, bool isFont) {
	DecodedBank bank;
	bank.version = 3;
	bank.isFont = isFont;
	bank.paletteSize = 0;
	unsigned int seed = 12345;
	for (int i = 0; i < count; i ++) {
		seed = seed * 1103515245 + 12345;
		DecodedSprite s;
		s.width = 1 + (seed >> 8) % 120;
		s.height = 1 + (seed >> 20) % 90;
		if (i == 7) { s.width = 2100; s.height = 30; }	// Too wide for a page
		if (i == 11) { s.width = 20; s.height = 2050; }	// Too tall
		s.xhot = s.yhot = 0;
		s.rgba.resize ((size_t) s.width * s.height * 4);
		for (int y = 0; y < s.height; y ++) {
			for (int x = 0; x < s.width; x ++) {
				unsigned char * p = &s.rgba[((size_t) y * s.width + x) * 4];
				p[0] = (unsigned char) (i + 1);
				p[1] = (unsigned char) x;
				p[2] = (unsigned char) y;
				p[3] = (x + y) % 3 ? 255 : 128;
			}
		}
		if (isFont) {
			s.burn.resize (s.rgba.size ());
			for (size_t k = 0; k < s.burn.size (); k += 4) {
				s.burn[k] = s.burn[k + 1] = s.burn[k + 2] = 255;
				s.burn[k + 3] = 200;
			}
		}
		bank.sprites.push_back (s);
	}
	return bank;
}

static bool isPowerOfTwo (int n) {
	return n > 0 && (n & (n - 1)) == 0;
}

static void testShelfLayout () {
	std::string error;
	for (int pot = 0; pot < 2; pot ++) {
		DecodedBank bank = mixedSprites (400, false);
		Layout l;
		CHECK (layout (bank, pot != 0, l, error), "shelf layout");
		size_t n = bank.sprites.size ();
		bool inside = true, fits = true, overlaps = false, sizes = true;
		for (size_t i = 0; i < n; i ++) {
			const DecodedSprite & s = bank.sprites[i];
			int p = l.texNum[i];
			if (p < 0 || p >= (int) l.pageWidth.size ()) { inside = false; continue; }
			// The sprite and its gutter inside the page.
			if (l.tex_x[i] - gutter < 0 || l.tex_y[i] - gutter < 0
					|| l.tex_x[i] + s.width + gutter > l.pageWidth[p] || l.tex_y[i] + s.height + gutter > l.pageHeight[p]) inside = false;
			bool big = s.width + 2 * gutter > pageSize || s.height + 2 * gutter > pageSize;
			if (big) {
				// Alone, in a page just big enough (before rounding up).
				for (size_t j = 0; j < n; j ++) if (j != i && l.texNum[j] == p) fits = false;
				if (! pot && (l.pageWidth[p] != s.width + 2 * gutter || l.pageHeight[p] != s.height + 2 * gutter)) fits = false;
			} else if (l.pageWidth[p] > pageSize || l.pageHeight[p] > pageSize) {
				fits = false;
			}
			// No two sprites' cells (sprite and gutter) overlap.
			for (size_t j = i + 1; j < n; j ++) {
				if (l.texNum[j] != p) continue;
				const DecodedSprite & t = bank.sprites[j];
				if (l.tex_x[i] - gutter < l.tex_x[j] + t.width + gutter && l.tex_x[j] - gutter < l.tex_x[i] + s.width + gutter
						&& l.tex_y[i] - gutter < l.tex_y[j] + t.height + gutter && l.tex_y[j] - gutter < l.tex_y[i] + s.height + gutter) overlaps = true;
			}
		}
		for (size_t p = 0; p < l.pageWidth.size (); p ++) {
			if (pot && (! isPowerOfTwo (l.pageWidth[p]) || ! isPowerOfTwo (l.pageHeight[p]))) sizes = false;
		}
		CHECK (inside, "every sprite and its gutter inside its page");
		CHECK (fits, "pages of at most 2048, and big sprites alone");
		CHECK (! overlaps, "no sprites or gutters overlap");
		CHECK (sizes, "power-of-two pages when asked");

		// The pages hold each sprite, by its own number, with empty gutters.
		Pages pages;
		CHECK (makePages (bank, l, false, pages, error), "shelf pages");
		bool same = true, empty = true;
		for (size_t i = 0; i < n; i ++) {
			const DecodedSprite & s = bank.sprites[i];
			int p = l.texNum[i], pw = l.pageWidth[p];
			const Bytes & page = pages.colour[p];
			for (int y = -gutter; y < s.height + gutter; y ++) {
				for (int x = -gutter; x < s.width + gutter; x ++) {
					const unsigned char * t = &page[((size_t) (l.tex_y[i] + y) * pw + l.tex_x[i] + x) * 4];
					if (x >= 0 && y >= 0 && x < s.width && y < s.height) {
						if (memcmp (t, &s.rgba[((size_t) y * s.width + x) * 4], 4)) same = false;
					} else if (t[0] || t[1] || t[2] || t[3]) {
						empty = false;
					}
				}
			}
		}
		CHECK (same, "each sprite in its place, with its own number");
		CHECK (empty, "gutters transparent black");
	}

	// The tallest first, so that shelves fill well: in the first page, no
	// shelf is lower than one below it.
	DecodedBank bank = spritesOfWidths (std::vector<int> {10, 20, 30}, 5);
	bank.sprites[1].height = 50;
	bank.sprites[1].rgba.assign (20 * 50 * 4, 2);
	Layout l;
	CHECK (layout (bank, false, l, error) && l.tex_y[1] == gutter && l.tex_x[1] == gutter, "the tallest sprite goes first");
	CHECK (l.texNum[0] == 0 && l.texNum[2] == 0 && l.pageWidth.size () == 1, "small banks fit one page");
}

static void testExtrusionAndBurn () {
	std::string error;
	DecodedBank bank = mixedSprites (60, true);
	Layout l;
	Pages plain, extruded;
	CHECK (layout (bank, false, l, error) && makePages (bank, l, false, plain, error) && makePages (bank, l, true, extruded, error), "font pages");
	bool burnPlaced = true, edges = true, alpha = true, inner = true;
	for (size_t i = 0; i < bank.sprites.size (); i ++) {
		const DecodedSprite & s = bank.sprites[i];
		int p = l.texNum[i], pw = l.pageWidth[p];
		for (int y = -gutter; y < s.height + gutter; y ++) {
			for (int x = -gutter; x < s.width + gutter; x ++) {
				size_t at = ((size_t) (l.tex_y[i] + y) * pw + l.tex_x[i] + x) * 4;
				bool in = x >= 0 && y >= 0 && x < s.width && y < s.height;
				// Burn pixels exactly where the colour pixels are.
				if (in != (plain.burn[p][at + 3] != 0)) burnPlaced = false;
				int sx = x < 0 ? 0 : (x >= s.width ? s.width - 1 : x);
				int sy = y < 0 ? 0 : (y >= s.height ? s.height - 1 : y);
				const unsigned char * edge = &s.rgba[((size_t) sy * s.width + sx) * 4];
				const unsigned char * t = &extruded.colour[p][at];
				if (in) {
					if (memcmp (t, edge, 4)) inner = false;
				} else {
					if (t[0] != edge[0] || t[1] != edge[1] || t[2] != edge[2]) edges = false;
					if (t[3] || extruded.burn[p][at + 3]) alpha = false;
					if (extruded.burn[p][at] != 255) edges = false;
				}
			}
		}
	}
	CHECK (burnPlaced, "burn pages placed exactly like the colour pages");
	CHECK (inner, "extrusion leaves the sprites as they are");
	CHECK (edges, "extruded gutters have the colour of the nearest edge pixel");
	CHECK (alpha, "extruded gutters stay transparent");
}

// The formulas of the draw functions before texRect, as they were.
static TexRect oldTexRect (int tex_x, int width, int height, int W, int H, TexKind kind) {
	TexRect r;
	if (kind == TEXKIND_SCALE) {
		r.u1 = (float)(tex_x) / W;
		r.v1 = (float) 1.0 / H;
		r.u2 = (float)(tex_x + width) / W;
		r.v2 = (float)(height + 1) / H;
	} else if (kind == TEXKIND_PASTE) {
		r.u1 = (float)(tex_x) / W;
		r.v1 = 0.0;
		r.u2 = (float)(tex_x + width) / W;
		r.v2 = (float)(height) / H;
	} else {
		r.u1 = (float)(tex_x - 0.5) / W;
		r.v1 = 0.0;
		r.u2 = (float)(tex_x + width + (kind == TEXKIND_FONT_FLIP ? 1.0 : 0.5)) / W;
		r.v2 = (float)(height + 2) / H;
	}
	return r;
}

static void testTexRect () {
	const TexKind kinds[] = { TEXKIND_SCALE, TEXKIND_PASTE, TEXKIND_FONT, TEXKIND_FONT_FLIP, TEXKIND_BURN };
	bool bits = true;
	unsigned int seed = 99;
	for (int n = 0; n < 2000; n ++) {
		seed = seed * 1103515245 + 12345;
		int W = 1 + (seed >> 4) % 2047, H = 3 + (seed >> 15) % 300;
		int w = 1 + (seed >> 9) % 200, h = 1 + (seed >> 22) % (H - 2);
		int x = (seed >> 13) % W;
		for (int k = 0; k < 5; k ++) {
			TexRect a = texRect (x, 1, w, h, W, H, kinds[k]), b = oldTexRect (x, w, h, W, H, kinds[k]);
			if (memcmp (&a, &b, sizeof (a))) bits = false;
		}
	}
	CHECK (bits, "with the sprite in row 1, texRect is the old formulas, bit for bit");

	// Relative to the sprite, every kind reaches as far as before: scale
	// exactly the sprite, paste from the row above, fonts half a texel either
	// side and a row above and below.
	const int W = 2048, H = 1531, x = 1237, y = 905, w = 37, h = 23;
	TexRect sc = texRect (x, y, w, h, W, H, TEXKIND_SCALE);
	TexRect pa = texRect (x, y, w, h, W, H, TEXKIND_PASTE);
	TexRect fo = texRect (x, y, w, h, W, H, TEXKIND_FONT);
	TexRect ff = texRect (x, y, w, h, W, H, TEXKIND_FONT_FLIP);
	const float e = 1e-3f;
	CHECK (fabsf (sc.u1 * W - x) < e && fabsf (sc.u2 * W - (x + w)) < e && fabsf (sc.v1 * H - y) < e && fabsf (sc.v2 * H - (y + h)) < e, "scale: the sprite exactly");
	CHECK (fabsf (pa.v1 * H - (y - 1)) < e && fabsf (pa.v2 * H - (y - 1 + h)) < e, "paste: from the row above");
	CHECK (fabsf (fo.u1 * W - (x - 0.5f)) < e && fabsf (fo.u2 * W - (x + w + 0.5f)) < e
		&& fabsf (fo.v1 * H - (y - 1)) < e && fabsf (fo.v2 * H - (y + h + 1)) < e, "font: half a texel and a row either side");
	CHECK (fabsf (ff.u2 * W - (x + w + 1)) < e, "flipped font: a whole texel on the right");

	// Pixel centres: drawing at 1:1, 2x and 0.5x, the centre of output pixel
	// i samples texel x + (i + 0.5) / scale.
	const float scales[] = { 1.0f, 2.0f, 0.5f };
	bool centres = true;
	for (int k = 0; k < 3; k ++) {
		int n = (int) (w * scales[k]);
		for (int i = 0; i < n; i ++) {
			float t = (i + 0.5f) / n;
			float u = sc.u1 + (sc.u2 - sc.u1) * t;
			float want = x + (i + 0.5f) * w / n;
			if (fabsf (u * W - want) > e) centres = false;
		}
	}
	CHECK (centres, "pixel centres sample texel centres at 1:1, 2x and 0.5x");
}

// Sample a page as nearest filtering with CLAMP_TO_EDGE does.
static const unsigned char * nearest (const Bytes & page, int W, int H, float u, float v) {
	int x = (int) floorf (u * W), y = (int) floorf (v * H);
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x >= W) x = W - 1;
	if (y >= H) y = H - 1;
	return &page[((size_t) y * W + x) * 4];
}

static void testNearestSampling () {
	std::string error;
	// At most 255 sprites, so that the red channel tells them apart.
	DecodedBank bank = mixedSprites (250, false);
	Layout shelf, old;
	Pages shelfPages, oldPages;
	CHECK (layout (bank, false, shelf, error) && makePages (bank, shelf, false, shelfPages, error), "shelf pages");
	CHECK (layoutOneRow (bank, false, old, error) && makePages (bank, old, false, oldPages, error), "old pages");

	const float scales[] = { 1.0f, 2.0f, 0.5f, 0.75f };
	int same = 0, ties = 0, wrong = 0, outside = 0;
	for (size_t i = 0; i < bank.sprites.size (); i ++) {
		const DecodedSprite & s = bank.sprites[i];
		if (s.width > 1000 || s.height > 1000) continue;
		int p = shelf.texNum[i], q = old.texNum[i];
		if (old.texNum[i] > 0 && old.tex_x[i] == 0) continue;	// The old quirks
		TexRect a = texRect (shelf.tex_x[i], shelf.tex_y[i], s.width, s.height, shelf.pageWidth[p], shelf.pageHeight[p], TEXKIND_SCALE);
		TexRect b = texRect (old.tex_x[i], old.tex_y[i], s.width, s.height, old.pageWidth[q], old.pageHeight[q], TEXKIND_SCALE);
		for (int k = 0; k < 4; k ++) {
			int nx = (int) (s.width * scales[k]), ny = (int) (s.height * scales[k]);
			for (int py = 0; py < ny; py ++) {
				for (int px = 0; px < nx; px ++) {
					float tx = (px + 0.5f) / nx, ty = (py + 0.5f) / ny;
					const unsigned char * na = nearest (shelfPages.colour[p], shelf.pageWidth[p], shelf.pageHeight[p],
						a.u1 + (a.u2 - a.u1) * tx, a.v1 + (a.v2 - a.v1) * ty);
					const unsigned char * nb = nearest (oldPages.colour[q], old.pageWidth[q], old.pageHeight[q],
						b.u1 + (b.u2 - b.u1) * tx, b.v1 + (b.v2 - b.v1) * ty);
					// Never a gutter or another sprite.
					if (na[0] != (unsigned char) (i + 1)) { outside ++; continue; }
					if (! memcmp (na, nb, 4)) { same ++; continue; }
					// Where the exact position lies on the edge between two
					// texels, either may come out: it depends on rounding.
					double ex = (px + 0.5) * s.width / nx, ey = (py + 0.5) * s.height / ny;
					bool tie = fabs (ex - floor (ex + 0.5)) < 1e-6 || fabs (ey - floor (ey + 0.5)) < 1e-6;
					if (tie && abs (na[1] - nb[1]) <= 1 && abs (na[2] - nb[2]) <= 1) ties ++;
					else wrong ++;
				}
			}
		}
	}
	CHECK (outside == 0, "nearest samples never reach a gutter or another sprite");
	CHECK (wrong == 0, "nearest samples read the old texels, except exactly between two");
	printf ("Nearest samples: %d as before, %d on a texel edge went the other way\n", same, ties);

	// At the smallest scales, the anti-aliasing samples of the smart scaler
	// never reach another sprite: down to a scale of 0.07. The shader samples
	// uv +- fwidth (uv) x scale, where fwidth is per pixel of the window and
	// scale is window / game x 0.25, at most 1. That is 1 / (4 x sprite
	// scale) texels away; we take a window 4 times the game, where the
	// fragments come closest to the sprite's edge.
	int reached = 0;
	for (size_t i = 0; i < bank.sprites.size (); i ++) {
		const DecodedSprite & s = bank.sprites[i];
		if (s.width > 1000 || s.height > 1000) continue;
		int p = shelf.texNum[i];
		int W = shelf.pageWidth[p], H = shelf.pageHeight[p];
		TexRect a = texRect (shelf.tex_x[i], shelf.tex_y[i], s.width, s.height, W, H, TEXKIND_SCALE);
		const float spriteScale = 0.07f, window = 4.0f;
		int nx = (int) (s.width * spriteScale * window), ny = (int) (s.height * spriteScale * window);
		if (nx < 1 || ny < 1) continue;
		float fu = (a.u2 - a.u1) / nx, fv = (a.v2 - a.v1) / ny;	// fwidth, times a scale of 1
		for (int py = 0; py < ny; py ++) {
			for (int px = 0; px < nx; px ++) {
				float u = a.u1 + fu * (px + 0.5f), v = a.v1 + fv * (py + 0.5f);
				for (int sx = -1; sx <= 1; sx += 2) {
					for (int sy = -1; sy <= 1; sy += 2) {
						const unsigned char * t = nearest (shelfPages.colour[p], W, H, u + sx * fu, v + sy * fv);
						if (t[3] && t[0] != (unsigned char) (i + 1)) reached ++;
					}
				}
			}
		}
	}
	CHECK (reached == 0, "anti-aliasing samples at scale 0.07 never reach another sprite");
}

int main () {
	testVersion0 ();
	testVersion2 ();
	testVersion3 ();
	testDamagedHeaders ();
	testOldLayout ();
	testShelfLayout ();
	testExtrusionAndBurn ();
	testTexRect ();
	testNearestSampling ();
	if (failures) {
		fprintf (stderr, "%d failures\n", failures);
		return 1;
	}
	printf ("All sprite bank tests passed\n");
	return 0;
}
