// Outlines (outline.cpp), step 2: the seeds. The silhouette's edge is where
// its alpha crosses one half. Where a pixel and one of its four neighbours
// lie on either side, the edge crosses the line between their centres,
// where the alpha that blends between them is one half. Each pixel keeps
// the nearest of its crossings, in pixels of the field, as outline.cpp
// codes them; pixels away from the edge have none.

#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#endif

uniform sampler2D sampler2d;	// The silhouette
uniform vec4 outline;			// Thickness, step, width and height of the field

varying vec2 varCoord;

float alphaAt (vec2 pixel)
{
	return texture2D (sampler2d, (pixel + 0.5) / outline.zw).a;
}

// A seed at pos, in eighths of a pixel, 16 bits for x and for y.
vec4 encode (vec2 pos)
{
	vec2 v = floor (pos * 8.0 + 0.5);
	vec2 hi = floor (v / 256.0);
	vec2 lo = v - hi * 256.0;
	return vec4 (hi.x, lo.x, hi.y, lo.y) / 255.0;
}

void crossing (vec2 pixel, float a, vec2 towards, inout float best, inout vec2 seed)
{
	float b = alphaAt (pixel + towards);
	if ((a >= 0.5) != (b >= 0.5)) {
		float t = (a - 0.5) / (a - b);
		if (t < best) {
			best = t;
			seed = pixel + 0.5 + towards * t;
		}
	}
}

void main (void)
{
	vec2 pixel = floor (varCoord * outline.zw);
	float a = alphaAt (pixel);
	float best = 2.0;
	vec2 seed = vec2 (0.0);
	crossing (pixel, a, vec2 (1.0, 0.0), best, seed);
	crossing (pixel, a, vec2 (-1.0, 0.0), best, seed);
	crossing (pixel, a, vec2 (0.0, 1.0), best, seed);
	crossing (pixel, a, vec2 (0.0, -1.0), best, seed);
	gl_FragColor = best < 2.0 ? encode (seed) : vec4 (1.0);
}
