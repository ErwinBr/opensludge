#ifndef SLUDGE_DEVKIT_FLOOR_VIEW_H
#define SLUDGE_DEVKIT_FLOOR_VIEW_H

/*
 * The floor editor of a .flo: the floor on its backdrop in a canvas, the
 * tools (FloorTools.h) in a toolbar, and the backdrop, the colour and the
 * state of the floor in the Inspector. The Floor tab of the room editor
 * edits a floor on its room instead. We remember the backdrop
 * and the colour of each floor file (FileMemory).
 */

#include <memory>
#include <string>
#include <vector>

#include "CanvasView.h"
#include "DocumentView.h"
#include "FloorDocument.h"
#include "FloorTools.h"
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

private:
	void drawToolbar ();

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

	FloorTools tools;
	struct spriteBank backdrop;
	std::string backdropFile;
	float colour[3];
	// The file that we remembered the backdrop and colour for.
	std::string rememberedPath;
	// The answer of the file dialog, which may come after we are gone.
	std::shared_ptr<std::string> chosenBackdrop;

	// A short message under the canvas, such as why a vertex can't go
	// somewhere, until this time.
	std::string notice;
	double noticeUntil;
};

} // namespace devkit

#endif
