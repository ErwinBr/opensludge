#version 450
#extension GL_GOOGLE_include_directive : require

// Outlines, step 2: the seeds, as outline_seed.frag of the OpenGL renderer.

#include "uniforms.glsl"

layout(set = 2, binding = 0) uniform sampler2D silhouette;

layout(location = 0) in vec2 varCoord0;

layout(location = 0) out vec4 fragColor;

float alphaAt (ivec2 pixel)
{
	ivec2 size = ivec2 (outline.zw);
	return texelFetch (silhouette, clamp (pixel, ivec2 (0), size - 1), 0).a;
}

vec4 encode (vec2 pos)
{
	vec2 v = floor (pos * 8.0 + 0.5);
	vec2 hi = floor (v / 256.0);
	vec2 lo = v - hi * 256.0;
	return vec4 (hi.x, lo.x, hi.y, lo.y) / 255.0;
}

void crossing (ivec2 pixel, float a, ivec2 towards, inout float best, inout vec2 seed)
{
	float b = alphaAt (pixel + towards);
	if ((a >= 0.5) != (b >= 0.5)) {
		float t = (a - 0.5) / (a - b);
		if (t < best) {
			best = t;
			seed = vec2 (pixel) + 0.5 + vec2 (towards) * t;
		}
	}
}

void main()
{
	ivec2 pixel = ivec2 (floor (varCoord0 * outline.zw));
	float a = alphaAt (pixel);
	float best = 2.0;
	vec2 seed = vec2 (0.0);
	crossing (pixel, a, ivec2 (1, 0), best, seed);
	crossing (pixel, a, ivec2 (-1, 0), best, seed);
	crossing (pixel, a, ivec2 (0, 1), best, seed);
	crossing (pixel, a, ivec2 (0, -1), best, seed);
	fragColor = best < 2.0 ? encode (seed) : vec4 (1.0);
}
