// Outlines (outline.cpp), step 3: a step of the jump flood. Each pixel
// takes the nearest of the seeds that it and the eight pixels a step away
// have found so far.

#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#endif

uniform sampler2D sampler2d;	// The seeds so far
uniform vec4 outline;			// Thickness, step, width and height of the field

varying vec2 varCoord;

vec2 decode (vec4 c)
{
	vec4 b = floor (c * 255.0 + 0.5);
	return vec2 (b.x * 256.0 + b.y, b.z * 256.0 + b.w) / 8.0;
}

void main (void)
{
	vec2 pixel = floor (varCoord * outline.zw);
	vec2 centre = pixel + 0.5;
	float best = 1.0e20;
	vec4 nearest = vec4 (1.0);
	for (int y = -1; y <= 1; y ++) {
		for (int x = -1; x <= 1; x ++) {
			vec2 q = pixel + vec2 (float (x), float (y)) * outline.y;
			if (q.x < 0.0 || q.y < 0.0 || q.x >= outline.z || q.y >= outline.w) continue;
			vec4 c = texture2D (sampler2d, (q + 0.5) / outline.zw);
			if (c.r + c.g + c.b + c.a > 3.99) continue;	// No seed
			vec2 d = decode (c) - centre;
			float dd = dot (d, d);
			if (dd < best) {
				best = dd;
				nearest = c;
			}
		}
	}
	gl_FragColor = nearest;
}
