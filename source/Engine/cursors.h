// The mouse cursor owns its animation (from setCursor), and plays it.
#include <stdint.h>
#include "ownedanimation.h"

typedef int64_t AnimationTime;	// As animmodel.h has it

void pickAnimCursor (AnimationHandle pp);
// For freezing, loading and saving: take the cursor's animation away (it is
// then the caller's), give it one with a frame, or look at it.
AnimationHandle takeCursorAnim ();
// The cursor's animation again, at frame, or for a TIMELINE clip at position.
void setCursorAnim (AnimationHandle pp, int frame, AnimationTime position = 0);
AnimationTime cursorPosition ();
const struct personaAnimation * cursorAnimation ();
int cursorFrame ();
// Move the cursor's animation on by ticks game ticks. Once a tick, before
// drawing.
void updateMouseCursor (int ticks);
// Draw the frame of the mouse cursor that the last update chose, at its
// position. Drawing doesn't move the animation on.
void displayCursor ();
// Draw the current frame of the mouse cursor at a position in game
// coordinates (between game frames, and in the controller settings).
void drawCursor (int x, int y);
bool hasCursorSprite ();
// The outline around the cursor (setCursorOutline): a thickness of 0 for
// none. Saved games keep it; freezing doesn't change it.
void setCursorOutline (int thickness, unsigned char r, unsigned char g, unsigned char b, unsigned char a);
const struct OutlineStyle & cursorOutlineStyle ();
void pasteCursor (int x, int y, const struct personaAnimation & c);
