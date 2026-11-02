/*
 * The SDL_GPU renderer: Metal on macOS, Vulkan or Direct3D 12 elsewhere.
 *
 * renderer.h follows the conventions of OpenGL, so we keep them here too:
 *
 * - Every texture, including the one that stands for the window, keeps the
 *   rows of OpenGL: row 0 is what OpenGL calls the bottom row. In SDL_GPU,
 *   y = +1 in normalised device coordinates is row 0 of a texture, so we
 *   flip y in the matrix of every draw. When we present a frame, we copy the
 *   window texture into the window upside down.
 * - OpenGL maps z from -1 to 1 to depths from 0 to 1, and SDL_GPU from 0
 *   to 1, so we change z in the matrix too.
 * - Instead of a viewport, which must lie inside the texture in Metal, we
 *   draw into the whole texture with the viewport folded into the matrix,
 *   and a scissor rectangle where the viewport and the scissor rectangle of
 *   OpenGL overlap.
 * - OpenGL clears in the scissor rectangle. A render pass can only clear
 *   the whole texture, so we clear a part by drawing a quad.
 *
 * We record all drawing into one command buffer for each frame, and submit
 * it when we present the frame, or earlier when we read pixels back.
 *
 * Meshes go into a vertex buffer and an index buffer, one after another in
 * each frame. We start at the beginning again in the next frame, and let
 * SDL cycle the buffers, so that we don't write over a frame that the GPU
 * may still be drawing.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <map>
#include <vector>

#include <SDL3/SDL.h>

#include "renderer.h"
#include "shaders/gpu/gpu_shaders.h"

namespace gfx {

struct Texture {
	SDL_GPUTexture * texture;
	int width, height;
	TexFormat format;
	Filter minFilter, magFilter;
	Wrap wrapS, wrapT;
};

struct RenderTarget {
	Texture * color;
	SDL_GPUTexture * depth;
	int depthWidth, depthHeight;
};

struct Mesh {
	SDL_GPUBuffer * vertices;
	SDL_GPUBuffer * indices;
	Uint32 vertexOffset, indexOffset;	// In bytes
};

namespace {

// The uniform blocks of the shaders (shaders/gpu/quad.vert, mesh.vert and
// uniforms.glsl), in the layout of std140.
struct VertexUniforms {
	float matrix[16];
	float positions[4][4];
	float texCoords01[4][4];
	float texCoords2[4][4];
};

struct MeshUniforms {
	float matrix[16];
};

struct FragmentUniforms {
	float color[4];
	float secondaryColor[4];
	float scale;
	float zBufferLayer;
	int32_t antialias;
	int32_t useLightTexture;
	int32_t zBuffer;
	int32_t modulateColor;
	float padding[2];
};

// The fragment shader of each program, with its number of samplers and
// uniform buffers.
struct ProgramInfo {
	const char * shader;
	Uint32 numSamplers;
	Uint32 numUniformBuffers;
};

const ProgramInfo programs[PROG_COUNT] = {
	{ "scale.frag", 2, 1 },
	{ "paste.frag", 3, 1 },
	{ "texture.frag", 1, 1 },
	{ "color.frag", 0, 1 },
	{ "yuv.frag", 3, 0 },
	{ "vertexcolor.frag", 1, 1 },
};

// A vertex buffer or index buffer for meshes, which we fill from the start
// in each frame.
struct MeshBuffer {
	SDL_GPUBuffer * buffer;
	Uint32 capacity, used;
};

SDL_GPUTextureFormat gpuFormat (TexFormat format) {
	return format == TEX_R8 ? SDL_GPU_TEXTUREFORMAT_R8_UNORM : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
}

int bytesPerPixel (TexFormat format) {
	return format == TEX_R8 ? 1 : 4;
}

class GPURenderer : public Renderer {
public:
	GPURenderer (SDL_GPUDevice * d, SDL_Window * w);
	~GPURenderer ();

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

private:
	SDL_GPUShader * makeShader (const char * name, SDL_GPUShaderStage stage, Uint32 numSamplers, Uint32 numUniformBuffers);
	SDL_GPUGraphicsPipeline * pipeline (Program program, Blend blend, ColorMask mask, DepthMode depth, bool hasDepth, bool clearing);
	SDL_GPUSampler * sampler (const Texture * t);
	SDL_GPUTexture * makeTexture (int width, int height, SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage);
	RenderTarget * target ();
	void updateWindowTarget ();
	SDL_GPUCommandBuffer * commandBuffer ();
	void beginPass (bool clearColor, bool clearDepth);
	void endPass ();
	bool scissorRect (bool withViewport, SDL_Rect * rect);
	void download (SDL_GPUTexture * texture, int x, int y, int width, int height, int bpp, unsigned char * out);
	bool startDraw (Program program, const PipelineState & state);
	void drawMatrix (const float * matrix, float * out);
	void pushFragmentUniforms (Program program, const DrawUniforms & u);
	Uint32 reserve (MeshBuffer & b, SDL_GPUBufferUsageFlags usage, Uint32 size, bool * cycle);

	SDL_GPUDevice * device;
	SDL_Window * window;
	SDL_GPUShaderFormat shaderFormat;	// MSL, DXIL or SPIR-V, as the driver takes
	SDL_GPUShader * vertexShader;
	SDL_GPUShader * meshVertexShader;
	SDL_GPUShader * fragmentShaders[PROG_COUNT];
	std::map<Uint32, SDL_GPUGraphicsPipeline *> pipelines;
	SDL_GPUSampler * samplers[16];
	Texture * blank;

	SDL_GPUCommandBuffer * cmd;
	SDL_GPURenderPass * pass;
	RenderTarget * current;		// NULL for the window
	Texture * windowTexture;
	RenderTarget * windowTarget;

	MeshBuffer meshVertices, meshIndices;
	std::vector<Mesh *> frameMeshes;
	std::vector<SDL_GPUBuffer *> retiredBuffers;	// Too small, freed when we present

	int viewport[4];
	int scissor[4];
	bool scissorOn;
	float antialiasScale;
};

GPURenderer::GPURenderer (SDL_GPUDevice * d, SDL_Window * w)
	: device (d), window (w), shaderFormat (SDL_GPU_SHADERFORMAT_INVALID), vertexShader (NULL), meshVertexShader (NULL), blank (NULL),
	  cmd (NULL), pass (NULL), current (NULL), windowTexture (NULL), windowTarget (NULL),
	  scissorOn (false), antialiasScale (1.0f) {
	memset (fragmentShaders, 0, sizeof (fragmentShaders));
	memset (samplers, 0, sizeof (samplers));
	memset (viewport, 0, sizeof (viewport));
	memset (scissor, 0, sizeof (scissor));
	memset (&meshVertices, 0, sizeof (meshVertices));
	memset (&meshIndices, 0, sizeof (meshIndices));
}

GPURenderer::~GPURenderer () {
	endPass ();
	if (cmd) SDL_SubmitGPUCommandBuffer (cmd);
	SDL_WaitForGPUIdle (device);
	for (std::map<Uint32, SDL_GPUGraphicsPipeline *>::iterator i = pipelines.begin (); i != pipelines.end (); ++ i) {
		SDL_ReleaseGPUGraphicsPipeline (device, i->second);
	}
	for (int i = 0; i < 16; i ++) {
		if (samplers[i]) SDL_ReleaseGPUSampler (device, samplers[i]);
	}
	for (int p = 0; p < PROG_COUNT; p ++) {
		if (fragmentShaders[p]) SDL_ReleaseGPUShader (device, fragmentShaders[p]);
	}
	if (vertexShader) SDL_ReleaseGPUShader (device, vertexShader);
	if (meshVertexShader) SDL_ReleaseGPUShader (device, meshVertexShader);
	for (size_t i = 0; i < frameMeshes.size (); i ++) delete frameMeshes[i];
	for (size_t i = 0; i < retiredBuffers.size (); i ++) SDL_ReleaseGPUBuffer (device, retiredBuffers[i]);
	if (meshVertices.buffer) SDL_ReleaseGPUBuffer (device, meshVertices.buffer);
	if (meshIndices.buffer) SDL_ReleaseGPUBuffer (device, meshIndices.buffer);
	destroyTarget (windowTarget);
	destroyTexture (windowTexture);
	destroyTexture (blank);
}

SDL_GPUShader * GPURenderer::makeShader (const char * name, SDL_GPUShaderStage stage, Uint32 numSamplers, Uint32 numUniformBuffers) {
	const gpuShaderCode * code = gpuShaders;
	while (code->name && strcmp (code->name, name)) code ++;
	if (! code->name) return NULL;

	SDL_GPUShaderCreateInfo info;
	SDL_zero (info);
	if (shaderFormat == SDL_GPU_SHADERFORMAT_MSL) {
		info.code = (const Uint8 *) code->msl;
		info.code_size = strlen (code->msl);
		info.entrypoint = "main0";
	} else if (shaderFormat == SDL_GPU_SHADERFORMAT_DXIL) {
		info.code = code->dxil;
		info.code_size = code->dxilSize;
		info.entrypoint = "main";
	} else {
		info.code = code->spirv;
		info.code_size = code->spirvSize;
		info.entrypoint = "main";
	}
	info.format = shaderFormat;
	info.stage = stage;
	info.num_samplers = numSamplers;
	info.num_uniform_buffers = numUniformBuffers;
	SDL_GPUShader * shader = SDL_CreateGPUShader (device, &info);
	if (! shader) logDebug ("Can't make the shader %s: %s\n", name, SDL_GetError ());
	return shader;
}

bool GPURenderer::init (const char ** error) {
	SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats (device);
	if (formats & SDL_GPU_SHADERFORMAT_MSL) shaderFormat = SDL_GPU_SHADERFORMAT_MSL;
	else if (formats & SDL_GPU_SHADERFORMAT_DXIL) shaderFormat = SDL_GPU_SHADERFORMAT_DXIL;
	else shaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
	vertexShader = makeShader ("quad.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
	if (! vertexShader) {
		*error = "quad.vert";
		return false;
	}
	meshVertexShader = makeShader ("mesh.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
	if (! meshVertexShader) {
		*error = "mesh.vert";
		return false;
	}
	for (int p = 0; p < PROG_COUNT; p ++) {
		fragmentShaders[p] = makeShader (programs[p].shader, SDL_GPU_SHADERSTAGE_FRAGMENT, programs[p].numSamplers, programs[p].numUniformBuffers);
		if (! fragmentShaders[p]) {
			*error = programs[p].shader;
			return false;
		}
	}
	// We bind this texture for the units that a draw doesn't use.
	static const unsigned char white[4] = { 255, 255, 255, 255 };
	blank = createTexture (1, 1, TEX_RGBA8, white);
	return true;
}

SDL_GPUTexture * GPURenderer::makeTexture (int width, int height, SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage) {
	SDL_GPUTextureCreateInfo info;
	SDL_zero (info);
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = format;
	info.usage = usage;
	info.width = width > 0 ? width : 1;
	info.height = height > 0 ? height : 1;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	info.sample_count = SDL_GPU_SAMPLECOUNT_1;
	return SDL_CreateGPUTexture (device, &info);
}

SDL_GPUCommandBuffer * GPURenderer::commandBuffer () {
	if (! cmd) cmd = SDL_AcquireGPUCommandBuffer (device);
	return cmd;
}

RenderTarget * GPURenderer::target () {
	if (current) return current;
	updateWindowTarget ();
	return windowTarget;
}

void GPURenderer::updateWindowTarget () {
	int w, h;
	SDL_GetWindowSizeInPixels (window, &w, &h);
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	if (windowTexture && windowTexture->width == w && windowTexture->height == h) return;
	if (! current) endPass ();
	destroyTarget (windowTarget);
	destroyTexture (windowTexture);
	windowTexture = createTexture (w, h, TEX_RGBA8, NULL);
	windowTarget = createTarget (windowTexture, true);
}

void GPURenderer::beginPass (bool clearColor, bool clearDepth) {
	RenderTarget * t = target ();
	SDL_GPUColorTargetInfo color;
	SDL_zero (color);
	color.texture = t->color->texture;
	color.load_op = clearColor ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
	color.store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPUDepthStencilTargetInfo depth;
	SDL_zero (depth);
	if (t->depth) {
		depth.texture = t->depth;
		depth.load_op = clearDepth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
		depth.store_op = SDL_GPU_STOREOP_STORE;
		depth.clear_depth = 1.0f;
		depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
		depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
	}
	pass = SDL_BeginGPURenderPass (commandBuffer (), &color, 1, t->depth ? &depth : NULL);
}

void GPURenderer::endPass () {
	if (pass) SDL_EndGPURenderPass (pass);
	pass = NULL;
}

Texture * GPURenderer::createTexture (int width, int height, TexFormat format, const void * pixels) {
	Texture * t = new Texture;
	t->texture = NULL;
	t->width = t->height = 0;
	t->format = format;
	t->minFilter = FILTER_NEAREST;
	t->magFilter = FILTER_LINEAR;
	t->wrapS = t->wrapT = WRAP_REPEAT;
	setTextureImage (t, width, height, pixels);
	return t;
}

void GPURenderer::setTextureImage (Texture * t, int width, int height, const void * pixels) {
	if (! t->texture || t->width != width || t->height != height) {
		if (t->texture) SDL_ReleaseGPUTexture (device, t->texture);
		// Any texture with colour can be a render target.
		SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
		if (t->format == TEX_RGBA8) usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
		t->texture = makeTexture (width, height, gpuFormat (t->format), usage);
		t->width = width;
		t->height = height;
	}
	if (pixels) {
		updateTexture (t, 0, 0, width, height, pixels);
	} else if (t->format == TEX_RGBA8) {
		// We start with transparent black, so that nothing depends on
		// what was in the memory before.
		endPass ();
		SDL_GPUColorTargetInfo color;
		SDL_zero (color);
		color.texture = t->texture;
		color.load_op = SDL_GPU_LOADOP_CLEAR;
		color.store_op = SDL_GPU_STOREOP_STORE;
		SDL_EndGPURenderPass (SDL_BeginGPURenderPass (commandBuffer (), &color, 1, NULL));
	}
}

void GPURenderer::updateTexture (Texture * t, int x, int y, int width, int height, const void * pixels) {
	if (width <= 0 || height <= 0) return;
	Uint32 size = (Uint32) width * height * bytesPerPixel (t->format);

	SDL_GPUTransferBufferCreateInfo info;
	SDL_zero (info);
	info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	info.size = size;
	SDL_GPUTransferBuffer * transfer = SDL_CreateGPUTransferBuffer (device, &info);
	void * mapped = SDL_MapGPUTransferBuffer (device, transfer, false);
	memcpy (mapped, pixels, size);
	SDL_UnmapGPUTransferBuffer (device, transfer);

	endPass ();
	SDL_GPUCopyPass * copy = SDL_BeginGPUCopyPass (commandBuffer ());
	SDL_GPUTextureTransferInfo source;
	SDL_zero (source);
	source.transfer_buffer = transfer;
	source.pixels_per_row = width;
	source.rows_per_layer = height;
	SDL_GPUTextureRegion destination;
	SDL_zero (destination);
	destination.texture = t->texture;
	destination.x = x;
	destination.y = y;
	destination.w = width;
	destination.h = height;
	destination.d = 1;
	SDL_UploadToGPUTexture (copy, &source, &destination, false);
	SDL_EndGPUCopyPass (copy);
	// SDL keeps the buffer until the upload is done.
	SDL_ReleaseGPUTransferBuffer (device, transfer);
}

void GPURenderer::destroyTexture (Texture * t) {
	if (! t) return;
	SDL_ReleaseGPUTexture (device, t->texture);
	delete t;
}

void GPURenderer::textureSize (const Texture * t, int * width, int * height) {
	*width = t->width;
	*height = t->height;
}

void GPURenderer::setTextureFilter (Texture * t, Filter minFilter, Filter magFilter) {
	t->minFilter = minFilter;
	t->magFilter = magFilter;
}

void GPURenderer::setTextureWrap (Texture * t, Wrap s, Wrap t2) {
	t->wrapS = s;
	t->wrapT = t2;
}

SDL_GPUSampler * GPURenderer::sampler (const Texture * t) {
	int key = t->minFilter | (t->magFilter << 1) | (t->wrapS << 2) | (t->wrapT << 3);
	if (! samplers[key]) {
		SDL_GPUSamplerCreateInfo info;
		SDL_zero (info);
		info.min_filter = t->minFilter == FILTER_LINEAR ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
		info.mag_filter = t->magFilter == FILTER_LINEAR ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
		info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
		info.address_mode_u = t->wrapS == WRAP_REPEAT ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		info.address_mode_v = t->wrapT == WRAP_REPEAT ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		samplers[key] = SDL_CreateGPUSampler (device, &info);
	}
	return samplers[key];
}

void GPURenderer::download (SDL_GPUTexture * texture, int x, int y, int width, int height, int bpp, unsigned char * out) {
	Uint32 size = (Uint32) width * height * bpp;
	SDL_GPUTransferBufferCreateInfo info;
	SDL_zero (info);
	info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
	info.size = size;
	SDL_GPUTransferBuffer * transfer = SDL_CreateGPUTransferBuffer (device, &info);

	endPass ();
	SDL_GPUCopyPass * copy = SDL_BeginGPUCopyPass (commandBuffer ());
	SDL_GPUTextureRegion source;
	SDL_zero (source);
	source.texture = texture;
	source.x = x;
	source.y = y;
	source.w = width;
	source.h = height;
	source.d = 1;
	SDL_GPUTextureTransferInfo destination;
	SDL_zero (destination);
	destination.transfer_buffer = transfer;
	destination.pixels_per_row = width;
	destination.rows_per_layer = height;
	SDL_DownloadFromGPUTexture (copy, &source, &destination);
	SDL_EndGPUCopyPass (copy);

	// We wait for everything so far, and go on in a new command buffer.
	SDL_GPUFence * fence = SDL_SubmitGPUCommandBufferAndAcquireFence (cmd);
	cmd = NULL;
	SDL_WaitForGPUFences (device, true, &fence, 1);
	SDL_ReleaseGPUFence (device, fence);

	void * mapped = SDL_MapGPUTransferBuffer (device, transfer, false);
	memcpy (out, mapped, size);
	SDL_UnmapGPUTransferBuffer (device, transfer);
	SDL_ReleaseGPUTransferBuffer (device, transfer);
}

void GPURenderer::readTexture (Texture * t, unsigned char * rgba) {
	download (t->texture, 0, 0, t->width, t->height, bytesPerPixel (t->format), rgba);
}

RenderTarget * GPURenderer::createTarget (Texture * color, bool depth) {
	RenderTarget * t = new RenderTarget;
	t->color = color;
	t->depth = NULL;
	t->depthWidth = t->depthHeight = 0;
	if (depth) {
		t->depth = makeTexture (color->width, color->height, SDL_GPU_TEXTUREFORMAT_D16_UNORM, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
		t->depthWidth = color->width;
		t->depthHeight = color->height;
	}
	return t;
}

void GPURenderer::destroyTarget (RenderTarget * t) {
	if (! t) return;
	if (current == t) {
		endPass ();
		current = NULL;
	}
	if (t->depth) SDL_ReleaseGPUTexture (device, t->depth);
	delete t;
}

void GPURenderer::setTarget (RenderTarget * t) {
	if (t != current || ! t) endPass ();
	current = t;
	if (! t) updateWindowTarget ();
}

void GPURenderer::setViewport (int x, int y, int width, int height) {
	viewport[0] = x;
	viewport[1] = y;
	viewport[2] = width;
	viewport[3] = height;
}

void GPURenderer::setScissor (int x, int y, int width, int height) {
	scissorOn = width > 0;
	scissor[0] = x;
	scissor[1] = y;
	scissor[2] = width;
	scissor[3] = height;
}

// Where we may draw in the current target, in its rows: the scissor
// rectangle, and the viewport too unless we clear.
bool GPURenderer::scissorRect (bool withViewport, SDL_Rect * rect) {
	RenderTarget * t = target ();
	int x1 = 0, y1 = 0, x2 = t->color->width, y2 = t->color->height;
	if (withViewport) {
		if (viewport[0] > x1) x1 = viewport[0];
		if (viewport[1] > y1) y1 = viewport[1];
		if (viewport[0] + viewport[2] < x2) x2 = viewport[0] + viewport[2];
		if (viewport[1] + viewport[3] < y2) y2 = viewport[1] + viewport[3];
	}
	if (scissorOn) {
		if (scissor[0] > x1) x1 = scissor[0];
		if (scissor[1] > y1) y1 = scissor[1];
		if (scissor[0] + scissor[2] < x2) x2 = scissor[0] + scissor[2];
		if (scissor[1] + scissor[3] < y2) y2 = scissor[1] + scissor[3];
	}
	if (x2 <= x1 || y2 <= y1) return false;
	rect->x = x1;
	rect->y = y1;
	rect->w = x2 - x1;
	rect->h = y2 - y1;
	return true;
}

void GPURenderer::clear (bool color, bool depth) {
	RenderTarget * t = target ();
	if (! t->depth) depth = false;
	if (! color && ! depth) return;
	SDL_Rect rect;
	if (! scissorRect (false, &rect)) return;

	// To clear the whole target, we start a render pass that clears.
	bool whole = rect.x == 0 && rect.y == 0 && rect.w == t->color->width && rect.h == t->color->height;
	if (whole) {
		endPass ();
		beginPass (color, depth);
		return;
	}

	if (! pass) beginPass (false, false);
	SDL_GPUViewport vp = { 0, 0, (float) t->color->width, (float) t->color->height, 0, 1 };
	SDL_SetGPUViewport (pass, &vp);
	SDL_SetGPUScissor (pass, &rect);
	SDL_BindGPUGraphicsPipeline (pass, pipeline (PROG_COLOR, BLEND_OFF, color ? MASK_RGBA : MASK_NONE,
		depth ? DEPTH_TEST_WRITE : DEPTH_OFF, t->depth != NULL, true));

	VertexUniforms v;
	memset (&v, 0, sizeof (v));
	v.matrix[0] = v.matrix[5] = v.matrix[10] = v.matrix[15] = 1.0f;
	static const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
	for (int i = 0; i < 4; i ++) {
		v.positions[i][0] = corners[i][0];
		v.positions[i][1] = corners[i][1];
		v.positions[i][2] = 1.0f;	// The far plane, which is depth 1
	}
	FragmentUniforms f;
	memset (&f, 0, sizeof (f));
	SDL_PushGPUVertexUniformData (cmd, 0, &v, sizeof (v));
	SDL_PushGPUFragmentUniformData (cmd, 0, &f, sizeof (f));
	SDL_DrawGPUPrimitives (pass, 4, 1, 0, 0);
}

void GPURenderer::copyToTexture (Texture * dst, int dstX, int dstY, int srcX, int srcY, int width, int height) {
	RenderTarget * t = target ();
	// We copy only what is in both textures.
	if (srcX < 0) { dstX -= srcX; width += srcX; srcX = 0; }
	if (srcY < 0) { dstY -= srcY; height += srcY; srcY = 0; }
	if (dstX < 0) { srcX -= dstX; width += dstX; dstX = 0; }
	if (dstY < 0) { srcY -= dstY; height += dstY; dstY = 0; }
	if (srcX + width > t->color->width) width = t->color->width - srcX;
	if (srcY + height > t->color->height) height = t->color->height - srcY;
	if (dstX + width > dst->width) width = dst->width - dstX;
	if (dstY + height > dst->height) height = dst->height - dstY;
	if (width <= 0 || height <= 0) return;

	endPass ();
	SDL_GPUCopyPass * copy = SDL_BeginGPUCopyPass (commandBuffer ());
	SDL_GPUTextureLocation source;
	SDL_zero (source);
	source.texture = t->color->texture;
	source.x = srcX;
	source.y = srcY;
	SDL_GPUTextureLocation destination;
	SDL_zero (destination);
	destination.texture = dst->texture;
	destination.x = dstX;
	destination.y = dstY;
	SDL_CopyGPUTextureToTexture (copy, &source, &destination, width, height, 1, false);
	SDL_EndGPUCopyPass (copy);
}

void GPURenderer::readPixels (int x, int y, int width, int height, unsigned char * rgba) {
	RenderTarget * t = target ();
	download (t->color->texture, x, y, width, height, 4, rgba);
}

void GPURenderer::setAntialiasScale (float scale) {
	antialiasScale = scale;
}

SDL_GPUGraphicsPipeline * GPURenderer::pipeline (Program program, Blend blend, ColorMask mask, DepthMode depth, bool hasDepth, bool clearing) {
	Uint32 key = program | (blend << 4) | (mask << 5) | (depth << 9) | (hasDepth << 11) | (clearing << 12);
	std::map<Uint32, SDL_GPUGraphicsPipeline *>::iterator found = pipelines.find (key);
	if (found != pipelines.end ()) return found->second;

	SDL_GPUColorTargetDescription colorTarget;
	SDL_zero (colorTarget);
	colorTarget.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	colorTarget.blend_state.enable_blend = blend == BLEND_ALPHA;
	colorTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
	colorTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	colorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
	colorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	colorTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	colorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
	colorTarget.blend_state.enable_color_write_mask = true;
	colorTarget.blend_state.color_write_mask = 0;
	if (mask & 1) colorTarget.blend_state.color_write_mask |= SDL_GPU_COLORCOMPONENT_R;
	if (mask & 2) colorTarget.blend_state.color_write_mask |= SDL_GPU_COLORCOMPONENT_G;
	if (mask & 4) colorTarget.blend_state.color_write_mask |= SDL_GPU_COLORCOMPONENT_B;
	if (mask & 8) colorTarget.blend_state.color_write_mask |= SDL_GPU_COLORCOMPONENT_A;

	SDL_GPUGraphicsPipelineCreateInfo info;
	SDL_zero (info);
	info.vertex_shader = vertexShader;
	info.fragment_shader = fragmentShaders[program];
	info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;

	// Meshes have their vertices in a vertex buffer, as gfx::Vertex2D.
	SDL_GPUVertexBufferDescription vertexBuffer;
	SDL_zero (vertexBuffer);
	SDL_GPUVertexAttribute attributes[3];
	SDL_zero (attributes);
	if (program == PROG_VERTEX_COLOR) {
		info.vertex_shader = meshVertexShader;
		info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		vertexBuffer.slot = 0;
		vertexBuffer.pitch = sizeof (Vertex2D);
		vertexBuffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
		attributes[0].location = 0;
		attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
		attributes[0].offset = offsetof (Vertex2D, x);
		attributes[1].location = 1;
		attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
		attributes[1].offset = offsetof (Vertex2D, u);
		attributes[2].location = 2;
		attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
		attributes[2].offset = offsetof (Vertex2D, color);
		info.vertex_input_state.vertex_buffer_descriptions = &vertexBuffer;
		info.vertex_input_state.num_vertex_buffers = 1;
		info.vertex_input_state.vertex_attributes = attributes;
		info.vertex_input_state.num_vertex_attributes = 3;
	}
	info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
	if (hasDepth && depth != DEPTH_OFF) {
		info.depth_stencil_state.enable_depth_test = true;
		info.depth_stencil_state.enable_depth_write = depth == DEPTH_TEST_WRITE;
		// Clearing writes the far plane everywhere.
		info.depth_stencil_state.compare_op = clearing ? SDL_GPU_COMPAREOP_ALWAYS : SDL_GPU_COMPAREOP_LESS;
	}
	info.target_info.color_target_descriptions = &colorTarget;
	info.target_info.num_color_targets = 1;
	info.target_info.has_depth_stencil_target = hasDepth;
	info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;

	SDL_GPUGraphicsPipeline * p = SDL_CreateGPUGraphicsPipeline (device, &info);
	if (! p) logDebug ("Can't make a pipeline: %s\n", SDL_GetError ());
	pipelines[key] = p;
	return p;
}

// Start drawing with a program, in the viewport and scissor rectangle.
// Returns false when there is nothing to draw in.
bool GPURenderer::startDraw (Program program, const PipelineState & state) {
	RenderTarget * t = target ();
	SDL_Rect rect;
	if (! scissorRect (true, &rect)) return false;
	SDL_GPUGraphicsPipeline * p = pipeline (program, state.blend, state.colorMask, state.depth, t->depth != NULL, false);
	if (! p) return false;

	if (! pass) beginPass (false, false);
	SDL_GPUViewport vp = { 0, 0, (float) t->color->width, (float) t->color->height, 0, 1 };
	SDL_SetGPUViewport (pass, &vp);
	SDL_SetGPUScissor (pass, &rect);
	SDL_BindGPUGraphicsPipeline (pass, p);
	return true;
}

// The matrix of OpenGL, then the viewport within the whole target, then y
// flipped and z from 0 to 1, applied to each column.
void GPURenderer::drawMatrix (const float * matrix, float * out) {
	RenderTarget * t = target ();
	float tw = (float) t->color->width, th = (float) t->color->height;
	float sx = viewport[2] / tw, ox = (2.0f * viewport[0] + viewport[2]) / tw - 1.0f;
	float sy = viewport[3] / th, oy = (2.0f * viewport[1] + viewport[3]) / th - 1.0f;
	for (int c = 0; c < 4; c ++) {
		const float * m = matrix + c * 4;
		float * o = out + c * 4;
		o[0] = m[0] * sx + m[3] * ox;
		o[1] = - (m[1] * sy + m[3] * oy);
		o[2] = 0.5f * m[2] + 0.5f * m[3];
		o[3] = m[3];
	}
}

void GPURenderer::pushFragmentUniforms (Program program, const DrawUniforms & u) {
	if (! programs[program].numUniformBuffers) return;
	FragmentUniforms f;
	memset (&f, 0, sizeof (f));
	memcpy (f.color, u.color, sizeof (f.color));
	memcpy (f.secondaryColor, u.secondaryColor, sizeof (f.secondaryColor));
	f.scale = antialiasScale;
	f.zBufferLayer = u.zBufferLayer;
	f.antialias = u.antialias;
	f.useLightTexture = u.useLightTexture;
	f.zBuffer = u.zBuffer;
	f.modulateColor = u.modulateColor;
	SDL_PushGPUFragmentUniformData (cmd, 0, &f, sizeof (f));
}

void GPURenderer::draw (const PipelineState & state, const DrawUniforms & u, Texture * const textures[3], const Quad & quad) {
	if (! startDraw (state.program, state)) return;

	VertexUniforms v;
	memset (&v, 0, sizeof (v));
	drawMatrix (u.matrix, v.matrix);
	for (int i = 0; i < 4; i ++) {
		v.positions[i][0] = quad.vertices[i * 3];
		v.positions[i][1] = quad.vertices[i * 3 + 1];
		v.positions[i][2] = quad.vertices[i * 3 + 2];
		if (quad.texCoords[0]) {
			v.texCoords01[i][0] = quad.texCoords[0][i * 2];
			v.texCoords01[i][1] = quad.texCoords[0][i * 2 + 1];
		}
		if (quad.texCoords[1]) {
			v.texCoords01[i][2] = quad.texCoords[1][i * 2];
			v.texCoords01[i][3] = quad.texCoords[1][i * 2 + 1];
		}
		if (quad.texCoords[2]) {
			v.texCoords2[i][0] = quad.texCoords[2][i * 2];
			v.texCoords2[i][1] = quad.texCoords[2][i * 2 + 1];
		}
	}
	SDL_PushGPUVertexUniformData (cmd, 0, &v, sizeof (v));
	pushFragmentUniforms (state.program, u);

	Uint32 numSamplers = programs[state.program].numSamplers;
	if (numSamplers) {
		SDL_GPUTextureSamplerBinding bindings[3];
		for (Uint32 i = 0; i < numSamplers; i ++) {
			Texture * tex = textures[i] ? textures[i] : blank;
			bindings[i].texture = tex->texture;
			bindings[i].sampler = sampler (tex);
		}
		SDL_BindGPUFragmentSamplers (pass, 0, bindings, numSamplers);
	}

	SDL_DrawGPUPrimitives (pass, 4, 1, 0, 0);
}

// Make room for size bytes in a mesh buffer, in a larger buffer if need
// be. Returns the offset, and whether SDL may cycle the buffer, which it
// may for the first write of a frame into it.
Uint32 GPURenderer::reserve (MeshBuffer & b, SDL_GPUBufferUsageFlags usage, Uint32 size, bool * cycle) {
	if (! b.buffer || b.used + size > b.capacity) {
		if (b.buffer) retiredBuffers.push_back (b.buffer);
		Uint32 capacity = b.capacity ? b.capacity * 2 : 256 * 1024;
		while (capacity < size) capacity *= 2;
		SDL_GPUBufferCreateInfo info;
		SDL_zero (info);
		info.usage = usage;
		info.size = capacity;
		b.buffer = SDL_CreateGPUBuffer (device, &info);
		b.capacity = capacity;
		b.used = 0;
	}
	*cycle = b.used == 0;
	Uint32 offset = b.used;
	// Offsets of vertex and index buffers are multiples of 4.
	b.used += (size + 3) & ~3u;
	return offset;
}

Mesh * GPURenderer::createFrameMesh (const Vertex2D * vertices, int vertexCount, const unsigned short * indices, int indexCount) {
	Uint32 vertexSize = (Uint32) vertexCount * sizeof (Vertex2D);
	Uint32 indexSize = (Uint32) indexCount * sizeof (unsigned short);
	Mesh * mesh = new Mesh;
	frameMeshes.push_back (mesh);
	bool cycleVertices, cycleIndices;
	mesh->vertexOffset = reserve (meshVertices, SDL_GPU_BUFFERUSAGE_VERTEX, vertexSize, &cycleVertices);
	mesh->indexOffset = reserve (meshIndices, SDL_GPU_BUFFERUSAGE_INDEX, indexSize, &cycleIndices);
	mesh->vertices = meshVertices.buffer;
	mesh->indices = meshIndices.buffer;
	if (! vertexSize || ! indexSize) return mesh;

	SDL_GPUTransferBufferCreateInfo info;
	SDL_zero (info);
	info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	info.size = vertexSize + indexSize;
	SDL_GPUTransferBuffer * transfer = SDL_CreateGPUTransferBuffer (device, &info);
	unsigned char * mapped = (unsigned char *) SDL_MapGPUTransferBuffer (device, transfer, false);
	memcpy (mapped, vertices, vertexSize);
	memcpy (mapped + vertexSize, indices, indexSize);
	SDL_UnmapGPUTransferBuffer (device, transfer);

	endPass ();
	SDL_GPUCopyPass * copy = SDL_BeginGPUCopyPass (commandBuffer ());
	SDL_GPUTransferBufferLocation source;
	SDL_zero (source);
	source.transfer_buffer = transfer;
	SDL_GPUBufferRegion destination;
	SDL_zero (destination);
	destination.buffer = mesh->vertices;
	destination.offset = mesh->vertexOffset;
	destination.size = vertexSize;
	SDL_UploadToGPUBuffer (copy, &source, &destination, cycleVertices);
	source.offset = vertexSize;
	destination.buffer = mesh->indices;
	destination.offset = mesh->indexOffset;
	destination.size = indexSize;
	SDL_UploadToGPUBuffer (copy, &source, &destination, cycleIndices);
	SDL_EndGPUCopyPass (copy);
	SDL_ReleaseGPUTransferBuffer (device, transfer);
	return mesh;
}

void GPURenderer::drawMesh (const PipelineState & state, const DrawUniforms & u, Texture * texture, Mesh * mesh, int firstIndex, int indexCount, int baseVertex) {
	if (indexCount <= 0) return;
	if (! startDraw (PROG_VERTEX_COLOR, state)) return;

	MeshUniforms v;
	drawMatrix (u.matrix, v.matrix);
	SDL_PushGPUVertexUniformData (cmd, 0, &v, sizeof (v));
	pushFragmentUniforms (PROG_VERTEX_COLOR, u);

	Texture * tex = texture ? texture : blank;
	SDL_GPUTextureSamplerBinding binding;
	binding.texture = tex->texture;
	binding.sampler = sampler (tex);
	SDL_BindGPUFragmentSamplers (pass, 0, &binding, 1);

	SDL_GPUBufferBinding vertexBinding;
	vertexBinding.buffer = mesh->vertices;
	vertexBinding.offset = mesh->vertexOffset + (Uint32) baseVertex * sizeof (Vertex2D);
	SDL_BindGPUVertexBuffers (pass, 0, &vertexBinding, 1);
	SDL_GPUBufferBinding indexBinding;
	indexBinding.buffer = mesh->indices;
	indexBinding.offset = mesh->indexOffset;
	SDL_BindGPUIndexBuffer (pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
	SDL_DrawGPUIndexedPrimitives (pass, indexCount, 1, firstIndex, 0, 0);
}

void GPURenderer::present () {
	endPass ();
	updateWindowTarget ();
	SDL_GPUCommandBuffer * c = commandBuffer ();
	SDL_GPUTexture * swapchain = NULL;
	Uint32 w, h;
	if (SDL_WaitAndAcquireGPUSwapchainTexture (c, window, &swapchain, &w, &h) && swapchain) {
		// The rows of the window texture start at the bottom.
		SDL_GPUBlitInfo blit;
		SDL_zero (blit);
		blit.source.texture = windowTexture->texture;
		blit.source.w = windowTexture->width;
		blit.source.h = windowTexture->height;
		blit.destination.texture = swapchain;
		blit.destination.w = w;
		blit.destination.h = h;
		blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
		blit.flip_mode = SDL_FLIP_VERTICAL;
		blit.filter = SDL_GPU_FILTER_NEAREST;
		SDL_BlitGPUTexture (c, &blit);
	}
	SDL_SubmitGPUCommandBuffer (c);
	cmd = NULL;

	for (size_t i = 0; i < frameMeshes.size (); i ++) delete frameMeshes[i];
	frameMeshes.clear ();
	for (size_t i = 0; i < retiredBuffers.size (); i ++) SDL_ReleaseGPUBuffer (device, retiredBuffers[i]);
	retiredBuffers.clear ();
	meshVertices.used = meshIndices.used = 0;
}

} // namespace

Renderer * createGPURenderer (SDL_GPUDevice * device, SDL_Window * window, const char ** error) {
	GPURenderer * r = new GPURenderer (device, window);
	if (! r->init (error)) {
		delete r;
		return NULL;
	}
	return r;
}

} // namespace gfx
