#ifndef SLUDGE_DEVKIT_ICONS_H
#define SLUDGE_DEVKIT_ICONS_H

/*
 * Icons of the Dev Kit's interface, drawn from SVG (Icons.cpp) in the
 * colour of the text, so that they follow the theme. Each is made into a
 * texture the first time it is drawn at a size, at the pixels of the
 * screen, and made again when that size changes.
 */

#include "imgui.h"

namespace devkit {

enum Icon {
	ICON_LOCKED,
	ICON_UNLOCKED,
	ICON_KEY,
	ICON_AUTOKEY,
	ICON_PLAY,
	ICON_PAUSE,
	ICON_FIRST,
	ICON_LAST,
	ICON_LOOP,
	ICON_PLUS,
	ICON_MINUS,
	NUM_ICONS
};

// An icon as high as the text, where the cursor is.
void icon (Icon which, const ImVec4 * colour = nullptr);
// A button of an icon, square, as high as a framed field unless side says
// otherwise, without a frame unless it is hovered (or on): true when
// clicked. In the colour of the text, or colour; disabled, it can't be
// clicked (and its tooltip still shows). on gives it the frame of a button
// that is pressed, for a toggle.
struct IconStyle {
	const ImVec4 * colour;
	bool enabled;
	bool on;
	IconStyle () : colour (nullptr), enabled (true), on (false) {}
};
bool iconButton (const char * id, Icon which, const char * tooltip = nullptr, float side = 0, const IconStyle & style = IconStyle ());
// The width of iconButton, as high as a framed field.
float iconButtonWidth ();
// Forget the textures, before the renderer goes.
void forgetIcons ();

} // namespace devkit

#endif
