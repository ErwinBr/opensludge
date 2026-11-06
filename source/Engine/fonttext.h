#include <stdint.h>

bool loadFont (int filenum, const char * charOrder, int);
// No font, as when a saved game had none.
void forgetFont ();
void pasteString (char * theText, int, int, spritePalette &);
void setFontColour (spritePalette & sP, byte r, byte g, byte b);
int stringWidth (char * theText);
int stringLength (char * theText);
void pasteStringToBackdrop (char * theText, int xOff, int y, spritePalette & thePal);
void burnStringToBackdrop (char * theText, int xOff, int y);

bool isInFont (char * theText);
// Return whether the current font of the game has a character.
bool fontHasCharacter (uint32_t c);
