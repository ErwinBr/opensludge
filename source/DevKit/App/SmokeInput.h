#ifndef SLUDGE_DEVKIT_SMOKE_INPUT_H
#define SLUDGE_DEVKIT_SMOKE_INPUT_H

/*
 * The input of a smoke test (--smoke-input): what the user would do with
 * the mouse and the keys, one step for each frame, so that a test can use
 * the editors as the user does. A file has a step on each line:
 *
 *   move <x> <y>     the mouse to a point of the window
 *   down [ctrl|shift], up
 *                    the left mouse button, with Ctrl or Shift held until up
 *   hold ctrl|shift  hold a modifier down, without the mouse, until release
 *   release          let go of it
 *   pointer arrow|text|hand
 *                    fail unless the last frame made the pointer that one
 *   key <name>       press and release a key, such as 2, Escape, ctrl+z or
 *                    shift+Tab, with ctrl+, shift+ and alt+ held with it
 *                    (Ctrl is Cmd on macOS, as in the shortcuts)
 *   type <text>      type text, as into a field
 *   wait <frames>    do nothing for some frames
 *   new <kind>       make a new document, such as a Z-Buffer
 *   open <file>      open a file, or drop it on the active document
 *   drop <file>      drop a file on the window, which the active editor may
 *                    take even if the Dev Kit opens it
 *   save <file>      save the active document as file
 *   close            close the tab of the active document, as its X does
 *   sheet <file>     open the Import Sprite Sheet dialog of the active
 *                    sprite bank with a picture, as if it had been chosen
 *   documents <n>    fail unless n documents are open
 *   cursor <file> <line> <column>
 *                    fail unless the active document is the script of that
 *                    name, with its selection starting there (from 1)
 *
 * Lines that start with # are comments.
 */

#include <string>
#include <vector>

namespace devkit {

class Workspace;

class SmokeInput {
public:
	SmokeInput () : next (0), waiting (0), keyUp (0), modifiersUp (0), heldKey (0) {}

	// Returns false, with the reason in *error, when we can't read it.
	bool read (const std::string & file, std::string * error);

	// Whether there are steps left.
	bool running () const { return next < steps.size () || waiting > 0; }
	// Give ImGui the input of the next frame, after the step before it.
	// Returns false when a step failed.
	bool step (Workspace & workspace);

private:
	struct Step {
		std::string what, text;
		float x, y;
		int frames;
	};
	std::vector<Step> steps;
	size_t next;
	int waiting;
	int keyUp;		// The key to release in the next frame, or 0
	int modifiersUp;	// The modifiers to release then too (ImGuiMod_ keys, as bits)
	int heldKey;	// The modifier held from down to up, or 0
};

} // namespace devkit

#endif
