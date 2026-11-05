/*
 * The upload of the pages of a sprite bank to textures (see loadSpriteBank
 * in sprites.cpp). Apart from the engine, so that a test can watch it with
 * a renderer of its own.
 */

#include "sprites.h"
#include "spritebankdecode.h"

void uploadPages (gfx::Renderer & renderer, const spritebank::Layout & layout, const spritebank::Pages & pages,
		bool isFont, gfx::Filter filter, std::vector<AtlasPage> & out) {
	size_t numTextures = layout.pageWidth.size ();
	out.clear ();
	out.resize (numTextures);
	for (size_t n = 0; n < numTextures; n ++) {
		AtlasPage & page = out[n];
		page.w = layout.pageWidth[n];
		page.h = layout.pageHeight[n];
		page.gpuBytes = (size_t) page.w * page.h * 4 * (isFont ? 2 : 1);

		// Sprites are drawn from their pages without mipmaps (the renderers
		// have none), clamped at the edge, with one filter both ways.
		gfx::Texture * t = renderer.createTexture (page.w, page.h, gfx::TEX_RGBA8, pages.colour[n].data ());
		renderer.setTextureWrap (t, gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
		renderer.setTextureFilter (t, filter, filter);
		page.tex = gfx::UniqueTexture (&renderer, t);

		if (isFont) {
			gfx::Texture * b = renderer.createTexture (page.w, page.h, gfx::TEX_RGBA8, pages.burn[n].data ());
			renderer.setTextureWrap (b, gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
			renderer.setTextureFilter (b, filter, filter);
			page.burn = gfx::UniqueTexture (&renderer, b);
		}
	}
}
