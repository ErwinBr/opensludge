#include <string.h>
#include <stdlib.h>

#include "allfiles.h"
#include "debug.h"
#include "graphics.h"
#include "newfatal.h"
#include "sprites.h"
#include "sprbanks.h"
#include "people.h"
#include "sludger.h"
#include "objtypes.h"
#include "region.h"
#include "backdrop.h"
#include "talk.h"
#include "fonttext.h"
#include "statusba.h"
#include "freeze.h"
#include "cursors.h"
#include "zbuffer.h"

extern onScreenPerson * allPeople;
extern screenRegion * allScreenRegions;
extern screenRegion * overRegion;
extern speechStruct * speech;
extern inputType input;
extern parallaxLayer * parallaxStuff;
extern int lightMapNumber, zBufferNumber;
extern eventHandlers * currentEvents;
extern int cameraX, cameraY;
extern unsigned int sceneWidth, sceneHeight;
extern float cameraZoom;
extern zBufferData zBuffer;
extern bool backdropExists;

frozenStuffStruct * frozenStuff = NULL;

extern unsigned int sceneWidth, sceneHeight;

void shufflePeople ();

gfx::Texture * freezeTex = NULL;

void freezeGraphics() {
	int w = winWidth;
	int h = winHeight;
	if (! NPOT_textures) {
		w = getNextPOT(winWidth);
		h = getNextPOT(winHeight);
	}

	freezeTex = gfx::renderer->createTexture (w, h, gfx::TEX_RGBA8, NULL);
	gfx::renderer->setTextureWrap (freezeTex, gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
	gfx::renderer->setTextureFilter (freezeTex, gfx::FILTER_NEAREST, gfx::FILTER_NEAREST);
	gfx::RenderTarget * target = gfx::renderer->createTarget (freezeTex, true);

	// Temporarily disable AA
	int antiAlias = gameSettings.antiAlias;
	gameSettings.antiAlias = 0;
	

	int x=0;
	while (x < winWidth) {
		int y=0;
		
		if (winWidth-x < realWinWidth) {
			w = winWidth-x;
		} else {
			w = realWinWidth;
		}
		
		while (y < winHeight) {
		
			if (winHeight-y < realWinHeight) {
				h = winHeight-y;
			} else {
				h = realWinHeight;
			}

			const GLfloat bPMVMatrix[] =
			{
			2.0f/realWinWidth*cameraZoom,                            .0,   .0,  .0,
			                          .0, 2.0f/realWinHeight*cameraZoom,   .0,  .0,
			                          .0,                            .0, 1.0f,  .0,
			 -2.0f*(x/realWinWidth)-1.0f,  -2.0f*(y/realWinHeight)-1.0f,   .0, 1.0f

			};
			for (int i = 0; i < 16; i++)
			{
				aPMVMatrix[i] = bPMVMatrix[i];
			}
			
			// Render scene, as if at the bottom left of the window, and
			// copied from there into the texture
			gfx::renderer->setTarget (target);
			gfx::renderer->setViewport (x, y, realWinWidth, realWinHeight);
			gfx::renderer->setScissor (x, y, w, h);
			gfx::renderer->clear (true, true);

			drawBackDrop ();				// Draw the room
			drawZBuffer(cameraX, cameraY);
			// Legacy: freezing has always moved the characters' animations on
			// by a tick (firing their frame events) for each tile it draws,
			// because drawing did. Games time on it, so we keep it.
			updatePeopleAnimations (1);
			drawPeople ();					// Then add any moving characters...
			
			y += h;
		}
		x += w;
	}

	gameSettings.antiAlias = antiAlias;

	gfx::renderer->releaseTarget (target);
	drawIntoWindow ();
	setPixelCoords(false);
}

bool freeze () {
	debugOut("calling freeze()\n");
	frozenStuffStruct * newFreezer = new frozenStuffStruct;
	if (! checkNew (newFreezer)) return false;

	// Grab a copy of the current scene
	freezeGraphics();

	newFreezer -> hadBackdrop = backdropTex != NULL;

	int picWidth = sceneWidth;
	int picHeight = sceneHeight;
	if (! NPOT_textures) {
		picWidth = getNextPOT(picWidth);
		picHeight = getNextPOT(picHeight);
	}
	newFreezer -> backdropTexture = new GLubyte [picHeight*picWidth*4];
	if (! checkNew (newFreezer -> backdropTexture)) return false;	

	gfx::Texture * oldBackdrop = backdropTex;
	if (oldBackdrop) gfx::renderer->readTexture (oldBackdrop, newFreezer->backdropTexture);
	setBackdropTexture (NULL);

	newFreezer -> sceneWidth = sceneWidth;
	newFreezer -> sceneHeight = sceneHeight;
	newFreezer -> cameraX = cameraX;
	newFreezer -> cameraY = cameraY;
	newFreezer -> cameraZoom = cameraZoom;

	newFreezer -> lightMapTexture = lightMap.data;
	gfx::Texture * oldLightMap = lightMap.tex;
	newFreezer -> lightMapNumber = lightMapNumber;
	lightMap.data = NULL;
	lightMap.tex = NULL;

	newFreezer -> parallaxStuff = parallaxStuff;
	parallaxStuff = NULL;

	newFreezer -> zBufferImage = zBuffer.tex;
	newFreezer -> zBufferNumber = zBuffer.originalNum;
	newFreezer -> zPanels = zBuffer.numPanels;
	zBuffer.tex = NULL;

	// resizeBackdrop kills parallax stuff, light map, z-buffer...
	if (! resizeBackdrop (winWidth, winHeight)) return fatal ("Can't create new temporary backdrop buffer");

	if (! NPOT_textures) {
		picWidth = getNextPOT(sceneWidth);
		picHeight = getNextPOT(sceneHeight);
		backdropTexW = (double) sceneWidth / picWidth;
		backdropTexH = (double) sceneHeight / picHeight;
	}

	// Copy the old scene to the new backdrop
	gfx::Texture * reserved = backdropTex;
	setBackdropTexture (freezeTex);
	gfx::renderer->releaseTexture (reserved);
	freezeTex = NULL;
	backdropExists = true;

	// Free texture memory used by old stuff
	parallaxStuff = newFreezer -> parallaxStuff;
	while (parallaxStuff) {
		gfx::renderer->releaseTexture (parallaxStuff -> tex);
		parallaxStuff -> tex = NULL;
		parallaxStuff = parallaxStuff -> next;
	}
	if (newFreezer -> zBufferImage) {
		gfx::renderer->releaseTexture (zBuffer.texture);
		zBuffer.texture = NULL;
	}
	gfx::renderer->releaseTexture (oldLightMap);
	gfx::renderer->releaseTexture (oldBackdrop);

	newFreezer -> allPeople = allPeople;
	allPeople = NULL;

	statusStuff * newStatusStuff = new statusStuff;
	if (! checkNew (newStatusStuff)) return false;
	newFreezer -> frozenStatus = copyStatusBarStuff (newStatusStuff);

	newFreezer -> allScreenRegions = allScreenRegions;
	allScreenRegions = NULL;
	overRegion = NULL;

	newFreezer -> mouseCursorFrameNum = cursorFrame ();
	newFreezer -> mouseCursorAnim.reset (takeCursorAnim ());
	setCursorAnim (makeNullAnim (), 0);

	newFreezer -> speech = speech;
	initSpeech ();

	newFreezer -> currentEvents = currentEvents;
	currentEvents = new eventHandlers;
	if (! checkNew (currentEvents)) return false;
	memset (currentEvents, 0, sizeof (eventHandlers));

	newFreezer -> next = frozenStuff;
	frozenStuff = newFreezer;
	return true;
}

int howFrozen () {
	int a = 0;
	frozenStuffStruct * f = frozenStuff;
	while (f) {
		a ++;
		f = f -> next;
	}
	return a;
}

extern GLubyte * backdropTexture;

void unfreeze (bool killImage) {
	frozenStuffStruct * killMe = frozenStuff;

	if (! frozenStuff) return;

	sceneWidth = frozenStuff -> sceneWidth;
	sceneHeight = frozenStuff -> sceneHeight;

	cameraX = frozenStuff -> cameraX;
	cameraY = frozenStuff -> cameraY;
	input.mouseX = (int)(input.mouseX * cameraZoom);
	input.mouseY = (int)(input.mouseY * cameraZoom);
	cameraZoom = frozenStuff -> cameraZoom;
	input.mouseX = (int)(input.mouseX / cameraZoom);
	input.mouseY = (int)(input.mouseY / cameraZoom);
	setPixelCoords(false);

	killAllPeople ();
	allPeople = frozenStuff -> allPeople;

	killAllRegions ();
	allScreenRegions = frozenStuff -> allScreenRegions;

	killLightMap ();
	lightMap.data = frozenStuff -> lightMapTexture;
	lightMapNumber = frozenStuff -> lightMapNumber;
	if (lightMapNumber) {
		loadLightMap(lightMapNumber);
	}

	killZBuffer ();
	zBuffer.tex = frozenStuff -> zBufferImage;
	zBuffer.originalNum = frozenStuff -> zBufferNumber;
	zBuffer.numPanels = frozenStuff -> zPanels;
	if (zBuffer.numPanels) {
		setZBuffer (zBuffer.originalNum);
	}

	killParallax ();
	parallaxStuff = frozenStuff -> parallaxStuff;
	reloadParallaxTextures ();

	if (killImage) killBackDrop ();
	gfx::Texture * replaced = backdropTex;
	setBackdropTexture (NULL);
	gfx::renderer->releaseTexture (replaced);
	if (backdropTexture) delete[] backdropTexture;
	backdropTexture = frozenStuff -> backdropTexture;
	backdropExists = true;
	if (frozenStuff -> hadBackdrop) {
		int picWidth = sceneWidth;
		int picHeight = sceneHeight;
		if (! NPOT_textures) {
			picWidth = getNextPOT(picWidth);
			picHeight = getNextPOT(picHeight);
		}
		// Restore the backdrop
		gfx::Texture * t = gfx::renderer->createTexture (picWidth, picHeight, gfx::TEX_RGBA8, frozenStuff -> backdropTexture);
		gfx::renderer->setTextureWrap (t, gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
		gfx::Filter filter = gameSettings.antiAlias < 0 ? gfx::FILTER_LINEAR : gfx::FILTER_NEAREST;
		gfx::renderer->setTextureFilter (t, filter, filter);
		setBackdropTexture (t);
	}

	setCursorAnim (frozenStuff -> mouseCursorAnim.release (), frozenStuff -> mouseCursorFrameNum);

	restoreBarStuff (frozenStuff -> frozenStatus);

	delete currentEvents;
	currentEvents = frozenStuff -> currentEvents;

	killAllSpeech ();
	delete speech;
	speech = frozenStuff -> speech;

	frozenStuff = frozenStuff -> next;

	overRegion = NULL;
	delete killMe;
	killMe = NULL;
}
