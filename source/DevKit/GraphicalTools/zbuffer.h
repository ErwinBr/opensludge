#include "sprites.h"

//bool processZBufferData ();
bool saveZBufferFile (const char * name, struct spriteBank *buffers);
bool loadZBufferFile (const char * name, struct spriteBank *loadhere);

// A layer of a z-buffer as RGBA pixels, the top row first: white where the
// layer is, and transparent elsewhere, so that the views draw it in the
// colour that they choose. Free it with delete [].
unsigned char * zBufferLayerPixels (const struct spriteBank * buffers, int layer);

// Make the layers of a z-buffer from a picture, in which each colour is a
// layer, on black or transparent, and the y of each is its lowest row.
bool loadZBufferFromTGA (const char * fileName, struct spriteBank *loadhere);
bool loadZBufferFromPNG (const char * fileName, struct spriteBank *loadhere);
// From a PNG or a TGA, by the extension of its name.
bool loadZBufferFromImage (const char * fileName, struct spriteBank *loadhere);

// Draw the z-buffers, the selected one in white and the others in purple,
// with the outline of the backdrop and the cut-off line of the selected
// z-buffer.
void drawZBufferView (Draw2D & draw, const struct spriteBank * backdrop, int selectedBuffer);
