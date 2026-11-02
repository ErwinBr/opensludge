attribute vec4 myVertex;
attribute vec2 myUV0;
attribute vec4 myVertexColor;

uniform mat4 myPMVMatrix;
uniform vec4 myColor;

varying vec2 varCoord;
varying vec4 color;

void main(void)
{
	gl_Position = myPMVMatrix * myVertex;
	varCoord = myUV0.st;
	color = myVertexColor * myColor;
}
