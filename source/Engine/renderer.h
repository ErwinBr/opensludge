#ifndef SLUDGE_RENDERER_H
#define SLUDGE_RENDERER_H

/*
 * The engine and the Dev Kit draw through this interface, so that they can
 * draw with OpenGL (renderer_gl.cpp) or with SDL_GPU (renderer_gpu.cpp:
 * Metal, Vulkan, Direct3D 12). The renderers are the library
 * sludge-renderer, which knows nothing of the engine.
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

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <vector>

struct SDL_GPUDevice;
struct SDL_Window;

namespace gfx {

// The renderers write details that help to find problems, such as the logs
// of the shader compilers and OpenGL errors, with this function. They write
// nothing until a program sets one. Problems that stop a renderer from
// drawing, and which renderer draws, go to stderr regardless.
typedef void (* LogFunction) (const char * format, va_list args);
void setLogFunction (LogFunction log);
void logDebug (const char * format, ...);

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
	PROG_VERTEX_COLOR,	// Meshes: a texture times the colour of each vertex and the colour
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

	// The identity matrix, white, transparent black as the secondary
	// colour, and false and 0 for the rest. The engine starts from
	// engineUniforms () in graphics.h instead.
	DrawUniforms ();
};

// Four vertices, drawn as a triangle strip: (x1, y1), (x2, y1), (x1, y2),
// (x2, y2). Each has x, y and z, and texture coordinates for up to three
// textures. Coordinates that are NULL are not used by the program.
struct Quad {
	const float * vertices;		// 12 floats
	const float * texCoords[3];	// 8 floats each, or NULL
};

// A vertex of a mesh: x and y, the texture coordinates, and red, green,
// blue and alpha. It has the layout of ImDrawVert of Dear ImGui.
struct Vertex2D {
	float x, y;
	float u, v;
	unsigned char color[4];
};

struct Texture;
struct RenderTarget;
struct Mesh;

// What a frame drew, for measuring (SLUDGE_RENDER_STATS in the engine).
struct RenderStats {
	unsigned int draws;				// Quads
	unsigned int meshDraws;
	unsigned int textureSwitches;	// Draws with another texture on unit 0 than the draw before
	unsigned int textures;			// Alive at the end of the frame
	size_t textureBytes;			// Their pixels
};

class Renderer {
public:
	Renderer ();
	virtual ~Renderer () {}

	// Destroy a texture once the GPU can no longer be using it: after the
	// frames that may still be on their way to the screen. destroyTexture
	// destroys it at once, which is only safe for a texture that no draw
	// since the last present used. Call both from the thread that made the
	// renderer.
	void releaseTexture (Texture * t);
	// The same for a render target. If we draw into it, we draw into the
	// window from now on, as after destroyTarget.
	void releaseTarget (RenderTarget * target);
	// How many released textures we haven't destroyed yet.
	size_t texturesBeingReleased () const { return released.size (); }
	// What the last presented frame drew.
	const RenderStats & lastFrameStats () const { return lastStats; }

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

	// Meshes of triangles, for the Dev Kit. A mesh is a copy of vertices
	// and 16-bit indices into them, which we can draw until the next
	// present. Copy all the meshes of a frame into one, if you can: with
	// SDL_GPU, each copy ends a render pass.
	virtual Mesh * createFrameMesh (const Vertex2D * vertices, int vertexCount, const unsigned short * indices, int indexCount) = 0;
	// Draw indexCount indices from firstIndex as triangles, with
	// baseVertex added to each index, and the texture on unit 0, or white
	// with NULL. state.program must be PROG_VERTEX_COLOR.
	virtual void drawMesh (const PipelineState & state, const DrawUniforms & uniforms, Texture * texture, Mesh * mesh, int firstIndex, int indexCount, int baseVertex) = 0;

	virtual void present () = 0;

protected:
	// The renderers call this at the end of present, and with all = true in
	// their destructor, once the GPU is idle.
	void destroyReleasedTextures (bool all);
	// How many presents after its release we destroy a texture.
	int framesInFlight;
	// Stop drawing into a target that is going (destroyTarget does it too).
	virtual void detachTarget (RenderTarget * target) = 0;

	// The renderers count what they draw and keep with these.
	void countDraw (const Texture * unit0);
	void countMeshDraw (const Texture * texture);
	void countTextureSize (TexFormat format, int oldWidth, int oldHeight, int newWidth, int newHeight);
	void countTexture (int change) { stats.textures += change; }
	void finishFrameStats ();

private:
	struct Released {
		Texture * texture;			// Or NULL, and
		RenderTarget * target;		// a target, or NULL
		uint64_t frame;
	};
	std::vector<Released> released;
	uint64_t frame;
	RenderStats stats, lastStats;
	const Texture * lastUnit0;
	uint64_t ownerThread;

	Renderer (const Renderer &);
	Renderer & operator= (const Renderer &);
};

// Owns a texture, and releases it with releaseTexture when it goes.
class UniqueTexture {
public:
	UniqueTexture () : owner (NULL), texture (NULL) {}
	UniqueTexture (Renderer * owner, Texture * texture) : owner (owner), texture (texture) {}
	~UniqueTexture () { reset (); }
	UniqueTexture (UniqueTexture && other) : owner (other.owner), texture (other.texture) {
		other.owner = NULL;
		other.texture = NULL;
	}
	UniqueTexture & operator= (UniqueTexture && other) {
		if (this != &other) {
			reset ();
			owner = other.owner;
			texture = other.texture;
			other.owner = NULL;
			other.texture = NULL;
		}
		return *this;
	}

	Texture * get () const { return texture; }
	void reset () {
		if (texture && owner) owner->releaseTexture (texture);
		owner = NULL;
		texture = NULL;
	}

private:
	Renderer * owner;
	Texture * texture;

	UniqueTexture (const UniqueTexture &);
	UniqueTexture & operator= (const UniqueTexture &);
};

// Make the OpenGL renderer for the current OpenGL context, which shows each
// frame with swapBuffers. Returns NULL and sets *error when the driver lacks
// something we need.
Renderer * createGLRenderer (void (* swapBuffers) (), const char ** error);
// Make the SDL_GPU renderer for a device that has claimed the window.
Renderer * createGPURenderer (SDL_GPUDevice * device, SDL_Window * window, const char ** error);

} // namespace gfx

#endif
