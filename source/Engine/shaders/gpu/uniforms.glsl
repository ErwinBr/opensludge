// The uniforms of every fragment shader, as gfx::DrawUniforms in
// renderer_gpu.cpp lays them out.
layout(set = 3, binding = 0) uniform Draw {
	vec4 color;
	vec4 secondaryColor;
	float scale;
	float zBufferLayer;
	int antialias;
	int useLightTexture;
	int zBuffer;
	int modulateColor;
	vec4 tint;
};
