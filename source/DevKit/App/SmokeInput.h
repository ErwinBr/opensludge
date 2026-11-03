#ifndef SLUDGE_DEVKIT_SMOKE_INPUT_H
#define SLUDGE_DEVKIT_SMOKE_INPUT_H

/*
 * The input of a smoke test (--smoke-input): what the user would do with
 * the mouse and the keys, one step for each frame, so that a test can use
 * the editors as the user does. A file has a step on each line:
 *
 *   move <x> <y>     the mouse to a point of the window
 *   down [ctrl], up  the left mouse button, with Ctrl held until up
 *   key <name>       press and release a key, such as 2, Escape or ctrl+z
 *                    (Ctrl is Cmd on macOS, as in the shortcuts)
 *   type <text>      type text, as into a field
 *   wait <frames>    do nothing for some frames
 *   new <kind>       make a new document, such as a Z-Buffer
 *   open <file>      open a file, or drop it on the active document
 *   save <file>      save the active document as file
 *
 * Lines that start with # are comments.
 */

#include <string>
#include <vector>

namespace devkit {

class Workspace;

class SmokeInput {
public:
	SmokeInput () : next (0), waiting (0), keyUp (0), ctrlUp (false), ctrlHeld (false) {}

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
	bool ctrlUp;	// Whether to release Ctrl then too
	bool ctrlHeld;	// Ctrl held from down to up
};

} // namespace devkit

#endif
