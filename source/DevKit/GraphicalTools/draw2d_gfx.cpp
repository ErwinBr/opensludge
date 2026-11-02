/*
 * draw2d_gfx.cpp - Part of the SLUDGE Dev Kit
 *
 * The 2D drawing of draw2d.h with gfx::Renderer. We collect what a view
 * draws, with the colour in each vertex, into one mesh, and draw it when
 * the view is finished, in batches of one texture each, so that SDL_GPU
 * ends its render pass only once for each view.
 *
 * The renderers draw triangles only, so a line is a quad, one pixel of the
 * view wide.
 */

#include <math.h>
#include <string.h>

#include <map>
#include <vector>

#include "renderer.h"

#include "draw2d_gfx.h"
#include "sprites.h"
#include "zbuffer.h"

namespace {

// With 16-bit indices, a batch has at most this many vertices.
const size_t MAX_BATCH_VERTICES = 65536 - 4;

class GfxDraw2D : public Draw2D {
public:
	explicit GfxDraw2D (gfx::Renderer * r) : renderer (r), pixel (1.0f) {
		colour[0] = colour[1] = colour[2] = colour[3] = 255;
		gfx::DrawUniforms identity;
		memcpy (matrix, identity.matrix, sizeof (matrix));
	}
	~GfxDraw2D ();

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
	void finish ();
	void forgetPictures (const spriteBank * bank);

private:
	void setProjection (float left, float right, float bottom, float top);
	// Start a batch, or go on with the last one if it can take count more
	// vertices of the same texture and matrix. Returns the index of the
	// first new vertex in the batch.
	unsigned short startBatch (gfx::Texture * texture, size_t count);
	void addVertex (float x, float y, float u, float v);
	void addLine (float x1, float y1, float x2, float y2);
	gfx::Texture * texture (const spriteBank * bank, int index, bool zBufferLayer);

	gfx::Renderer * renderer;
	float matrix[16];
	unsigned char colour[4];
	// How much of the picture one pixel of the view covers.
	float pixel;

	struct Batch {
		gfx::Texture * texture;
		float matrix[16];
		size_t firstVertex, firstIndex, vertexCount, indexCount;
	};
	std::vector<Batch> batches;
	std::vector<gfx::Vertex2D> vertices;
	std::vector<unsigned short> indices;

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
		gfx::Texture * texture;
		unsigned int changes;
	};
	std::map<Key, Picture> pictures;
};

GfxDraw2D::~GfxDraw2D () {
	for (std::map<Key, Picture>::iterator i = pictures.begin (); i != pictures.end (); ++ i) {
		renderer->destroyTexture (i->second.texture);
	}
}

void GfxDraw2D::clear () {
	finish ();
	renderer->clear (true, false);
}

// The projection of glOrtho, with the near plane at 1 and the far plane
// at -1, as in draw2d_gl.cpp.
void GfxDraw2D::setProjection (float left, float right, float bottom, float top) {
	float m[16] = {
		2.0f / (right - left), 0, 0, 0,
		0, 2.0f / (top - bottom), 0, 0,
		0, 0, 1.0f, 0,
		-(right + left) / (right - left), -(top + bottom) / (top - bottom), 0, 1.0f
	};
	memcpy (matrix, m, sizeof (matrix));
}

void GfxDraw2D::setViewArea (int x, int y, int w, int h, float zmul) {
	pixel = zmul;
	setProjection (x*zmul, (x+w)*zmul, (-y+h)*zmul, -y*zmul);
}

void GfxDraw2D::setSpriteViewArea (int x, int y, int w, int h, float zmul) {
	pixel = zmul;
	setProjection (x*zmul, (x+w)*zmul, (y-h)*zmul, y*zmul);
}

static unsigned char toByte (float c) {
	if (c <= 0.0f) return 0;
	if (c >= 1.0f) return 255;
	return (unsigned char) (c * 255.0f + 0.5f);
}

void GfxDraw2D::setDrawingColour (float r, float g, float b, float a) {
	colour[0] = toByte (r);
	colour[1] = toByte (g);
	colour[2] = toByte (b);
	colour[3] = toByte (a);
}

unsigned short GfxDraw2D::startBatch (gfx::Texture * texture, size_t count) {
	if (! batches.empty ()) {
		Batch & last = batches.back ();
		if (last.texture == texture && ! memcmp (last.matrix, matrix, sizeof (matrix))
				&& last.vertexCount + count <= MAX_BATCH_VERTICES) {
			unsigned short first = (unsigned short) last.vertexCount;
			last.vertexCount += count;
			return first;
		}
	}
	Batch batch;
	batch.texture = texture;
	memcpy (batch.matrix, matrix, sizeof (matrix));
	batch.firstVertex = vertices.size ();
	batch.firstIndex = indices.size ();
	batch.vertexCount = count;
	batch.indexCount = 0;
	batches.push_back (batch);
	return 0;
}

void GfxDraw2D::addVertex (float x, float y, float u, float v) {
	gfx::Vertex2D vertex = { x, y, u, v, { colour[0], colour[1], colour[2], colour[3] } };
	vertices.push_back (vertex);
}

// A line, as a quad one pixel of the view wide.
void GfxDraw2D::addLine (float x1, float y1, float x2, float y2) {
	float dx = x2 - x1, dy = y2 - y1;
	float length = sqrtf (dx * dx + dy * dy);
	if (length == 0.0f) return;
	float nx = - dy / length * pixel * 0.5f, ny = dx / length * pixel * 0.5f;
	unsigned short first = startBatch (NULL, 4);
	addVertex (x1 + nx, y1 + ny, 0, 0);
	addVertex (x2 + nx, y2 + ny, 0, 0);
	addVertex (x1 - nx, y1 - ny, 0, 0);
	addVertex (x2 - nx, y2 - ny, 0, 0);
	const unsigned short quad[6] = { 0, 1, 2, 2, 1, 3 };
	for (int i = 0; i < 6; i ++) indices.push_back (first + quad[i]);
	batches.back ().indexCount += 6;
}

void GfxDraw2D::drawLines (const float * points, int count) {
	for (int i = 0; i + 1 < count; i += 2) {
		addLine (points[i * 2], points[i * 2 + 1], points[i * 2 + 2], points[i * 2 + 3]);
	}
}

void GfxDraw2D::drawLineLoop (const float * points, int count) {
	for (int i = 0; i < count; i ++) {
		int j = (i + 1) % count;
		addLine (points[i * 2], points[i * 2 + 1], points[j * 2], points[j * 2 + 1]);
	}
}

void GfxDraw2D::drawPolygon (const float * points, int count) {
	if (count < 3) return;
	unsigned short first = startBatch (NULL, count);
	for (int i = 0; i < count; i ++) addVertex (points[i * 2], points[i * 2 + 1], 0, 0);
	// A triangle fan.
	for (int i = 1; i + 1 < count; i ++) {
		indices.push_back (first);
		indices.push_back (first + i);
		indices.push_back (first + i + 1);
	}
	batches.back ().indexCount += (count - 2) * 3;
}

gfx::Texture * GfxDraw2D::texture (const spriteBank * bank, int index, bool zBufferLayer) {
	Key key = { bank, index, zBufferLayer };
	std::map<Key, Picture>::iterator found = pictures.find (key);
	if (found != pictures.end () && found->second.changes == bank->changes) return found->second.texture;

	const sprite & single = bank->sprites[index];
	int width = single.width;
	int height = single.height < 0 ? - single.height : single.height;
	unsigned char * rgba = zBufferLayer ? zBufferLayerPixels (bank, index) : spritePixels (bank, index);
	Picture picture;
	if (found != pictures.end ()) {
		picture.texture = found->second.texture;
		renderer->setTextureImage (picture.texture, width, height, rgba);
	} else {
		picture.texture = renderer->createTexture (width, height, gfx::TEX_RGBA8, rgba);
	}
	delete [] rgba;
	picture.changes = bank->changes;
	// We draw sprites smoothly, and z-buffers with sharp edges.
	gfx::Filter filter = zBufferLayer ? gfx::FILTER_NEAREST : gfx::FILTER_LINEAR;
	renderer->setTextureFilter (picture.texture, filter, filter);
	renderer->setTextureWrap (picture.texture, gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
	pictures[key] = picture;
	return picture.texture;
}

void GfxDraw2D::drawPicture (const spriteBank * bank, int index, bool zBufferLayer,
		float x1, float y1, float x2, float y2,
		float tx1, float ty1, float tx2, float ty2) {
	gfx::Texture * t = texture (bank, index, zBufferLayer);
	unsigned short first = startBatch (t, 4);
	addVertex (x1, y1, tx1, ty1);
	addVertex (x2, y1, tx2, ty1);
	addVertex (x1, y2, tx1, ty2);
	addVertex (x2, y2, tx2, ty2);
	const unsigned short quad[6] = { 0, 1, 2, 2, 1, 3 };
	for (int i = 0; i < 6; i ++) indices.push_back (first + quad[i]);
	batches.back ().indexCount += 6;
}

void GfxDraw2D::finish () {
	if (batches.empty ()) return;
	gfx::Mesh * mesh = renderer->createFrameMesh (vertices.data (), (int) vertices.size (), indices.data (), (int) indices.size ());
	gfx::PipelineState state (gfx::PROG_VERTEX_COLOR, gfx::BLEND_ALPHA);
	for (size_t i = 0; i < batches.size (); i ++) {
		const Batch & batch = batches[i];
		gfx::DrawUniforms uniforms;
		memcpy (uniforms.matrix, batch.matrix, sizeof (uniforms.matrix));
		renderer->drawMesh (state, uniforms, batch.texture, mesh,
			(int) batch.firstIndex, (int) batch.indexCount, (int) batch.firstVertex);
	}
	batches.clear ();
	vertices.clear ();
	indices.clear ();
}

void GfxDraw2D::forgetPictures (const spriteBank * bank) {
	std::map<Key, Picture>::iterator i = pictures.begin ();
	while (i != pictures.end ()) {
		if (i->first.bank == bank) {
			renderer->destroyTexture (i->second.texture);
			pictures.erase (i ++);
		} else {
			++ i;
		}
	}
}

} // namespace

Draw2D * createGfxDraw2D (gfx::Renderer * renderer) {
	return new GfxDraw2D (renderer);
}
