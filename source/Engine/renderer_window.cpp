/*
 * The window and its renderer (renderer_window.h).
 */

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "sludge_gl.h"
#include "renderer.h"
#include "renderer_window.h"
#include "dxgipresent.h"

#if defined(SLUDGE_FRAMEBUFFER_VSYNC)
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, __u32)
#endif
#endif

namespace gfx {

namespace {

SDL_Window * window = NULL;
Renderer * renderer = NULL;
SDL_GLContext glContext = NULL;
// We draw with SDL_GPU instead of OpenGL.
bool usingGPU = false;

#if defined(SLUDGE_RENDERER_GPU)
SDL_GPUDevice * gpuDevice = NULL;

// Make the window and the SDL_GPU renderer. Returns false, and leaves
// nothing behind, when SDL_GPU can't draw in this window.
bool createGPUWindow (const WindowOptions & options) {
	gpuDevice = SDL_CreateGPUDevice (SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_DXIL, options.debug, NULL);
	if (! gpuDevice) {
		fprintf (stderr, "SDL_GPU is not available: %s\n", SDL_GetError ());
		return false;
	}
	// On macOS, SDL makes an OpenGL window unless we ask for another kind,
	// and that fails without OpenGL.
	SDL_WindowFlags flags = options.flags;
	if (! strcmp (SDL_GetGPUDeviceDriver (gpuDevice), "metal")) flags |= SDL_WINDOW_METAL;
	window = SDL_CreateWindow (options.title, options.width, options.height, flags);
	const char * error = NULL;
	if (window && SDL_ClaimWindowForGPUDevice (gpuDevice, window)) {
		if (options.immediate && SDL_WindowSupportsGPUPresentMode (gpuDevice, window, SDL_GPU_PRESENTMODE_IMMEDIATE)) {
			SDL_SetGPUSwapchainParameters (gpuDevice, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_IMMEDIATE);
		}
		renderer = createGPURenderer (gpuDevice, window, &error);
		if (renderer) {
			usingGPU = true;
			fprintf (stderr, "Renderer: SDL_GPU (%s)\n", SDL_GetGPUDeviceDriver (gpuDevice));
			return true;
		}
		fprintf (stderr, "Can't make the SDL_GPU renderer: %s\n", error);
		SDL_ReleaseWindowFromGPUDevice (gpuDevice, window);
	} else {
		fprintf (stderr, "Can't draw with SDL_GPU in the window: %s\n", SDL_GetError ());
	}
	if (window) SDL_DestroyWindow (window);
	window = NULL;
	SDL_DestroyGPUDevice (gpuDevice);
	gpuDevice = NULL;
	return false;
}
#endif

// Why OpenGL couldn't draw, for the message when no renderer can.
WindowProblem glProblem;

bool glFailed (const char * problem, const char * detail, int exitCode) {
	glProblem.message = problem;
	SDL_strlcpy (glProblem.detail, detail ? detail : "", sizeof (glProblem.detail));
	glProblem.exitCode = exitCode;
	fprintf (stderr, "%s %s\n", problem, glProblem.detail);
#if defined(_WIN32) && ! defined(HAVE_GLES2)
	if (presentingWithDXGI ()) {
		stopDXGIPresentation ();
		glContext = NULL;
	}
#endif
	if (glContext) SDL_GL_DestroyContext (glContext);
	glContext = NULL;
	if (window) SDL_DestroyWindow (window);
	window = NULL;
	return false;
}

// Make the window and the OpenGL renderer. Returns false, and leaves
// nothing behind, when OpenGL can't draw in this window.
bool createGLWindow (const WindowOptions & options) {
	/*
	 * We want *at least* 8 bits of red, green, blue and alpha, a 16-bit
	 * depth buffer and a double buffered window.
	 */
	SDL_GL_SetAttribute (SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_DEPTH_SIZE, 16);
	SDL_GL_SetAttribute (SDL_GL_DOUBLEBUFFER, 1);
#if defined(HAVE_GLES2)
	SDL_GL_SetAttribute (SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute (SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute (SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
	// The shaders are in GLSL 1.10, which is only available in a
	// compatibility context. On macOS, that is an OpenGL 2.1 context.
	SDL_GL_SetAttribute (SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif

#if defined(_WIN32) && ! defined(HAVE_GLES2)
	// On Windows, we show the frames through DXGI where we can, in a
	// window without OpenGL (see dxgipresent.cpp).
	window = SDL_CreateWindow (options.title, options.width, options.height, options.flags);
	if (window) {
		glContext = startDXGIPresentation (window);
		if (! glContext) {
			SDL_DestroyWindow (window);
			window = NULL;
		}
	}
#endif

	if (! window) {
		window = SDL_CreateWindow (options.title, options.width, options.height, options.flags | SDL_WINDOW_OPENGL);
		if (! window) return glFailed ("Startup Error: Couldn't create the game window.", SDL_GetError (), 2);
		glContext = SDL_GL_CreateContext (window);
		if (! glContext) return glFailed ("Startup Error: Couldn't create an OpenGL context.", SDL_GetError (), 2);
		SDL_GL_MakeCurrent (window, glContext);
		SDL_GL_SetSwapInterval (options.immediate ? 0 : 1);
	}

	const char * missing = loadGLFunctions ();
	if (missing) return glFailed ("Startup Error: Your graphics driver doesn't support OpenGL 2.0.", missing, 1);

	const char * error = NULL;
	renderer = createGLRenderer (swapWindowBuffers, &error);
	if (! renderer) return glFailed ("Startup Error: Couldn't build the shader programs.", error, 1);

	fprintf (stderr, "Renderer: OpenGL\n");
	logDebug ("OpenGL vendor: %s\n", glGetString (GL_VENDOR));
	logDebug ("OpenGL renderer: %s\n", glGetString (GL_RENDERER));
	logDebug ("OpenGL version: %s\n", glGetString (GL_VERSION));
	return true;
}

#if defined(SLUDGE_FRAMEBUFFER_VSYNC)
// Wait for the next vertical sync of the framebuffer. Without /dev/fb0, we
// do not wait.
void waitForFramebufferVSync () {
	static int framebuffer = open ("/dev/fb0", O_RDONLY);
	if (framebuffer < 0) return;
	int arg = 0;
	ioctl (framebuffer, FBIO_WAITFORVSYNC, &arg);
}
#endif

} // namespace

Renderer * createWindowRenderer (const WindowOptions & options, SDL_Window ** result, WindowProblem * problem) {
	// With auto, we draw with SDL_GPU: with Metal, Direct3D 12 or Vulkan.
	// When one renderer can't draw, we try the other.
	const char * choice = options.renderer ? options.renderer : "auto";
	bool gpuFirst = true;
	if (! strcmp (choice, "gl")) {
		gpuFirst = false;
	} else if (strcmp (choice, "gpu") && strcmp (choice, "auto")) {
		fprintf (stderr, "Unknown renderer \"%s\", so we choose one\n", choice);
	}

	glProblem.message = NULL;
	glProblem.detail[0] = 0;
	glProblem.exitCode = 0;
#if defined(SLUDGE_RENDERER_GPU)
	bool ok = gpuFirst && createGPUWindow (options);
	if (! ok) ok = createGLWindow (options);
	if (! ok && ! gpuFirst) ok = createGPUWindow (options);
#else
	if (! strcmp (choice, "gpu")) fprintf (stderr, "This was built without SDL_GPU\n");
	bool ok = createGLWindow (options);
#endif
	if (! ok) {
		*problem = glProblem;
		*result = NULL;
		return NULL;
	}
	*result = window;
	return renderer;
}

void destroyWindowRenderer () {
	delete renderer;
	renderer = NULL;
#if defined(SLUDGE_RENDERER_GPU)
	if (gpuDevice) {
		SDL_ReleaseWindowFromGPUDevice (gpuDevice, window);
		SDL_DestroyGPUDevice (gpuDevice);
		gpuDevice = NULL;
	}
#endif
#if defined(_WIN32) && ! defined(HAVE_GLES2)
	if (presentingWithDXGI ()) {
		stopDXGIPresentation ();
		glContext = NULL;
	}
#endif
	if (glContext) SDL_GL_DestroyContext (glContext);
	glContext = NULL;
	if (window) SDL_DestroyWindow (window);
	window = NULL;
	usingGPU = false;
}

bool windowRendererUsesGPU () {
	return usingGPU;
}

const char * windowRendererName () {
	static char name[64];
#if defined(SLUDGE_RENDERER_GPU)
	if (usingGPU) {
		SDL_snprintf (name, sizeof (name), "SDL_GPU (%s)", SDL_GetGPUDeviceDriver (gpuDevice));
		return name;
	}
#endif
	return renderer ? "OpenGL" : "none";
}

void windowResized (int width, int height) {
#if defined(_WIN32) && ! defined(HAVE_GLES2)
	resizeDXGIPresentation (width, height);
#else
	(void) width;
	(void) height;
#endif
}

void swapWindowBuffers () {
#if defined(SLUDGE_FRAMEBUFFER_VSYNC)
	waitForFramebufferVSync ();
#endif
#if defined(_WIN32) && ! defined(HAVE_GLES2)
	if (presentingWithDXGI ()) {
		presentWithDXGI ();
		return;
	}
#endif
	SDL_GL_SwapWindow (window);
}

} // namespace gfx
