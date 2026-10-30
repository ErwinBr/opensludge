#ifndef SLUDGE_RENDERER_H
#define SLUDGE_RENDERER_H

/*
 * The engine draws through this interface, so that it can draw with OpenGL
 * (renderer_gl.cpp) or, later, with SDL_GPU (Metal, Vulkan, Direct3D 12).
 *
 * Coordinates follow OpenGL:
 *
 * - The matrix of a draw maps the vertices to normalised device coordinates,
 *   where y = -1 is the bottom of the window, and row 0 of a texture that we
 *   draw into.
 * - Viewports and rectangles count pixels from that bottom row.
 * - Row 0 of a texture is the first row of pixels that we upload or read.
 *   For the backdrop, sprites and images of the game, that is their top row.
 *
 * A texture keeps its filter and wrap modes, as in OpenGL. The SDL_GPU
 * renderer chooses a sampler from them when it draws.
 */

namespace gfx {

enum TexFormat {
	TEX_RGBA8,
	TEX_R8,			// One channel, which the shaders read as alpha
};

enum Filter { FILTER_NEAREST, FILTER_LINEAR };
enum Wrap { WRAP_CLAMP, WRAP_REPEAT };

// The shader programs (source/Engine/*.vert and *.frag).
enum Program {
	PROG_SMART_SCALER,	// Sprites, fonts and the backdrop, with optional anti-aliasing
	PROG_PASTE,			// Pasting onto the backdrop: blends with the backdrop on unit 2
	PROG_TEXTURE,		// A texture, optionally as z-buffer layer or multiplied by the colour
	PROG_COLOR,			// A solid colour
	PROG_YUV,			// The Y, U and V planes of a movie frame
	PROG_COUNT
};

enum Blend {
	BLEND_OFF,
	BLEND_ALPHA,		// Colour: source alpha; alpha: one, one minus source alpha
};

enum ColorMask {
	MASK_NONE = 0,
	MASK_RGB = 7,
	MASK_RGBA = 15,
};

enum DepthMode {
	DEPTH_OFF,
	DEPTH_TEST,			// Pass when nearer, without writing depth
	DEPTH_TEST_WRITE,	// Pass when nearer, and write depth
};

struct PipelineState {
	Program program;
	Blend blend;
	ColorMask colorMask;
	DepthMode depth;

	PipelineState (Program p, Blend b = BLEND_OFF, DepthMode d = DEPTH_OFF, ColorMask m = MASK_RGBA)
		: program (p), blend (b), colorMask (m), depth (d) {}
};

struct DrawUniforms {
	float matrix[16];
	float color[4];
	float secondaryColor[4];
	bool useLightTexture;	// PROG_SMART_SCALER and PROG_PASTE
	bool antialias;			// PROG_SMART_SCALER
	bool zBuffer;			// PROG_TEXTURE
	float zBufferLayer;		// PROG_TEXTURE
	bool modulateColor;		// PROG_TEXTURE

	// The matrix and colours from setPMVMatrix and setPrimaryColor in
	// graphics.cpp, and false and 0 for the rest.
	DrawUniforms ();
};

// Four vertices, drawn as a triangle strip: (x1, y1), (x2, y1), (x1, y2),
// (x2, y2). Each has x, y and z, and texture coordinates for up to three
// textures. Coordinates that are NULL are not used by the program.
struct Quad {
	const float * vertices;		// 12 floats
	const float * texCoords[3];	// 8 floats each, or NULL
};

struct Texture;
struct RenderTarget;

class Renderer {
public:
	virtual ~Renderer () {}

	// Textures. Pixels are tightly packed, 4 or 1 bytes each, and may be
	// NULL for a texture whose contents we draw.
	virtual Texture * createTexture (int width, int height, TexFormat format, const void * pixels) = 0;
	// Replace the whole contents of a texture, possibly with a new size.
	virtual void setTextureImage (Texture * t, int width, int height, const void * pixels) = 0;
	virtual void updateTexture (Texture * t, int x, int y, int width, int height, const void * pixels) = 0;
	virtual void destroyTexture (Texture * t) = 0;
	virtual void textureSize (const Texture * t, int * width, int * height) = 0;
	virtual void setTextureFilter (Texture * t, Filter minFilter, Filter magFilter) = 0;
	virtual void setTextureWrap (Texture * t, Wrap s, Wrap t2) = 0;
	// Read the whole texture as RGBA.
	virtual void readTexture (Texture * t, unsigned char * rgba) = 0;

	// Render targets draw into a texture, with an optional depth buffer.
	virtual RenderTarget * createTarget (Texture * color, bool depth) = 0;
	virtual void destroyTarget (RenderTarget * target) = 0;
	// Draw into a target, or into the window with NULL.
	virtual void setTarget (RenderTarget * target) = 0;
	virtual void setViewport (int x, int y, int width, int height) = 0;
	// Draw and clear only in this rectangle. A width of 0 turns it off.
	virtual void setScissor (int x, int y, int width, int height) = 0;
	// Clear to transparent black and a depth of 1.
	virtual void clear (bool color, bool depth) = 0;
	// Copy pixels of the current target into a texture.
	virtual void copyToTexture (Texture * dst, int dstX, int dstY, int srcX, int srcY, int width, int height) = 0;
	// Read pixels of the current target as RGBA, the bottom row first.
	virtual void readPixels (int x, int y, int width, int height, unsigned char * rgba) = 0;

	// The scale for the anti-aliasing of PROG_SMART_SCALER: how far apart
	// it samples the texture, from the size of the game in the window.
	virtual void setAntialiasScale (float scale) = 0;

	// textures[i] is the texture for unit i, or NULL.
	virtual void draw (const PipelineState & state, const DrawUniforms & uniforms, Texture * const textures[3], const Quad & quad) = 0;
	// A line of the given width, with PROG_COLOR.
	virtual void drawLine (const PipelineState & state, const DrawUniforms & uniforms, int x1, int y1, int x2, int y2, float width) = 0;

	virtual void present () = 0;
};

extern Renderer * renderer;

// Make the OpenGL renderer for the current OpenGL context. Returns NULL and
// sets *error when the driver lacks something we need.
Renderer * createGLRenderer (const char ** error);

// Shortcuts for drawing with one texture or none.
void draw (const PipelineState & state, const DrawUniforms & uniforms, Texture * texture, const float * vertices, const float * texCoords);
void draw (const PipelineState & state, const DrawUniforms & uniforms, const float * vertices);

} // namespace gfx

#endif
