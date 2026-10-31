#version 450
#extension GL_GOOGLE_include_directive : require

// Sprites, fonts and the backdrop, as scale.frag of the OpenGL renderer.
/*
   Inspired by a shader by guest(r) - guest.r@gmail.com - that was found at
   http://www.razyboard.com/system/morethread-smart-texture-mag-filter-for-ogl2-and-dosbox-pete_bernert-266904-5689051-0.html
*/

#include "uniforms.glsl"

layout(set = 2, binding = 0) uniform sampler2D Texture;
layout(set = 2, binding = 1) uniform sampler2D lightTexture;

layout(location = 0) in vec2 varCoord0;
layout(location = 1) in vec2 varCoord1;

layout(location = 0) out vec4 fragColor;

void main()
{
	vec4 c11 = texture(Texture, varCoord0.xy);

	// We take the derivatives outside the branch, where they are defined
	// for every pixel.
	vec2 fw = fwidth(varCoord0.xy)*scale;
	if (antialias != 0) {
		vec2 sd1 = vec2( fw.x,fw.y);
		vec2 sd2 = vec2(-fw.x,fw.y);

		vec4 s00 = texture(Texture, varCoord0.xy-sd1);
		vec4 s20 = texture(Texture, varCoord0.xy-sd2);
		vec4 s22 = texture(Texture, varCoord0.xy+sd1);
		vec4 s02 = texture(Texture, varCoord0.xy+sd2);

		vec4 dt = vec4(1.0,1.0,1.0,1.0);

		float m1=dot(abs(s00-s22),dt)+0.0001;
		float m2=dot(abs(s02-s20),dt)+0.0001;

		vec4 temp1 = m2*(s00 + s22) + m1*(s02 + s20);

		c11 = c11*0.333333 + (temp1/(m1+m2)) * 0.333333;
	}

	vec3 col;
	if (useLightTexture != 0) {
		vec4 texture1 = texture (lightTexture, varCoord1.xy);
		col = texture1.rgb * c11.rgb;
	} else {
		col = color.rgb * c11.rgb;
	}
	col += vec3(secondaryColor);
	fragColor = vec4 (col, color.a * c11.a);
}
