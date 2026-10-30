#include <png.h>

#include <string.h>
#include <math.h>
#include <stdlib.h>

#include "allfiles.h"

#include "fileset.h"
#include "people.h"
#include "sprites.h"
#include "moreio.h"
#include "newfatal.h"
#include "colours.h"
#include "backdrop.h"
#include "sludger.h"
#include "zbuffer.h"
#include "debug.h"
#include "graphics.h"
#include "objtypes.h"

#include "shaders.h"

extern zBufferData zBuffer;

extern inputType input;
extern int cameraX, cameraY;
extern float cameraZoom;

unsigned char currentBurnR = 0, currentBurnG = 0, currentBurnB = 0;


void forgetSpriteBank (spriteBank & forgetme) {

	for (int i = 0; i < forgetme.total; i ++) {
		delete [] forgetme.sprites[i].alpha;
	}

	for (int i = 0; i < forgetme.myPalette.numTextures; i ++) {
		gfx::renderer->destroyTexture (forgetme.myPalette.tex_names[i]);
	}
	if (forgetme.isFont) {
		for (int i = 0; i < forgetme.myPalette.numTextures; i ++) {
			gfx::renderer->destroyTexture (forgetme.myPalette.burnTex_names[i]);
		}
		delete [] forgetme.myPalette.burnTex_names;
		forgetme.myPalette.burnTex_names = NULL;
	}
	
	delete [] forgetme.myPalette.tex_names;
	forgetme.myPalette.tex_names = NULL;
	delete [] forgetme.myPalette.tex_w;
	forgetme.myPalette.tex_w = NULL;
	delete [] forgetme.myPalette.tex_h;
	forgetme.myPalette.tex_h = NULL;
	
	if (forgetme.myPalette.pal) {
		delete  [] forgetme.myPalette.pal;
		forgetme.myPalette.pal = NULL;
		delete  [] forgetme.myPalette.r;
		forgetme.myPalette.r = NULL;
		delete  [] forgetme.myPalette.g;
		forgetme.myPalette.g = NULL;
		delete  [] forgetme.myPalette.b;
		forgetme.myPalette.b = NULL;
	}

	delete [] forgetme.sprites;
	forgetme.sprites = NULL;
    
    
    // TODO: also remove sprite bank from allLoadedBanks
    // And add a function call for this function to the scripting language
}

bool reserveSpritePal (spritePalette & sP, int n) {
	if (sP.pal) {
		delete  [] sP.pal;
		delete  [] sP.r;
		delete  [] sP.g;
		delete  [] sP.b;
	}

	sP.pal = new unsigned short int [n];
	if (! checkNew (sP.pal)) return false;

	sP.r = new unsigned char [n];
	if (! checkNew (sP.r)) return false;
	sP.g = new unsigned char [n];
	if (! checkNew (sP.g)) return false;
	sP.b = new unsigned char [n];
	if (! checkNew (sP.b)) return false;
	sP.total = n;
	return (bool) (sP.pal != NULL) && (sP.r != NULL) && (sP.g != NULL) && (sP.b != NULL);
}

bool loadSpriteBank (int fileNum, spriteBank & loadhere, bool isFont) {
	int i, tex_num, total, picwidth, picheight, spriteBankVersion = 0, howmany=0, startIndex=0;
	int * totalwidth, * maxheight;
	int numTextures = 0;
	byte * data;

	setResourceForFatal (fileNum);
	if (! openFileFromNum (fileNum)) return fatal ("Can't open sprite bank / font");

	loadhere.isFont = isFont;

	total = get2bytes (bigDataFile);
	if (! total) {
		spriteBankVersion = fgetc (bigDataFile);
		if (spriteBankVersion == 1) {
			total = 0;
		} else {
			total = get2bytes (bigDataFile);
		}
	}

	if (total <= 0) return fatal ("No sprites in bank or invalid sprite bank file");
	if (spriteBankVersion > 3) return fatal ("Unsupported sprite bank file format");

	loadhere.total = total;
	loadhere.sprites = new sprite [total];
	if (! checkNew (loadhere.sprites)) return false;
	byte ** spriteData = new byte * [total];
	if (! checkNew (spriteData)) return false;

	totalwidth = new int[total];
	if (! checkNew (totalwidth)) return false;

	maxheight = new int[total];
	if (! checkNew (maxheight)) return false;

	loadhere.myPalette.tex_names = new gfx::Texture * [total];
	if (! checkNew (loadhere.myPalette.tex_names)) return false;

	if (isFont) {
		loadhere.myPalette.burnTex_names = new gfx::Texture * [total];
		if (! checkNew (loadhere.myPalette.burnTex_names)) return false;
	}
	loadhere.myPalette.tex_w = new int [total];
	if (! checkNew (loadhere.myPalette.tex_w)) return false;
	loadhere.myPalette.tex_h = new int [total];
	if (! checkNew (loadhere.myPalette.tex_h)) return false;
	
	if (spriteBankVersion && spriteBankVersion < 3) {
		howmany = fgetc (bigDataFile);
		startIndex = 1;
	}

	totalwidth[0] = maxheight[0] = 1;

	for (i = 0; i < total; i ++) {
		switch (spriteBankVersion) {
			case 3:
			{
				loadhere.sprites[i].xhot = getSigned (bigDataFile);
				loadhere.sprites[i].yhot = getSigned (bigDataFile);

				png_structp png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
				if (!png_ptr) {
					return fatal ("Can't open sprite bank / font.");
				}

				png_infop info_ptr = png_create_info_struct(png_ptr);
				if (!info_ptr) {
					png_destroy_read_struct(&png_ptr, (png_infopp) NULL, (png_infopp) NULL);
					return fatal ("Can't open sprite bank / font.");
				}

				png_infop end_info = png_create_info_struct(png_ptr);
				if (!end_info) {
					png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
					return fatal ("Can't open sprite bank / font.");
				}
				png_init_io(png_ptr, bigDataFile);		// Tell libpng which file to read
				png_set_sig_bytes(png_ptr, 8);			// No sig

				png_read_info(png_ptr, info_ptr);

				png_uint_32 width, height;
				int bit_depth, color_type, interlace_type, compression_type, filter_method;
				png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, &compression_type, &filter_method);

				int rowbytes = png_get_rowbytes(png_ptr, info_ptr);

				unsigned char * row_pointers[height];
				spriteData[i] = new unsigned char [rowbytes*height];
				if (! checkNew (spriteData[i])) return false;

				for (unsigned int row = 0; row<height; row++)
					row_pointers[row] = spriteData[i] + row*rowbytes;

				png_read_image(png_ptr, (png_byte **) row_pointers);
				png_read_end(png_ptr, NULL);
				png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);

				picwidth = loadhere.sprites[i].width = width;
				picheight = loadhere.sprites[i].height = height;
				break;
			}
			case 2:
			picwidth = get2bytes (bigDataFile);
			picheight = get2bytes (bigDataFile);
			loadhere.sprites[i].xhot = getSigned (bigDataFile);
			loadhere.sprites[i].yhot = getSigned (bigDataFile);
			break;

			default:
			picwidth = (byte) fgetc (bigDataFile);
			picheight = (byte) fgetc (bigDataFile);
			loadhere.sprites[i].xhot = fgetc (bigDataFile);
			loadhere.sprites[i].yhot = fgetc (bigDataFile);
			break;
		}

		if (((picwidth>511) && (totalwidth[numTextures] + picwidth < 2047)) || ((picwidth<511) && (totalwidth[numTextures] + picwidth < 511))) {
			loadhere.sprites[i].tex_x = totalwidth[numTextures];
			totalwidth[numTextures] += (loadhere.sprites[i].width = picwidth) + 1;
			if ((loadhere.sprites[i].height = picheight)+2 > maxheight[numTextures]) maxheight[numTextures] = picheight+2;
		} else {
			numTextures++;
			if (numTextures > 255) return fatal ("Can't open sprite bank / font - it's too big.");
			loadhere.sprites[i].tex_x = 0;
			totalwidth[numTextures] = (loadhere.sprites[i].width = picwidth);
			maxheight[numTextures] = loadhere.sprites[i].height = picheight;
		}
		loadhere.sprites[i].texNum = numTextures;

		if (spriteBankVersion < 3) {
			data = (byte *) new byte [picwidth * (picheight + 1)];
			if (! checkNew (data)) return false;
			int ooo = picwidth * picheight;
			for (int tt = 0; tt < picwidth; tt ++) {
				data[ooo ++] = 0;
			}
			spriteData[i] = data;
			switch (spriteBankVersion) {
				case 2:			// RUN LENGTH COMPRESSED DATA
				{
					unsigned size = picwidth * picheight;
					unsigned pip = 0;

					while (pip < size) {
						byte col = fgetc (bigDataFile);
						int looper;

						if (col > howmany) {
							col -= howmany + 1;
							looper = fgetc (bigDataFile) + 1;
						} else looper = 1;

						while (looper --) {
							data[pip ++] = col;
						}
					}
				}
				break;

				default:		// RAW DATA
				size_t bytes_read = fread (data, picwidth, picheight, bigDataFile);
				if (bytes_read != picwidth * picheight && ferror (bigDataFile)) {
					debugOut("Reading error in loadSpriteBank.\n");
				}
				break;
			}
		}
	}
	numTextures++;


	if (! spriteBankVersion) {
		howmany = fgetc (bigDataFile);
		startIndex = fgetc (bigDataFile);
	}

	if (spriteBankVersion < 3) {
		if (! reserveSpritePal (loadhere.myPalette, howmany + startIndex)) return false;

		for (i = 0; i < howmany; i ++) {
			loadhere.myPalette.r[i + startIndex] = (byte) fgetc (bigDataFile);
			loadhere.myPalette.g[i + startIndex] = (byte) fgetc (bigDataFile);
			loadhere.myPalette.b[i + startIndex] = (byte) fgetc (bigDataFile);
			loadhere.myPalette.pal[i + startIndex] = makeColour (loadhere.myPalette.r[i + startIndex], loadhere.myPalette.g[i + startIndex], loadhere.myPalette.b[i + startIndex]);
		}
	}

	loadhere.myPalette.originalRed = loadhere.myPalette.originalGreen = loadhere.myPalette.originalBlue = 255;

	loadhere.myPalette.numTextures = numTextures;
	GLubyte * tmp[numTextures];
	GLubyte * tmp2[numTextures];
	for (tex_num = 0; tex_num < numTextures; tex_num++) {
		if (! NPOT_textures) {
			totalwidth[tex_num] = getNextPOT(totalwidth[tex_num]);
			maxheight[tex_num] = getNextPOT(maxheight[tex_num]);
		}
		tmp[tex_num] = new GLubyte [(maxheight[tex_num]+1)*totalwidth[tex_num]*4];
		if (! checkNew (tmp[tex_num])) return false;
		memset (tmp[tex_num], 0, maxheight[tex_num]*totalwidth[tex_num]*4);
		if (isFont) {
			tmp2[tex_num] = new GLubyte [(maxheight[tex_num]+1)*totalwidth[tex_num]*4];
			if (! checkNew (tmp2[tex_num])) return false;
			memset (tmp2[tex_num], 0, maxheight[tex_num]*totalwidth[tex_num]*4);
		}
		loadhere.myPalette.tex_w[tex_num] = totalwidth[tex_num];
		loadhere.myPalette.tex_h[tex_num] = maxheight[tex_num];
	}

	int fromhere;
	unsigned char s;

	for (i = 0; i < total; i ++) {
		fromhere = 0;

		int transColour = -1;
		if (spriteBankVersion < 3) {
			int size = loadhere.sprites[i].height * loadhere.sprites[i].width;
			while (fromhere < size) {
				s = spriteData[i][fromhere++];
				if (s) {
					transColour = s;
					break;
				}
			}
			fromhere = 0;
		}

		for (int y = 1; y < 1 + loadhere.sprites[i].height; y ++) {
			for (int x = loadhere.sprites[i].tex_x; x < loadhere.sprites[i].tex_x+loadhere.sprites[i].width; x ++) {
				GLubyte * target = tmp[loadhere.sprites[i].texNum] + 4*totalwidth[loadhere.sprites[i].texNum]*y + x*4;
				if (spriteBankVersion < 3) {
					s = spriteData[i][fromhere++];
					if (s) {
						target[0] = (GLubyte) loadhere.myPalette.r[s];
						target[1] = (GLubyte) loadhere.myPalette.g[s];
						target[2] = (GLubyte) loadhere.myPalette.b[s];
						target[3] = (GLubyte) 255;
						transColour = s;
					} else if (transColour >=0) {
						target[0] = (GLubyte) loadhere.myPalette.r[transColour];
						target[1] = (GLubyte) loadhere.myPalette.g[transColour];
						target[2] = (GLubyte) loadhere.myPalette.b[transColour];
						target[3] = (GLubyte) 0;
					}
					if (isFont) {
						target = tmp2[loadhere.sprites[i].texNum] + 4*totalwidth[loadhere.sprites[i].texNum]*y + x*4;
						target[0] = (GLubyte) 255;
						target[1] = (GLubyte) 255;
						target[2] = (GLubyte) 255;
						if (s)
							target[3] = (GLubyte) loadhere.myPalette.r[s];
						/*else
							target[3] = (GLubyte) 0;*/
					}
				} else {
					target[0] = (GLubyte) spriteData[i][fromhere++];
					target[1] = (GLubyte) spriteData[i][fromhere++];
					target[2] = (GLubyte) spriteData[i][fromhere++];
					target[3] = (GLubyte) spriteData[i][fromhere++];
				}
			}
		}
		delete[] spriteData[i];
	}
	delete[] spriteData;
	spriteData = NULL;


	// We keep the alpha of the sprites, but not of fonts, to find out which
	// character the mouse is over. Each sprite starts in the second row of
	// its texture.
	for (i = 0; i < total; i ++) {
		sprite & sp = loadhere.sprites[i];
		sp.alpha = NULL;
		if (isFont) continue;
		sp.alpha = new unsigned char [sp.width * sp.height];
		if (! checkNew (sp.alpha)) return false;
		const GLubyte * pixels = tmp[sp.texNum];
		int stride = totalwidth[sp.texNum];
		for (int y = 0; y < sp.height; y ++) {
			for (int x = 0; x < sp.width; x ++) {
				sp.alpha[y * sp.width + x] = pixels[4 * (stride * (y + 1) + sp.tex_x + x) + 3];
			}
		}
	}

	gfx::Filter filter = gameSettings.antiAlias < 0 ? gfx::FILTER_LINEAR : gfx::FILTER_NEAREST;

	for (tex_num = 0; tex_num < numTextures; tex_num++) {

		gfx::Texture * t = gfx::renderer->createTexture (totalwidth[tex_num], maxheight[tex_num], gfx::TEX_RGBA8, tmp[tex_num]);
		gfx::renderer->setTextureWrap (t, gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
		gfx::renderer->setTextureFilter (t, filter, filter);
		loadhere.myPalette.tex_names[tex_num] = t;

		delete[] tmp[tex_num];
		tmp[tex_num] = NULL;

		if (isFont) {
			gfx::Texture * b = gfx::renderer->createTexture (totalwidth[tex_num], maxheight[tex_num], gfx::TEX_RGBA8, tmp2[tex_num]);
			gfx::renderer->setTextureWrap (b, gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
			gfx::renderer->setTextureFilter (b, filter, filter);
			loadhere.myPalette.burnTex_names[tex_num] = b;

			delete[] tmp2[tex_num];
			tmp2[tex_num] = NULL;
		}
	}

	finishAccess ();
	setResourceForFatal (-1);

	return true;
}

void pasteSpriteToBackDrop (int x1, int y1, sprite & single, const spritePalette & fontPal) {

	float tx1 = (float)(single.tex_x) / fontPal.tex_w[single.texNum];
	float ty1 = 0.0;
	float tx2 = (float)(single.tex_x + single.width) / fontPal.tex_w[single.texNum];
	float ty2 = (float)(single.height)/fontPal.tex_h[single.texNum];

	float btx1;
	float btx2;
	float bty1;
	float bty2;

	int diffX = single.width;
	int diffY = single.height;

	x1 -= single.xhot;
	y1 -= single.yhot;

	if (! NPOT_textures) {
		btx1 = backdropTexW * x1 / sceneWidth;
		btx2 = backdropTexW * (x1+single.width) / sceneWidth;
		bty1 = backdropTexH * y1 / sceneHeight;
		bty2 = backdropTexH * (y1+single.height) / sceneHeight;
	} else {
		btx1 = (float) x1 / sceneWidth;
		btx2 = (float) (x1+single.width) / sceneWidth;
		bty1 = (float) y1 / sceneHeight;
		bty2 = (float) (y1+single.height) / sceneHeight;
	}

	const GLfloat btexCoords[] = { 
		btx1, bty1,
		btx2, bty1,
		btx1, bty2,
		btx2, bty2
	}; 

	if (x1 < 0) diffX += x1;
	if (y1 < 0) diffY += y1;
	if (x1 + diffX > sceneWidth) diffX = sceneWidth - x1;
	if (y1 + diffY > sceneHeight) diffY = sceneHeight - y1;
	if (diffX < 0) return;
	if (diffY < 0) return;

	setPixelCoords (true);

	int xoffset = 0;
	while (xoffset < diffX) {
		int w = (diffX-xoffset < viewportWidth) ? diffX-xoffset : viewportWidth;

		int yoffset = 0;
		while (yoffset < diffY) {
			int h = (diffY-yoffset < viewportHeight) ? diffY-yoffset : viewportHeight;

			// Render the sprite to the backdrop
			// (using mulitexturing, so the backdrop is seen where alpha < 1.0)
			gfx::Texture * copy = copyOfBackdrop ();
			drawIntoTile (backdropTarget, (x1<0) ? xoffset : x1+xoffset, (y1<0) ? yoffset : y1+yoffset, (x1<0) ? -x1 : 0, (y1<0) ? -y1 : 0, w, h);

			setPrimaryColor(fontPal.originalRed/255.f, fontPal.originalGreen/255.f, fontPal.originalBlue/255.f, 1.0f);

			const GLfloat vertices[] = { 
				(GLfloat)-xoffset, (GLfloat)-yoffset, 0.,
				(GLfloat)single.width-xoffset, (GLfloat)-yoffset, 0.,
				(GLfloat)-xoffset, (GLfloat)single.height-yoffset, 0.,
				(GLfloat)single.width-xoffset, (GLfloat)single.height-yoffset, 0.
			};

			const GLfloat texCoords[] = { 
				tx1, ty1,
				tx2, ty1,
				tx1, ty2,
				tx2, ty2
			}; 

			gfx::Texture * const textures[3] = { fontPal.tex_names[single.texNum], NULL, copy };
			gfx::Quad quad = { vertices, { texCoords, NULL, btexCoords } };
			gfx::renderer->draw (gfx::PipelineState (gfx::PROG_PASTE), gfx::DrawUniforms (), textures, quad);

			yoffset += viewportHeight;
		}
		xoffset += viewportWidth;
	}
	drawIntoWindow ();
	setPixelCoords (false);
}

void burnSpriteToBackDrop (int x1, int y1, sprite & single, const spritePalette & fontPal) {

	float tx1 = (float)(single.tex_x - 0.5) / fontPal.tex_w[single.texNum];
	float ty1 = 0.0;
	float tx2 = (float)(single.tex_x + single.width + 0.5) / fontPal.tex_w[single.texNum];
	float ty2 = (float)(single.height+2)/fontPal.tex_h[single.texNum];

	const GLfloat spriteTexCoords[] = { 
		tx1, ty1,
		tx2, ty1,
		tx1, ty2,
		tx2, ty2
	}; 

	x1 -= single.xhot;
	y1 -= single.yhot-1;

	float bx1 = (float)x1*backdropTexW/sceneWidth;
	float by1 = (float)y1*backdropTexH/sceneHeight;
	float bx2 = (float)(x1+single.width-1)*backdropTexW/sceneWidth;
	float by2 = (float)(y1+single.height-1)*backdropTexH/sceneHeight;

	const GLfloat backdropTexCoords[] = { 
		bx1, by1,
		bx2, by1,
		bx1, by2,
		bx2, by2
	};

	const GLfloat backdropTexCoords2[] = { 
		0.0f, 0.0f,
		backdropTexW, 0.0f,
		0.0f, backdropTexH,
		backdropTexW, backdropTexH
	}; 

	int diffX = single.width+1;
	int diffY = single.height+2;

	if (x1 < 0) diffX += x1;
	if (y1 < 0) diffY += y1;
	if (x1 + diffX > sceneWidth) diffX = sceneWidth - x1;
	if (y1 + diffY > sceneHeight) diffY = sceneHeight - y1;
	if (diffX < 0) return;
	if (diffY < 0) return;

	setPixelCoords (true);
	setPrimaryColor(currentBurnR/255.f, currentBurnG/255.f, currentBurnB/255.f, 1.0f);

	GLfloat xoffset = 0.0f;
	while (xoffset < diffX) {
		int w = (diffX-xoffset < viewportWidth) ? diffX-xoffset : viewportWidth;

		GLfloat yoffset = 0.0f;
		while (yoffset < diffY) {
			int h = (diffY-yoffset < viewportHeight) ? diffY-yoffset : viewportHeight;

			const GLfloat backdropVertices[] = { 
				-x1-xoffset, -y1+yoffset, 0.0f, 
				sceneWidth-1.0f-x1-xoffset, -y1+yoffset, 0.0f, 
				-x1-xoffset, sceneHeight-1.0f-y1+yoffset, 0.0f,
				sceneWidth-1.0f-x1-xoffset, sceneHeight-1.0f-y1+yoffset, 0.0f
			};

			const GLfloat spriteVertices[] = { 
				-xoffset, -yoffset, 0.0f, 
				single.width-1-xoffset, -yoffset, 0.0f, 
				-xoffset, single.height-1-yoffset, 0.0f,
				single.width-1-xoffset, single.height-1-yoffset, 0.0f
			};

			gfx::Texture * copy = copyOfBackdrop ();
			drawIntoTile (backdropTarget, (x1<0) ? xoffset : x1+xoffset, (y1<0) ? yoffset : y1+yoffset, 0, 0, w, h);

			gfx::draw (gfx::PipelineState (gfx::PROG_TEXTURE), gfx::DrawUniforms (), copy, backdropVertices, backdropTexCoords2);

//FIXME: Test this some more. Also pasting the backdrop again is not strictly necessary but allows using the paste shader.
			gfx::Texture * const textures[3] = { fontPal.burnTex_names[single.texNum], NULL, copy };
			gfx::Quad quad = { spriteVertices, { spriteTexCoords, NULL, backdropTexCoords } };
			gfx::renderer->draw (gfx::PipelineState (gfx::PROG_PASTE), gfx::DrawUniforms (), textures, quad);

			yoffset += viewportHeight;
		}
		xoffset += viewportWidth;
	}
	drawIntoWindow ();
	setPixelCoords (false);
}


void fontSprite (bool flip, int x, int y, sprite & single, const spritePalette & fontPal) {

	float tx1 = (float)(single.tex_x-0.5) / fontPal.tex_w[single.texNum];
	float ty1 = 0.0;
	float tx2 = (float)(single.tex_x + single.width+(flip ? 1.0 : 0.5)) / fontPal.tex_w[single.texNum];
	float ty2 = (float)(single.height+2)/fontPal.tex_h[single.texNum];

	float x1 = (float)x - (float)single.xhot/cameraZoom;
	float y1 = (float)y - (float)single.yhot/cameraZoom;
	float x2 = x1 + (float)single.width/cameraZoom;
	float y2 = y1 + (float)single.height/cameraZoom;

	GLfloat vertices[] = { 
		x1, y1, 0.0f, 
		x2, y1, 0.0f, 
		x1, y2, 0.0f,
		x2, y2, 0.0f
	};
	if (flip) {
		vertices[0] = x2;
		vertices[3] = x1;
		vertices[6] = x2;
		vertices[9] = x1;
	}

	const GLfloat texCoords[] = { 
		tx1, ty1,
		tx2, ty1,
		tx1, ty2,
		tx2, ty2
	}; 

	//glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); // GL_MODULATE instead of decal mixes the colours!
	setPrimaryColor(fontPal.originalRed/255.f, fontPal.originalGreen/255.f, fontPal.originalBlue/255.f, 1.0f);

	gfx::DrawUniforms uniforms;
	uniforms.antialias = gameSettings.antiAlias == 1;
	gfx::draw (gfx::PipelineState (gfx::PROG_SMART_SCALER, gfx::BLEND_ALPHA), uniforms, fontPal.tex_names[single.texNum], vertices, texCoords);
}

void fontSprite (int x, int y, sprite & single, const spritePalette & fontPal) {
	fontSprite (false, x, y, single, fontPal);
}

void flipFontSprite (int x, int y, sprite & single, const spritePalette & fontPal) {
	fontSprite (true, x, y, single, fontPal);
}




unsigned char curLight[3];

void setDrawMode (onScreenPerson * thisPerson) {
	if (thisPerson->colourmix) {
		//glEnable(GL_COLOR_SUM); FIXME: replace line?
		setSecondaryColor(curLight[0]*thisPerson->r*thisPerson->colourmix/65025/255.f, curLight[1]*thisPerson->g*thisPerson->colourmix/65025/255.f, curLight[2]*thisPerson->b*thisPerson->colourmix/65025/255.f, 1.0f);
	}

	//glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	setPrimaryColor(curLight[0]*(255-thisPerson->colourmix)/65025.f, curLight[1]*(255-thisPerson->colourmix)/65025.f, curLight[2]*(255-thisPerson->colourmix)/65025.f, 1.0f - thisPerson->transparency/255.f);
}


// Is the mouse over an opaque pixel of a sprite that we draw from (x1, y1)
// to (x2, y2), and not over a part of the z-buffer in front of it? We take
// the pixel that the centre of the pixel under the mouse samples, as
// nearest filtering does. Characters on z-buffer panel `panel` are behind
// the parts of the z-buffer of higher panels.
static bool mouseOverSprite (const sprite & single, float x1, float y1, float x2, float y2, bool mirror, bool behindZBuffer, int panel) {
	if (! single.alpha || x2 <= x1 || y2 <= y1) return false;
	int sx = (int) floorf ((input.mouseX + 0.5f - x1) / (x2 - x1) * single.width);
	int sy = (int) floorf ((input.mouseY + 0.5f - y1) / (y2 - y1) * single.height);
	if (sx < 0 || sy < 0 || sx >= single.width || sy >= single.height) return false;
	if (mirror) sx = single.width - 1 - sx;
	if (! single.alpha[sy * single.width + sx]) return false;
	if (behindZBuffer) {
		int p = zBufferPanelAt (input.mouseX + cameraX, input.mouseY + cameraY);
		if (p > zBuffer.numPanels - 1) p = zBuffer.numPanels - 1;
		if (p > panel) return false;
	}
	return true;
}

bool scaleSprite (sprite & single, const spritePalette & fontPal, onScreenPerson * thisPerson, bool mirror) {

	float x = thisPerson->x;
	float y = thisPerson->y;

	float scale = thisPerson-> scale;
	bool light = ! (thisPerson->extra & EXTRA_NOLITE);

	if (scale <= 0.05) return false;

	float tx1 = (float)(single.tex_x) / fontPal.tex_w[single.texNum];
	float ty1 = (float) 1.0/fontPal.tex_h[single.texNum];
	float tx2 = (float)(single.tex_x + single.width) / fontPal.tex_w[single.texNum];
	float ty2 = (float)(single.height+1)/fontPal.tex_h[single.texNum];

	int diffX = (int)(((float)single.width) * scale);
	int diffY = (int)(((float)single.height) * scale);

	GLfloat x1, y1, x2, y2;

	if (thisPerson -> extra & EXTRA_FIXTOSCREEN) {
		x = x / cameraZoom;
		y = y / cameraZoom;
		if (single.xhot < 0)
			x1 = x - (int)((mirror ? (float) (single.width - single.xhot) : (float)(single.xhot+1) ) * scale/cameraZoom);
		else
			x1 = x - (int)((mirror ? (float) (single.width - (single.xhot+1)) : (float)single.xhot ) * scale / cameraZoom);
		y1 = y - (int)((single.yhot - thisPerson->floaty) * scale / cameraZoom);
		x2 = x1 + (int)(diffX / cameraZoom);
		y2 = y1 + (int)(diffY / cameraZoom);
	} else {
		x -= cameraX;
		y -= cameraY;
		if (single.xhot < 0)
			x1 = x - (int)((mirror ? (float) (single.width - single.xhot) : (float)(single.xhot+1) ) * scale);
		else
			x1 = x - (int)((mirror ? (float) (single.width - (single.xhot+1)) : (float)single.xhot ) * scale);
		y1 = y - (int)((single.yhot - thisPerson->floaty) * scale);
		x2 = x1 + diffX;
		y2 = y1 + diffY;
	}

	GLfloat z;
	bool behindZBuffer = (! (thisPerson->extra & EXTRA_NOZB)) && zBuffer.numPanels;
	int panel = 0;

	if (behindZBuffer) {
		int i;
		for (i = 1; i<zBuffer.numPanels; i++) {
			if (zBuffer.panel[i] >= y + cameraY) {
				i--;
				break;
			}
		}
		panel = i;
		z = 0.999 - (double) i * (1.0 / 128.0);
	} else {
		z = -0.5;
	}

	float ltx1, ltx2, lty1, lty2;
	if (! NPOT_textures) {
		ltx1 = lightMap.texW * (x1+cameraX) / sceneWidth;
		ltx2 = lightMap.texW * (x2+cameraX) / sceneWidth;
		lty1 = lightMap.texH * (y1+cameraY) / sceneHeight;
		lty2 = lightMap.texH * (y2+cameraY) / sceneHeight;
	} else {
		ltx1 = (float) (x1+cameraX) / sceneWidth;
		ltx2 = (float) (x2+cameraX) / sceneWidth;
		lty1 = (float) (y1+cameraY) / sceneHeight;
		lty2 = (float) (y2+cameraY) / sceneHeight;
	}

	const GLfloat ltexCoords[] = { 
		ltx1, lty1,
		ltx2, lty1,
		ltx1, lty2,
		ltx2, lty2
	}; 

	if (light && lightMap.data) {
		if (lightMapMode == LIGHTMAPMODE_HOTSPOT) {
			int lx=(int)(x+cameraX);
			int ly=(int)(y+cameraY);

			if (lx<0) lx = 0;
			else if (lx>=sceneWidth) lx = sceneWidth-1;
			if (ly<0) ly = 0;
			else if (ly>=sceneHeight) ly = sceneHeight-1;

			GLubyte *target;
			if (! NPOT_textures) {
				target = lightMap.data + (ly*getNextPOT(sceneWidth) + lx)*4;
			} else {
				target = lightMap.data + (ly*sceneWidth + lx)*4;
			}
			curLight[0] = target[0];
			curLight[1] = target[1];
			curLight[2] = target[2];
		} else if (lightMapMode == LIGHTMAPMODE_PIXEL) {
			curLight[0] = curLight[1] = curLight[2] = 255;
		}
	} else {
		curLight[0] = curLight[1] = curLight[2] = 255;
	}
	setDrawMode (thisPerson);

	gfx::DrawUniforms uniforms;
	uniforms.useLightTexture = light && lightMapMode == LIGHTMAPMODE_PIXEL && lightMap.data;
	uniforms.antialias = gameSettings.antiAlias == 1;

	const GLfloat vertices[] = { 
		x1, y1, z, 
		x2, y1, z, 
		x1, y2, z,
		x2, y2, z
	};

	if (! mirror) {
		GLfloat tx3 = tx1;
		tx1 = tx2;
		tx2 = tx3;
	}
	const GLfloat texCoords[] = { 
		tx2, ty1,
		tx1, ty1,
		tx2, ty2,
		tx1, ty2
	}; 

	// Characters are drawn with the depth test, behind the z-buffer.
	gfx::Texture * const textures[3] = { fontPal.tex_names[single.texNum], uniforms.useLightTexture ? lightMap.tex : NULL, NULL };
	gfx::Quad quad = { vertices, { texCoords, ltexCoords, NULL } };
	gfx::renderer->draw (gfx::PipelineState (gfx::PROG_SMART_SCALER, gfx::BLEND_ALPHA, gfx::DEPTH_TEST), uniforms, textures, quad);

	setSecondaryColor(0., 0., 0.,1.);
	//glDisable(GL_COLOR_SUM); FIXME: replace line?

	// Are we pointing at the sprite?
	if (input.mouseX >= x1 && input.mouseX <= x2 && input.mouseY >= y1 && input.mouseY <= y2) {
		if (thisPerson->extra & EXTRA_RECTANGULAR) return true;
		return mouseOverSprite (single, x1, y1, x2, y2, mirror, behindZBuffer, panel);
	}
	return false;
}

// Paste a scaled sprite onto the backdrop
void fixScaleSprite (int x, int y, sprite & single, const spritePalette & fontPal, onScreenPerson * thisPerson, int camX, int camY, bool mirror) {

	float scale = thisPerson-> scale;
	bool useZB = ! (thisPerson->extra & EXTRA_NOZB);
	bool light = ! (thisPerson->extra & EXTRA_NOLITE);

	if (scale <= 0.05) return;

	float tx1 = (float)(single.tex_x) / fontPal.tex_w[single.texNum];
	float ty1 = (float) 1.0/fontPal.tex_h[single.texNum];//0.0;
	float tx2 = (float)(single.tex_x + single.width) / fontPal.tex_w[single.texNum];
	float ty2 = (float)(single.height+1)/fontPal.tex_h[single.texNum];

	int diffX = (int)(((float)single.width) * scale);
	int diffY = (int)(((float)single.height) * scale);
	int x1;
	if (single.xhot < 0)
		x1 = x - (int)((mirror ? (float) (single.width - single.xhot) : (float)(single.xhot+1) ) * scale);
	else
		x1 = x - (int)((mirror ? (float) (single.width - (single.xhot+1)) : (float)single.xhot ) * scale);
	int y1 = y - (int)((single.yhot - thisPerson->floaty) * scale);

	float spriteWidth = diffX;
	float spriteHeight = diffY;
	if (x1 < 0) diffX += x1;
	if (y1 < 0) diffY += y1;
	if (x1 + diffX > sceneWidth) diffX = sceneWidth - x1;
	if (y1 + diffY > sceneHeight) diffY = sceneHeight - y1;
	if (diffX < 0) return;
	if (diffY < 0) return;

	GLfloat z;


	if (useZB && zBuffer.numPanels) {
		int i;
		for (i = 1; i<zBuffer.numPanels; i++) {
			if (zBuffer.panel[i] >= y + cameraY) {
				i--;
				break;
			}
		}
		z = 0.999 - (double) i * (1.0 / 128.0);
	} else {
		z = -0.5;
	}

	float ltx1, btx1;
	float ltx2, btx2;
	float lty1, bty1;
	float lty2, bty2;
	if (! NPOT_textures) {
		ltx1 = lightMap.texW * x1 / sceneWidth;
		ltx2 = lightMap.texW * (x1+spriteWidth) / sceneWidth;
		lty1 = lightMap.texH * y1 / sceneHeight;
		lty2 = lightMap.texH * (y1+spriteHeight) / sceneHeight;
		btx1 = backdropTexW * x1 / sceneWidth;
		btx2 = backdropTexW * (x1+spriteWidth) / sceneWidth;
		bty1 = backdropTexH * y1 / sceneHeight;
		bty2 = backdropTexH * (y1+spriteHeight) / sceneHeight;
	} else {
		btx1 = ltx1 = (float) x1 / sceneWidth;
		btx2 = ltx2 = (float) (x1+spriteWidth) / sceneWidth;
		bty1 = lty1 = (float) y1 / sceneHeight;
		bty2 = lty2 = (float) (y1+spriteHeight) / sceneHeight;
	}

	const GLfloat ltexCoords[] = { 
		ltx1, lty1,
		ltx2, lty1,
		ltx1, lty2,
		ltx2, lty2
	}; 

	const GLfloat btexCoords[] = { 
		btx1, bty1,
		btx2, bty1,
		btx1, bty2,
		btx2, bty2
	}; 

	if (light && lightMap.data) {
		if (lightMapMode == LIGHTMAPMODE_HOTSPOT) {
			int lx=x+cameraX;
			int ly=y+cameraY;
			if (lx<0 || ly<0 || lx>=sceneWidth || ly>=sceneHeight) {
				curLight[0] = curLight[1] = curLight[2] = 255;
			} else {
				GLubyte *target = lightMap.data + (ly*sceneWidth + lx)*4;
				curLight[0] = target[0];
				curLight[1] = target[1];
				curLight[2] = target[2];
			}
		} else if (lightMapMode == LIGHTMAPMODE_PIXEL) {
			curLight[0] = curLight[1] = curLight[2] = 255;
		}
	} else {
		curLight[0] = curLight[1] = curLight[2] = 255;
	}

	setPixelCoords (true);
	GLfloat xoffset = 0.0f;
	while (xoffset < diffX) {
		int w = (diffX-xoffset < viewportWidth) ? (int) (diffX-xoffset) : viewportWidth;

		GLfloat yoffset = 0.0f;
		while (yoffset < diffY) {

			int h = (diffY-yoffset< viewportHeight) ? (int) (diffY-yoffset) : viewportHeight;

			// Render the scene - first the old backdrop (so that it'll show through when the z-buffer is active
			gfx::renderer->setTextureFilter (backdropTex, gfx::FILTER_NEAREST, gfx::FILTER_NEAREST);
			gfx::Texture * copy = copyOfBackdrop ();
			drawIntoTile (backdropTarget, (x1<0) ? xoffset : x1+xoffset, (y1<0) ? yoffset : y1+yoffset, (x1<0) ? -x1 : 0, (y1<0) ? -y1 : 0, w, h);

			const GLfloat vertices[] = { 
				-x1-xoffset, -y1-yoffset, 0.0f, 
				sceneWidth-x1-xoffset, -y1-yoffset, 0.0f, 
				-x1-xoffset, sceneHeight-y1-yoffset, 0.0f,
				sceneWidth-x1-xoffset, sceneHeight-y1-yoffset, 0.0f
			};

			const GLfloat texCoords[] = { 
				0.0f, 0.0f,
				backdropTexW, 0.0f,
				0.0f, backdropTexH,
				backdropTexW, backdropTexH
			}; 

			gfx::draw (gfx::PipelineState (gfx::PROG_TEXTURE), gfx::DrawUniforms (), copy, vertices, texCoords);

			// The z-buffer
			if (useZB) {
				drawZBuffer((int) (x1+xoffset+camX), (int) (y1+yoffset+camY));
			}

			// Then the sprite
			setDrawMode (thisPerson);

			const GLfloat vertices2[] = { 
				-xoffset, -yoffset, z, 
				spriteWidth-xoffset, -yoffset, z, 
				-xoffset, spriteHeight-yoffset, z,
				spriteWidth-xoffset, spriteHeight-yoffset, z
			};

			if (! mirror) {
				GLfloat tx3 = tx1;
				tx1 = tx2;
				tx2 = tx3;
			}
			const GLfloat texCoords2[] = { 
				tx2, ty1,
				tx1, ty1,
				tx2, ty2,
				tx1, ty2
			}; 

			gfx::DrawUniforms uniforms;
			uniforms.useLightTexture = light && lightMapMode == LIGHTMAPMODE_PIXEL && lightMap.data;
			gfx::Texture * const textures[3] = { fontPal.tex_names[single.texNum], uniforms.useLightTexture ? lightMap.tex : NULL, copy };
			gfx::Quad quad = { vertices2, { texCoords2, ltexCoords, btexCoords } };
			gfx::renderer->draw (gfx::PipelineState (gfx::PROG_PASTE, gfx::BLEND_OFF, useZB ? gfx::DEPTH_TEST : gfx::DEPTH_OFF), uniforms, textures, quad);

			setSecondaryColor(0., 0., 0., 1.);
			//glDisable(GL_COLOR_SUM); FIXME: replace line?

			yoffset += viewportHeight;
		}
		xoffset += viewportWidth;
	}

	drawIntoWindow ();
	setPixelCoords (false);
}

