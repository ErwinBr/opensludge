#include <memory>
#include <vector>

#include "animation.h"

#define EXTRA_FRONT			1
#define EXTRA_FIXEDSIZE		2
#define EXTRA_NOSCALE		2	// Alternative name
#define EXTRA_NOZB			4
#define EXTRA_FIXTOSCREEN	8
#define EXTRA_NOLITE		16
#define EXTRA_NOREMOVE		32
#define EXTRA_RECTANGULAR	64

// A costume: 3 animations (stand, walk, talk) for each direction, which it
// owns. Variables and characters share costumes, and count them in timesUsed
// (see retainCostume and releaseCostume): the script layer of ownership,
// apart from the shared_ptr of animation definitions and sprite banks.
struct persona {
	std::vector<std::unique_ptr<personaAnimation> > animation;	// numDirections * 3
	int numDirections;
	int timesUsed;

	LifetimeCounter<persona> counted;

	persona () : numDirections (0), timesUsed (0) {}
};

struct onScreenPerson {
	float x, y;
	int height, floaty, walkSpeed;
	float scale;
	onScreenPerson * next;
	int walkToX, walkToY, thisStepX, thisStepY, inPoly, walkToPoly;
	bool walking, spinning;
	struct loadedFunction * continueAfterWalking;
	// The animation the character shows: one of its costume, or ownAnim.
	// Doesn't own it.
	personaAnimation * myAnim;
	// An animation the character owns (from animate or a saved game), or NULL.
	std::unique_ptr<personaAnimation> ownAnim;
	// Where it is in myAnim. updatePeopleAnimations moves it on; drawing
	// only reads it.
	AnimationPlayback playback;
	persona * myPersona;
	int angle, wantAngle, angleOffset;
	bool show;
	int direction, directionWhenDoneWalking;
	struct objectType * thisType;
	int extra, spinSpeed;
	unsigned char r,g,b,colourmix,transparency;
	// A number of its own, from addCharacter, which orders the events of
	// TIMELINE clips: the order of drawing never decides when or whether an
	// event fires. Saved games keep it.
	unsigned sequence;
	LifetimeCounter<onScreenPerson> counted;
};

// Initialisation and creation

bool initPeople ();
bool addPerson (int x, int y, int objNum, persona * p);

// Move every character's animation on by ticks game ticks, firing their
// events. Once a tick, before drawing. First the legacy pass: animations of
// anim (), in the order in which characters are drawn, firing their events
// at once, as the engine always did. Then the timeline pass: TIMELINE clips
// of every character, shown or not, a tick at a time; each tick's events
// fire after all have moved on, in order of character sequence, layer,
// position and event ID: their functions run in that order.
void updatePeopleAnimations (int ticks);
// The legacy extra tick of freezing and snapshots, which moves on only the
// animations of anim (): the timeline never depends on drawing.
void stepLegacyAnimations ();

// Draw to screen and to backdrop. Drawing doesn't change the animations.

void drawPeople ();
void freezePeople (int, int);

// Removalisationisms

void killAllPeople ();
void killMostPeople ();
void removeOneCharacter (int i);

// Things which affect or use all characters

onScreenPerson * findPerson (int v);
void setScale (short int h, short int d);

// Things which affect one character

void makeTalker (onScreenPerson & me);
void makeSilent (onScreenPerson & me);
void setShown (bool h, int ob);
void setDrawMode (int h, int ob);
void setPersonTransparency (int ob, unsigned char x);
void setPersonColourise (int ob, unsigned char r, unsigned char g, unsigned char b, unsigned char colourmix);

// Moving 'em

void movePerson (int x, int y, int objNum);
bool makeWalkingPerson (int x, int y, int objNum, struct loadedFunction * func, int di);
bool forceWalkingPerson (int x, int y, int objNum, struct loadedFunction * func, int di);
void jumpPerson (int x, int y, int objNum);
void walkAllPeople ();
bool turnPersonToFace (int thisNum, int direc);
bool stopPerson (int o);
bool floatCharacter (int f, int objNum);
bool setCharacterWalkSpeed (int f, int objNum);

// Animating 'em

// Takes ownership of the animation, and deletes it when there is no such character.
void animatePerson (int obj, AnimationHandle anim);
void animatePerson (int obj, persona * per);
// The frames of anim (), from the stack.
std::vector<AnimationFrame> readAnimationFrames (int num, struct variableStack * & stacky);
bool setPersonExtra (int f, int newSetting);
int timeForAnim (const personaAnimation * fram);
// A new handle on the same definition: no frames are copied.
AnimationHandle copyAnim (const personaAnimation * orig);
// A handle on no animation (anim 0 in scripts).
AnimationHandle makeNullAnim ();

// Loading and saving

bool saveAnim (const personaAnimation * p, FILE * fp);
bool loadAnim (personaAnimation * p, FILE * fp);
bool savePeople (FILE * fp);
bool loadPeople (FILE * fp);
bool saveCostume (persona * cossy, FILE * fp);
bool loadCostume (persona * cossy, FILE * fp);

// Costumes are counted: each variable and character that holds one has a
// reference. The last release deletes the costume and its animations.
void retainCostume (persona * cossy);
void releaseCostume (persona * cossy);

// Holds a reference to a costume until it goes, for the built-in functions.
class CostumeRef {
public:
	explicit CostumeRef (persona * p) : p (p) {}
	~CostumeRef () { releaseCostume (p); }
	persona * get () const { return p; }
private:
	persona * p;
	CostumeRef (const CostumeRef &);
	CostumeRef & operator= (const CostumeRef &);
};

// With SLUDGE_LIFETIME_LOG set, write how many animation handles,
// definitions, costumes, characters and sprite banks are alive, on stderr.
void logLifetimes (const char * when);

// Show an animation of the character's costume (which it doesn't own), or
// give it an animation of its own. Either frees the one it owned before.
void setPersonAnim (onScreenPerson & person, personaAnimation * anim);
void givePersonAnim (onScreenPerson & person, AnimationHandle anim);
