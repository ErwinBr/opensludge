
#include "sludge_gl.h"

#include <stdlib.h>
#include "graphics.h"

#include "allfiles.h"

extern int sceneWidth, sceneHeight;
extern gfx::RenderTarget * backdropTarget;
gfx::Texture * copyOfBackdrop ();

void drawLine(int x1, int y1, int x2, int y2) {
	int x, y;
	bool backwards = false;
	
	if (x1 < 0)  x1 = 0;
	if (y1 < 0)  y1 = 0;
	if (x2 < 0)  x2 = 0;
	if (y2 < 0)  y2 = 0;
	if (x1 > sceneWidth) x1 = sceneWidth - 1;
	if (x2 > sceneWidth) x2 = sceneWidth - 1;
	if (y1 > sceneHeight) y1 = sceneHeight - 1;
	if (y2 > sceneHeight) y2 = sceneHeight - 1;

	if (x1 > x2) {
		x = x2; 
		backwards = !backwards;
	} else x = x1;
		
	if (y1 > y2) {
		y = y2; 
		backwards = !backwards;
	} else y = y1;	
	
	int diffX = abs(x2-x1);
	int diffY = abs(y2-y1);	
	
	if (! diffX) {
		diffX = 1;
		if (x == sceneWidth - 1) x = sceneWidth -2;
	}
	if (! diffY) {
		diffY = 1;
		if (y == sceneHeight - 1) y = sceneHeight -2;
	}
	
	setPixelCoords (true);
	
	int xoffset = 0;
	while (xoffset < diffX) {
		int w = (diffX-xoffset < viewportWidth) ? diffX-xoffset : viewportWidth;
		
		int yoffset = 0;
		while (yoffset < diffY) {
			int h = (diffY-yoffset < viewportHeight) ? diffY-yoffset : viewportHeight;
			
			// Render the scene - first the old backdrop
			gfx::Texture * copy = copyOfBackdrop ();
			drawIntoTile (backdropTarget, x+xoffset, y+yoffset, 0, 0, w, h);

			const GLfloat vertices[] = { 
				(GLfloat)-x-xoffset, (GLfloat)1-y-yoffset, 0., 
				(GLfloat)sceneWidth-x-xoffset, (GLfloat)1-y-yoffset, 0., 
				(GLfloat)-x-xoffset, (GLfloat)sceneHeight-y-yoffset, 0.,
				(GLfloat)sceneWidth-x-xoffset, (GLfloat)sceneHeight-y-yoffset, 0.
			};

			const GLfloat texCoords[] = { 
				0.0f, 0.0f,
				backdropTexW, 0.0f,
				0.0f, backdropTexH,
				backdropTexW, backdropTexH
			}; 

			gfx::draw (gfx::PipelineState (gfx::PROG_TEXTURE), gfx::DrawUniforms (), copy, vertices, texCoords);
			
			// Then the line
			//FIXME:Removing the lines doesn't work, but also didn't work properly before.

			GLint xo1=-xoffset, xo2=-xoffset;
			if (! backwards) {
				xo2 += diffX;
			} else {
				xo1 += diffX;
			}
			gfx::DrawUniforms lineUniforms;
			lineUniforms.color[0] = lineUniforms.color[1] = lineUniforms.color[2] = 0.0f;
			lineUniforms.color[3] = 1.0f;
			gfx::renderer->drawLine (gfx::PipelineState (gfx::PROG_COLOR), lineUniforms, xo1, -yoffset, xo2, -yoffset+diffY, 2.0f);

			yoffset += viewportHeight;
		}		
		xoffset += viewportWidth;
	}
	drawIntoWindow ();
	setPixelCoords (false);	
}
