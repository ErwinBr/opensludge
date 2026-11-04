/*
 * Image tests for the views of the floor maker, the z-buffer maker and the
 * sprite bank editor.
 *
 * We draw each view offscreen and compare the picture with a reference
 * image in the references folder. We count a pixel as different when its
 * red, green or blue differs by more than 8, and a picture as different when
 * more than 0.1% of its pixels are. For a picture that differs, we write it
 * and a picture of the differences into the current folder.
 *
 * We compare the colours, not the alpha: the views show what they draw over
 * a background, and the references come from OpenGL 3.2 in the old GTK
 * tools, which blended the alpha by the alpha (a quarter of a quarter for
 * the floor), while the renderers of the engine blend it as coverage (a
 * quarter).
 *
 * The views draw with the renderers of the engine (draw2d_gfx.cpp), into a
 * render target, with the renderer that we name: gl or gpu.
 *
 * Usage: view_test [--update] [gl|gpu]
 *
 * With --update, we write the pictures as the new reference images.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include <png.h>

#include <SDL3/SDL.h>
#include "renderer.h"
#include "renderer_window.h"
#include "draw2d_gfx.h"

#include "draw2d.h"
#include "floormaker.h"
#include "interface.h"
#include "sprites.h"
#include "zbuffer.h"

// In the Dev Kit, we show a dialog in each function of interface.h. In the
// tests, we print the messages instead.

const char * getTempDir () {
	return "/tmp";
}

bool askAQuestion (const char * head, const char * msg) {
	fprintf (stderr, "Question: %s: %s\n", head, msg);
	return false;
}

bool errorBox (const char * head, const char * msg) {
	fprintf (stderr, "Error: %s: %s\n", head, msg);
	return false;
}

static const int maxChannelDifference = 8;
static const double maxDifferentPixels = 0.001;

static std::string testFolder;
static bool update = false;
static int failures = 0;
// What we put before the names of the pictures that we write.
static std::string prefix;

static Draw2D * draw = NULL;

// A picture of RGBA pixels, the top row first.
struct Picture {
	int width, height;
	std::vector<unsigned char> rgba;
};

static bool readPNG (const std::string & filename, Picture & picture) {
	png_image png;
	memset (&png, 0, sizeof (png));
	png.version = PNG_IMAGE_VERSION;
	if (! png_image_begin_read_from_file (&png, filename.c_str ())) return false;
	png.format = PNG_FORMAT_RGBA;
	picture.width = png.width;
	picture.height = png.height;
	picture.rgba.resize (PNG_IMAGE_SIZE (png));
	if (! png_image_finish_read (&png, NULL, picture.rgba.data (), 0, NULL)) {
		png_image_free (&png);
		return false;
	}
	return true;
}

static void writePNG (const std::string & filename, const Picture & picture) {
	png_image png;
	memset (&png, 0, sizeof (png));
	png.version = PNG_IMAGE_VERSION;
	png.width = picture.width;
	png.height = picture.height;
	png.format = PNG_FORMAT_RGBA;
	if (! png_image_write_to_file (&png, filename.c_str (), 0, picture.rgba.data (), 0, NULL)) {
		fprintf (stderr, "We can't write %s: %s\n", filename.c_str (), png.message);
	}
}

// Flip the rows of RGBA pixels that we read bottom row first.
static void flipRows (Picture & picture) {
	size_t row = (size_t) picture.width * 4;
	std::vector<unsigned char> swap (row);
	for (int y = 0; y < picture.height / 2; y ++) {
		unsigned char * a = &picture.rgba[y * row];
		unsigned char * b = &picture.rgba[(picture.height - 1 - y) * row];
		memcpy (swap.data (), a, row);
		memcpy (a, b, row);
		memcpy (b, swap.data (), row);
	}
}


static gfx::Renderer * renderer = NULL;

static void logToStderr (const char * format, va_list args) {
	vfprintf (stderr, format, args);
}

// Make a hidden window with the renderer that we name, and set up our
// drawing with it.
static bool createContext (const char * choice) {
	if (getenv ("SLUDGE_TEST_DEBUG")) gfx::setLogFunction (logToStderr);
	if (! SDL_Init (SDL_INIT_VIDEO)) {
		fprintf (stderr, "We can't start SDL: %s\n", SDL_GetError ());
		return false;
	}
	gfx::WindowOptions options;
	options.title = "view_test";
	options.flags = SDL_WINDOW_HIDDEN;
	options.renderer = choice;
	options.immediate = true;
	SDL_Window * window;
	gfx::WindowProblem problem;
	renderer = gfx::createWindowRenderer (options, &window, &problem);
	if (! renderer) {
		fprintf (stderr, "%s %s\n", problem.message, problem.detail);
		return false;
	}
	if ((strcmp (choice, "gpu") == 0) != gfx::windowRendererUsesGPU ()) {
		fprintf (stderr, "We asked for %s and got the other renderer.\n", choice);
		return false;
	}
	draw = createGfxDraw2D (renderer);
	prefix = std::string (choice) + "-";
	return true;
}

// A render target of the size of a view.
struct View {
	int width, height;
	gfx::Texture * texture;
	gfx::RenderTarget * target;
};

static View startView (int width, int height) {
	View v = { width, height, NULL, NULL };
	v.texture = renderer->createTexture (width, height, gfx::TEX_RGBA8, NULL);
	v.target = renderer->createTarget (v.texture, false);
	renderer->setTarget (v.target);
	renderer->setScissor (0, 0, 0, 0);
	renderer->setViewport (0, 0, width, height);
	return v;
}

static bool readView (const View & v, Picture & picture, std::string *) {
	picture.width = v.width;
	picture.height = v.height;
	picture.rgba.resize ((size_t) v.width * v.height * 4);
	renderer->setTarget (v.target);
	renderer->readPixels (0, 0, v.width, v.height, picture.rgba.data ());
	flipRows (picture);
	return true;
}

static void endView (const View & v) {
	renderer->setTarget (NULL);
	renderer->present ();
	renderer->destroyTarget (v.target);
	renderer->destroyTexture (v.texture);
}


// Compare the picture in the view with the reference image of the test.
static void check (const char * name, const View & v) {
	Picture picture;
	std::string problem;
	bool readable = readView (v, picture, &problem);
	std::string reference = testFolder + "/references/" + name + ".png";

	if (update) {
		writePNG (reference, picture);
		printf ("Updated %s\n", reference.c_str ());
		return;
	}

	Picture expected;
	bool same = readPNG (reference, expected)
		&& expected.width == picture.width && expected.height == picture.height;
	int different = 0;
	// Where the pixels that differ are, and by how much at most, so that
	// the log of a failure says what changed.
	int largest = 0, left = picture.width, top = picture.height, right = -1, bottom = -1;
	if (same) {
		Picture difference = picture;
		for (size_t p = 0; p < picture.rgba.size (); p += 4) {
			const unsigned char * a = &picture.rgba[p];
			const unsigned char * b = &expected.rgba[p];
			unsigned char * d = &difference.rgba[p];
			bool pixelDifferent = false;
			for (int c = 0; c < 3; c ++) {
				int channel = abs (a[c] - b[c]);
				if (channel > largest) largest = channel;
				if (channel > maxChannelDifference) pixelDifferent = true;
			}
			if (pixelDifferent) {
				int x = (int) (p / 4) % picture.width, y = (int) (p / 4) / picture.width;
				if (x < left) left = x;
				if (x > right) right = x;
				if (y < top) top = y;
				if (y > bottom) bottom = y;
			}
			d[0] = pixelDifferent ? 255 : a[0] / 4;
			d[1] = d[2] = pixelDifferent ? 0 : a[0] / 4;
			d[3] = 255;
			if (pixelDifferent) different ++;
		}
		same = different <= maxDifferentPixels * picture.width * picture.height;
		if (! same) writePNG (prefix + name + "-difference.png", difference);
	}
	if (! same) writePNG (prefix + name + "-actual.png", picture);

	if (! readable) {
		printf ("FAIL %s: %s\n", name, problem.c_str ());
		failures ++;
	} else if (! same) {
		printf ("FAIL %s: %d pixels differ from %s\n", name, different, reference.c_str ());
		if (different) printf ("     by up to %d, from (%d, %d) to (%d, %d); see %s%s-actual.png and -difference.png\n",
			largest, left, top, right, bottom, prefix.c_str (), name);
		failures ++;
	} else {
		printf ("ok   %s (%d pixels differ)\n", name, different);
	}
}

// The floor maker, with the floor from fixtures/floor.flo, a lit vertex, a
// selection line, selected vertices and a selection box, shown whole at half
// size and in part at full size.
static void testFloorMaker () {
	struct spriteBank backdrop;
	memset (&backdrop, 0, sizeof (backdrop));
	addSprite (0, &backdrop);
	struct polyList * floor = NULL;
	std::string file = testFolder + "/fixtures/floor.flo";
	if (! loadFloorFromFile ((char *) file.c_str (), &floor)) {
		printf ("FAIL floor: we can't load %s\n", file.c_str ());
		failures ++;
		return;
	}
	// Two selected vertices, and a box being dragged around another.
	const int selectedXY[] = { 560, 240, 600, 400 };
	struct floorMakerView fmv = {
		&backdrop, floor, 1.0f, 1.0f, 1.0f,
		true, 300, 260,
		true, 60, 300, 420, 420,
		2.0f,
		selectedXY, 2,
		true, 40, 20, 300, 220
	};

	View v = startView (320, 240);
	draw->setViewArea (0, 0, v.width, v.height, 2.0f);
	drawFloorMakerView (*draw, &fmv);
	check ("floormaker", v);

	draw->setViewArea (200, -200, v.width, v.height, 1.0f);
	fmv.r = 0.4f;
	fmv.pointPixels = 1.0f;
	drawFloorMakerView (*draw, &fmv);
	check ("floormaker-scrolled", v);
	endView (v);

	noFloor (&floor);
	draw->forgetPictures (&backdrop);
	forgetSpriteBank (&backdrop);
}

// The sprite bank editor, with the sprite from fixtures/sprite.png, its
// hotspot at its bottom centre, without and with its box.
static void testSpriteBankEditor () {
	struct spriteBank sprites;
	memset (&sprites, 0, sizeof (sprites));
	sprites.type = 2;
	addSprite (0, &sprites);
	std::string file = testFolder + "/fixtures/sprite.png";
	if (! loadSpriteFromPNG (file.c_str (), &sprites, 0)) {
		printf ("FAIL spritebank: we can't load %s\n", file.c_str ());
		failures ++;
		return;
	}
	sprites.sprites[0].xhot = sprites.sprites[0].width / 2;
	sprites.sprites[0].yhot = sprites.sprites[0].height;

	View v = startView (160, 160);
	draw->setSpriteViewArea (-80, 112, v.width, v.height, 1.0f);
	drawSpriteBankView (*draw, &sprites, 0, false);
	check ("spritebank", v);

	draw->setSpriteViewArea (-80, 144, v.width, v.height, 0.5f);
	drawSpriteBankView (*draw, &sprites, 0, true);
	check ("spritebank-box", v);
	endView (v);

	draw->forgetPictures (&sprites);
	forgetSpriteBank (&sprites);
}

// The z-buffer maker, with the z-buffers from fixtures/zbuffer.tga, without
// and with a selected z-buffer.
static void testZBufferMaker () {
	struct spriteBank backdrop;
	memset (&backdrop, 0, sizeof (backdrop));
	std::string file = testFolder + "/fixtures/zbuffer.tga";
	if (! loadZBufferFromTGA (file.c_str (), &backdrop)) {
		printf ("FAIL zbuffer: we can't load %s\n", file.c_str ());
		failures ++;
		return;
	}

	View v = startView (200, 160);
	draw->setViewArea (-20, -20, v.width, v.height, 1.0f);
	drawZBufferView (*draw, &backdrop, 0);
	check ("zbuffer", v);

	drawZBufferView (*draw, &backdrop, 2);
	check ("zbuffer-selected", v);
	endView (v);

	draw->forgetPictures (&backdrop);
	forgetSpriteBank (&backdrop);
}

int main (int argc, char ** argv) {
	testFolder = VIEW_TEST_FOLDER;
	const char * choice = "auto";
	for (int i = 1; i < argc; i ++) {
		if (! strcmp (argv[i], "--update")) update = true;
		else if (! strcmp (argv[i], "gl") || ! strcmp (argv[i], "gpu")) choice = argv[i];
		else {
			fprintf (stderr, "Usage: view_test [--update] [gl|gpu]\n");
			return 2;
		}
	}
	if (! createContext (choice)) return 1;

	testFloorMaker ();
	testSpriteBankEditor ();
	testZBufferMaker ();

	delete draw;
	gfx::destroyWindowRenderer ();
	SDL_Quit ();
	if (failures) printf ("%d tests failed\n", failures);
	return failures ? 1 : 0;
}
