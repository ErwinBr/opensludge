#include "debug.h"
#include "allfiles.h"
#include <SDL3/SDL.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <algorithm>
#include <memory>
#include <string>

#include "sludger.h"
#include "builtin.h"
#include "stringy.h"
#include "newfatal.h"
#include "cursors.h"
#include "statusba.h"
#include "loadsave.h"
#include "backdrop.h"
#include "bg_effects.h"
#include "sprites.h"
#include "fonttext.h"
#include "sprbanks.h"
#include "animformat.h"
#include "fileset.h"
#include "people.h"
#include "sound.h"
#include "objtypes.h"
#include "floor.h"
#include "zbuffer.h"
#include "talk.h"
#include "region.h"
#include "language.h"
#include "moreio.h"
#include "movie.h"
#include "savedata.h"
#include "freeze.h"
#include "colours.h"
#include "language.h"
#include "thumbnail.h"
#include "graphics.h"
#include "utf8.h"
#include "helpers.h"

extern char * gamePath;

int speechMode = 0;
int cameraX, cameraY;
float cameraZoom = 1.0;
spritePalette pastePalette;

char * launchMe = NULL;
variable * launchResult = NULL;

extern int lastFramesPerSecond, thumbWidth, thumbHeight;
extern bool allowAnyFilename;
extern bool captureAllKeys;
extern short fontSpace;
extern eventHandlers * currentEvents;
extern variableStack * noStack;
extern statusStuff * nowStatus;
extern screenRegion * overRegion;
extern unsigned int sceneWidth, sceneHeight;
extern int numBIFNames, numUserFunc;
extern char builtInFunctionNames[][25];

extern char * * allUserFunc;
extern char * * allBIFNames;
extern inputType input;
extern char * loadNow;



extern float speechSpeed;
extern unsigned char brightnessLevel;
extern unsigned char fadeMode;
extern unsigned short saveEncoding;
extern frozenStuffStruct * frozenStuff;
extern unsigned int currentBlankColour;
extern unsigned int languageID;
extern unsigned char currentBurnR, currentBurnG, currentBurnB;

int paramNum[] = {-1, 0, 1, 1, -1, -1, 1, 3, 4, 1, 0, 0, 8, -1,		// SAY -> MOVEMOUSE
	-1, 0, 0, -1, -1, 1, 1, 1, 1, 4, 1, 1, 2, 1,// FOCUS -> REMOVEREGION
	2, 2, 0, 0, 2,								// ANIMATE -> SETSCALE
	-1, 2, 1, 0, 0, 0, 1, 0, 3, 				// new/push/pop stack, status stuff
	2, 0, 0, 3, 1, 0, 2,						// delFromStack -> completeTimers
	-1, -1, -1, 2, 2, 0, 3, 1, 					// anim, costume, pO, setC, wait, sS, substring, stringLength
	0, 1, 1, 0, 2,  							// dark, save, load, quit, rename
	1, 3, 3, 1, 2, 1, 1, 3, 1, 0, 0, 2, 1,		// stackSize, pasteString, startMusic, defvol, vol, stopmus, stopsound, setfont, alignStatus, show x 2, pos'Status, setFloor
	-1, -1, 1, 1, 2, 1, 1, 1, -1, -1, -1, 1, 1,	// force, jump, peekstart, peekend, enqueue, getSavedGames, inFont, loopSound, removeChar, stopCharacter
	1, 0, 3, 3, 1, 2, 1, 2, 2,					// launch, howFrozen, pastecol, litcol, checksaved, float, cancelfunc, walkspeed, delAll
	2, 3, 1, 2, 2, 0, 0, 1, 2, 3, 1, -1,		// extras, mixoverlay, pastebloke, getMScreenX/Y, setSound(Default/-)Volume, looppoints, speechMode, setLightMap
	-1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1,			// think, getCharacterDirection, is(char/region/moving), deleteGame, renameGame, hardScroll, stringWidth, speechSpeed, normalCharacter
	2, 1, 2, 1, 3, 1, 1, 2, 1,					// fetchEvent, setBrightness, spin, fontSpace, burnString, captureAll, cacheSound, setSpinSpeed, transitionMode
	1, 0, 0, 1, 0, 2, 1, 1, 1,					// movie(Start/Abort/Playing), updateDisplay, getSoundCache, savedata, loaddata, savemode, freeSound
	3, 0, 3, 3, 2, 1, 1,						// setParallax, clearParallax, setBlankColour, setBurnColour, getPixelColour, makeFastArray, getCharacterScale
	0, 2, 0,									// getLanguage, launchWith, getFramesPerSecond
	3, 2, 2, 0, 0, 1,							// readThumbnail, setThumbnailSize, hasFlag, snapshot, clearSnapshot, anyFilename
	2, 1,										// regGet, fatal
	4, 3, -1, 0,								// chr AA, max AA, setBackgroundEffect, doBackgroundEffect
	2,											// setCharacterAngleOffset
	2, 5,										// setCharacterTransparency, setCharacterColourise
	1,											// zoomCamera
	1, 0, 0,									// playMovie, stopMovie, pauseMovie
	-1, -1, 3, 3, 3, 2,							// newClip, clipSprite, clipFrames, clipEvent, setClipSpeed, isClipPlaying
	0, -1, 3, 2, -1,							// newRig, addPart, setPartDepth, setCharacterRig, clipKey
	-1, 2, 4, 3, 1,								// playClip, stopClip, setPartProperty, clearPartProperty, mirrorClip
	4, 1, 1									// setPartPivot, loadRig, loadClip
};

bool failSecurityCheck (char * fn) {
	if (fn == NULL) return true;

	int a = 0;

	while (fn[a]) {
		switch (fn[a]) {
			case ':':
			case '\\':
			case '/':
			case '*':
			case '?':
			case '"':
			case '<':
			case '>':
			case '|':
				fatal ("Filenames may not contain the following characters: \n\n\\  /  :  \"  <  >  |  ?  *\n\nConsequently, the following filename is not allowed:", fn);
				return true;
		}
		a ++;
	}
	return false;
}

loadedFunction * saverFunc;

typedef builtReturn (* builtInSludgeFunc) (int numParams, loadedFunction * fun);
struct builtInFunctionData
{
	builtInSludgeFunc func;
};

#define builtIn(a) 			static builtReturn builtIn_ ## a (int numParams, loadedFunction * fun)
#define UNUSEDALL		 	(void) (0 && sizeof(numParams) && sizeof (fun));

static builtReturn sayCore (int numParams, loadedFunction * fun, bool sayIt)
{
	int fileNum = -1;
	char * newText;
	int objT, p;
	killSpeechTimers ();

	switch (numParams) {
		case 3:
			if (! getValueType (fileNum, SVT_FILE, fun -> stack -> thisVar)) return BR_ERROR;
			trimStack (fun -> stack);
			// No break; here

		case 2:
			newText = getTextFromAnyVar (fun -> stack -> thisVar);
			if (! newText) return BR_ERROR;
			trimStack (fun -> stack);
			if (! getValueType (objT, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
			trimStack (fun -> stack);
			p = wrapSpeech (newText, objT, fileNum, sayIt);
			fun -> timeLeft = p;
			//debugOut ("BUILTIN: sayCore: %s (%i)\n", newText, p);
			fun -> isSpeech = true;
			delete [] newText;
			newText = NULL;
			return BR_KEEP_AND_PAUSE;
	}

	fatal ("Function should have either 2 or 3 parameters");
	return BR_ERROR;
}

#pragma mark -
#pragma mark Built in functions

builtIn(say)
{
	UNUSEDALL
	return sayCore (numParams, fun, true);
}

builtIn(think)
{
	UNUSEDALL
	return sayCore (numParams, fun, false);
}

builtIn(freeze)
{
	UNUSEDALL
	freeze ();
	freezeSubs ();
	fun -> freezerLevel = 0;
	return BR_CONTINUE;
}

builtIn(unfreeze)
{
	UNUSEDALL
	unfreeze ();
	unfreezeSubs ();
	return BR_CONTINUE;
}

builtIn(howFrozen)
{
	UNUSEDALL
	setVariable (fun -> reg, SVT_INT, howFrozen ());
	return BR_CONTINUE;
}

builtIn(setCursor)
{
	UNUSEDALL
	pickAnimCursor (getAnimationFromVar (fun -> stack -> thisVar));
	trimStack (fun -> stack);
	return BR_CONTINUE;
}

builtIn(getMouseX)
{
	UNUSEDALL
	setVariable (fun -> reg, SVT_INT, input.mouseX + cameraX);
	return BR_CONTINUE;
}

builtIn(getMouseY)
{
	UNUSEDALL
	setVariable (fun -> reg, SVT_INT, input.mouseY + cameraY);
	return BR_CONTINUE;
}

builtIn(getMouseScreenX)
{
	UNUSEDALL
	setVariable (fun -> reg, SVT_INT, input.mouseX*cameraZoom);
	return BR_CONTINUE;
}

builtIn(getMouseScreenY)
{
	UNUSEDALL
	setVariable (fun -> reg, SVT_INT, input.mouseY*cameraZoom);
	return BR_CONTINUE;
}

builtIn(getStatusText)
{
	UNUSEDALL
	makeTextVar (fun -> reg, statusBarText ());
	return BR_CONTINUE;
}

builtIn(getMatchingFiles)
{
	UNUSEDALL
	char * newText = getTextFromAnyVar (fun -> stack -> thisVar);
	if (! newText) return BR_ERROR;
	trimStack (fun -> stack);
	unlinkVar (fun -> reg);

	// Return value
	fun -> reg.varType = SVT_STACK;
	fun -> reg.varData.theStack = new stackHandler;
	if (! checkNew (fun -> reg.varData.theStack)) return BR_ERROR;
	fun -> reg.varData.theStack -> first = NULL;
	fun -> reg.varData.theStack -> last = NULL;
	fun -> reg.varData.theStack -> timesUsed = 1;
	if (! getSavedGamesStack (fun -> reg.varData.theStack, newText)) return BR_ERROR;
	delete [] newText;
	newText = NULL;
	return BR_CONTINUE;
}

builtIn(saveGame)
{
	UNUSEDALL

	if (frozenStuff) {
		fatal ("Can't save game state while the engine is frozen");
	}

	loadNow = getTextFromAnyVar (fun -> stack -> thisVar);
	trimStack (fun -> stack);

	char * aaaaa = encodeFilename (loadNow);
	delete[] loadNow;
	if (failSecurityCheck (aaaaa)) return BR_ERROR;		// Won't fail if encoded, how cool is that? OK, not very.

	loadNow = joinStrings (":", aaaaa);
	delete[] aaaaa;

	setVariable (fun -> reg, SVT_INT, 0);
	saverFunc = fun;
	return BR_KEEP_AND_PAUSE;
}

builtIn(fileExists)
{
	UNUSEDALL
	loadNow = getTextFromAnyVar (fun -> stack -> thisVar);
	trimStack (fun -> stack);
	char * aaaaa = encodeFilename (loadNow);
	delete [] loadNow;
	if (failSecurityCheck (aaaaa)) return BR_ERROR;
	FILE * fp = fopen (aaaaa, "rb");
	if (! fp) {
		char currentDir[1000];
		if (! getcwd (currentDir, 998)) {
			debugOut( "Can't get current directory.\n");
		}

		if (chdir (gamePath)) {
			debugOut("Error: Failed changing to directory %s\n", gamePath);
		}
		fp = fopen (aaaaa, "rb");
		if (chdir (currentDir)) {
			debugOut("Error: Failed changing to directory %s\n", currentDir);
		}
	}
	// Return value
	setVariable (fun -> reg, SVT_INT, (fp != NULL));
	if (fp) fclose (fp);
	delete[] aaaaa;
	loadNow = NULL;
	return BR_CONTINUE;
}

builtIn(loadGame)
{
	UNUSEDALL
	char * aaaaa = getTextFromAnyVar (fun -> stack -> thisVar);
	trimStack (fun -> stack);
	loadNow = encodeFilename (aaaaa);
	delete [] aaaaa;

	if (frozenStuff) {
		fatal ("Can't load a saved game while the engine is frozen");
	}

	if (failSecurityCheck (loadNow)) return BR_ERROR;
	FILE * fp = fopen (loadNow, "rb");
	if (fp) {
		fclose (fp);
		return BR_KEEP_AND_PAUSE;
	}
	delete [] loadNow;
	loadNow = NULL;
	return BR_CONTINUE;
}

//--------------------------------------
#pragma mark -
#pragma mark Background image - Painting

builtIn(blankScreen)
{
	UNUSEDALL
	blankScreen (0, 0, sceneWidth, sceneHeight);
	return BR_CONTINUE;
}

builtIn(blankArea)
{
	UNUSEDALL
	int x1, y1, x2, y2;
	if (! getValueType (y2, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (x2, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (y1, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (x1, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	blankScreen (x1, y1, x2, y2);
	return BR_CONTINUE;
}

builtIn(darkBackground)
{
	UNUSEDALL
	darkScreen ();
	return BR_CONTINUE;
}

builtIn(addOverlay)
{
	UNUSEDALL
	int fileNumber, xPos, yPos;
	if (! getValueType (yPos, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (xPos, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (fileNumber, SVT_FILE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	loadBackDrop (fileNumber, xPos, yPos);
	return BR_CONTINUE;
}

builtIn(mixOverlay)
{
	UNUSEDALL
	int fileNumber, xPos, yPos;
	if (! getValueType (yPos, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (xPos, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (fileNumber, SVT_FILE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	mixBackDrop (fileNumber, xPos, yPos);
	return BR_CONTINUE;
}

builtIn(pasteImage)
{
	UNUSEDALL
	int x, y;
	if (! getValueType (y, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (x, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	AnimationHandle pp = getAnimationFromVar (fun -> stack -> thisVar);
	trimStack (fun -> stack);
	if (! pp) return BR_CONTINUE;

	pasteCursor (x, y, * pp);
	return BR_CONTINUE;
}

#pragma mark -
#pragma mark Background Image - Scrolling

builtIn(setSceneDimensions)
{
	UNUSEDALL
	int x, y;
	if (! getValueType (y, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (x, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (resizeBackdrop (x, y)) {
		blankScreen (0, 0, x, y);
		return BR_CONTINUE;
	}
	fatal ("Out of memory creating new backdrop.");
	return BR_ERROR;
}

builtIn(aimCamera)
{
	UNUSEDALL
	if (! getValueType (cameraY, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (cameraX, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);

	cameraX -= (float)(winWidth >> 1)/ cameraZoom;
	cameraY -= (float)(winHeight >> 1)/ cameraZoom;

	if (cameraX < 0) cameraX = 0;
	else if (cameraX > sceneWidth - (float)winWidth/ cameraZoom) cameraX = sceneWidth - (float)winWidth/ cameraZoom;
	if (cameraY < 0) cameraY = 0;
	else if (cameraY > sceneHeight - (float)winHeight/ cameraZoom) cameraY = sceneHeight - (float)winHeight/ cameraZoom;
	return BR_CONTINUE;
}


builtIn(zoomCamera)
{
	UNUSEDALL
	int z;
	if (! getValueType (z, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);

	input.mouseX = input.mouseX * cameraZoom;
	input.mouseY = input.mouseY * cameraZoom;


	cameraZoom = (float) z * 0.01;
	if ((float) winWidth / cameraZoom > sceneWidth) cameraZoom = (float)winWidth / sceneWidth;
	if ((float) winHeight / cameraZoom > sceneHeight) cameraZoom = (float)winHeight / sceneHeight;
	setPixelCoords (false);

	input.mouseX = input.mouseX / cameraZoom;
	input.mouseY = input.mouseY / cameraZoom;

	return BR_CONTINUE;
}

#pragma mark -
#pragma mark Variables


builtIn(pickOne)
{
	UNUSEDALL
	if (! numParams) {
		fatal ("Built-in function should have at least 1 parameter");
		return BR_ERROR;
	}
	int i = getRandom (numParams);

	// Return value
	while (numParams --) {
		if (i == numParams) copyVariable (fun -> stack -> thisVar, fun -> reg);
		trimStack (fun -> stack);
	}
	return BR_CONTINUE;
}

builtIn(substring)
{
	UNUSEDALL
	char * wholeString;
	char * newString;
	int start, length;

	//debugOut ("BUILTIN: substring\n");

	if (! getValueType (length, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (start, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	wholeString = getTextFromAnyVar (fun -> stack -> thisVar);
	trimStack (fun -> stack);
	
	if (u8_strlen(wholeString) < start+length) {
		length = u8_strlen(wholeString) - start;
		if (u8_strlen(wholeString) < start) {
			start = 0;
		}
	}
	if (length<0) {
		length=0;
	}
	
	int startoffset = u8_offset (wholeString, start);
	int endoffset = u8_offset (wholeString, start+length);

	newString = new char[endoffset - startoffset + 1];
	if (! checkNew (newString)) {
		return BR_ERROR;
	}
	
	memcpy (newString, wholeString + startoffset, endoffset - startoffset);
	newString[endoffset - startoffset] = 0;
	
	makeTextVar (fun -> reg, newString);
	delete [] newString;
	return BR_CONTINUE;
}

builtIn(stringLength)
{
	UNUSEDALL
	char * newText = getTextFromAnyVar (fun -> stack -> thisVar);
	trimStack (fun -> stack);
	setVariable (fun -> reg, SVT_INT, stringLength(newText));
	delete[] newText;
	return BR_CONTINUE;
}

builtIn(newStack)
{
	UNUSEDALL
	unlinkVar (fun -> reg);

	// Return value
	fun -> reg.varType = SVT_STACK;
	fun -> reg.varData.theStack = new stackHandler;
	if (! checkNew (fun -> reg.varData.theStack)) return BR_ERROR;
	fun -> reg.varData.theStack -> first = NULL;
	fun -> reg.varData.theStack -> last = NULL;
	fun -> reg.varData.theStack -> timesUsed = 1;
	while (numParams --) {
		if (! addVarToStack (fun -> stack -> thisVar, fun -> reg.varData.theStack -> first)) return BR_ERROR;
		if (fun -> reg.varData.theStack -> last == NULL) {
			fun -> reg.varData.theStack -> last = fun -> reg.varData.theStack -> first;
		}
		trimStack (fun -> stack);
	}
	return BR_CONTINUE;
}

// wait is exactly the same function, but limited to 2 parameters
#define builtIn_wait builtIn_newStack

builtIn(stackSize)
{
	UNUSEDALL
	switch (fun -> stack -> thisVar.varType) {
		case SVT_STACK:
			// Return value
			setVariable (fun -> reg, SVT_INT, stackSize (fun -> stack -> thisVar.varData.theStack));
			trimStack (fun -> stack);
			return BR_CONTINUE;

		case SVT_FASTARRAY:
			// Return value
			setVariable (fun -> reg, SVT_INT, fun -> stack -> thisVar.varData.fastArray -> size);
			trimStack (fun -> stack);
			return BR_CONTINUE;

		default:
			break;
	}
	fatal ("Parameter isn't a stack or a fast array.");
	return BR_ERROR;
}

builtIn(copyStack)
{
	UNUSEDALL
	if (fun -> stack -> thisVar.varType != SVT_STACK) {
		fatal ("Parameter isn't a stack.");
		return BR_ERROR;
	}
	// Return value
	if (! copyStack (fun -> stack -> thisVar, fun -> reg)) return BR_ERROR;
	trimStack (fun -> stack);
	return BR_CONTINUE;
}

builtIn(pushToStack)
{
	UNUSEDALL
	if (fun -> stack -> next -> thisVar.varType != SVT_STACK) {
		fatal ("Parameter isn't a stack");
		return BR_ERROR;
	}

	if (! addVarToStack (fun -> stack -> thisVar, fun -> stack -> next -> thisVar.varData.theStack -> first))
		return BR_ERROR;

	if (fun -> stack -> next -> thisVar.varData.theStack -> first -> next == NULL)
		fun -> stack -> next -> thisVar.varData.theStack -> last = fun -> stack -> next -> thisVar.varData.theStack -> first;

	trimStack (fun -> stack);
	trimStack (fun -> stack);
	return BR_CONTINUE;
}

builtIn(enqueue)
{
	UNUSEDALL
	if (fun -> stack -> next -> thisVar.varType != SVT_STACK) {
		fatal ("Parameter isn't a stack");
		return BR_ERROR;
	}

	if (fun -> stack -> next -> thisVar.varData.theStack -> first == NULL)
	{
		if (! addVarToStack (fun -> stack -> thisVar, fun -> stack -> next -> thisVar.varData.theStack -> first))
			return BR_ERROR;

		fun -> stack -> next -> thisVar.varData.theStack -> last = fun -> stack -> next -> thisVar.varData.theStack -> first;
	}
	else
	{
		if (! addVarToStack (fun -> stack -> thisVar,
							 fun -> stack -> next -> thisVar.varData.theStack -> last -> next))
			return BR_ERROR;
		fun -> stack -> next -> thisVar.varData.theStack -> last = fun -> stack -> next -> thisVar.varData.theStack -> last -> next;
	}

	trimStack (fun -> stack);
	trimStack (fun -> stack);
	return BR_CONTINUE;
}

builtIn(deleteFromStack)
{
	UNUSEDALL
	if (fun -> stack -> next -> thisVar.varType != SVT_STACK) {
		fatal ("Parameter isn't a stack.");
		return BR_ERROR;
	}

	// Return value
	setVariable (fun -> reg, SVT_INT,
				 deleteVarFromStack (fun -> stack -> thisVar,
									 fun -> stack -> next -> thisVar.varData.theStack -> first, false));

	// Horrible hacking because 'last' value might now be wrong!
	fun->stack->next->thisVar.varData.theStack->last = stackFindLast (fun->stack->next->thisVar.varData.theStack->first);

	trimStack (fun -> stack);
	trimStack (fun -> stack);
	return BR_CONTINUE;
}

builtIn(deleteAllFromStack)
{
	UNUSEDALL
	if (fun -> stack -> next -> thisVar.varType != SVT_STACK) {
		fatal ("Parameter isn't a stack.");
		return BR_ERROR;
	}

	// Return value
	setVariable (fun -> reg, SVT_INT,
				 deleteVarFromStack (fun -> stack -> thisVar,
									 fun -> stack -> next -> thisVar.varData.theStack -> first, true));

	// Horrible hacking because 'last' value might now be wrong!
	fun->stack->next->thisVar.varData.theStack->last = stackFindLast (fun->stack->next->thisVar.varData.theStack->first);

	trimStack (fun -> stack);
	trimStack (fun -> stack);
	return BR_CONTINUE;
}

builtIn(popFromStack)
{
	UNUSEDALL
	if (fun -> stack -> thisVar.varType != SVT_STACK) {
		fatal ("Parameter isn't a stack.");
		return BR_ERROR;
	}
	if (fun -> stack -> thisVar.varData.theStack -> first == NULL) {
		fatal ("The stack's empty.");
		return BR_ERROR;
	}

	// Return value
	copyVariable (fun -> stack -> thisVar.varData.theStack -> first -> thisVar, fun -> reg);
	trimStack (fun -> stack -> thisVar.varData.theStack -> first);
	trimStack (fun -> stack);
	return BR_CONTINUE;
}

builtIn(peekStart)
{
	UNUSEDALL
	if (fun -> stack -> thisVar.varType != SVT_STACK) {
		fatal ("Parameter isn't a stack.");
		return BR_ERROR;
	}
	if (fun -> stack -> thisVar.varData.theStack -> first == NULL) {
		fatal ("The stack's empty.");
		return BR_ERROR;
	}

	// Return value
	copyVariable (fun -> stack -> thisVar.varData.theStack -> first -> thisVar, fun -> reg);
	trimStack (fun -> stack);
	return BR_CONTINUE;
}

builtIn(peekEnd)
{
	UNUSEDALL
	if (fun -> stack -> thisVar.varType != SVT_STACK) {
		fatal ("Parameter isn't a stack.");
		return BR_ERROR;
	}
	if (fun -> stack -> thisVar.varData.theStack -> first == NULL) {
		fatal ("The stack's empty.");
		return BR_ERROR;
	}

	// Return value
	copyVariable (fun -> stack -> thisVar.varData.theStack -> last -> thisVar, fun -> reg);
	trimStack (fun -> stack);
	return BR_CONTINUE;
}

builtIn(random)
{
	UNUSEDALL
	int num;

	if (! getValueType (num, SVT_INT, fun -> stack -> thisVar))
		return BR_ERROR;

	trimStack (fun -> stack);
	if (num <= 0) num = 1;
	setVariable (fun -> reg, SVT_INT, getRandom (num));
	return BR_CONTINUE;
}

static bool getRGBParams(int & red, int & green, int & blue, loadedFunction * fun)
{
	if (! getValueType (blue, SVT_INT, fun -> stack -> thisVar)) return false;
	trimStack (fun -> stack);
	if (! getValueType (green, SVT_INT, fun -> stack -> thisVar)) return false;
	trimStack (fun -> stack);
	if (! getValueType (red, SVT_INT, fun -> stack -> thisVar)) return false;
	trimStack (fun -> stack);
	return true;
}

builtIn (setStatusColour)
{
	UNUSEDALL
	int red, green, blue;

	if (! getRGBParams(red, green, blue, fun))
		return BR_ERROR;

	statusBarColour ((byte) red, (byte) green, (byte) blue);
	return BR_CONTINUE;
}

builtIn (setLitStatusColour)
{
	UNUSEDALL
	int red, green, blue;

	if (! getRGBParams(red, green, blue, fun))
		return BR_ERROR;

	statusBarLitColour ((byte) red, (byte) green, (byte) blue);
	return BR_CONTINUE;
}

builtIn (setPasteColour)
{
	UNUSEDALL
	int red, green, blue;

	if (! getRGBParams(red, green, blue, fun))
		return BR_ERROR;

	setFontColour (pastePalette, (byte) red, (byte) green, (byte) blue);
	return BR_CONTINUE;
}

builtIn (setBlankColour)
{
	UNUSEDALL
	int red, green, blue;

	if (! getRGBParams(red, green, blue, fun))
		return BR_ERROR;

	currentBlankColour = makeColour (red & 255, green & 255, blue & 255);
	setVariable (fun -> reg, SVT_INT, 1);
	return BR_CONTINUE;
}

builtIn (setBurnColour)
{
	UNUSEDALL
	int red, green, blue;

	if (! getRGBParams(red, green, blue, fun))
		return BR_ERROR;

	currentBurnR = red;
	currentBurnG = green;
	currentBurnB = blue;
	setVariable (fun -> reg, SVT_INT, 1);
	return BR_CONTINUE;
}


builtIn(setFont)
{
	UNUSEDALL
	int fileNumber, newHeight;
	if (! getValueType (newHeight, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	//				newDebug ("  Height:", newHeight);
	trimStack (fun -> stack);
	char * newText = getTextFromAnyVar (fun -> stack -> thisVar);
	if (! newText) return BR_ERROR;
	//				newDebug ("  Character supported:", newText);
	trimStack (fun -> stack);
	if (! getValueType (fileNumber, SVT_FILE, fun -> stack -> thisVar)) return BR_ERROR;
	//				newDebug ("  File:", fileNumber);
	trimStack (fun -> stack);
	if (! loadFont (fileNumber, newText, newHeight)) return BR_ERROR;
	//				newDebug ("  Done!");
	delete [] newText;

	return BR_CONTINUE;
}

builtIn(inFont)
{
	UNUSEDALL
	char * newText = getTextFromAnyVar (fun -> stack -> thisVar);
	if (! newText) return BR_ERROR;
	trimStack (fun -> stack);

	// Return value
	
	setVariable (fun -> reg, SVT_INT, isInFont(newText));
	return BR_CONTINUE;
}

builtIn(pasteString)
{
	UNUSEDALL
	char * newText = getTextFromAnyVar (fun -> stack -> thisVar);
	trimStack (fun -> stack);
	int y, x;
	if (! getValueType (y, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (x, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (x == IN_THE_CENTRE) x = (winWidth - stringWidth (newText)) >> 1;
	pasteStringToBackdrop (newText, x, y, pastePalette);
	delete[] newText;
	return BR_CONTINUE;
}

builtIn(anim)
{
	UNUSEDALL
	if (numParams < 2) {
		fatal ("Built-in function anim() must have at least 2 parameters.");
		return BR_ERROR;
	}

	// First store the frame numbers and take 'em off the stack
	std::vector<AnimationFrame> frames = readAnimationFrames (numParams - 1, fun -> stack);

	// Only remaining paramter is the file number
	int fileNumber;
	if (! getValueType (fileNumber, SVT_FILE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);

	// Load the required sprite bank
	SpriteBankRef sprBanky = loadBankForAnim (fileNumber);
	if (! sprBanky) return BR_ERROR;	// File not found, fatal done already
	AnimationHandle ba (new personaAnimation);
	ba -> definition = makeAnimation (sprBanky, frames);

	// Return value
	newAnimationVariable (fun -> reg, std::move (ba));

	return BR_CONTINUE;
}

builtIn(costume)
{
	UNUSEDALL
	persona * newPersona = new persona;
	if (! checkNew (newPersona)) return BR_ERROR;
	newPersona -> numDirections = numParams / 3;
	if (numParams == 0 || newPersona -> numDirections * 3 != numParams) {
		fatal ("Illegal number of parameters (should be greater than 0 and divisible by 3)");
		return BR_ERROR;
	}
	int iii;
	newPersona -> animation.resize (numParams);
	for (iii = numParams - 1; iii >= 0; iii --) {
		newPersona -> animation[iii] = getAnimationFromVar (fun -> stack -> thisVar);
		trimStack (fun -> stack);
	}

	// Return value
	newCostumeVariable (fun -> reg, newPersona);
	return BR_CONTINUE;
}

builtIn(launch)
{
	UNUSEDALL
	char * newTextA = getTextFromAnyVar (fun -> stack -> thisVar);
	if (! newTextA) return BR_ERROR;

	char * newText = encodeFilename (newTextA);

	trimStack (fun -> stack);
	if (newTextA[0] == 'h' &&
		newTextA[1] == 't' &&
		newTextA[2] == 't' &&
		newTextA[3] == 'p' &&
		(newTextA[4] == ':' || (newTextA[4] == 's' && newTextA[5] == ':'))) {

		// IT'S A WEBSITE!
		launchMe = copyString(newTextA);
	} else {
		char *gameDir;
#ifdef _WIN32
		gameDir = joinStrings(gamePath, "\\");
#else
		gameDir = joinStrings(gamePath, "/");
#endif
		launchMe = joinStrings (gameDir, newText);
		delete [] newText;
		if (! launchMe) return BR_ERROR;
	}
	delete [] newTextA;
	setGraphicsWindow(false);
	setVariable (fun -> reg, SVT_INT, 1);
	launchResult = &fun->reg;

	return BR_KEEP_AND_PAUSE;
}

builtIn(pause)
{
	UNUSEDALL
	int theTime;
	if (! getValueType (theTime, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (theTime > 0) {
		fun -> timeLeft = theTime - 1;
		fun -> isSpeech = false;
		return BR_KEEP_AND_PAUSE;
	}
	return BR_CONTINUE;
}

builtIn(completeTimers)
{
	UNUSEDALL
	completeTimers();
	return BR_CONTINUE;
}

builtIn(callEvent)
{
	UNUSEDALL
	int obj1, obj2;
	if (! getValueType (obj2, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (obj1, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);

	int fNum = getCombinationFunction (obj1, obj2);

	// Return value
	if (fNum) {
		setVariable (fun -> reg, SVT_FUNC, fNum);
		return BR_CALLAFUNC;
	}
	setVariable (fun -> reg, SVT_INT, 0);
	return BR_CONTINUE;
}


SDL_Event quit_event;
bool reallyWantToQuit = false;

builtIn(quitGame)
{
	UNUSEDALL
	reallyWantToQuit = true;
	SDL_zero(quit_event);
	quit_event.type=SDL_EVENT_QUIT;
	SDL_PushEvent(&quit_event);
	return BR_CONTINUE;
}


#pragma mark -
#pragma mark Movie functions

// The old movie functions are deprecated and does nothing.
builtIn(_rem_movieStart)
{
	UNUSEDALL
	trimStack (fun -> stack);
	return BR_CONTINUE;
}

builtIn(_rem_movieAbort)
{
	UNUSEDALL
	setVariable (fun -> reg, SVT_INT, 0);
	return BR_CONTINUE;
}

builtIn(_rem_moviePlaying)
{
	UNUSEDALL
	setVariable (fun -> reg, SVT_INT, 0);
	return BR_CONTINUE;
}

builtIn (playMovie)
{
	UNUSEDALL
	int fileNumber, r;

	if (! getValueType (fileNumber, SVT_FILE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);

	r = playMovie(fileNumber);

	setVariable (fun -> reg, SVT_INT, r);

	// The main loop plays the movie, and this function waits until it has
	// ended (see runSludge).
	if (r) {
		fun -> waitsForMovie = true;
		return BR_KEEP_AND_PAUSE;
	}
	return BR_CONTINUE;
}

builtIn (stopMovie)
{
	UNUSEDALL
	int r;
	
	r = stopMovie();
	
	setVariable (fun -> reg, SVT_INT, 0);
	return BR_CONTINUE;
}

builtIn (pauseMovie)
{
	UNUSEDALL
	int r;
	
	r = pauseMovie();
	
	setVariable (fun -> reg, SVT_INT, 0);
	return BR_CONTINUE;
}


#pragma mark -
#pragma mark Audio functions

builtIn(startMusic)
{
	UNUSEDALL
	int fromTrack, musChan, fileNumber;
	if (! getValueType (fromTrack, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (musChan, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (fileNumber, SVT_FILE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! playMOD (fileNumber, musChan, fromTrack)) return BR_CONTINUE; //BR_ERROR;
	return BR_CONTINUE;
}

builtIn(stopMusic)
{
	UNUSEDALL
	int v;
	if (! getValueType (v, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	stopMOD (v);
	return BR_CONTINUE;
}

builtIn(setMusicVolume)
{
	UNUSEDALL
	int musChan, v;
	if (! getValueType (v, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (musChan, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	setMusicVolume (musChan, v);
	return BR_CONTINUE;
}

builtIn(setDefaultMusicVolume)
{
	UNUSEDALL
	int v;
	if (! getValueType (v, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	setDefaultMusicVolume (v);
	return BR_CONTINUE;
}

builtIn(playSound)
{
	UNUSEDALL
	int fileNumber;
	if (! getValueType (fileNumber, SVT_FILE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! startSound (fileNumber, false)) return BR_CONTINUE;	// Was BR_ERROR
	return BR_CONTINUE;
}
builtIn(loopSound)
{
	UNUSEDALL
	int fileNumber;

	if (numParams < 1) {
		fatal ("Built-in function loopSound() must have at least 1 parameter.");
		return BR_ERROR;
	} else if (numParams < 2) {

		if (! getValueType (fileNumber, SVT_FILE, fun -> stack -> thisVar)) return BR_ERROR;
		trimStack (fun -> stack);
		if (! startSound (fileNumber, true)) return BR_CONTINUE;	// Was BR_ERROR
		return BR_CONTINUE;
	} else {
		// We have more than one sound to play!

		int doLoop = 2;
		soundList *s = NULL;
		soundList * old = NULL;

		// Should we loop?
		if (fun->stack->thisVar.varType != SVT_FILE) {
			getValueType (doLoop, SVT_INT, fun -> stack -> thisVar);
			trimStack (fun -> stack);
			numParams--;
		}
		while (numParams) {
			if (! getValueType (fileNumber, SVT_FILE, fun -> stack -> thisVar)) {
				fatal ("Illegal parameter given built-in function loopSound().");
				return BR_ERROR;
			}
			s = new soundList;
			if (! checkNew (s)) return BR_ERROR;

			s-> next = old;
			s-> prev = NULL;
			s-> sound = fileNumber;
			old = s;

			trimStack(fun->stack);
			numParams--;
		}
		while (s->next) s = s-> next;
		if (doLoop > 1) {
			s->next = old;
			old->prev = s;
		} else if (doLoop) {
			s->next = s;
		}
		old->vol = -1;
		playSoundList(old);
		return BR_CONTINUE;
	}
}

builtIn(stopSound)
{
	UNUSEDALL
	int v;
	if (! getValueType (v, SVT_FILE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	huntKillSound (v);
	return BR_CONTINUE;
}

builtIn(setDefaultSoundVolume)
{
	UNUSEDALL
	int v;
	if (! getValueType (v, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	setDefaultSoundVolume (v);
	return BR_CONTINUE;
}

builtIn(setSoundVolume)
{
	UNUSEDALL
	int musChan, v;
	if (! getValueType (v, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (musChan, SVT_FILE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	setSoundVolume (musChan, v);
	return BR_CONTINUE;
}


builtIn(setSoundLoopPoints)
{
	UNUSEDALL
	int musChan, theEnd, theStart;
	if (! getValueType (theEnd, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (theStart, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (musChan, SVT_FILE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	setSoundLoop (musChan, theStart, theEnd);
	return BR_CONTINUE;
}

#pragma mark -
#pragma mark Extra room bits

builtIn(setFloor)
{
	UNUSEDALL
	if (fun -> stack -> thisVar.varType == SVT_FILE) {
		int v;
		getValueType (v, SVT_FILE, fun -> stack -> thisVar);
		trimStack (fun -> stack);
		if (! setFloor (v)) return BR_ERROR;
	} else {
		trimStack (fun -> stack);
		setFloorNull ();
	}
	return BR_CONTINUE;
}

builtIn(showFloor)
{
	UNUSEDALL
	drawFloor ();
	return BR_CONTINUE;
}

builtIn(setZBuffer)
{
	UNUSEDALL
	if (fun -> stack -> thisVar.varType == SVT_FILE) {
		int v;
		getValueType (v, SVT_FILE, fun -> stack -> thisVar);
		trimStack (fun -> stack);
		if (! setZBuffer (v)) return BR_ERROR;
	} else {
		trimStack (fun -> stack);
		killZBuffer ();
	}
	return BR_CONTINUE;
}

builtIn(setLightMap)
{
	UNUSEDALL
	switch (numParams) {
		case 2:
			if (! getValueType (lightMapMode, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
			trimStack (fun -> stack);
			lightMapMode %= LIGHTMAPMODE_NUM;
			// No break;

		case 1:
			if (fun -> stack -> thisVar.varType == SVT_FILE) {
				int v;
				getValueType (v, SVT_FILE, fun -> stack -> thisVar);
				trimStack (fun -> stack);
				if (! loadLightMap (v)) return BR_ERROR;
				setVariable (fun -> reg, SVT_INT, 1);
			} else {
				trimStack (fun -> stack);
				killLightMap ();
				setVariable (fun -> reg, SVT_INT, 0);
			}
			break;

		default:
			fatal ("Function should have either 2 or 3 parameters");
			return BR_ERROR;
	}
	return BR_CONTINUE;
}


#pragma mark -
#pragma mark Objects

builtIn(setSpeechMode)
{
	UNUSEDALL
	if (! getValueType (speechMode, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (speechMode < 0 || speechMode > 2) {
		fatal ("Valid parameters are be SPEECHANDTEXT, SPEECHONLY or TEXTONLY");
		return BR_ERROR;
	}
	return BR_CONTINUE;
}

builtIn(somethingSpeaking)
{
	UNUSEDALL
	int i = isThereAnySpeechGoingOn ();
	if (i == -1) {
		setVariable (fun -> reg, SVT_INT, 0);
	} else {
		setVariable (fun -> reg, SVT_OBJTYPE, i);
	}
	return BR_CONTINUE;
}

builtIn(skipSpeech)
{
	UNUSEDALL
	killSpeechTimers ();
	return BR_CONTINUE;
}

builtIn(getOverObject)
{
	UNUSEDALL
	if (overRegion)
		// Return value
		setVariable (fun -> reg, SVT_OBJTYPE, overRegion -> thisType -> objectNum);
	else
		// Return value
		setVariable (fun -> reg, SVT_INT, 0);
	return BR_CONTINUE;
}

builtIn(rename)
{
	UNUSEDALL
	char * newText = getTextFromAnyVar (fun -> stack -> thisVar);
	int objT;
	if (! newText) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (objT, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	objectType * o = findObjectType (objT);
	delete [] o -> screenName;
	o -> screenName = newText;
	return BR_CONTINUE;
}

builtIn (getObjectX)
{
	UNUSEDALL
	int objectNumber;
	if (! getValueType (objectNumber, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);

	onScreenPerson * pers = findPerson (objectNumber);
	if (pers) {
		setVariable (fun -> reg, SVT_INT, pers -> x);
	} else {
		screenRegion * la = getRegionForObject (objectNumber);
		if (la) {
			setVariable (fun -> reg, SVT_INT, la -> sX);
		} else {
			setVariable (fun -> reg, SVT_INT, 0);
		}
	}
	return BR_CONTINUE;
}

builtIn (getObjectY)
{
	UNUSEDALL
	int objectNumber;
	if (! getValueType (objectNumber, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);

	onScreenPerson * pers = findPerson (objectNumber);
	if (pers) {
		setVariable (fun -> reg, SVT_INT, pers -> y);
	} else {
		screenRegion * la = getRegionForObject (objectNumber);
		if (la) {
			setVariable (fun -> reg, SVT_INT, la -> sY);
		} else {
			setVariable (fun -> reg, SVT_INT, 0);
		}
	}
	return BR_CONTINUE;
}


builtIn(addScreenRegion)
{
	UNUSEDALL
	int sX, sY, x1, y1, x2, y2, di, objectNumber;
	if (! getValueType (di, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (sY, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (sX, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (y2, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (x2, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (y1, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (x1, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (objectNumber, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (addScreenRegion (x1, y1, x2, y2, sX, sY, di, objectNumber)) return BR_CONTINUE;
	return BR_ERROR;

}

builtIn(removeScreenRegion)
{
	UNUSEDALL
	int objectNumber;
	if (! getValueType (objectNumber, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	removeScreenRegion (objectNumber);
	return BR_CONTINUE;
}

builtIn(showBoxes)
{
	UNUSEDALL
	showBoxes ();
	return BR_CONTINUE;
}

builtIn(removeAllScreenRegions)
{
	UNUSEDALL
	killAllRegions ();
	return BR_CONTINUE;
}

builtIn(addCharacter)
{
	UNUSEDALL
	int x, y, objectNumber;

	// Our reference goes on every return; addPerson takes its own.
	CostumeRef p (getCostumeFromVar (fun -> stack -> thisVar));
	if (p.get () == NULL) return BR_ERROR;

	trimStack (fun -> stack);
	if (! getValueType (y, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (x, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (objectNumber, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (addPerson (x, y, objectNumber, p.get ())) return BR_CONTINUE;
	return BR_ERROR;
}

builtIn(hideCharacter)
{
	UNUSEDALL
	int objectNumber;
	if (! getValueType (objectNumber, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	setShown (false, objectNumber);
	return BR_CONTINUE;
}

builtIn(showCharacter)
{
	UNUSEDALL
	int objectNumber;
	if (! getValueType (objectNumber, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	setShown (true, objectNumber);
	return BR_CONTINUE;
}

builtIn(removeAllCharacters)
{
	UNUSEDALL
	killSpeechTimers ();
	killMostPeople ();
	return BR_CONTINUE;
}

builtIn(setCharacterDrawMode)
{
	UNUSEDALL
	int obj, di;
	if (! getValueType (di, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (obj, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	setDrawMode (di, obj);
	return BR_CONTINUE;
}
builtIn(setCharacterTransparency)
{
	UNUSEDALL
	int obj, x;
	if (! getValueType (x, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (obj, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	setPersonTransparency (obj, x);
	return BR_CONTINUE;
}
builtIn(setCharacterColourise)
{
	UNUSEDALL
	int obj, r, g, b, mix;
	if (! getValueType (mix, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (b, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (g, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (r, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (obj, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	setPersonColourise (obj, r, g, b, mix);
	return BR_CONTINUE;
}

builtIn(setScale)
{
	UNUSEDALL
	int val1, val2;
	if (! getValueType (val2, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (val1, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	setScale ((short int) val1, (short int) val2);
	return BR_CONTINUE;
}

builtIn(stopCharacter)
{
	UNUSEDALL
	int obj;
	if (! getValueType (obj, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);

	// Return value
	setVariable (fun -> reg, SVT_INT, stopPerson (obj));
	return BR_CONTINUE;
}

builtIn(pasteCharacter)
{
	UNUSEDALL
	int obj;
	if (! getValueType (obj, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);

	onScreenPerson * thisPerson = findPerson (obj);
	if (thisPerson) {
		personaAnimation * myAnim;
		myAnim = thisPerson -> myAnim;
		// As before, a new animation starts again here, without its events.
		if (myAnim != thisPerson -> playback.playing) restartAnimationQuietly (thisPerson -> playback, myAnim);

		// A character of parts pastes its parts, as they are now.
		if (thisPerson -> rig) {
			computePose (* thisPerson, true);
			pasteRiggedPerson (thisPerson);
			computePose (* thisPerson);
			setVariable (fun -> reg, SVT_INT, 1);
			return BR_CONTINUE;
		}

		const SpriteRef * layered = layeredSprite (thisPerson, true);
		if (layered || isTimeline (myAnim)) {
			const SpriteRef * r = layered ? layered : sampleSprite (* myAnim -> definition, "", currentTime (thisPerson -> playback, myAnim));
			const spriteBank * bank;
			const sprite * s = resolveSprite (r, bank);
			if (s) fixScaleSprite (thisPerson -> x, thisPerson -> y, * s, * bank, thisPerson, 0, 0, r -> mirror);
			setVariable (fun -> reg, SVT_INT, 1);
			return BR_CONTINUE;
		}

		int fNum = myAnim -> frame (thisPerson -> playback.frame).sprite;
		const spriteBank & bank = myAnim -> sprites () -> bank;
		fixScaleSprite (thisPerson -> x, thisPerson -> y, bank.sprites[abs (fNum)], bank, thisPerson, 0, 0, fNum < 0);
		setVariable (fun -> reg, SVT_INT, 1);
	} else {
		setVariable (fun -> reg, SVT_INT, 0);
	}
	return BR_CONTINUE;
}

builtIn(animate)
{
	UNUSEDALL
	int obj;
	AnimationHandle pp = getAnimationFromVar (fun -> stack -> thisVar);
	if (! pp) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (obj, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	// animatePerson takes the animation, and may free it.
	int time = timeForAnim (pp.get ());
	animatePerson (obj, std::move (pp));
	setVariable (fun -> reg, SVT_INT, time);
	return BR_CONTINUE;
}

builtIn(setCostume)
{
	UNUSEDALL
	int obj;
	// Our reference goes on every return; animatePerson takes its own.
	CostumeRef pp (getCostumeFromVar (fun -> stack -> thisVar));
	if (pp.get () == NULL) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (obj, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	animatePerson (obj, pp.get ());
	return BR_CONTINUE;
}

builtIn(floatCharacter)
{
	UNUSEDALL
	int obj, di;
	if (! getValueType (di, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (obj, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	setVariable (fun -> reg, SVT_INT, floatCharacter (di, obj));
	return BR_CONTINUE;
}

builtIn(setCharacterWalkSpeed)
{
	UNUSEDALL
	int obj, di;
	if (! getValueType (di, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (obj, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	setVariable (fun -> reg, SVT_INT, setCharacterWalkSpeed (di, obj));
	return BR_CONTINUE;
}

builtIn(turnCharacter)
{
	UNUSEDALL
	int obj, di;
	if (! getValueType (di, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (obj, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	setVariable (fun -> reg, SVT_INT, turnPersonToFace (obj, di));
	return BR_CONTINUE;
}

builtIn(setCharacterExtra)
{
	UNUSEDALL
	int obj, di;
	if (! getValueType (di, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (obj, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	setVariable (fun -> reg, SVT_INT, setPersonExtra (obj, di));
	return BR_CONTINUE;
}

builtIn(removeCharacter)
{
	UNUSEDALL
	int objectNumber;
	if (! getValueType (objectNumber, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	removeOneCharacter (objectNumber);
	return BR_CONTINUE;
}

static builtReturn moveChr(int numParams, loadedFunction * fun, bool force, bool immediate)
{
	switch (numParams) {
		case 3:
		{
			int x, y, objectNumber;

			if (! getValueType (y, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
			trimStack (fun -> stack);
			if (! getValueType (x, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
			trimStack (fun -> stack);
			if (! getValueType (objectNumber, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
			trimStack (fun -> stack);

			if (force) {
				if (forceWalkingPerson (x, y, objectNumber, fun, -1)) return BR_PAUSE;
			} else if (immediate) {
				jumpPerson (x, y, objectNumber);
			} else {
				if (makeWalkingPerson (x, y, objectNumber, fun, -1)) return BR_PAUSE;
			}
			return BR_CONTINUE;
		}

		case 2:
		{
			int toObj, objectNumber;
			screenRegion * reggie;

			if (! getValueType (toObj, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
			trimStack (fun -> stack);
			if (! getValueType (objectNumber, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
			trimStack (fun -> stack);
			reggie = getRegionForObject (toObj);
			if (reggie == NULL) return BR_CONTINUE;

			if (force)
			{
				if (forceWalkingPerson (reggie -> sX, reggie -> sY,	objectNumber, fun, reggie -> di)) return BR_PAUSE;
			}
			else if (immediate)
			{
				jumpPerson (reggie -> sX, reggie -> sY,	objectNumber);
			}
			else
			{
				if (makeWalkingPerson (reggie -> sX, reggie -> sY, objectNumber, fun, reggie -> di)) return BR_PAUSE;
			}
			return BR_CONTINUE;
		}

		default:
			fatal ("Built-in function must have either 2 or 3 parameters.");
			return BR_ERROR;
	}
}

builtIn(moveCharacter)
{
	UNUSEDALL
	return moveChr(numParams, fun, false, false);
}

builtIn(forceCharacter)
{
	UNUSEDALL
	return moveChr(numParams, fun, true, false);
}

builtIn(jumpCharacter)
{
	UNUSEDALL
	return moveChr(numParams, fun, false, true);
}

builtIn(clearStatus)
{
	UNUSEDALL
	clearStatusBar ();
	return BR_CONTINUE;
}

builtIn(removeLastStatus)
{
	UNUSEDALL
	killLastStatus ();
	return BR_CONTINUE;
}

builtIn(addStatus)
{
	UNUSEDALL
	addStatusBar ();
	return BR_CONTINUE;
}

builtIn(statusText)
{
	UNUSEDALL
	char * newText = getTextFromAnyVar (fun -> stack -> thisVar);
	if (! newText) return BR_ERROR;
	trimStack (fun -> stack);
	setStatusBar (newText);
	delete [] newText;
	return BR_CONTINUE;
}

builtIn(lightStatus)
{
	UNUSEDALL
	int val;
	if (! getValueType (val, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	setLitStatus (val);
	return BR_CONTINUE;
}

builtIn(positionStatus)
{
	UNUSEDALL
	int x, y;
	if (! getValueType (y, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (x, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	positionStatus (x, y);
	return BR_CONTINUE;
}

builtIn(alignStatus)
{
	UNUSEDALL
	int val;
	if (! getValueType (val, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	nowStatus -> alignStatus = (short) val;
	return BR_CONTINUE;
}

static bool getFuncNumForCallback(int numParams, loadedFunction * fun, int & functionNum)
{
	switch (numParams) {
		case 0:
			functionNum = 0;
			break;

		case 1:
			if (! getValueType (functionNum, SVT_FUNC, fun -> stack -> thisVar)) return false;
			trimStack (fun -> stack);
			break;

		default:
			fatal ("Too many parameters.");
			return false;
	}
	return true;
}

builtIn (onLeftMouse)
{
	UNUSEDALL
	int functionNum;
	if (getFuncNumForCallback (numParams, fun, functionNum))
	{
		currentEvents -> leftMouseFunction = functionNum;
		return BR_CONTINUE;
	}
	return BR_ERROR;
}

builtIn (onLeftMouseUp)
{
	UNUSEDALL
	int functionNum;
	if (getFuncNumForCallback (numParams, fun, functionNum))
	{
		currentEvents -> leftMouseUpFunction = functionNum;
		return BR_CONTINUE;
	}
	return BR_ERROR;
}

builtIn (onRightMouse)
{
	UNUSEDALL
	int functionNum;
	if (getFuncNumForCallback (numParams, fun, functionNum))
	{
		currentEvents -> rightMouseFunction = functionNum;
		return BR_CONTINUE;
	}
	return BR_ERROR;
}

builtIn (onRightMouseUp)
{
	UNUSEDALL
	int functionNum;
	if (getFuncNumForCallback (numParams, fun, functionNum))
	{
		currentEvents -> rightMouseUpFunction = functionNum;
		return BR_CONTINUE;
	}
	return BR_ERROR;
}

builtIn (onFocusChange)
{
	UNUSEDALL
	int functionNum;
	if (getFuncNumForCallback (numParams, fun, functionNum))
	{
		currentEvents -> focusFunction = functionNum;
		return BR_CONTINUE;
	}
	return BR_ERROR;
}

builtIn (onMoveMouse)
{
	UNUSEDALL
	int functionNum;
	if (getFuncNumForCallback (numParams, fun, functionNum))
	{
		currentEvents -> moveMouseFunction = functionNum;
		return BR_CONTINUE;
	}
	return BR_ERROR;
}

builtIn (onKeyboard)
{
	UNUSEDALL
	int functionNum;
	if (getFuncNumForCallback (numParams, fun, functionNum))
	{
		currentEvents -> spaceFunction = functionNum;
		return BR_CONTINUE;
	}
	return BR_ERROR;
}

builtIn (spawnSub)
{
	UNUSEDALL
	int functionNum;
	if (getFuncNumForCallback (numParams, fun, functionNum))
	{
		if (! startNewFunctionNum (functionNum, 0, NULL, noStack)) return BR_ERROR;
		return BR_CONTINUE;
	}
	return BR_ERROR;
}

builtIn (cancelSub)
{
	UNUSEDALL
	int functionNum;
	if (getFuncNumForCallback (numParams, fun, functionNum))
	{
		bool killedMyself;
		cancelAFunction (functionNum, fun, killedMyself);
		if (killedMyself) {
			abortFunction (fun);
			return BR_ALREADY_GONE;
		}
		return BR_CONTINUE;
	}
	return BR_ERROR;
}

builtIn(stringWidth)
{
	UNUSEDALL
	char * theText = getTextFromAnyVar (fun -> stack -> thisVar);
	if (! theText) return BR_ERROR;
	trimStack (fun -> stack);

	// Return value
	setVariable (fun -> reg, SVT_INT, stringWidth (theText));
	delete [] theText;
	return BR_CONTINUE;
}

builtIn(hardScroll)
{
	UNUSEDALL
	int v;
	if (! getValueType (v, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	hardScroll (v);
	return BR_CONTINUE;
}


builtIn(isScreenRegion)
{
	UNUSEDALL
	int objectNumber;
	if (! getValueType (objectNumber, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	setVariable (fun -> reg, SVT_INT, getRegionForObject (objectNumber) != NULL);
	return BR_CONTINUE;
}

builtIn(setSpeechSpeed)
{
	UNUSEDALL
	int number;
	if (! getValueType (number, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	speechSpeed = number * 0.01;
	setVariable (fun -> reg, SVT_INT, 1);
	return BR_CONTINUE;
}

builtIn(setFontSpacing)
{
	UNUSEDALL
	int fontSpaceI;
	if (! getValueType (fontSpaceI, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	fontSpace = fontSpaceI;
	trimStack (fun -> stack);
	setVariable (fun -> reg, SVT_INT, 1);
	return BR_CONTINUE;
}

builtIn(transitionLevel)
{
	UNUSEDALL
	int number;
	if (! getValueType (number, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);

	if (number < 0)
		brightnessLevel = 0;
	else if (number > 255)
		brightnessLevel = 255;
	else
		brightnessLevel = number;

	setVariable (fun -> reg, SVT_INT, 1);
	return BR_CONTINUE;
}

builtIn(captureAllKeys)
{
	UNUSEDALL
	captureAllKeys = getBoolean (fun -> stack -> thisVar);
	trimStack (fun -> stack);
	setVariable (fun -> reg, SVT_INT, captureAllKeys);
	return BR_CONTINUE;
}


builtIn(spinCharacter)
{
	UNUSEDALL
	int number, objectNumber;
	if (! getValueType (number, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (objectNumber, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);

	onScreenPerson * thisPerson = findPerson (objectNumber);
	if (thisPerson) {
		thisPerson -> wantAngle = number;
		thisPerson -> spinning = true;
		thisPerson -> continueAfterWalking = fun;
		setVariable (fun -> reg, SVT_INT, 1);
		return BR_PAUSE;
	} else {
		setVariable (fun -> reg, SVT_INT, 0);
		return BR_CONTINUE;
	}
}

builtIn(getCharacterDirection)
{
	UNUSEDALL
	int objectNumber;
	if (! getValueType (objectNumber, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	onScreenPerson * thisPerson = findPerson (objectNumber);
	if (thisPerson) {
		setVariable (fun -> reg, SVT_INT, thisPerson -> direction);
	} else {
		setVariable (fun -> reg, SVT_INT, 0);
	}
	return BR_CONTINUE;
}

builtIn(isCharacter)
{
	UNUSEDALL
	int objectNumber;
	if (! getValueType (objectNumber, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	onScreenPerson * thisPerson = findPerson (objectNumber);
	setVariable (fun -> reg, SVT_INT, thisPerson != NULL);
	return BR_CONTINUE;
}

builtIn(normalCharacter)
{
	UNUSEDALL
	int objectNumber;
	if (! getValueType (objectNumber, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	onScreenPerson * thisPerson = findPerson (objectNumber);
	if (thisPerson)
	{
		setPersonAnim (* thisPerson, thisPerson -> myPersona -> animation[thisPerson -> direction].get ());
		setVariable (fun -> reg, SVT_INT, 1);
	} else {
		setVariable (fun -> reg, SVT_INT, 0);
	}
	return BR_CONTINUE;
}

builtIn(isMoving)
{
	UNUSEDALL
	int objectNumber;
	if (! getValueType (objectNumber, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	onScreenPerson * thisPerson = findPerson (objectNumber);
	if (thisPerson)
	{
		setVariable (fun -> reg, SVT_INT, thisPerson -> walking);
	} else {
		setVariable (fun -> reg, SVT_INT, 0);
	}
	return BR_CONTINUE;
}

builtIn(fetchEvent)
{
	UNUSEDALL
	int obj1, obj2;
	if (! getValueType (obj2, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (obj1, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);

	int fNum = getCombinationFunction (obj1, obj2);

	// Return value
	if (fNum) {
		setVariable (fun -> reg, SVT_FUNC, fNum);
	} else {
		setVariable (fun -> reg, SVT_INT, 0);
	}
	return BR_CONTINUE;
}

builtIn(deleteFile)
{
	UNUSEDALL

	char * namNormal = getTextFromAnyVar (fun -> stack -> thisVar);
	trimStack (fun -> stack);
	char * nam = encodeFilename (namNormal);
	delete [] namNormal;
	if (failSecurityCheck (nam)) return BR_ERROR;
	setVariable (fun -> reg, SVT_INT, remove (nam));
	delete [] nam;

	return BR_CONTINUE;
}

builtIn(renameFile)
{
	UNUSEDALL
	char * temp;

	temp = getTextFromAnyVar (fun -> stack -> thisVar);
	char * newnam = encodeFilename (temp);
	trimStack (fun -> stack);
	if (failSecurityCheck (newnam)) return BR_ERROR;
	delete [] temp;

	temp = getTextFromAnyVar (fun -> stack -> thisVar);
	char * nam = encodeFilename (temp);
	trimStack (fun -> stack);
	if (failSecurityCheck (nam)) return BR_ERROR;
	delete [] temp;

	setVariable (fun -> reg, SVT_INT, rename (nam, newnam));
	delete [] nam;
	delete [] newnam;


	return BR_CONTINUE;
}

builtIn(cacheSound)
{
	UNUSEDALL
	int fileNumber;
	if (! getValueType (fileNumber, SVT_FILE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (cacheSound (fileNumber) == -1) return BR_ERROR;
	return BR_CONTINUE;
}

builtIn(burnString)
{
	UNUSEDALL
	char * newText = getTextFromAnyVar (fun -> stack -> thisVar);
	trimStack (fun -> stack);
	int y, x;
	if (! getValueType (y, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (x, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (x == IN_THE_CENTRE) x = (winWidth - stringWidth (newText)) >> 1;
	burnStringToBackdrop (newText, x, y);
	delete[] newText;
	return BR_CONTINUE;
}

builtIn(setCharacterSpinSpeed)
{
	UNUSEDALL
	int speed, who;
	if (! getValueType (speed, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (who, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);

	onScreenPerson * thisPerson = findPerson (who);

	if (thisPerson) {
		thisPerson -> spinSpeed = speed;
		setVariable (fun -> reg, SVT_INT, 1);
	} else {
		setVariable (fun -> reg, SVT_INT, 0);
	}
	return BR_CONTINUE;
}

builtIn(setCharacterAngleOffset)
{
	UNUSEDALL
	int angle, who;
	if (! getValueType (angle, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (who, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);

	onScreenPerson * thisPerson = findPerson (who);

	if (thisPerson) {
		thisPerson -> angleOffset = angle;
		setVariable (fun -> reg, SVT_INT, 1);
	} else {
		setVariable (fun -> reg, SVT_INT, 0);
	}
	return BR_CONTINUE;
}


builtIn(transitionMode)
{
	UNUSEDALL
	int n;
	if (! getValueType (n, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	fadeMode = n;
	trimStack (fun -> stack);
	setVariable (fun -> reg, SVT_INT, 1);
	return BR_CONTINUE;
}


// Removed function - does nothing
builtIn(_rem_updateDisplay)
{
	UNUSEDALL
	trimStack (fun -> stack);
	setVariable (fun -> reg, SVT_INT, true);
	return BR_CONTINUE;
}

builtIn(getSoundCache)
{
	UNUSEDALL
	fun -> reg.varType = SVT_STACK;
	fun -> reg.varData.theStack = new stackHandler;
	if (! checkNew (fun -> reg.varData.theStack)) return BR_ERROR;
	fun -> reg.varData.theStack -> first = NULL;
	fun -> reg.varData.theStack -> last = NULL;
	fun -> reg.varData.theStack -> timesUsed = 1;
	if (! getSoundCacheStack (fun -> reg.varData.theStack)) return BR_ERROR;
	return BR_CONTINUE;
}

builtIn(saveCustomData)
{
	UNUSEDALL
	// saveCustomData (thisStack, fileName);
	char * fileNameB = getTextFromAnyVar (fun -> stack -> thisVar);
	if (! checkNew (fileNameB)) return BR_ERROR;

	char * fileName = encodeFilename (fileNameB);
	delete [] fileNameB;

	if (failSecurityCheck (fileName)) return BR_ERROR;
	trimStack (fun -> stack);

	if (fun -> stack -> thisVar.varType != SVT_STACK) {
		fatal ("First parameter isn't a stack");
		return BR_ERROR;
	}
	if (! stackToFile (fileName, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	delete [] fileName;
	return BR_CONTINUE;
}

builtIn(loadCustomData)
{
	UNUSEDALL

	char * newTextA = getTextFromAnyVar (fun -> stack -> thisVar);
	if (! checkNew (newTextA)) return BR_ERROR;

	char * newText = encodeFilename (newTextA);
	delete [] newTextA;

	if (failSecurityCheck (newText)) return BR_ERROR;
	trimStack (fun -> stack);

	unlinkVar (fun -> reg);
	fun -> reg.varType = SVT_STACK;
	fun -> reg.varData.theStack = new stackHandler;
	if (! checkNew (fun -> reg.varData.theStack)) return BR_ERROR;
	fun -> reg.varData.theStack -> first = NULL;
	fun -> reg.varData.theStack -> last = NULL;
	fun -> reg.varData.theStack -> timesUsed = 1;
	if (! fileToStack (newText, fun -> reg.varData.theStack)) return BR_ERROR;
	delete [] newText;
	return BR_CONTINUE;
}

builtIn(setCustomEncoding)
{
	UNUSEDALL
	int n;
	if (! getValueType (n, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	saveEncoding = n;
	trimStack (fun -> stack);
	setVariable (fun -> reg, SVT_INT, 1);
	return BR_CONTINUE;
}

builtIn(freeSound)
{
	UNUSEDALL
	int v;
	if (! getValueType (v, SVT_FILE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	huntKillFreeSound (v);
	return BR_CONTINUE;
}

builtIn(parallaxAdd)
{
	UNUSEDALL
	if (frozenStuff) {
		fatal ("Can't set background parallax image while frozen");
		return BR_ERROR;
	} else {
		int wrapX, wrapY, v;
		if (! getValueType (wrapY, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
		trimStack (fun -> stack);
		if (! getValueType (wrapX, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
		trimStack (fun -> stack);
		if (! getValueType (v, SVT_FILE, fun -> stack -> thisVar)) return BR_ERROR;
		trimStack (fun -> stack);

		if (! loadParallax (v, wrapX, wrapY)) return BR_ERROR;
		setVariable (fun -> reg, SVT_INT, 1);
	}
	return BR_CONTINUE;
}

builtIn(parallaxClear)
{
	UNUSEDALL
	killParallax ();
	setVariable (fun -> reg, SVT_INT, 1);
	return BR_CONTINUE;
}

builtIn(getPixelColour)
{
	UNUSEDALL
	int x, y;
	if (! getValueType (y, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (x, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);

	unlinkVar (fun -> reg);
	fun -> reg.varType = SVT_STACK;
	fun -> reg.varData.theStack = new stackHandler;
	if (! checkNew (fun -> reg.varData.theStack)) return BR_ERROR;
	fun -> reg.varData.theStack -> first = NULL;
	fun -> reg.varData.theStack -> last = NULL;
	fun -> reg.varData.theStack -> timesUsed = 1;
	if (! getRGBIntoStack (x, y, fun -> reg.varData.theStack)) return BR_ERROR;

	return BR_CONTINUE;
}

builtIn(makeFastArray)
{
	UNUSEDALL
	switch (fun -> stack -> thisVar.varType) {
		case SVT_STACK:
		{
			bool success = makeFastArrayFromStack (fun -> reg, fun -> stack -> thisVar.varData.theStack);
			trimStack (fun -> stack);
			return success ? BR_CONTINUE : BR_ERROR;
		}
			break;

		case SVT_INT:
		{
			int i = fun -> stack -> thisVar.varData.intValue;
			trimStack (fun -> stack);
			return makeFastArraySize (fun -> reg, i) ? BR_CONTINUE : BR_ERROR;
		}
			break;

		default:
			break;
	}
	fatal ("Parameter must be a number or a stack.");
	return BR_ERROR;
}

builtIn(getCharacterScale)
{
	UNUSEDALL
	int objectNumber;
	if (! getValueType (objectNumber, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);

	onScreenPerson * pers = findPerson (objectNumber);
	if (pers) {
		setVariable (fun -> reg, SVT_INT, pers -> scale * 100);
	} else {
		setVariable (fun -> reg, SVT_INT, 0);
	}
	return BR_CONTINUE;
}

builtIn(getLanguageID)
{
	UNUSEDALL
	setVariable (fun -> reg, SVT_INT, gameSettings.languageID);
	return BR_CONTINUE;
}

// Removed function
builtIn(_rem_launchWith)
{
	UNUSEDALL

	trimStack (fun -> stack);
	trimStack (fun -> stack);
	setVariable (fun -> reg, SVT_INT, false);

	return BR_CONTINUE;

}

builtIn(getFramesPerSecond)
{
	UNUSEDALL
	setVariable (fun -> reg, SVT_INT, lastFramesPerSecond);
	return BR_CONTINUE;
}

builtIn(showThumbnail)
{
	UNUSEDALL
	int x, y;
	if (! getValueType (y, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (x, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);

	// Encode the name! Encode the name!
	char * aaaaa = getTextFromAnyVar (fun -> stack -> thisVar);
	//				deb ("Got name:", aaaaa;)
	trimStack (fun -> stack);
	//				deb ("About to encode", aaaaa);
	char * file = encodeFilename (aaaaa);
	//				deb ("Made new name", file);
	//				deb ("aaaaa is still ", aaaaa);
	delete[] aaaaa;
	//				deb ("Deleted", "aaaaa");
	showThumbnail (file, x, y);
	delete[] file;
	return BR_CONTINUE;
}

builtIn(setThumbnailSize)
{
	UNUSEDALL
	if (! getValueType (thumbHeight, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (thumbWidth, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (thumbWidth < 0 || thumbHeight < 0 || thumbWidth > winWidth || thumbHeight > winHeight) {
		char buff[50];
		snprintf (buff, sizeof (buff), "%d x %d", thumbWidth, thumbHeight);
		fatal ("Invalid thumbnail size", buff);
		return BR_ERROR;
	}
	return BR_CONTINUE;
}

builtIn(hasFlag)
{
	UNUSEDALL
	int objNum, flagIndex;
	if (! getValueType (flagIndex, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (objNum, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	objectType * objT = findObjectType (objNum);
	if (! objT) return BR_ERROR;
	setVariable (fun -> reg, SVT_INT, objT->flags & (1 << flagIndex));
	return BR_CONTINUE;
}

builtIn(snapshotGrab)
{
	UNUSEDALL
	return snapshot () ? BR_CONTINUE : BR_ERROR;
}

builtIn(snapshotClear)
{
	UNUSEDALL
	nosnapshot ();
	return BR_CONTINUE;
}

builtIn(bodgeFilenames)
{
	UNUSEDALL
	bool lastValue = allowAnyFilename;
	allowAnyFilename = getBoolean (fun -> stack -> thisVar);
	trimStack (fun -> stack);
	setVariable (fun -> reg, SVT_INT, lastValue);
	return BR_CONTINUE;
}

// Deprecated - does nothing.
builtIn(_rem_registryGetString)
{
	UNUSEDALL
	trimStack (fun -> stack);
	trimStack (fun -> stack);
	setVariable (fun -> reg, SVT_INT, 0);

	return BR_CONTINUE;
}

builtIn(quitWithFatalError)
{
	UNUSEDALL
	char * mess = getTextFromAnyVar (fun -> stack -> thisVar);
	trimStack (fun -> stack);
	fatal (mess);
	return BR_ERROR;
}

builtIn(_rem_setCharacterAA)
{
	UNUSEDALL

	trimStack (fun -> stack);
	trimStack (fun -> stack);
	trimStack (fun -> stack);
	trimStack (fun -> stack);

	return BR_CONTINUE;
}

builtIn(_rem_setMaximumAA)
{
	UNUSEDALL

	trimStack (fun -> stack);
	trimStack (fun -> stack);
	trimStack (fun -> stack);

	return BR_CONTINUE;

}

builtIn(setBackgroundEffect)
{
	UNUSEDALL
	bool done = blur_createSettings(numParams, fun->stack);
	setVariable (fun -> reg, SVT_INT, done ? 1 : 0);
	return BR_CONTINUE;
}

builtIn(doBackgroundEffect)
{
	UNUSEDALL
	bool done = blurScreen ();
	setVariable (fun -> reg, SVT_INT, done ? 1 : 0);
	return BR_CONTINUE;
}

#pragma mark -
#pragma mark Clips: animations of tracks (animmodel.h)

extern const char * typeName[];

// The values that clipSprite takes in place of a sprite bank and sprite.
const int clipNoSprite = -1;		// NOSPRITE: an empty frame
const int clipPartDefault = -2;		// PART_DEFAULT: the part's own sprite

// The clip of a variable that a script builds, to change; NULL after fatal.
static ClipDefinition * clipToChange (variable & v) {
	if (v.varType != SVT_ANIM) {
		fatal ("Expecting a clip of newClip (); found variable of type", typeName[v.varType]);
		return NULL;
	}
	std::string why;
	ClipDefinition * c = editableClip (animationOf (v), why);
	if (! c) fatal (why.c_str ());
	return c;
}

// The name of a part: any text.
static bool getPartName (variable & v, std::string & name) {
	char * text = getTextFromAnyVar (v);
	if (! text) return false;
	name = text;
	delete [] text;
	return true;
}

// newClip (length [, loop]): an empty clip of length ticks, to fill with
// clipSprite, clipFrames and clipEvent, and play with animate and costume.
builtIn(newClip)
{
	UNUSEDALL
	if (numParams < 1 || numParams > 2) {
		fatal ("Built-in function newClip() must have 1 or 2 parameters.");
		return BR_ERROR;
	}
	int loop = 0, length;
	if (numParams == 2) {
		if (! getValueType (loop, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
		trimStack (fun -> stack);
	}
	if (! getValueType (length, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (length < 0) {
		fatal ("A clip can't be shorter than 0 ticks.");
		return BR_ERROR;
	}
	std::shared_ptr<ClipDefinition> clip = std::make_shared<ClipDefinition> ();
	clip -> length = length;
	clip -> loop = loop != 0;
	AnimationHandle h (new personaAnimation);
	h -> definition = clip;
	newAnimationVariable (fun -> reg, std::move (h));
	return BR_CONTINUE;
}

// clipSprite (clip, part, tick, file, sprite): from tick on, the part shows
// that sprite of the bank (negative for its mirror image), until its next
// key. clipSprite (clip, part, tick, NOSPRITE) shows nothing there, and
// clipSprite (clip, part, tick, PART_DEFAULT) the part's own sprite.
builtIn(clipSprite)
{
	UNUSEDALL
	if (numParams < 4 || numParams > 5) {
		fatal ("Built-in function clipSprite() must have 4 or 5 parameters.");
		return BR_ERROR;
	}
	Key key;
	if (numParams == 5) {
		int number, file;
		if (! getValueType (number, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
		trimStack (fun -> stack);
		if (! getValueType (file, SVT_FILE, fun -> stack -> thisVar)) return BR_ERROR;
		trimStack (fun -> stack);
		SpriteBankRef bank = loadBankForAnim (file);
		if (! bank) return BR_ERROR;	// fatal done already
		key.sprite = SpriteRef::sprite (bank, file, number >= 0 ? number : number == INT_MIN ? INT_MAX : - number, number < 0);
	} else {
		int which;
		if (! getValueType (which, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
		trimStack (fun -> stack);
		if (which == clipNoSprite) {
			key.sprite = SpriteRef::none ();
		} else if (which == clipPartDefault) {
			key.sprite = SpriteRef ();
		} else {
			fatal ("With 4 parameters, clipSprite() takes NOSPRITE or PART_DEFAULT; give a sprite bank and a sprite for a sprite.");
			return BR_ERROR;
		}
	}
	if (! getValueType (key.time, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	std::string part;
	if (! getPartName (fun -> stack -> thisVar, part)) return BR_ERROR;
	trimStack (fun -> stack);
	ClipDefinition * clip = clipToChange (fun -> stack -> thisVar);
	if (! clip) return BR_ERROR;
	trimStack (fun -> stack);
	key.easing = EASING_STEP;
	std::string why;
	if (! setKey (* clip, part, CHANNEL_SPRITE, key, why)) {
		fatal (why.c_str ());
		return BR_ERROR;
	}
	return BR_CONTINUE;
}

// clipFrames (clip, part, anim (...)): the frames of an animation of
// anim () as the part's sprites, from the start of the clip to its end,
// with the animation's sounds and functions as events (see addLegacyFrames).
builtIn(clipFrames)
{
	UNUSEDALL
	AnimationHandle frames = getAnimationFromVar (fun -> stack -> thisVar);
	if (! frames) return BR_ERROR;
	trimStack (fun -> stack);
	std::string part;
	if (! getPartName (fun -> stack -> thisVar, part)) return BR_ERROR;
	trimStack (fun -> stack);
	ClipDefinition * clip = clipToChange (fun -> stack -> thisVar);
	if (! clip) return BR_ERROR;
	trimStack (fun -> stack);
	std::string why;
	if (! addLegacyFrames (* clip, part, * frames, why)) {
		fatal (why.c_str ());
		return BR_ERROR;
	}
	return BR_CONTINUE;
}

// clipEvent (clip, tick, sound or function): play the sound, or start the
// function, when the clip passes tick.
builtIn(clipEvent)
{
	UNUSEDALL
	FrameEvent what;
	variable & v = fun -> stack -> thisVar;
	if (v.varType == SVT_FILE) {
		what.kind = FrameEvent::SOUND;
	} else if (v.varType == SVT_FUNC) {
		what.kind = FrameEvent::FUNCTION;
	} else {
		fatal ("Expecting a sound file or a function for clipEvent(); found variable of type", typeName[v.varType]);
		return BR_ERROR;
	}
	what.number = v.varData.intValue;
	trimStack (fun -> stack);
	int tick;
	if (! getValueType (tick, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	ClipDefinition * clip = clipToChange (fun -> stack -> thisVar);
	if (! clip) return BR_ERROR;
	trimStack (fun -> stack);
	std::string why;
	if (! addEvent (* clip, tick, what, why)) {
		fatal (why.c_str ());
		return BR_ERROR;
	}
	return BR_CONTINUE;
}

// The character and layer of the functions of layers, from the stack:
// layer 0 is the costume's (and animate's), 1 to maxClipLayer playClip's.
static bool getCharacterLayer (loadedFunction * fun, int & obj, int & layer) {
	if (! getValueType (layer, SVT_INT, fun -> stack -> thisVar)) return false;
	trimStack (fun -> stack);
	if (! getValueType (obj, SVT_OBJTYPE, fun -> stack -> thisVar)) return false;
	trimStack (fun -> stack);
	if (layer < 0 || layer > maxClipLayer) {
		fatal ("Layers of clips are numbered from 0 to 99.");
		return false;
	}
	return true;
}

// setClipSpeed (character, layer, percent): 100 is normal speed, 0 pauses.
// For animations of anim () too.
builtIn(setClipSpeed)
{
	UNUSEDALL
	int percent, obj, layer;
	if (! getValueType (percent, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getCharacterLayer (fun, obj, layer)) return BR_ERROR;
	if (percent < 0 || percent > 100000) {
		fatal ("The speed of a clip is a percentage from 0 to 100000.");
		return BR_ERROR;
	}
	int speed = std::min (maxPlaybackSpeed, (int) (((long long) percent * subTicksPerTick + 50) / 100));
	onScreenPerson * p = findPerson (obj);
	bool done = false;
	if (p && layer == 0) {
		p -> playback.speed = speed;
		done = true;
	} else if (p) {
		ClipLayer * l = personLayer (* p, layer);
		if (l) l -> playback.speed = speed;
		done = l != NULL;
	}
	setVariable (fun -> reg, SVT_INT, done);
	return BR_CONTINUE;
}

// isClipPlaying (character, layer): whether the character plays a clip
// there that hasn't finished. Animations of anim () never finish.
builtIn(isClipPlaying)
{
	UNUSEDALL
	int obj, layer;
	if (! getCharacterLayer (fun, obj, layer)) return BR_ERROR;
	onScreenPerson * p = findPerson (obj);
	bool playing = false;
	if (p && layer == 0 && p -> myAnim) {
		if (isTimeline (p -> myAnim)) playing = ! timelineFinished (p -> playback, p -> myAnim);
		else playing = p -> myAnim -> numFrames () > 0;
	} else if (p && layer > 0) {
		ClipLayer * l = personLayer (* p, layer);
		playing = l && ! timelineFinished (l -> playback, l -> anim.get ());
	}
	setVariable (fun -> reg, SVT_INT, playing);
	return BR_CONTINUE;
}


#pragma mark -
#pragma mark Rigs: characters of parts (animmodel.h, animpose.h)

// The rig of a variable that a script builds, to change; NULL after fatal.
// Like clips, a rig can change until something uses it (setCharacterRig).
static RigDefinition * rigToChange (variable & v) {
	if (v.varType != SVT_RIG) {
		fatal ("Expecting a rig of newRig (); found variable of type", typeName[v.varType]);
		return NULL;
	}
	const RigDefinitionRef & rig = rigOf (v);
	if (! rig) {
		fatal ("This rig is empty.");
		return NULL;
	}
	if (rig -> sealed) {
		fatal ("This rig is already used by a character, so it can't change; make another with newRig ()");
		return NULL;
	}
	return const_cast<RigDefinition *> (rig.get ());
}

// newRig (): a rig of nothing but its root, the character itself ("").
builtIn(newRig)
{
	UNUSEDALL
	newRigVariable (fun -> reg, std::make_shared<RigDefinition> ());
	return BR_CONTINUE;
}

// addPart (rig, name, parent, x, y [, file, sprite]): a part called name,
// attached to the part called parent ("" for the character) with its
// hotspot at (x, y) from the parent's. Without a sprite, it is a joint: it
// draws nothing, and places what is attached to it.
builtIn(addPart)
{
	UNUSEDALL
	if (numParams != 5 && numParams != 7) {
		fatal ("Built-in function addPart() must have 5 or 7 parameters.");
		return BR_ERROR;
	}
	SpriteRef sprite;
	if (numParams == 7) {
		int number, file;
		if (! getValueType (number, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
		trimStack (fun -> stack);
		if (! getValueType (file, SVT_FILE, fun -> stack -> thisVar)) return BR_ERROR;
		trimStack (fun -> stack);
		SpriteBankRef bank = loadBankForAnim (file);
		if (! bank) return BR_ERROR;	// fatal done already
		sprite = SpriteRef::sprite (bank, file, number >= 0 ? number : number == INT_MIN ? INT_MAX : - number, number < 0);
	}
	PartPose rest;
	int x, y;
	if (! getValueType (y, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (x, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	rest.x = (float) x;
	rest.y = (float) y;
	std::string name, parent;
	if (! getPartName (fun -> stack -> thisVar, parent)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getPartName (fun -> stack -> thisVar, name)) return BR_ERROR;
	trimStack (fun -> stack);
	RigDefinition * rig = rigToChange (fun -> stack -> thisVar);
	if (! rig) return BR_ERROR;
	trimStack (fun -> stack);
	std::string why;
	if (! addPart (* rig, name, parent, rest, sprite, why)) {
		fatal (why.c_str ());
		return BR_ERROR;
	}
	return BR_CONTINUE;
}

// setPartDepth (rig, name, depth): the order of drawing within the
// character: deeper (lower) first; parts of the same depth in the order
// they were added.
builtIn(setPartDepth)
{
	UNUSEDALL
	int depth;
	if (! getValueType (depth, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	std::string name;
	if (! getPartName (fun -> stack -> thisVar, name)) return BR_ERROR;
	trimStack (fun -> stack);
	RigDefinition * rig = rigToChange (fun -> stack -> thisVar);
	if (! rig) return BR_ERROR;
	trimStack (fun -> stack);
	int part = rig -> findPart (name);
	if (part < 0) {
		fatal ("There is no part of that name in this rig:", name.c_str ());
		return BR_ERROR;
	}
	rig -> parts[part].depth = depth;
	return BR_CONTINUE;
}

// setPartPivot (rig, name, x, y): the point the part turns, scales and
// flips about, and that its x, y place: its sprite's hotspot, moved by x, y
// of the sprite's pixels. It moves the sprite about the part's origin; the
// origin, and the parts attached to it, stay where they are.
builtIn(setPartPivot)
{
	UNUSEDALL
	int x, y;
	if (! getValueType (y, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (x, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	std::string name;
	if (! getPartName (fun -> stack -> thisVar, name)) return BR_ERROR;
	trimStack (fun -> stack);
	RigDefinition * rig = rigToChange (fun -> stack -> thisVar);
	if (! rig) return BR_ERROR;
	trimStack (fun -> stack);
	int part = rig -> findPart (name);
	if (part < 0) {
		fatal ("There is no part of that name in this rig:", name.c_str ());
		return BR_ERROR;
	}
	rig -> parts[part].pivotX = (float) x;
	rig -> parts[part].pivotY = (float) y;
	return BR_CONTINUE;
}

// The rigs and clips of a file of the game: a rig (.rig) or clip (.clp) of
// the project, as the compiler packs it (see animassets.h of the compiler).
static bool readAnimationFile (int file, animformat::Document & doc) {
	setResourceForFatal (file);
	unsigned int length = openFileFromNum (file);
	if (! length) return fatal ("Can't open a rig or clip");
	animformat::Bytes bytes (length);
	bool read = fread (bytes.data (), 1, length, bigDataFile) == length;
	finishAccess ();
	if (! read) return fatal ("Can't open a rig or clip - the file ends too soon.");
	std::string why;
	if (! animformat::decode (bytes.data (), bytes.size (), doc, why)) return fatal ("Not a rig or clip:", why.c_str ());
	setResourceForFatal (-1);
	return true;
}

// loadRig ('a.rig'): the rig of a file that the Dev Kit made, which a script
// may add to until a character has it, as one of newRig.
builtIn(loadRig)
{
	UNUSEDALL
	int file;
	if (! getValueType (file, SVT_FILE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	animformat::Document doc;
	if (! readAnimationFile (file, doc)) return BR_ERROR;
	if (doc.rigs.size () != 1 || ! doc.clips.empty ()) {
		fatal ("loadRig () takes a rig (a .rig file) of the project.");
		return BR_ERROR;
	}
	std::shared_ptr<RigDefinition> rig = std::make_shared<RigDefinition> (doc.rigs[0]);
	if (! resolveBanks (* rig, loadBankForAnim)) return BR_ERROR;	// fatal done already
	newRigVariable (fun -> reg, rig);
	return BR_CONTINUE;
}

// loadClip ('a.clp'): the clip of a file that the Dev Kit made, which a
// script may add to until it is used, as one of newClip.
builtIn(loadClip)
{
	UNUSEDALL
	int file;
	if (! getValueType (file, SVT_FILE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	animformat::Document doc;
	if (! readAnimationFile (file, doc)) return BR_ERROR;
	if (doc.clips.size () != 1 || ! doc.rigs.empty ()) {
		fatal ("loadClip () takes a clip (a .clp file) of the project.");
		return BR_ERROR;
	}
	std::shared_ptr<ClipDefinition> clip = std::make_shared<ClipDefinition> (doc.clips[0]);
	if (! resolveBanks (* clip, loadBankForAnim)) return BR_ERROR;
	AnimationHandle h (new personaAnimation);
	h -> definition = clip;
	newAnimationVariable (fun -> reg, std::move (h));
	return BR_CONTINUE;
}

// setCharacterRig (character, rig): the character is of the rig's parts,
// and its animations move them; 0 makes it a character of one sprite again.
builtIn(setCharacterRig)
{
	UNUSEDALL
	RigDefinitionRef rig;
	variable & v = fun -> stack -> thisVar;
	if (v.varType == SVT_RIG) {
		rig = rigOf (v);
	} else if (! (v.varType == SVT_INT && v.varData.intValue == 0)) {
		fatal ("Expecting a rig of newRig (), or 0; found variable of type", typeName[v.varType]);
		return BR_ERROR;
	}
	trimStack (fun -> stack);
	int obj;
	if (! getValueType (obj, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	setVariable (fun -> reg, SVT_INT, setPersonRig (obj, rig));
	return BR_CONTINUE;
}

// clipKey (clip, part, property, tick, value [, easing]): from tick on, the
// part's property moves towards value: PART_X and PART_Y in pixels,
// PART_ROTATION in degrees (clockwise), PART_SCALE (both axes), PART_SCALE_X
// and PART_SCALE_Y in percent, PART_DEPTH,
// and PART_VISIBLE and PART_FLIP as TRUE or FALSE. The easing (towards the
// next key) is EASE_LINEAR unless given: EASE_STEP holds the value until
// the next key, EASE_INOUT eases in and out. Depth, visibility and
// flipping always step.
builtIn(clipKey)
{
	UNUSEDALL
	if (numParams != 5 && numParams != 6) {
		fatal ("Built-in function clipKey() must have 5 or 6 parameters.");
		return BR_ERROR;
	}
	int easing = EASING_LINEAR;
	if (numParams == 6) {
		if (! getValueType (easing, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
		trimStack (fun -> stack);
		if (easing < 0 || easing >= NUM_EASINGS) {
			fatal ("Not an easing: use EASE_STEP, EASE_LINEAR or EASE_INOUT.");
			return BR_ERROR;
		}
	}
	int value, tick, property;
	if (! getValueType (value, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (tick, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (! getValueType (property, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	if (property < 0 || property >= NUM_CHANNELS || property == CHANNEL_SPRITE) {
		fatal ("Not a property of a part: use PART_X, PART_Y, PART_ROTATION, PART_SCALE, PART_SCALE_X, PART_SCALE_Y, PART_DEPTH, PART_VISIBLE or PART_FLIP (and clipSprite for sprites).");
		return BR_ERROR;
	}
	std::string part;
	if (! getPartName (fun -> stack -> thisVar, part)) return BR_ERROR;
	trimStack (fun -> stack);
	ClipDefinition * clip = clipToChange (fun -> stack -> thisVar);
	if (! clip) return BR_ERROR;
	trimStack (fun -> stack);
	Channel channel = (Channel) property;
	Key key;
	key.time = tick;
	key.value = isScale (channel) ? value / 100.0f : (float) value;
	if (channel == CHANNEL_VISIBLE || channel == CHANNEL_FLIP) key.value = value ? 1.0f : 0.0f;
	key.easing = isDiscrete (channel) ? EASING_STEP : (Easing) easing;
	std::string why;
	if (! setKey (* clip, part, channel, key, why)) {
		fatal (why.c_str ());
		return BR_ERROR;
	}
	return BR_CONTINUE;
}

#pragma mark -
#pragma mark Layers and overrides

// playClip (character, layer, clip [, LAYER_OVERRIDE or LAYER_ADD]): play a
// clip on a layer of its own (1 to 99), over the costume's layer (0) and
// the layers below it, from its start. An override layer sets what its
// clip keys; an additive one adds to places and angles, and multiplies
// scales. Returns the clip's length, as animate does.
builtIn(playClip)
{
	UNUSEDALL
	if (numParams != 3 && numParams != 4) {
		fatal ("Built-in function playClip() must have 3 or 4 parameters.");
		return BR_ERROR;
	}
	int blend = LAYER_BLEND_OVERRIDE;
	if (numParams == 4) {
		if (! getValueType (blend, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
		trimStack (fun -> stack);
		if (blend < 0 || blend >= NUM_LAYER_BLENDS) {
			fatal ("A layer is LAYER_OVERRIDE or LAYER_ADD.");
			return BR_ERROR;
		}
	}
	AnimationHandle clip = getAnimationFromVar (fun -> stack -> thisVar);
	if (! clip) return BR_ERROR;
	trimStack (fun -> stack);
	int obj, layer;
	if (! getCharacterLayer (fun, obj, layer)) return BR_ERROR;
	if (layer == 0) {
		fatal ("Layer 0 is the costume's: play a clip there with animate ().");
		return BR_ERROR;
	}
	if (! isTimeline (clip.get ())) {
		fatal ("playClip() plays clips of newClip (); make one of anim () with clipFrames ().");
		return BR_ERROR;
	}
	int length = timeForAnim (clip.get ());
	playPersonLayer (obj, layer, std::move (clip), (LayerBlend) blend);
	setVariable (fun -> reg, SVT_INT, length);
	return BR_CONTINUE;
}

// stopClip (character, layer): the layer goes, and what it did with it.
builtIn(stopClip)
{
	UNUSEDALL
	int obj, layer;
	if (! getCharacterLayer (fun, obj, layer)) return BR_ERROR;
	if (layer == 0) {
		fatal ("Layer 0 is the costume's: it can't stop.");
		return BR_ERROR;
	}
	setVariable (fun -> reg, SVT_INT, stopPersonLayer (obj, layer));
	return BR_CONTINUE;
}

// The part and property of setPartProperty and clearPartProperty.
static bool getPartProperty (loadedFunction * fun, std::string & part, Channel & channel) {
	int property;
	if (! getValueType (property, SVT_INT, fun -> stack -> thisVar)) return false;
	trimStack (fun -> stack);
	if (property < 0 || property >= NUM_CHANNELS || property == CHANNEL_SPRITE) {
		fatal ("Not a property of a part: use PART_X, PART_Y, PART_ROTATION, PART_SCALE, PART_SCALE_X, PART_SCALE_Y, PART_DEPTH, PART_VISIBLE or PART_FLIP.");
		return false;
	}
	channel = (Channel) property;
	if (! getPartName (fun -> stack -> thisVar, part)) return false;
	trimStack (fun -> stack);
	return true;
}

// setPartProperty (character, part, property, value): the property of the
// part has value, over every layer, until clearPartProperty: in the units
// of clipKey.
builtIn(setPartProperty)
{
	UNUSEDALL
	int value, obj;
	if (! getValueType (value, SVT_INT, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	std::string part;
	Channel channel;
	if (! getPartProperty (fun, part, channel)) return BR_ERROR;
	if (! getValueType (obj, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	float v = isScale (channel) ? value / 100.0f : (float) value;
	if (channel == CHANNEL_VISIBLE || channel == CHANNEL_FLIP) v = value ? 1.0f : 0.0f;
	setVariable (fun -> reg, SVT_INT, setPersonOverride (obj, part, channel, v));
	return BR_CONTINUE;
}

// clearPartProperty (character, part, property): the layers decide it again.
builtIn(clearPartProperty)
{
	UNUSEDALL
	int obj;
	std::string part;
	Channel channel;
	if (! getPartProperty (fun, part, channel)) return BR_ERROR;
	if (! getValueType (obj, SVT_OBJTYPE, fun -> stack -> thisVar)) return BR_ERROR;
	trimStack (fun -> stack);
	setVariable (fun -> reg, SVT_INT, clearPersonOverride (obj, part, channel));
	return BR_CONTINUE;
}

// mirrorClip (clip): a new clip, the mirror image of clip: the character
// ("") flips, where clip had it flip or not. It can change, as a clip of
// newClip can.
builtIn(mirrorClip)
{
	UNUSEDALL
	variable & v = fun -> stack -> thisVar;
	if (v.varType != SVT_ANIM || ! isTimeline (animationOf (v))) {
		fatal ("mirrorClip() takes a clip of newClip ().");
		return BR_ERROR;
	}
	std::shared_ptr<ClipDefinition> mirrored = std::make_shared<ClipDefinition> (* animationOf (v) -> definition);
	trimStack (fun -> stack);
	mirrored -> sealed = false;
	Track * flip = NULL;
	for (size_t t = 0; t < mirrored -> tracks.size (); t ++) {
		if (mirrored -> tracks[t].part.empty () && mirrored -> tracks[t].channel == CHANNEL_FLIP) flip = & mirrored -> tracks[t];
	}
	if (flip) {
		for (size_t k = 0; k < flip -> keys.size (); k ++) flip -> keys[k].value = flip -> keys[k].value != 0 ? 0.0f : 1.0f;
	} else {
		Key key;
		key.value = 1;
		key.easing = EASING_STEP;
		std::string why;
		if (! setKey (* mirrored, "", CHANNEL_FLIP, key, why)) {
			fatal (why.c_str ());
			return BR_ERROR;
		}
	}
	AnimationHandle h (new personaAnimation);
	h -> definition = mirrored;
	newAnimationVariable (fun -> reg, std::move (h));
	return BR_CONTINUE;
}

#pragma mark -
#pragma mark Other functions

//-------------------------------------

#define FUNC(special,name)		{builtIn_ ## name},
static builtInFunctionData builtInFunctionArray[] =
{
#include "functionlist.h"
};
#undef FUNC

#define FUNC(special,name)		{#name},
char builtInFunctionNames[][25] =
{
#include "functionlist.h"
};
#undef FUNC

#define NUM_FUNCS			(sizeof (builtInFunctionArray) / sizeof (builtInFunctionArray[0]))



builtReturn callBuiltIn (int whichFunc, int numParams, loadedFunction * fun) {
   // fprintf (stderr, "Calling function %d: %s\n", whichFunc, builtInFunctionNames[whichFunc]);    fflush (stderr);
	if (numBIFNames) {

		//		deb ("IN:", (fun -> originalNumber < numUserFunc) ? allUserFunc[fun -> originalNumber] : "Unknown user function");
		//		deb ("GO:", (whichFunc < numBIFNames) ? allBIFNames[whichFunc] : "Unknown built-in function");

		setFatalInfo (
					  (fun -> originalNumber < numUserFunc) ? allUserFunc[fun -> originalNumber] : "Unknown user function",
					  (whichFunc < numBIFNames) ? allBIFNames[whichFunc] : "Unknown built-in function");
	}

	if (whichFunc < NUM_FUNCS) {
		if (paramNum[whichFunc] != -1) {
			if (paramNum[whichFunc] != numParams) {
				char buff[100];
				snprintf (buff, sizeof (buff), "Built in function must have %i parameter%s",
						 paramNum[whichFunc],
						 (paramNum[whichFunc] == 1) ? "" : "s");

				fatal (copyString (buff));
				return BR_ERROR;
			}
		}

		if (builtInFunctionArray[whichFunc].func)
		{
			//fprintf (stderr, "Calling %i: %s\n", whichFunc, builtInFunctionNames[whichFunc]);
			return builtInFunctionArray[whichFunc].func (numParams, fun);
		}
	}

	fatal ("Unknown / unimplemented built-in function.");
	return BR_ERROR;
}

