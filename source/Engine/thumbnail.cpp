#include <stdint.h>

#include "allfiles.h"
#include "errors.h"
#include "moreio.h"
#include "version.h"
#include "sludger.h"
#include "colours.h"
#include "backdrop.h"
#include "graphics.h"
#include "newfatal.h"

bool freeze ();
void unfreeze (bool);	// Because FREEZE.H needs a load of other includes

int thumbWidth = 0, thumbHeight = 0;

extern gfx::Texture * backdropTex;
extern gfx::RenderTarget * backdropTarget;
unsigned short rgbTo565 (const unsigned char * rgb);


bool saveThumbnail (FILE * fp) {
	put4bytes (thumbWidth, fp);
	put4bytes (thumbHeight, fp);

	if (thumbWidth && thumbHeight) {
		if (! freeze ()) return false;


		setPixelCoords (true);

		gfx::Texture * thumbnailTex = gfx::renderer->createTexture (thumbWidth, thumbHeight, gfx::TEX_RGBA8, NULL);
		gfx::renderer->setTextureWrap (thumbnailTex, gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
		gfx::renderer->setTextureFilter (thumbnailTex, gfx::FILTER_NEAREST, gfx::FILTER_NEAREST);
		gfx::RenderTarget * target = gfx::renderer->createTarget (thumbnailTex, false);
		drawIntoTile (target, 0, 0, 0, 0, thumbWidth, thumbHeight);

		// Render the backdrop (scaled)
		gfx::renderer->setTextureFilter (backdropTex, gfx::FILTER_LINEAR, gfx::FILTER_NEAREST);

		const GLfloat vertices[] = { 
			0., 0., 0., 
			thumbWidth-1.f, 0., 0., 
			0., thumbHeight-1.f, 0.,
			thumbWidth-1.f, thumbHeight-1.f, 0.
		};

		const GLfloat texCoords[] = { 
			0.0f, 0.0f,
			backdropTexW, 0.0f,
			0.0f, backdropTexH,
			backdropTexW, backdropTexH
		}; 

		gfx::draw (gfx::PipelineState (gfx::PROG_TEXTURE), engineUniforms (), backdropTex, vertices, texCoords);

		gfx::Filter filter = gameSettings.antiAlias < 0 ? gfx::FILTER_LINEAR : gfx::FILTER_NEAREST;
		gfx::renderer->setTextureFilter (backdropTex, filter, filter);

		// Save the thumbnail, in 16 bits
		GLushort* image = new GLushort [thumbWidth*thumbHeight];
		if (! checkNew (image)) return false;
		GLubyte * rgba = new GLubyte [thumbWidth*thumbHeight*4];
		gfx::renderer->readPixels (0, 0, thumbWidth, thumbHeight, rgba);
		for (int i = 0; i < thumbWidth*thumbHeight; i ++) {
			image[i] = rgbTo565 (rgba + i*4);
		}
		delete [] rgba;

		gfx::renderer->destroyTarget (target);
		gfx::renderer->destroyTexture (thumbnailTex);
		drawIntoWindow ();
		setPixelCoords (false);

		for (int y = 0; y < thumbHeight; y ++) {
			for (int x = 0; x < thumbWidth; x ++) {
				put2bytes ((*(image +y*thumbWidth+x)), fp);
			}
		}
		delete[] image;
		image = NULL;
		unfreeze (true);
	}
	fputc ('!', fp);
	return true;
}

void showThumbnail (char * filename, int atX, int atY) {
	GLubyte * thumbnailTexture = NULL;

	GLfloat texCoordW = 1.0;
	GLfloat texCoordH = 1.0;	
	
	int ssgVersion;
	FILE * fp = openAndVerify (filename, 'S', 'A', ERROR_GAME_LOAD_NO, ssgVersion);
	if (ssgVersion >= VERSION(1,4)) {
		if (fp == NULL) return;
		int fileWidth = get4bytes (fp);
		int fileHeight = get4bytes (fp);

		int picWidth = fileWidth;
		int picHeight = fileHeight;
		if (! NPOT_textures) {
			picWidth = getNextPOT(picWidth);
			picHeight = getNextPOT(picHeight);
			texCoordW = ((double)fileWidth) / picWidth;
			texCoordH = ((double)fileHeight) / picHeight;
			
		}

		thumbnailTexture = new GLubyte [picHeight*picWidth*4] ();
		if (thumbnailTexture == NULL) return;

		int t1, t2;
		unsigned short c;
		GLubyte * target;
		for (t2 = 0; t2 < fileHeight; t2 ++) {
			t1 = 0;
			while (t1 < fileWidth) {
				c = (unsigned short) get2bytes (fp);
				target = thumbnailTexture + 4*picWidth*t2 + t1*4;
				target[0] = (GLubyte) redValue(c);
				target[1] = (GLubyte) greenValue(c);
				target[2] = (GLubyte) blueValue(c);
				target[3] = (GLubyte) 255;
				t1++;
			}
		}

		fclose (fp);

		gfx::Texture * thumbnailTex = gfx::renderer->createTexture (picWidth, picHeight, gfx::TEX_RGBA8, thumbnailTexture);
		gfx::renderer->setTextureWrap (thumbnailTex, gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
		gfx::renderer->setTextureFilter (thumbnailTex, gfx::FILTER_NEAREST, gfx::FILTER_NEAREST);

		delete [] thumbnailTexture;
		thumbnailTexture = NULL;

		if (atX<0){
			fileWidth+= atX;
			atX=0;
		}
		if (atY<0){
			fileHeight+= atY;
			atY=0;
		}
		if (fileWidth + atX > sceneWidth) fileWidth = sceneWidth-atX;
		if (fileHeight + atY > sceneHeight) fileHeight = sceneHeight-atY;

		setPixelCoords (true);

		int xoffset = 0;
		while (xoffset < fileWidth) {
			int w = (fileWidth-xoffset < viewportWidth) ? fileWidth-xoffset : viewportWidth;

			int yoffset = 0;
			while (yoffset < fileHeight) {
				int h = (fileHeight-yoffset < viewportHeight) ? fileHeight-yoffset : viewportHeight;

				drawIntoTile (backdropTarget, atX+xoffset, atY+yoffset, 0, 0, w, h);

				const GLfloat vertices[] = { 
					(GLfloat)fileWidth-1.f-xoffset, (GLfloat)-yoffset, 0., 
					(GLfloat)-xoffset, (GLfloat)-yoffset, 0., 
					(GLfloat)fileWidth-1.f-xoffset, (GLfloat)fileHeight-1.f-yoffset, 0.,
					(GLfloat)-xoffset, (GLfloat)fileHeight-1.f-yoffset, 0.
				};

				const GLfloat texCoords[] = { 
					texCoordW, 0.0f,
					0.0f, 0.0f,
					texCoordW, texCoordH,
					0.0f, texCoordH
				}; 
	
				gfx::draw (gfx::PipelineState (gfx::PROG_TEXTURE), engineUniforms (), thumbnailTex, vertices, texCoords);

				yoffset += viewportHeight;
			}
			xoffset += viewportWidth;
		}
		drawIntoWindow ();
		setPixelCoords (false);
		gfx::renderer->destroyTexture (thumbnailTex);
	}
}

bool skipThumbnail (FILE * fp) {
	thumbWidth = get4bytes (fp);
	thumbHeight = get4bytes (fp);
	uint32_t skippy = thumbWidth;
	skippy *= thumbHeight << 1;
	fseek (fp, skippy, 1);
	return (fgetc (fp) == '!');
}
