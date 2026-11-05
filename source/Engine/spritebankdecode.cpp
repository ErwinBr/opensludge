#include "spritebankdecode.h"

#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <setjmp.h>
#include <png.h>

namespace spritebank {

bool Reader::byte (int & value) {
	if (remaining () < 1) return false;
	value = data[pos ++];
	return true;
}

bool Reader::be16 (int & value) {
	if (remaining () < 2) return false;
	value = data[pos] * 256 + data[pos + 1];
	pos += 2;
	return true;
}

bool Reader::le16Signed (int & value) {
	if (remaining () < 2) return false;
	value = (int16_t) (uint16_t) (data[pos] | (data[pos + 1] << 8));
	pos += 2;
	return true;
}

bool Reader::bytes (unsigned char * to, size_t n) {
	if (remaining () < n) return false;
	memcpy (to, data + pos, n);
	pos += n;
	return true;
}

bool Reader::skip (size_t n) {
	if (remaining () < n) return false;
	pos += n;
	return true;
}

int nextPowerOfTwo (int n) {
	// The same as getNextPOT in graphics.cpp.
	-- n;
	n |= n >> 16;
	n |= n >> 8;
	n |= n >> 4;
	n |= n >> 2;
	n |= n >> 1;
	return ++ n;
}

namespace {

// Counts the bytes that a bank decodes to, and refuses more than maxBankBytes.
class Budget {
public:
	Budget () : used (0) {}
	bool take (uint64_t width, uint64_t height, uint64_t bytesPerPixel) {
		uint64_t n = width * height * bytesPerPixel;	// Each is at most maxDimension, so this can't overflow
		if (n > maxBankBytes - used) return false;
		used += n;
		return true;
	}
private:
	uint64_t used;
};

bool fail (std::string & error, const char * message) {
	error = message;
	return false;
}

// PNG decoding. libpng reports errors with longjmp, which mustn't jump over
// C++ objects with destructors, so this part holds only plain C data: the
// png structures, and pixels in memory from malloc.

struct PngSource {
	const unsigned char * data;
	size_t size, pos;
};

struct PngOut {
	unsigned char * rgba;	// malloc'd, width * height * 4
	uint32_t width, height;
};

enum PngResult { PNG_OK, PNG_BROKEN, PNG_TOO_BIG, PNG_NO_MEMORY };

void readPngBytes (png_structp png, png_bytep to, png_size_t n) {
	PngSource * source = (PngSource *) png_get_io_ptr (png);
	if (source -> size - source -> pos < n) png_error (png, "The PNG ends too soon");
	memcpy (to, source -> data + source -> pos, n);
	source -> pos += n;
}

void ignorePngWarning (png_structp, png_const_charp) {}

// We report errors ourselves, without libpng's message on stderr.
void quietPngError (png_structp png, png_const_charp) {
	png_longjmp (png, 1);
}

PngResult decodePngRgba (PngSource * source, PngOut * out) {
	png_structp png = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, quietPngError, ignorePngWarning);
	if (! png) return PNG_NO_MEMORY;
	png_infop info = png_create_info_struct (png);
	if (! info) {
		png_destroy_read_struct (&png, NULL, NULL);
		return PNG_NO_MEMORY;
	}

	// Changed after setjmp, so volatile.
	unsigned char * volatile pixels = NULL;
	png_bytep * volatile rows = NULL;
	volatile PngResult result = PNG_BROKEN;

	if (setjmp (png_jmpbuf (png))) {
		free (pixels);
		free (rows);
		png_destroy_read_struct (&png, &info, NULL);
		return result;
	}

	png_set_read_fn (png, source, readPngBytes);
	png_set_sig_bytes (png, 8);		// The compiler leaves out the signature
	png_set_user_limits (png, maxDimension, maxDimension);
	png_set_chunk_malloc_max (png, 64 * 1024 * 1024);

	png_read_info (png, info);

	png_uint_32 width, height;
	int bitDepth, colourType, interlace;
	png_get_IHDR (png, info, &width, &height, &bitDepth, &colourType, &interlace, NULL, NULL);
	if (! width || ! height || width > (png_uint_32) maxDimension || height > (png_uint_32) maxDimension) {
		result = PNG_TOO_BIG;
		png_error (png, "Bad size");
	}

	// Whatever the PNG holds, we want 8-bit RGBA. The compiler writes 8-bit
	// RGBA, so none of these change the pixels of its banks.
	if (colourType == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb (png);
	if (colourType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) png_set_expand_gray_1_2_4_to_8 (png);
	if (png_get_valid (png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha (png);
	if (bitDepth == 16) png_set_strip_16 (png);
	if (colourType == PNG_COLOR_TYPE_GRAY || colourType == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb (png);
	if (! (colourType & PNG_COLOR_MASK_ALPHA) && ! png_get_valid (png, info, PNG_INFO_tRNS)) {
		png_set_add_alpha (png, 0xff, PNG_FILLER_AFTER);
	}
	png_set_interlace_handling (png);
	png_read_update_info (png, info);

	if (png_get_rowbytes (png, info) != (png_size_t) width * 4) png_error (png, "Not RGBA");

	result = PNG_NO_MEMORY;
	pixels = (unsigned char *) malloc ((size_t) width * height * 4);
	rows = (png_bytep *) malloc (sizeof (png_bytep) * height);
	if (! pixels || ! rows) png_error (png, "Out of memory");
	result = PNG_BROKEN;

	for (png_uint_32 y = 0; y < height; y ++) rows[y] = pixels + (size_t) y * width * 4;
	png_read_image (png, rows);
	png_read_end (png, NULL);

	free (rows);
	png_destroy_read_struct (&png, &info, NULL);
	out -> rgba = pixels;
	out -> width = width;
	out -> height = height;
	return PNG_OK;
}

bool readPngSprite (Reader & in, DecodedSprite & sprite, Budget & budget, std::string & error) {
	PngSource source = { in.here (), in.remaining (), 0 };
	PngOut out = { NULL, 0, 0 };
	switch (decodePngRgba (&source, &out)) {
		case PNG_OK: break;
		case PNG_TOO_BIG: return fail (error, "Can't open sprite bank / font - a sprite is too big.");
		case PNG_NO_MEMORY: return fail (error, "Can't open sprite bank / font - out of memory.");
		default: return fail (error, "Can't open sprite bank / font - a sprite is damaged.");
	}
	bool ok = budget.take (out.width, out.height, 4);
	if (ok) {
		sprite.width = out.width;
		sprite.height = out.height;
		sprite.rgba.assign (out.rgba, out.rgba + (size_t) out.width * out.height * 4);
	}
	free (out.rgba);
	if (! ok) return fail (error, "Can't open sprite bank / font - it's too big.");
	in.skip (source.pos);
	return true;
}

// Run-length compressed palette indices (version 2): a value above the
// number of colours is a run, with its length in the next byte.
bool readRunLengthIndices (Reader & in, int numColours, unsigned char * to, size_t size) {
	size_t pip = 0;
	while (pip < size) {
		int col, looper = 1;
		if (! in.byte (col)) return false;
		if (col > numColours) {
			col -= numColours + 1;
			if (! in.byte (looper)) return false;
			looper ++;
		}
		// As the engine always did, a run stops at the end of the sprite
		// (before, it wrote past the end).
		while (looper -- && pip < size) to[pip ++] = (unsigned char) col;
	}
	return true;
}

}

bool decode (Reader & in, bool isFont, DecodedBank & bank, std::string & error) {
	const char * truncated = "Can't open sprite bank / font - the file ends too soon.";
	int total, version = 0, numColours = 0, startIndex = 0;

	bank.isFont = isFont;
	bank.paletteSize = 0;
	bank.sprites.clear ();

	if (! in.be16 (total)) return fail (error, truncated);
	if (! total) {
		if (! in.byte (version)) return fail (error, truncated);
		if (version == 1) {
			total = 0;
		} else if (! in.be16 (total)) return fail (error, truncated);
	}
	if (total <= 0) return fail (error, "No sprites in bank or invalid sprite bank file");
	if (version > 3) return fail (error, "Unsupported sprite bank file format");
	bank.version = version;

	if (version && version < 3) {
		if (! in.byte (numColours)) return fail (error, truncated);
		startIndex = 1;
	}

	Budget budget;
	bank.sprites.resize (total);
	// Palette indices of each sprite, until we have read the palette after them.
	std::vector<std::vector<unsigned char> > indices (version < 3 ? total : 0);

	for (int i = 0; i < total; i ++) {
		DecodedSprite & sp = bank.sprites[i];
		switch (version) {
			case 3:
			if (! in.le16Signed (sp.xhot) || ! in.le16Signed (sp.yhot)) return fail (error, truncated);
			if (! readPngSprite (in, sp, budget, error)) return false;
			break;

			case 2:
			if (! in.be16 (sp.width) || ! in.be16 (sp.height)) return fail (error, truncated);
			if (! in.le16Signed (sp.xhot) || ! in.le16Signed (sp.yhot)) return fail (error, truncated);
			break;

			default:
			// Version 0 has unsigned hotspots of one byte.
			if (! in.byte (sp.width) || ! in.byte (sp.height) || ! in.byte (sp.xhot) || ! in.byte (sp.yhot)) return fail (error, truncated);
			break;
		}

		if (version < 3) {
			if (sp.width > maxDimension || sp.height > maxDimension) return fail (error, "Can't open sprite bank / font - a sprite is too big.");
			if (! budget.take (sp.width, sp.height, isFont ? 9 : 5)) return fail (error, "Can't open sprite bank / font - it's too big.");
			size_t size = (size_t) sp.width * sp.height;
			indices[i].resize (size);
			if (version == 2) {
				if (! readRunLengthIndices (in, numColours, indices[i].data (), size)) return fail (error, truncated);
			} else {
				if (! in.bytes (indices[i].data (), size)) return fail (error, truncated);
			}
		}
	}

	if (version == 3) return true;

	if (! version) {
		if (! in.byte (numColours) || ! in.byte (startIndex)) return fail (error, truncated);
	}

	// The palette. Index 0 is transparent. An index that the palette doesn't
	// have was undefined before (we read past the palette); now it's black.
	unsigned char r[256 * 2] = {0}, g[256 * 2] = {0}, b[256 * 2] = {0};
	bank.paletteSize = numColours + startIndex;
	for (int c = 0; c < numColours; c ++) {
		int cr, cg, cb;
		if (! in.byte (cr) || ! in.byte (cg) || ! in.byte (cb)) return fail (error, truncated);
		r[c + startIndex] = cr;
		g[c + startIndex] = cg;
		b[c + startIndex] = cb;
	}

	for (int i = 0; i < total; i ++) {
		DecodedSprite & sp = bank.sprites[i];
		const std::vector<unsigned char> & idx = indices[i];
		size_t size = idx.size ();
		sp.rgba.assign (size * 4, 0);
		if (isFont) sp.burn.assign (size * 4, 0);

		// Transparent pixels take the colour of the last opaque pixel, and
		// at first the colour of the first opaque pixel.
		int transColour = -1;
		for (size_t p = 0; p < size; p ++) {
			if (idx[p]) {
				transColour = idx[p];
				break;
			}
		}

		for (size_t p = 0; p < size; p ++) {
			int s = idx[p];
			unsigned char * target = &sp.rgba[p * 4];
			if (s) {
				target[0] = r[s];
				target[1] = g[s];
				target[2] = b[s];
				target[3] = 255;
				transColour = s;
			} else if (transColour >= 0) {
				target[0] = r[transColour];
				target[1] = g[transColour];
				target[2] = b[transColour];
				target[3] = 0;
			}
			if (isFont) {
				unsigned char * burn = &sp.burn[p * 4];
				burn[0] = burn[1] = burn[2] = 255;
				if (s) burn[3] = r[s];
			}
		}
		std::vector<unsigned char> ().swap (indices[i]);
	}
	return true;
}

namespace {

bool checkPages (const DecodedBank & bank, Layout & out, bool powerOfTwo, std::string & error) {
	uint64_t bytes = 0;
	for (size_t p = 0; p < out.pageWidth.size (); p ++) {
		if (powerOfTwo) {
			out.pageWidth[p] = nextPowerOfTwo (out.pageWidth[p]);
			out.pageHeight[p] = nextPowerOfTwo (out.pageHeight[p]);
		}
		if (out.pageWidth[p] > maxDimension || out.pageHeight[p] > maxDimension) {
			return fail (error, "Can't open sprite bank / font - a texture would be too big.");
		}
		bytes += (uint64_t) out.pageWidth[p] * out.pageHeight[p] * 4 * (bank.isFont ? 2 : 1);
		if (bytes > maxBankBytes) return fail (error, "Can't open sprite bank / font - it's too big.");
	}
	return true;
}

struct Shelf {
	int y, height, used;
};

}

bool layoutOneRow (const DecodedBank & bank, bool powerOfTwo, Layout & out, std::string & error) {
	size_t total = bank.sprites.size ();
	out.tex_x.assign (total, 0);
	out.tex_y.assign (total, 1);
	out.texNum.assign (total, 0);
	out.pageWidth.assign (1, 1);
	out.pageHeight.assign (1, 1);

	int n = 0;
	for (size_t i = 0; i < total; i ++) {
		int w = bank.sprites[i].width, h = bank.sprites[i].height;
		// Exactly as before: a sprite exactly 511 wide always starts a new
		// texture, and the first sprite of each texture after the first has
		// no gap to its left and no empty row below it.
		if (((w > 511) && (out.pageWidth[n] + w < 2047)) || ((w < 511) && (out.pageWidth[n] + w < 511))) {
			out.tex_x[i] = out.pageWidth[n];
			out.pageWidth[n] += w + 1;
			if (h + 2 > out.pageHeight[n]) out.pageHeight[n] = h + 2;
		} else {
			n ++;
			if (n > 255) return fail (error, "Can't open sprite bank / font - it's too big.");
			out.tex_x[i] = 0;
			out.pageWidth.push_back (w);
			out.pageHeight.push_back (h);
		}
		out.texNum[i] = n;
	}

	return checkPages (bank, out, powerOfTwo, error);
}

bool layout (const DecodedBank & bank, bool powerOfTwo, Layout & out, std::string & error) {
	size_t total = bank.sprites.size ();
	out.tex_x.assign (total, 0);
	out.tex_y.assign (total, 0);
	out.texNum.assign (total, 0);
	out.pageWidth.clear ();
	out.pageHeight.clear ();

	// The tallest first; the sprites keep their numbers.
	std::vector<int> order (total);
	for (size_t i = 0; i < total; i ++) order[i] = (int) i;
	std::stable_sort (order.begin (), order.end (), [&bank] (int a, int b) {
		return bank.sprites[a].height > bank.sprites[b].height;
	});

	// For each page we fill: its shelves, and the height they take.
	std::vector<std::vector<Shelf> > shelves;
	std::vector<int> usedHeight;

	for (size_t o = 0; o < total; o ++) {
		int i = order[o];
		int w = bank.sprites[i].width + 2 * gutter, h = bank.sprites[i].height + 2 * gutter;

		int page = -1, x = 0, y = 0;
		if (w > pageSize || h > pageSize) {
			// A page of its own, just big enough.
			page = (int) out.pageWidth.size ();
			out.pageWidth.push_back (w);
			out.pageHeight.push_back (h);
			shelves.push_back (std::vector<Shelf> ());
			usedHeight.push_back (pageSize + 1);	// Nothing else goes in
		} else {
			// The first shelf with room, in any page; else a new shelf; else a new page.
			for (size_t p = 0; p < shelves.size () && page < 0; p ++) {
				for (size_t sh = 0; sh < shelves[p].size (); sh ++) {
					Shelf & shelf = shelves[p][sh];
					if (shelf.height >= h && shelf.used + w <= pageSize) {
						page = (int) p;
						x = shelf.used;
						y = shelf.y;
						shelf.used += w;
						break;
					}
				}
			}
			for (size_t p = 0; p < shelves.size () && page < 0; p ++) {
				if (usedHeight[p] + h <= pageSize) {
					page = (int) p;
					Shelf shelf = { usedHeight[p], h, w };
					shelves[p].push_back (shelf);
					y = usedHeight[p];
					usedHeight[p] += h;
				}
			}
			if (page < 0) {
				page = (int) out.pageWidth.size ();
				if (page > 255) return fail (error, "Can't open sprite bank / font - it's too big.");
				out.pageWidth.push_back (0);
				out.pageHeight.push_back (0);
				Shelf shelf = { 0, h, w };
				shelves.push_back (std::vector<Shelf> (1, shelf));
				usedHeight.push_back (h);
			}
			if (x + w > out.pageWidth[page]) out.pageWidth[page] = x + w;
			if (y + h > out.pageHeight[page]) out.pageHeight[page] = y + h;
		}
		out.texNum[i] = page;
		out.tex_x[i] = x + gutter;
		out.tex_y[i] = y + gutter;
	}

	return checkPages (bank, out, powerOfTwo, error);
}

// These are the formulas of the draw functions in sprites.cpp from before
// sprites had a tex_y, with the row the sprite starts in (then always 1) as
// tex_y. They must stay the same, float for float.
TexRect texRect (int tex_x, int tex_y, int width, int height, int pageW, int pageH, TexKind kind) {
	TexRect r;
	switch (kind) {
		case TEXKIND_SCALE:
		r.u1 = (float) (tex_x) / pageW;
		r.v1 = (float) (tex_y) / pageH;
		r.u2 = (float) (tex_x + width) / pageW;
		r.v2 = (float) (tex_y + height) / pageH;
		break;

		case TEXKIND_PASTE:
		r.u1 = (float) (tex_x) / pageW;
		r.v1 = (float) (tex_y - 1) / pageH;
		r.u2 = (float) (tex_x + width) / pageW;
		r.v2 = (float) (tex_y - 1 + height) / pageH;
		break;

		case TEXKIND_FONT:
		case TEXKIND_FONT_FLIP:
		case TEXKIND_BURN:
		r.u1 = (float) (tex_x - 0.5) / pageW;
		r.v1 = (float) (tex_y - 1) / pageH;
		r.u2 = (float) (tex_x + width + (kind == TEXKIND_FONT_FLIP ? 1.0 : 0.5)) / pageW;
		r.v2 = (float) (tex_y - 1 + height + 2) / pageH;
		break;
	}
	return r;
}

namespace {

// Give the gutter around a sprite at (x0, y0), w x h, the colour of the
// nearest pixel of the sprite, with alpha 0.
void extrudeGutter (std::vector<unsigned char> & page, int pw, int ph, int x0, int y0, int w, int h) {
	if (w <= 0 || h <= 0) return;
	for (int y = y0 - gutter; y < y0 + h + gutter; y ++) {
		if (y < 0 || y >= ph) continue;
		int sy = y < y0 ? y0 : (y >= y0 + h ? y0 + h - 1 : y);
		if (sy >= ph) continue;
		for (int x = x0 - gutter; x < x0 + w + gutter; x ++) {
			if (x < 0 || x >= pw) continue;
			if (y >= y0 && y < y0 + h && x >= x0 && x < x0 + w) continue;
			int sx = x < x0 ? x0 : (x >= x0 + w ? x0 + w - 1 : x);
			unsigned char * to = &page[((size_t) y * pw + x) * 4];
			const unsigned char * from = &page[((size_t) sy * pw + sx) * 4];
			to[0] = from[0];
			to[1] = from[1];
			to[2] = from[2];
			to[3] = 0;
		}
	}
}

}

bool makePages (const DecodedBank & bank, const Layout & layout, bool extrude, Pages & out, std::string & error) {
	size_t numPages = layout.pageWidth.size ();
	out.colour.assign (numPages, std::vector<unsigned char> ());
	out.burn.assign (bank.isFont ? numPages : 0, std::vector<unsigned char> ());
	for (size_t p = 0; p < numPages; p ++) {
		size_t size = (size_t) layout.pageWidth[p] * layout.pageHeight[p] * 4;
		out.colour[p].assign (size, 0);
		if (bank.isFont) out.burn[p].assign (size, 0);
	}

	for (size_t i = 0; i < bank.sprites.size (); i ++) {
		const DecodedSprite & sp = bank.sprites[i];
		int page = layout.texNum[i];
		int pw = layout.pageWidth[page], ph = layout.pageHeight[page];
		int x0 = layout.tex_x[i], y0 = layout.tex_y[i];
		if (x0 < 0 || x0 + sp.width > pw || y0 < 0) return fail (error, "Can't open sprite bank / font - bad layout.");
		size_t rowBytes = (size_t) sp.width * 4;
		// In the old layout (layoutOneRow), the last row of a sprite may not
		// fit in its texture; it was never drawn.
		for (int y = 0; y < sp.height && y0 + y < ph; y ++) {
			size_t at = ((size_t) (y0 + y) * pw + x0) * 4;
			memcpy (&out.colour[page][at], &sp.rgba[y * rowBytes], rowBytes);
			if (bank.isFont && ! sp.burn.empty ()) memcpy (&out.burn[page][at], &sp.burn[y * rowBytes], rowBytes);
		}
		if (extrude) {
			extrudeGutter (out.colour[page], pw, ph, x0, y0, sp.width, sp.height);
			if (bank.isFont) extrudeGutter (out.burn[page], pw, ph, x0, y0, sp.width, sp.height);
		}
	}
	return true;
}

}
