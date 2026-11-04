#ifndef SLUDGE_DEVKIT_ZBUFFER_VIEW_H
#define SLUDGE_DEVKIT_ZBUFFER_VIEW_H

/*
 * The z-buffer editor: the layers in a canvas, the selected one in white
 * with its y as a green line, and the layers and their y in the Inspector.
 * Clicking or dragging on the canvas sets the y of the selected layer, as
 * in the GTK Z-Buffer Maker; with Ctrl (Cmd on macOS), a click selects
 * the layer under the cursor.
 */

#include <memory>
#include <string>

#include "CanvasView.h"
#include "DocumentView.h"
#include "ZBufferDocument.h"

namespace devkit {

class WorkspaceActions;

class ZBufferView : public DocumentView {
public:
	ZBufferView (ZBufferDocument * zbuffer, WorkspaceActions & actions, const CanvasGraphics & graphics);
	~ZBufferView ();

	Document * document () { return zbuffer; }
	void draw ();
	void drawInspector ();
	// A TGA picture becomes the layers.
	bool takeFile (const std::string & path);
	bool animating () const;

private:
	void choosePicture ();
	void makeFrom (const std::string & path);
	void handleMouse ();
	void say (const std::string & text);

	ZBufferDocument * zbuffer;
	// Its pictures, which we forget when we go, after the document.
	const spriteBank * pictures;
	WorkspaceActions & actions;
	CanvasGraphics graphics;
	CanvasView canvas;

	int selected;			// The layer, from 1
	bool dragging;
	// The answer of the file dialog, which may come after we are gone.
	std::shared_ptr<std::string> chosenPicture;

	std::string notice;
	double noticeUntil;
};

} // namespace devkit

#endif
