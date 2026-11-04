#include <stdio.h>

struct TGAHeader {
	unsigned char IDBlockSize;
	unsigned char gotMap;
	bool compressed;
	unsigned short int firstPalColour;
	unsigned short int numPalColours;
	unsigned char bitsPerPalColour;
	unsigned short xOrigin;
	unsigned short yOrigin;
	unsigned short width;
	unsigned short height;
	unsigned char pixelDepth;
	unsigned char imageDescriptor;
};

struct palCol
{
	unsigned char r,g,b;
};

void grabRGBCompressed (FILE * fp, int bpc, unsigned char & r2, unsigned char & g2, unsigned char & b2, palCol thePalette[]);
void grabRGB (FILE * fp, int bpc, unsigned char & r, unsigned char & g, unsigned char & b, palCol thePalette[]);
void grabRGBACompressed (FILE * fp, int bpc, unsigned char & r2, unsigned char & g2, unsigned char & b2, unsigned char & a2, palCol thePalette[]);
void grabRGBA (FILE * fp, int bpc, unsigned char & r, unsigned char & g, unsigned char & b,unsigned char & a, palCol thePalette[]);

unsigned short int makeColour (unsigned char r, unsigned char g, unsigned char b);
unsigned short readAColour (FILE * fp, int bpc, palCol thePalette[], int x, int y);
unsigned short readCompressedColour (FILE * fp, int bpc, palCol thePalette[], int x, int y);
const char * readTGAHeader (TGAHeader & h, FILE * fp, palCol thePalette[]);

// The pixels of a TGA come row by row, from the bottom row up, unless bit 5
// of the image descriptor says they come from the top row down (as Krita,
// GIMP and Photoshop often write them). Gives the row, counted from the top,
// that the n-th row in the file belongs on.
inline int tgaRowFromTop (const TGAHeader & h, int n) {
	return (h.imageDescriptor & 32) ? n : h.height - 1 - n;
}
void setDither (int dither);
bool getDither ();

inline unsigned short redValue (unsigned short c) { return (c >> 11) << 3; }
inline unsigned short greenValue (unsigned short c) { return ((c >> 5) & 63) << 2; }
inline unsigned short blueValue (unsigned short c) { return (c & 31) << 3; }
inline int brightness (unsigned short c) {
	return ((int) redValue (c)) + ((int) greenValue (c)) + ((int) blueValue (c) >> 1);
}
