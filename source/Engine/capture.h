#ifndef SLUDGE_CAPTURE_H
#define SLUDGE_CAPTURE_H

/*
 * Deterministic runs for testing the engine.
 *
 * With --capture, --input or --max-frames, the engine runs a game the same
 * way every time: the clock advances by exactly one frame per frame, the
 * random numbers start from a fixed seed, there is no sound, and the mouse
 * and keyboard events come from an input script instead of the player. We
 * write a CRC of every frame to frames.tsv in the capture folder, and the
 * frames chosen with --frames as PNG files, so that we can compare the
 * drawing of two builds of the engine.
 *
 * With --record-input, the player plays as usual, without sound, and we
 * write the mouse and keyboard events to an input script that --input can
 * play back.
 *
 * An input script has one command per line, after the number of the frame
 * in which it runs. Frames count from 0, and positions are in game pixels.
 *
 *   120 move 320 240      Move the mouse.
 *   121 down left         Press a mouse button (left or right).
 *   122 up left           Release a mouse button.
 *   130 click 100 50      Move, press the left button, release it a frame later.
 *   300 key Return        Press a key, by its SDL name (Escape, Left, F1...).
 *   400 text hello        Type text, one character in each frame.
 *   500 capture           Write this frame as a PNG file.
 *   900 dump backdrop     Write the backdrop as a PNG file.
 *   1200 quit             Stop the engine.
 *
 * Empty lines and lines starting with # are ignored.
 */

#include <SDL3/SDL.h>

// Take the capture options out of argv, so that the platform code parses
// only the other options. Returns false after printing an error.
bool parseCaptureOptions (int & argc, char ** argv);
void printCaptureUsage ();

// True for a deterministic run: --capture, --input or --max-frames.
bool captureIsDeterministic ();
// True for a deterministic run and for --record-input. These runs are
// silent and use a fixed seed, so that recordings play back the same way.
bool captureIsActive ();
unsigned int captureSeed ();
// With --pot-textures, we make every texture a power of two in size, as on
// OpenGL ES 2.0 devices, to test that code on other computers.
bool captureForcePOTTextures ();
// The renderer to draw with: "auto", "gpu" or "gl", from --renderer, or
// else from the environment variable SLUDGE_RENDERER, or else from RENDERER
// in the settings file of the game. "auto" when none of them says.
const char * requestedRenderer ();

// The engine clock. In a deterministic run it only advances in
// engineDelayMS, so it does not depend on how fast the computer is.
Uint64 engineTicksNS ();
Uint64 engineTicksMS ();
void engineDelayMS (Uint32 ms);

// If the run has its own folder for settings and saved games, change to it
// and return true.
bool captureChangeToUserDir ();

// Call at the start of each frame of the main loop, before checkInput.
void captureStartFrame ();
// Call from checkInput for each event, to record it with --record-input.
void captureRecordEvent (const SDL_Event & event);
// Call when a frame of the main loop is drawn, before showing it.
void captureFrameDrawn ();
// Call at the end of each frame of the main loop. Returns true when the
// run should stop.
bool captureEndFrame ();
// Call when the engine stops.
void captureFinish ();

#endif
