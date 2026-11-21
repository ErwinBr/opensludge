#ifndef SLUDGE_DEVKIT_CANVAS_VIEW_H
#define SLUDGE_DEVKIT_CANVAS_VIEW_H

/*
 * The picture of an editor, such as a floor on its backdrop: drawn with
 * Draw2D into a texture of the size of the view, which we show in the
 * window. The canvas zooms with the mouse wheel around the cursor, pans
 * with the middle or right button or with Space and the left button, and
 * zooms to 100% and to fit. The editor gets the left button in the
 * coordinates of the picture.
 *
 * With rulers, the top and the left of the box show the pixels of the
 * picture from its origin, and the user drags guides out of them: a
 * horizontal one from the top ruler, a vertical one from the left one.
 * Dragging a guide back into a ruler, or out of the canvas, removes it.
 */

#include <functional>
#include <string>
#include <vector>

#include "imgui.h"

class Draw2D;

namespace gfx {
class Renderer;
struct Texture;
struct RenderTarget;
}

namespace devkit {

// What the canvases draw with: the renderer of the window, and a Draw2D
// over it whose textures they share.
struct CanvasGraphics {
	gfx::Renderer * renderer;
	Draw2D * draw;
};

class CanvasView {
public:
	explicit CanvasView (const CanvasGraphics & graphics);
	~CanvasView ();

	// The size of the picture, which 100% and Fit show. When it changes, we
	// fit it in the view again.
	void setPictureSize (int width, int height);
	// The part of the picture that 100% and Fit show, when it doesn't start
	// at 0, 0, as a sprite around its hotspot. A change doesn't fit it
	// again, so that the next sprite of an animation stays in its place.
	void setPictureBounds (int x, int y, int width, int height);
	// Draw with Draw2D::setSpriteViewArea, in which y grows upwards from
	// the hotspot. The mouse is still in y that grows downwards: -y of the
	// sprite's.
	void setSpriteView (bool on) { spriteView = on; }
	// Rulers on the top and the left of the box, which the guides come from.
	void setRulers (bool on) { rulers = on; }
	void zoomToFit ();
	void zoomToActualSize ();
	void zoomBy (float factor);
	// The zoom, in points of the window for each pixel of the picture.
	float zoom () const { return scale; }

	// The buttons of the zoom, for a toolbar.
	void drawZoomButtons ();
	// Draw the canvas in a box of size points, with paint, which draws the
	// picture with the view area set up.
	void draw (const ImVec2 & size, std::function<void (Draw2D & draw)> paint);
	// The cursor, the size of the picture and the zoom, for a status line.
	std::string status () const;

	// After draw: the mouse in the picture, and the left button there.
	bool hovered () const { return isHovered; }
	float mouseX () const { return mouse.x; }
	float mouseY () const { return mouse.y; }
	bool pressed () const { return leftPressed; }
	bool held () const { return leftHeld; }
	bool released () const { return leftReleased; }
	// How many pixels of the picture some points of the window are, for
	// how close the cursor must be to something.
	float pixelsOfPoints (float points) const { return points / scale; }
	// After draw: where a point of the picture is in the window, in points,
	// to draw over the canvas with ImGui; and the box of the canvas there.
	ImVec2 toWindow (float x, float y) const;
	ImVec2 viewMin () const { return viewOrigin; }
	ImVec2 viewMax () const { return ImVec2 (viewOrigin.x + viewSize.x, viewOrigin.y + viewSize.y); }

	// After a press that the editor has no use for: take the guide under
	// the mouse, if there is one, which the user then drags. The left
	// button isn't held for the editor any more.
	bool grabGuide ();

	// Whether the user is panning or dragging a guide, and we need frames.
	bool animating () const { return panning || guideDrag != GUIDE_NONE; }

private:
	enum GuideDrag { GUIDE_NONE, GUIDE_HORIZONTAL, GUIDE_VERTICAL };

	void ensureTarget (int width, int height);
	void centre ();
	// The guide that the mouse is on, if any, at most a few points away.
	bool guideAtMouse (GuideDrag & kind, int & index) const;
	void dragGuide (bool overView);
	void drawRulers (ImDrawList * list, const ImVec2 & box, const ImVec2 & boxEnd);
	void drawGuides (ImDrawList * list);

	CanvasGraphics graphics;
	gfx::Texture * texture;
	gfx::RenderTarget * target;
	int targetWidth, targetHeight;

	int pictureX, pictureY, pictureWidth, pictureHeight;
	bool spriteView;
	bool rulers;
	// The guides, in pixels of the picture: y for horizontal ones, x for
	// vertical ones; and the one that the user drags.
	std::vector<float> horizontalGuides, verticalGuides;
	GuideDrag guideDrag;
	float guidePosition;
	bool fitPending;
	// Whether we show the picture as Fit did, so that we fit it again when
	// the view changes size, until the user zooms or pans.
	bool fitted;
	float scale;				// Points for each pixel of the picture
	// The origin of the picture is scrollX, scrollY points left of and
	// above the top left of the view.
	float scrollX, scrollY;
	ImVec2 viewSize;			// In points
	float pixelRatio;			// Pixels of the window for each point

	bool isHovered, panning, leftPressed, leftHeld, leftReleased;
	ImVec2 mouse;
	// How the last draw showed the picture: the top left of the view, the
	// scroll in pixels of the window, and pixels of the picture for each.
	ImVec2 viewOrigin;
	int drawnX, drawnY;
	float drawnZmul;
};

} // namespace devkit

#endif
