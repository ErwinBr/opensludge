#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "debug.h"
#include "platform-dependent.h"
#include "specialsettings.h"
#include "graphics.h"
#include "dxgipresent.h"
#include "language.h"
#include "newfatal.h"
#include "sprbanks.h"
#include "zbuffer.h"
#include "backdrop.h"
#include "renderer.h"
#include "movie.h"
#include "stringy.h"
#include "capture.h"

#include "language.h" // for settings

#if defined(SLUDGE_FRAMEBUFFER_VSYNC)
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, __u32)
#endif
#endif

unsigned int winWidth, winHeight;
int viewportHeight, viewportWidth;
int viewportOffsetX = 0, viewportOffsetY = 0;

extern float cameraZoom;

bool NPOT_textures = true;

extern int specialSettings;

void setMovieViewport();

extern gfx::Texture * backdropTex;

GLfloat aPMVMatrix[16];

void sludgeDisplay ();

GLfloat primaryColor[4];
GLfloat secondaryColor[4];

// The frame that we draw, as large as the viewport, which we copy into the
// window. Without NPOT textures, the texture is a power of two larger.
static gfx::Texture * frameTexture = NULL;
static gfx::RenderTarget * frameTarget = NULL;

gfx::DrawUniforms::DrawUniforms ()
	: useLightTexture (false), antialias (false), zBuffer (false), zBufferLayer (0.0f), modulateColor (false) {
	memcpy (matrix, aPMVMatrix, sizeof (matrix));
	memcpy (color, primaryColor, sizeof (color));
	memcpy (secondaryColor, ::secondaryColor, sizeof (secondaryColor));
}

void setPrimaryColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
	primaryColor[0] = r;
	primaryColor[1] = g;
	primaryColor[2] = b;
	primaryColor[3] = a;
}

void setSecondaryColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
	secondaryColor[0] = r;
	secondaryColor[1] = g;
	secondaryColor[2] = b;
	secondaryColor[3] = a;
}

void setGameMatrix (float scale) {
	GLfloat w = (GLfloat) winWidth / (cameraZoom * scale);
	GLfloat h = (GLfloat) winHeight / (cameraZoom * scale);

	const GLfloat bPMVMatrix[] =
	{
	2.0f/w,      .0,   .0,  .0,
	    .0, -2.0f/h,   .0,  .0,
	    .0,      .0, 1.0f,  .0,
	  -1.0,    1.0f,   .0, 1.0f

	};
	for (int i = 0; i < 16; i++)
	{
		aPMVMatrix[i] = bPMVMatrix[i];
	}
}

// This is for swapping settings between rendering to texture or to the screen
void setPixelCoords (bool pixels) {
	if (pixels) {
		if (backdropTex) gfx::renderer->setTextureFilter (backdropTex, gfx::FILTER_NEAREST, gfx::FILTER_NEAREST);

		const GLfloat bPMVMatrix[] =
		{
		2.0f/viewportWidth,                  .0,   .0,  .0,
			        .0, 2.0f/viewportHeight,   .0,  .0,
			        .0,                  .0, 1.0f,  .0,
			      -1.0,               -1.0f,   .0, 1.0f

		};
		for (int i = 0; i < 16; i++)
		{
			aPMVMatrix[i] = bPMVMatrix[i];
		}
	} else {
		if (backdropTex) {
			gfx::Filter filter = gameSettings.antiAlias < 0 ? gfx::FILTER_LINEAR : gfx::FILTER_NEAREST;
			gfx::renderer->setTextureFilter (backdropTex, filter, filter);
		}

		setGameMatrix (1);
	}
}

void drawIntoTile (gfx::RenderTarget * target, int dstX, int dstY, int srcX, int srcY, int width, int height) {
	gfx::renderer->setTarget (target);
	gfx::renderer->setViewport (dstX - srcX, dstY - srcY, viewportWidth, viewportHeight);
	gfx::renderer->setScissor (dstX, dstY, width, height);
	gfx::renderer->clear (true, true);
}

void drawIntoWindow () {
	gfx::renderer->setScissor (0, 0, 0, 0);
	gfx::renderer->setTarget (NULL);
	gfx::renderer->setViewport (viewportOffsetX, viewportOffsetY, viewportWidth, viewportHeight);
}

bool runningFullscreen = false;
extern char * gameName;

void beginFrame () {
	gfx::renderer->setScissor (0, 0, 0, 0);
	gfx::renderer->setTarget (frameTarget);
	gfx::renderer->setViewport (0, 0, viewportWidth, viewportHeight);
}

void drawFrameIntoWindow () {
	drawIntoWindow ();
	gfx::renderer->clear (true, false);

	// We draw the frame pixel for pixel, and keep the projection of the
	// game for what comes next.
	GLfloat gameMatrix[16];
	memcpy (gameMatrix, aPMVMatrix, sizeof (gameMatrix));
	setPixelCoords (true);

	int tw, th;
	gfx::renderer->textureSize (frameTexture, &tw, &th);
	const GLfloat vertices[] = {
		0, 0, 0,
		(GLfloat) viewportWidth, 0, 0,
		0, (GLfloat) viewportHeight, 0,
		(GLfloat) viewportWidth, (GLfloat) viewportHeight, 0
	};
	GLfloat u = (GLfloat) viewportWidth / tw;
	GLfloat v = (GLfloat) viewportHeight / th;
	const GLfloat texCoords[] = {
		0, 0,
		u, 0,
		0, v,
		u, v
	};
	gfx::draw (gfx::PipelineState (gfx::PROG_TEXTURE), gfx::DrawUniforms (), frameTexture, vertices, texCoords);

	setPixelCoords (false);
	memcpy (aPMVMatrix, gameMatrix, sizeof (gameMatrix));
}

// Make the frame texture as large as the viewport.
static void resizeFrame () {
	int width = NPOT_textures ? viewportWidth : getNextPOT (viewportWidth);
	int height = NPOT_textures ? viewportHeight : getNextPOT (viewportHeight);
	if (frameTexture) {
		int tw, th;
		gfx::renderer->textureSize (frameTexture, &tw, &th);
		if (tw == width && th == height) return;
		gfx::renderer->destroyTarget (frameTarget);
		gfx::renderer->destroyTexture (frameTexture);
	}
	frameTexture = gfx::renderer->createTexture (width, height, gfx::TEX_RGBA8, NULL);
	gfx::renderer->setTextureFilter (frameTexture, gfx::FILTER_NEAREST, gfx::FILTER_NEAREST);
	gfx::renderer->setTextureWrap (frameTexture, gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
	frameTarget = gfx::renderer->createTarget (frameTexture, true);
}

unsigned char * readFramePixels (int * width, int * height) {
	int w = viewportWidth, h = viewportHeight;
	unsigned char * pixels = new unsigned char [(size_t) w * h * 4];
	gfx::renderer->readPixels (viewportOffsetX, viewportOffsetY, w, h, pixels);

	// OpenGL reads the bottom row first.
	unsigned char * row = new unsigned char [(size_t) w * 4];
	for (int y = 0; y < h / 2; y ++) {
		unsigned char * a = pixels + (size_t) y * w * 4;
		unsigned char * b = pixels + (size_t) (h - 1 - y) * w * 4;
		memcpy (row, a, (size_t) w * 4);
		memcpy (a, b, (size_t) w * 4);
		memcpy (b, row, (size_t) w * 4);
	}
	delete [] row;

	*width = w;
	*height = h;
	return pixels;
}

unsigned char * readBackdropPixels (int * width, int * height) {
	if (! backdropTex) return NULL;

	// Without NPOT textures, the texture can be bigger than the scene.
	int tw, th;
	gfx::renderer->textureSize (backdropTex, &tw, &th);
	unsigned char * texture = new unsigned char [(size_t) tw * th * 4];
	gfx::renderer->readTexture (backdropTex, texture);

	int w = sceneWidth < (unsigned int) tw ? sceneWidth : tw;
	int h = sceneHeight < (unsigned int) th ? sceneHeight : th;
	unsigned char * pixels = new unsigned char [(size_t) w * h * 4];
	for (int y = 0; y < h; y ++) {
		memcpy (pixels + (size_t) y * w * 4, texture + (size_t) y * tw * 4, (size_t) w * 4);
	}
	delete [] texture;

	*width = w;
	*height = h;
	return pixels;
}

SDL_Window * sludgeWindow = NULL;
static SDL_GLContext glContext = NULL;

// Set the area of the window in which we draw the game. We keep the aspect
// ratio of the game and leave black bars in the rest of the window. With
// fixed pixels, we scale the game by a whole number only. All sizes are in
// framebuffer pixels, which on a high-DPI display are more than the window
// size in points.
void updateViewport () {
	if (! sludgeWindow) return;

	SDL_GetWindowSizeInPixels (sludgeWindow, &realWinWidth, &realWinHeight);
	if (realWinWidth < 1) realWinWidth = 1;
	if (realWinHeight < 1) realWinHeight = 1;
#if defined(_WIN32) && ! defined(HAVE_GLES2)
	resizeDXGIPresentation (realWinWidth, realWinHeight);
#endif

	if (gameSettings.fixedPixels) {
		int scale = realWinWidth / (int) winWidth;
		if (realWinHeight / (int) winHeight < scale) scale = realWinHeight / (int) winHeight;
		if (scale < 1) scale = 1;
		viewportWidth = winWidth * scale;
		viewportHeight = winHeight * scale;
	} else {
		float winAspect = (float) winWidth / winHeight;
		float realAspect = (float) realWinWidth / realWinHeight;
		if (realAspect > winAspect) {
			viewportHeight = realWinHeight;
			viewportWidth = (int) (realWinHeight * winAspect);
		} else {
			viewportWidth = realWinWidth;
			viewportHeight = (int) ((float) realWinWidth / winAspect);
		}
	}
	viewportOffsetX = (realWinWidth - viewportWidth) / 2;
	viewportOffsetY = (realWinHeight - viewportHeight) / 2;

	if (gfx::renderer) {
		float scale = (float) viewportWidth / (float) winWidth * 0.25;
		if (scale > 1.0) scale = 1.0;
		gfx::renderer->setAntialiasScale (scale);
		resizeFrame ();
		drawIntoWindow ();
	}

	setPixelCoords (false);

	if (movieIsPlaying)
		setMovieViewport ();

	debugOut ("Window is %dx%d pixels, game drawn at %d,%d size %dx%d\n", realWinWidth, realWinHeight, viewportOffsetX, viewportOffsetY, viewportWidth, viewportHeight);
}

// Convert a position in window coordinates, as in mouse events, to game
// coordinates.
void windowToGameCoords (float x, float y, int * gameX, int * gameY) {
	float density = sludgeWindow ? SDL_GetWindowPixelDensity (sludgeWindow) : 1.0f;
	if (density <= 0) density = 1.0f;

	// In OpenGL, the viewport offset is from the bottom of the window. Mouse
	// positions are from the top.
	float px = x * density - viewportOffsetX;
	float py = y * density - (realWinHeight - viewportOffsetY - viewportHeight);

	*gameX = (int) (px * ((float) winWidth / cameraZoom) / viewportWidth);
	*gameY = (int) (py * ((float) winHeight / cameraZoom) / viewportHeight);
}

// Convert a position in game coordinates to window coordinates.
void gameToWindowCoords (float gameX, float gameY, float * x, float * y) {
	float density = sludgeWindow ? SDL_GetWindowPixelDensity (sludgeWindow) : 1.0f;
	if (density <= 0) density = 1.0f;

	float px = gameX * viewportWidth / ((float) winWidth / cameraZoom) + viewportOffsetX;
	float py = gameY * viewportHeight / ((float) winHeight / cameraZoom) + (realWinHeight - viewportOffsetY - viewportHeight);
	*x = px / density;
	*y = py / density;
}

// We draw with SDL_GPU instead of OpenGL.
static bool usingGPU = false;
static SDL_GPUDevice * gpuDevice = NULL;

// Make the window and the SDL_GPU renderer. Returns false, and leaves
// nothing behind, when SDL_GPU can't draw in this window.
static bool createGPUWindow (const char * title, int w, int h, SDL_WindowFlags flags) {
	gpuDevice = SDL_CreateGPUDevice (SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, gameSettings.debugMode, NULL);
	if (! gpuDevice) {
		debugOut ("SDL_GPU is not available: %s\n", SDL_GetError ());
		return false;
	}
	sludgeWindow = SDL_CreateWindow (title, w, h, flags);
	const char * error = NULL;
	if (sludgeWindow && SDL_ClaimWindowForGPUDevice (gpuDevice, sludgeWindow)) {
		// In a test run, we don't wait for the display.
		if (captureIsDeterministic () && SDL_WindowSupportsGPUPresentMode (gpuDevice, sludgeWindow, SDL_GPU_PRESENTMODE_IMMEDIATE)) {
			SDL_SetGPUSwapchainParameters (gpuDevice, sludgeWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_IMMEDIATE);
		}
		gfx::renderer = gfx::createGPURenderer (gpuDevice, sludgeWindow, &error);
		if (gfx::renderer) {
			usingGPU = true;
			// We always say which renderer draws, for tests and bug reports.
			fprintf (stderr, "Renderer: SDL_GPU (%s)\n", SDL_GetGPUDeviceDriver (gpuDevice));
			return true;
		}
		debugOut ("Can't make the SDL_GPU renderer: %s\n", error);
		SDL_ReleaseWindowFromGPUDevice (gpuDevice, sludgeWindow);
	} else {
		debugOut ("Can't draw with SDL_GPU in the window: %s\n", SDL_GetError ());
	}
	if (sludgeWindow) SDL_DestroyWindow (sludgeWindow);
	sludgeWindow = NULL;
	SDL_DestroyGPUDevice (gpuDevice);
	gpuDevice = NULL;
	return false;
}

static void createGraphicsWindow (bool fullscreen) {
	SDL_Rect usable;
	if (! SDL_GetDisplayUsableBounds (SDL_GetPrimaryDisplay (), &usable)) {
		usable.w = 1024;
		usable.h = 768;
	}

	int w, h;
	if (gameSettings.fixedPixels) {
		w = winWidth;
		h = winHeight;
	} else {
		// Fill three quarters of the screen height by default.
		float winAspect = (float) winWidth / winHeight;
		h = usable.h * 3 / 4;
		w = (int) (h * winAspect);
		if (w > usable.w) {
			w = usable.w;
			h = (int) ((float) w / winAspect);
		}
	}

	// We show the window when it is ready.
	SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
	if (captureIsDeterministic ()) {
		// In a test run, the window has one pixel for each pixel of the
		// game on every display, so that the frames are the same.
		w = winWidth;
		h = winHeight;
		flags = SDL_WINDOW_HIDDEN;
	}
	if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
	const char * title = gameName ? gameName : "SLUDGE";

	if (! strcmp (requestedRenderer (), "gpu") && createGPUWindow (title, w, h, flags)) {
		SDL_SetWindowMinimumSize (sludgeWindow, 160, 120);
		SDL_ShowWindow (sludgeWindow);
		return;
	}

#if defined(_WIN32) && ! defined(HAVE_GLES2)
	// On Windows, we show the frames through DXGI where we can, in a
	// window without OpenGL (see dxgipresent.cpp).
	sludgeWindow = SDL_CreateWindow (title, w, h, flags);
	if (sludgeWindow) {
		glContext = startDXGIPresentation (sludgeWindow);
		if (! glContext) {
			SDL_DestroyWindow (sludgeWindow);
			sludgeWindow = NULL;
		}
	}
#endif

	if (! sludgeWindow) {
		sludgeWindow = SDL_CreateWindow (title, w, h, flags | SDL_WINDOW_OPENGL);
		if (! sludgeWindow) {
			msgBox ("Startup Error: Couldn't create the game window.", SDL_GetError ());
			SDL_Quit ();
			exit (2);
		}
		glContext = SDL_GL_CreateContext (sludgeWindow);
		if (! glContext) {
			msgBox ("Startup Error: Couldn't create an OpenGL context.", SDL_GetError ());
			SDL_Quit ();
			exit (2);
		}
		SDL_GL_MakeCurrent (sludgeWindow, glContext);
		// In a test run, we don't wait for the display.
		SDL_GL_SetSwapInterval (captureIsDeterministic () ? 0 : 1);
	}
	SDL_SetWindowMinimumSize (sludgeWindow, 160, 120);
	SDL_ShowWindow (sludgeWindow);

	const char * missing = loadGLFunctions ();
	if (missing) {
		msgBox ("Startup Error: Your graphics driver doesn't support OpenGL 2.0.", missing);
		SDL_Quit ();
		exit (1);
	}

	fprintf (stderr, "Renderer: OpenGL\n");
	debugOut ("OpenGL vendor: %s\n", glGetString (GL_VENDOR));
	debugOut ("OpenGL renderer: %s\n", glGetString (GL_RENDERER));
	debugOut ("OpenGL version: %s\n", glGetString (GL_VERSION));

	const char * error = NULL;
	gfx::renderer = gfx::createGLRenderer (&error);
	if (! gfx::renderer) {
		msgBox ("Startup Error: Couldn't build the shader programs.", error);
		SDL_Quit ();
		exit (1);
	}
}

// Switch between windowed and fullscreen, and create the window the first
// time. We keep the same OpenGL context when switching, so the textures stay
// loaded.
void setGraphicsWindow(bool fullscreen, bool restoreGraphics) {
#if defined(SLUDGE_ALWAYS_FULLSCREEN)
	fullscreen = true;
#endif
	// A test run is always in a window of the size of the game.
	if (captureIsDeterministic ()) fullscreen = false;
	if (fullscreen) specialSettings &= ~SPECIAL_INVISIBLE;

	if (! sludgeWindow) {
		debugHeader();
		createGraphicsWindow (fullscreen);
	} else if (fullscreen != runningFullscreen) {
		SDL_SetWindowFullscreen (sludgeWindow, fullscreen);
		SDL_SyncWindow (sludgeWindow);
	}
	runningFullscreen = fullscreen;

	updateViewport ();

	if (restoreGraphics) sludgeDisplay ();
}

void setupOpenGLStuff() {

	/*
	 * We want *at least* 8 bits of red, green, blue and alpha, a 16-bit
	 * depth buffer and a double buffered window.
	 */
	SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 16 );
	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
#if defined(HAVE_GLES2)
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
	// The shaders are in GLSL 1.10, which is only available in a
	// compatibility context. On macOS, that is an OpenGL 2.1 context.
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif

	setGraphicsWindow(gameSettings.userFullScreen, false);

	if (usingGPU) {
		NPOT_textures = true;
	} else {
#if !defined(HAVE_GLES2)
	/* Check for graphics capabilities... */
	if (getGLVersion () >= 20) {
		// Yes! Textures can be any size!
		NPOT_textures = true;
		debugOut( "OpenGL 2.0! All is good.\n");
	} else {
		NPOT_textures = false;
		debugOut( "Warning: OpenGL version older than 2.0. Expect trouble.\n");
	}
#else
	NPOT_textures = false;
#endif
		int n;
		glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, (GLint *) &n);
		debugOut("Max texture image units: %d\n", n);
	}
	if (captureForcePOTTextures ()) NPOT_textures = false;
	// The frame texture depends on this, and we made it with the window.
	updateViewport ();
}

#if defined(SLUDGE_FRAMEBUFFER_VSYNC)
// Wait for the next vertical sync of the framebuffer. Without /dev/fb0, we
// do not wait.
static void waitForFramebufferVSync () {
	static int framebuffer = open ("/dev/fb0", O_RDONLY);
	if (framebuffer < 0) return;
	int arg = 0;
	ioctl (framebuffer, FBIO_WAITFORVSYNC, &arg);
}
#endif

void swapGraphicsBuffers () {
#if defined(SLUDGE_FRAMEBUFFER_VSYNC)
	waitForFramebufferVSync ();
#endif
#if defined(_WIN32) && ! defined(HAVE_GLES2)
	if (presentingWithDXGI ()) {
		presentWithDXGI ();
		return;
	}
#endif
	SDL_GL_SwapWindow (sludgeWindow);
}


// I found this function on a coding forum on the 'net.
// Looks a bit weird, but it should work.
int getNextPOT(int n) {
	--n;
	n |= n >> 16;
	n |= n >> 8;
	n |= n >> 4;
	n |= n >> 2;
	n |= n >> 1;
	++n;
	return n;
}

int printOglError (const char *file, int         line)
{
	/* Returns 1 if an OpenGL error occurred, 0 otherwise. */
	GLenum glErr;
	int    retCode = 0;

	glErr = glGetError ();
	while (glErr != GL_NO_ERROR)
    {
		debugOut("glError in file %s @ line %d: error code 0x%x\n", file, line, glErr);
		retCode = 1;
		glErr = glGetError ();
    }
	return retCode;
}
