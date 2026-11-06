#include "allfiles.h"
#include <math.h>
#include <stdlib.h>

#include "version.h"
#include "sprites.h"
#include "sprbanks.h"
#include "sludger.h"
#include "objtypes.h"
#include "region.h"
#include "people.h"
#include "talk.h"
#include "newfatal.h"
#include "variable.h"
#include "moreio.h"
#include "loadsave.h"
#include "floor.h"
#include "zbuffer.h"
#include "sound.h"
#include "debug.h"

#define ANGLEFIX (180.0 / 3.14157)
#define ANI_STAND 0
#define ANI_WALK 1
#define ANI_TALK 2

extern speechStruct * speech;

extern variableStack * noStack;

extern int ssgVersion;

extern int cameraX, cameraY;
screenRegion personRegion;
extern screenRegion * lastRegion;
extern flor * currentFloor;

extern inputType input;
onScreenPerson * allPeople = NULL;
short int scaleHorizon = 75;
short int scaleDivide = 150;
extern screenRegion * allScreenRegions;

#define TF_max(a, b) ((a > b) ? a : b)

inline int TF_abs (int a) {
	return (a > 0) ? a : -a;
}

// Free the animation the character owned, unless it is keep.
static void dropOwnAnim (onScreenPerson & m, const personaAnimation * keep) {
	if (! m.ownAnim || m.ownAnim.get () == keep) return;
	// Another animation could take its address, so that the playback
	// wouldn't see the change. Before, the old animation was never freed.
	if (m.playback.playing == m.ownAnim.get ()) m.playback.playing = NULL;
	m.ownAnim.reset ();
}

void setPersonAnim (onScreenPerson & m, personaAnimation * anim) {
	dropOwnAnim (m, anim);
	m.myAnim = anim;
}

void givePersonAnim (onScreenPerson & m, AnimationHandle anim) {
	dropOwnAnim (m, anim.get ());
	m.myAnim = anim.get ();
	m.ownAnim = std::move (anim);
}

void setFrames (onScreenPerson & m, int a) {
	setPersonAnim (m, m.myPersona -> animation[(a * m.myPersona -> numDirections) + m.direction].get ());
}

void retainCostume (persona * cossy) {
	if (cossy) cossy -> timesUsed ++;
}

void releaseCostume (persona * cossy) {
	if (! cossy) return;
	if (-- cossy -> timesUsed > 0) return;
	delete cossy;	// With its animations
}

// The character no longer uses old: forget the last animation if it was one
// of old, as the animations of old may go.
static void forgetCostume (onScreenPerson & m, persona * old) {
	if (! old) return;
	for (size_t a = 0; a < old -> animation.size (); a ++) {
		if (m.playback.playing == old -> animation[a].get ()) m.playback.playing = NULL;
	}
}

static void deletePerson (onScreenPerson * p) {
	setPersonAnim (* p, NULL);
	forgetCostume (* p, p -> myPersona);
	releaseCostume (p -> myPersona);
	delete p;
}

// Holds a character while loadPeople reads it, and deletes it, with what it
// owns, if loading stops before the character is in allPeople.
class PersonLoadGuard {
public:
	explicit PersonLoadGuard (onScreenPerson * p) : p (p) {}
	~PersonLoadGuard () { if (p) deletePerson (p); }
	onScreenPerson * release () { onScreenPerson * r = p; p = NULL; return r; }
private:
	onScreenPerson * p;
	PersonLoadGuard (const PersonLoadGuard &);
	PersonLoadGuard & operator= (const PersonLoadGuard &);
};

std::vector<AnimationFrame> readAnimationFrames (int num, variableStack * & stacky) {
	std::vector<AnimationFrame> frames (num);

	// A sound or a function is a frame too, which keeps the sprite and
	// ticks of the frame after it (the stack has the frames backwards). It
	// is passed when played, but its ticks count in timeForAnim.
	int a = num, frameNum = 0, howMany = 0;

	while (a) {
		a --;
		int noise = 0;
		if (stacky -> thisVar.varType == SVT_FILE) {
			noise = stacky -> thisVar.varData.intValue;
		} else if (stacky -> thisVar.varType == SVT_FUNC) {
			noise = - stacky -> thisVar.varData.intValue;
		} else if (stacky -> thisVar.varType == SVT_STACK) {
			getValueType (frameNum, SVT_INT, stacky -> thisVar.varData.theStack -> first -> thisVar);
			getValueType (howMany, SVT_INT, stacky -> thisVar.varData.theStack -> first -> next -> thisVar);
		} else {
			getValueType (frameNum, SVT_INT, stacky -> thisVar);
			howMany = 1;
		}
		trimStack (stacky);
		frames[a].sprite = frameNum;
		frames[a].ticks = howMany;
		frames[a].event = FrameEvent::fromNoise (noise);
	}

	return frames;
}

AnimationHandle makeNullAnim () {
	// No definition: no frames.
	return AnimationHandle (new personaAnimation);
}

AnimationHandle copyAnim (const personaAnimation * orig) {
	AnimationHandle newAnim (new personaAnimation);
	newAnim -> definition = orig -> definition;
	return newAnim;
}

void turnMeAngle (onScreenPerson * thisPerson, int direc) {
	int d = thisPerson -> myPersona -> numDirections;
	thisPerson -> angle = direc;
	direc += (180 / d) + 180 + thisPerson -> angleOffset;
	while (direc >= 360) direc -= 360;
	thisPerson -> direction = (direc * d) / 360;
}

bool initPeople () {
	personRegion.sX = 0;
	personRegion.sY = 0;
	personRegion.di = -1;
	allScreenRegions = NULL;

	return true;
}

void spinStep (onScreenPerson * thisPerson) {
	int diff = (thisPerson -> angle + 360) - thisPerson -> wantAngle;
	int eachSlice = thisPerson -> spinSpeed ? thisPerson -> spinSpeed : (360 / thisPerson -> myPersona -> numDirections);
	while (diff > 180) {
		diff -= 360;
	}

	if (diff >= eachSlice) {
		turnMeAngle (thisPerson, thisPerson -> angle - eachSlice);
	} else if (diff <= - eachSlice) {
		turnMeAngle (thisPerson, thisPerson -> angle + eachSlice);
	} else {
		turnMeAngle (thisPerson, thisPerson -> wantAngle);
		thisPerson -> spinning = false;
	}
}

void rethinkAngle (onScreenPerson * thisPerson) {
	int d = thisPerson -> myPersona -> numDirections;
	int direc = thisPerson -> angle + (180 / d) + 180 + thisPerson -> angleOffset;
	while (direc >= 360) direc -= 360;
	thisPerson -> direction = (direc * d) / 360;
}

bool turnPersonToFace (int thisNum, int direc) {
	onScreenPerson * thisPerson = findPerson (thisNum);
	if (thisPerson) {
		if (thisPerson -> continueAfterWalking) abortFunction (thisPerson -> continueAfterWalking);
		thisPerson -> continueAfterWalking = NULL;
		thisPerson -> walking = false;
		thisPerson -> spinning = false;
		turnMeAngle (thisPerson, direc);
		setFrames (* thisPerson, (thisPerson == speech->currentTalker) ? ANI_TALK : ANI_STAND);
		return true;
	}
	return false;
}

bool setPersonExtra (int thisNum, int extra) {
	onScreenPerson * thisPerson = findPerson (thisNum);
	if (thisPerson) {
		thisPerson -> extra = extra;
		if (extra & EXTRA_NOSCALE) thisPerson -> scale = 1;
		return true;
	}
	return false;
}

void setScale (short int h, short int d) {
	scaleHorizon = h;
	scaleDivide = d;
}

void moveAndScale (onScreenPerson & me, float x, float y) {
	me.x = x;
	me.y = y;
	if (! (me.extra & EXTRA_NOSCALE) && scaleDivide) me.scale = (me.y - scaleHorizon) / scaleDivide;
}

onScreenPerson * findPerson (int v) {
	onScreenPerson * thisPerson = allPeople;
	while (thisPerson) {
		if (v == thisPerson -> thisType -> objectNum) break;
		thisPerson = thisPerson -> next;
	}
	return thisPerson;
}

void movePerson (int x, int y, int objNum) {
	onScreenPerson * moveMe = findPerson (objNum);
	if (moveMe) moveAndScale (* moveMe, x, y);
}

void setShown (bool h, int ob) {
	onScreenPerson * moveMe = findPerson (ob);
	if (moveMe) moveMe -> show = h;
}

enum drawModes {
	drawModeNormal,
	drawModeTransparent1,
	drawModeTransparent2,
	drawModeTransparent3,
	drawModeDark1,
	drawModeDark2,
	drawModeDark3,
	drawModeBlack,
	drawModeShadow1,
	drawModeShadow2,
	drawModeShadow3,
	drawModeFoggy1,
	drawModeFoggy2,
	drawModeFoggy3,
	drawModeFoggy4,
	drawModeGlow1,
	drawModeGlow2,
	drawModeGlow3,
	drawModeGlow4,
	drawModeInvisible,
	numDrawModes
};

void setMyDrawMode (onScreenPerson *moveMe, int h) {
	switch (h) {
		case drawModeTransparent3:
			moveMe->r=moveMe->g=moveMe->b=0;
			moveMe->colourmix=0;
			moveMe->transparency = 64;
			break;
		case drawModeTransparent2:
			moveMe->r=moveMe->g=moveMe->b=0;
			moveMe->colourmix=0;
			moveMe->transparency = 128;
			break;
		case drawModeTransparent1:
			moveMe->r=moveMe->g=moveMe->b=0;
			moveMe->colourmix=0;
			moveMe->transparency = 192;
			break;
		case drawModeInvisible:
			moveMe->r=moveMe->g=moveMe->b=0;
			moveMe->colourmix=0;
			moveMe->transparency = 254;
			break;
		case drawModeDark1:
			moveMe->r=moveMe->g=moveMe->b=0;
			moveMe->colourmix=192;
			moveMe->transparency = 0;
			break;
		case drawModeDark2:
			moveMe->r=moveMe->g=moveMe->b=0;
			moveMe->colourmix=128;
			moveMe->transparency = 0;
			break;
		case drawModeDark3:
			moveMe->r=moveMe->g=moveMe->b=0;
			moveMe->colourmix=64;
			moveMe->transparency = 0;
			break;
		case drawModeBlack:
			moveMe->r=moveMe->g=moveMe->b=0;
			moveMe->colourmix=255;
			moveMe->transparency = 0;
			break;
		case 	drawModeShadow1:
			moveMe->r=moveMe->g=moveMe->b=0;
			moveMe->colourmix=255;
			moveMe->transparency = 64;
			break;
		case 	drawModeShadow2:
			moveMe->r=moveMe->g=moveMe->b=0;
			moveMe->colourmix=255;
			moveMe->transparency = 128;
			break;
		case 	drawModeShadow3:
			moveMe->r=moveMe->g=moveMe->b=0;
			moveMe->colourmix=255;
			moveMe->transparency = 192;
			break;
		case 	drawModeFoggy3:
			moveMe->r=moveMe->g=moveMe->b=128;
			moveMe->colourmix=192;
			moveMe->transparency = 0;
			break;
		case 	drawModeFoggy2:
			moveMe->r=moveMe->g=moveMe->b=128;
			moveMe->colourmix=128;
			moveMe->transparency = 0;
			break;
		case 	drawModeFoggy1:
			moveMe->r=moveMe->g=moveMe->b=128;
			moveMe->colourmix=64;
			moveMe->transparency = 0;
			break;
		case 	drawModeFoggy4:
			moveMe->r=moveMe->g=moveMe->b=128;
			moveMe->colourmix=255;
			moveMe->transparency = 0;
			break;
		case 	drawModeGlow3:
			moveMe->r=moveMe->g=moveMe->b=255;
			moveMe->colourmix=192;
			moveMe->transparency = 0;
			break;
		case 	drawModeGlow2:
			moveMe->r=moveMe->g=moveMe->b=255;
			moveMe->colourmix=128;
			moveMe->transparency = 0;
			break;
		case 	drawModeGlow1:
			moveMe->r=moveMe->g=moveMe->b=255;
			moveMe->colourmix=64;
			moveMe->transparency = 0;
			break;
		case 	drawModeGlow4:
			moveMe->r=moveMe->g=moveMe->b=255;
			moveMe->colourmix=255;
			moveMe->transparency = 0;
			break;
		default:
			moveMe->r=moveMe->g=moveMe->b=0;
			moveMe->colourmix=0;
			moveMe->transparency = 0;
			break;
	}
	
}

void setDrawMode (int h, int ob) {
	onScreenPerson * moveMe = findPerson (ob);
	if (! moveMe) return;
		
	setMyDrawMode (moveMe, h);
}

void setPersonTransparency (int ob, unsigned char x) {
	onScreenPerson * moveMe = findPerson (ob);
	if (! moveMe) return;
	
	if (x > 254) x = 254;
	moveMe->transparency = x;
}

void setPersonColourise (int ob, unsigned char r, unsigned char g, unsigned char b, unsigned char colourmix) {
	onScreenPerson * moveMe = findPerson (ob);
	if (! moveMe) return;
	
	moveMe->r = r;
	moveMe->g = g;
	moveMe->b = b;
	moveMe->colourmix = colourmix;
}



extern screenRegion * overRegion;

void shufflePeople () {
	onScreenPerson * * thisReference = & allPeople;
	onScreenPerson * A, * B;

	if (! allPeople) return;

	while ((* thisReference) -> next) {
		float y1 = (* thisReference) -> y;
		if ((* thisReference) -> extra & EXTRA_FRONT) y1 += 1000;

		float y2 = (* thisReference) -> next -> y;
		if ((* thisReference) -> next -> extra & EXTRA_FRONT) y2 += 1000;

		if (y1 > y2) {
			A = (* thisReference);
			B = (* thisReference) -> next;
			A -> next = B -> next;
			B -> next = A;
			(* thisReference) = B;
		} else {
			thisReference = & ((* thisReference) -> next);
		}
	}
}



namespace {

// Frame events start sounds and script functions, as drawPeople always did.
class EngineFrameEvents : public FrameEventSink {
public:
	void frameEvent (const FrameEvent & e) {
		if (e.kind == FrameEvent::SOUND) {
			startSound (e.number, false);
		} else if (e.kind == FrameEvent::FUNCTION) {
			startNewFunctionNum (e.number, 0, NULL, noStack);
		}
	}
};

}

void logLifetimes (const char * when) {
	static int on = -1;
	if (on < 0) on = getenv ("SLUDGE_LIFETIME_LOG") != NULL;
	if (! on) return;
	fprintf (stderr, "Lifetimes %s: %d handles, %d definitions, %d costumes, %d characters, %d banks\n", when,
		LifetimeCounter<personaAnimation>::live (), LifetimeCounter<AnimationDefinition>::live (),
		LifetimeCounter<persona>::live (), LifetimeCounter<onScreenPerson>::live (), spriteBankStats ().loaded);
}

void updatePeopleAnimations (int ticks) {
	// One pass of the sort a tick, as before, so that characters update and
	// fire their events in the order they are drawn in.
	shufflePeople ();
	EngineFrameEvents events;
	for (onScreenPerson * p = allPeople; p; p = p -> next) {
		updateAnimation (p -> playback, p -> myAnim, ticks, p -> show, events);
	}
}

void drawPeople () {
	overRegion = NULL;

	for (const onScreenPerson * thisPerson = allPeople; thisPerson; thisPerson = thisPerson -> next) {
		const personaAnimation * myAnim = thisPerson -> myAnim;
		if (! thisPerson -> show || ! myAnim || ! myAnim -> numFrames ()) continue;

		// The frame that the last update chose. Until an update has started
		// a new animation (as when freeze draws), its first frame.
		int f = thisPerson -> playback.playing == myAnim ? thisPerson -> playback.displayFrame : 0;
		if (f >= myAnim -> numFrames ()) f = 0;

		int fNumSign = myAnim -> frame (f).sprite;
		int m = fNumSign < 0;
		int fNum = abs (fNumSign);
		const spriteBank & bank = myAnim -> sprites () -> bank;
		if (fNum >= bank.total) {
			fNum = 0;
			m = 2 - m;
		}
		if (m != 2) {
			bool r = scaleSprite (bank.sprites[fNum], bank, thisPerson, m);
			if (r) {
				if (thisPerson -> thisType -> screenName[0]) {
					if (personRegion.thisType != thisPerson -> thisType) lastRegion = NULL;
					personRegion.thisType = thisPerson -> thisType;
					overRegion = & personRegion;
				}
			}
		}
	}
}

void makeTalker (onScreenPerson & me) {
	setFrames (me, ANI_TALK);
}

void makeSilent (onScreenPerson & me) {
	setFrames (me, ANI_STAND);
}

bool handleClosestPoint (int & setX, int & setY, int & setPoly) {
	int gotX = 320, gotY = 200, gotPoly = -1, i, j, xTest1, yTest1,
		xTest2, yTest2, closestX, closestY, oldJ, currentDistance = 0xFFFFF,
		thisDistance;

//	FILE * dbug = fopen ("debug_closest.txt", "at");
//	fprintf (dbug, "\nGetting closest point to %i, %i\n", setX, setY);

	for (i = 0; i < currentFloor -> numPolygons; i ++) {
		oldJ = currentFloor -> polygon[i].numVertices - 1;
		for (j = 0; j < currentFloor -> polygon[i].numVertices; j ++) {
//			fprintf (dbug, "Polygon %i, line %i... ", i, j);
			xTest1 = currentFloor -> vertex[currentFloor -> polygon[i].vertexID[j]].x;
			yTest1 = currentFloor -> vertex[currentFloor -> polygon[i].vertexID[j]].y;
			xTest2 = currentFloor -> vertex[currentFloor -> polygon[i].vertexID[oldJ]].x;
			yTest2 = currentFloor -> vertex[currentFloor -> polygon[i].vertexID[oldJ]].y;
			closestPointOnLine (closestX, closestY, xTest1, yTest1, xTest2, yTest2, setX, setY);
//			fprintf (dbug, "closest point is %i, %i... ", closestX, closestY);
			xTest1 = setX - closestX;
			yTest1 = setY - closestY;
			thisDistance = xTest1 * xTest1 + yTest1 * yTest1;
//			fprintf (dbug, "Distance squared %i\n", thisDistance);

			if (thisDistance < currentDistance) {
//				fprintf (dbug, "** We have a new winner! **\n");

				currentDistance = thisDistance;
				gotX = closestX;
				gotY = closestY;
				gotPoly = i;
			}
			oldJ = j;
		}
	}
//	fclose (dbug);

	if (gotPoly == -1) return false;
	setX = gotX;
	setY = gotY;
	setPoly = gotPoly;

	return true;
}

bool doBorderStuff (onScreenPerson * moveMe) {
	if (moveMe -> inPoly == moveMe -> walkToPoly) {
		moveMe -> inPoly = -1;
		moveMe -> thisStepX = moveMe -> walkToX;
		moveMe -> thisStepY = moveMe -> walkToY;
	} else {
		// The section in which we need to be next...
		int newPoly = currentFloor -> matrix[moveMe -> inPoly][moveMe -> walkToPoly];
		if (newPoly == -1) return false;

		// Grab the index of the second matching corner...
		int ID, ID2;
		if (! getMatchingCorners (currentFloor -> polygon[moveMe -> inPoly], currentFloor -> polygon[newPoly], ID, ID2))
			return fatal ("Not a valid floor plan!");

		// Remember that we're walking to the new polygon...
		moveMe -> inPoly = newPoly;

		// Calculate the destination position on the coincidantal line...
		int x1 = moveMe -> x, y1 = moveMe -> y;
		int x2 = moveMe -> walkToX, y2 = moveMe -> walkToY;
		int x3 = currentFloor -> vertex[ID].x, y3 = currentFloor -> vertex[ID].y;
		int x4 = currentFloor -> vertex[ID2].x, y4 = currentFloor -> vertex[ID2].y;

		int xAB = x1 - x2;
		int yAB = y1 - y2;
		int xCD = x4 - x3;
		int yCD = y4 - y3;

		double m = (yAB * (x3 - x1) - xAB * (y3 - y1));
		m /= ((xAB * yCD) - (yAB * xCD));

		if (m > 0 && m < 1) {
			moveMe -> thisStepX = x3 + m * xCD;
			moveMe -> thisStepY = y3 + m * yCD;
		} else {
			int dx13 = x1-x3, dx14 = x1-x4, dx23 = x2-x3, dx24 = x2-x4;
			int dy13 = y1-y3, dy14 = y1-y4, dy23 = y2-y3, dy24 = y2-y4;

			dx13 *= dx13; dx14 *= dx14; dx23 *= dx23; dx24 *= dx24;
			dy13 *= dy13; dy14 *= dy14; dy23 *= dy23; dy24 *= dy24;

			if (sqrt((double) dx13 + dy13) + sqrt((double) dx23 + dy23) <
				sqrt((double) dx14 + dy14) + sqrt((double) dx24 + dy24)) {
				moveMe -> thisStepX = x3;
				moveMe -> thisStepY = y3;
			} else {
				moveMe -> thisStepX = x4;
				moveMe -> thisStepY = y4;
			}
		}
	}

	float yDiff = moveMe -> thisStepY - moveMe -> y;
	float xDiff = moveMe -> x - moveMe -> thisStepX;
	if (xDiff || yDiff) {
		moveMe -> wantAngle = 180 + ANGLEFIX * atan2(xDiff, yDiff * 2);
		moveMe -> spinning = true;
	}

	setFrames (* moveMe, ANI_WALK);
	return true;
}

bool walkMe (onScreenPerson * thisPerson, bool move = true) {
	float xDiff, yDiff, maxDiff, s;

	for (;;) {
		xDiff = thisPerson -> thisStepX - thisPerson -> x;
		yDiff = (thisPerson -> thisStepY - thisPerson -> y) * 2;
		s = thisPerson -> scale * thisPerson -> walkSpeed;
		if (s < 0.2) s = 0.2;

		maxDiff = (TF_abs (xDiff) >= TF_abs (yDiff)) ? TF_abs (xDiff) : TF_abs (yDiff);

		if (TF_abs (maxDiff) > s) {
			if (thisPerson -> spinning) {
				spinStep (thisPerson);
				setFrames (* thisPerson, ANI_WALK);
			}
			s = maxDiff / s;
			if (move)
				moveAndScale (* thisPerson,
							  thisPerson -> x + xDiff / s,
							  thisPerson -> y + yDiff / (s * 2));
			return true;
		}

		if (thisPerson -> inPoly == -1) {
			if (thisPerson -> directionWhenDoneWalking != -1) {
				thisPerson -> wantAngle = thisPerson -> directionWhenDoneWalking;
				thisPerson -> spinning = true;
				spinStep (thisPerson);
			}
			break;
		}
		if (! doBorderStuff (thisPerson)) break;
	}

	thisPerson -> walking = false;
	setFrames (* thisPerson, ANI_STAND);
	moveAndScale (* thisPerson,
				  thisPerson -> walkToX,
				  thisPerson -> walkToY);
	return false;
}

bool makeWalkingPerson (int x, int y, int objNum, loadedFunction * func, int di) {
	if (x == 0 && y == 0) return false;
	if (currentFloor -> numPolygons == 0) return false;
	onScreenPerson * moveMe = findPerson (objNum);
	if (! moveMe) return false;

	if (moveMe -> continueAfterWalking) abortFunction (moveMe -> continueAfterWalking);
	moveMe -> continueAfterWalking = NULL;
	moveMe -> walking = true;
	moveMe -> directionWhenDoneWalking = di;

	moveMe -> walkToX = x;
	moveMe -> walkToY = y;
	moveMe -> walkToPoly = inFloor (x, y);
	if (moveMe -> walkToPoly == -1) {
		if (! handleClosestPoint (moveMe -> walkToX, moveMe -> walkToY, moveMe -> walkToPoly)) return false;
	}

	moveMe -> inPoly = inFloor (moveMe -> x, moveMe -> y);
	if (moveMe -> inPoly == -1) {
		int xxx = moveMe -> x, yyy = moveMe -> y;
		if (! handleClosestPoint (xxx, yyy, moveMe -> inPoly)) return false;
	}

	doBorderStuff (moveMe);
	if (walkMe (moveMe, false) || moveMe -> spinning) {
		moveMe -> continueAfterWalking = func;
		return true;
	} else {
		return false;
	}
}

bool stopPerson (int o) {
	onScreenPerson * moveMe = findPerson (o);
	if (moveMe)
		if (moveMe -> continueAfterWalking) {
			abortFunction (moveMe -> continueAfterWalking);
			moveMe -> continueAfterWalking = NULL;
			moveMe -> walking = false;
			moveMe -> spinning = false;
			setFrames (* moveMe, ANI_STAND);
			return true;
		}
	return false;
}

bool forceWalkingPerson (int x, int y, int objNum, loadedFunction * func, int di) {
	if (x == 0 && y == 0) return false;
	onScreenPerson * moveMe = findPerson (objNum);
	if (! moveMe) return false;

	if (moveMe -> continueAfterWalking) abortFunction (moveMe -> continueAfterWalking);
	moveMe -> walking = true;
	moveMe -> continueAfterWalking = NULL;
	moveMe -> directionWhenDoneWalking = di;

	moveMe -> walkToX = x;
	moveMe -> walkToY = y;

	// Let's pretend the start and end points are both in the same
	// polygon (which one isn't important)
	moveMe -> inPoly = 0;
	moveMe -> walkToPoly = 0;

	doBorderStuff (moveMe);
	if (walkMe (moveMe) || moveMe -> spinning) {
		moveMe -> continueAfterWalking = func;
		return true;
	} else {
		return false;
	}
}

void jumpPerson (int x, int y, int objNum) {
	if (x == 0 && y == 0) return;
	onScreenPerson * moveMe = findPerson (objNum);
	if (! moveMe) return;
	if (moveMe -> continueAfterWalking) abortFunction (moveMe -> continueAfterWalking);
	moveMe -> continueAfterWalking = NULL;
	moveMe -> walking = false;
	moveMe -> spinning = false;
	moveAndScale (* moveMe, x, y);
}

bool floatCharacter (int f, int objNum) {
	onScreenPerson * moveMe = findPerson (objNum);
	if (! moveMe) return false;
	moveMe -> floaty = f;
	return true;
}

bool setCharacterWalkSpeed (int f, int objNum) {
	if (f <= 0) return false;
	onScreenPerson * moveMe = findPerson (objNum);
	if (! moveMe) return false;
	moveMe -> walkSpeed = f;
	return true;
}

void walkAllPeople () {
	onScreenPerson * thisPerson = allPeople;

	while (thisPerson) {
		if (thisPerson -> walking) {
			walkMe (thisPerson);
		} else if (thisPerson -> spinning) {
			spinStep (thisPerson);
			setFrames (* thisPerson, ANI_STAND);
		}
		if ((! thisPerson -> walking) && (! thisPerson -> spinning) && thisPerson -> continueAfterWalking) {
			restartFunction (thisPerson -> continueAfterWalking);
			thisPerson -> continueAfterWalking = NULL;
		}
		thisPerson = thisPerson -> next;
	}
}

bool addPerson (int x, int y, int objNum, persona * p) {
	onScreenPerson * newPerson = new onScreenPerson;
	if (! checkNew (newPerson)) return false;

	// EASY STUFF
	newPerson -> thisType = loadObjectType (objNum);
	newPerson -> scale = 1;
	newPerson -> extra = 0;
	newPerson -> continueAfterWalking = NULL;
	moveAndScale (* newPerson, x, y);
	newPerson -> walkToX = x;
	newPerson -> walkToY = y;
	newPerson -> walking = false;
	newPerson -> spinning = false;
	newPerson -> show = true;
	newPerson -> direction = 0;
	newPerson -> angle = 180;
	newPerson -> wantAngle = 180;
	newPerson -> angleOffset = 0;
	newPerson -> floaty = 0;
	newPerson -> walkSpeed = newPerson -> thisType -> walkSpeed;
	newPerson -> myAnim = NULL;
	newPerson -> spinSpeed = newPerson -> thisType -> spinSpeed;
	newPerson -> r = 0;
	newPerson -> g = 0;
	newPerson -> b = 0;
	newPerson -> colourmix = 0;
	newPerson -> transparency = 0;
	newPerson -> myPersona = p;
	retainCostume (p);

	setFrames (* newPerson, ANI_STAND);

	// HEIGHT (BASED ON 1st FRAME OF 1st ANIMATION... INC. SPECIAL CASES)
	const personaAnimation & first = * p -> animation[0];
	int fNumSigned = first.frame (0).sprite;
	int fNum = abs (fNumSigned);
	const spriteBank & bank = first.sprites () -> bank;
	if (fNum >= bank.total) {
		if (fNumSigned < 0) {
			newPerson -> height = 5;
		} else {
			newPerson -> height = bank.sprites[0].yhot + 5;
		}
	} else {
		newPerson -> height = bank.sprites[fNum].yhot + 5;
	}

	// NOW ADD IT IN THE RIGHT PLACE
	onScreenPerson * * changethat = & allPeople;

	while (((* changethat) != NULL) && ((* changethat) -> y < y))
		changethat = & ((* changethat) -> next);

	newPerson -> next = (* changethat);
	(* changethat) = newPerson;

	return (bool) (newPerson -> thisType != NULL);
}

int timeForAnim (const personaAnimation * fram) {
	int total = 0;
	for (int a = 0; a < fram -> numFrames (); a ++) {
		total += fram -> frame (a).ticks;
	}
	return total;
}

void animatePerson (int obj, AnimationHandle fram) {	// Set a new SINGLE animation
	onScreenPerson * moveMe = findPerson (obj);
	if (moveMe) {
		if (moveMe -> continueAfterWalking) abortFunction (moveMe -> continueAfterWalking);
		moveMe -> continueAfterWalking = NULL;
		moveMe -> walking = false;
		moveMe -> spinning = false;
		givePersonAnim (* moveMe, std::move (fram));
	}
	// Without such a character, fram goes here.
}

void animatePerson (int obj, persona * per) {			// Set a new costume
	onScreenPerson * moveMe = findPerson (obj);
	if (moveMe) {
	//	if (moveMe -> continueAfterWalking) abortFunction (moveMe -> continueAfterWalking);
	//	moveMe -> continueAfterWalking = NULL;
	//	moveMe -> walking = false;
		moveMe -> spinning = false;
		// The same costume again keeps its count.
		retainCostume (per);
		persona * old = moveMe -> myPersona;
		moveMe -> myPersona = per;
		rethinkAngle (moveMe);
		if (moveMe-> walking) {
			setFrames (* moveMe, ANI_WALK);
		} else {
			setFrames (* moveMe, ANI_STAND);
		}
		if (old != per) forgetCostume (* moveMe, old);
		releaseCostume (old);
	}
}

void killAllPeople () {
	onScreenPerson * killPeople;
	while (allPeople) {
		if (allPeople -> continueAfterWalking) abortFunction (allPeople -> continueAfterWalking);
		allPeople -> continueAfterWalking = NULL;
		killPeople = allPeople;
		allPeople = allPeople -> next;
		removeObjectType (killPeople -> thisType);
		deletePerson (killPeople);
	}
}

void killMostPeople () {
	onScreenPerson * killPeople;
	onScreenPerson * * lookyHere = & allPeople;

	while (* lookyHere) {
		if ((* lookyHere) -> extra & EXTRA_NOREMOVE) {
			lookyHere = & (* lookyHere) -> next;
		} else {
			killPeople = (* lookyHere);

			// Change last pointer to NEXT in the list instead
			(* lookyHere) = killPeople -> next;

			// Gone from the list... now free some memory
			if (killPeople -> continueAfterWalking) abortFunction (killPeople -> continueAfterWalking);
			killPeople -> continueAfterWalking = NULL;
			removeObjectType (killPeople -> thisType);
			deletePerson (killPeople);
		}
	}
}

void removeOneCharacter (int i) {
	onScreenPerson * p = findPerson (i);

	if (p) {
		if (overRegion == &personRegion && overRegion->thisType == p->thisType) {
			overRegion = NULL;
		}

		if (p -> continueAfterWalking) abortFunction (p -> continueAfterWalking);
		p -> continueAfterWalking = NULL;
		onScreenPerson * * killPeople;

		for (killPeople = & allPeople;
			* killPeople != p;
			killPeople = & ((* killPeople) -> next)) {;}

		* killPeople = p -> next;
		removeObjectType (p -> thisType);
		deletePerson (p);
	}
}

bool saveAnim (const personaAnimation * p, FILE * fp) {
	int n = p -> numFrames ();
	put2bytes (n, fp);
	if (n) {
		put4bytes (p -> sprites () -> ID, fp);

		for (int a = 0; a < n; a ++) {
			const AnimationFrame & f = p -> frame (a);
			put4bytes (f.sprite, fp);
			put4bytes (f.ticks, fp);
			put4bytes (f.event.toNoise (), fp);
		}
	}
	return true;
}

bool loadAnim (personaAnimation * p, FILE * fp) {
	int n = get2bytes (fp);
	// At the end of a cut-off save, get2bytes gives -257.
	if (n < 0 || feof (fp)) return false;

	if (n) {
		std::vector<AnimationFrame> frames (n);
		int bankFile = get4bytes (fp);

		for (int a = 0; a < n; a ++) {
			frames[a].sprite = get4bytes (fp);
			frames[a].ticks = get4bytes (fp);
			frames[a].event = FrameEvent::fromNoise (ssgVersion >= VERSION(2,0) ? get4bytes (fp) : 0);
		}
		// Not a bank of a number read past the end of a cut-off save.
		if (feof (fp)) return false;
		p -> definition = makeAnimation (loadBankForAnim (bankFile), frames);
	} else {
		p -> definition.reset ();
	}
	return true;
}
/*
void debugCostume (char * message, persona * cossy) {
	FILE * db = fopen ("debuTURN.txt", "at");
	fprintf (db, "  %s costume with %i directions...\n", message, cossy -> numDirections);
	for (int a = 0; a < cossy -> numDirections * 3; a ++) {
		fprintf (db, "      %i frames:", cossy -> animation[a] -> numFrames);
		for (int b = 0; b < cossy -> animation[a] -> numFrames; b ++) {
			fprintf (db, " %i", cossy -> animation[a] -> frames[b]);
		}
		fprintf (db, "\n");

	}
	fclose (db);
}
*/
bool saveCostume (persona * cossy, FILE * fp) {
	int a;
	put2bytes (cossy -> numDirections, fp);
	for (a = 0; a < cossy -> numDirections * 3; a ++) {
		if (! saveAnim (cossy -> animation[a].get (), fp)) return false;
	}
//	debugCostume ("Saved", cossy);
	return true;
}

bool loadCostume (persona * cossy, FILE * fp) {
	int a;
	int directions = get2bytes (fp);
	// At the end of a cut-off save, get2bytes gives -257.
	if (directions < 0 || feof (fp)) return false;
	cossy -> numDirections = directions;
	cossy -> animation.clear ();
	cossy -> animation.resize (cossy -> numDirections * 3);
	for (a = 0; a < cossy -> numDirections * 3; a ++) {
		cossy -> animation[a].reset (new personaAnimation);
		if (! loadAnim (cossy -> animation[a].get (), fp)) return false;
	}
//	debugCostume ("Loaded", cossy);
	return true;
}

bool savePeople (FILE * fp) {
	onScreenPerson * me = allPeople;
	int countPeople = 0, a;

	putSigned (scaleHorizon, fp);
	putSigned (scaleDivide, fp);

	while (me) {
		countPeople ++;
		me = me -> next;
	}

	put2bytes (countPeople, fp);

	me = allPeople;
	for (a = 0; a < countPeople; a ++) {

		putFloat (me -> x, fp);
		putFloat (me -> y, fp);

		saveCostume (me -> myPersona, fp);
		saveAnim (me -> myAnim, fp);
		fputc (me -> myAnim == me -> playback.playing, fp);

		putFloat (me -> scale, fp);

		put2bytes (me -> extra, fp);
		put2bytes (me -> height, fp);
		put2bytes (me -> walkToX, fp);
		put2bytes (me -> walkToY, fp);
		put2bytes (me -> thisStepX, fp);
		put2bytes (me -> thisStepY, fp);
		put2bytes (me -> playback.frame, fp);
		put2bytes (me -> playback.ticksLeft, fp);
		put2bytes (me -> walkSpeed, fp);
		put2bytes (me -> spinSpeed, fp);
		putSigned (me -> floaty, fp);
		fputc (me -> show, fp);
		fputc (me -> walking, fp);
		fputc (me -> spinning, fp);
		if (me -> continueAfterWalking) {
			fputc (1, fp);
			saveFunction (me -> continueAfterWalking, fp);
		} else {
			fputc (0, fp);
		}
		put2bytes (me -> direction, fp);
		put2bytes (me -> angle, fp);
		put2bytes (me -> angleOffset, fp);
		put2bytes (me -> wantAngle, fp);
		putSigned (me -> directionWhenDoneWalking, fp);
		putSigned (me -> inPoly, fp);
		putSigned (me -> walkToPoly, fp);

		fputc (me -> r, fp);
		fputc (me -> g, fp);
		fputc (me -> b, fp);
		fputc (me -> colourmix, fp);
		fputc (me -> transparency, fp);
		
		saveObjectRef (me -> thisType, fp);

		me = me -> next;
	}
	return true;
}

bool loadPeople (FILE * fp) {
	onScreenPerson * * pointy = & allPeople;
	onScreenPerson * me;

	scaleHorizon = getSigned (fp);
	scaleDivide = getSigned (fp);

	int countPeople = get2bytes (fp);
	int a;

	allPeople = NULL;
	for (a = 0; a < countPeople; a ++) {
		me = new onScreenPerson;
		if (! checkNew (me)) return false;
		// What deletePerson reads, before anything can fail.
		me -> myPersona = NULL;
		me -> myAnim = NULL;
		me -> continueAfterWalking = NULL;
		PersonLoadGuard guard (me);

		me -> myPersona = new persona;
		if (! checkNew (me -> myPersona)) return false;
		me -> myPersona -> timesUsed = 1;

		// A copy of its own, even when it was an animation of the costume.
		givePersonAnim (* me, AnimationHandle (new personaAnimation));

		me -> x = getFloat (fp);
		me -> y = getFloat (fp);

		if (! loadCostume (me -> myPersona, fp)) return false;
		if (! loadAnim (me -> myAnim, fp)) return false;

		me -> playback.playing = fgetc (fp) ? me -> myAnim : NULL;

		me -> scale = getFloat (fp);

		me -> extra = get2bytes (fp);
		me -> height = get2bytes (fp);
		me -> walkToX = get2bytes (fp);
		me -> walkToY = get2bytes (fp);
		me -> thisStepX = get2bytes (fp);
		me -> thisStepY = get2bytes (fp);
		me -> playback.frame = get2bytes (fp);
		me -> playback.ticksLeft = get2bytes (fp);
		me -> playback.displayFrame = me -> playback.frame;
		me -> walkSpeed = get2bytes (fp);
		me -> spinSpeed = get2bytes (fp);
		me -> floaty = getSigned (fp);
		me -> show = fgetc (fp);
		me -> walking = fgetc (fp);
		me -> spinning = fgetc (fp);
		if (getFlag (fp)) {
			me -> continueAfterWalking = loadFunction (fp);
			if (! me -> continueAfterWalking) return false;
		} else {
			me -> continueAfterWalking = NULL;
		}
		me -> direction = get2bytes(fp);
		me -> angle = get2bytes(fp);
		if (ssgVersion >= VERSION(2,0)) {
			me -> angleOffset = get2bytes(fp);
		} else {
			me -> angleOffset = 0;
		}
		me -> wantAngle = get2bytes(fp);
		me -> directionWhenDoneWalking = getSigned(fp);
		me -> inPoly = getSigned(fp);
		me -> walkToPoly = getSigned(fp);
		if (ssgVersion >= VERSION(2,0)) {
			me -> r = fgetc (fp);
			me -> g = fgetc (fp);
			me -> b = fgetc (fp);
			me -> colourmix = fgetc (fp);
			me -> transparency = fgetc (fp);
		} else {
			setMyDrawMode(me, get2bytes(fp));
		}
		me -> thisType = loadObjectRef (fp);

		// Anti-aliasing settings
		if (ssgVersion >= VERSION(1,6))
		{
			if (ssgVersion < VERSION (2,0)) {
				// aaLoad
				fgetc (fp);
				getFloat (fp);
				getFloat (fp);
			}
		}

		me -> next = NULL;
		* pointy = guard.release ();
		pointy = & (me -> next);
	}
//	db ("End of loadPeople");
	return true;
}
