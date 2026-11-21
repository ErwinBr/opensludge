#include <float.h>
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
// The numbers of the rulers are this much of the font, at least this many
// points apart, every 100, 200, 500, 1000... pixels of the picture.
static const float RULER_FONT = 0.75f;
static const float RULER_SPACING = 64.0f;
static const int RULER_STEP = 100;
// How close the mouse must be to a guide to take it, in points.
static const float GUIDE_GRAB_POINTS = 3.0f;
static const ImU32 GUIDE_COLOUR = IM_COL32 (255, 255, 255, 255);

CanvasView::CanvasView (const CanvasGraphics & g)
	: graphics (g), texture (NULL), target (NULL), targetWidth (0), targetHeight (0),
	  pictureX (0), pictureY (0), pictureWidth (0), pictureHeight (0), spriteView (false), rulers (false),
	  guideDrag (GUIDE_NONE), guidePosition (0), fitPending (true), fitted (false), scale (1.0f), scrollX (0), scrollY (0),
	  viewSize (0, 0), pixelRatio (1.0f),
	  isHovered (false), panning (false), leftPressed (false), leftHeld (false), leftReleased (false), mouse (0, 0),
	  viewOrigin (0, 0), drawnX (0), drawnY (0), drawnZmul (1.0f) {}

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
	bool space = ImGui::IsKeyDown (ImGuiKey_Space) && ! io.WantTextInput;

	// The rulers take the top and the left of the box, the picture the rest.
	ImVec2 box = ImGui::GetCursorScreenPos ();
	ImVec2 boxEnd (box.x + (size.x < 1 ? 1 : floorf (size.x)), box.y + (size.y < 1 ? 1 : floorf (size.y)));
	float rulerTop = 0, rulerLeft = 0;
	if (rulers) {
		float fontSize = floorf (ImGui::GetFontSize () * RULER_FONT + 0.5f);
		rulerTop = ceilf (fontSize + 6);
		// The numbers of the left ruler are turned, so it is as wide.
		rulerLeft = rulerTop;
	}
	ImVec2 newSize (size.x - rulerLeft < 1 ? 1 : floorf (size.x - rulerLeft), size.y - rulerTop < 1 ? 1 : floorf (size.y - rulerTop));
	if (fitted && (newSize.x != viewSize.x || newSize.y != viewSize.y)) fitPending = true;
	viewSize = newSize;
	pixelRatio = io.DisplayFramebufferScale.x > 0 ? io.DisplayFramebufferScale.x : 1.0f;
	if (fitPending) zoomToFit ();

	// A press on a ruler starts a guide, which the user drags onto the
	// picture: a horizontal one from the top ruler, a vertical one from the
	// left one. The canvas comes last, so that it is the item after draw.
	if (rulers) {
		ImGui::SetCursorScreenPos (ImVec2 (box.x + rulerLeft, box.y));
		ImGui::InvisibleButton ("top ruler", ImVec2 (viewSize.x, rulerTop));
		if (ImGui::IsItemActivated () && ! space && guideDrag == GUIDE_NONE) guideDrag = GUIDE_HORIZONTAL;
		if (guideDrag == GUIDE_NONE) ImGui::SetItemTooltip ("Drag to add a guide");
		ImGui::SetCursorScreenPos (ImVec2 (box.x, box.y + rulerTop));
		ImGui::InvisibleButton ("left ruler", ImVec2 (rulerLeft, viewSize.y));
		if (ImGui::IsItemActivated () && ! space && guideDrag == GUIDE_NONE) guideDrag = GUIDE_VERTICAL;
		if (guideDrag == GUIDE_NONE) ImGui::SetItemTooltip ("Drag to add a guide");
		ImGui::SetCursorScreenPos (ImVec2 (box.x + rulerLeft, box.y + rulerTop));
	}

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
	viewOrigin = origin;
	drawnX = sx;
	drawnY = sy;
	drawnZmul = zmul;
	// Without a mouse, as before it first moves, ImGui has it at -FLT_MAX,
	// and we keep where it was.
	if (ImGui::IsMousePosValid ()) {
		mouse.x = ((io.MousePos.x - origin.x) * pixelRatio + sx) * zmul;
		mouse.y = ((io.MousePos.y - origin.y) * pixelRatio + sy) * zmul;
	}

	// A guide follows the mouse, and stays where the button lets go of it
	// over the picture. Otherwise, the pointer shows that a guide can be
	// taken.
	if (guideDrag != GUIDE_NONE) {
		bool overView = ImGui::IsMousePosValid () && io.MousePos.x >= origin.x && io.MousePos.y >= origin.y
			&& io.MousePos.x < origin.x + viewSize.x && io.MousePos.y < origin.y + viewSize.y;
		dragGuide (overView);
	} else if (rulers && isHovered && ! panning && ! space && ! leftHeld) {
		GuideDrag kind;
		int index;
		if (guideAtMouse (kind, index)) ImGui::SetMouseCursor (kind == GUIDE_HORIZONTAL ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_ResizeEW);
	}

	// The left button is the editor's.
	if (! panning && ! space && guideDrag == GUIDE_NONE) {
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
	if (rulers) {
		drawGuides (list);
		drawRulers (list, box, boxEnd);
	}
}

bool CanvasView::guideAtMouse (GuideDrag & kind, int & index) const {
	float nearest = pixelsOfPoints (GUIDE_GRAB_POINTS);
	bool found = false;
	for (size_t i = 0; i < horizontalGuides.size (); i ++) {
		float d = fabsf (mouse.y - horizontalGuides[i]);
		if (d <= nearest) {
			nearest = d;
			kind = GUIDE_HORIZONTAL;
			index = (int) i;
			found = true;
		}
	}
	for (size_t i = 0; i < verticalGuides.size (); i ++) {
		float d = fabsf (mouse.x - verticalGuides[i]);
		if (d <= nearest) {
			nearest = d;
			kind = GUIDE_VERTICAL;
			index = (int) i;
			found = true;
		}
	}
	return found;
}

bool CanvasView::grabGuide () {
	if (! rulers || ! isHovered || guideDrag != GUIDE_NONE) return false;
	GuideDrag kind;
	int index;
	if (! guideAtMouse (kind, index)) return false;
	std::vector<float> & guides = kind == GUIDE_HORIZONTAL ? horizontalGuides : verticalGuides;
	guidePosition = guides[index];
	guides.erase (guides.begin () + index);
	guideDrag = kind;
	leftHeld = false;
	return true;
}

void CanvasView::dragGuide (bool overView) {
	bool horizontal = guideDrag == GUIDE_HORIZONTAL;
	if (ImGui::IsMousePosValid ()) guidePosition = floorf ((horizontal ? mouse.y : mouse.x) + 0.5f);
	ImGui::SetMouseCursor (horizontal ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_ResizeEW);
	if (ImGui::IsMouseDown (ImGuiMouseButton_Left)) return;
	// Let go over a ruler, or out of the canvas, the guide is gone.
	if (overView) (horizontal ? horizontalGuides : verticalGuides).push_back (guidePosition);
	guideDrag = GUIDE_NONE;
}

// A line along the top or the left edge of the pixel of each guide.
void CanvasView::drawGuides (ImDrawList * list) {
	ImVec2 end = viewMax ();
	list->PushClipRect (viewOrigin, end, true);
	for (size_t i = 0; i <= horizontalGuides.size (); i ++) {
		float y;
		if (i < horizontalGuides.size ()) y = horizontalGuides[i];
		else if (guideDrag == GUIDE_HORIZONTAL) y = guidePosition;
		else break;
		float at = floorf (toWindow (0, y).y) + 0.5f;
		list->AddLine (ImVec2 (viewOrigin.x, at), ImVec2 (end.x, at), GUIDE_COLOUR);
	}
	for (size_t i = 0; i <= verticalGuides.size (); i ++) {
		float x;
		if (i < verticalGuides.size ()) x = verticalGuides[i];
		else if (guideDrag == GUIDE_VERTICAL) x = guidePosition;
		else break;
		float at = floorf (toWindow (x, 0).x) + 0.5f;
		list->AddLine (ImVec2 (at, viewOrigin.y), ImVec2 (at, end.y), GUIDE_COLOUR);
	}
	list->PopClipRect ();
}

// The pixels of the picture from its origin, numbered every step, which
// grows when we zoom out so that the numbers keep apart, with a tick
// halfway between them.
// Text turned a quarter anticlockwise, which reads upwards, with the top
// left of the text at at. ImGui leaves out glyphs outside the clip rect as
// it adds them, so we add them at from, inside it, and turn them from there.
static void addTurnedText (ImDrawList * list, ImFont * font, float size, const ImVec2 & from, const ImVec2 & at, ImU32 colour, const char * text) {
	int first = list->VtxBuffer.Size;
	list->AddText (font, size, from, colour, text);
	for (int i = first; i < list->VtxBuffer.Size; i ++) {
		ImVec2 & p = list->VtxBuffer[i].pos;
		float dx = p.x - from.x, dy = p.y - from.y;
		p = ImVec2 (at.x + dy, at.y - dx);
	}
}

void CanvasView::drawRulers (ImDrawList * list, const ImVec2 & box, const ImVec2 & boxEnd) {
	ImFont * font = ImGui::GetFont ();
	float fontSize = floorf (ImGui::GetFontSize () * RULER_FONT + 0.5f);
	ImVec2 end = viewMax ();
	ImU32 background = ImGui::GetColorU32 (ImGuiCol_MenuBarBg);
	ImU32 border = ImGui::GetColorU32 (ImGuiCol_Border);
	ImU32 ink = ImGui::GetColorU32 (ImGuiCol_TextDisabled);
	list->AddRectFilled (box, ImVec2 (boxEnd.x, viewOrigin.y), background);
	list->AddRectFilled (ImVec2 (box.x, viewOrigin.y), ImVec2 (viewOrigin.x, boxEnd.y), background);
	list->AddLine (ImVec2 (viewOrigin.x, viewOrigin.y - 0.5f), ImVec2 (boxEnd.x, viewOrigin.y - 0.5f), border);
	list->AddLine (ImVec2 (viewOrigin.x - 0.5f, viewOrigin.y), ImVec2 (viewOrigin.x - 0.5f, boxEnd.y), border);

	// 100, 200, 500, 1000, 2000...
	int step = RULER_STEP;
	for (int i = 0; step * scale < RULER_SPACING && step < 100000000; i ++) step = i % 3 == 1 ? step * 5 / 2 : step * 2;
	int tick = step / 2;
	char number[16];

	// The top ruler: x across the picture.
	float height = viewOrigin.y - box.y;
	list->PushClipRect (ImVec2 (viewOrigin.x, box.y), ImVec2 (end.x, viewOrigin.y), true);
	float left = toWindow (0, 0).x;
	int first = (int) floorf ((viewOrigin.x - left) * drawnZmul * pixelRatio / tick) - 1;
	int last = (int) ceilf ((end.x - left) * drawnZmul * pixelRatio / tick) + 1;
	for (int i = first; i <= last; i ++) {
		float at = floorf (toWindow ((float) i * tick, 0).x) + 0.5f;
		bool major = i % 2 == 0;
		list->AddLine (ImVec2 (at, major ? box.y : viewOrigin.y - height * 0.3f), ImVec2 (at, viewOrigin.y), ink);
		if (! major) continue;
		snprintf (number, sizeof (number), "%d", i * tick);
		list->AddText (font, fontSize, ImVec2 (at + 3, box.y + 2), ink, number);
	}
	list->PopClipRect ();

	// The left ruler: y down the picture, with the numbers turned, under
	// the ticks. The clip rect takes in the picture too, where we add the
	// numbers before we turn them into the ruler.
	float width = viewOrigin.x - box.x;
	list->PushClipRect (ImVec2 (box.x, viewOrigin.y), end, true);
	float top = toWindow (0, 0).y;
	first = (int) floorf ((viewOrigin.y - top) * drawnZmul * pixelRatio / tick) - 1;
	last = (int) ceilf ((end.y - top) * drawnZmul * pixelRatio / tick) + 1;
	for (int i = first; i <= last; i ++) {
		float at = floorf (toWindow (0, (float) i * tick).y) + 0.5f;
		bool major = i % 2 == 0;
		list->AddLine (ImVec2 (major ? box.x : viewOrigin.x - width * 0.3f, at), ImVec2 (viewOrigin.x, at), ink);
		if (! major) continue;
		snprintf (number, sizeof (number), "%d", i * tick);
		float length = font->CalcTextSizeA (fontSize, FLT_MAX, 0, number).x;
		addTurnedText (list, font, fontSize, ImVec2 (viewOrigin.x + 1, viewOrigin.y + 1), ImVec2 (box.x + 2, at + 3 + length), ink, number);
	}
	list->PopClipRect ();
}

ImVec2 CanvasView::toWindow (float x, float y) const {
	return ImVec2 (viewOrigin.x + (x / drawnZmul - drawnX) / pixelRatio, viewOrigin.y + (y / drawnZmul - drawnY) / pixelRatio);
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
