#include <stdint.h>
#include <string.h>

#include "allfiles.h"
#include "colours.h"
#include "backdrop.h"
#include "graphics.h"
#include "newfatal.h"

extern gfx::Texture * snapshotTex;
extern unsigned char brightnessLevel;

extern float snapTexW, snapTexH;

unsigned char fadeMode = 2;



//----------------------------------------------------
// PROPER BRIGHTNESS FADING
//----------------------------------------------------

unsigned lastFrom, lastTo;

void transitionFader () {
	const GLfloat vertices[] = { 
		0.f, (GLfloat)winHeight, 0.f, 
		(GLfloat)winWidth, (GLfloat)winHeight, 0.f, 
		0.f, 0.f, 0.f,
		(GLfloat)winWidth, 0.f, 0.f
	};

	setPrimaryColor(0.0f, 0.0f, 0.0f, 1.0f - brightnessLevel/255.f);
	gfx::draw (gfx::PipelineState (gfx::PROG_COLOR, gfx::BLEND_ALPHA), engineUniforms (), vertices);
}

void transitionCrossFader () {

	if (! snapshotTex) return;

	const GLfloat vertices[] = { 
		0.f, (GLfloat)winHeight, 0.f, 
		(GLfloat)winWidth, (GLfloat)winHeight, 0.f, 
		0.f, 0.f, 0.f,
		(GLfloat)winWidth, 0.f, 0.f
	};

	const GLfloat texCoords[] = { 
		0.0f, snapTexH,
		snapTexW, snapTexH,
		0.0f, 0.0f,
		snapTexW, 0.0f
	}; 

	setPrimaryColor(1.0f, 1.0f, 1.0f, 1.0f - brightnessLevel/255.f);
	gfx::DrawUniforms uniforms = engineUniforms ();
	uniforms.modulateColor = true;
	gfx::draw (gfx::PipelineState (gfx::PROG_TEXTURE, gfx::BLEND_ALPHA), uniforms, snapshotTex, vertices, texCoords);
}

void transitionSnapshotBox () {
	
	if (! snapshotTex) return; 

	float xScale = (float) brightnessLevel * winWidth / 510.f;	// 510 = 255*2
	float yScale = (float) brightnessLevel * winHeight / 510.f;
	
	const GLfloat vertices[] = { 
		xScale, winHeight-yScale, 0, 
		winWidth-xScale, winHeight-yScale, 0, 
		xScale, yScale, 0,
		winWidth-xScale, yScale, 0
	};

	const GLfloat texCoords[] = { 
		0.0f, snapTexH,
		snapTexW, snapTexH,
		0.0f, 0.0f,
		snapTexW, 0.0f
	}; 

	gfx::draw (gfx::PipelineState (gfx::PROG_TEXTURE), engineUniforms (), snapshotTex, vertices, texCoords);
}

//----------------------------------------------------
// FAST PSEUDO-RANDOM NUMBER STUFF FOR DISOLVE EFFECT
//----------------------------------------------------

#define KK 17
uint32_t randbuffer[KK][2];  // history buffer
int p1, p2;

void resetRandW () {
	int32_t seed = 12345;
	
	for (int i=0; i<KK; i++) {
		for (int j=0; j<2; j++) {
			seed = seed * 2891336453u + 1;
			randbuffer[i][j] = seed;
		}
	}
	
	p1 = 0, p2 = 10;
}

GLubyte * transitionTexture = NULL;
gfx::Texture * transitionTex = NULL;

bool reserveTransitionTexture () {
	
	if (! transitionTexture) {
		transitionTexture = new GLubyte [256*256*4];
		if (! checkNew (transitionTexture)) return false;
	}
	
	if (transitionTex) {
		gfx::renderer->setTextureImage (transitionTex, 256, 256, transitionTexture);
	} else {
		transitionTex = gfx::renderer->createTexture (256, 256, gfx::TEX_RGBA8, transitionTexture);
	}
	gfx::renderer->setTextureWrap (transitionTex, gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
	gfx::renderer->setTextureFilter (transitionTex, gfx::FILTER_NEAREST, gfx::FILTER_NEAREST);
	
	return true;
}

// Stretch the transition texture over the window. With nearest filtering,
// the centres of some pixels fall exactly on the edge between two texels,
// for example every fifth column of 640 pixels, and GPUs round those
// differently. A quarter of a texel moves every centre off the edges.
void drawTransitionTexture () {
	const GLfloat vertices[] = {
		0.f, (GLfloat)winHeight, 0.f,
		(GLfloat)winWidth, (GLfloat)winHeight, 0.f,
		0.f, 0.f, 0.f,
		(GLfloat)winWidth, 0.f, 0.f
	};

	const GLfloat q = 0.25f / 256.f;
	const GLfloat texCoords[] = {
		q, 1.0f + q,
		1.0f + q, 1.0f + q,
		q, q,
		1.0f + q, q
	};

	setPrimaryColor(1.0f, 1.0f, 1.0f, 1.0f);
	gfx::DrawUniforms uniforms = engineUniforms ();
	uniforms.modulateColor = true;
	gfx::draw (gfx::PipelineState (gfx::PROG_TEXTURE, gfx::BLEND_ALPHA), uniforms, transitionTex, vertices, texCoords);
}


void transitionDisolve () {
	
	if (! transitionTex) reserveTransitionTexture();

	if (! brightnessLevel) {
		transitionFader ();
		return;
	}
	
	uint32_t n;
	uint32_t y;

	GLubyte * toScreen = transitionTexture;
	GLubyte * end = transitionTexture + (256 * 256*4);
	
	do {
		// generate next number
		n = randbuffer[p1][1];
		y = (n << 27) | ((n >> (32 - 27)) + randbuffer[p2][1]);

		n = randbuffer[p1][0];
		randbuffer[p1][1] = (n << 19) | ((n >> (32 - 19)) + randbuffer[p2][0]);
		randbuffer[p1][0] = y;

		// rotate list pointers
		if (! p1 --) p1 = KK - 1;
		if (! p2 --) p2 = KK - 1;
		
		if ((y & 255u) > brightnessLevel) {
			toScreen[0] =toScreen[1] =toScreen[2] = 0;
			toScreen[3] = 255;
		} else {
			toScreen[0] =toScreen[1] =toScreen[2] =toScreen[3] = 0;
		}
		toScreen += 4;
	} while (toScreen < end);

	gfx::renderer->updateTexture (transitionTex, 0, 0, 256, 256, transitionTexture);

	drawTransitionTexture ();
}

void transitionTV () {

	if (! transitionTex) reserveTransitionTexture();

	uint32_t n;
	uint32_t y;

	GLubyte * toScreen = transitionTexture;
	GLubyte * end = transitionTexture + (256 * 256*4);
	
	do {
		// generate next number
		n = randbuffer[p1][1];
		y = (n << 27) | ((n >> (32 - 27)) + randbuffer[p2][1]);

		n = randbuffer[p1][0];
		randbuffer[p1][1] = (n << 19) | ((n >> (32 - 19)) + randbuffer[p2][0]);
		randbuffer[p1][0] = y;

		// rotate list pointers
		if (! p1 --) p1 = KK - 1;
		if (! p2 --) p2 = KK - 1;
		
		if ((y & 255u) > brightnessLevel) {
			toScreen[0] =toScreen[1] =toScreen[2] = (n & 255);
			toScreen[3] = (n & 255);
		} else {
			toScreen[0] =toScreen[1] =toScreen[2] =toScreen[3] = 0;
		}
		toScreen += 4;
	} while (toScreen < end);
	
	gfx::renderer->updateTexture (transitionTex, 0, 0, 256, 256, transitionTexture);
	
	drawTransitionTexture ();
}

void transitionBlinds () {
	if (! transitionTex) reserveTransitionTexture();
	// The blinds repeat down the screen.
	gfx::renderer->setTextureWrap (transitionTex, gfx::WRAP_CLAMP, gfx::WRAP_REPEAT);

	GLubyte * toScreen = transitionTexture;

	int level = brightnessLevel/8;
	
	if (level) memset (toScreen, 0, 256*32*level);
	if (level < 32) memset (toScreen+256*32*level, 255, 256*32*(32-level));
	
	gfx::renderer->updateTexture (transitionTex, 0, 0, 256, 256, transitionTexture);

	const GLfloat vertices[] = { 
		0.f, (GLfloat)winHeight, 0.f, 
		(GLfloat)winWidth, (GLfloat)winHeight, 0.f, 
		0.f, 0.f, 0.f,
		(GLfloat)winWidth, 0.f, 0.f
	};

	const GLfloat texCoords[] = { 
		0.0f, 0.0f,
		1.0f, 0.0f,
		0.0f, 25.0f,
		1.0f, 25.0f
	}; 

	setPrimaryColor(0.0f, 0.0f, 0.0f, 1.0f);
	gfx::DrawUniforms uniforms = engineUniforms ();
	uniforms.modulateColor = true;
	gfx::draw (gfx::PipelineState (gfx::PROG_TEXTURE, gfx::BLEND_ALPHA), uniforms, transitionTex, vertices, texCoords);
}

//----------------------------------------------------

void fixBrightness () {
	switch (fadeMode) {
		case 0:		transitionFader ();				break;		
		case 1:		resetRandW ();					// Fall through!
		case 2:		transitionDisolve ();			break;
		case 3:		transitionTV ();				break;
		case 4:		transitionBlinds ();			break;
		case 5:		transitionCrossFader ();		break;
		case 6:		transitionSnapshotBox ();		break;
 
	}
}
