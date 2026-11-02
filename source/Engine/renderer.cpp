/*
 * What the renderers share.
 */

#include <stddef.h>

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

DrawUniforms::DrawUniforms ()
	: useLightTexture (false), antialias (false), zBuffer (false), zBufferLayer (0.0f), modulateColor (false) {
	for (int i = 0; i < 16; i ++) matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
	for (int i = 0; i < 4; i ++) {
		color[i] = 1.0f;
		secondaryColor[i] = 0.0f;
	}
}

} // namespace gfx
