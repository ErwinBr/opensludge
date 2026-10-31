#version 450

// The vertex shader of the SDL_GPU renderer (renderer_gpu.cpp). It draws
// the four corners of a quad as a triangle strip, which come with their
// texture coordinates in the uniform block, so there are no vertex buffers.

layout(set = 1, binding = 0) uniform Quad {
	mat4 matrix;
	vec4 positions[4];	// x, y and z
	vec4 texCoords01[4];	// Texture coordinates 0 in xy, 1 in zw
	vec4 texCoords2[4];	// Texture coordinates 2 in xy
};

layout(location = 0) out vec2 varCoord0;
layout(location = 1) out vec2 varCoord1;
layout(location = 2) out vec2 varCoord2;

void main()
{
	gl_Position = matrix * vec4(positions[gl_VertexIndex].xyz, 1.0);
	varCoord0 = texCoords01[gl_VertexIndex].xy;
	varCoord1 = texCoords01[gl_VertexIndex].zw;
	varCoord2 = texCoords2[gl_VertexIndex].xy;
}
