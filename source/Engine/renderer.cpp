/*
 * What the renderers share.
 */

#include "renderer.h"

namespace gfx {

DrawUniforms::DrawUniforms ()
	: useLightTexture (false), antialias (false), zBuffer (false), zBufferLayer (0.0f), modulateColor (false) {
	for (int i = 0; i < 16; i ++) matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
	for (int i = 0; i < 4; i ++) {
		color[i] = 1.0f;
		secondaryColor[i] = 0.0f;
	}
}

} // namespace gfx
