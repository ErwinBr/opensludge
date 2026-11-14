// Outlines (outline.cpp), step 4: the outline, in the colour, where the
// nearest seed of the edge is less than the thickness away, with a pixel
// of anti-aliasing at its outer edge. In the silhouette, it fills or leaves
// out the inside.

#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#endif

uniform sampler2D sampler2d;	// The seeds
uniform sampler2D silhouette;
uniform vec4 outline;			// Thickness, fill, width and height of the field

varying vec2 varCoord;
varying vec4 color;

vec2 decode (vec4 c)
{
	vec4 b = floor (c * 255.0 + 0.5);
	return vec2 (b.x * 256.0 + b.y, b.z * 256.0 + b.w) / 8.0;
}

void main (void)
{
	vec2 pos = varCoord * outline.zw;
	vec2 uv = (floor (pos) + 0.5) / outline.zw;
	float a = texture2D (silhouette, uv).a;
	float coverage = 1.0;
	if (a < 0.5) {
		vec4 c = texture2D (sampler2d, uv);
		if (c.r + c.g + c.b + c.a > 3.99) discard;	// No seed
		coverage = clamp (outline.x - length (decode (c) - pos) + 0.5, 0.0, 1.0);
	}
	coverage *= mix (1.0 - a, 1.0, outline.y);
	if (coverage <= 0.0) discard;
	gl_FragColor = vec4 (color.rgb, color.a * coverage);
}
