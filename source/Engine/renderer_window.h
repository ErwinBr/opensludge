#ifndef SLUDGE_RENDERER_WINDOW_H
#define SLUDGE_RENDERER_WINDOW_H

/*
 * A window with a renderer that draws in it, for the engine and the Dev
 * Kit. We try SDL_GPU (Metal, Vulkan, Direct3D 12) and OpenGL, and fall
 * back to the other when one can't draw. There is one such window at a
 * time.
 */

#include <SDL3/SDL_video.h>

namespace gfx {

class Renderer;

struct WindowOptions {
	const char * title;
	int width, height;
	SDL_WindowFlags flags;	// Without SDL_WINDOW_OPENGL or SDL_WINDOW_METAL
	const char * renderer;	// "auto" or "gpu" for SDL_GPU first, "gl" for OpenGL first
	bool debug;				// With the debug layer of SDL_GPU
	bool immediate;			// Don't wait for the display, as in test runs

	WindowOptions ()
		: title ("SLUDGE"), width (640), height (480), flags (0), renderer ("auto"), debug (false), immediate (false) {}
};

// Why no renderer could draw.
struct WindowProblem {
	const char * message;
	char detail[256];
	int exitCode;
};

// Make the window and its renderer. Returns the renderer and sets *window,
// or returns NULL, leaves nothing behind and fills in *problem. We always
// say on stderr which renderer draws, for tests and bug reports. Before
// this, set SDL_HINT_GPU_DRIVER to choose the driver of SDL_GPU.
Renderer * createWindowRenderer (const WindowOptions & options, SDL_Window ** window, WindowProblem * problem);
// Destroy the renderer, its device or context, and the window.
void destroyWindowRenderer ();

// Whether the renderer of the window draws with SDL_GPU.
bool windowRendererUsesGPU ();

// Tell the renderer that the window changed size, in pixels.
void windowResized (int width, int height);

// Show the frame that OpenGL drew in the window. The OpenGL renderer calls
// this when it presents.
void swapWindowBuffers ();

} // namespace gfx

#endif
