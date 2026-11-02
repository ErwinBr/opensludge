/*
 * Test the meshes of the renderers (gfx::Renderer::createFrameMesh and
 * drawMesh), which the Dev Kit draws with: vertex colours, a texture, the
 * first index and base vertex, the uniform colour, blending and the
 * scissor rectangle, over two frames.
 *
 * Usage: renderer_test gl|gpu
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "renderer.h"
#include "renderer_window.h"

static void logToStderr (const char * format, va_list args) {
	vfprintf (stderr, format, args);
}

static const int W = 64, H = 32;
static int failures = 0;

static gfx::Vertex2D vertex (float x, float y, float u, float v, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
	gfx::Vertex2D vx = { x, y, u, v, { r, g, b, a } };
	return vx;
}

// A matrix from pixels, with row 0 at the bottom, to the target.
static void pixelMatrix (float * m) {
	memset (m, 0, 16 * sizeof (float));
	m[0] = 2.0f / W;
	m[5] = 2.0f / H;
	m[10] = 1.0f;
	m[12] = -1.0f;
	m[13] = -1.0f;
	m[15] = 1.0f;
}

static void expect (const unsigned char * pixels, int x, int y, int r, int g, int b, int a, const char * what) {
	const unsigned char * p = pixels + (y * W + x) * 4;
	int want[4] = { r, g, b, a };
	for (int i = 0; i < 4; i ++) {
		if (abs (p[i] - want[i]) > 2) {
			fprintf (stderr, "FAIL %s at (%d, %d): got %d %d %d %d, want %d %d %d %d\n",
				what, x, y, p[0], p[1], p[2], p[3], r, g, b, a);
			failures ++;
			return;
		}
	}
}

int main (int argc, char ** argv) {
	const char * choice = argc > 1 ? argv[1] : "auto";
	if (! SDL_Init (SDL_INIT_VIDEO)) {
		fprintf (stderr, "Can't start SDL: %s\n", SDL_GetError ());
		return 1;
	}
	if (getenv ("SLUDGE_TEST_DEBUG")) gfx::setLogFunction (logToStderr);
	gfx::WindowOptions options;
	options.title = "renderer_test";
	options.width = W;
	options.height = H;
	options.flags = SDL_WINDOW_HIDDEN;
	options.renderer = choice;
	options.immediate = true;
	SDL_Window * window;
	gfx::WindowProblem problem;
	gfx::Renderer * r = gfx::createWindowRenderer (options, &window, &problem);
	if (! r) {
		fprintf (stderr, "%s %s\n", problem.message, problem.detail);
		return 1;
	}
	if ((strcmp (choice, "gpu") == 0) != gfx::windowRendererUsesGPU ()) {
		fprintf (stderr, "FAIL: asked for %s and got the other renderer\n", choice);
		return 1;
	}

	gfx::Texture * color = r->createTexture (W, H, gfx::TEX_RGBA8, NULL);
	gfx::RenderTarget * target = r->createTarget (color, false);

	// A checkerboard of white and blue, with row 0 at the bottom.
	static const unsigned char checker[16] = {
		255, 255, 255, 255,   0, 0, 255, 255,
		0, 0, 255, 255,   255, 255, 255, 255,
	};
	gfx::Texture * texture = r->createTexture (2, 2, gfx::TEX_RGBA8, checker);
	r->setTextureFilter (texture, gfx::FILTER_NEAREST, gfx::FILTER_NEAREST);

	// Two quads: red on the left, and the checkerboard times green on
	// the right, each with indices from 0.
	gfx::Vertex2D vertices[8] = {
		vertex (0, 0, 0, 0, 255, 0, 0, 255), vertex (32, 0, 0, 0, 255, 0, 0, 255),
		vertex (0, 32, 0, 0, 255, 0, 0, 255), vertex (32, 32, 0, 0, 255, 0, 0, 255),
		vertex (32, 0, 0, 0, 0, 255, 0, 255), vertex (64, 0, 1, 0, 0, 255, 0, 255),
		vertex (32, 32, 0, 1, 0, 255, 0, 255), vertex (64, 32, 1, 1, 0, 255, 0, 255),
	};
	unsigned short indices[12] = { 0, 1, 2, 2, 1, 3, 0, 1, 2, 2, 1, 3 };
	// Yellow at half alpha over everything.
	gfx::Vertex2D cover[4] = {
		vertex (0, 0, 0, 0, 255, 255, 0, 128), vertex (64, 0, 0, 0, 255, 255, 0, 128),
		vertex (0, 32, 0, 0, 255, 255, 0, 128), vertex (64, 32, 0, 0, 255, 255, 0, 128),
	};

	gfx::DrawUniforms u;
	pixelMatrix (u.matrix);
	gfx::PipelineState opaque (gfx::PROG_VERTEX_COLOR);
	gfx::PipelineState blended (gfx::PROG_VERTEX_COLOR, gfx::BLEND_ALPHA);
	unsigned char * pixels = new unsigned char[W * H * 4];

	for (int frame = 0; frame < 2; frame ++) {
		r->setTarget (target);
		r->setViewport (0, 0, W, H);
		r->setScissor (0, 0, 0, 0);
		r->clear (true, false);

		gfx::Mesh * mesh = r->createFrameMesh (vertices, 8, indices, 12);
		gfx::Mesh * coverMesh = r->createFrameMesh (cover, 4, indices, 6);
		r->drawMesh (opaque, u, NULL, mesh, 0, 6, 0);
		r->drawMesh (opaque, u, texture, mesh, 6, 6, 4);
		// Only in the top half, which is rows 16 to 31.
		r->setScissor (0, 16, W, 16);
		r->drawMesh (blended, u, NULL, coverMesh, 0, 6, 0);
		r->setScissor (0, 0, 0, 0);

		// The uniform colour multiplies the vertex colour: a quarter of
		// the width, in the second frame only, half grey on red.
		if (frame == 1) {
			gfx::DrawUniforms grey = u;
			grey.color[0] = grey.color[1] = grey.color[2] = 0.5f;
			r->drawMesh (opaque, grey, NULL, mesh, 0, 3, 0);
		}

		r->readPixels (0, 0, W, H, pixels);
		char what[32];
		snprintf (what, sizeof (what), "frame %d", frame);
		if (frame == 0) expect (pixels, 4, 4, 255, 0, 0, 255, what);
		else expect (pixels, 4, 4, 128, 0, 0, 255, what);
		expect (pixels, 24, 12, 255, 0, 0, 255, what);	// Outside the grey triangle
		expect (pixels, 40, 4, 0, 255, 0, 255, what);		// White times green
		expect (pixels, 56, 4, 0, 0, 0, 255, what);		// Blue times green
		expect (pixels, 20, 24, 255, 128, 0, 255, what);	// Yellow over red
		expect (pixels, 40, 24, 128, 128, 0, 255, what);	// Yellow over black
		expect (pixels, 56, 24, 128, 255, 0, 255, what);	// Yellow over green

		r->setTarget (NULL);
		r->present ();
	}

	// The same into the window, as the Dev Kit draws, in two frames.
	for (int frame = 0; frame < 2; frame ++) {
		r->setTarget (NULL);
		r->setViewport (0, 0, W, H);
		r->clear (true, true);
		gfx::Mesh * mesh = r->createFrameMesh (vertices, 8, indices, 12);
		r->drawMesh (opaque, u, NULL, mesh, 0, 6, 0);
		r->drawMesh (opaque, u, texture, mesh, 6, 6, 4);
		r->readPixels (0, 0, W, H, pixels);
		char what[32];
		snprintf (what, sizeof (what), "window frame %d", frame);
		expect (pixels, 24, 12, 255, 0, 0, 255, what);
		expect (pixels, 40, 4, 0, 255, 0, 255, what);
		expect (pixels, 56, 28, 0, 255, 0, 255, what);
		r->present ();
	}

	delete [] pixels;
	r->destroyTarget (target);
	r->destroyTexture (color);
	r->destroyTexture (texture);
	gfx::destroyWindowRenderer ();
	SDL_Quit ();
	if (failures) return 1;
	printf ("renderer_test %s: all good\n", choice);
	return 0;
}
