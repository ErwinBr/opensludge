#include "scalemap.h"

struct frozenStuffStruct {
	onScreenPerson * allPeople;
	screenRegion * allScreenRegions;
	struct regionWatch * allRegionWatches;
	GLubyte * backdropTexture;
	// We keep the pixels of the backdrop, and make a texture from them
	// again when we unfreeze.
	bool hadBackdrop;
	GLubyte * lightMapTexture;
	GLubyte * zBufferImage;
	int zPanels;
	parallaxLayer * parallaxStuff;
	int lightMapNumber, zBufferNumber;
	scaleMapData scaleMap;
	speechStruct * speech;
	statusStuff * frozenStatus;
	eventHandlers * currentEvents;
	std::unique_ptr<personaAnimation> mouseCursorAnim;	// The cursor's, while frozen
	int mouseCursorFrameNum;
	int cameraX, cameraY, sceneWidth, sceneHeight;
	float cameraZoom;

	frozenStuffStruct * next;
};

bool freeze ();
void unfreeze (bool killImage = true);
int howFrozen ();
