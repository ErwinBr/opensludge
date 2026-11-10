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
	bool canCopy () const { return ! selectedKeys.empty () || ! selectedEvents.empty (); }
	void copy () { copySelection (); }
	bool canPaste () const;
	void pasteClipboard () { paste (); }

private:
	// A row of the track panel: a part, or a track of one.
	struct Row {
		bool isTrack;
		std::string part;
		TrackId track;
		Channel channel;
		int depth;
		bool missing;		// A part that the rig doesn't have
		// Its tracks: one, or of a colour, those of its four channels that
		// the clip has (track is the first of them).
		std::vector<TrackId> tracks;
	};
	// A key as a row shows it: of a colour, the keys of its channels at a
	// tick, with the colour there and the easing of the first.
	struct RowKey {
		int time;
		std::vector<KeyRef> refs;
		Easing easing;
		float colour[4];
	};
	std::vector<RowKey> rowKeys (const Row & row) const;
	// The rows of a part's tracks.
	void addTrackRows (const std::string & part, int depth, bool missing);
	struct Pending {
		bool waiting, answered;
		enum What { SOUND, BANK, PART_BANK } what;
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
	void drawTransport ();
	// The bar between the picture and the timeline, which moves the split.
	void drawSplitter (float total, float least);
	void drawPartPanel (float height);
	// Select the part of that name, unless it is locked or the rig lacks it.
	void selectPart (const std::string & name);
	void drawCanvas (float height);
	void drawTimeline ();
	void drawLengthDialog ();
	void drawFunctionDialog ();
	void handleCanvasMouse ();
	void handleKeys ();
	void handlePending ();
	void askForFile (Pending::What what);

	int playhead () const;
	void scrub (int tick);
	// Pauses, and silences the sounds it was playing.
	void pause ();
	// Plays the sounds of events that playing passed.
	void playSounds (const std::vector<const ClipEvent *> & passed);
	std::set<std::string> soundsSaid;
	void requestLength (int length);
	void selectOnly (const KeyRef & key);
	void selectOnly (const std::vector<KeyRef> & keys);
	// The selected keys, when they are one key of a row: one key, or the
	// keys of a colour at a tick.
	bool soleKey (std::vector<KeyRef> & refs) const;
	void removeSelection ();
	void copySelection ();
	void paste ();
	void addKeyAtPlayhead (TrackId track);

	// A change to the selected part: keyed at the playhead with Auto key, and
	// otherwise pending, a preview until its key button keys it. Pending
	// values go when the playhead moves, the clip plays or changes, or
	// another part is selected.
	struct PendingValue {
		std::string part;
		Channel channel;
		float value;
		SpriteRef::Kind kind;	// For SPRITE: its bank, number and mirror
		std::string bank;
		int number;
		bool mirror;
	};
	void setValue (const std::string & part, Channel channel, float value, int mergeKey);
	void setSprite (const std::string & part, SpriteRef::Kind kind, const std::string & bank, int number, bool mirror);
	PendingValue * pendingValue (const std::string & part, Channel channel);
	void forgetStalePending ();
	// The clip with the pending values keyed in it, for the preview.
	ClipDefinitionRef withPending (const ClipDefinitionRef & clip);
	// Key the channels at the playhead: their pending values, or what they
	// are there now.
	void keyChannels (const std::string & part, const std::vector<Channel> & channels);
	bool keyedAt (const std::string & part, Channel channel) const;
	float valueOf (int part, Channel channel) const;
	// A key button at the end of a row of the Inspector, for its channels.
	void keyButton (const char * id, const std::string & part, const std::vector<Channel> & channels, float rightX = 0);
	void drawPartInspector ();
	void drawKeyInspector (const std::vector<KeyRef> & keys);
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
	std::set<EventId> selectedEvents;
	EventId draggedEvent;
	// The one event selected, or 0.
	EventId soleEvent () const { return selectedEvents.size () == 1 ? * selectedEvents.begin () : 0; }
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
	float split;			// Of the height, the picture's; 0 until read
	std::vector<PendingValue> pendingValues;
	PartId pendingPart;
	int pendingTick;
	uint64_t pendingBasis;
	std::string notice;
	double noticeUntil;
};

} // namespace devkit

#endif
