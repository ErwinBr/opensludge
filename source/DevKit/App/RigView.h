#ifndef SLUDGE_DEVKIT_RIG_VIEW_H
#define SLUDGE_DEVKIT_RIG_VIEW_H

/*
 * The character creator: the parts of a rig as a tree on the left, the
 * character in its rest pose in a canvas, and the selected part's sprite,
 * position, rotation, scale, depth and pivot in the Inspector.
 *
 * On the canvas, a click selects the part whose sprite shows there, and a
 * drag moves it, which places it against its parent: so it places the
 * point where it is attached. The ring around the selected part's origin
 * rotates it with its round knob. The squares on the corners of its
 * sprite's box scale it, and those in the middles of the sides scale one
 * axis when its axes scale apart (the lock of Scale in the Inspector). A
 * drag of the origin itself moves the pivot, the point the part rotates
 * about, and leaves the sprite where it is. Positions and pivots snap to
 * whole pixels, rotations to whole degrees, and scales to whole percents.
 *
 * In the tree, dragging a part onto another attaches it there, where it
 * stays on screen; the arrows order the parts of a parent, which the engine
 * draws in that order when their depths are the same. The lock at the end
 * of a row locks its part: it can't be selected, so a click goes through
 * to the part under it.
 *
 * The paths of sprite banks are from the open project's folder, as the
 * compiler finds them, or from the rig's own folder without a project.
 */

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "CanvasView.h"
#include "DocumentView.h"
#include "PosePreview.h"
#include "RigDocument.h"

namespace devkit {

class WorkspaceActions;

class RigView : public DocumentView {
public:
	RigView (RigDocument * rig, WorkspaceActions & actions, const CanvasGraphics & graphics);
	~RigView ();

	Document * document () { return rig; }
	void draw ();
	void drawInspector ();
	// A sprite bank becomes the selected part's sprite.
	bool takeFile (const std::string & path);
	bool animating () const;

private:
	struct Pending {
		bool waiting, answered;
		std::vector<std::string> files;
		Pending () : waiting (false), answered (false) {}
	};
	enum Drag { DRAG_NONE, DRAG_MOVE, DRAG_TURN, DRAG_SCALE, DRAG_PIVOT };

	// The folder that the paths of banks are from, or empty while there is
	// none (an untitled rig, without a project).
	std::string baseFolder () const;
	// The bank of a path of the rig, loaded once; NULL if it can't be.
	AnimBankRef bank (const std::string & path);
	// The rig with its banks, and its rest pose.
	void pose ();

	void drawToolbar ();
	void drawTree ();
	void drawCanvas ();
	void handleMouse ();
	void handleKeys ();
	void handlePending ();
	void chooseBank ();
	void setBank (const std::string & file);

	void addPart ();
	void removeSelected ();
	void moveSelected (int by);
	bool canPick (int part) const;
	void centrePivot (const RigPart & part);
	void say (const std::string & text);
	int selectedIndex () const;

	RigDocument * rig;
	WorkspaceActions & actions;
	CanvasGraphics graphics;
	CanvasView canvas;

	PartId selected;
	bool haveSelection;
	PosePreview preview;
	RigDefinitionRef posedRig;
	Pose restPose;

	Drag drag;
	float dragX, dragY;			// Where the drag started, on the canvas
	PartPose dragRest;
	ScaleHandle dragHandle;
	float dragPivotX, dragPivotY;
	Transform2D dragParent, dragWorld;

	std::string nameField;
	PartId nameFieldPart;
	bool nameEditing;
	bool keepPivotInPlace;
	std::shared_ptr<Pending> pending;
	std::string notice;
	double noticeUntil;
};

} // namespace devkit

#endif
