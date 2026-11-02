#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "debug.h"
#include "platform-dependent.h"
#include "specialsettings.h"
#include "graphics.h"
#include "language.h"
#include "newfatal.h"
#include "sprbanks.h"
#include "zbuffer.h"
#include "backdrop.h"
#include "renderer.h"
#include "renderer_window.h"
#include "movie.h"
#include "stringy.h"
#include "capture.h"

#include "language.h" // for settings

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

gfx::Renderer * gfx::renderer = NULL;

gfx::DrawUniforms engineUniforms () {
	gfx::DrawUniforms u;
	memcpy (u.matrix, aPMVMatrix, sizeof (u.matrix));
	memcpy (u.color, primaryColor, sizeof (u.color));
	memcpy (u.secondaryColor, secondaryColor, sizeof (u.secondaryColor));
	return u;
}

void gfx::draw (const PipelineState & state, const DrawUniforms & uniforms, Texture * texture, const float * vertices, const float * texCoords) {
	Texture * const textures[3] = { texture, NULL, NULL };
	Quad quad = { vertices, { texCoords, NULL, NULL } };
	renderer->draw (state, uniforms, textures, quad);
}

void gfx::draw (const PipelineState & state, const DrawUniforms & uniforms, const float * vertices) {
	Texture * const textures[3] = { NULL, NULL, NULL };
	Quad quad = { vertices, { NULL, NULL, NULL } };
	renderer->draw (state, uniforms, textures, quad);
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
	gfx::draw (gfx::PipelineState (gfx::PROG_TEXTURE), engineUniforms (), frameTexture, vertices, texCoords);

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
	gfx::windowResized (realWinWidth, realWinHeight);

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

	gfx::setLogFunction (debugOutV);
	gfx::WindowOptions options;
	options.title = gameName ? gameName : "SLUDGE";
	options.width = w;
	options.height = h;
	options.flags = flags;
	options.renderer = requestedRenderer ();
	options.debug = gameSettings.debugMode;
	// In a test run, we don't wait for the display.
	options.immediate = captureIsDeterministic ();
	gfx::WindowProblem problem;
	gfx::renderer = gfx::createWindowRenderer (options, &sludgeWindow, &problem);
	if (! gfx::renderer) {
		msgBox (problem.message, problem.detail);
		SDL_Quit ();
		exit (problem.exitCode);
	}
	SDL_SetWindowMinimumSize (sludgeWindow, 160, 120);
	SDL_ShowWindow (sludgeWindow);
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
	setGraphicsWindow(gameSettings.userFullScreen, false);

	if (gfx::windowRendererUsesGPU ()) {
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
