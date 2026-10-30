struct frozenStuffStruct {
	onScreenPerson * allPeople;
	screenRegion * allScreenRegions;
	GLubyte * backdropTexture;
	// We keep the pixels of the backdrop, and make a texture from them
	// again when we unfreeze.
	bool hadBackdrop;
	GLubyte * lightMapTexture;
	GLubyte * zBufferImage;
	int zPanels;
	parallaxLayer * parallaxStuff;
	int lightMapNumber, zBufferNumber;
	speechStruct * speech;
	statusStuff * frozenStatus;
	eventHandlers * currentEvents;
	personaAnimation * mouseCursorAnim;
	int mouseCursorFrameNum;
	int cameraX, cameraY, sceneWidth, sceneHeight;
	float cameraZoom;

	frozenStuffStruct * next;
};

bool freeze ();
void unfreeze (bool killImage = true);
int howFrozen ();
