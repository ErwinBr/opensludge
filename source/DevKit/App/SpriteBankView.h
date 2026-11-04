#ifndef SLUDGE_DEVKIT_SPRITE_BANK_VIEW_H
#define SLUDGE_DEVKIT_SPRITE_BANK_VIEW_H

/*
 * The sprite bank editor: the sprites as thumbnails on the left, the
 * selected one at its hotspot in a canvas, and the mode, the hotspot and
 * the font in the Inspector. Dragging on the canvas moves the sprite
 * around its hotspot, as in the GTK Sprite Bank Editor; Ctrl-click
 * (Cmd-click on macOS) puts the hotspot on a pixel. In the list,
 * Shift-click selects a range of sprites and Ctrl-click (Cmd-click) adds
 * or takes away one, for Delete and for the hotspot buttons; dragging
 * sprites in the list puts them somewhere else.
 */

#include <memory>
#include <string>
#include <vector>

#include "CanvasView.h"
#include "DocumentView.h"
#include "SpriteBankDocument.h"

namespace devkit {

class WorkspaceActions;

class SpriteBankView : public DocumentView {
public:
	SpriteBankView (SpriteBankDocument * bank, WorkspaceActions & actions, const CanvasGraphics & graphics);
	~SpriteBankView ();

	Document * document () { return bank; }
	void draw ();
	void drawInspector ();
	// Pictures become sprites, after the selected ones.
	bool takeFile (const std::string & path);
	bool animating () const;

private:
	// What a file dialog was for, and its answer, which may come after we
	// are gone.
	struct Pending {
		enum What { NONE, INSERT, REPLACE, EXPORT, FONT } what;
		bool answered;
		std::vector<std::string> files;
		Pending () : what (NONE), answered (false) {}
	};

	void drawToolbar ();
	void drawThumbnails ();
	void drawCanvas ();
	void handleMouse ();
	void handleKeys ();
	void handlePending ();
	void askForFiles (Pending::What what);

	// The selected sprites, ascending.
	std::vector<int> selection () const;
	int selectedCount () const;
	bool isSelected (int index) const { return index >= 0 && index < (int) chosen.size () && chosen[index]; }
	// Where new sprites go: after the last selected one.
	int insertionPoint () const;
	// Keep the selection as long as the bank, with something in it.
	void keepSelection ();
	// Select one sprite, or the range from the anchor to it.
	void select (int index, bool extend);
	// Select howMany sprites from one.
	void selectRun (int from, int howMany, int newCurrent);
	// A click on a sprite in the list.
	void click (int index, bool shift, bool ctrl);
	// Move the sprites that a drag from dragged took to the gap before to.
	void moveDragged (int dragged, int to);
	void insert (const std::vector<std::string> & files);
	void deleteSelected ();
	void say (const std::string & text);

	SpriteBankDocument * bank;
	// Its pictures, which we forget when we go, after the document.
	const spriteBank * pictures;
	WorkspaceActions & actions;
	CanvasGraphics graphics;
	CanvasView canvas;

	// The selected sprites; current is the one on the canvas, and anchor
	// where a Shift-click range starts.
	std::vector<bool> chosen;
	int anchor, current;
	bool scrollToCurrent;
	bool showBox;
	int spaceWidth;		// For Make Font

	// A drag of the sprite around its hotspot.
	bool dragging;
	Hotspot dragHotspot;
	float dragX, dragY;

	std::shared_ptr<Pending> pending;
	std::string notice;
	double noticeUntil;
};

} // namespace devkit

#endif
