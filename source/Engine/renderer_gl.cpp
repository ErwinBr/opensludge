/*
 * The OpenGL renderer: OpenGL 2.0 with frame buffer objects, or OpenGL ES
 * 2.0 with HAVE_GLES2.
 */

#include <stdlib.h>
#include <string.h>

#include <vector>

#include <SDL3/SDL.h>

#include "sludge_gl.h"
#include "renderer.h"
#include "dxgipresent.h"
#include "shaders.h"

namespace gfx {

struct Texture {
	GLuint name;
	int width, height;
	TexFormat format;
	Filter minFilter, magFilter;
	Wrap wrapS, wrapT;
};

struct RenderTarget {
	GLuint framebuffer;
	GLuint depth;
	Texture * color;
};

// We draw meshes from client memory, as the quads.
struct Mesh {
	std::vector<Vertex2D> vertices;
	std::vector<unsigned short> indices;
};

namespace {

// The uniforms and attributes of a program.
struct ProgramInfo {
	GLuint program;
	GLint matrix, color, secondaryColor;
	GLint useLightTexture, antialias, scale;
	GLint zBuffer, zBufferLayer, modulateColor, tint;
	GLint vertex, texCoords[3], vertexColor;
};

GLenum glFormat (TexFormat format) {
	return format == TEX_R8 ? GL_ALPHA : GL_RGBA;
}

int bytesPerPixel (TexFormat format) {
	return format == TEX_R8 ? 1 : 4;
}

GLint glFilter (Filter filter) {
	return filter == FILTER_LINEAR ? GL_LINEAR : GL_NEAREST;
}

GLint glWrap (Wrap wrap) {
	return wrap == WRAP_REPEAT ? GL_REPEAT : GL_CLAMP_TO_EDGE;
}

class GLRenderer : public Renderer {
public:
	GLRenderer (void (* swap) ()) : swapBuffers (swap), currentTarget (NULL), antialiasScale (1.0f), readFramebuffer (0), blank (NULL) {}

	bool init (const char ** error);

	Texture * createTexture (int width, int height, TexFormat format, const void * pixels);
	void setTextureImage (Texture * t, int width, int height, const void * pixels);
	void updateTexture (Texture * t, int x, int y, int width, int height, const void * pixels);
	void destroyTexture (Texture * t);
	void textureSize (const Texture * t, int * width, int * height);
	void setTextureFilter (Texture * t, Filter minFilter, Filter magFilter);
	void setTextureWrap (Texture * t, Wrap s, Wrap t2);
	void readTexture (Texture * t, unsigned char * rgba);

	RenderTarget * createTarget (Texture * color, bool depth);
	void destroyTarget (RenderTarget * target);
	void detachTarget (RenderTarget * target);
	void setTarget (RenderTarget * target);
	void setViewport (int x, int y, int width, int height);
	void setScissor (int x, int y, int width, int height);
	void clear (bool color, bool depth);
	void copyToTexture (Texture * dst, int dstX, int dstY, int srcX, int srcY, int width, int height);
	void readPixels (int x, int y, int width, int height, unsigned char * rgba);

	void setAntialiasScale (float scale);

	void draw (const PipelineState & state, const DrawUniforms & uniforms, Texture * const textures[3], const Quad & quad);

	Mesh * createFrameMesh (const Vertex2D * vertices, int vertexCount, const unsigned short * indices, int indexCount);
	void drawMesh (const PipelineState & state, const DrawUniforms & uniforms, Texture * texture, Mesh * mesh, int firstIndex, int indexCount, int baseVertex);

	void present ();

	~GLRenderer ();

private:
	bool buildProgram (Program p, const char * vertexFile, const char * fragmentFile);
	void bindFramebuffer (RenderTarget * target);
	void applyState (const PipelineState & state);
	void setUniforms (const ProgramInfo & info, const DrawUniforms & uniforms);

	void (* swapBuffers) ();
	ProgramInfo programs[PROG_COUNT];
	RenderTarget * currentTarget;
	float antialiasScale;
	GLuint readFramebuffer;
	// We bind this texture for the units that a draw doesn't use. With
	// texture 0, which has no image, macOS warns about the samplers that
	// the shaders declare for those units.
	Texture * blank;
	// The meshes of this frame, which we free when we present it.
	std::vector<Mesh *> frameMeshes;
};

bool GLRenderer::buildProgram (Program p, const char * vertexFile, const char * fragmentFile) {
	char * vertex = shaderFileRead (vertexFile);
	char * fragment = shaderFileRead (fragmentFile);
	if (! vertex || ! fragment) {
		free (vertex);
		free (fragment);
		return false;
	}
#if defined(HAVE_GLES2)
	static const char precision[] = "precision mediump float;\n";
	char * withPrecision = (char *) malloc (sizeof (precision) + strlen (fragment));
	strcpy (withPrecision, precision);
	strcat (withPrecision, fragment);
	free (fragment);
	fragment = withPrecision;
#endif
	GLuint program = buildShaders (vertex, fragment);
	free (vertex);
	free (fragment);
	if (! program) return false;
	logDebug ("Built shader program: %d (%s)\n", program, fragmentFile);

	ProgramInfo & info = programs[p];
	info.program = program;
	info.matrix = glGetUniformLocation (program, "myPMVMatrix");
	info.color = glGetUniformLocation (program, "myColor");
	info.secondaryColor = glGetUniformLocation (program, "mySecondaryColor");
	info.useLightTexture = glGetUniformLocation (program, "useLightTexture");
	info.antialias = glGetUniformLocation (program, "antialias");
	info.scale = glGetUniformLocation (program, "scale");
	info.zBuffer = glGetUniformLocation (program, "zBuffer");
	info.zBufferLayer = glGetUniformLocation (program, "zBufferLayer");
	info.modulateColor = glGetUniformLocation (program, "modulateColor");
	info.tint = glGetUniformLocation (program, "tint");
	info.vertex = glGetAttribLocation (program, "myVertex");
	info.texCoords[0] = glGetAttribLocation (program, "myUV0");
	info.texCoords[1] = glGetAttribLocation (program, "myUV1");
	info.texCoords[2] = glGetAttribLocation (program, "myUV2");
	info.vertexColor = glGetAttribLocation (program, "myVertexColor");

	// The samplers read texture units 0, 1 and 2, in the order in which
	// the shaders declare them.
	static const char * const samplers[][3] = {
		{ "Texture", "lightTexture", NULL },
		{ "tex0", "tex1", "tex2" },
		{ "sampler2d", NULL, NULL },
		{ NULL, NULL, NULL },
		{ "Ytex", "Utex", "Vtex" },
		{ "sampler2d", NULL, NULL },
	};
	glUseProgram (program);
	for (int unit = 0; unit < 3; unit ++) {
		if (! samplers[p][unit]) continue;
		GLint location = glGetUniformLocation (program, samplers[p][unit]);
		if (location >= 0) glUniform1i (location, unit);
	}
	glUseProgram (0);
	return true;
}

bool GLRenderer::init (const char ** error) {
	static const char * const files[PROG_COUNT][2] = {
#if defined(HAVE_GLES2)
		// Without the extension for derivatives, we can't anti-alias in
		// the shader.
		{ "scale.vert", "scale_noaa.frag" },
#else
		{ "scale.vert", "scale.frag" },
#endif
		{ "fixScaleSprite.vert", "fixScaleSprite.frag" },
		{ "texture.vert", "texture.frag" },
		{ "color.vert", "color.frag" },
		{ "yuv.vert", "yuv.frag" },
		{ "vertexcolor.vert", "vertexcolor.frag" },
	};
	for (int p = 0; p < PROG_COUNT; p ++) {
		if (! buildProgram ((Program) p, files[p][0], files[p][1])) {
			*error = files[p][1];
			return false;
		}
	}

	glGenFramebuffers (1, &readFramebuffer);

	glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei (GL_PACK_ALIGNMENT, 1);
	glDisable (GL_CULL_FACE);
	glBlendFuncSeparate (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glClearColor (0.0f, 0.0f, 0.0f, 0.0f);

	static const unsigned char white[4] = { 255, 255, 255, 255 };
	blank = createTexture (1, 1, TEX_RGBA8, white);
	return true;
}

Texture * GLRenderer::createTexture (int width, int height, TexFormat format, const void * pixels) {
	Texture * t = new Texture;
	glGenTextures (1, &t->name);
	t->format = format;
	t->width = t->height = 0;
	countTexture (1);
	// The defaults of OpenGL, but with a minification filter that works
	// without mipmaps.
	t->minFilter = FILTER_NEAREST;
	t->magFilter = FILTER_LINEAR;
	t->wrapS = t->wrapT = WRAP_REPEAT;
	glBindTexture (GL_TEXTURE_2D, t->name);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	setTextureImage (t, width, height, pixels);
	return t;
}

void GLRenderer::setTextureImage (Texture * t, int width, int height, const void * pixels) {
	countTextureSize (t->format, t->width, t->height, width, height);
	t->width = width;
	t->height = height;
	glBindTexture (GL_TEXTURE_2D, t->name);
	glTexImage2D (GL_TEXTURE_2D, 0, glFormat (t->format), width, height, 0, glFormat (t->format), GL_UNSIGNED_BYTE, pixels);
}

void GLRenderer::updateTexture (Texture * t, int x, int y, int width, int height, const void * pixels) {
	glBindTexture (GL_TEXTURE_2D, t->name);
	glTexSubImage2D (GL_TEXTURE_2D, 0, x, y, width, height, glFormat (t->format), GL_UNSIGNED_BYTE, pixels);
}

void GLRenderer::destroyTexture (Texture * t) {
	if (! t) return;
	countTextureSize (t->format, t->width, t->height, 0, 0);
	countTexture (-1);
	glDeleteTextures (1, &t->name);
	delete t;
}

void GLRenderer::textureSize (const Texture * t, int * width, int * height) {
	*width = t->width;
	*height = t->height;
}

void GLRenderer::setTextureFilter (Texture * t, Filter minFilter, Filter magFilter) {
	t->minFilter = minFilter;
	t->magFilter = magFilter;
	glBindTexture (GL_TEXTURE_2D, t->name);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glFilter (minFilter));
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glFilter (magFilter));
}

void GLRenderer::setTextureWrap (Texture * t, Wrap s, Wrap t2) {
	t->wrapS = s;
	t->wrapT = t2;
	glBindTexture (GL_TEXTURE_2D, t->name);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, glWrap (s));
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, glWrap (t2));
}

void GLRenderer::readTexture (Texture * t, unsigned char * rgba) {
	// We attach the texture to a frame buffer object and read it, which
	// works with OpenGL ES too, which has no glGetTexImage.
	glBindFramebuffer (GL_FRAMEBUFFER, readFramebuffer);
	glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->name, 0);
	glReadPixels (0, 0, t->width, t->height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
	bindFramebuffer (currentTarget);
}

RenderTarget * GLRenderer::createTarget (Texture * color, bool depth) {
	RenderTarget * target = new RenderTarget;
	target->color = color;
	target->depth = 0;
	glGenFramebuffers (1, &target->framebuffer);
	glBindFramebuffer (GL_FRAMEBUFFER, target->framebuffer);
	glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color->name, 0);
	if (depth) {
		// 16 bits, like the depth buffer of the window, and because OpenGL
		// ES 2.0 has no other format without extensions.
		glGenRenderbuffers (1, &target->depth);
		glBindRenderbuffer (GL_RENDERBUFFER, target->depth);
		glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, color->width, color->height);
		glBindRenderbuffer (GL_RENDERBUFFER, 0);
		glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, target->depth);
	}
	GLenum status = glCheckFramebufferStatus (GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		logDebug ("Frame buffer object of %dx%d is incomplete: 0x%04X\n", color->width, color->height, status);
	}
	bindFramebuffer (currentTarget);
	return target;
}

void GLRenderer::detachTarget (RenderTarget * target) {
	if (currentTarget == target) setTarget (NULL);
}

void GLRenderer::destroyTarget (RenderTarget * target) {
	if (! target) return;
	detachTarget (target);
	glDeleteFramebuffers (1, &target->framebuffer);
	if (target->depth) glDeleteRenderbuffers (1, &target->depth);
	delete target;
}

void GLRenderer::bindFramebuffer (RenderTarget * target) {
	if (target) {
		glBindFramebuffer (GL_FRAMEBUFFER, target->framebuffer);
		return;
	}
#if defined(_WIN32) && ! defined(HAVE_GLES2)
	// With DXGI, a frame buffer object takes the place of the window.
	if (presentingWithDXGI ()) {
		glBindFramebuffer (GL_FRAMEBUFFER, dxgiFramebuffer ());
		return;
	}
#endif
	glBindFramebuffer (GL_FRAMEBUFFER, 0);
}

void GLRenderer::setTarget (RenderTarget * target) {
	currentTarget = target;
	bindFramebuffer (target);
}

void GLRenderer::setViewport (int x, int y, int width, int height) {
	glViewport (x, y, width, height);
}

void GLRenderer::setScissor (int x, int y, int width, int height) {
	if (width > 0) {
		glEnable (GL_SCISSOR_TEST);
		glScissor (x, y, width, height);
	} else {
		glDisable (GL_SCISSOR_TEST);
	}
}

void GLRenderer::clear (bool color, bool depth) {
	GLbitfield mask = 0;
	if (color) {
		glColorMask (GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		mask |= GL_COLOR_BUFFER_BIT;
	}
	if (depth) {
		glDepthMask (GL_TRUE);
		mask |= GL_DEPTH_BUFFER_BIT;
	}
	glClear (mask);
}

void GLRenderer::copyToTexture (Texture * dst, int dstX, int dstY, int srcX, int srcY, int width, int height) {
	glBindTexture (GL_TEXTURE_2D, dst->name);
	glCopyTexSubImage2D (GL_TEXTURE_2D, 0, dstX, dstY, srcX, srcY, width, height);
}

void GLRenderer::readPixels (int x, int y, int width, int height, unsigned char * rgba) {
	glReadPixels (x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

void GLRenderer::setAntialiasScale (float scale) {
	antialiasScale = scale;
}

void GLRenderer::applyState (const PipelineState & state) {
	if (state.blend == BLEND_ALPHA) glEnable (GL_BLEND);
	else glDisable (GL_BLEND);
	glColorMask ((state.colorMask & 1) != 0, (state.colorMask & 2) != 0, (state.colorMask & 4) != 0, (state.colorMask & 8) != 0);
	if (state.depth == DEPTH_OFF) {
		glDisable (GL_DEPTH_TEST);
	} else {
		glEnable (GL_DEPTH_TEST);
		glDepthFunc (GL_LESS);
	}
	glDepthMask (state.depth == DEPTH_TEST_WRITE);
}

void GLRenderer::setUniforms (const ProgramInfo & info, const DrawUniforms & u) {
	if (info.matrix >= 0) glUniformMatrix4fv (info.matrix, 1, GL_FALSE, u.matrix);
	if (info.color >= 0) glUniform4f (info.color, u.color[0], u.color[1], u.color[2], u.color[3]);
	if (info.secondaryColor >= 0) glUniform4f (info.secondaryColor, u.secondaryColor[0], u.secondaryColor[1], u.secondaryColor[2], u.secondaryColor[3]);
	if (info.tint >= 0) glUniform4f (info.tint, u.tint[0], u.tint[1], u.tint[2], u.tint[3]);
	if (info.useLightTexture >= 0) glUniform1i (info.useLightTexture, u.useLightTexture);
	if (info.antialias >= 0) glUniform1i (info.antialias, u.antialias);
	if (info.scale >= 0) glUniform1f (info.scale, antialiasScale);
	if (info.zBuffer >= 0) glUniform1i (info.zBuffer, u.zBuffer);
	if (info.zBufferLayer >= 0) glUniform1f (info.zBufferLayer, u.zBufferLayer);
	if (info.modulateColor >= 0) glUniform1i (info.modulateColor, u.modulateColor);
}

void GLRenderer::draw (const PipelineState & state, const DrawUniforms & uniforms, Texture * const textures[3], const Quad & quad) {
	const ProgramInfo & info = programs[state.program];
	countDraw (textures[0]);
	applyState (state);
	glUseProgram (info.program);
	setUniforms (info, uniforms);

	for (int unit = 2; unit >= 0; unit --) {
		glActiveTexture (GL_TEXTURE0 + unit);
		glBindTexture (GL_TEXTURE_2D, (textures[unit] ? textures[unit] : blank)->name);
	}

	glEnableVertexAttribArray (info.vertex);
	glVertexAttribPointer (info.vertex, 3, GL_FLOAT, GL_FALSE, 0, quad.vertices);
	for (int i = 0; i < 3; i ++) {
		if (info.texCoords[i] >= 0 && quad.texCoords[i]) {
			glEnableVertexAttribArray (info.texCoords[i]);
			glVertexAttribPointer (info.texCoords[i], 2, GL_FLOAT, GL_FALSE, 0, quad.texCoords[i]);
		}
	}

	glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);

	for (int i = 0; i < 3; i ++) {
		if (info.texCoords[i] >= 0 && quad.texCoords[i]) glDisableVertexAttribArray (info.texCoords[i]);
	}
	glDisableVertexAttribArray (info.vertex);
	glUseProgram (0);
}



Mesh * GLRenderer::createFrameMesh (const Vertex2D * vertices, int vertexCount, const unsigned short * indices, int indexCount) {
	Mesh * mesh = new Mesh;
	mesh->vertices.assign (vertices, vertices + vertexCount);
	mesh->indices.assign (indices, indices + indexCount);
	frameMeshes.push_back (mesh);
	return mesh;
}

void GLRenderer::drawMesh (const PipelineState & state, const DrawUniforms & uniforms, Texture * texture, Mesh * mesh, int firstIndex, int indexCount, int baseVertex) {
	if (indexCount <= 0) return;
	countMeshDraw (texture);
	const ProgramInfo & info = programs[PROG_VERTEX_COLOR];
	applyState (state);
	glUseProgram (info.program);
	setUniforms (info, uniforms);

	for (int unit = 2; unit >= 0; unit --) {
		glActiveTexture (GL_TEXTURE0 + unit);
		glBindTexture (GL_TEXTURE_2D, (unit == 0 && texture ? texture : blank)->name);
	}

	const Vertex2D * v = &mesh->vertices[baseVertex];
	GLsizei stride = sizeof (Vertex2D);
	glEnableVertexAttribArray (info.vertex);
	glVertexAttribPointer (info.vertex, 2, GL_FLOAT, GL_FALSE, stride, &v->x);
	glEnableVertexAttribArray (info.texCoords[0]);
	glVertexAttribPointer (info.texCoords[0], 2, GL_FLOAT, GL_FALSE, stride, &v->u);
	glEnableVertexAttribArray (info.vertexColor);
	glVertexAttribPointer (info.vertexColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, v->color);

	glDrawElements (GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, &mesh->indices[firstIndex]);

	glDisableVertexAttribArray (info.vertexColor);
	glDisableVertexAttribArray (info.texCoords[0]);
	glDisableVertexAttribArray (info.vertex);
	glUseProgram (0);
}

void GLRenderer::present () {
	glFlush ();
	swapBuffers ();
	for (size_t i = 0; i < frameMeshes.size (); i ++) delete frameMeshes[i];
	frameMeshes.clear ();
	// OpenGL waits itself for draws that use a deleted texture.
	destroyReleasedTextures (false);
	finishFrameStats ();
}

GLRenderer::~GLRenderer () {
	destroyReleasedTextures (true);
	for (size_t i = 0; i < frameMeshes.size (); i ++) delete frameMeshes[i];
}

} // namespace

Renderer * createGLRenderer (void (* swapBuffers) (), const char ** error) {
	GLRenderer * r = new GLRenderer (swapBuffers);
	if (! r->init (error)) {
		delete r;
		return NULL;
	}
	return r;
}

} // namespace gfx
