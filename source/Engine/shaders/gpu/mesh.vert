#version 450

// The vertex shader of the meshes of the SDL_GPU renderer (drawMesh in
// renderer_gpu.cpp), with the vertices in a vertex buffer: a position, the
// texture coordinates and a colour, as gfx::Vertex2D.

layout(set = 1, binding = 0) uniform Mesh {
	mat4 matrix;
};

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec4 vertexColor;

layout(location = 0) out vec2 varCoord0;
layout(location = 1) out vec4 varColor;

void main()
{
	gl_Position = matrix * vec4(position, 0.0, 1.0);
	varCoord0 = texCoord;
	varColor = vertexColor;
}
