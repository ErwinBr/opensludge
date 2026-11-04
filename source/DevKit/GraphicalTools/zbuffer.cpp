#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>

#include <string>
#include <vector>

#include <png.h>

#include "tga.h"
#include "interface.h"
#include "moreio.h"
#include "sprites.h"
#include "draw2d.h"
#include "zbuffer.h"

// The most layers of a z-buffer, with the background: its file keeps the
// layer of a pixel in four bits.
static const int MAX_PANELS = 16;

// Make the layers of a z-buffer from the colours of a picture, `keys`, the
// top row first, in which 0 is the background and each other key a layer.
// The layers are numbered in the order that we meet their colours, going
// through the rows in the order of `rows` (from the top), the order of the
// file, so that a picture always makes the same layers.
// The cut-off of a layer is one past the lowest row it covers.
static bool buildZBufferLayers (const std::vector<uint32_t> & keys, int width, int height,
		const std::vector<int> & rows, spriteBank * loadhere, const char * errorTitle) {
	uint32_t panels[MAX_PANELS];
	int cutoff[MAX_PANELS];
	int numPanels = 1;
	panels[0] = cutoff[0] = 0;

	unsigned char * data = new unsigned char [width * height];
	for (int r = 0; r < height; r ++) {
		int y = rows[r];
		for (int x = 0; x < width; x ++) {
			uint32_t key = keys[y * width + x];
			int n = 0;
			if (key) {
				for (n = 1; n < numPanels; n ++) {
					if (panels[n] == key) break;
				}
				if (n == numPanels) {
					if (n == MAX_PANELS) {
						delete [] data;
						errorBox (errorTitle, "Too many colours. Max 16 are allowed for making z-buffers. Each separate colour will become a z-buffer. If you need more, use sprites instead.");
						return false;
					}
					panels[n] = key;
					cutoff[n] = 0;
					numPanels ++;
				}
				if (y + 1 > cutoff[n]) cutoff[n] = y + 1;
			}
			data[y * width + x] = n;
		}
	}

	if (numPanels < 2) {
		delete [] data;
		errorBox (errorTitle, "Can't find any z-buffers in the file.");
		return false;
	}

	loadhere->total = numPanels;
	loadhere->sprites = new sprite [loadhere->total];

	for (int n = 0; n < loadhere->total; n ++) {
		loadhere->sprites[n].width = width;
		loadhere->sprites[n].height = -height;
		loadhere->sprites[n].xhot = 0;
		loadhere->sprites[n].yhot = 0;
		loadhere->sprites[n].special = cutoff[n];

		if (n)
			loadhere->sprites[n].data = new unsigned char [width * height];
		else
			loadhere->sprites[n].data = data;
	}

	for (int p = 0; p < width * height; p ++) {
		int n = data[p];
		for (int i = 1; i < loadhere->total; i ++) {
			loadhere->sprites[i].data[p] = (n == i) ? 255: 0;
		}
	}

	spriteBankChanged (loadhere);
	return true;
}

bool loadZBufferFromTGA (const char * fileName, spriteBank *loadhere) {
	palCol thePalette[256];

	// Open the file
	FILE * fp = fopen (fileName, "rb");
	if (fp == NULL) {
		errorBox ("Error", "Can't open that image file!");
		return false;
	}

	// Grab the header
	TGAHeader imageHeader;
	const char * errorBack;
	errorBack = readTGAHeader (imageHeader, fp, thePalette);
	if (errorBack) {
		fclose (fp);
		errorBox ("Error reading TGA file", errorBack);
		return false;
	}

	unsigned short (* readColFunction) (FILE * fp, int bpc, palCol thePalette[], int x, int y) =
		imageHeader.compressed ? readCompressedColour : readAColour;

	int width = imageHeader.width, height = imageHeader.height;
	std::vector<uint32_t> keys ((size_t) width * height);
	std::vector<int> rows (height);
	for (int row = 0; row < height; row ++) {
		rows[row] = tgaRowFromTop (imageHeader, row);
		uint32_t * pointer = &keys[(size_t) rows[row] * width];
		for (int x = 0; x < width; x ++) {
			* (pointer ++) = readColFunction (fp, imageHeader.pixelDepth, thePalette, 0, 0);
		}
	}
	fclose (fp);

	return buildZBufferLayers (keys, width, height, rows, loadhere, "Error reading TGA file");
}

bool loadZBufferFromPNG (const char * fileName, spriteBank *loadhere) {
	png_image png;
	memset (&png, 0, sizeof (png));
	png.version = PNG_IMAGE_VERSION;
	if (! png_image_begin_read_from_file (&png, fileName)) {
		errorBox ("Error reading PNG file", png.message[0] ? png.message : "Can't open that image file!");
		return false;
	}
	png.format = PNG_FORMAT_RGBA;
	int width = png.width, height = png.height;
	std::vector<unsigned char> rgba (PNG_IMAGE_SIZE (png));
	if (! png_image_finish_read (&png, NULL, rgba.data (), 0, NULL)) {
		errorBox ("Error reading PNG file", png.message[0] ? png.message : "Can't read that image file!");
		png_image_free (&png);
		return false;
	}

	// Black and transparent pixels are the background; each other colour
	// is a layer.
	std::vector<uint32_t> keys ((size_t) width * height);
	std::vector<int> rows (height);
	for (int y = 0; y < height; y ++) rows[y] = y;
	for (size_t p = 0; p < keys.size (); p ++) {
		const unsigned char * c = &rgba[p * 4];
		keys[p] = c[3] < 128 ? 0 : ((uint32_t) c[0] << 16) | ((uint32_t) c[1] << 8) | c[2];
	}

	return buildZBufferLayers (keys, width, height, rows, loadhere, "Error reading PNG file");
}

bool loadZBufferFromImage (const char * fileName, spriteBank *loadhere) {
	std::string name (fileName);
	size_t dot = name.find_last_of ('.');
	std::string extension = dot == std::string::npos ? "" : name.substr (dot + 1);
	for (size_t i = 0; i < extension.size (); i ++) extension[i] = tolower ((unsigned char) extension[i]);
	if (extension == "png") return loadZBufferFromPNG (fileName, loadhere);
	return loadZBufferFromTGA (fileName, loadhere);
}


bool loadZBufferFile (const char * name, spriteBank *loadhere) {
	int n, i, x, y, zbWidth, zbHeight;
	uint32_t stillToGo = 0;
	
	FILE * fp = fopen (name, "rb");
	if (! fp) return false;
	if (fgetc (fp) != 'S') { fclose (fp); return false; }
	if (fgetc (fp) != 'z') { fclose (fp); return false; }
	if (fgetc (fp) != 'b') { fclose (fp); return false; }
	switch (fgetc (fp)) {
		case 0:
		zbWidth = 640;
		zbHeight = 480;
		break;
		
		case 1:
		zbWidth = get2bytes (fp);
		zbHeight = get2bytes (fp);
		break;
		
		default:
		fclose (fp);
		return false;
	}

	loadhere->total = fgetc (fp);
	loadhere->sprites = new sprite [loadhere->total];

	for (n = 0; n < loadhere->total; n ++) {
		loadhere->sprites[n].width = zbWidth;
		loadhere->sprites[n].height = -zbHeight;
		loadhere->sprites[n].xhot = 0;
		loadhere->sprites[n].yhot = 0;
		loadhere->sprites[n].special = get2bytes (fp);
		
		loadhere->sprites[n].data = new unsigned char [zbWidth * zbHeight];
	}
	
	for (y = 0; y < zbHeight; y ++) {
		for (x = 0; x < zbWidth; x ++) {
			if (stillToGo == 0) {
				n = fgetc (fp);
				stillToGo = n >> 4;
				if (stillToGo == 15) stillToGo = get2bytes (fp) + 16l;
				else stillToGo ++;
				n &= 15;
			}
			loadhere->sprites[0].data[y*zbWidth+x] = n;
			for (i = 1; i < loadhere->total; i ++) {
				loadhere->sprites[i].data[y*zbWidth+x] = (n == i) ? 255: 0;
			}
			stillToGo --;
		}
	}
	fclose (fp);
	spriteBankChanged (loadhere);
	return true;
}

unsigned char * zBufferLayerPixels (const spriteBank * buffers, int layer) {
	const sprite & single = buffers->sprites[layer];
	int size = single.width * -single.height;
	unsigned char * rgba = new unsigned char [size * 4];
	for (int p = 0; p < size; p ++) {
		rgba[p * 4] = rgba[p * 4 + 1] = rgba[p * 4 + 2] = 255;
		rgba[p * 4 + 3] = single.data[p];
	}
	return rgba;
}

bool saveZBufferFile (const char * name, spriteBank *buffers) {
	FILE * fp = fopen (name, "wb");
	if (! fp) return false;
	int n;
	uint32_t totalPixels = buffers->sprites[0].width * (-buffers->sprites[0].height);
	uint32_t thisPixel = 0;
	uint32_t countPixels = 0;
	unsigned short thisColour;

	fputc ('S', fp);
	fputc ('z', fp);
	fputc ('b', fp);
	fputc (1, fp);
	put2bytes (buffers->sprites[0].width, fp);
	put2bytes (-buffers->sprites[0].height, fp);
	fputc (buffers->total, fp);
	for (n = 0; n < buffers->total; n ++) {
		put2bytes (buffers->sprites[n].special, fp);
	}
		
	while (thisPixel < totalPixels) {
		thisColour = buffers->sprites[0].data[thisPixel];

		// Find how many pixels of the same colour follow this
		// (Not past the last pixel, which this once read beyond.)
		countPixels = thisPixel + 1;
		while (countPixels < totalPixels && thisColour == buffers->sprites[0].data[countPixels]) {
			countPixels ++;
			if (countPixels - thisPixel == 65551) break;
		}
		countPixels -= thisPixel;
		
		if (thisColour > 15) n = 0;
		if (countPixels < 16) {
			fputc (thisColour + ((unsigned char) ((countPixels - 1) << 4)), fp);
		} else {
			fputc (thisColour + 240, fp);
			put2bytes (countPixels - 16, fp);
		}
		thisPixel += countPixels;
	}
	return fclose (fp) == 0;
}

void drawZBufferView (Draw2D & draw, const struct spriteBank * backdrop, int selectedBuffer) {
	draw.clear ();

	int i;
	int b = selectedBuffer;

	if (backdrop->total>1)
		for (i=1; i< backdrop->total; i++) {
			if (i == b) {
				draw.setDrawingColour(1.0f, 1.0f, 1.0f, 1.0f);
			} else
				draw.setDrawingColour(0.2f, 0.0f, 0.5f, 1.0f);

			pasteSprite(draw, backdrop, i, true, false);
		}

	draw.setDrawingColour(1.0f, 0.35f, 0.35f, 1.0f);
	const float outline[] = {
		0.0, 0.0,
		(float) backdrop->sprites[0].width, 0.0,
		(float) backdrop->sprites[0].width, (float) -backdrop->sprites[0].height,
		0.0, (float) -backdrop->sprites[0].height
	};
	draw.drawLineLoop(outline, 4);

	if (b>0) {
		draw.setDrawingColour(0.0f, 1.0f, 0.0f, 0.7f);
		const float cutoff[] = {
			0.0, (float) backdrop->sprites[b].special,
			(float) backdrop->sprites[0].width, (float) backdrop->sprites[b].special
		};
		draw.drawLines(cutoff, 2);
	}
	draw.finish ();
}
