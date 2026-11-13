#include <vector>
#include "variable.h"
#include "graphics.h"

#include "sludge_gl.h"

enum {
	LIGHTMAPMODE_NONE		= -1,
	LIGHTMAPMODE_HOTSPOT,
	LIGHTMAPMODE_PIXEL,
	LIGHTMAPMODE_NUM
};

extern unsigned int winWidth, winHeight, sceneWidth, sceneHeight;
extern int lightMapMode;

struct parallaxLayer {
	GLubyte * texture;
	gfx::Texture * tex;
	int width, height, speedX, speedY;
	bool wrapS, wrapT;
	unsigned short fileNum, fractionX, fractionY;
	int cameraX, cameraY;
	parallaxLayer * next;
	parallaxLayer * prev;
};

// The backdrop, and the target for drawing into it.
extern gfx::Texture * backdropTex;
extern gfx::RenderTarget * backdropTarget;
// Make t the backdrop texture, with a new target.
void setBackdropTexture (gfx::Texture * t);
// Copy the backdrop, so that we can draw from the copy while we draw into
// the backdrop. The copy is valid until the next call.
gfx::Texture * copyOfBackdrop ();

extern gfx::Texture * snapshotTex;

bool resizeBackdrop (int x, int y);
void killBackDrop ();
void loadBackDrop (int fileNum, int x, int y);
void mixBackDrop (int fileNum, int x, int y);
void drawBackDrop ();
void blankScreen (int x1, int y1, int x2, int y2);
void darkScreen ();
void saveHSI (FILE * writer);
void saveCoreHSI (FILE * writer, gfx::Texture * texture, int w, int h);
bool loadHSI (FILE * fp, int, int, bool);
bool mixHSI (FILE * fp, int x = 0, int y = 0);
void drawHorizontalLine (unsigned int, unsigned int, unsigned int);
void drawVerticalLine (unsigned int, unsigned int, unsigned int);
void hardScroll (int distance);
bool getRGBIntoStack (unsigned int x, unsigned int y, stackHandler * sH);

// Read the image (PNG or HSI) of a file of the game into rgba, w * h pixels
// of 4 bytes, without padding. What is for the error message.
bool loadImageFile (int fileNum, const char * what, int & w, int & h, std::vector<GLubyte> & rgba);

// Also the light map stuff

void killLightMap ();
bool loadLightMap (int v);

extern texture lightMap;


// And background parallax scrolling

void killParallax ();
bool loadParallax (unsigned short v, unsigned short fracX, unsigned short fracY);
void saveParallaxRecursive (parallaxLayer * me, FILE * fp);
void reloadParallaxTextures ();

void nosnapshot ();
bool snapshot ();
void saveSnapshot(FILE * fp);
bool restoreSnapshot (FILE * fp);
