#include <stdio.h>
#include <stdint.h>

#include "tga.h"
#include "interface.h"
#include "moreio.h"
#include "sprites.h"
#include "draw2d.h"
#include "zbuffer.h"


bool loadZBufferFromTGA (const char * fileName, spriteBank *loadhere) {
	unsigned char * pointer;
	unsigned short int t1, t2;
	palCol thePalette[256];
	int numPanels = 1;
	int panels[16];
	int cutoff[16];
	int color, panel;
	unsigned char n;
	unsigned char * data;

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
	
	data = new unsigned char [imageHeader.width * imageHeader.height];
	panels[0] = cutoff[0] = 0;
	
	for (int row = 0; row < imageHeader.height; row ++) {
		// The cutoff of a layer is one past the lowest row it covers, counted from the top
		t2 = tgaRowFromTop (imageHeader, row) + 1;
		pointer = data + (t2-1)*imageHeader.width;
		for (t1 = 0; t1 < imageHeader.width; t1 ++) {
			color = readColFunction (fp, imageHeader.pixelDepth, thePalette, 0, 0);
			if (color) {
				for (n = 1; n < numPanels; n ++) {
					if (panels[n] == color) break;
				}
				if (n == numPanels) {
					if (n < 16) {
						panels[n] = color;
						numPanels ++;
						cutoff[n] = 0;

					} else {
						delete [] data;
						fclose (fp);
						errorBox ("Error reading TGA file", "Too many colours. Max 16 are allowed for making z-buffers. Each separate colour will become a z-buffer. If you need more, use sprites instead.");						
						return false;
					}
				}
				if (t2 > cutoff[n]) cutoff[n] = t2;
				panel = n;
			} else {
				panel = 0;
			}
			* (pointer ++) = panel;
		}
	}
	fclose (fp);
	
	if (numPanels<2) {
		delete [] data;
		errorBox ("Error reading TGA file", "Can't find any z-buffers in the file.");						
		return false;
	}
	
	loadhere->total = numPanels;
	loadhere->sprites = new sprite [loadhere->total];
	
	for (n = 0; n < loadhere->total; n ++) {
		loadhere->sprites[n].width = imageHeader.width;
		loadhere->sprites[n].height = -imageHeader.height;
		loadhere->sprites[n].xhot = 0;
		loadhere->sprites[n].yhot = 0;
		loadhere->sprites[n].special = cutoff[n];
		
		if (n)
			loadhere->sprites[n].data = new unsigned char [imageHeader.width * imageHeader.height];
		else
			loadhere->sprites[n].data = data;
	}
		
	for (int y = 0; y < imageHeader.height; y ++) {
		for (int x = 0; x < imageHeader.width; x ++) {
			n = loadhere->sprites[0].data[y*imageHeader.width+x];
			for (int i = 1; i < loadhere->total; i ++) {
				loadhere->sprites[i].data[y*imageHeader.width+x] = (n == i) ? 255: 0;
			}
		}
	}	
	
	spriteBankChanged (loadhere);
	return true;
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
