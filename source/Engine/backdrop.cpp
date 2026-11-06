#include <vector>
#include <png.h>

#include <stdlib.h>
#include <stdint.h>

#include "sludge_gl.h"

#include "allfiles.h"

#include "debug.h"
#include "newfatal.h"
#include "colours.h"
#include "fileset.h"
#include "cursors.h"
#include "backdrop.h"
#include "language.h"
#include "moreio.h"
#include "variable.h"
#include "zbuffer.h"
#include "graphics.h"
#include "line.h"
#include "people.h"
#include "talk.h"
#include "sludger.h"
#include "statusba.h"

#include "version.h"

extern inputType input;


bool freeze ();
void unfreeze (bool);	// Because FREEZE.H needs a load of other includes

GLubyte * backdropTexture = NULL;
gfx::Texture * backdropTex = NULL;
gfx::RenderTarget * backdropTarget = NULL;
bool backdropExists = false;
GLfloat backdropTexW = 1.0;
GLfloat backdropTexH = 1.0;

// A copy of the backdrop, from which we draw while we draw into the
// backdrop, because we can't draw from a texture while we draw into it.
static gfx::Texture * backdropCopy = NULL;

texture lightMap;

gfx::Texture * snapshotTex = NULL;
float snapTexW = 1.0;
float snapTexH = 1.0;

int lightMapMode = LIGHTMAPMODE_PIXEL;

parallaxLayer * parallaxStuff = NULL;

int cameraPX = 0, cameraPY = 0;

unsigned int sceneWidth, sceneHeight;
int lightMapNumber;
unsigned int currentBlankColour = makeColour (0, 0, 0);

extern int cameraX, cameraY;
extern float cameraZoom;

void setBackdropTexture (gfx::Texture * t) {
	if (backdropTarget) gfx::renderer->releaseTarget (backdropTarget);
	backdropTarget = NULL;
	backdropTex = t;
	if (t) backdropTarget = gfx::renderer->createTarget (t, true);
}

gfx::Texture * copyOfBackdrop () {
	int tw, th;
	gfx::renderer->textureSize (backdropTex, &tw, &th);
	if (backdropCopy) {
		int cw, ch;
		gfx::renderer->textureSize (backdropCopy, &cw, &ch);
		if (cw != tw || ch != th) {
			gfx::renderer->releaseTexture (backdropCopy);
			backdropCopy = NULL;
		}
	}
	if (! backdropCopy) {
		backdropCopy = gfx::renderer->createTexture (tw, th, gfx::TEX_RGBA8, NULL);
		gfx::renderer->setTextureWrap (backdropCopy, gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
	}
	// We draw into the backdrop pixel for pixel, with the backdrop filtered
	// as nearest, as setPixelCoords (true) sets it.
	gfx::renderer->setTextureFilter (backdropCopy, gfx::FILTER_NEAREST, gfx::FILTER_NEAREST);

	gfx::renderer->setScissor (0, 0, 0, 0);
	gfx::renderer->setTarget (backdropTarget);
	gfx::renderer->copyToTexture (backdropCopy, 0, 0, 0, 0, tw, th);
	return backdropCopy;
}

void nosnapshot () {
	gfx::renderer->releaseTexture (snapshotTex);
	snapshotTex = NULL;
}

void saveSnapshot(FILE * fp) {
	if (snapshotTex) {
		fputc (1, fp);				// 1 for snapshot follows
		saveCoreHSI (fp, snapshotTex, winWidth, winHeight);
	} else {
		fputc (0, fp);
	}
}

bool snapshot () {

	nosnapshot ();
	if (! freeze ()) return false;


	setPixelCoords (true);

	int w = winWidth;
	int h = winHeight;
	if (! NPOT_textures) {
		w = getNextPOT(winWidth);
		h = getNextPOT(winHeight);
		snapTexW = ((double)winWidth) / w;
		snapTexH = ((double)winHeight) / h;
	}

	snapshotTex = gfx::renderer->createTexture (w, h, gfx::TEX_RGBA8, NULL);
	gfx::renderer->setTextureWrap (snapshotTex, gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
	gfx::renderer->setTextureFilter (snapshotTex, gfx::FILTER_NEAREST, gfx::FILTER_NEAREST);
	gfx::RenderTarget * target = gfx::renderer->createTarget (snapshotTex, true);

	// Render scene
	drawIntoTile (target, 0, 0, 0, 0, winWidth, winHeight);

	drawBackDrop ();				// Draw the room
	drawZBuffer(cameraX, cameraY);

	// Legacy: a snapshot has always moved the characters' animations on by a
	// tick (firing their frame events), because drawing did. Games time on
	// it, so we keep it.
	updatePeopleAnimations (1);
	drawPeople ();					// Then add any moving characters...
	viewSpeech ();					// ...and anything being said
	drawStatusBar ();

	gfx::renderer->releaseTarget (target);
	drawIntoWindow ();
	setPixelCoords (false);

	unfreeze (false);
	return true;
}

bool restoreSnapshot (FILE * fp) {
	unsigned int picWidth = get2bytes (fp);
	unsigned int picHeight = get2bytes (fp);

	if ((picWidth != winWidth) || (picHeight != winHeight))
		return false;

	unsigned int t1, t2, n;
	unsigned short c;

	GLubyte * target;
	if (! NPOT_textures) {
		picWidth = getNextPOT(picWidth);
		picHeight = getNextPOT(picHeight);
		snapTexW = ((double)winWidth) / picWidth;
		snapTexH = ((double)winHeight) / picHeight;
	}
	GLubyte * snapshotTexture = new GLubyte [picHeight*picWidth*4] ();
	if (! snapshotTexture) return fatal("Out of memory while restoring snapshot.");

	for (t2 = 0; t2 < winHeight; t2 ++) {
		t1 = 0;
		while (t1 < winWidth) {
			c = (unsigned short) get2bytes (fp);
			if (c & 32) {
				n = fgetc (fp) + 1;
				c -= 32;
			} else {
				n = 1;
			}
			while (n --) {
				target = snapshotTexture + 4*picWidth*t2 + t1*4;
				target[0] = (GLubyte) redValue(c);
				target[1] = (GLubyte) greenValue(c);
				target[2] = (GLubyte) blueValue(c);
				target[3] = (GLubyte) 255;
				t1++;
			}
		}
	}

	if (snapshotTex) {
		gfx::renderer->setTextureImage (snapshotTex, picWidth, picHeight, snapshotTexture);
	} else {
		snapshotTex = gfx::renderer->createTexture (picWidth, picHeight, gfx::TEX_RGBA8, snapshotTexture);
	}
	gfx::renderer->setTextureWrap (snapshotTex, gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
	gfx::renderer->setTextureFilter (snapshotTex, gfx::FILTER_NEAREST, gfx::FILTER_NEAREST);

	delete [] snapshotTexture;
	snapshotTexture = NULL;

	return true;
}


void killBackDrop () {
	gfx::Texture * t = backdropTex;
	setBackdropTexture (NULL);
	gfx::renderer->releaseTexture (t);
	backdropExists = false;
}

void killLightMap () {
	gfx::renderer->releaseTexture (lightMap.tex);
	lightMap.tex = NULL;
	if (lightMap.data) {
		delete [] lightMap.data;
		lightMap.data = NULL;
	}
	lightMapNumber = 0;
}

void killParallax () {
	while (parallaxStuff) {

		parallaxLayer * k = parallaxStuff;
		parallaxStuff = k -> next;

		// Now kill the image
		gfx::renderer->releaseTexture (k -> tex);
		delete [] k -> texture;
		delete k;
		k = NULL;
	}
}

bool reserveBackdrop () {
	cameraX = 0;
	cameraY = 0;
	input.mouseX = (int)((float)input.mouseX * cameraZoom);
	input.mouseY = (int)((float)input.mouseY * cameraZoom);
	cameraZoom = 1.0;
	input.mouseX = (int)((float)input.mouseX / cameraZoom);
	input.mouseY = (int)((float)input.mouseY / cameraZoom);
	setPixelCoords(false);
	int picWidth = sceneWidth;
	int picHeight = sceneHeight;

	if (backdropTexture) delete [] backdropTexture;
	if (! NPOT_textures) {
		picWidth = getNextPOT(sceneWidth);
		picHeight = getNextPOT(sceneHeight);
		backdropTexW = ((double)sceneWidth) / picWidth;
		backdropTexH = ((double)sceneHeight) / picHeight;
	}
	// We start with a transparent backdrop. Without NPOT textures, the
	// texture is larger than the scene, and its edges are drawn from the
	// pixels beyond the scene, so these must not be left uninitialised.
	backdropTexture = new GLubyte [picWidth*picHeight*4] ();
	if (! checkNew (backdropTexture)) return false;

	gfx::Texture * t = backdropTex;
	if (t) {
		gfx::renderer->setTextureImage (t, picWidth, picHeight, backdropTexture);
	} else {
		t = gfx::renderer->createTexture (picWidth, picHeight, gfx::TEX_RGBA8, backdropTexture);
	}
	gfx::renderer->setTextureWrap (t, gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
	gfx::Filter filter = gameSettings.antiAlias < 0 ? gfx::FILTER_LINEAR : gfx::FILTER_NEAREST;
	gfx::renderer->setTextureFilter (t, filter, filter);
	// The size may have changed, so we make a new target.
	setBackdropTexture (t);

	return true;
}

bool resizeBackdrop (int x, int y) {
	killLightMap ();
	killBackDrop ();
	killParallax ();
	killZBuffer ();
	sceneWidth = x;
	sceneHeight = y;
	return reserveBackdrop ();
}

void loadBackDrop (int fileNum, int x, int y) {
	setResourceForFatal (fileNum);

	if (! openFileFromNum (fileNum)) {
		fatal ("Can't load overlay image");
		return;
	}

	if (! loadHSI (bigDataFile, x, y, false)) {
		char mess[200];
		snprintf (mess, sizeof (mess), "Can't paste overlay image outside scene dimensions\n\nX = %i\nY = %i\nWidth = %i\nHeight = %i", x, y, sceneWidth, sceneHeight);
		fatal (mess);
	}

	finishAccess ();
	setResourceForFatal (-1);
}

void mixBackDrop (int fileNum, int x, int y) {
	setResourceForFatal (fileNum);
	if (! openFileFromNum (fileNum)) {
		fatal ("Can't load overlay image");
		return;
	}

	if (! mixHSI (bigDataFile, x, y)) {
		fatal ("Can't paste overlay image outside screen dimensions");
	}

	finishAccess ();
	setResourceForFatal (-1);
}

void blankScreen (int x1, int y1, int x2, int y2) {

	if (y1 < 0) y1 = 0;
	if (x1 < 0) x1 = 0;
	if (x2 > (int) sceneWidth) x2 = (int)sceneWidth;
	if (y2 > (int) sceneHeight) y2 = (int)sceneHeight;

	int picWidth = x2-x1;
	int picHeight = y2-y1;

	setPixelCoords (true);

	int xoffset = 0;
	while (xoffset < picWidth) {
		int w = (picWidth-xoffset < viewportWidth) ? picWidth-xoffset : viewportWidth;

		int yoffset = 0;
		while (yoffset < picHeight) {
			int h = (picHeight-yoffset < viewportHeight) ? picHeight-yoffset : viewportHeight;

			drawIntoTile (backdropTarget, x1+xoffset, y1+yoffset, 0, 0, w, h);

			const GLfloat vertices[] = { 
				-10.325f, -1.325f, 0.0f, 
				w+1.325f, -1.325f, 0.0f, 
				-10.325f, h+1.325f, 0.0f,
				w+1.325f, h+1.325f, 0.0f 
			};

			setPrimaryColor(redValue(currentBlankColour)/255.0f, greenValue(currentBlankColour)/255.0f, blueValue(currentBlankColour)/255.0f, 1.0f);
			gfx::draw (gfx::PipelineState (gfx::PROG_COLOR), engineUniforms (), vertices);

			yoffset += viewportHeight;
		}
		xoffset += viewportWidth;
	}

	drawIntoWindow ();
	setPixelCoords (false);
}

void hardScroll (int distance) {
	if (abs (distance) >= sceneHeight) {
		blankScreen (0, 0, sceneWidth, sceneHeight);
		return;
	}

	if (! distance) return;

	const GLfloat backdropTexCoords[] = { 
		0.0f, 0.0f,
		backdropTexW, 0.0f,
		0.0f, backdropTexH,
		backdropTexW, backdropTexH
	}; 

	setPixelCoords (true);

	unsigned int xoffset = 0;
	while (xoffset < sceneWidth) {
		int w = (sceneWidth-xoffset < viewportWidth) ? sceneWidth-xoffset : viewportWidth;

		unsigned int yoffset = 0;
		while (yoffset < sceneHeight) {
			int h = (sceneHeight-yoffset < viewportHeight) ? sceneHeight-yoffset : viewportHeight;

			// Render the backdrop
			gfx::renderer->setTextureFilter (backdropTex, gfx::FILTER_NEAREST, gfx::FILTER_NEAREST);
			gfx::Texture * copy = copyOfBackdrop ();
			drawIntoTile (backdropTarget, xoffset, yoffset, 0, 0, w, h);

			const GLfloat vertices[] = { 
				(GLfloat)-xoffset, (GLfloat)-distance-yoffset, 0., 
				(GLfloat)sceneWidth-xoffset, (GLfloat)-distance-yoffset, 0., 
				(GLfloat)-xoffset, (GLfloat)sceneHeight-distance-yoffset, 0.,
				(GLfloat)sceneWidth-xoffset, (GLfloat)sceneHeight-distance-yoffset, 0. 
			};

			gfx::draw (gfx::PipelineState (gfx::PROG_TEXTURE), engineUniforms (), copy, vertices, backdropTexCoords);

			yoffset += viewportHeight;
		}
		xoffset += viewportWidth;
	}
	drawIntoWindow ();
	setPixelCoords (false);
}

void drawVerticalLine (unsigned int x, unsigned int y1, unsigned int y2) {
	drawLine (x, y1, x, y2);
}

void drawHorizontalLine (unsigned int x1, unsigned int y, unsigned int x2) {
	drawLine (x1, y, x2, y);
}

void darkScreen () {
	setPixelCoords (true);

	int xoffset = 0;
	while (xoffset < sceneWidth) {
		int w = (sceneWidth-xoffset < viewportWidth) ? sceneWidth-xoffset : viewportWidth;

		int yoffset = 0;
		while (yoffset < sceneHeight) {
			int h = (sceneHeight-yoffset < viewportHeight) ? sceneHeight-yoffset : viewportHeight;

			// Render the scene - first the old backdrop
			gfx::renderer->setTextureFilter (backdropTex, gfx::FILTER_NEAREST, gfx::FILTER_NEAREST);
			gfx::Texture * copy = copyOfBackdrop ();
			drawIntoTile (backdropTarget, xoffset, yoffset, 0, 0, w, h);

			const GLfloat vertices[] = { 
				(GLfloat)-xoffset, (GLfloat)-yoffset, 0., 
				(GLfloat)sceneWidth-xoffset, (GLfloat)-yoffset, 0., 
				(GLfloat)-xoffset, (GLfloat)sceneHeight-yoffset, 0.,
				(GLfloat)sceneWidth-xoffset, (GLfloat)sceneHeight-yoffset, 0.
			};

			const GLfloat texCoords[] = { 
				0.0f, 0.0f,
				backdropTexW, 0.0f,
				0.0f, backdropTexH,
				backdropTexW, backdropTexH
			}; 

			gfx::draw (gfx::PipelineState (gfx::PROG_TEXTURE), engineUniforms (), copy, vertices, texCoords);

			// Then the darkness, without changing the alpha
			setPrimaryColor(0.0f, 0.0f, 0.0f, 0.5f);
			gfx::draw (gfx::PipelineState (gfx::PROG_COLOR, gfx::BLEND_ALPHA, gfx::DEPTH_OFF, gfx::MASK_RGB), engineUniforms (), vertices);

			yoffset += h;
		}
		xoffset += w;
	}

	drawIntoWindow ();
	setPixelCoords (false);
}



inline int sortOutPCamera (int cX, int fX, int sceneMax, int boxMax) {
	return (fX == 65535) ?
	(sceneMax ? ((cX * boxMax) / sceneMax) : 0)
	:
	((cX * fX) / 100);
}


void drawBackDrop () {

	setPrimaryColor(1.0, 1.0, 1.0, 1.0);

	gfx::PipelineState state (gfx::PROG_SMART_SCALER, gfx::BLEND_ALPHA);
	gfx::DrawUniforms uniforms = engineUniforms ();
	uniforms.antialias = gameSettings.antiAlias == 1;

	if (parallaxStuff) {
		parallaxLayer * ps = parallaxStuff;

		while (ps->next) ps = ps->next;

		while (ps) {
			ps -> cameraX = sortOutPCamera (cameraX, ps -> fractionX, (int)(sceneWidth - (float)winWidth/cameraZoom), (int)(ps -> width - (float)winWidth/cameraZoom));
			ps -> cameraY = sortOutPCamera (cameraY, ps -> fractionY, (int)(sceneHeight - (float)winHeight/cameraZoom), (int)(ps -> height - (float)winHeight/cameraZoom));

			float w = (ps->wrapS) ? sceneWidth : ps->width;
			float h = (ps->wrapT) ? sceneHeight : ps->height;
			float texw;
			float texh;
			if (! NPOT_textures) {
				texw = (ps->wrapS) ? (float) sceneWidth / ps->width: (float) ps->width / getNextPOT(ps->width);
				texh = (ps->wrapT) ? (float) sceneHeight / ps->height: (float) ps->height / getNextPOT(ps->height);
			} else {
				texw = (ps->wrapS) ? (float) sceneWidth / ps->width: 1.0;
				texh = (ps->wrapT) ? (float) sceneHeight / ps->height: 1.0;
			}

			const GLfloat vertices[] = { 
				(GLfloat)-ps -> cameraX, (GLfloat)-ps -> cameraY, 0.1f, 
				w -ps -> cameraX, (GLfloat)-ps -> cameraY, 0.1f, 
				(GLfloat)-ps -> cameraX, h -ps -> cameraY, 0.1f,
				w -ps -> cameraX, h -ps -> cameraY, 0.1f
			};

			const GLfloat texCoords[] = { 
				0.0f, 0.0f,
				texw, 0.0f,
				0.0f, texh,
				texw, texh
			}; 
			gfx::draw (state, uniforms, ps->tex, vertices, texCoords);

			ps = ps -> prev;
		}
	}

	const GLfloat backdropTexCoords[] = { 
		0.0f, 0.0f,
		backdropTexW, 0.0f,
		0.0f, backdropTexH,
		backdropTexW, backdropTexH
	}; 

	const GLfloat vertices[] = { 
		(GLfloat)-cameraX, (GLfloat)-cameraY, 0., 
		(GLfloat)sceneWidth- (GLfloat)cameraX, (GLfloat)-cameraY, 0., 
		(GLfloat)-cameraX, (GLfloat)sceneHeight- (GLfloat)cameraY, 0.,
		(GLfloat)sceneWidth- (GLfloat)cameraX, (GLfloat)sceneHeight- (GLfloat)cameraY, 0.
	};

	gfx::draw (state, uniforms, backdropTex, vertices, backdropTexCoords);
}

bool loadLightMap (int v) {
	int newPicWidth, newPicHeight;

	setResourceForFatal (v);
	if (! openFileFromNum (v)) return fatal ("Can't open light map.");

	long file_pointer = ftell (bigDataFile);

	png_structp png_ptr;
	png_infop info_ptr, end_info;


	int fileIsPNG = true;

	// Is this a PNG file?

	char tmp[10];
	size_t bytes_read = fread(tmp, 1, 8, bigDataFile);
	if (bytes_read != 8 && ferror (bigDataFile)) {
		debugOut("Reading error in loadLightMap.\n");
	}

    if (png_sig_cmp((png_byte *) tmp, 0, 8)) {
		// No, it's old-school HSI
		fileIsPNG = false;
		fseek(bigDataFile, file_pointer, SEEK_SET);

		newPicWidth = lightMap.w = get2bytes (bigDataFile);
		newPicHeight = lightMap.h = get2bytes (bigDataFile);
	} else {
		// Read the PNG header

		png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
		if (!png_ptr) {
			return false;
		}

		info_ptr = png_create_info_struct(png_ptr);
		if (!info_ptr) {
			png_destroy_read_struct(&png_ptr, (png_infopp) NULL, (png_infopp) NULL);
			return false;
		}

		end_info = png_create_info_struct(png_ptr);
		if (!end_info) {
			png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
			return false;
		}
		png_init_io(png_ptr, bigDataFile);		// Tell libpng which file to read
		png_set_sig_bytes(png_ptr, 8);	// 8 bytes already read

		png_read_info(png_ptr, info_ptr);

		png_uint_32 width, height;
		int bit_depth, color_type, interlace_type, compression_type, filter_method;
		png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, &compression_type, &filter_method);

		newPicWidth = lightMap.w = width;
		newPicHeight = lightMap.h = height;

		if (bit_depth < 8) png_set_packing(png_ptr);
		png_set_expand(png_ptr);
		if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png_ptr);
		if (bit_depth == 16) png_set_strip_16(png_ptr);

		png_set_add_alpha(png_ptr, 0xff, PNG_FILLER_AFTER);

		png_read_update_info(png_ptr, info_ptr);
		png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, &compression_type, &filter_method);

		//int rowbytes = png_get_rowbytes(png_ptr, info_ptr);

	}

	if (lightMapMode == LIGHTMAPMODE_HOTSPOT) {
		if (lightMap.w != sceneWidth || lightMap.h != sceneHeight) {
			return fatal ("Light map width and height don't match scene width and height. That is required for lightmaps in HOTSPOT mode.");
		}
	}

	if (! NPOT_textures) {
		newPicWidth = getNextPOT(lightMap.w);
		newPicHeight = getNextPOT(lightMap.h);
		lightMap.texW = (double) lightMap.w / newPicWidth;
		lightMap.texH = (double) lightMap.h / newPicHeight;
	} else {
		lightMap.texW = 1.0;
		lightMap.texH = 1.0;
	}

	killLightMap ();
	lightMapNumber = v;

	if (lightMap.data) delete [] lightMap.data;

	lightMap.data = new GLubyte [newPicWidth*newPicHeight*4] ();
	if (! lightMap.data) {
		return fatal ("Out of memory loading light map.");
	}

	int t1, t2, n;
	unsigned short c;
	GLubyte * target;

	if (fileIsPNG) {
		std::vector<unsigned char *> row_pointers (lightMap.h);
		for (int i = 0; i<lightMap.h; i++)
			row_pointers[i] = lightMap.data + 4*i*newPicWidth;

		png_read_image(png_ptr, (png_byte **) row_pointers.data ());
		png_read_end(png_ptr, NULL);
		png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
	} else {

		for (t2 = 0; t2 < lightMap.h; t2 ++) {
			t1 = 0;
			while (t1 < lightMap.w) {
				c = (unsigned short) get2bytes (bigDataFile);
				if (c & 32) {
					n = fgetc (bigDataFile) + 1;
					c -= 32;
				} else {
					n = 1;
				}
				while (n --) {
					target = lightMap.data + 4*newPicWidth*t2 + t1*4;
					target[0] = (GLubyte) redValue(c);
					target[1] = (GLubyte) greenValue(c);
					target[2] = (GLubyte) blueValue(c);
					target[3] = (GLubyte) 255;
					t1++;
				}
			}
		}
	}

	lightMap.tex = gfx::renderer->createTexture (newPicWidth, newPicHeight, gfx::TEX_RGBA8, lightMap.data);
	gfx::renderer->setTextureWrap (lightMap.tex, gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
	gfx::renderer->setTextureFilter (lightMap.tex, gfx::FILTER_NEAREST, gfx::FILTER_NEAREST);

	finishAccess ();
	setResourceForFatal (-1);

	return true;
}

static void setParallaxSampling (parallaxLayer * nP) {
	gfx::renderer->setTextureWrap (nP->tex, nP->wrapS ? gfx::WRAP_REPEAT : gfx::WRAP_CLAMP, nP->wrapT ? gfx::WRAP_REPEAT : gfx::WRAP_CLAMP);
	gfx::Filter filter = gameSettings.antiAlias < 0 ? gfx::FILTER_LINEAR : gfx::FILTER_NEAREST;
	gfx::renderer->setTextureFilter (nP->tex, filter, filter);
}

void reloadParallaxTextures () {
	parallaxLayer * nP = parallaxStuff;
	if (! nP) return;

	while (nP) {
		//fprintf (stderr, "Reloading parallax. (%d, %d) ", nP->width, nP->height);
		if (! NPOT_textures) {
			nP->tex = gfx::renderer->createTexture (getNextPOT(nP->width), getNextPOT(nP->height), gfx::TEX_RGBA8, nP->texture);
		} else {
			nP->tex = gfx::renderer->createTexture (nP->width, nP->height, gfx::TEX_RGBA8, nP->texture);
		}
		setParallaxSampling (nP);
		nP = nP->next;
	}
}

bool loadParallax (unsigned short v, unsigned short fracX, unsigned short fracY) {
	setResourceForFatal (v);
	if (! openFileFromNum (v)) return fatal ("Can't open parallax image");

	parallaxLayer * nP = new parallaxLayer;
	if (! checkNew (nP)) return false;

	nP -> next = parallaxStuff;
	parallaxStuff = nP;
	if (nP -> next) {
		nP -> next -> prev = nP;
	}
	nP -> prev = NULL;

	int picWidth;
	int picHeight;

	long file_pointer = ftell (bigDataFile);

	png_structp png_ptr;
	png_infop info_ptr, end_info;


	int fileIsPNG = true;

	// Is this a PNG file?

	char tmp[10];
	size_t bytes_read = fread(tmp, 1, 8, bigDataFile);
	if (bytes_read != 8 && ferror (bigDataFile)) {
		debugOut("Reading error in loadParallax.\n");
	}
    if (png_sig_cmp((png_byte *) tmp, 0, 8)) {
		// No, it's old-school HSI
		fileIsPNG = false;
		fseek(bigDataFile, file_pointer, SEEK_SET);

		picWidth = nP -> width = get2bytes (bigDataFile);
		picHeight = nP -> height = get2bytes (bigDataFile);
	} else {
		// Read the PNG header

		png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
		if (!png_ptr) {
			return false;
		}

		info_ptr = png_create_info_struct(png_ptr);
		if (!info_ptr) {
			png_destroy_read_struct(&png_ptr, (png_infopp) NULL, (png_infopp) NULL);
			return false;
		}

		end_info = png_create_info_struct(png_ptr);
		if (!end_info) {
			png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
			return false;
		}
		png_init_io(png_ptr, bigDataFile);		// Tell libpng which file to read
		png_set_sig_bytes(png_ptr, 8);	// 8 bytes already read

		png_read_info(png_ptr, info_ptr);

		png_uint_32 width, height;
		int bit_depth, color_type, interlace_type, compression_type, filter_method;
		png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, &compression_type, &filter_method);

		picWidth = nP -> width = width;
		picHeight = nP -> height = height;

		if (bit_depth < 8) png_set_packing(png_ptr);
		png_set_expand(png_ptr);
		if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png_ptr);
		if (bit_depth == 16) png_set_strip_16(png_ptr);

		png_set_add_alpha(png_ptr, 0xff, PNG_FILLER_AFTER);

		png_read_update_info(png_ptr, info_ptr);
		png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, &compression_type, &filter_method);

		//int rowbytes = png_get_rowbytes(png_ptr, info_ptr);

	}

	if (! NPOT_textures) {
		picWidth = getNextPOT(picWidth);
		picHeight = getNextPOT(picHeight);
	}

	nP -> fileNum = v;
	nP -> fractionX = fracX;
	nP -> fractionY = fracY;

	if (fracX == 65535) {
		nP -> wrapS = false;
		if (nP -> width < winWidth) {
			fatal ("For AUTOFIT parallax backgrounds, the image must be at least as wide as the game window/screen.");
			return false;
		}
	} else {
		nP -> wrapS = true;
	}

	if (fracY == 65535) {
		nP -> wrapT = false;
		if (nP -> height < winHeight) {
			fatal ("For AUTOFIT parallax backgrounds, the image must be at least as tall as the game window/screen.");
			return false;
		}
	} else {
		nP -> wrapT = true;
	}

	nP -> texture = new GLubyte [picHeight * picWidth * 4] ();
	if (! checkNew (nP -> texture)) return false;

	if (fileIsPNG) {
		std::vector<unsigned char *> row_pointers (nP -> height);
		for (int i = 0; i < nP -> height; i++)
			row_pointers[i] = nP -> texture + 4*i*picWidth;

		png_read_image(png_ptr, (png_byte **) row_pointers.data ());
		png_read_end(png_ptr, NULL);
		png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
	} else {

		int t1, t2, n;
		unsigned short c;
		GLubyte * target;

		for (t2 = 0; t2 < nP -> height; t2 ++) {
			t1 = 0;
			while (t1 < nP -> width) {
				c = (unsigned short) get2bytes (bigDataFile);
				if (c & 32) {
					n = fgetc (bigDataFile) + 1;
					c -= 32;
				} else {
					n = 1;
				}
				while (n--) {
					target = nP -> texture + 4*picWidth*t2 + t1*4;
					if (c == 63519 || c == 2015) {
						target[0] = (GLubyte) 0;
						target[1] = (GLubyte) 0;
						target[2] = (GLubyte) 0;
						target[3] = (GLubyte) 0;
					} else {
						target[0] = (GLubyte) redValue(c);
						target[1] = (GLubyte) greenValue(c);
						target[2] = (GLubyte) blueValue(c);
						target[3] = (GLubyte) 255;
					}
					t1 ++;
				}
			}
		}
	}

	nP->tex = gfx::renderer->createTexture (picWidth, picHeight, gfx::TEX_RGBA8, nP->texture);
	setParallaxSampling (nP);

	finishAccess ();
	setResourceForFatal (-1);
	return true;
}

extern int viewportOffsetX, viewportOffsetY;


bool loadHSI (FILE * fp, int x, int y, bool reserve) {

	int t1, t2, n;
	unsigned short c;
	GLubyte * target;
	int32_t transCol = reserve ? -1 : 63519;
	int picWidth;
	int picHeight;
	int realPicWidth, realPicHeight;
	long file_pointer = ftell (fp);

	png_structp png_ptr;
	png_infop info_ptr, end_info;


	int fileIsPNG = true;

	// Is this a PNG file?

	char tmp[10];
	size_t bytes_read = fread(tmp, 1, 8, fp);
	if (bytes_read != 8 && ferror (fp)) {
		debugOut("Reading error in loadHSI.\n");
	}
    if (png_sig_cmp((png_byte *) tmp, 0, 8)) {
		// No, it's old-school HSI
		fileIsPNG = false;
		fseek(fp, file_pointer, SEEK_SET);

		picWidth = realPicWidth = get2bytes (fp);
		picHeight = realPicHeight = get2bytes (fp);
	} else {
		// Read the PNG header

		png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
		if (!png_ptr) {
			return false;
		}

		info_ptr = png_create_info_struct(png_ptr);
		if (!info_ptr) {
			png_destroy_read_struct(&png_ptr, (png_infopp) NULL, (png_infopp) NULL);
			return false;
		}

		end_info = png_create_info_struct(png_ptr);
		if (!end_info) {
			png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
			return false;
		}
		png_init_io(png_ptr, fp);		// Tell libpng which file to read
		png_set_sig_bytes(png_ptr, 8);	// 8 bytes already read

		png_read_info(png_ptr, info_ptr);

		png_uint_32 width, height;
		int bit_depth, color_type, interlace_type, compression_type, filter_method;
		png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, &compression_type, &filter_method);

		picWidth = realPicWidth = width;
		picHeight = realPicHeight = height;

		if (bit_depth < 8) png_set_packing(png_ptr);
		png_set_expand(png_ptr);
		if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png_ptr);
		if (bit_depth == 16) png_set_strip_16(png_ptr);

		png_set_add_alpha(png_ptr, 0xff, PNG_FILLER_AFTER);

		png_read_update_info(png_ptr, info_ptr);
		png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, &compression_type, &filter_method);

		//int rowbytes = png_get_rowbytes(png_ptr, info_ptr);

	}


	GLfloat texCoordW = 1.0;
	GLfloat texCoordH = 1.0;
	if (! NPOT_textures) {
		picWidth = getNextPOT(picWidth);
		picHeight = getNextPOT(picHeight);
		texCoordW = ((double)realPicWidth) / picWidth;
		texCoordH = ((double)realPicHeight) / picHeight;
	}

	if (reserve) {
		if (! resizeBackdrop (realPicWidth, realPicHeight)) return false;
	}

	if (x == IN_THE_CENTRE) x = (sceneWidth - realPicWidth) >> 1;
	if (y == IN_THE_CENTRE) y = (sceneHeight - realPicHeight) >> 1;
	if (x < 0 || x + realPicWidth > sceneWidth || y < 0 || y + realPicHeight > sceneHeight) return false;

	if (fileIsPNG) {
		std::vector<unsigned char *> row_pointers (realPicHeight);
		for (int i = 0; i<realPicHeight; i++)
			row_pointers[i] = backdropTexture + 4*i*picWidth;

		png_read_image(png_ptr, (png_byte **) row_pointers.data ());
		png_read_end(png_ptr, NULL);
		png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
	} else {
		for (t2 = 0; t2 < realPicHeight; t2 ++) {
			t1 = 0;
			while (t1 < realPicWidth) {
				c = (unsigned short) get2bytes (fp);
				if (c & 32) {
					n = fgetc (fp) + 1;
					c -= 32;
				} else {
					n = 1;
				}
				while (n --) {
					target = backdropTexture + 4*picWidth*t2 + t1*4;
					if (c == transCol || c == 2015) {
						target[0] = (GLubyte) 0;
						target[1] = (GLubyte) 0;
						target[2] = (GLubyte) 0;
						target[3] = (GLubyte) 0;
					} else {
						target[0] = (GLubyte) redValue(c);
						target[1] = (GLubyte) greenValue(c);
						target[2] = (GLubyte) blueValue(c);
						target[3] = (GLubyte) 255;
					}
					t1++;
				}
			}
		}
	}

	gfx::Texture * tmpTex = gfx::renderer->createTexture (picWidth, picHeight, gfx::TEX_RGBA8, backdropTexture);
	gfx::renderer->setTextureWrap (tmpTex, gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
	gfx::Filter tmpFilter = gameSettings.antiAlias < 0 ? gfx::FILTER_LINEAR : gfx::FILTER_NEAREST;
	gfx::renderer->setTextureFilter (tmpTex, tmpFilter, tmpFilter);


	//glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);


	float btx1;
	float btx2;
	float bty1;
	float bty2;
	if (! NPOT_textures) {
		btx1 = backdropTexW * x / sceneWidth;
		btx2 = backdropTexW * (x+realPicWidth) / sceneWidth;
		bty1 = backdropTexH * y / sceneHeight;
		bty2 = backdropTexH * (y+realPicHeight) / sceneHeight;
	} else {
		btx1 = (float) x / sceneWidth;
		btx2 = (float) (x+realPicWidth) / sceneWidth;
		bty1 = (float) y / sceneHeight;
		bty2 = (float) (y+realPicHeight) / sceneHeight;
	}

	const GLfloat btexCoords[] = { 
		btx1, bty1,
		btx2, bty1,
		btx1, bty2,
		btx2, bty2 
	}; 

	setPixelCoords (true);

	int xoffset = 0;
	while (xoffset < realPicWidth) {
		int w = (realPicWidth-xoffset < viewportWidth) ? realPicWidth-xoffset : viewportWidth;

		int yoffset = 0;
		while (yoffset < realPicHeight) {
			int h = (realPicHeight-yoffset < viewportHeight) ? realPicHeight-yoffset : viewportHeight;

			gfx::Texture * copy = backdropExists ? copyOfBackdrop () : NULL;
			drawIntoTile (backdropTarget, x+xoffset, y+yoffset, 0, 0, w, h);

			const GLfloat vertices[] = { 
				(GLfloat)-xoffset, (GLfloat)-yoffset, 0., 
				(GLfloat)realPicWidth-xoffset, (GLfloat)-yoffset, 0., 
				(GLfloat)-xoffset, (GLfloat)-yoffset+realPicHeight, 0.,
				(GLfloat)realPicWidth-xoffset, (GLfloat)-yoffset+realPicHeight, 0.
			};

			const GLfloat texCoords[] = { 
				0.0f, 0.0f,
				texCoordW, 0.0f,
				0.0f, texCoordH,
				texCoordW, texCoordH
			}; 

			if (backdropExists) {
				// Render the sprite to the backdrop
				// (using mulitexturing, so the old backdrop is seen where alpha < 1.0)
				setPrimaryColor(1.0, 1.0, 1.0, 1.0);
				gfx::Texture * const textures[3] = { tmpTex, NULL, copy };
				gfx::Quad quad = { vertices, { texCoords, NULL, btexCoords } };
				gfx::renderer->draw (gfx::PipelineState (gfx::PROG_PASTE), engineUniforms (), textures, quad);
			} else {
				// It's all new - nothing special to be done.
				setPrimaryColor(1.0, 0.0, 0.0, 0.0);
				gfx::draw (gfx::PipelineState (gfx::PROG_TEXTURE), engineUniforms (), tmpTex, vertices, texCoords);
			}

			yoffset += viewportHeight;
		}

		xoffset += viewportWidth;
	}
	gfx::renderer->releaseTexture (tmpTex);

	drawIntoWindow ();
	setPixelCoords (false);

	backdropExists = true;
	return true;
}

bool mixHSI (FILE * fp, int x, int y) {
	int realPicWidth, realPicHeight;
	int picWidth;
	int picHeight;

	long file_pointer = ftell (fp);

	png_structp png_ptr;
	png_infop info_ptr, end_info;


	int fileIsPNG = true;

	// Is this a PNG file?
	char tmp[10];
	size_t bytes_read = fread(tmp, 1, 8, fp);
	if (bytes_read != 8 && ferror (fp)) {
		debugOut("Reading error in mixHSI.\n");
	}
    if (png_sig_cmp((png_byte *) tmp, 0, 8)) {
		// No, it's old-school HSI
		fileIsPNG = false;
		fseek(fp, file_pointer, SEEK_SET);

		picWidth = realPicWidth = get2bytes (fp);
		picHeight = realPicHeight = get2bytes (fp);
	} else {
		// Read the PNG header

		png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
		if (!png_ptr) {
			return false;
		}

		info_ptr = png_create_info_struct(png_ptr);
		if (!info_ptr) {
			png_destroy_read_struct(&png_ptr, (png_infopp) NULL, (png_infopp) NULL);
			return false;
		}

		end_info = png_create_info_struct(png_ptr);
		if (!end_info) {
			png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
			return false;
		}
		png_init_io(png_ptr, fp);		// Tell libpng which file to read
		png_set_sig_bytes(png_ptr, 8);	// 8 bytes already read

		png_read_info(png_ptr, info_ptr);

		png_uint_32 width, height;
		int bit_depth, color_type, interlace_type, compression_type, filter_method;
		png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, &compression_type, &filter_method);

		picWidth = realPicWidth = width;
		picHeight = realPicHeight = height;

		if (bit_depth < 8) png_set_packing(png_ptr);
		png_set_expand(png_ptr);
		if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png_ptr);
		if (bit_depth == 16) png_set_strip_16(png_ptr);

		png_set_add_alpha(png_ptr, 0xff, PNG_FILLER_AFTER);

		png_read_update_info(png_ptr, info_ptr);
		png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, &compression_type, &filter_method);

		//int rowbytes = png_get_rowbytes(png_ptr, info_ptr);

	}

	if (x == IN_THE_CENTRE) x = (sceneWidth - realPicWidth) >> 1;
	if (y == IN_THE_CENTRE) y = (sceneHeight - realPicHeight) >> 1;
	if (x < 0 || x + realPicWidth > sceneWidth || y < 0 || y + realPicHeight > sceneHeight) return false;

	float btx1, tx1;
	float btx2, tx2;
	float bty1, ty1;
	float bty2, ty2;

	if (! NPOT_textures) {
		tx1 = 0.0;
		ty1 = 0.0;
		tx2 = ((double)picWidth) / getNextPOT(picWidth);
		ty2 = ((double)picHeight) / getNextPOT(picHeight);
		picWidth = getNextPOT(picWidth);
		picHeight = getNextPOT(picHeight);
		btx1 = backdropTexW * x / sceneWidth;
		btx2 = backdropTexW * (x+realPicWidth) / sceneWidth;
		bty1 = backdropTexH * y / sceneHeight;
		bty2 = backdropTexH * (y+realPicHeight) / sceneHeight;
	} else {
		tx1 = 0.0;
		ty1 = 0.0;
		tx2 = 1.0;
		ty2 = 1.0;
		btx1 = (float) x / sceneWidth;
		btx2 = (float) (x+picWidth) / sceneWidth;
		bty1 = (float) y / sceneHeight;
		bty2 = (float) (y+picHeight) / sceneHeight;
	}

	const GLfloat texCoords[] = { 
		tx1, ty1,
		tx2, ty1,
		tx1, ty2,
		tx2, ty2
	}; 

	const GLfloat btexCoords[] = { 
		btx1, bty1,
		btx2, bty1,
		btx1, bty2,
		btx2, bty2
	}; 

	int t1, t2, n;
	unsigned short c;
	GLubyte * target;
	int32_t transCol = 63519;


	if (fileIsPNG) {
		std::vector<unsigned char *> row_pointers (realPicHeight);
		for (int i = 0; i<realPicHeight; i++)
			row_pointers[i] = backdropTexture + 4*i*picWidth;

		png_read_image(png_ptr, (png_byte **) row_pointers.data ());
		png_read_end(png_ptr, NULL);
		png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
	} else {
		for (t2 = 0; t2 < realPicHeight; t2 ++) {
			t1 = 0;
			while (t1 < realPicWidth) {
				c = (unsigned short) get2bytes (fp);
				if (c & 32) {
					n = fgetc (fp) + 1;
					c -= 32;
				} else {
					n = 1;
				}
				while (n --) {
					target = backdropTexture + 4*picWidth*t2 + t1*4;
					if (c == transCol || c == 2015) {
						target[0] = (GLubyte) 0;
						target[1] = (GLubyte) 0;
						target[2] = (GLubyte) 0;
						target[3] = (GLubyte) 0;
					} else {
						target[0] = (GLubyte) redValue(c);
						target[1] = (GLubyte) greenValue(c);
						target[2] = (GLubyte) blueValue(c);
						target[3] = (GLubyte) 255;
					}
					t1++;
				}
			}
		}
	}

	gfx::Texture * tmpTex = gfx::renderer->createTexture (picWidth, picHeight, gfx::TEX_RGBA8, backdropTexture);
	gfx::renderer->setTextureWrap (tmpTex, gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
	gfx::Filter tmpFilter = gameSettings.antiAlias < 0 ? gfx::FILTER_LINEAR : gfx::FILTER_NEAREST;
	gfx::renderer->setTextureFilter (tmpTex, tmpFilter, tmpFilter);


	//glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

	setPixelCoords (true);


	int xoffset = 0;
	while (xoffset < realPicWidth) {
		int w = (realPicWidth-xoffset < viewportWidth) ? realPicWidth-xoffset : viewportWidth;

		int yoffset = 0;
		while (yoffset < realPicHeight) {
			int h = (realPicHeight-yoffset < viewportHeight) ? realPicHeight-yoffset : viewportHeight;

			gfx::Texture * copy = copyOfBackdrop ();
			drawIntoTile (backdropTarget, (x<0) ? xoffset : x+xoffset, (y<0) ? yoffset : y+yoffset, (x<0) ? -x : 0, (y<0) ? -y : 0, w, h);

			// Render the sprite to the backdrop
			// (using mulitexturing, so the backdrop is seen where alpha < 1.0)
			setPrimaryColor(1.0, 1.0, 1.0, 0.5);

			const GLfloat vertices[] = { 
				(GLfloat)-xoffset, (GLfloat)-yoffset, 0., 
				(GLfloat)realPicWidth-xoffset, (GLfloat)-yoffset, 0., 
				(GLfloat)-xoffset, (GLfloat)-yoffset+realPicHeight, 0.,
				(GLfloat)realPicWidth-xoffset, (GLfloat)-yoffset+realPicHeight, 0.
			};

			gfx::Texture * const textures[3] = { tmpTex, NULL, copy };
			gfx::Quad quad = { vertices, { texCoords, NULL, btexCoords } };
			gfx::renderer->draw (gfx::PipelineState (gfx::PROG_PASTE), engineUniforms (), textures, quad);

			yoffset += viewportHeight;
		}

		xoffset += viewportWidth;
	}
	gfx::renderer->releaseTexture (tmpTex);
	drawIntoWindow ();
	setPixelCoords (false);
	return true;
}

void saveCorePNG  (FILE * writer, gfx::Texture * texture, int w, int h) {
	int tw, th;
	gfx::renderer->textureSize (texture, &tw, &th);

	GLubyte* image = new GLubyte [tw*th*4];
	if (! checkNew (image)) return;

	gfx::renderer->readTexture (texture, image);

	png_structp png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr) {
		fatal ("Error saving image!");
		return;
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr){
		png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
		fatal ("Error saving image!");
		return;
	}

	png_init_io(png_ptr, writer);

	png_set_IHDR(png_ptr, info_ptr, w, h,
				 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	std::vector<unsigned char *> row_pointers (h);

	for (int i = 0; i < h; i++) {
		row_pointers[i] = image + 4*i*tw;
	}

	png_set_rows(png_ptr, info_ptr, row_pointers.data ());
    png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

	delete [] image;
	image = NULL;
}

unsigned short rgbTo565 (const unsigned char * rgb) {
	// We keep the upper bits, as OpenGL on macOS and the OpenGL ES code of
	// the engine did when they read pixels in this format.
	unsigned short r = rgb[0] >> 3;
	unsigned short g = rgb[1] >> 2;
	unsigned short b = rgb[2] >> 3;
	return (r << 11) | (g << 5) | b;
}

void saveCoreHSI (FILE * writer, gfx::Texture * texture, int w, int h) {

	int tw, th;
	gfx::renderer->textureSize (texture, &tw, &th);

	GLushort* image = new GLushort [tw*th];
	if (! checkNew (image)) return;

	GLubyte * rgba = new GLubyte [tw*th*4];
	gfx::renderer->readTexture (texture, rgba);
	for (int i = 0; i < tw*th; i ++) {
		image[i] = rgbTo565 (rgba + i*4);
	}
	delete [] rgba;

	int x, y, lookAhead;
	unsigned short int * fromHere, * lookPointer;

	put2bytes (w, writer);
	put2bytes (h, writer);

	for (y = 0; y < h; y ++) {
		fromHere = image +(y*tw);
		x = 0;
		while (x < w) {
			lookPointer = fromHere + 1;
			for (lookAhead = x + 1; lookAhead < w; lookAhead ++) {
				if (lookAhead - x == 256) break;
				if (* fromHere != * lookPointer) break;
				lookPointer ++;
			}
			if (lookAhead == x + 1) {
				put2bytes ((* fromHere) & 65503, writer);
			} else {
				put2bytes (* fromHere | 32, writer);
				fputc (lookAhead - x - 1, writer);
			}
			fromHere = lookPointer;
			x = lookAhead;
		}
	}
	delete [] image;
	image = NULL;
}

void saveHSI (FILE * writer) {
	saveCorePNG  (writer, backdropTex, sceneWidth, sceneHeight);
}

bool getRGBIntoStack (unsigned int x, unsigned int y, stackHandler * sH) {
	if (x >= sceneWidth || y >= sceneHeight) {
		return fatal ("Co-ordinates are outside current scene!");
	}

	variable newValue;

	newValue.varType = SVT_NULL;

	gfx::renderer->readTexture (backdropTex, backdropTexture);

	GLubyte * target;
	if (! NPOT_textures) {
		target = backdropTexture + 4*getNextPOT(sceneWidth)*y + x*4;
	} else {
		target = backdropTexture + 4*sceneWidth*y + x*4;
	}

	setVariable (newValue, SVT_INT, target[2]);
	if (! addVarToStackQuick (newValue, sH -> first)) return false;
	sH -> last = sH -> first;

	setVariable (newValue, SVT_INT, target[1]);
	if (! addVarToStackQuick (newValue, sH -> first)) return false;

	setVariable (newValue, SVT_INT, target[0]);
	if (! addVarToStackQuick (newValue, sH -> first)) return false;

	return true;
}

void saveParallaxRecursive (parallaxLayer * me, FILE * fp) {
	if (me) {
		saveParallaxRecursive (me -> next, fp);
		fputc (1, fp);
		put2bytes (me->fileNum, fp);
		put2bytes (me ->fractionX, fp);
		put2bytes (me->fractionY, fp);
	}
}

