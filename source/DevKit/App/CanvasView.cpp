#include <math.h>
#include <stdio.h>

#include "imgui.h"

#include "renderer.h"

#include "CanvasView.h"
#include "draw2d.h"
#include "imgui_impl_gfx.h"

namespace devkit {

// The zoom goes from 5% to 3200%.
static const float MIN_ZOOM = 0.05f, MAX_ZOOM = 32.0f;
// Each step of the mouse wheel zooms by this much.
static const float WHEEL_STEP = 1.15f;
// Fit leaves a margin around the picture.
static const float FIT_MARGIN = 0.95f;

CanvasView::CanvasView (const CanvasGraphics & g)
	: graphics (g), texture (NULL), target (NULL), targetWidth (0), targetHeight (0),
	  pictureX (0), pictureY (0), pictureWidth (0), pictureHeight (0), spriteView (false), fitPending (true), fitted (false), scale (1.0f), scrollX (0), scrollY (0),
	  viewSize (0, 0), pixelRatio (1.0f),
	  isHovered (false), panning (false), leftPressed (false), leftHeld (false), leftReleased (false), mouse (0, 0) {}

CanvasView::~CanvasView () {
	if (target) graphics.renderer->destroyTarget (target);
	if (texture) graphics.renderer->destroyTexture (texture);
}

void CanvasView::setPictureSize (int width, int height) {
	if (width == pictureWidth && height == pictureHeight && ! pictureX && ! pictureY) return;
	pictureX = pictureY = 0;
	pictureWidth = width;
	pictureHeight = height;
	fitPending = true;
}

void CanvasView::setPictureBounds (int x, int y, int width, int height) {
	pictureX = x;
	pictureY = y;
	pictureWidth = width;
	pictureHeight = height;
}

// Show the middle of the picture in the middle of the view.
void CanvasView::centre () {
	scrollX = (pictureX + pictureWidth * 0.5f) * scale - viewSize.x * 0.5f;
	scrollY = (pictureY + pictureHeight * 0.5f) * scale - viewSize.y * 0.5f;
}

void CanvasView::zoomToFit () {
	if (viewSize.x <= 0 || viewSize.y <= 0 || pictureWidth <= 0 || pictureHeight <= 0) {
		fitPending = true;
		return;
	}
	fitPending = false;
	fitted = true;
	float zx = viewSize.x / pictureWidth, zy = viewSize.y / pictureHeight;
	scale = (zx < zy ? zx : zy) * FIT_MARGIN;
	if (scale < MIN_ZOOM) scale = MIN_ZOOM;
	if (scale > MAX_ZOOM) scale = MAX_ZOOM;
	centre ();
}

void CanvasView::zoomToActualSize () {
	fitPending = false;
	fitted = false;
	scale = 1.0f;
	centre ();
}

// Zoom around the middle of the view.
void CanvasView::zoomBy (float factor) {
	float newScale = scale * factor;
	if (newScale < MIN_ZOOM) newScale = MIN_ZOOM;
	if (newScale > MAX_ZOOM) newScale = MAX_ZOOM;
	float cx = (viewSize.x * 0.5f + scrollX) / scale, cy = (viewSize.y * 0.5f + scrollY) / scale;
	scale = newScale;
	scrollX = cx * scale - viewSize.x * 0.5f;
	scrollY = cy * scale - viewSize.y * 0.5f;
	fitPending = false;
	fitted = false;
}

void CanvasView::drawZoomButtons () {
	if (ImGui::Button ("100%")) zoomToActualSize ();
#if defined(__APPLE__)
	ImGui::SetItemTooltip ("Show the picture at its size (Cmd+1)");
#else
	ImGui::SetItemTooltip ("Show the picture at its size (Ctrl+1)");
#endif
	ImGui::SameLine ();
	if (ImGui::Button ("Fit")) zoomToFit ();
#if defined(__APPLE__)
	ImGui::SetItemTooltip ("Show the whole picture (Cmd+0)");
#else
	ImGui::SetItemTooltip ("Show the whole picture (Ctrl+0)");
#endif
}

void CanvasView::ensureTarget (int width, int height) {
	if (target && width == targetWidth && height == targetHeight) return;
	if (target) graphics.renderer->destroyTarget (target);
	if (texture) graphics.renderer->destroyTexture (texture);
	texture = graphics.renderer->createTexture (width, height, gfx::TEX_RGBA8, NULL);
	// We show it pixel for pixel.
	graphics.renderer->setTextureFilter (texture, gfx::FILTER_NEAREST, gfx::FILTER_NEAREST);
	target = graphics.renderer->createTarget (texture, false);
	targetWidth = width;
	targetHeight = height;
}

void CanvasView::draw (const ImVec2 & size, std::function<void (Draw2D & draw)> paint) {
	ImGuiIO & io = ImGui::GetIO ();
	leftPressed = leftReleased = false;
	ImVec2 newSize (size.x < 1 ? 1 : floorf (size.x), size.y < 1 ? 1 : floorf (size.y));
	if (fitted && (newSize.x != viewSize.x || newSize.y != viewSize.y)) fitPending = true;
	viewSize = newSize;
	pixelRatio = io.DisplayFramebufferScale.x > 0 ? io.DisplayFramebufferScale.x : 1.0f;
	if (fitPending) zoomToFit ();

	ImVec2 origin = ImGui::GetCursorScreenPos ();
	ImGui::InvisibleButton ("canvas", viewSize,
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	isHovered = ImGui::IsItemHovered ();
	// The wheel zooms here, rather than scroll the window.
	ImGui::SetItemKeyOwner (ImGuiKey_MouseWheelY);
	ImGui::SetItemKeyOwner (ImGuiKey_MouseWheelX);
	bool active = ImGui::IsItemActive ();

	// Zoom with the wheel, keeping the point under the cursor in its place.
	if (isHovered && io.MouseWheel != 0) {
		float vx = io.MousePos.x - origin.x, vy = io.MousePos.y - origin.y;
		float px = (vx + scrollX) / scale, py = (vy + scrollY) / scale;
		float newScale = scale * powf (WHEEL_STEP, io.MouseWheel);
		if (newScale < MIN_ZOOM) newScale = MIN_ZOOM;
		if (newScale > MAX_ZOOM) newScale = MAX_ZOOM;
		scale = newScale;
		scrollX = px * scale - vx;
		scrollY = py * scale - vy;
		fitPending = false;
		fitted = false;
	}
	// A sideways wheel scrolls.
	if (isHovered && io.MouseWheelH != 0) {
		scrollX -= io.MouseWheelH * ImGui::GetFontSize () * 2;
		fitted = false;
	}

	// Pan with the middle or right button, or with Space and the left one.
	bool space = ImGui::IsKeyDown (ImGuiKey_Space) && ! io.WantTextInput;
	panning = active && (ImGui::IsMouseDown (ImGuiMouseButton_Middle) || ImGui::IsMouseDown (ImGuiMouseButton_Right)
		|| (space && ImGui::IsMouseDown (ImGuiMouseButton_Left)));
	if (panning) {
		fitted = false;
		scrollX -= io.MouseDelta.x;
		scrollY -= io.MouseDelta.y;
		ImGui::SetMouseCursor (ImGuiMouseCursor_ResizeAll);
	}

	if (ImGui::IsWindowFocused (ImGuiFocusedFlags_RootAndChildWindows)) {
		if (ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiKey_Equal) || ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiKey_KeypadAdd)) zoomBy (2.0f);
		if (ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiKey_Minus) || ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiKey_KeypadSubtract)) zoomBy (0.5f);
		if (ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiKey_0)) zoomToFit ();
		if (ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiKey_1)) zoomToActualSize ();
	}

	// The view in pixels of the window, scrolled by whole pixels, so that
	// the mouse finds what we drew.
	int width = (int) (viewSize.x * pixelRatio + 0.5f), height = (int) (viewSize.y * pixelRatio + 0.5f);
	int sx = (int) floorf (scrollX * pixelRatio + 0.5f), sy = (int) floorf (scrollY * pixelRatio + 0.5f);
	float zmul = 1.0f / (scale * pixelRatio);
	mouse.x = ((io.MousePos.x - origin.x) * pixelRatio + sx) * zmul;
	mouse.y = ((io.MousePos.y - origin.y) * pixelRatio + sy) * zmul;

	// The left button is the editor's.
	if (! panning && ! space) {
		if (isHovered && ImGui::IsMouseClicked (ImGuiMouseButton_Left)) {
			leftPressed = true;
			leftHeld = true;
		}
	}
	if (leftHeld && ! ImGui::IsMouseDown (ImGuiMouseButton_Left)) {
		leftHeld = false;
		leftReleased = true;
	}

	ensureTarget (width, height);
	gfx::Renderer * renderer = graphics.renderer;
	renderer->setTarget (target);
	renderer->setScissor (0, 0, 0, 0);
	renderer->setViewport (0, 0, width, height);
	if (spriteView) graphics.draw->setSpriteViewArea (sx, -sy, width, height, zmul);
	else graphics.draw->setViewArea (sx, -sy, width, height, zmul);
	paint (*graphics.draw);
	graphics.draw->finish ();
	renderer->setTarget (NULL);

	ImDrawList * list = ImGui::GetWindowDrawList ();
	ImVec2 end (origin.x + viewSize.x, origin.y + viewSize.y);
	list->AddRectFilled (origin, end, IM_COL32 (24, 24, 26, 255));
	// Row 0 of a target is its bottom row.
	list->AddImage (ImTextureRef (ImGui_ImplGfx_TextureID (texture)), origin, end, ImVec2 (0, 1), ImVec2 (1, 0));
}

std::string CanvasView::status () const {
	char text[160];
	int percent = (int) (scale * 100.0f + 0.5f);
	if (isHovered) {
		snprintf (text, sizeof (text), "%d, %d    %d \xC3\x97 %d    %d%%", (int) floorf (mouse.x), (int) floorf (mouse.y),
			pictureWidth, pictureHeight, percent);
	} else {
		snprintf (text, sizeof (text), "%d \xC3\x97 %d    %d%%", pictureWidth, pictureHeight, percent);
	}
	return text;
}

} // namespace devkit
