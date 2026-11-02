/*
 * draw2d_gl.cpp - Part of the SLUDGE Dev Kit
 *
 * The 2D drawing of draw2d.h with OpenGL 3.2, for the GTK tools.
 */

#include <stdio.h>

#include <map>

#include <epoxy/gl.h>

#include "draw2d_gl.h"
#include "sprites.h"
#include "zbuffer.h"

static const char * vertexShaderSource =
	"#version 150\n"
	"uniform mat4 projection;\n"
	"in vec2 position;\n"
	"in vec2 textureCoordinates;\n"
	"out vec2 fragmentTextureCoordinates;\n"
	"void main () {\n"
	"	fragmentTextureCoordinates = textureCoordinates;\n"
	"	gl_Position = projection * vec4 (position, 0.0, 1.0);\n"
	"}\n";

static const char * fragmentShaderSource =
	"#version 150\n"
	"uniform vec4 colour;\n"
	"uniform bool textured;\n"
	"uniform sampler2D picture;\n"
	"in vec2 fragmentTextureCoordinates;\n"
	"out vec4 fragmentColour;\n"
	"void main () {\n"
	"	fragmentColour = textured ? colour * texture (picture, fragmentTextureCoordinates) : colour;\n"
	"}\n";

static GLuint compileShader (GLenum type, const char * source) {
	GLuint shader = glCreateShader (type);
	glShaderSource (shader, 1, &source, NULL);
	glCompileShader (shader);
	GLint compiled;
	glGetShaderiv (shader, GL_COMPILE_STATUS, &compiled);
	if (! compiled) {
		char log[1024];
		glGetShaderInfoLog (shader, sizeof (log), NULL, log);
		fprintf (stderr, "We can't compile a shader: %s\n", log);
	}
	return shader;
}

namespace {

class GLDraw2D : public Draw2D {
public:
	GLDraw2D () : program (0) {}
	~GLDraw2D ();
	bool init ();

	void clear ();
	void setViewArea (int x, int y, int w, int h, float zmul);
	void setSpriteViewArea (int x, int y, int w, int h, float zmul);
	void setDrawingColour (float r, float g, float b, float a);
	void drawLines (const float * points, int count);
	void drawLineLoop (const float * points, int count);
	void drawPolygon (const float * points, int count);
	void drawPicture (const spriteBank * bank, int index, bool zBufferLayer,
		float x1, float y1, float x2, float y2,
		float tx1, float ty1, float tx2, float ty2);
	void forgetPictures (const spriteBank * bank);

private:
	void setProjection (float left, float right, float bottom, float top);
	void drawVertices (GLenum mode, const float * vertices, int count, bool textured);
	void drawPoints (GLenum mode, const float * points, int count);
	GLuint texture (const spriteBank * bank, int index, bool zBufferLayer);

	GLuint program, vertexArray, vertexBuffer;
	GLint projectionLocation, colourLocation, texturedLocation;

	// The textures of the pictures, which we make again when their bank
	// has changed.
	struct Key {
		const spriteBank * bank;
		int index;
		bool zBufferLayer;
		bool operator < (const Key & other) const {
			if (bank != other.bank) return bank < other.bank;
			if (index != other.index) return index < other.index;
			return zBufferLayer < other.zBufferLayer;
		}
	};
	struct Picture {
		GLuint name;
		unsigned int changes;
	};
	std::map<Key, Picture> pictures;
};

bool GLDraw2D::init () {
	GLuint vertexShader = compileShader (GL_VERTEX_SHADER, vertexShaderSource);
	GLuint fragmentShader = compileShader (GL_FRAGMENT_SHADER, fragmentShaderSource);
	program = glCreateProgram ();
	glAttachShader (program, vertexShader);
	glAttachShader (program, fragmentShader);
	glBindAttribLocation (program, 0, "position");
	glBindAttribLocation (program, 1, "textureCoordinates");
	glLinkProgram (program);
	glDeleteShader (vertexShader);
	glDeleteShader (fragmentShader);
	GLint linked;
	glGetProgramiv (program, GL_LINK_STATUS, &linked);
	if (! linked) {
		char log[1024];
		glGetProgramInfoLog (program, sizeof (log), NULL, log);
		fprintf (stderr, "We can't link the shader program: %s\n", log);
		glDeleteProgram (program);
		program = 0;
		return false;
	}
	projectionLocation = glGetUniformLocation (program, "projection");
	colourLocation = glGetUniformLocation (program, "colour");
	texturedLocation = glGetUniformLocation (program, "textured");

	// Each vertex is x, y and the texture coordinates.
	glGenVertexArrays (1, &vertexArray);
	glBindVertexArray (vertexArray);
	glGenBuffers (1, &vertexBuffer);
	glBindBuffer (GL_ARRAY_BUFFER, vertexBuffer);
	glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float), (void *) 0);
	glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float), (void *) (2 * sizeof (float)));
	glEnableVertexAttribArray (0);
	glEnableVertexAttribArray (1);

	glUseProgram (program);
	glUniform4f (colourLocation, 1.0f, 1.0f, 1.0f, 1.0f);
	return true;
}

GLDraw2D::~GLDraw2D () {
	for (std::map<Key, Picture>::iterator i = pictures.begin (); i != pictures.end (); ++ i) {
		glDeleteTextures (1, &i->second.name);
	}
	if (program) {
		glDeleteProgram (program);
		glDeleteBuffers (1, &vertexBuffer);
		glDeleteVertexArrays (1, &vertexArray);
	}
}

void GLDraw2D::clear () {
	glClearColor (0, 0, 0, 0);
	glClear (GL_COLOR_BUFFER_BIT);
}

// The projection of glOrtho, with the near plane at 1 and the far plane
// at -1.
void GLDraw2D::setProjection (float left, float right, float bottom, float top) {
	GLfloat m[16] = {
		2.0f / (right - left), 0, 0, 0,
		0, 2.0f / (top - bottom), 0, 0,
		0, 0, 1.0f, 0,
		-(right + left) / (right - left), -(top + bottom) / (top - bottom), 0, 1.0f
	};
	glUseProgram (program);
	glUniformMatrix4fv (projectionLocation, 1, GL_FALSE, m);
}

void GLDraw2D::setViewArea (int x, int y, int w, int h, float zmul) {
	setProjection (x*zmul, (x+w)*zmul, (-y+h)*zmul, -y*zmul);
}

void GLDraw2D::setSpriteViewArea (int x, int y, int w, int h, float zmul) {
	setProjection (x*zmul, (x+w)*zmul, (y-h)*zmul, y*zmul);
}

void GLDraw2D::setDrawingColour (float r, float g, float b, float a) {
	glUseProgram (program);
	glUniform4f (colourLocation, r, g, b, a);
}

// Draw count vertices of four floats each.
void GLDraw2D::drawVertices (GLenum mode, const float * vertices, int count, bool textured) {
	glEnable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram (program);
	glUniform1i (texturedLocation, textured);
	glBindVertexArray (vertexArray);
	glBindBuffer (GL_ARRAY_BUFFER, vertexBuffer);
	glBufferData (GL_ARRAY_BUFFER, count * 4 * sizeof (float), vertices, GL_STREAM_DRAW);
	glDrawArrays (mode, 0, count);
}

void GLDraw2D::drawPoints (GLenum mode, const float * points, int count) {
	float * vertices = new float[count * 4];
	for (int i = 0; i < count; i ++) {
		vertices[i * 4] = points[i * 2];
		vertices[i * 4 + 1] = points[i * 2 + 1];
		vertices[i * 4 + 2] = vertices[i * 4 + 3] = 0;
	}
	drawVertices (mode, vertices, count, false);
	delete [] vertices;
}

void GLDraw2D::drawLines (const float * points, int count) {
	drawPoints (GL_LINES, points, count);
}

void GLDraw2D::drawLineLoop (const float * points, int count) {
	drawPoints (GL_LINE_LOOP, points, count);
}

void GLDraw2D::drawPolygon (const float * points, int count) {
	drawPoints (GL_TRIANGLE_FAN, points, count);
}

GLuint GLDraw2D::texture (const spriteBank * bank, int index, bool zBufferLayer) {
	Key key = { bank, index, zBufferLayer };
	std::map<Key, Picture>::iterator found = pictures.find (key);
	if (found != pictures.end () && found->second.changes == bank->changes) return found->second.name;

	Picture picture;
	if (found != pictures.end ()) {
		picture.name = found->second.name;
	} else {
		glGenTextures (1, &picture.name);
	}
	picture.changes = bank->changes;

	const sprite & single = bank->sprites[index];
	int width = single.width;
	int height = single.height < 0 ? - single.height : single.height;
	unsigned char * rgba = zBufferLayer ? zBufferLayerPixels (bank, index) : spritePixels (bank, index);
	// We draw sprites smoothly, and z-buffers with sharp edges.
	GLint filter = zBufferLayer ? GL_NEAREST : GL_LINEAR;
	glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
	glBindTexture (GL_TEXTURE_2D, picture.name);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	delete [] rgba;

	pictures[key] = picture;
	return picture.name;
}

void GLDraw2D::drawPicture (const spriteBank * bank, int index, bool zBufferLayer,
		float x1, float y1, float x2, float y2,
		float tx1, float ty1, float tx2, float ty2) {
	GLuint name = texture (bank, index, zBufferLayer);
	const float vertices[] = {
		x1, y1, tx1, ty1,
		x2, y1, tx2, ty1,
		x2, y2, tx2, ty2,
		x1, y2, tx1, ty2
	};
	glActiveTexture (GL_TEXTURE0);
	glBindTexture (GL_TEXTURE_2D, name);
	drawVertices (GL_TRIANGLE_FAN, vertices, 4, true);
}

void GLDraw2D::forgetPictures (const spriteBank * bank) {
	std::map<Key, Picture>::iterator i = pictures.begin ();
	while (i != pictures.end ()) {
		if (i->first.bank == bank) {
			glDeleteTextures (1, &i->second.name);
			pictures.erase (i ++);
		} else {
			++ i;
		}
	}
}

} // namespace

Draw2D * createGLDraw2D () {
	GLDraw2D * draw = new GLDraw2D;
	if (! draw->init ()) {
		delete draw;
		return NULL;
	}
	return draw;
}
