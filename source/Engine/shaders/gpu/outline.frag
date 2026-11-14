#version 450
#extension GL_GOOGLE_include_directive : require

// Outlines, step 4: the outline, as outline.frag of the OpenGL renderer.

#include "uniforms.glsl"

layout(set = 2, binding = 0) uniform sampler2D seeds;
layout(set = 2, binding = 1) uniform sampler2D silhouette;

layout(location = 0) in vec2 varCoord0;

layout(location = 0) out vec4 fragColor;

vec2 decode (vec4 c)
{
	vec4 b = floor (c * 255.0 + 0.5);
	return vec2 (b.x * 256.0 + b.y, b.z * 256.0 + b.w) / 8.0;
}

void main()
{
	ivec2 size = ivec2 (outline.zw);
	vec2 pos = varCoord0 * outline.zw;
	ivec2 pixel = clamp (ivec2 (floor (pos)), ivec2 (0), size - 1);
	float a = texelFetch (silhouette, pixel, 0).a;
	float coverage = 1.0;
	if (a < 0.5) {
		vec4 c = texelFetch (seeds, pixel, 0);
		if (c.r + c.g + c.b + c.a > 3.99) discard;	// No seed
		coverage = clamp (outline.x - length (decode (c) - pos) + 0.5, 0.0, 1.0);
	}
	coverage *= mix (1.0 - a, 1.0, outline.y);
	if (coverage <= 0.0) discard;
	fragColor = vec4 (color.rgb, color.a * coverage);
}
