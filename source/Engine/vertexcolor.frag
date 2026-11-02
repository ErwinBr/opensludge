uniform sampler2D sampler2d;

varying vec2 varCoord;
varying vec4 color;

void main(void)
{
	gl_FragColor = texture2D(sampler2d, varCoord) * color;
}
