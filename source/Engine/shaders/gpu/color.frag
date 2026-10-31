#version 450
#extension GL_GOOGLE_include_directive : require

// A solid colour, as color.frag of the OpenGL renderer.

#include "uniforms.glsl"

layout(location = 0) out vec4 fragColor;

void main()
{
	fragColor = color;
}
