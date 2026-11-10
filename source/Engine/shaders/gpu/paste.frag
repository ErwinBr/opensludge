#version 450
#extension GL_GOOGLE_include_directive : require

// Pasting onto the backdrop, which is on unit 2, as fixScaleSprite.frag of
// the OpenGL renderer.

#include "uniforms.glsl"

layout(set = 2, binding = 0) uniform sampler2D tex0;
layout(set = 2, binding = 1) uniform sampler2D tex1;
layout(set = 2, binding = 2) uniform sampler2D tex2;

layout(location = 0) in vec2 varCoord0;
layout(location = 1) in vec2 varCoord1;
layout(location = 2) in vec2 varCoord2;

layout(location = 0) out vec4 fragColor;

void main()
{
	vec4 texture0 = texture (tex0, varCoord0);
	vec4 texture2 = texture (tex2, varCoord2);
	vec3 col;
	if (useLightTexture != 0) {
		vec4 texture1 = texture (tex1, varCoord1);
		col = texture1.rgb * texture0.rgb;
	} else {
		col = color.rgb * texture0.rgb;
	}
	col += vec3(secondaryColor);
	vec4 c = vec4 (col * tint.rgb, color.a * texture0.a * tint.a);
	col = mix (texture2.rgb, c.rgb, c.a);
	fragColor = vec4 (col, max(texture0.a, texture2.a));
}
