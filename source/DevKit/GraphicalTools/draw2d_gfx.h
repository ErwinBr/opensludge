/*
 * draw2d_gfx.h - Part of the SLUDGE Dev Kit
 *
 * The 2D drawing of draw2d.h with the renderers of the engine
 * (gfx::Renderer), for the new Dev Kit. It draws into the target and the
 * viewport that the renderer has when we call finish.
 */

#ifndef __DRAW2D_GFX_H__
#define __DRAW2D_GFX_H__

#include "draw2d.h"

namespace gfx {
class Renderer;
}

namespace gfx {
struct Texture;
}

Draw2D * createGfxDraw2D (gfx::Renderer * renderer);

// The texture of a picture, which a Draw2D of createGfxDraw2D made or
// makes now, to show elsewhere, as the thumbnails of a sprite bank. Its
// top row is row 0. It goes when the bank changes.
gfx::Texture * gfxPictureTexture (Draw2D * draw, const spriteBank * bank, int index, bool zBufferLayer);

#endif
