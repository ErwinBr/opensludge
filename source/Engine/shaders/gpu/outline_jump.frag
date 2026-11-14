#version 450
#extension GL_GOOGLE_include_directive : require

// Outlines, step 3: a step of the jump flood, as outline_jump.frag of the
// OpenGL renderer.

#include "uniforms.glsl"

layout(set = 2, binding = 0) uniform sampler2D seeds;

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
	ivec2 pixel = ivec2 (floor (varCoord0 * outline.zw));
	vec2 centre = vec2 (pixel) + 0.5;
	int step = int (outline.y);
	float best = 1.0e20;
	vec4 nearest = vec4 (1.0);
	for (int y = -1; y <= 1; y ++) {
		for (int x = -1; x <= 1; x ++) {
			ivec2 q = pixel + ivec2 (x, y) * step;
			if (q.x < 0 || q.y < 0 || q.x >= size.x || q.y >= size.y) continue;
			vec4 c = texelFetch (seeds, q, 0);
			if (c.r + c.g + c.b + c.a > 3.99) continue;	// No seed
			vec2 d = decode (c) - centre;
			float dd = dot (d, d);
			if (dd < best) {
				best = dd;
				nearest = c;
			}
		}
	}
	fragColor = nearest;
}
