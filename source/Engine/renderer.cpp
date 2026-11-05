/*
 * What the renderers share.
 */

#include <stddef.h>

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
	: framesInFlight (1), frame (0), ownerThread (SDL_GetCurrentThreadID ()) {
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
	}
}

} // namespace gfx
