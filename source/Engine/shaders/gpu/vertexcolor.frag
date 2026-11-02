#version 450
#extension GL_GOOGLE_include_directive : require

// A texture multiplied by the colour of the vertices and the colour, as
// vertexcolor.frag of the OpenGL renderer.

#include "uniforms.glsl"

layout(set = 2, binding = 0) uniform sampler2D sampler2d;

layout(location = 0) in vec2 varCoord0;
layout(location = 1) in vec4 varColor;

layout(location = 0) out vec4 fragColor;

void main()
{
	fragColor = texture(sampler2d, varCoord0) * varColor * color;
}
