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

void setCursorAnim (AnimationHandle pp, int frame) {
	cursorAnim = std::move (pp);
	cursorPlayback.frame = frame;
	cursorPlayback.displayFrame = frame;
}

const personaAnimation * cursorAnimation () {
	return cursorAnim.get ();
}

int cursorFrame () {
	return cursorPlayback.frame;
}

bool hasCursorSprite () {
	return cursorAnim && cursorAnim -> numFrames ();
}

static void drawCursorFrame (int x, int y, int frame) {
	if (! hasCursorSprite ()) return;
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
	drawCursorFrame (x, y, cursorPlayback.frame);
}

void updateMouseCursor (int ticks) {
	updateCursorAnimation (cursorPlayback, cursorAnim.get (), ticks);
}

void displayCursor () {
	drawCursorFrame (input.mouseX, input.mouseY, cursorPlayback.displayFrame);
}

void pasteCursor (int x, int y, const personaAnimation & c) {
	if (c.numFrames ()) {
		const spriteBank & bank = c.sprites () -> bank;
		pasteSpriteToBackDrop (x, y, bank.sprites[c.frame (0).sprite], bank, bank.myPalette);
	}
}
