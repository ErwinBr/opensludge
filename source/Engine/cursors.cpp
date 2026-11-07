#include "sludge_gl.h"

#include "allfiles.h"
#include "cursors.h"
#include "colours.h"
#include "sprites.h"
#include "sprbanks.h"
#include "people.h"
#include "sludger.h"

// The mouse cursor's animation, which it owns, and where it is in it.
static AnimationHandle cursorAnim;
static AnimationPlayback cursorPlayback;

extern inputType input;

void pickAnimCursor (AnimationHandle pp) {
	cursorAnim = std::move (pp);
	cursorPlayback = AnimationPlayback ();
}

AnimationHandle takeCursorAnim () {
	return std::move (cursorAnim);
}

void setCursorAnim (AnimationHandle pp, int frame, AnimationTime position) {
	cursorAnim = std::move (pp);
	cursorPlayback.frame = frame;
	cursorPlayback.displayFrame = frame;
	if (isTimeline (cursorAnim.get ())) {
		cursorPlayback.playing = cursorAnim.get ();
		cursorPlayback.position = cursorPlayback.displayPosition = clipTime (* cursorAnim -> definition, position);
	}
}

AnimationTime cursorPosition () {
	return currentTime (cursorPlayback, cursorAnim.get ());
}

const personaAnimation * cursorAnimation () {
	return cursorAnim.get ();
}

int cursorFrame () {
	return cursorPlayback.frame;
}

bool hasCursorSprite () {
	return cursorAnim && (cursorAnim -> numFrames () || isTimeline (cursorAnim.get ()));
}

// A frame of a legacy animation, or the root's sprite at time of a TIMELINE
// clip.
static void drawCursorFrame (int x, int y, int frame, AnimationTime time) {
	if (! hasCursorSprite ()) return;
	if (isTimeline (cursorAnim.get ())) {
		const SpriteRef * r = sampleSprite (* cursorAnim -> definition, "", time);
		const spriteBank * bank;
		const sprite * s = resolveSprite (r, bank);
		if (s) (r -> mirror ? flipFontSprite : fontSprite) (x, y, * s, * bank, bank -> myPalette);
		return;
	}
	if (frame < 0 || frame >= cursorAnim -> numFrames ()) frame = 0;

	int spriteNum = cursorAnim -> frame (frame).sprite;
	int flipMe = 0;
	const spriteBank & bank = cursorAnim -> sprites () -> bank;

	if (spriteNum < 0) {
		spriteNum = -spriteNum;
		flipMe = 1;
		if (spriteNum >= bank.total) spriteNum = 0;
	} else {
		if (spriteNum >= bank.total) flipMe = 2;
	}

	if (flipMe != 2) {
		(flipMe ? flipFontSprite : fontSprite) (x, y, bank.sprites[spriteNum], bank, bank.myPalette);
	}
}

void drawCursor (int x, int y) {
	drawCursorFrame (x, y, cursorPlayback.frame, currentTime (cursorPlayback, cursorAnim.get ()));
}

void updateMouseCursor (int ticks) {
	updateCursorAnimation (cursorPlayback, cursorAnim.get (), ticks);
}

void displayCursor () {
	drawCursorFrame (input.mouseX, input.mouseY, cursorPlayback.displayFrame, shownTime (cursorPlayback, cursorAnim.get ()));
}

void pasteCursor (int x, int y, const personaAnimation & c) {
	if (isTimeline (& c)) {
		const spriteBank * bank;
		const sprite * s = resolveSprite (sampleSprite (* c.definition, "", 0), bank);
		if (s) pasteSpriteToBackDrop (x, y, * s, * bank, bank -> myPalette);
	} else if (c.numFrames ()) {
		const spriteBank & bank = c.sprites () -> bank;
		pasteSpriteToBackDrop (x, y, bank.sprites[c.frame (0).sprite], bank, bank.myPalette);
	}
}
