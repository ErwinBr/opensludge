#version 450

// The Y, U and V planes of a movie frame, as yuv.frag of the OpenGL
// renderer. Each plane has one channel, which we read as red.

layout(set = 2, binding = 0) uniform sampler2D Ytex;
layout(set = 2, binding = 1) uniform sampler2D Utex;
layout(set = 2, binding = 2) uniform sampler2D Vtex;

layout(location = 0) in vec2 varCoord0;

layout(location = 0) out vec4 fragColor;

void main()
{
	float y, u, v, r, g, b;

	y=texture(Ytex, varCoord0).r;
	u=texture(Utex, varCoord0).r;
	v=texture(Vtex, varCoord0).r;

	y=1.1643*(y-0.0625);
	u=u-0.5;
	v=v-0.5;

	r=y+1.5958*v;
	g=y-0.39173*u-0.81290*v;
	b=y+2.017*u;

	fragColor=vec4(r,g,b,1.0);
}
