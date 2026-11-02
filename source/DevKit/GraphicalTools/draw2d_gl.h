/*
 * draw2d_gl.h - Part of the SLUDGE Dev Kit
 *
 * The 2D drawing of draw2d.h with OpenGL 3.2, for the GTK tools.
 */

#ifndef __DRAW2D_GL_H__
#define __DRAW2D_GL_H__

#include "draw2d.h"

// Make the drawing for the current OpenGL context. Returns NULL when we
// can't build its shaders.
Draw2D * createGLDraw2D ();

#endif
