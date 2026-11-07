#ifndef SLUDGER_H
#define SLUDGER_H

#include <stdint.h>

#include "variable.h"
#include "csludge.h"
#include "language.h"

#ifndef _WIN32
typedef struct _FILETIME {
	uint32_t dwLowDateTime;
	uint32_t dwHighDateTime;
} FILETIME;
#endif


struct eventHandlers {
	int leftMouseFunction;
	int leftMouseUpFunction;
	int rightMouseFunction;
	int rightMouseUpFunction;
	int moveMouseFunction;
	int focusFunction;
	int spaceFunction;
};

struct lineOfCode {
	sludgeCommand				theCommand;
	int32_t						param;
};

struct loadedFunction {
	int							originalNumber;
	lineOfCode *				compiledLines;
	int							numLocals, timeLeft, numArgs;
	variable *					localVars;
	variableStack *				stack;
	variable					reg;
	unsigned int				runThisLine;
	loadedFunction *			calledBy;
	loadedFunction *			next;
	bool						returnSomething, isSpeech, unfreezable, cancelMe;
	bool						waitsForMovie;	// for the movie that it plays
	unsigned char				freezerLevel;
};

struct inputType {
	bool leftClick, rightClick, justMoved, leftRelease, rightRelease;
	int mouseX, mouseY, keyPressed;
};

extern unsigned char * gameIcon;
extern int iconW, iconH;


bool initSludge (char *);
void sludgeDisplay ();
// Show in the window the frame that we drew.
void showFrame ();
int startNewFunctionNum (unsigned int, unsigned int, loadedFunction *, variableStack * &, bool = true);
bool handleInput ();
void restartFunction (loadedFunction * fun);
bool loadFunctionCode (loadedFunction * newFunc);
void loadHandlers (FILE * fp);
void saveHandlers (FILE * fp);
void finishFunction (loadedFunction * fun);
void abortFunction (loadedFunction * fun);
// newest: the latest version that we read; games are of WHOLE_VERSION at most,
// saved games of SAVE_VERSION.
FILE * openAndVerify (char * filename, char extra1, char extra2, const char * er, int & fileVersion, int newest);
void freezeSubs ();
void unfreezeSubs ();
void completeTimers ();
void killSpeechTimers ();
int cancelAFunction (int funcNum, loadedFunction * myself, bool & killedMyself);

#endif
