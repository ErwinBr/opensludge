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
	Released r = { t, frame };
	released.push_back (r);
}

void Renderer::destroyReleasedTextures (bool all) {
	SDL_assert (SDL_GetCurrentThreadID () == ownerThread);
	if (! all) frame ++;
	size_t kept = 0;
	for (size_t i = 0; i < released.size (); i ++) {
		if (all || frame - released[i].frame >= (uint64_t) framesInFlight) {
			destroyTexture (released[i].texture);
		} else {
			released[kept ++] = released[i];
		}
	}
	released.resize (kept);
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
