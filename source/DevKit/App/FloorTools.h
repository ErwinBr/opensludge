#ifndef SLUDGE_DEVKIT_FLOOR_TOOLS_H
#define SLUDGE_DEVKIT_FLOOR_TOOLS_H

/*
 * The tools of the GTK Floor Maker (keys 1 to 5) on a floor in a canvas,
 * for both floor editors: the floor editor of a .flo on a backdrop
 * (FloorView), and the Floor tab of the room editor (RoomView), which draws
 * the floor over the room. What the tools do is a command of the floor
 * document; here are the mouse, the selection and what we draw of them.
 */

#include <functional>
#include <string>
#include <vector>

#include "FloorDocument.h"

class Draw2D;
struct spriteBank;

namespace devkit {

class CanvasView;

class FloorTools {
public:
	enum Tool { TOOL_DRAW, TOOL_MOVE, TOOL_DELETE, TOOL_SPLIT_LINE, TOOL_SPLIT_FLOOR, TOOL_COUNT };

	// say shows a short message, such as why a vertex can't go somewhere.
	FloorTools (Tool tool, std::function<void (const std::string & text)> say);

	Tool tool;

	// Whether a drag or a box needs frames.
	bool busy () const { return dragging || boxing; }
	// Forget the drag, the box and the selection, as for another floor.
	void reset ();

	// The buttons of the tools, for a toolbar, and their keys, which we
	// look at while the user doesn't type.
	void drawToolButtons ();
	void handleToolKeys ();
	// After the canvas is drawn: the mouse and the keys of the tools.
	void handleMouse (FloorDocument & floor, const CanvasView & canvas);
	// The floor, as it will be while the user drags, on the backdrop, or
	// over what the canvas has drawn already when it is NULL.
	void paint (Draw2D & draw, const FloorDocument & floor, const CanvasView & canvas, spriteBank * backdrop, const float colour[3]) const;

	// The parts of the Inspector: the tools, and the state of the floor.
	void drawToolInspector ();
	void drawFloorInspector (FloorDocument & floor);

private:
	// How close, in pixels of the picture, the cursor must be to a vertex.
	int closeness (const CanvasView & canvas) const;
	void keepSelection (const FloorDocument & floor);
	void selectionOffset (int * dx, int * dy) const;

	std::function<void (const std::string & text)> say;

	// The vertex under the cursor.
	bool lit;
	int litX, litY;
	// A drag of the Move, Split Line or Split Floor tool, from a vertex.
	bool dragging;
	int fromX, fromY, toX, toY;
	// Where the mouse was pressed for the drag, in pixels of the picture.
	int pressX, pressY;
	// The vertices that the Move tool moves, and the box that selects them.
	std::vector<FloorPoint> selected;
	bool boxing;
	int boxX1, boxY1, boxX2, boxY2;
};

} // namespace devkit

#endif
