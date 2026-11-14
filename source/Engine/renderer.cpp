/*
 * What the renderers share.
 */

#include <stddef.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "renderer.h"

namespace gfx {

static LogFunction logFunction = NULL;

void setLogFunction (LogFunction log) {
	logFunction = log;
}

void logDebug (const char * format, ...) {
	if (! logFunction) return;
	va_list args;
	va_start (args, format);
	logFunction (format, args);
	va_end (args);
}

Renderer::Renderer ()
	: framesInFlight (1), frame (0), ownerThread (SDL_GetCurrentThreadID ()), lastUnit0 (NULL) {
	memset (&stats, 0, sizeof (stats));
	memset (&lastStats, 0, sizeof (lastStats));
}

void Renderer::countDraw (const Texture * unit0) {
	stats.draws ++;
	if (unit0 != lastUnit0) stats.textureSwitches ++;
	lastUnit0 = unit0;
}

void Renderer::countMeshDraw (const Texture * texture) {
	stats.meshDraws ++;
	if (texture != lastUnit0) stats.textureSwitches ++;
	lastUnit0 = texture;
}

void Renderer::countTextureSize (TexFormat format, int oldWidth, int oldHeight, int newWidth, int newHeight) {
	size_t bpp = format == TEX_RGBA8 ? 4 : 1;
	stats.textureBytes -= (size_t) oldWidth * oldHeight * bpp;
	stats.textureBytes += (size_t) newWidth * newHeight * bpp;
}

void Renderer::finishFrameStats () {
	lastStats = stats;
	stats.draws = stats.meshDraws = stats.textureSwitches = 0;
	lastUnit0 = NULL;
}

void Renderer::releaseTexture (Texture * t) {
	if (! t) return;
	SDL_assert (SDL_GetCurrentThreadID () == ownerThread);
	Released r = { t, NULL, frame };
	released.push_back (r);
}

void Renderer::releaseTarget (RenderTarget * target) {
	if (! target) return;
	SDL_assert (SDL_GetCurrentThreadID () == ownerThread);
	detachTarget (target);
	Released r = { NULL, target, frame };
	released.push_back (r);
}

void Renderer::destroyReleasedTextures (bool all) {
	SDL_assert (SDL_GetCurrentThreadID () == ownerThread);
	if (! all) frame ++;
	// Targets first: a target may draw into a texture released with it.
	for (int pass = 0; pass < 2; pass ++) {
		size_t kept = 0;
		for (size_t i = 0; i < released.size (); i ++) {
			Released & r = released[i];
			bool due = all || frame - r.frame >= (uint64_t) framesInFlight;
			if (due && pass == 0 && r.target) {
				destroyTarget (r.target);
			} else if (due && pass == 1 && r.texture) {
				destroyTexture (r.texture);
			} else {
				released[kept ++] = r;
			}
		}
		released.resize (kept);
	}
}

DrawUniforms::DrawUniforms ()
	: useLightTexture (false), antialias (false), zBuffer (false), zBufferLayer (0.0f), modulateColor (false) {
	for (int i = 0; i < 16; i ++) matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
	for (int i = 0; i < 4; i ++) {
		color[i] = 1.0f;
		secondaryColor[i] = 0.0f;
		tint[i] = 1.0f;
		outline[i] = 0.0f;
	}
}

} // namespace gfx
