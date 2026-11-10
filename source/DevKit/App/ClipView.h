#ifndef SLUDGE_DEVKIT_CLIP_VIEW_H
#define SLUDGE_DEVKIT_CLIP_VIEW_H

/*
 * The animation editor: a clip (.clp) of the parts of a rig. At the top,
 * the character, posed as the clip has it at the playhead; below, its
 * tracks in the order of the rig's parts, with their keys on a timeline,
 * and the clip's events above them. The Inspector shows what is selected:
 * a key, an event, or the clip.
 *
 * On the canvas, moving, rotating or scaling a part keys it at the
 * playhead, always: it makes the part's track if it has none, and its key
 * there, or changes the one there, as one change that undoes. The rest pose
 * is the rig's, in the character creator. Scale handles are on the corners
 * of the selected sprite's box, and in the middles of its sides when its
 * axes scale apart (the lock by the part's name in the toolbar). A locked
 * part (the lock at the end of its row) can't be selected or keyed on the
 * canvas, nor its keys selected or moved on the timeline.
 *
 * On the timeline, a click selects a key (Shift adds it), a drag of the
 * empty lanes selects the keys in its box, and a drag of keys moves them a
 * whole tick at a time (with Alt it copies them). Delete removes them. A
 * click in the ruler puts the playhead there, which shows the pose there
 * and nothing more; Play plays the clip, at the game's speed or another,
 * with a second clip over it on a layer if one is chosen, and flashes the
 * events as it passes them. The handle at the end of the ruler changes the
 * clip's length.
 */

#include <set>
#include <string>
#include <vector>

#include "CanvasView.h"
#include "ClipDocument.h"
#include "DocumentView.h"
#include "PosePreview.h"
#include "PreviewPlayer.h"

namespace devkit {

class WorkspaceActions;

class ClipView : public DocumentView {
public:
	ClipView (ClipDocument * clip, WorkspaceActions & actions, const CanvasGraphics & graphics);
	~ClipView ();

	Document * document () { return clip; }
	void draw ();
	void drawInspector ();
	// A sprite bank becomes the selected part's sprite at the playhead.
	bool takeFile (const std::string & path);
	bool animating () const;

private:
	// A row of the track panel: a part, or a track of one.
	struct Row {
		bool isTrack;
		std::string part;
		TrackId track;
		Channel channel;
		int depth;
		bool missing;		// A part that the rig doesn't have
	};
	struct Pending {
		bool waiting, answered;
		enum What { SOUND, BANK } what;
		std::vector<std::string> files;
		Pending () : waiting (false), answered (false), what (SOUND) {}
	};
	enum Drag { DRAG_NONE, DRAG_MOVE, DRAG_TURN, DRAG_SCALE };
	enum TimelineDrag { TL_NONE, TL_SCRUB, TL_LENGTH, TL_KEYS, TL_BOX, TL_EVENT };

	std::string baseFolder () const;
	// The rig it is made for, live from its document if that is open.
	bool findRig ();
	void findSecond ();
	void pose ();
	void buildRows ();
	void addRows (int part, int depth);
	bool partShown (int part) const;
	// Whether a part (by name) is locked, and whether its axes scale
	// together: as the clip says, or as its rig's part does.
	bool locked (const std::string & part) const { return clip->isLocked (part); }
	bool uniform (const std::string & part) const;

	void drawToolbar ();
	void drawCanvas ();
	void drawTimeline ();
	void drawLengthDialog ();
	void drawFunctionDialog ();
	void handleCanvasMouse ();
	void handleKeys ();
	void handlePending ();
	void askForFile (Pending::What what);

	int playhead () const;
	void scrub (int tick);
	void requestLength (int length);
	void selectOnly (const KeyRef & key);
	void removeSelection ();
	void addKeyAtPlayhead (TrackId track);
	void say (const std::string & text);

	ClipDocument * clip;
	WorkspaceActions & actions;
	CanvasGraphics graphics;
	CanvasView canvas;
	PosePreview preview;
	PreviewPlayer player;
	double lastTime;

	// The rig, and where it came from.
	animformat::Asset rigAsset;
	bool haveRig;
	std::string rigFile;
	RigDefinitionRef posedRig;
	ClipDefinitionRef posedClip, posedSecond;
	Pose shown, keyed;		// With the second clip; of the edited one alone
	PoseBinding bindings[2], keyBinding;

	// The second clip: a file of the project, and how it blends.
	std::string secondFile;
	animformat::Asset secondAsset;
	bool haveSecond;
	LayerBlend secondBlend;

	std::vector<Row> rows;
	std::set<std::string> collapsed;
	bool onlyAnimated;

	std::set<KeyRef> selectedKeys;
	EventId selectedEvent;
	PartId selectedPart;
	bool havePart;
	// The track of the panel that K keys at the playhead.
	TrackId currentTrack;

	TimelineDrag timelineDrag;
	float dragStartX, dragStartY;
	int dragTicks;
	bool dragCopy;
	int dragEventTime;
	float tickWidth, laneLeft;

	Drag drag;
	bool dragMoved;		// A click without a move keys nothing
	float dragX, dragY;
	PartPose dragLocal;
	ScaleHandle dragHandle;
	Transform2D dragParent, dragWorld;
	std::string keying;

	int pendingLength;
	bool lengthDialog;
	std::string functionName;
	bool functionDialog;
	std::vector<std::pair<EventId, double> > flashes;
	std::shared_ptr<Pending> pending;
	std::string notice;
	double noticeUntil;
};

} // namespace devkit

#endif
