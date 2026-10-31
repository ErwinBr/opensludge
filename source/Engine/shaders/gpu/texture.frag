#version 450
#extension GL_GOOGLE_include_directive : require

// A texture, as a layer of the z-buffer or multiplied by the colour, as
// texture.frag of the OpenGL renderer. The z-buffer has one channel, which
// we read as red here and as alpha in OpenGL.

#include "uniforms.glsl"

layout(set = 2, binding = 0) uniform sampler2D sampler2d;

layout(location = 0) in vec2 varCoord0;

layout(location = 0) out vec4 fragColor;

void main()
{
	vec4 col = texture(sampler2d, varCoord0);
	if (zBuffer != 0 && col.r < 0.0625*zBufferLayer-0.03)
	{
		discard;
	}
	if (modulateColor != 0)
	{
		col = col * color;
	}
	fragColor = col;
}
