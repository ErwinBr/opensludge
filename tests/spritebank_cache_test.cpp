/*
 * Tests of the lifetime of sprite banks: the cache of banks
 * (source/Engine/sprbanks.cpp), which keeps unused banks within a budget,
 * and the release of textures by the renderers (Renderer::releaseTexture in
 * source/Engine/renderer.cpp), which destroys them only once the GPU can't
 * be using them. Banks here are fakes that only have a size.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>
#include <algorithm>
#include <string>
#include <map>

#include "spritebankdecode.h"

#include "sprbanks.h"

static int failures = 0;

#define CHECK(cond, what) do { if (! (cond)) { fprintf (stderr, "FAIL: %s (line %d)\n", what, __LINE__); failures ++; } } while (0)

// Fakes of what sprbanks.cpp uses from the engine.

static int loads = 0;
static const size_t bankBytes = 1000;

bool loadSpriteBank (int fileNum, spriteBank & loadhere, bool isFont) {
	loads ++;
	spriteBank bank;
	bank.isFont = isFont;
	bank.total = fileNum;
	bank.pages.resize (1);
	bank.pages[0].w = bank.pages[0].h = 1;
	bank.pages[0].gpuBytes = bankBytes;
	loadhere = std::move (bank);
	return true;
}

size_t spriteBank::gpuBytes () const {
	size_t n = 0;
	for (size_t i = 0; i < pages.size (); i ++) n += pages[i].gpuBytes;
	return n;
}

void debugOut (const char *, ...) {}

static void testCache () {
	setSpriteBankCacheBudget (2500);

	SpriteBankRef a = loadBankForAnim (1);
	SpriteBankRef b = loadBankForAnim (2);
	SpriteBankRef c = loadBankForAnim (3);
	CHECK (a && b && c && loads == 3, "three banks load");
	CHECK (a->ID == 1 && a->bank.total == 1, "a bank knows its file");
	CHECK (loadBankForAnim (1) == a && loads == 3, "a bank in use loads once");

	trimSpriteBanks ();
	CHECK (spriteBankStats ().loaded == 3 && spriteBankStats ().inUse == 3, "banks in use stay");

	// Each becomes unused in a later frame: the first unused goes first.
	a.reset ();
	trimSpriteBanks ();
	b.reset ();
	trimSpriteBanks ();
	CHECK (spriteBankStats ().unused == 2 && spriteBankStats ().evictions == 0, "unused banks within the budget stay");
	c.reset ();
	trimSpriteBanks ();
	SpriteBankStats s = spriteBankStats ();
	CHECK (s.loaded == 2 && s.unused == 2 && s.unusedGpuBytes == 2000 && s.evictions == 1, "over the budget, one bank goes");

	b = loadBankForAnim (2);
	CHECK (loads == 3, "an unused bank comes back without loading");
	a = loadBankForAnim (1);
	CHECK (loads == 4, "an unloaded bank loads again");

	// A copy of a reference (as copyAnim makes) keeps the bank.
	setSpriteBankCacheBudget (0);
	SpriteBankRef copy = a;
	a.reset ();
	trimSpriteBanks ();
	CHECK (copy && spriteBankStats ().inUse == 2 && spriteBankStats ().unused == 0, "with no budget, only banks in use stay");
	copy.reset ();
	b.reset ();
	trimSpriteBanks ();
	CHECK (spriteBankStats ().loaded == 0 && spriteBankStats ().gpuBytes == 0, "with no budget, unused banks go");

	setSpriteBankCacheBudget (1 << 20);
	a = loadBankForAnim (7);
	a.reset ();
	forgetUnusedSpriteBanks ();
	CHECK (spriteBankStats ().loaded == 0, "forgetUnusedSpriteBanks unloads them all");
	CHECK (spriteBankStats ().hits == 2 && spriteBankStats ().misses == 5, "hits and misses");
}

// A renderer that only counts its textures.
class FakeRenderer : public gfx::Renderer {
public:
	FakeRenderer () : next (1) { framesInFlight = 3; }
	~FakeRenderer () { destroyReleasedTextures (true); }

	std::vector<gfx::Texture *> alive, destroyed;
	uintptr_t next;
	// What happened to targets, in order: 'd' detached, 'x' destroyed, 't' a texture destroyed.
	std::string events;

	bool isAlive (gfx::Texture * t) const { return std::find (alive.begin (), alive.end (), t) != alive.end (); }
	void flush () { destroyReleasedTextures (true); }

	gfx::Texture * createTexture (int, int, gfx::TexFormat, const void *) {
		gfx::Texture * t = (gfx::Texture *) (next ++);
		alive.push_back (t);
		Modes m = { -1, -1, -1, -1 };	// Unset
		modes[t] = m;
		return t;
	}
	void destroyTexture (gfx::Texture * t) {
		alive.erase (std::find (alive.begin (), alive.end (), t));
		destroyed.push_back (t);
		events += 't';
	}
	void present () { destroyReleasedTextures (false); }

	void setTextureImage (gfx::Texture *, int, int, const void *) {}
	void updateTexture (gfx::Texture *, int, int, int, int, const void *) {}
	void textureSize (const gfx::Texture *, int *, int *) {}
	struct Modes { int minFilter, magFilter, wrapS, wrapT; };
	std::map<gfx::Texture *, Modes> modes;
	void setTextureFilter (gfx::Texture * t, gfx::Filter a, gfx::Filter b) { modes[t].minFilter = a; modes[t].magFilter = b; }
	void setTextureWrap (gfx::Texture * t, gfx::Wrap s, gfx::Wrap w) { modes[t].wrapS = s; modes[t].wrapT = w; }
	void readTexture (gfx::Texture *, unsigned char *) {}
	gfx::RenderTarget * createTarget (gfx::Texture *, bool) { return (gfx::RenderTarget *) (next ++); }
	void destroyTarget (gfx::RenderTarget * t) { detachTarget (t); events += 'x'; }
	void detachTarget (gfx::RenderTarget *) { events += 'd'; }
	void setTarget (gfx::RenderTarget *) {}
	void setViewport (int, int, int, int) {}
	void setScissor (int, int, int, int) {}
	void clear (bool, bool) {}
	void copyToTexture (gfx::Texture *, int, int, int, int, int, int) {}
	void readPixels (int, int, int, int, unsigned char *) {}
	void setAntialiasScale (float) {}
	void draw (const gfx::PipelineState &, const gfx::DrawUniforms &, gfx::Texture * const [3], const gfx::Quad &) {}
	gfx::Mesh * createFrameMesh (const gfx::Vertex2D *, int, const unsigned short *, int) { return NULL; }
	void drawMesh (const gfx::PipelineState &, const gfx::DrawUniforms &, gfx::Texture *, gfx::Mesh *, int, int, int) {}
};

static void testRelease () {
	FakeRenderer r;
	gfx::Texture * t = r.createTexture (1, 1, gfx::TEX_RGBA8, NULL);
	{
		gfx::UniqueTexture owner (&r, t);
		gfx::UniqueTexture moved (std::move (owner));
		CHECK (owner.get () == NULL && moved.get () == t, "moving a texture");
	}
	CHECK (r.isAlive (t) && r.texturesBeingReleased () == 1, "a released texture isn't destroyed at once");
	r.present ();
	r.present ();
	CHECK (r.isAlive (t), "nor while frames may still use it");
	r.present ();
	CHECK (! r.isAlive (t) && r.destroyed.size () == 1 && r.texturesBeingReleased () == 0, "but after 3 frames");

	// Released textures go when the renderer does.
	gfx::Texture * u = r.createTexture (1, 1, gfx::TEX_RGBA8, NULL);
	r.releaseTexture (u);
	r.flush ();
	CHECK (! r.isAlive (u), "flushed at the end");

	// A bank's textures go through the same release.
	gfx::Texture * v = r.createTexture (1, 1, gfx::TEX_RGBA8, NULL);
	{
		spriteBank bank;
		bank.pages.resize (1);
		bank.pages[0].tex = gfx::UniqueTexture (&r, v);
		spriteBank other = std::move (bank);
	}
	CHECK (r.isAlive (v) && r.texturesBeingReleased () == 1, "a bank releases its textures once");
	r.flush ();
	CHECK (! r.isAlive (v), "and they go later");

	// A released target stops being drawn into at once, and goes with the
	// textures, before them.
	gfx::Texture * colour = r.createTexture (1, 1, gfx::TEX_RGBA8, NULL);
	gfx::RenderTarget * target = r.createTarget (colour, false);
	r.events.clear ();
	r.releaseTexture (colour);
	r.releaseTarget (target);
	CHECK (r.events == "d" && r.isAlive (colour), "a released target is detached at once");
	r.present ();
	r.present ();
	CHECK (r.events == "d", "and kept while frames may use it");
	r.present ();
	CHECK (r.events == "ddxt" && ! r.isAlive (colour), "then destroyed, before its texture");
}

// uploadPages makes each page, and for fonts each burn page, clamped at the
// edges, with one filter both ways. The renderers have no mipmaps: OpenGL
// makes textures without them and SDL_GPU with one level (num_levels = 1).
static void testUpload () {
	for (int font = 0; font < 2; font ++) {
		for (int linear = 0; linear < 2; linear ++) {
			spritebank::DecodedBank decoded;
			decoded.version = 3;
			decoded.isFont = font != 0;
			decoded.paletteSize = 0;
			for (int i = 0; i < 40; i ++) {
				spritebank::DecodedSprite sp;
				sp.width = 30 + i * 7;
				sp.height = 20 + i * 11;
				sp.xhot = sp.yhot = 0;
				sp.rgba.assign ((size_t) sp.width * sp.height * 4, 255);
				if (font) sp.burn = sp.rgba;
				decoded.sprites.push_back (sp);
			}
			spritebank::Layout layout;
			spritebank::Pages pages;
			std::string error;
			CHECK (spritebank::layout (decoded, false, layout, error) && spritebank::makePages (decoded, layout, false, pages, error), "pages to upload");
			FakeRenderer r;
			std::vector<AtlasPage> out;
			gfx::Filter filter = linear ? gfx::FILTER_LINEAR : gfx::FILTER_NEAREST;
			uploadPages (r, layout, pages, font != 0, filter, out);
			CHECK (out.size () == layout.pageWidth.size () && r.alive.size () == out.size () * (font ? 2 : 1), "a texture for each page, and each burn page");
			bool clamped = true, filtered = true, sized = true;
			for (size_t n = 0; n < out.size (); n ++) {
				gfx::Texture * ts[2] = { out[n].tex.get (), out[n].burn.get () };
				for (int k = 0; k < (font ? 2 : 1); k ++) {
					FakeRenderer::Modes m = r.modes[ts[k]];
					if (m.wrapS != gfx::WRAP_CLAMP || m.wrapT != gfx::WRAP_CLAMP) clamped = false;
					if (m.minFilter != filter || m.magFilter != filter) filtered = false;
				}
				if (out[n].w != layout.pageWidth[n] || out[n].h != layout.pageHeight[n]) sized = false;
				if (out[n].gpuBytes != (size_t) out[n].w * out[n].h * 4 * (font ? 2 : 1)) sized = false;
			}
			CHECK (clamped, "pages clamp at the edges");
			CHECK (filtered, "pages filter one way, nearest or linear, both ways");
			CHECK (sized, "pages have the layout's sizes");
		}
	}
}

int main () {
	testCache ();
	testRelease ();
	testUpload ();
	if (failures) {
		fprintf (stderr, "%d failures\n", failures);
		return 1;
	}
	printf ("All sprite bank cache tests passed\n");
	return 0;
}
