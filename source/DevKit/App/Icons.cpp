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

// The icons, as drawn in Inkscape (their shapes only), in the order of Icon.
const char * const sources[NUM_ICONS] = {
	// ICON_LOCKED
	"<svg viewBox=\"-5 -10 68.574997 82.864502\" width=\"68.574997\" height=\"82.864502\" xmlns=\"http://www.w3.org/2000/svg\">"
	"<path d=\"M 52.1494,18.5745 H 50.7197 V 11.43 c 0,-11.812 -9.6133,-21.43 -21.43,-21.43 -11.812,0 -21.43,9.6133 "
	"-21.43,21.43 v 7.1445 H 6.43 c -6.3008,0 -11.43,5.1289 -11.43,11.43 v 31.43 c 0,6.3008 5.1289,11.43 "
	"11.43,11.43 h 45.715 c 6.3008,0 11.43,-5.1289 11.43,-11.43 v -31.43 c 0,-6.3008 -5.1289,-11.43 -11.43,-11.43 "
	"z m -20,32.855 c 0,1.5703 -1.2852,2.8555 -2.8555,2.8555 -1.5703,0 -2.8555,-1.2852 -2.8555,-2.8555 v -11.43 c "
	"0,-1.5703 1.2852,-2.8555 2.8555,-2.8555 1.5703,0 2.8555,1.2852 2.8555,2.8555 z m 12.859,-32.855 h -31.43 V "
	"11.43 c 0,-8.6719 7.043,-15.715 15.715,-15.715 8.672,0 15.715,7.043 15.715,15.715 z\"/></svg>",
	// ICON_UNLOCKED
	"<svg viewBox=\"-5 -10 68.574997 82.864502\" width=\"68.574997\" height=\"82.864502\" xmlns=\"http://www.w3.org/2000/svg\">"
	"<path d=\"m 52.1406,18.5703 h -38.57 v -7.1445 c 0,-8.6719 7.046883,-15.4555145 15.715,-15.715 1.521559,-0.045549 "
	"2.705587,-1.1611042 2.766023,-2.6815559 C 32.108484,-8.4012929 31.030772,-9.9740277 29.2897,-10 "
	"17.479014,-10.176185 7.8597,-0.3867 7.8597,11.43 v 7.1445 H 6.43 c -6.3008,0 -11.43,5.1289 -11.43,11.43 v "
	"31.43 c 0,6.3008 5.1289,11.43 11.43,11.43 h 45.715 c 6.3008,0 11.43,-5.1289 11.43,-11.43 v -31.43 c 0,-6.3008 "
	"-5.1289,-11.43 -11.43,-11.43 z m -20,32.855 c 0,1.5703 -1.2852,2.8555 -2.8555,2.8555 -1.5703,0 "
	"-2.8555,-1.2852 -2.8555,-2.8555 v -11.43 c 0,-1.5703 1.2852,-2.8555 2.8555,-2.8555 1.5703,0 2.8555,1.2852 "
	"2.8555,2.8555 z\"/></svg>",
	// ICON_KEY
	"<svg viewBox=\"0 0 88.877312 44.000019\" width=\"88.877312\" height=\"44.000019\" xmlns=\"http://www.w3.org/2000/svg\">"
	"<path d=\"m 22,0 c 8.5479,0 15.9516,4.8785 19.5938,12 h 38.026203 c 0,0 9.344845,0.143999 9.25669,4.226544 "
	"-0.123316,5.710871 -13.25669,15.773476 -13.25669,15.773476 l -6,-6 -6,6 -6,-6 -6,6 H 41.5938 c -3.6422,7.1215 "
	"-11.0459,12 -19.5938,12 -12.1502,0 -22,-9.8498 -22,-22 C 0,9.8498 9.8498,0 22,0 Z m -7,16 c -3.3137,0 "
	"-6,2.6863 -6,6.00002 0,3.3137 2.6863,6 6,6 3.3137,0 6,-2.6863 6,-6 C 21,18.6863 18.3137,16 15,16 Z\"/></svg>",
	// ICON_AUTOKEY
	"<svg viewBox=\"0 0 89.197228 64.667984\" width=\"89.197228\" height=\"64.667984\" xmlns=\"http://www.w3.org/2000/svg\">"
	"<path d=\"m 22,0 c 8.5479,0 15.9516,4.8785 19.5938,12 h 38.026203 c 0,0 9.344845,0.143999 9.25669,4.226544 "
	"-0.123316,5.710871 -13.25669,15.773476 -13.25669,15.773476 l -6,-6 -6,6 -6,-6 -6,6 H 41.5938 c -3.6422,7.1215 "
	"-11.0459,12 -19.5938,12 -12.1502,0 -22,-9.8498 -22,-22 C 0,9.8498 9.8498,0 22,0 Z m -7,16 c -3.3137,0 "
	"-6,2.6863 -6,6.00002 0,3.3137 2.6863,6 6,6 3.3137,0 6,-2.6863 6,-6 C 21,18.6863 18.3137,16 15,16 Z\"/><circle cx=\"75.817642\" cy=\"51.288403\" r=\"13.379583\"/></svg>",
	// ICON_PLAY
	"<svg viewBox=\"0 0 45.450001 51.82\" width=\"45.450001\" height=\"51.82\" xmlns=\"http://www.w3.org/2000/svg\">"
	"<path d=\"M 44.35,24 3.3,0.3 C 2.96,0.1 2.58,0 2.2,0 1.82,0 1.44,0.1 1.1,0.3 0.41,0.7 0,1.41 0,2.21 v 47.4 c 0,0.8 "
	"0.41,1.51 1.1,1.91 0.69,0.4 1.51,0.4 2.2,0 l 41.05,-23.7 c 0.69,-0.4 1.1,-1.11 1.1,-1.91 0,-0.8 -0.41,-1.51 "
	"-1.1,-1.91 z\"/></svg>",
	// ICON_PAUSE
	"<svg viewBox=\"0 0 37.549999 51.490002\" width=\"37.549999\" height=\"51.490002\" xmlns=\"http://www.w3.org/2000/svg\">"
	"<path d=\"m 32.13,0 h -2.57 c -2.99,0 -5.42,2.43 -5.42,5.42 v 40.65 c 0,2.99 2.43,5.42 5.42,5.42 h 2.57 c 2.99,0 "
	"5.42,-2.43 5.42,-5.42 V 5.43 C 37.55,2.44 35.12,0 32.13,0 Z M 7.99,0 H 5.42 C 2.43,0 0,2.43 0,5.42 v 40.65 c "
	"0,2.99 2.43,5.42 5.42,5.42 h 2.57 c 2.99,0 5.42,-2.43 5.42,-5.42 V 5.43 C 13.41,2.44 10.98,0 7.99,0 Z\"/></svg>",
	// ICON_FIRST
	"<svg viewBox=\"0 0 48.490002 49.889999\" width=\"48.490002\" height=\"49.889999\" xmlns=\"http://www.w3.org/2000/svg\">"
	"<path d=\"M 4.96,0 C 7.7,0 9.92,2.23 9.92,4.97 V 21.94 L 45.19,1.57 c 0.34,-0.2 0.72,-0.3 1.1,-0.3 0.38,0 0.76,0.1 "
	"1.1,0.3 0.69,0.4 1.1,1.11 1.1,1.91 v 42.93 c 0,0.8 -0.41,1.51 -1.1,1.91 -0.69,0.4 -1.51,0.4 -2.2,0 L "
	"9.93,27.95 v 16.97 c 0,2.74 -2.23,4.97 -4.96,4.97 C 2.23,49.89 0,47.66 0,44.92 V 4.96 C 0,2.22 2.22,0 4.96,0 Z\"/></svg>",
	// ICON_LAST
	"<svg viewBox=\"0 0 48.5 49.889999\" width=\"48.5\" height=\"49.889999\" xmlns=\"http://www.w3.org/2000/svg\">"
	"<path d=\"M 43.54,0 C 40.8,0 38.58,2.23 38.58,4.97 V 21.94 L 3.3,1.57 C 2.96,1.37 2.58,1.27 2.2,1.27 1.82,1.27 "
	"1.44,1.37 1.1,1.57 0.41,1.97 0,2.68 0,3.48 v 42.93 c 0,0.8 0.41,1.51 1.1,1.91 0.69,0.4 1.51,0.4 2.2,0 L "
	"38.57,27.95 v 16.97 c 0,2.74 2.23,4.97 4.96,4.97 2.74,0 4.97,-2.23 4.97,-4.97 V 4.96 C 48.5,2.22 46.28,0 "
	"43.54,0 Z\"/></svg>",
	// ICON_LOOP
	"<svg viewBox=\"0 0 16.002001 14\" width=\"16.002001\" height=\"14\" xmlns=\"http://www.w3.org/2000/svg\">"
	"<path d=\"m 9.001,0 v 2 h -7.5 C 1.129,2 0.728,2.148 0.438,2.438 0.148,2.728 0,3.128 0,3.501 v 7 c 0,0.372 0.148,0.773 "
	"0.438,1.063 0.29,0.29 0.69,0.438 1.063,0.438 h 1.5 v -2 h -1 v -6 h 7 v 2 l 3,-3 -3,-3 z m 4,2 v 2 h 1 v 6 h "
	"-7 V 8 l -3,3 3,3 v -2 h 7.5 c 0.372,0 0.773,-0.148 1.063,-0.438 0.29,-0.29 0.438,-0.69 0.438,-1.063 v -7 c "
	"0,-0.372 -0.148,-0.773 -0.438,-1.063 -0.29,-0.29 -0.69,-0.438 -1.063,-0.438 h -1.5 z\"/></svg>",
	// ICON_PLUS
	"<svg viewBox=\"0 0 94 94\" width=\"94\" height=\"94\" xmlns=\"http://www.w3.org/2000/svg\">"
	"<path d=\"m 91.050001,39.900001 q -2.905859,-2.901953 -7.05,-2.9 h -27 v -27 q 0.002,-4.1441403 -2.95,-7.0999997 Q "
	"51.144142,-0.00195179 47.000001,1.3136098e-6 42.85586,-0.00195179 39.900001,2.9000013 q -2.901953,2.9558594 "
	"-2.9,7.0999997 v 27 h -27 q -4.1441403,-0.002 -7.0999997,2.9 -2.90195309,2.955859 -2.8999999863902,7.1 "
	"-0.00195310361,4.144141 2.8999999863902,7.05 2.9558594,2.951953 7.0999997,2.95 h 27 v 27 q -0.002,4.144141 "
	"2.9,7.05 2.955859,2.951953 7.1,2.95 4.144141,0.002 7.05,-2.95 2.951953,-2.905859 2.95,-7.05 v -27 h 27 q "
	"4.144141,0.002 7.05,-2.95 2.951953,-2.905859 2.95,-7.05 0.002,-4.144141 -2.95,-7.1 z\"/></svg>",
	// ICON_MINUS
	"<svg viewBox=\"0 0 94 20.000002\" width=\"94\" height=\"20.000002\" xmlns=\"http://www.w3.org/2000/svg\">"
	"<path d=\"m 91.050001,2.9000013 q -2.905859,-2.90195299 -7.05,-2.8999999865246 h -74 Q 5.8558607,-0.00195169 "
	"2.9000013,2.9000013 -0.00195179,5.8558603 1.3136098e-6,10.000001 -0.00195179,14.144142 2.9000013,17.050001 q "
	"2.9558594,2.951953 7.0999997,2.95 h 74 q 4.144141,0.002 7.05,-2.95 2.951953,-2.905859 2.95,-7.05 "
	"0.002,-4.1441407 -2.95,-7.0999997 z\"/></svg>",
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

bool iconButton (const char * id, Icon which, const char * tooltip, float side, const IconStyle & style) {
	if (side <= 0) side = ImGui::GetFrameHeight ();
	ImVec2 at = ImGui::GetCursorScreenPos ();
	// Disabled, it is still hovered, for its tooltip, but can't be clicked.
	bool clicked = ImGui::InvisibleButton (id, ImVec2 (side, side), ImGuiButtonFlags_None) && style.enabled;
	bool hovered = ImGui::IsItemHovered (ImGuiHoveredFlags_AllowWhenDisabled), held = ImGui::IsItemActive () && style.enabled;
	ImGuiCol frame = held ? ImGuiCol_ButtonActive : style.on ? ImGuiCol_ButtonActive : hovered && style.enabled ? ImGuiCol_ButtonHovered : ImGuiCol_COUNT;
	if (frame != ImGuiCol_COUNT) {
		ImGui::GetWindowDrawList ()->AddRectFilled (at, ImVec2 (at.x + side, at.y + side), ImGui::GetColorU32 (frame), ImGui::GetStyle ().FrameRounding);
	}
	if (tooltip && hovered) ImGui::SetTooltip ("%s", tooltip);
	// The icon, as high as the text, in the middle of the button.
	float glyph = ImGui::GetFontSize () < side ? ImGui::GetFontSize () : side;
	float pad = (side - glyph) * 0.5f;
	// (Inside BeginDisabled, the style's alpha dims it.)
	ImU32 colour = style.colour ? ImGui::GetColorU32 (* style.colour) : ImGui::GetColorU32 (ImGuiCol_Text);
	drawIn (which, ImVec2 (floorf (at.x + pad), floorf (at.y + pad)), glyph, colour);
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
