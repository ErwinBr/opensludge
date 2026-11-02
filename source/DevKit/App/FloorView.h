#ifndef SLUDGE_DEVKIT_FLOOR_VIEW_H
#define SLUDGE_DEVKIT_FLOOR_VIEW_H

/*
 * The floor editor: the floor on its backdrop in a canvas, the tools of the
 * GTK Floor Maker in a toolbar (keys 1 to 5), and the backdrop, the colour
 * and the state of the floor in the Inspector. We remember the backdrop
 * and the colour of each floor file (FileMemory).
 */

#include <memory>
#include <string>

#include "CanvasView.h"
#include "DocumentView.h"
#include "FloorDocument.h"
#include "sprites.h"

namespace devkit {

class FileMemory;
class WorkspaceActions;

class FloorView : public DocumentView {
public:
	FloorView (FloorDocument * floor, WorkspaceActions & actions, const CanvasGraphics & graphics, FileMemory & memory);
	~FloorView ();

	Document * document () { return floor; }
	void draw ();
	void drawInspector ();
	// A picture becomes the backdrop.
	bool takeFile (const std::string & path);
	bool animating () const;

	enum Tool { TOOL_DRAW, TOOL_MOVE, TOOL_DELETE, TOOL_SPLIT_LINE, TOOL_SPLIT_FLOOR, TOOL_COUNT };

private:
	void drawToolbar ();
	void handleMouse ();
	void paint (Draw2D & draw);
	// How close, in pixels of the picture, the cursor must be to a vertex.
	int closeness () const;

	void chooseBackdrop ();
	bool loadBackdrop (const std::string & path);
	void clearBackdrop ();
	void remember ();
	void recall ();
	void say (const std::string & text);

	FloorDocument * floor;
	WorkspaceActions & actions;
	CanvasGraphics graphics;
	FileMemory & memory;
	CanvasView canvas;

	Tool tool;
	struct spriteBank backdrop;
	std::string backdropFile;
	float colour[3];
	// The file that we remembered the backdrop and colour for.
	std::string rememberedPath;
	// The answer of the file dialog, which may come after we are gone.
	std::shared_ptr<std::string> chosenBackdrop;

	// The vertex under the cursor.
	bool lit;
	int litX, litY;
	// A drag of the Move, Split Line or Split Floor tool, from a vertex.
	bool dragging;
	int fromX, fromY, toX, toY;

	// A short message under the canvas, such as why a vertex can't go
	// somewhere, until this time.
	std::string notice;
	double noticeUntil;
};

} // namespace devkit

#endif
