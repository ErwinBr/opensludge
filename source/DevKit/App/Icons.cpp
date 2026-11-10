#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg.h"
#include "nanosvgrast.h"

#include "renderer.h"
#include "imgui_impl_gfx.h"
#include "Icons.h"

namespace devkit {

namespace {

// The padlocks, closed and open, as drawn in Inkscape (their paths only).
const char * const sources[NUM_ICONS] = {
	"<svg viewBox=\"-5 -10 68.574997 82.864502\" width=\"68.574997\" height=\"82.864502\" xmlns=\"http://www.w3.org/2000/svg\">"
	"<path d=\"M 52.1494,18.5745 H 50.7197 V 11.43 c 0,-11.812 -9.6133,-21.43 -21.43,-21.43 -11.812,0 -21.43,9.6133 -21.43,21.43 "
	"v 7.1445 H 6.43 c -6.3008,0 -11.43,5.1289 -11.43,11.43 v 31.43 c 0,6.3008 5.1289,11.43 11.43,11.43 h 45.715 "
	"c 6.3008,0 11.43,-5.1289 11.43,-11.43 v -31.43 c 0,-6.3008 -5.1289,-11.43 -11.43,-11.43 z m -20,32.855 "
	"c 0,1.5703 -1.2852,2.8555 -2.8555,2.8555 -1.5703,0 -2.8555,-1.2852 -2.8555,-2.8555 v -11.43 "
	"c 0,-1.5703 1.2852,-2.8555 2.8555,-2.8555 1.5703,0 2.8555,1.2852 2.8555,2.8555 z m 12.859,-32.855 h -31.43 V 11.43 "
	"c 0,-8.6719 7.043,-15.715 15.715,-15.715 8.672,0 15.715,7.043 15.715,15.715 z\" fill=\"#000000\"/></svg>",

	"<svg viewBox=\"-5 -10 68.574997 82.864502\" width=\"68.574997\" height=\"82.864502\" xmlns=\"http://www.w3.org/2000/svg\">"
	"<path d=\"m 52.1406,18.5703 h -38.57 v -7.1445 c 0,-8.6719 7.043,-15.715 15.715,-15.715 8.1719,0 15.059,6.3711 15.672,14.516 "
	"0.11328,1.5703 1.4727,2.7578 3.0586,2.6445 C 49.5865,12.75802 50.774,11.3869 50.6607,9.801 49.81695,-1.301 40.4307,-10 29.2897,-10 "
	"c -11.812,0 -21.43,9.6133 -21.43,21.43 v 7.1445 H 6.43 c -6.3008,0 -11.43,5.1289 -11.43,11.43 v 31.43 "
	"c 0,6.3008 5.1289,11.43 11.43,11.43 h 45.715 c 6.3008,0 11.43,-5.1289 11.43,-11.43 v -31.43 "
	"c 0,-6.3008 -5.1289,-11.43 -11.43,-11.43 z m -20,32.855 c 0,1.5703 -1.2852,2.8555 -2.8555,2.8555 "
	"-1.5703,0 -2.8555,-1.2852 -2.8555,-2.8555 v -11.43 c 0,-1.5703 1.2852,-2.8555 2.8555,-2.8555 "
	"1.5703,0 2.8555,1.2852 2.8555,2.8555 z\" fill=\"#000000\"/></svg>",
};

struct Made {
	gfx::Texture * texture;
	int pixels;		// Its width and height
};
Made made[NUM_ICONS];

gfx::Renderer * rendererOf () {
	return (gfx::Renderer *) ImGui::GetIO ().BackendRendererUserData;
}

// The icon, square, of so many pixels: white, with its shape in alpha, so
// that drawing it in a colour gives that colour. NULL if it can't be made.
gfx::Texture * texture (Icon which, int pixels) {
	Made & m = made[which];
	if (m.texture && m.pixels == pixels) return m.texture;
	gfx::Renderer * renderer = rendererOf ();
	if (! renderer || pixels < 1 || pixels > 512) return NULL;
	if (m.texture) {
		renderer->releaseTexture (m.texture);
		m.texture = NULL;
	}
	// nsvgParse writes into what it parses.
	std::vector<char> text (sources[which], sources[which] + strlen (sources[which]) + 1);
	NSVGimage * image = nsvgParse (text.data (), "px", 96.0f);
	if (! image) return NULL;
	NSVGrasterizer * raster = nsvgCreateRasterizer ();
	std::vector<unsigned char> rgba ((size_t) pixels * pixels * 4, 0);
	if (raster && image->width > 0 && image->height > 0) {
		float big = image->width > image->height ? image->width : image->height;
		float scale = pixels / big;
		// Centred in the square.
		float tx = (pixels - image->width * scale) * 0.5f, ty = (pixels - image->height * scale) * 0.5f;
		nsvgRasterize (raster, image, tx, ty, scale, rgba.data (), pixels, pixels, pixels * 4);
	}
	nsvgDeleteRasterizer (raster);
	nsvgDelete (image);
	for (size_t i = 0; i < rgba.size (); i += 4) rgba[i] = rgba[i + 1] = rgba[i + 2] = 255;
	m.texture = renderer->createTexture (pixels, pixels, gfx::TEX_RGBA8, rgba.data ());
	if (m.texture) {
		renderer->setTextureFilter (m.texture, gfx::FILTER_LINEAR, gfx::FILTER_LINEAR);
		renderer->setTextureWrap (m.texture, gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
	}
	m.pixels = pixels;
	return m.texture;
}

// Draw it in a box of the window, in a colour.
void drawIn (Icon which, const ImVec2 & from, float side, ImU32 colour) {
	float screen = ImGui::GetIO ().DisplayFramebufferScale.y;
	gfx::Texture * t = texture (which, (int) ceilf (side * (screen > 0 ? screen : 1)));
	if (! t) return;
	ImGui::GetWindowDrawList ()->AddImage (ImTextureRef (ImGui_ImplGfx_TextureID (t)), from,
		ImVec2 (from.x + side, from.y + side), ImVec2 (0, 0), ImVec2 (1, 1), colour);
}

}

void icon (Icon which, const ImVec4 * colour) {
	float side = ImGui::GetFontSize ();
	ImVec2 at = ImGui::GetCursorScreenPos ();
	ImGui::Dummy (ImVec2 (side, side));
	drawIn (which, at, side, colour ? ImGui::GetColorU32 (* colour) : ImGui::GetColorU32 (ImGuiCol_Text));
}

float iconButtonWidth () {
	return ImGui::GetFrameHeight ();
}

bool iconButton (const char * id, Icon which, const char * tooltip, float side) {
	if (side <= 0) side = ImGui::GetFrameHeight ();
	ImVec2 at = ImGui::GetCursorScreenPos ();
	bool clicked = ImGui::InvisibleButton (id, ImVec2 (side, side));
	bool hovered = ImGui::IsItemHovered (), held = ImGui::IsItemActive ();
	if (hovered || held) {
		ImGui::GetWindowDrawList ()->AddRectFilled (at, ImVec2 (at.x + side, at.y + side),
			ImGui::GetColorU32 (held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered), ImGui::GetStyle ().FrameRounding);
	}
	if (tooltip) ImGui::SetItemTooltip ("%s", tooltip);
	// The icon, as high as the text, in the middle of the button.
	float glyph = ImGui::GetFontSize () < side ? ImGui::GetFontSize () : side;
	float pad = (side - glyph) * 0.5f;
	// (Disabled, the style's alpha dims it.)
	drawIn (which, ImVec2 (floorf (at.x + pad), floorf (at.y + pad)), glyph, ImGui::GetColorU32 (ImGuiCol_Text));
	return clicked;
}

void forgetIcons () {
	gfx::Renderer * renderer = rendererOf ();
	for (int i = 0; i < NUM_ICONS; i ++) {
		if (made[i].texture && renderer) renderer->destroyTexture (made[i].texture);
		made[i].texture = NULL;
		made[i].pixels = 0;
	}
}

} // namespace devkit
