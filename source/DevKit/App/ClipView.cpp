#include <math.h>
#include <stdio.h>

#include <algorithm>

#include "imgui.h"
#include "imgui_stdlib.h"
#include "renderer.h"

#include "animedit.h"
#include "draw2d.h"
#include "draw2d_gfx.h"

#include "ClipView.h"
#include "imgui_impl_gfx.h"
#include "Icons.h"
#include "PartTree.h"
#include "Platform.h"
#include "Properties.h"
#include "SoundPreview.h"
#include "RigDocument.h"
#include "WorkspaceActions.h"

namespace devkit {

namespace {

const double NOTICE_SECONDS = 5.0;
// How long an event's marker flashes as playback passes it.
const double FLASH_SECONDS = 0.3;
// The merge keys: a drag on the canvas, the fields, a drag of an event.
const int DRAG_KEY = 1, FIELD_KEY = 2, EVENT_KEY = 3, BUTTON_KEY = 4;
const float RING_POINTS = 40, GRAB_POINTS = 8;
const double pi = 3.14159265358979323846;

// The channels in the order of the panel.
const Channel channelOrder[] = { CHANNEL_X, CHANNEL_Y, CHANNEL_ROTATION, CHANNEL_SCALE, CHANNEL_SCALE_X, CHANNEL_SCALE_Y,
	CHANNEL_SPRITE, CHANNEL_VISIBLE, CHANNEL_DEPTH, CHANNEL_FLIP };

const char * channelLabel (Channel c) {
	switch (c) {
		case CHANNEL_X: return "Position X";
		case CHANNEL_Y: return "Position Y";
		case CHANNEL_ROTATION: return "Rotation";
		case CHANNEL_SCALE: return "Scale (both)";
		case CHANNEL_SCALE_X: return "Scale X";
		case CHANNEL_SCALE_Y: return "Scale Y";
		case CHANNEL_SPRITE: return "Sprite";
		case CHANNEL_VISIBLE: return "Visible";
		case CHANNEL_DEPTH: return "Depth";
		case CHANNEL_FLIP: return "Flip";
		default: return "?";
	}
}

const char * easingLabels[] = { "Step: hold until the next key", "Linear: at a steady speed", "In and out: slow, fast, slow" };

// "X" or "Y" before a field of one axis, which takes the rest of the row.
void axisLabel (const char * axis) {
	ImGui::TextUnformatted (axis);
	ImGui::SameLine (0, ImGui::GetStyle ().ItemInnerSpacing.x);
	ImGui::SetNextItemWidth (-FLT_MIN);
}

std::string partLabel (const std::string & part) {
	return part.empty () ? std::string ("Character") : part;
}

// The colours of the timeline.
const ImU32 KEYING = IM_COL32 (255, 80, 80, 255);
const ImU32 SOUND = IM_COL32 (255, 170, 60, 255);
const ImU32 FUNCTION = IM_COL32 (190, 120, 255, 255);

// The colour of a key by its easing: step grey, linear blue, in and out
// pink; selected, green (as a key in the Inspector).
ImU32 easingColour (Easing e, bool selected) {
	ImU32 c = e == EASING_STEP ? IM_COL32 (190, 190, 190, 255) : e == EASING_INOUT ? IM_COL32 (255, 120, 200, 255) : IM_COL32 (100, 170, 255, 255);
	return selected ? IM_COL32 (90, 220, 110, 255) : c;
}

float distance (float x1, float y1, float x2, float y2) {
	return sqrtf ((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
}

Transform2D linear (Transform2D t) {
	t.tx = t.ty = 0;
	return t;
}

float wrapDegrees (float d) {
	d = fmodf (d, 360.0f);
	if (d >= 180.0f) d -= 360.0f;
	if (d < -180.0f) d += 360.0f;
	return d;
}

bool loadAsset (const std::string & path, animformat::Asset & asset) {
	FILE * f = fopen (path.c_str (), "rb");
	if (! f) return false;
	animformat::Bytes bytes;
	unsigned char buffer[4096];
	size_t n;
	while ((n = fread (buffer, 1, sizeof (buffer), f)) > 0) bytes.insert (bytes.end (), buffer, buffer + n);
	fclose (f);
	std::string why;
	return animformat::decodeAsset (bytes.data (), bytes.size (), asset, why);
}

}

ClipView::ClipView (ClipDocument * c, WorkspaceActions & a, const CanvasGraphics & g)
	: clip (c), actions (a), graphics (g), canvas (g), preview (g.draw), lastTime (-1), haveRig (false),
	  haveSecond (false), secondBlend (LAYER_BLEND_OVERRIDE), onlyAnimated (true), draggedEvent (0), selectedPart (0),
	  havePart (false), currentTrack (0), timelineDrag (TL_NONE), dragStartX (0), dragStartY (0), dragTicks (0),
	  dragCopy (false), dragEventTime (0), tickWidth (1), laneLeft (0), drag (DRAG_NONE), dragMoved (false), dragX (0), dragY (0),
	  pendingLength (0), lengthDialog (false), functionDialog (false), pending (new Pending), pendingPart (0), pendingTick (0), pendingBasis (0), split (0), noticeUntil (0) {
}

ClipView::~ClipView () {
	if (player.playing ()) actions.sounds ().stopAll ();
}

bool ClipView::animating () const {
	return player.playing () || drag != DRAG_NONE || timelineDrag != TL_NONE || ! flashes.empty () || canvas.animating ();
}

void ClipView::say (const std::string & text) {
	notice = text;
	noticeUntil = ImGui::GetTime () + NOTICE_SECONDS;
}

std::string ClipView::baseFolder () const {
	std::string project = actions.projectFile ();
	return project.empty () ? folderOf (clip->path ()) : folderOf (project);
}

int ClipView::playhead () const {
	int t = (int) floor (player.ticks () + 0.5);
	return t < 0 ? 0 : t > clip->clip ().length ? clip->clip ().length : t;
}

void ClipView::pause () {
	actions.sounds ().stopAll ();
	player.pause ();
}

void ClipView::playSounds (const std::vector<const ClipEvent *> & passed) {
	for (size_t i = 0; i < passed.size (); i ++) {
		if (passed[i]->what.kind != FrameEvent::SOUND) continue;
		std::string name = clip->eventName (passed[i]->id), why;
		if (name.empty ()) continue;
		// A sound that can't play says so once, and playing carries on.
		if (! actions.sounds ().play (baseFolder () + name, why) && soundsSaid.insert (name).second) say (why);
	}
}

void ClipView::scrub (int tick) {
	pause ();
	player.scrub (tick < 0 ? 0 : tick > clip->clip ().length ? clip->clip ().length : tick);
}

bool ClipView::findRig () {
	const animformat::Asset & a = clip->asset ();
	if (a.rigId.isNull ()) {
		haveRig = false;
		rigFile.clear ();
		return false;
	}
	std::string full = baseFolder () + a.rigPath;
	// Live from the character creator, if the rig is open there.
	RigDocument * open = dynamic_cast<RigDocument *> (actions.findDocument (full));
	if (open && open->asset ().id == a.rigId) {
		rigAsset = open->asset ();
		haveRig = true;
		rigFile = full;
		return true;
	}
	if (full != rigFile) {
		rigFile = full;
		animformat::Asset loaded;
		haveRig = loadAsset (full, loaded) && loaded.kind == animformat::Asset::RIG && loaded.id == a.rigId;
		if (haveRig) rigAsset = loaded;
	}
	return haveRig;
}

void ClipView::findSecond () {
	if (secondFile.empty ()) {
		haveSecond = false;
		return;
	}
	ClipDocument * open = dynamic_cast<ClipDocument *> (actions.findDocument (secondFile));
	if (open) {
		secondAsset = open->asset ();
		haveSecond = true;
	} else if (! haveSecond) {
		haveSecond = loadAsset (secondFile, secondAsset) && secondAsset.kind == animformat::Asset::CLIP;
		if (! haveSecond) secondFile.clear ();
	}
}

void ClipView::pose () {
	std::string base = baseFolder ();
	findRig ();
	findSecond ();
	posedRig = haveRig ? preview.rig (rigAsset, base) : RigDefinitionRef (std::make_shared<RigDefinition> ());
	posedClip = withPending (preview.clip (clip->asset (), base));
	posedSecond = haveSecond ? preview.clip (secondAsset, base) : ClipDefinitionRef ();
	player.setClip (0, posedClip, LAYER_BLEND_OVERRIDE);
	player.setClip (1, posedSecond, secondBlend);
	std::vector<LayerSamples> layers;
	std::vector<TrackSamples> samples;
	player.layers (layers, samples, bindings);
	evaluatePose (posedRig, layers, std::vector<PartOverride> (), Transform2D (), shown);
	// The edited clip alone: what keying starts from.
	TrackSamples own;
	evaluateClip (* posedClip, player.time (0), own);
	evaluatePose (posedRig, posedClip, own, Transform2D (), keyBinding, keyed);
}

bool ClipView::uniform (const std::string & part) const {
	animformat::EditorPart::Scaling s = clip->scaling (part);
	if (s != animformat::EditorPart::SCALING_DEFAULT) return s == animformat::EditorPart::SCALING_UNIFORM;
	int p = haveRig ? rigAsset.rig.findPart (part) : -1;
	return p < 0 || rigAsset.editorPart (rigAsset.rig.parts[p].id).scaling != animformat::EditorPart::SCALING_FREE;
}

bool ClipView::partShown (int part) const {
	if (! onlyAnimated) return true;
	const RigDefinition & r = rigAsset.rig;
	const ClipDefinition & c = clip->clip ();
	for (size_t t = 0; t < c.tracks.size (); t ++) {
		int p = r.findPart (c.tracks[t].part);
		if (p >= 0 && partIsUnder (r, p, part)) return true;
	}
	return false;
}

void ClipView::addRows (int part, int depth) {
	const RigDefinition & r = rigAsset.rig;
	if (! partShown (part)) return;
	const std::string & name = r.parts[part].name;
	Row header = { false, name, 0, CHANNEL_X, depth, false };
	rows.push_back (header);
	if (collapsed.count (name)) return;
	for (size_t i = 0; i < sizeof (channelOrder) / sizeof (channelOrder[0]); i ++) {
		const Track * t = clip->track (name, channelOrder[i]);
		if (! t) continue;
		Row row = { true, name, t->id, t->channel, depth + 1, false };
		rows.push_back (row);
	}
	std::vector<int> children = partChildren (r, part);
	for (size_t i = 0; i < children.size (); i ++) addRows (children[i], depth + 1);
}

void ClipView::buildRows () {
	rows.clear ();
	std::vector<std::string> missing;
	if (haveRig) {
		addRows (0, 0);
		missing = clip->missingParts (rigAsset.rig);
	} else {
		std::set<std::string> parts;
		for (size_t t = 0; t < clip->clip ().tracks.size (); t ++) parts.insert (clip->clip ().tracks[t].part);
		missing.assign (parts.begin (), parts.end ());
	}
	// The tracks of parts that the rig hasn't, at the end, so that none is
	// hidden.
	for (size_t m = 0; m < missing.size (); m ++) {
		Row header = { false, missing[m], 0, CHANNEL_X, 0, haveRig };
		rows.push_back (header);
		if (collapsed.count (missing[m])) continue;
		for (size_t i = 0; i < sizeof (channelOrder) / sizeof (channelOrder[0]); i ++) {
			const Track * t = clip->track (missing[m], channelOrder[i]);
			if (! t) continue;
			Row row = { true, missing[m], t->id, t->channel, 1, haveRig };
			rows.push_back (row);
		}
	}
}

void ClipView::selectOnly (const KeyRef & key) {
	selectedKeys.clear ();
	selectedKeys.insert (key);
	selectedEvents.clear ();
	currentTrack = key.track;
}

void ClipView::removeSelection () {
	for (std::set<EventId>::const_iterator e = selectedEvents.begin (); e != selectedEvents.end (); ++ e) clip->removeEvent (* e);
	selectedEvents.clear ();
	if (! selectedKeys.empty ()) {
		clip->removeKeys (std::vector<KeyRef> (selectedKeys.begin (), selectedKeys.end ()));
		selectedKeys.clear ();
	}
}

bool ClipView::canPaste () const {
	return ! actions.clipClipboard ().empty ();
}

void ClipView::copySelection () {
	std::vector<ClipItem> items = clip->copy (std::vector<KeyRef> (selectedKeys.begin (), selectedKeys.end ()),
		std::vector<EventId> (selectedEvents.begin (), selectedEvents.end ()));
	if (items.empty ()) return;
	actions.clipClipboard () = items;
	char text[64];
	snprintf (text, sizeof (text), "Copied %d key%s and event%s.", (int) items.size (), items.size () == 1 ? "" : "s", items.size () == 1 ? "" : "s");
	say (text);
}

void ClipView::paste () {
	const std::vector<ClipItem> & items = actions.clipClipboard ();
	std::string why;
	if (items.empty ()) {
		say ("Nothing to paste: copy keys or events first.");
		return;
	}
	// Keys of one part go onto the selected part, if another is selected.
	std::string onePart;
	bool one = true, anyKey = false;
	for (size_t i = 0; i < items.size (); i ++) {
		if (items[i].isEvent) continue;
		if (anyKey && items[i].part != onePart) one = false;
		onePart = items[i].part;
		anyKey = true;
	}
	int p = havePart && posedRig ? partWithId (* posedRig, selectedPart) : -1;
	std::string target = p >= 0 ? posedRig->parts[p].name : std::string ();
	bool onto = anyKey && one && p >= 0 && target != onePart;
	// Locked parts take no keys.
	for (size_t i = 0; i < items.size (); i ++) {
		if (items[i].isEvent) continue;
		const std::string & part = onto ? target : items[i].part;
		if (locked (part)) {
			say (partLabel (part) + " is locked: unlock it to paste keys on it.");
			return;
		}
	}
	pause ();
	std::vector<KeyRef> keys;
	std::vector<EventId> events;
	if (! clip->paste (items, playhead (), onto ? &target : NULL, &keys, &events, why)) {
		say (why);
		return;
	}
	selectedKeys = std::set<KeyRef> (keys.begin (), keys.end ());
	selectedEvents = std::set<EventId> (events.begin (), events.end ());
}

void ClipView::requestLength (int length) {
	if (length < 0) length = 0;
	if (length == clip->clip ().length) return;
	std::string why;
	if (clip->itemsPast (length)) {
		pendingLength = length;
		lengthDialog = true;
	} else if (! clip->setLength (length, false, why)) {
		say (why);
	}
}

void ClipView::addKeyAtPlayhead (TrackId id) {
	const Track * t = clip->track (id);
	if (! t) return;
	int tick = playhead ();
	std::string why;
	bool ok = true;
	if (t->channel == CHANNEL_SPRITE) {
		const SpriteRef * s = sampleSprite (clip->clip (), t->part, ticksToTime (tick));
		if (s && s->kind == SpriteRef::SPRITE) ok = clip->setSpriteKey (id, tick, clip->bankOf (* s), s->number, s->mirror, false, why);
		else ok = clip->setSpriteKey (id, tick, "", 0, false, ! s || s->kind == SpriteRef::DEFAULT, why);
	} else {
		// What the part has there now, as the clip poses it.
		int p = posedRig ? posedRig->findPart (t->part) : -1;
		float value = 0;
		if (p >= 0 && p < (int) keyed.parts.size ()) {
			const PosedPart & part = keyed.parts[p];
			switch (t->channel) {
				case CHANNEL_X: value = part.local.x; break;
				case CHANNEL_Y: value = part.local.y; break;
				case CHANNEL_ROTATION: value = part.local.rotation; break;
				case CHANNEL_SCALE:
				case CHANNEL_SCALE_X: value = part.local.scaleX; break;
				case CHANNEL_SCALE_Y: value = part.local.scaleY; break;
				case CHANNEL_VISIBLE: value = part.local.visible ? 1 : 0; break;
				case CHANNEL_DEPTH: value = (float) part.depth; break;
				case CHANNEL_FLIP: value = part.local.flip ? 1 : 0; break;
				default: break;
			}
		} else if (isScale (t->channel) || t->channel == CHANNEL_VISIBLE) {
			value = 1;
		}
		ok = clip->autoKey (t->part, t->channel, tick, value, why);
	}
	if (! ok) say (why);
	else selectOnly (KeyRef (id, tick));
}

void ClipView::askForFile (Pending::What what) {
	if (pending->waiting) return;
	std::string base = baseFolder ();
	if (base.empty ()) {
		say ("Save the clip, or open its project, first: paths are from that folder.");
		return;
	}
	pending->waiting = true;
	pending->answered = false;
	pending->what = what;
	std::shared_ptr<Pending> p = pending;
	std::vector<FileFilter> filters;
	FileFilter sounds = { "Sounds", "wav;ogg;xm;mod;s3m;it" };
	FileFilter banks = { "Sprite banks", "duc" };
	filters.push_back (what == Pending::SOUND ? sounds : banks);
	actions.platform ().openFileDialog (what == Pending::SOUND ? "Choose the sound of the event" : "Choose the sprite bank of the key",
		filters, base, false, [p] (const std::vector<std::string> & files) {
			p->files = files;
			p->answered = true;
		});
}

void ClipView::handlePending () {
	if (! pending->waiting || ! pending->answered) return;
	pending->waiting = false;
	if (pending->files.empty ()) return;
	std::string path = relativePathFrom (baseFolder (), pending->files[0]);
	std::string why;
	if (pending->what == Pending::SOUND) {
		EventId one = soleEvent ();
		if (one && clip->event (one) && clip->event (one)->what.kind == FrameEvent::SOUND) {
			if (! clip->setEvent (one, clip->event (one)->time, FrameEvent::SOUND, path, why)) say (why);
		} else {
			EventId added;
			if (clip->addEvent (playhead (), FrameEvent::SOUND, path, &added, why)) {
				selectedEvents.clear ();
				selectedEvents.insert (added);
				selectedKeys.clear ();
			} else {
				say (why);
			}
		}
	} else if (pending->what == Pending::PART_BANK) {
		int p = havePart && posedRig ? partWithId (* posedRig, selectedPart) : -1;
		if (p >= 0) setSprite (posedRig->parts[p].name, SpriteRef::SPRITE, path, 0, false);
	} else if (selectedKeys.size () == 1) {
		KeyRef k = * selectedKeys.begin ();
		const Key * key = clip->key (k);
		int number = key && key->sprite.kind == SpriteRef::SPRITE ? key->sprite.number : 0;
		if (! clip->setSpriteKey (k.track, k.time, path, number, false, false, why)) say (why);
	}
}

bool ClipView::takeFile (const std::string & path) {
	if (extensionOf (path) != "duc" || ! havePart) return false;
	int p = posedRig ? partWithId (* posedRig, selectedPart) : -1;
	if (p < 0) return false;
	std::string part = posedRig->parts[p].name;
	std::string base = baseFolder ();
	if (base.empty ()) {
		say ("Save the clip, or open its project, first: paths are from that folder.");
		return true;
	}
	setSprite (part, SpriteRef::SPRITE, relativePathFrom (base, path), 0, false);
	const Track * t = clip->track (part, CHANNEL_SPRITE);
	if (t && clip->key (KeyRef (t->id, playhead ()))) selectOnly (KeyRef (t->id, playhead ()));
	return true;
}

ClipView::PendingValue * ClipView::pendingValue (const std::string & part, Channel channel) {
	for (size_t i = 0; i < pendingValues.size (); i ++) {
		if (pendingValues[i].part == part && pendingValues[i].channel == channel) return &pendingValues[i];
	}
	return NULL;
}

void ClipView::forgetStalePending () {
	if (pendingValues.empty ()) return;
	if (player.playing () || playhead () != pendingTick || ! havePart || selectedPart != pendingPart
		|| clip->commands ().lastSerial () != pendingBasis) {
		pendingValues.clear ();
	}
}

ClipDefinitionRef ClipView::withPending (const ClipDefinitionRef & c) {
	if (pendingValues.empty () || ! c) return c;
	std::shared_ptr<ClipDefinition> copy = std::make_shared<ClipDefinition> (* c);
	for (size_t i = 0; i < pendingValues.size (); i ++) {
		const PendingValue & p = pendingValues[i];
		Key k;
		k.time = pendingTick;
		k.easing = EASING_LINEAR;
		// As a key there would ease (ClipDocument::autoKey).
		if (const Track * t = copy->track (p.part, p.channel)) {
			for (size_t j = 0; j < t->keys.size () && t->keys[j].time <= pendingTick; j ++) k.easing = t->keys[j].easing;
		}
		if (isDiscrete (p.channel)) k.easing = EASING_STEP;
		if (p.channel == CHANNEL_SPRITE) {
			if (p.kind == SpriteRef::SPRITE) k.sprite = SpriteRef::sprite (preview.bank (baseFolder (), p.bank), -1, p.number, p.mirror);
			else if (p.kind == SpriteRef::NONE) k.sprite = SpriteRef::none ();
		} else {
			k.value = p.value;
		}
		std::string why;
		setKey (* copy, p.part, p.channel, k, why);
	}
	return copy;
}

void ClipView::setValue (const std::string & part, Channel channel, float value, int mergeKey) {
	std::string why;
	if (actions.autoKey ()) {
		if (! clip->autoKey (part, channel, playhead (), value, why, mergeKey)) say (why);
		return;
	}
	forgetStalePending ();
	int p = posedRig ? posedRig->findPart (part) : -1;
	if (p < 0) return;
	pendingPart = posedRig->parts[p].id;
	pendingTick = playhead ();
	pendingBasis = clip->commands ().lastSerial ();
	PendingValue * now = pendingValue (part, channel);
	if (! now) {
		PendingValue v;
		v.part = part;
		v.channel = channel;
		v.kind = SpriteRef::DEFAULT;
		v.number = 0;
		v.mirror = false;
		pendingValues.push_back (v);
		now = &pendingValues.back ();
	}
	now->value = isDiscrete (channel) && channel != CHANNEL_DEPTH ? (value != 0 ? 1.0f : 0.0f) : value;
}

void ClipView::setSprite (const std::string & part, SpriteRef::Kind kind, const std::string & bank, int number, bool mirror) {
	std::string why;
	if (actions.autoKey ()) {
		const Track * t = clip->track (part, CHANNEL_SPRITE);
		TrackId id = t ? t->id : 0;
		if (! t && ! clip->addTrack (part, CHANNEL_SPRITE, &id, why)) {
			say (why);
			return;
		}
		if (! clip->setSpriteKey (id, playhead (), kind == SpriteRef::SPRITE ? bank : std::string (), number, mirror, kind == SpriteRef::DEFAULT, why)) say (why);
		return;
	}
	setValue (part, CHANNEL_SPRITE, 0, 0);
	PendingValue * v = pendingValue (part, CHANNEL_SPRITE);
	if (! v) return;
	v->kind = kind;
	v->bank = bank;
	v->number = number;
	v->mirror = mirror;
}

bool ClipView::keyedAt (const std::string & part, Channel channel) const {
	const Track * t = clip->track (part, channel);
	return t && clip->key (KeyRef (t->id, playhead ()));
}

float ClipView::valueOf (int part, Channel channel) const {
	if (part < 0 || part >= (int) keyed.parts.size ()) return 0;
	const PosedPart & p = keyed.parts[part];
	switch (channel) {
		case CHANNEL_X: return p.local.x;
		case CHANNEL_Y: return p.local.y;
		case CHANNEL_ROTATION: return p.local.rotation;
		case CHANNEL_SCALE:
		case CHANNEL_SCALE_X: return p.local.scaleX;
		case CHANNEL_SCALE_Y: return p.local.scaleY;
		case CHANNEL_VISIBLE: return p.local.visible ? 1.0f : 0.0f;
		case CHANNEL_DEPTH: return (float) p.depth;
		case CHANNEL_FLIP: return p.local.flip ? 1.0f : 0.0f;
		default: return 0;
	}
}

void ClipView::keyChannels (const std::string & part, const std::vector<Channel> & channels) {
	int p = posedRig ? posedRig->findPart (part) : -1;
	if (p < 0) return;
	int tick = playhead ();
	std::string why;
	// Their values first: keying one changes the pose.
	std::vector<PendingValue> values;
	for (size_t i = 0; i < channels.size (); i ++) {
		PendingValue * pending = pendingValue (part, channels[i]);
		PendingValue v;
		if (pending) {
			v = * pending;
		} else {
			v.part = part;
			v.channel = channels[i];
			v.value = valueOf (p, channels[i]);
			v.kind = SpriteRef::DEFAULT;
			v.number = 0;
			v.mirror = false;
			if (channels[i] == CHANNEL_SPRITE) {
				const SpriteRef * s = sampleSprite (clip->clip (), part, ticksToTime (tick));
				if (s) {
					v.kind = s->kind;
					v.bank = clip->bankOf (* s);
					v.number = s->number;
					v.mirror = s->mirror;
				}
			}
		}
		values.push_back (v);
	}
	for (size_t i = 0; i < values.size (); i ++) {
		const PendingValue & v = values[i];
		bool ok = true;
		if (v.channel == CHANNEL_SPRITE) {
			const Track * t = clip->track (part, CHANNEL_SPRITE);
			TrackId id = t ? t->id : 0;
			ok = t || clip->addTrack (part, CHANNEL_SPRITE, &id, why);
			if (ok) ok = clip->setSpriteKey (id, tick, v.kind == SpriteRef::SPRITE ? v.bank : std::string (), v.number, v.mirror, v.kind == SpriteRef::DEFAULT, why);
		} else {
			ok = clip->autoKey (part, v.channel, tick, v.value, why, BUTTON_KEY);
		}
		if (! ok) say (why);
	}
	clip->commands ().stopMerging ();
	for (size_t i = pendingValues.size (); i -- > 0; ) {
		for (size_t c = 0; c < channels.size (); c ++) {
			if (pendingValues[i].part == part && pendingValues[i].channel == channels[c]) {
				pendingValues.erase (pendingValues.begin () + i);
				break;
			}
		}
	}
	pendingBasis = clip->commands ().lastSerial ();
}

void ClipView::keyButton (const char * id, const std::string & part, const std::vector<Channel> & channels, float rightX) {
	bool pending = false, all = true;
	for (size_t i = 0; i < channels.size (); i ++) {
		pending = pending || pendingValue (part, channels[i]);
		all = all && keyedAt (part, channels[i]);
	}
	// Yellow: changed, not keyed yet. Green: keyed there.
	static const ImVec4 yellow (1.0f, 0.85f, 0.2f, 1.0f), green (0.35f, 0.86f, 0.43f, 1.0f);
	IconStyle style;
	if (pending) style.colour = &yellow;
	else if (all) {
		style.colour = &green;
		style.enabled = false;
	}
	ImGui::SameLine ();
	// At the end of the row.
	if (rightX > 0) ImGui::SetCursorScreenPos (ImVec2 (rightX, ImGui::GetCursorScreenPos ().y));
	if (iconButton (id, ICON_KEY, pending ? "Key the change at the playhead" : all ? "Keyed at the playhead" : "Key it at the playhead", 0, style)) {
		keyChannels (part, channels);
	}
}

void ClipView::drawToolbar () {
	std::string base = baseFolder ();
	ImGui::AlignTextToFramePadding ();
	ImGui::TextUnformatted ("Rig");
	ImGui::SameLine ();
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 10);
	std::string current = clip->asset ().rigId.isNull () ? std::string ("None") : clip->asset ().rigPath;
	if (ImGui::BeginCombo ("##rig", current.c_str ())) {
		std::vector<std::string> rigs = base.empty () ? std::vector<std::string> () : filesWithExtension (base, "rig");
		if (rigs.empty ()) ImGui::TextDisabled ("No rigs in the folder of the project");
		for (size_t i = 0; i < rigs.size (); i ++) {
			std::string path = relativePathFrom (base, rigs[i]);
			if (ImGui::Selectable (path.c_str (), path == clip->asset ().rigPath)) {
				animformat::Asset a;
				if (loadAsset (rigs[i], a) && a.kind == animformat::Asset::RIG) {
					clip->bindToRig (a.id, path);
					rigFile.clear ();
				} else {
					say ("That isn't a rig that can be read.");
				}
			}
		}
		ImGui::EndCombo ();
	}
	ImGui::SetItemTooltip ("The rig that the clip is made for, and that the preview shows");
	ImGui::SameLine (0, ImGui::GetFontSize ());
	ImGui::AlignTextToFramePadding ();
	ImGui::TextUnformatted ("Preview layered clip");
	ImGui::SetItemTooltip ("A clip to play over this one on a layer, as playClip does in a game: for the preview only, it isn't saved with the clip");
	ImGui::SameLine ();
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 9);
	std::string second = secondFile.empty () ? std::string ("No second clip") : relativePathFrom (base, secondFile);
	if (ImGui::BeginCombo ("##second", second.c_str ())) {
		if (ImGui::Selectable ("No second clip", secondFile.empty ())) {
			secondFile.clear ();
			haveSecond = false;
			player.reset ();
		}
		std::vector<std::string> clips = base.empty () ? std::vector<std::string> () : filesWithExtension (base, "clp");
		for (size_t i = 0; i < clips.size (); i ++) {
			if (clips[i] == clip->path ()) continue;
			std::string path = relativePathFrom (base, clips[i]);
			if (ImGui::Selectable (path.c_str (), clips[i] == secondFile)) {
				secondFile = clips[i];
				haveSecond = false;
				player.reset ();
			}
		}
		ImGui::EndCombo ();
	}
	ImGui::SetItemTooltip ("A clip to play over this one on a layer, as playClip does in a game: for the preview only, it isn't saved with the clip");
	ImGui::SameLine ();
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 6);
	int blend = secondBlend;
	const char * blends[] = { "Override", "Additive" };
	if (ImGui::Combo ("##blend", &blend, blends, 2)) {
		secondBlend = (LayerBlend) blend;
		player.reset ();
	}
	ImGui::SetItemTooltip ("Override: the second clip sets what it keys. Additive: it adds to positions and rotations, and multiplies scales");
	ImGui::SameLine (0, ImGui::GetFontSize ());
	canvas.drawZoomButtons ();
}

void ClipView::drawTransport () {
	int length = clip->clip ().length;
	ImVec2 at = ImGui::GetCursorScreenPos ();
	float width = ImGui::GetContentRegionAvail ().x, side = iconButtonWidth (), gap = ImGui::GetFontSize () * 0.6f;
	// The buttons that play, in the middle of the row.
	float x = at.x + floorf ((width - (side * 3 + gap * 2)) * 0.5f);
	ImGui::SetCursorScreenPos (ImVec2 (x, at.y));
	if (iconButton ("first", ICON_FIRST, "To the first tick")) scrub (0);
	ImGui::SetCursorScreenPos (ImVec2 (x + side + gap, at.y));
	if (iconButton ("play", player.playing () ? ICON_PAUSE : ICON_PLAY, player.playing () ? "Pause" : "Play")) {
		if (player.playing ()) pause ();
		else player.play ();
	}
	ImGui::SetCursorScreenPos (ImVec2 (x + (side + gap) * 2, at.y));
	if (iconButton ("last", ICON_LAST, "To the last tick")) scrub (length);

	// Loop, Auto key and the speed, on the right.
	float speedWidth = ImGui::GetFontSize () * 5, small = ImGui::GetStyle ().ItemSpacing.x;
	float right = at.x + width - (side * 2 + small * 2 + gap + speedWidth);
	ImGui::SetCursorScreenPos (ImVec2 (right, at.y));
	bool loop = clip->clip ().loop;
	IconStyle loopStyle;
	loopStyle.on = loop;
	if (iconButton ("loop", ICON_LOOP, loop ? "Loops: it goes round and round. Click to play it once." : "Plays once. Click to make it loop.", 0, loopStyle)) {
		clip->setLoop (! loop);
	}
	ImGui::SetCursorScreenPos (ImVec2 (right + side + small, at.y));
	bool autoKey = actions.autoKey ();
	IconStyle keyStyle;
	ImVec4 red (1.0f, 0.3f, 0.3f, 1.0f);
	if (autoKey) keyStyle.colour = &red;
	if (iconButton ("autokey", ICON_AUTOKEY, autoKey ? "Auto key is on: changes to parts are keyed at the playhead at once. Click to turn it off."
			: "Auto key is off: changes to parts are a preview until you click their key buttons. Click to key them at once.", 0, keyStyle)) {
		actions.setAutoKey (! autoKey);
	}
	ImGui::SetCursorScreenPos (ImVec2 (right + (side + small) * 2 + gap, at.y));
	ImGui::SetNextItemWidth (speedWidth);
	int speed = player.speed ();
	if (ImGui::SliderInt ("##speed", &speed, 10, 400, "%d%%")) player.setSpeed (speed);
	ImGui::SetItemTooltip ("How fast the preview plays, of the game's speed: the keys stay where they are");
	// The row, as one item.
	ImGui::SetCursorScreenPos (at);
	ImGui::Dummy (ImVec2 (width, ImGui::GetFrameHeight ()));
}

void ClipView::drawSplitter (float total, float least) {
	ImVec2 at = ImGui::GetCursorScreenPos ();
	float width = ImGui::GetContentRegionAvail ().x, thick = ImGui::GetStyle ().ItemSpacing.y + 4;
	ImGui::InvisibleButton ("##split", ImVec2 (width, thick));
	bool hovered = ImGui::IsItemHovered (), active = ImGui::IsItemActive ();
	if (hovered || active) ImGui::SetMouseCursor (ImGuiMouseCursor_ResizeNS);
	ImGui::SetItemTooltip ("Drag to share the height between the picture and the timeline");
	if (active && total > 0) {
		float top = total * split + ImGui::GetIO ().MouseDelta.y;
		if (top < least) top = least;
		if (top > total - least) top = total - least;
		split = top / total;
	}
	if (ImGui::IsItemDeactivated ()) actions.setClipSplit (split);
	ImU32 colour = ImGui::GetColorU32 (active ? ImGuiCol_SeparatorActive : hovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator);
	float y = floorf (at.y + thick * 0.5f);
	ImGui::GetWindowDrawList ()->AddLine (ImVec2 (at.x, y), ImVec2 (at.x + width, y), colour, active || hovered ? 2.0f : 1.0f);
}

void ClipView::selectPart (const std::string & name) {
	int p = posedRig ? posedRig->findPart (name) : -1;
	if (p < 0 || locked (name)) return;
	selectedPart = posedRig->parts[p].id;
	havePart = true;
}

void ClipView::drawPartPanel (float height) {
	ImGui::BeginChild ("parts", ImVec2 (ImGui::GetFontSize () * 12, height), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
	if (posedRig) {
		PartTree tree;
		ClipView * self = this;
		RigDefinitionRef r = posedRig;
		tree.isLocked = [self, r] (PartId id) { int p = partWithId (* r, id); return p >= 0 && self->locked (r->parts[p].name); };
		tree.setLocked = [self, r] (PartId id, bool on) { int p = partWithId (* r, id); if (p >= 0) self->clip->setLocked (r->parts[p].name, on); };
		tree.select = [self, r] (PartId id) { int p = partWithId (* r, id); if (p >= 0) self->selectPart (r->parts[p].name); };
		drawPartTree (* posedRig, havePart, selectedPart, tree);
	}
	ImGui::EndChild ();
}

void ClipView::handleCanvasMouse () {
	float mx = canvas.mouseX (), my = canvas.mouseY ();
	if (canvas.pressed ()) {
		pause ();
		drag = DRAG_NONE;
		float grab = canvas.pixelsOfPoints (GRAB_POINTS);
		int at = havePart && posedRig ? partWithId (* posedRig, selectedPart) : -1;
		if (at >= 0) {
			PartHandles h = partHandles (shown, at, canvas.pixelsOfPoints (RING_POINTS));
			int handles = uniform (posedRig->parts[at].name) ? 4 : 8;
			// The nearest handle within reach: the origin, then the knob,
			// win a tie (a scale handle may lie on the origin).
			float nearest = grab;
			float d = distance (mx, my, h.originX, h.originY);
			if (d < nearest) {
				nearest = d;
				drag = DRAG_MOVE;
			}
			d = distance (mx, my, h.turnX, h.turnY);
			if (d < nearest) {
				nearest = d;
				drag = DRAG_TURN;
			}
			for (int i = 0; i < handles; i ++) {
				d = distance (mx, my, h.scale[i].x, h.scale[i].y);
				if (d < nearest) {
					nearest = d;
					drag = DRAG_SCALE;
					dragHandle = h.scale[i];
				}
			}
		}
		if (drag == DRAG_NONE) {
			ClipView * self = this;
			RigDefinitionRef r = posedRig;
			int hit = partAt (shown, mx, my, [self, r] (int part) { return ! self->locked (r->parts[part].name); });
			if (hit >= 0) {
				selectedPart = posedRig->parts[hit].id;
				havePart = true;
				drag = DRAG_MOVE;
			} else {
				havePart = false;
			}
		}
		at = havePart && posedRig ? partWithId (* posedRig, selectedPart) : -1;
		if (drag != DRAG_NONE && at >= 0 && at < (int) keyed.parts.size ()) {
			int parent = posedRig->parts[at].parent;
			dragMoved = false;
			dragX = mx;
			dragY = my;
			dragLocal = keyed.parts[at].local;
			dragParent = parent >= 0 ? shown.parts[parent].world : Transform2D ();
			dragWorld = shown.parts[at].world;
		} else {
			drag = DRAG_NONE;
		}
	}

	int at = havePart && posedRig ? partWithId (* posedRig, selectedPart) : -1;
	if (drag != DRAG_NONE && canvas.held () && at >= 0 && ! dragMoved && distance (mx, my, dragX, dragY) >= canvas.pixelsOfPoints (2)) dragMoved = true;
	if (drag != DRAG_NONE && dragMoved && canvas.held () && at >= 0) {
		std::string part = posedRig->parts[at].name;
		int tick = playhead ();
		char where[96];
		snprintf (where, sizeof (where), " at tick %d", tick);
		if (drag == DRAG_MOVE) {
			Transform2D up;
			if (linear (dragParent).inverse (up)) {
				float lx, ly;
				up.apply (mx - dragX, my - dragY, lx, ly);
				setValue (part, CHANNEL_X, floorf (dragLocal.x + lx + 0.5f), DRAG_KEY);
				setValue (part, CHANNEL_Y, floorf (dragLocal.y + ly + 0.5f), DRAG_KEY);
			}
			keying = "Keying " + partLabel (part) + " > position" + where;
		} else if (drag == DRAG_TURN) {
			float ox = dragWorld.tx, oy = dragWorld.ty;
			double turned = (atan2 (my - oy, mx - ox) - atan2 (dragY - oy, dragX - ox)) * 180.0 / pi;
			if (dragParent.mirrors ()) turned = - turned;
			setValue (part, CHANNEL_ROTATION, wrapDegrees (floorf (dragLocal.rotation + (float) turned + 0.5f)), DRAG_KEY);
			keying = "Keying " + partLabel (part) + " > rotation" + where;
		} else if (drag == DRAG_SCALE) {
			float sx, sy;
			bool together = uniform (part);
			scaleFromHandle (dragParent, dragLocal, dragHandle, together, dragHandle.x + mx - dragX, dragHandle.y + my - dragY, sx, sy);
			// A clip that scales the part with SCALE keeps doing so while
			// its axes scale together; otherwise each axis has its track.
			if (together && clip->track (part, CHANNEL_SCALE) && ! clip->track (part, CHANNEL_SCALE_X) && ! clip->track (part, CHANNEL_SCALE_Y)) {
				setValue (part, CHANNEL_SCALE, sx, DRAG_KEY);
			} else {
				if (dragHandle.axes & ScaleHandle::X) setValue (part, CHANNEL_SCALE_X, sx, DRAG_KEY);
				if (dragHandle.axes & ScaleHandle::Y) setValue (part, CHANNEL_SCALE_Y, sy, DRAG_KEY);
			}
			keying = "Keying " + partLabel (part) + " > scale" + where;
		}
		if (! actions.autoKey ()) keying = "Not keyed: click the key icons, or turn Auto key on";
	}
	if (drag != DRAG_NONE && canvas.released ()) {
		drag = DRAG_NONE;
		keying.clear ();
		clip->commands ().stopMerging ();
	}
}

void ClipView::drawCanvas (float height) {
	ImVec2 size = ImGui::GetContentRegionAvail ();
	size.y = height - ImGui::GetTextLineHeightWithSpacing ();
	float x1, y1, x2, y2;
	if (! poseBounds (shown, x1, y1, x2, y2)) {
		x1 = y1 = -100;
		x2 = y2 = 100;
	}
	float margin = ((x2 - x1) > (y2 - y1) ? (x2 - x1) : (y2 - y1)) * 0.1f + 10;
	canvas.setPictureBounds ((int) floorf (x1 - margin), (int) floorf (y1 - margin),
		(int) ceilf (x2 - x1 + margin * 2), (int) ceilf (y2 - y1 + margin * 2));
	const Pose & p = shown;
	const RigDefinition & r = * posedRig;
	int at = havePart ? partWithId (r, selectedPart) : -1;
	float pixel = canvas.pixelsOfPoints (1);
	bool sides = at >= 0 && ! uniform (r.parts[at].name);
	canvas.draw (size, [&p, &r, at, pixel, sides] (Draw2D & draw) {
		draw.clear ();
		drawPose (draw, p);
		drawPoseOverlay (draw, r, p, at, pixel, sides);
		draw.finish ();
	});
	// With Auto key, a drag on the canvas keys: the frame says so.
	bool autoKey = actions.autoKey ();
	ImGui::GetWindowDrawList ()->AddRect (ImGui::GetItemRectMin (), ImGui::GetItemRectMax (),
		autoKey ? (drag != DRAG_NONE ? KEYING : IM_COL32 (255, 80, 80, 90)) : ImGui::GetColorU32 (ImGuiCol_Border), 0.0f, autoKey && drag != DRAG_NONE ? 2.0f : 1.0f);
	handleCanvasMouse ();

	char tick[48];
	snprintf (tick, sizeof (tick), "Tick %d of %d", playhead (), clip->clip ().length);
	ImGui::TextDisabled ("%s    %s", tick, canvas.status ().c_str ());
	if (! keying.empty ()) {
		ImGui::SameLine (0, ImGui::GetFontSize () * 2);
		ImGui::TextColored (actions.autoKey () ? ImVec4 (1.0f, 0.35f, 0.35f, 1.0f) : ImVec4 (1.0f, 0.6f, 0.2f, 1.0f), "%s", keying.c_str ());
	} else if (! haveRig) {
		ImGui::SameLine (0, ImGui::GetFontSize () * 2);
		ImGui::TextColored (ImVec4 (1.0f, 0.8f, 0.3f, 1.0f), "%s", clip->asset ().rigId.isNull ()
			? "Choose the rig that the clip is made for." : "The rig of the clip isn't there: choose it again.");
	} else if (! notice.empty () && ImGui::GetTime () < noticeUntil) {
		ImGui::SameLine (0, ImGui::GetFontSize () * 2);
		ImGui::TextColored (ImVec4 (1.0f, 0.8f, 0.3f, 1.0f), "%s", notice.c_str ());
	}
}

void ClipView::drawTimeline () {
	ImGui::BeginChild ("timeline", ImVec2 (0, 0), ImGuiChildFlags_Borders);
	ImDrawList * list = ImGui::GetWindowDrawList ();
	ImGuiIO & io = ImGui::GetIO ();
	ImVec2 origin = ImGui::GetCursorScreenPos ();
	// A row's height, with room between rows; what is in it, as high as a
	// field, is centred in it.
	float h = ImGui::GetFrameHeightWithSpacing (), fh = ImGui::GetFrameHeight (), pad = (h - fh) * 0.5f;
	// Wide enough for the parts' names, and for the buttons of the events.
	const ImGuiStyle & style = ImGui::GetStyle ();
	float buttons = ImGui::CalcTextSize ("Events").x + ImGui::CalcTextSize ("+ Sound").x + ImGui::CalcTextSize ("+ Function").x
		+ style.FramePadding.x * 4 + style.ItemSpacing.x * 2 + 8;
	float labelWidth = std::max (ImGui::GetFontSize () * 14, buttons);
	float width = ImGui::GetContentRegionAvail ().x;
	int length = clip->clip ().length;
	laneLeft = origin.x + labelWidth + 8;
	float laneRight = origin.x + width - 12;
	tickWidth = (laneRight - laneLeft) / (float) (length > 0 ? length : 1);
	float rowsTop = origin.y + h * 2;
	float bottom = rowsTop + h * rows.size ();
	ImU32 text = ImGui::GetColorU32 (ImGuiCol_Text), faint = ImGui::GetColorU32 (ImGuiCol_TextDisabled);

	// The left column: the options, "Events", and the rows.
	ImGui::SetCursorScreenPos (ImVec2 (origin.x, origin.y));
	ImGui::Checkbox ("Only animated parts", &onlyAnimated);
	ImGui::SetItemTooltip ("Hide the parts that have no tracks, keeping the parts they hang from");
	ImGui::SetCursorScreenPos (ImVec2 (origin.x, origin.y + h));
	ImGui::AlignTextToFramePadding ();
	ImGui::TextUnformatted ("Events");
	ImGui::SameLine ();
	if (ImGui::Button ("+ Sound")) askForFile (Pending::SOUND);
	ImGui::SameLine ();
	if (ImGui::Button ("+ Function")) {
		functionName.clear ();
		functionDialog = true;
	}
	// The columns of the lock and the plus or minus, at the end of the rows.
	float lockX = origin.x + labelWidth - fh * 2 - 4, signX = origin.x + labelWidth - fh;
	int at = havePart && posedRig ? partWithId (* posedRig, selectedPart) : -1;
	std::string selectedName = at >= 0 ? posedRig->parts[at].name : std::string ();
	// Names centred in their fields' height, level with the arrows, as in the tree.
	ImGui::PushStyleVar (ImGuiStyleVar_SelectableTextAlign, ImVec2 (0.0f, 0.5f));
	for (size_t r = 0; r < rows.size (); r ++) {
		const Row & row = rows[r];
		float y = rowsTop + h * r + pad;
		float x = origin.x + row.depth * style.IndentSpacing;
		ImGui::PushID ((int) r);
		if (row.isTrack) {
			// Its name: the current track, of the selected part.
			bool current = row.track == currentTrack;
			ImGui::SetCursorScreenPos (ImVec2 (x, y));
			std::string label = std::string (channelLabel (row.channel));
			if (ImGui::Selectable (label.c_str (), current, 0, ImVec2 (signX - x - 4, fh))) {
				currentTrack = row.track;
				selectPart (row.part);
			}
			ImGui::SetCursorScreenPos (ImVec2 (signX, y));
			if (iconButton ("remove", ICON_MINUS, "Remove the track, and its keys", fh)) {
				clip->removeTrack (row.track);
				selectedKeys.clear ();
			}
		} else {
			bool open = ! collapsed.count (row.part);
			bool isLocked = locked (row.part);
			// The fold, then the name, which selects the part.
			// The fold's arrow, as the rig's tree draws it.
			ImGui::SetCursorScreenPos (ImVec2 (x, y));
			if (foldArrow ("fold", open)) {
				if (open) collapsed.insert (row.part);
				else collapsed.erase (row.part);
			}
			ImGui::SetItemTooltip (open ? "Fold its tracks away" : "Show its tracks");
			float nameX = x + foldWidth ();
			ImGui::SetCursorScreenPos (ImVec2 (nameX, y));
			std::string label = partLabel (row.part);
			if (row.missing) label += "  (not in the rig)";
			if (row.missing) ImGui::PushStyleColor (ImGuiCol_Text, ImVec4 (1.0f, 0.6f, 0.3f, 1.0f));
			else if (isLocked) ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
			if (ImGui::Selectable ((label + "##name").c_str (), ! row.missing && row.part == selectedName && at >= 0, 0,
					ImVec2 (lockX - nameX - 4, fh))) {
				selectPart (row.part);
			}
			if (row.missing || isLocked) ImGui::PopStyleColor ();
			ImGui::SetCursorScreenPos (ImVec2 (lockX, y));
			if (iconButton ("lock", isLocked ? ICON_LOCKED : ICON_UNLOCKED,
					isLocked ? "Locked: it can't be selected or keyed on the canvas, nor its keys moved. Click to unlock it."
						: "Lock it, so that it can't be keyed, nor its keys moved, by accident", fh)) {
				clip->setLocked (row.part, ! isLocked);
			}
			ImGui::SetCursorScreenPos (ImVec2 (signX, y));
			if (iconButton ("add", ICON_PLUS, "Add a track to the part", fh)) ImGui::OpenPopup ("add");
			if (ImGui::BeginPopup ("add")) {
				const char * kinds[] = { "Position", "Rotation", "Scale", "Sprite", "Visible", "Depth", "Flip" };
				const Channel first[] = { CHANNEL_X, CHANNEL_ROTATION, CHANNEL_SCALE_X, CHANNEL_SPRITE, CHANNEL_VISIBLE, CHANNEL_DEPTH, CHANNEL_FLIP };
				for (int k = 0; k < 7; k ++) {
					bool has = clip->track (row.part, first[k]) != NULL;
					if (ImGui::MenuItem (kinds[k], NULL, false, ! has)) {
						std::string why;
						TrackId added;
						if (! clip->addTrack (row.part, first[k], &added, why)) say (why);
						else currentTrack = added;
						if (first[k] == CHANNEL_X && ! clip->track (row.part, CHANNEL_Y)) clip->addTrack (row.part, CHANNEL_Y, NULL, why);
						if (first[k] == CHANNEL_SCALE_X && ! clip->track (row.part, CHANNEL_SCALE_Y)) clip->addTrack (row.part, CHANNEL_SCALE_Y, NULL, why);
						collapsed.erase (row.part);
					}
				}
				ImGui::EndPopup ();
			}
		}
		ImGui::PopID ();
	}
	ImGui::PopStyleVar ();

	// The lanes, and the mouse on them.
	ImGui::SetCursorScreenPos (ImVec2 (laneLeft - 8, origin.y));
	ImGui::InvisibleButton ("##lanes", ImVec2 (laneRight - laneLeft + 20, (bottom > origin.y + h * 2 ? bottom : origin.y + h * 2) - origin.y));
	bool hovered = ImGui::IsItemHovered ();
	float mx = io.MousePos.x, my = io.MousePos.y;
	auto xOf = [this] (double t) { return laneLeft + (float) t * tickWidth; };
	auto tickAt = [this, length] (float x) {
		int t = (int) floorf ((x - laneLeft) / tickWidth + 0.5f);
		return t < 0 ? 0 : t > length ? length : t;
	};
	// The key under the mouse, if any.
	KeyRef hitKey;
	bool hit = false;
	for (size_t r = 0; r < rows.size () && ! hit; r ++) {
		if (! rows[r].isTrack) continue;
		float cy = rowsTop + h * r + h * 0.5f;
		if (fabsf (my - cy) > h * 0.5f) continue;
		const Track * t = clip->track (rows[r].track);
		if (t && locked (t->part)) continue;
		for (size_t k = 0; t && k < t->keys.size (); k ++) {
			if (fabsf (mx - xOf (t->keys[k].time)) <= 5) {
				hitKey = KeyRef (t->id, t->keys[k].time);
				hit = true;
				break;
			}
		}
	}
	EventId hitEvent = 0;
	if (fabsf (my - (origin.y + h * 1.5f)) <= h * 0.5f) {
		for (size_t e = 0; e < clip->clip ().events.size (); e ++) {
			if (fabsf (mx - xOf (clip->clip ().events[e].time)) <= 6) hitEvent = clip->clip ().events[e].id;
		}
	}

	if (ImGui::IsItemActivated ()) {
		dragStartX = mx;
		dragStartY = my;
		dragTicks = 0;
		if (my < origin.y + h) {
			if (fabsf (mx - xOf (length)) <= 6) {
				timelineDrag = TL_LENGTH;
				dragTicks = length;
			} else {
				timelineDrag = TL_SCRUB;
				scrub (tickAt (mx));
			}
		} else if (my < rowsTop) {
			if (hitEvent && io.KeyShift) {
				// Shift adds it to the selection, or takes it out.
				if (selectedEvents.count (hitEvent)) selectedEvents.erase (hitEvent);
				else selectedEvents.insert (hitEvent);
				timelineDrag = TL_NONE;
			} else if (hitEvent) {
				if (! selectedEvents.count (hitEvent)) {
					selectedEvents.clear ();
					selectedEvents.insert (hitEvent);
					selectedKeys.clear ();
				}
				draggedEvent = hitEvent;
				timelineDrag = TL_EVENT;
				dragEventTime = clip->event (hitEvent)->time;
			} else {
				if (! io.KeyShift) {
					selectedEvents.clear ();
					selectedKeys.clear ();
				}
				timelineDrag = TL_BOX;
			}
		} else if (hit) {
			if (io.KeyShift) {
				if (selectedKeys.count (hitKey)) selectedKeys.erase (hitKey);
				else selectedKeys.insert (hitKey);
				currentTrack = hitKey.track;
			} else if (! selectedKeys.count (hitKey)) {
				selectOnly (hitKey);
			}
			if (const Track * t = clip->track (hitKey.track)) selectPart (t->part);
			timelineDrag = selectedKeys.count (hitKey) ? TL_KEYS : TL_NONE;
			dragCopy = io.KeyAlt;
		} else {
			if (! io.KeyShift) {
				selectedKeys.clear ();
				selectedEvents.clear ();
			}
			timelineDrag = TL_BOX;
		}
	}
	if (ImGui::IsItemActive ()) {
		if (timelineDrag == TL_SCRUB) scrub (tickAt (mx));
		else if (timelineDrag == TL_LENGTH) {
			int t = (int) floorf ((mx - laneLeft) / tickWidth + 0.5f);
			dragTicks = t < 0 ? 0 : t > 100000 ? 100000 : t;
		} else if (timelineDrag == TL_KEYS) {
			dragTicks = (int) floorf ((mx - dragStartX) / tickWidth + 0.5f);
		} else if (timelineDrag == TL_EVENT) {
			int t = dragEventTime + (int) floorf ((mx - dragStartX) / tickWidth + 0.5f);
			t = t < 0 ? 0 : t > length ? length : t;
			const ClipEvent * e = clip->event (draggedEvent);
			std::string why;
			if (e && e->time != t) clip->setEvent (draggedEvent, t, e->what.kind, clip->eventName (draggedEvent), why, EVENT_KEY);
		}
	}
	if (ImGui::IsItemDeactivated ()) {
		std::string why;
		if (timelineDrag == TL_LENGTH) {
			requestLength (dragTicks);
		} else if (timelineDrag == TL_KEYS && dragTicks != 0) {
			std::vector<KeyRef> keys (selectedKeys.begin (), selectedKeys.end ());
			if (clip->moveKeys (keys, dragTicks, dragCopy, why)) {
				std::set<KeyRef> moved;
				for (size_t i = 0; i < keys.size (); i ++) moved.insert (KeyRef (keys[i].track, keys[i].time + dragTicks));
				selectedKeys = moved;
			} else {
				say (why);
			}
		} else if (timelineDrag == TL_BOX) {
			float bx1 = std::min (dragStartX, mx), bx2 = std::max (dragStartX, mx);
			float by1 = std::min (dragStartY, my), by2 = std::max (dragStartY, my);
			// The events, if the box takes in their lane.
			float ey = origin.y + h * 1.5f;
			if (ey >= by1 && ey <= by2) {
				for (size_t e = 0; e < clip->clip ().events.size (); e ++) {
					float ex = xOf (clip->clip ().events[e].time);
					if (ex >= bx1 && ex <= bx2) selectedEvents.insert (clip->clip ().events[e].id);
				}
			}
			for (size_t r = 0; r < rows.size (); r ++) {
				if (! rows[r].isTrack) continue;
				float cy = rowsTop + h * r + h * 0.5f;
				if (cy < by1 || cy > by2) continue;
				const Track * t = clip->track (rows[r].track);
				if (t && locked (t->part)) continue;
				for (size_t k = 0; t && k < t->keys.size (); k ++) {
					float kx = xOf (t->keys[k].time);
					if (kx >= bx1 && kx <= bx2) selectedKeys.insert (KeyRef (t->id, t->keys[k].time));
				}
			}
		} else if (timelineDrag == TL_EVENT) {
			clip->commands ().stopMerging ();
		}
		timelineDrag = TL_NONE;
	}

	// The ruler: ticks, numbers a readable distance apart, and the length.
	list->AddRectFilled (ImVec2 (laneLeft - 8, origin.y), ImVec2 (laneRight + 12, origin.y + h), ImGui::GetColorU32 (ImGuiCol_FrameBg));
	int every = 1;
	const int steps[] = { 1, 2, 4, 8, 12, 24, 48, 96, 192, 480, 960 };
	for (size_t i = 0; i < sizeof (steps) / sizeof (steps[0]); i ++) {
		every = steps[i];
		if (every * tickWidth >= ImGui::GetFontSize () * 3) break;
	}
	for (int t = 0; t <= length; t += every) {
		float x = xOf (t);
		list->AddLine (ImVec2 (x, origin.y + h * 0.6f), ImVec2 (x, origin.y + h), faint);
		list->AddLine (ImVec2 (x, rowsTop), ImVec2 (x, bottom), IM_COL32 (255, 255, 255, 18));
		char number[16];
		snprintf (number, sizeof (number), "%d", t);
		list->AddText (ImVec2 (x + 2, origin.y), faint, number);
	}
	int shownLength = timelineDrag == TL_LENGTH ? dragTicks : length;
	float lx = timelineDrag == TL_LENGTH ? laneLeft + dragTicks * tickWidth : xOf (length);
	list->AddTriangleFilled (ImVec2 (lx - 6, origin.y), ImVec2 (lx + 6, origin.y), ImVec2 (lx, origin.y + h * 0.6f), text);
	if (timelineDrag == TL_LENGTH) {
		char tip[48];
		snprintf (tip, sizeof (tip), "Length: %d ticks", shownLength);
		ImGui::SetTooltip ("%s", tip);
	} else if (hovered && my < origin.y + h && fabsf (mx - xOf (length)) <= 6) {
		ImGui::SetTooltip ("Drag to change the length of the clip");
	}

	// The events: sounds as round markers, functions as diamonds.
	double now = ImGui::GetTime ();
	for (size_t e = 0; e < clip->clip ().events.size (); e ++) {
		const ClipEvent & ev = clip->clip ().events[e];
		float x = xOf (ev.time), cy = origin.y + h * 1.5f, r = h * 0.3f;
		bool flashing = false;
		for (size_t f = 0; f < flashes.size (); f ++) flashing = flashing || (flashes[f].first == ev.id && flashes[f].second > now);
		// Selected, in the yellow of a selected key.
		ImU32 c = selectedEvents.count (ev.id) ? easingColour (EASING_STEP, true) : ev.what.kind == FrameEvent::SOUND ? SOUND : FUNCTION;
		if (flashing) r *= 1.6f;
		if (ev.what.kind == FrameEvent::SOUND) list->AddCircleFilled (ImVec2 (x, cy), r, c);
		else list->AddQuadFilled (ImVec2 (x, cy - r), ImVec2 (x + r, cy), ImVec2 (x, cy + r), ImVec2 (x - r, cy), c);
		if (hovered && hitEvent == ev.id) {
			std::string name = clip->eventName (ev.id);
			ImGui::SetTooltip ("%s at tick %d: %s", ev.what.kind == FrameEvent::SOUND ? "Sound" : "Function", ev.time, name.c_str ());
		}
	}

	// The keys: diamonds for continuous channels, squares for discrete ones,
	// coloured by their easing towards the next key.
	for (size_t r = 0; r < rows.size (); r ++) {
		float y = rowsTop + h * r;
		if (r % 2) list->AddRectFilled (ImVec2 (laneLeft - 8, y), ImVec2 (laneRight + 12, y + h), IM_COL32 (255, 255, 255, 8));
		if (! rows[r].isTrack) continue;
		const Track * t = clip->track (rows[r].track);
		if (! t) continue;
		// A locked part's keys are dimmed.
		ImU32 dim = locked (t->part) ? 0x50FFFFFF : 0xFFFFFFFF;
		float cy = y + h * 0.5f, s = h * 0.28f;
		for (size_t k = 0; k + 1 < t->keys.size (); k ++) {
			if (isDiscrete (t->channel) || t->keys[k].easing == EASING_STEP) continue;
			list->AddLine (ImVec2 (xOf (t->keys[k].time), cy), ImVec2 (xOf (t->keys[k + 1].time), cy), easingColour (t->keys[k].easing, false) & 0x60FFFFFF, 2);
		}
		for (size_t k = 0; k < t->keys.size (); k ++) {
			bool sel = selectedKeys.count (KeyRef (t->id, t->keys[k].time)) != 0;
			float offset = sel && timelineDrag == TL_KEYS ? dragTicks * tickWidth : 0;
			float x = xOf (t->keys[k].time) + offset;
			ImU32 c = easingColour (t->keys[k].easing, sel) & dim;
			if (isDiscrete (t->channel)) list->AddRectFilled (ImVec2 (x - s, cy - s), ImVec2 (x + s, cy + s), c);
			else list->AddQuadFilled (ImVec2 (x, cy - s * 1.3f), ImVec2 (x + s * 1.3f, cy), ImVec2 (x, cy + s * 1.3f), ImVec2 (x - s * 1.3f, cy), c);
			if (sel && timelineDrag == TL_KEYS && dragCopy && offset != 0) {
				float x0 = xOf (t->keys[k].time);
				list->AddQuad (ImVec2 (x0, cy - s * 1.3f), ImVec2 (x0 + s * 1.3f, cy), ImVec2 (x0, cy + s * 1.3f), ImVec2 (x0 - s * 1.3f, cy), c);
			}
		}
	}
	if (timelineDrag == TL_BOX) {
		list->AddRect (ImVec2 (std::min (dragStartX, mx), std::min (dragStartY, my)), ImVec2 (std::max (dragStartX, mx), std::max (dragStartY, my)),
			easingColour (EASING_STEP, true) & 0xC8FFFFFF);
	}
	// The playhead, in the colour of keying with Auto key: a change keys here.
	float px = xOf (player.ticks ());
	list->AddLine (ImVec2 (px, origin.y), ImVec2 (px, bottom > rowsTop ? bottom : rowsTop), actions.autoKey () ? KEYING : text, 2);

	ImGui::SetCursorScreenPos (origin);
	ImGui::Dummy (ImVec2 (width, (bottom > rowsTop ? bottom : rowsTop) - origin.y + h * 0.5f));
	ImGui::EndChild ();
}

void ClipView::drawLengthDialog () {
	if (lengthDialog) {
		ImGui::OpenPopup ("Shorten the Clip?");
		lengthDialog = false;
	}
	if (ImGui::BeginPopupModal ("Shorten the Clip?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
		int past = clip->itemsPast (pendingLength);
		ImGui::Text ("%d key%s and event%s lie past tick %d, the new end.", past, past == 1 ? "" : "s", past == 1 ? "" : "s", pendingLength);
		ImGui::TextUnformatted ("A clip can't keep keys past its end: shortening it removes them.");
		if (ImGui::Button ("Remove Them")) {
			std::string why;
			if (! clip->setLength (pendingLength, true, why)) say (why);
			selectedKeys.clear ();
			selectedEvents.clear ();
			ImGui::CloseCurrentPopup ();
		}
		ImGui::SameLine ();
		if (ImGui::Button ("Cancel")) ImGui::CloseCurrentPopup ();
		ImGui::EndPopup ();
	}
}

void ClipView::drawFunctionDialog () {
	if (functionDialog) {
		ImGui::OpenPopup ("Add a Function Event");
		functionDialog = false;
	}
	if (ImGui::BeginPopup ("Add a Function Event")) {
		ImGui::Text ("At tick %d, call the function:", playhead ());
		if (ImGui::IsWindowAppearing ()) ImGui::SetKeyboardFocusHere ();
		bool enter = ImGui::InputText ("##function", &functionName, ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SetItemTooltip ("A function of the project's scripts, without parameters");
		if (enter || ImGui::Button ("Add")) {
			std::string why;
			EventId added;
			if (clip->addEvent (playhead (), FrameEvent::FUNCTION, functionName, &added, why)) {
				selectedEvents.clear ();
				selectedEvents.insert (added);
				selectedKeys.clear ();
				ImGui::CloseCurrentPopup ();
			} else {
				say (why);
			}
		}
		ImGui::EndPopup ();
	}
}

void ClipView::handleKeys () {
	if (! ImGui::IsWindowFocused (ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO ().WantTextInput) return;
	if (ImGui::Shortcut (ImGuiKey_Delete) || ImGui::Shortcut (ImGuiKey_Backspace)) removeSelection ();
	if (ImGui::Shortcut (ImGuiKey_K) && currentTrack) addKeyAtPlayhead (currentTrack);
	if (ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiKey_C)) copySelection ();
	if (ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiKey_V)) paste ();
	if (ImGui::Shortcut (ImGuiKey_LeftArrow, ImGuiInputFlags_Repeat)) scrub (playhead () - 1);
	if (ImGui::Shortcut (ImGuiKey_RightArrow, ImGuiInputFlags_Repeat)) scrub (playhead () + 1);
}

void ClipView::draw () {
	handlePending ();
	double now = ImGui::GetTime ();
	if (lastTime >= 0) {
		std::vector<const ClipEvent *> passed = player.advance ((now - lastTime) * 1000.0, actions.tickMilliseconds ());
		for (size_t i = 0; i < passed.size (); i ++) flashes.push_back (std::make_pair (passed[i]->id, now + FLASH_SECONDS));
		playSounds (passed);
	}
	actions.sounds ().update ();
	lastTime = now;
	for (size_t i = flashes.size (); i -- > 0; ) {
		if (flashes[i].second <= now) flashes.erase (flashes.begin () + i);
	}
	// Undo may have taken away what was selected, or a lock.
	for (std::set<KeyRef>::iterator i = selectedKeys.begin (); i != selectedKeys.end (); ) {
		const Track * t = clip->track (i->track);
		if (! clip->key (* i) || ! t || locked (t->part)) selectedKeys.erase (i ++);
		else ++ i;
	}
	for (std::set<EventId>::iterator e = selectedEvents.begin (); e != selectedEvents.end (); ) {
		if (! clip->event (* e)) selectedEvents.erase (e ++);
		else ++ e;
	}
	if (currentTrack && ! clip->track (currentTrack)) currentTrack = 0;
	if (player.ticks () > clip->clip ().length && ! player.playing ()) scrub (clip->clip ().length);
	forgetStalePending ();

	pose ();
	if (havePart && posedRig) {
		int p = partWithId (* posedRig, selectedPart);
		if (p < 0 || locked (posedRig->parts[p].name)) havePart = false;
	}
	buildRows ();
	drawToolbar ();
	handleKeys ();
	// The picture over the timeline, split where the bar between them says.
	float total = ImGui::GetContentRegionAvail ().y, least = ImGui::GetTextLineHeightWithSpacing () * 6;
	if (split <= 0) split = actions.clipSplit ();
	float top = floorf (total * split);
	if (top > total - least) top = total - least;
	if (top < least) top = least;
	drawPartPanel (top);
	ImGui::SameLine ();
	ImGui::BeginGroup ();
	drawCanvas (top);
	ImGui::EndGroup ();
	drawSplitter (total, least);
	drawTransport ();
	drawTimeline ();
	drawLengthDialog ();
	drawFunctionDialog ();
}

void ClipView::drawPartInspector () {
	int p = havePart && posedRig ? partWithId (* posedRig, selectedPart) : -1;
	if (p < 0 || p >= (int) keyed.parts.size ()) return;
	const std::string part = posedRig->parts[p].name;
	int tick = playhead ();
	float keyWidth = iconButtonWidth () + ImGui::GetStyle ().ItemSpacing.x;
	// The fields of what is changed and not keyed are yellow; of what is
	// keyed at the playhead, green.
	static const ImVec4 orange (0.5f, 0.42f, 0.08f, 1.0f), keyedTint (0.13f, 0.36f, 0.17f, 1.0f);
	// A pending value's field is orange.
	auto pendingField = [this, &part] (Channel a, Channel b) {
		if (pendingValue (part, a) || (b != NUM_CHANNELS && pendingValue (part, b))) {
			ImGui::PushStyleColor (ImGuiCol_FrameBg, orange);
			return true;
		}
		if (keyedAt (part, a) && (b == NUM_CHANNELS || keyedAt (part, b))) {
			ImGui::PushStyleColor (ImGuiCol_FrameBg, keyedTint);
			return true;
		}
		return false;
	};
	// Where the key buttons go: at the end of the field column.
	auto right = [] () { return ImGui::GetCursorScreenPos ().x + ImGui::GetContentRegionAvail ().x - iconButtonWidth (); };
	float keyX = 0;
	char title[96];
	snprintf (title, sizeof (title), "%s at tick %d", partLabel (part).c_str (), tick);
	if (! beginProperties (title, 0.28f)) return;
	bool done = false;

	// Position.
	{
		float x = valueOf (p, CHANNEL_X), y = valueOf (p, CHANNEL_Y), nx = x, ny = y;
		propertyRow ("Position", "Where its origin is, from its parent's, in its parent's pixels; +Y is down");
		keyX = right ();
		bool on = pendingField (CHANNEL_X, CHANNEL_Y);
		if (xyFields ("position", &nx, &ny, 1.0f, "%g", 0, 0, &done, keyWidth)) {
			if (nx != x) setValue (part, CHANNEL_X, nx, FIELD_KEY);
			if (ny != y) setValue (part, CHANNEL_Y, ny, FIELD_KEY);
		}
		if (on) ImGui::PopStyleColor ();
		keyButton ("keyposition", part, std::vector<Channel> { CHANNEL_X, CHANNEL_Y }, keyX);
	}

	// Rotation.
	{
		float r = valueOf (p, CHANNEL_ROTATION);
		propertyRow ("Rotation", "In degrees, clockwise, about its pivot");
		keyX = right ();
		bool on = pendingField (CHANNEL_ROTATION, NUM_CHANNELS);
		ImGui::SetNextItemWidth (ImGui::GetContentRegionAvail ().x - keyWidth);
		if (ImGui::DragFloat ("##rotation", &r, 1.0f, -3600, 3600, "%.4g\xC2\xB0")) setValue (part, CHANNEL_ROTATION, r, FIELD_KEY);
		done |= ImGui::IsItemDeactivated ();
		if (on) ImGui::PopStyleColor ();
		keyButton ("keyrotation", part, std::vector<Channel> { CHANNEL_ROTATION }, keyX);
	}

	// Scale: by SCALE where the clip keys it so and its axes scale together.
	{
		bool together = uniform (part);
		bool both = together && clip->track (part, CHANNEL_SCALE) && ! clip->track (part, CHANNEL_SCALE_X) && ! clip->track (part, CHANNEL_SCALE_Y);
		std::vector<Channel> channels = both ? std::vector<Channel> { CHANNEL_SCALE } : std::vector<Channel> { CHANNEL_SCALE_X, CHANNEL_SCALE_Y };
		float sx = valueOf (p, CHANNEL_SCALE_X) * 100.0f, sy = valueOf (p, CHANNEL_SCALE_Y) * 100.0f, nx = sx, ny = sy;
		propertyRow ("Scale", "In percent, along its own axes");
		keyX = right ();
		bool on = pendingField (both ? CHANNEL_SCALE : CHANNEL_SCALE_X, both ? NUM_CHANNELS : CHANNEL_SCALE_Y);
		if (xyFields ("scale", &nx, &ny, 1.0f, "%g%%", 1, 10000, &done, keyWidth + iconButtonWidth () + ImGui::GetStyle ().ItemSpacing.x)) {
			if (together) {
				if (nx != sx) ny = nx;
				else nx = ny;
			}
			float fx = (nx < 1 ? 1 : nx) / 100.0f, fy = (ny < 1 ? 1 : ny) / 100.0f;
			if (both) {
				setValue (part, CHANNEL_SCALE, fx, FIELD_KEY);
			} else {
				if (fx != sx / 100.0f) setValue (part, CHANNEL_SCALE_X, fx, FIELD_KEY);
				if (fy != sy / 100.0f) setValue (part, CHANNEL_SCALE_Y, fy, FIELD_KEY);
			}
		}
		if (on) ImGui::PopStyleColor ();
		if (iconButton ("uniform", together ? ICON_LOCKED : ICON_UNLOCKED,
				together ? "X and Y scale together. Click to scale them apart." : "X and Y scale apart. Click to scale them together.")) {
			clip->setScaling (part, together ? animformat::EditorPart::SCALING_FREE : animformat::EditorPart::SCALING_UNIFORM);
		}
		keyButton ("keyscale", part, channels, keyX);
	}

	// Its sprite: as the clip has it there, or its own (the rig's).
	{
		PendingValue * pending = pendingValue (part, CHANNEL_SPRITE);
		SpriteRef::Kind kind = SpriteRef::DEFAULT;
		std::string bank;
		int number = 0;
		bool mirror = false;
		if (pending) {
			kind = pending->kind;
			bank = pending->bank;
			number = pending->number;
			mirror = pending->mirror;
		} else if (const SpriteRef * s = sampleSprite (clip->clip (), part, ticksToTime (tick))) {
			kind = s->kind;
			bank = clip->bankOf (* s);
			number = s->number;
			mirror = s->mirror;
		}
		propertyRow ("Sprite", "A sprite of a bank, nothing, or its own sprite, which the rig gives it");
		keyX = right ();
		bool tinted = pendingField (CHANNEL_SPRITE, NUM_CHANNELS);
		ImGui::SetNextItemWidth (ImGui::GetContentRegionAvail ().x - keyWidth);
		std::string shown = kind == SpriteRef::NONE ? std::string ("Nothing") : kind == SpriteRef::DEFAULT ? std::string ("Its own (from the rig)") : bank;
		if (ImGui::BeginCombo ("##sprite", shown.c_str ())) {
			if (ImGui::Selectable ("Its own (from the rig)", kind == SpriteRef::DEFAULT)) setSprite (part, SpriteRef::DEFAULT, "", 0, false);
			if (ImGui::Selectable ("Nothing", kind == SpriteRef::NONE)) setSprite (part, SpriteRef::NONE, "", 0, false);
			if (kind == SpriteRef::SPRITE) ImGui::Selectable (bank.c_str (), true);
			if (ImGui::Selectable ("A sprite bank...")) askForFile (Pending::PART_BANK);
			ImGui::EndCombo ();
		}
		if (tinted) ImGui::PopStyleColor ();
		keyButton ("keysprite", part, std::vector<Channel> { CHANNEL_SPRITE }, keyX);
		if (kind == SpriteRef::SPRITE) {
			AnimBankRef b = preview.bank (baseFolder (), bank);
			int n = number;
			propertyRow ("Sprite number", "Its number in the bank");
			keyX = right ();
			if (ImGui::InputInt ("##spritenumber", &n) && n != number) {
				int count = b ? b->animSpriteCount () : 0;
				if (n >= count) n = count - 1;
				if (n < 0) n = 0;
				setSprite (part, SpriteRef::SPRITE, bank, n, mirror);
			}
			bool m = mirror;
			propertyRow ("Mirror image");
			keyX = right ();
			if (ImGui::Checkbox ("##spritemirror", &m)) setSprite (part, SpriteRef::SPRITE, bank, number, m);
		}
		// What it shows there.
		const SpriteRef & shows = keyed.parts[p].sprite;
		if (shows.kind == SpriteRef::SPRITE && shows.bank && shows.number < shows.bank->animSpriteCount ()) {
			const spriteBank * sprites = devkitBank (shows.bank);
			gfx::Texture * texture = gfxPictureTexture (graphics.draw, sprites, shows.number, false);
			const sprite & sp = sprites->sprites[shows.number];
			float side = ImGui::GetFontSize () * 4;
			float zoom = sp.width > sp.height ? side / sp.width : side / (sp.height ? sp.height : 1);
			propertyRow ("");
			if (texture) ImGui::Image (ImTextureRef (ImGui_ImplGfx_TextureID (texture)), ImVec2 (sp.width * zoom, sp.height * zoom));
		}
	}

	// Visible, depth and flip.
	{
		bool visible = valueOf (p, CHANNEL_VISIBLE) != 0;
		propertyRow ("Visible", "Hidden, it hides the parts attached to it too");
		keyX = right ();
		bool on = pendingField (CHANNEL_VISIBLE, NUM_CHANNELS);
		if (ImGui::Checkbox ("##visible", &visible)) setValue (part, CHANNEL_VISIBLE, visible ? 1.0f : 0.0f, 0);
		if (on) ImGui::PopStyleColor ();
		keyButton ("keyvisible", part, std::vector<Channel> { CHANNEL_VISIBLE }, keyX);

		int depth = (int) lrintf (valueOf (p, CHANNEL_DEPTH));
		propertyRow ("Depth", "The order of drawing among the character's parts: higher is in front");
		keyX = right ();
		on = pendingField (CHANNEL_DEPTH, NUM_CHANNELS);
		ImGui::SetNextItemWidth (ImGui::GetContentRegionAvail ().x - keyWidth);
		if (ImGui::DragInt ("##depth", &depth, 0.1f)) setValue (part, CHANNEL_DEPTH, (float) depth, FIELD_KEY);
		done |= ImGui::IsItemDeactivated ();
		if (on) ImGui::PopStyleColor ();
		keyButton ("keydepth", part, std::vector<Channel> { CHANNEL_DEPTH }, keyX);

		bool flip = valueOf (p, CHANNEL_FLIP) != 0;
		propertyRow ("Flip", "Mirrors it, and the parts attached to it");
		keyX = right ();
		on = pendingField (CHANNEL_FLIP, NUM_CHANNELS);
		if (ImGui::Checkbox ("##flip", &flip)) setValue (part, CHANNEL_FLIP, flip ? 1.0f : 0.0f, 0);
		if (on) ImGui::PopStyleColor ();
		keyButton ("keyflip", part, std::vector<Channel> { CHANNEL_FLIP }, keyX);
	}
	if (done) clip->commands ().stopMerging ();
	ImGui::EndTable ();
}

void ClipView::drawKeyInspector (const KeyRef & k) {
	std::string why;
	const Track * t = clip->track (k.track);
	const Key * key = clip->key (k);
	if (! t || ! key) return;
	const Track track = * t;
	const Key now = * key;
	std::string title = "Keyframe " + partLabel (track.part) + " > " + channelLabel (track.channel);
	if (beginProperties (title.c_str ())) {
		propertyRow ("Tick", "Where the key is on the timeline");
		int time = now.time;
		if (ImGui::InputInt ("##tick", &time) && time != now.time) {
			if (clip->moveKeys (std::vector<KeyRef> (1, k), time - now.time, false, why)) selectOnly (KeyRef (k.track, time));
			else say (why);
		}
		float value = now.value;
		bool changed = false, done = false;
		switch (track.channel) {
			case CHANNEL_X:
			case CHANNEL_Y:
				propertyRow ("Position", "Where its origin is, from its parent's, in its parent's pixels; +Y is down");
				axisLabel (track.channel == CHANNEL_X ? "X" : "Y");
				changed = ImGui::DragFloat ("##value", &value, 1.0f, 0, 0, "%g");
				done = ImGui::IsItemDeactivated ();
				break;
			case CHANNEL_ROTATION:
				propertyRow ("Rotation", "In degrees, clockwise");
				changed = ImGui::DragFloat ("##value", &value, 1.0f, -3600, 3600, "%g\xC2\xB0");
				done = ImGui::IsItemDeactivated ();
				break;
			case CHANNEL_SCALE:
			case CHANNEL_SCALE_X:
			case CHANNEL_SCALE_Y: {
				float percent = value * 100.0f;
				propertyRow ("Scale", track.channel == CHANNEL_SCALE ? "In percent, of both of its axes" : "In percent, along its own axis");
				if (track.channel != CHANNEL_SCALE) axisLabel (track.channel == CHANNEL_SCALE_X ? "X" : "Y");
				if (ImGui::DragFloat ("##value", &percent, 1.0f, 1, 10000, "%g%%")) {
					value = (percent < 1 ? 1 : percent) / 100.0f;
					changed = true;
				}
				done = ImGui::IsItemDeactivated ();
				break;
			}
			case CHANNEL_VISIBLE:
			case CHANNEL_FLIP: {
				bool on = value != 0;
				propertyRow (track.channel == CHANNEL_VISIBLE ? "Visible" : "Flipped");
				if (ImGui::Checkbox ("##value", &on)) {
					value = on ? 1.0f : 0.0f;
					changed = true;
				}
				break;
			}
			case CHANNEL_DEPTH: {
				int depth = (int) lrintf (value);
				propertyRow ("Depth", "Higher is in front of the character's other parts");
				if (ImGui::DragInt ("##value", &depth, 0.1f)) {
					value = (float) depth;
					changed = true;
				}
				done = ImGui::IsItemDeactivated ();
				break;
			}
			default:
				break;
		}
		if (changed && ! clip->setKey (k.track, k.time, value, now.easing, why, FIELD_KEY)) say (why);
		if (done) clip->commands ().stopMerging ();
		if (! isDiscrete (track.channel)) {
			propertyRow ("Towards the next key", "How the value moves from this key to the next one; the last key's isn't used");
			int easing = now.easing;
			if (ImGui::Combo ("##easing", &easing, easingLabels, 3)) clip->setEasing (k, (Easing) easing);
		}
		if (track.channel == CHANNEL_SPRITE) {
			std::string bank = clip->bankOf (now.sprite);
			propertyRow ("Shows", "A sprite of a bank, nothing, or the part's own sprite");
			ImGui::TextUnformatted (now.sprite.kind == SpriteRef::NONE ? "Nothing (an empty frame)"
				: now.sprite.kind == SpriteRef::DEFAULT ? "The part's own sprite" : bank.c_str ());
			propertyRow ("");
			if (ImGui::Button ("Choose...")) askForFile (Pending::BANK);
			ImGui::SameLine ();
			if (ImGui::Button ("Nothing")) clip->setSpriteKey (k.track, k.time, "", 0, false, false, why);
			ImGui::SameLine ();
			if (ImGui::Button ("Own")) clip->setSpriteKey (k.track, k.time, "", 0, false, true, why);
			if (now.sprite.kind == SpriteRef::SPRITE) {
				AnimBankRef b = preview.bank (baseFolder (), bank);
				int number = now.sprite.number;
				propertyRow ("Sprite", "Its number in the bank");
				if (ImGui::InputInt ("##sprite", &number)) {
					int count = b ? b->animSpriteCount () : 0;
					if (number >= count) number = count - 1;
					if (number < 0) number = 0;
					clip->setSpriteKey (k.track, k.time, bank, number, now.sprite.mirror, false, why);
				}
				bool mirror = now.sprite.mirror;
				propertyRow ("Mirror image");
				if (ImGui::Checkbox ("##mirror", &mirror)) clip->setSpriteKey (k.track, k.time, bank, now.sprite.number, mirror, false, why);
				if (b && now.sprite.number < b->animSpriteCount ()) {
					const spriteBank * sprites = devkitBank (b);
					gfx::Texture * texture = gfxPictureTexture (graphics.draw, sprites, now.sprite.number, false);
					const sprite & s = sprites->sprites[now.sprite.number];
					float side = ImGui::GetFontSize () * 5;
					float zoom = s.width > s.height ? side / s.width : side / (s.height ? s.height : 1);
					propertyRow ("");
					if (texture) ImGui::Image (ImTextureRef (ImGui_ImplGfx_TextureID (texture)), ImVec2 (s.width * zoom, s.height * zoom));
				}
			}
		}
		ImGui::EndTable ();
	}
	if (ImGui::Button ("Remove Key")) removeSelection ();
}

void ClipView::drawInspector () {
	std::string why;
	if (soleEvent () && selectedKeys.empty () && clip->event (soleEvent ())) {
		const ClipEvent ev = * clip->event (soleEvent ());
		std::string name = clip->eventName (soleEvent ());
		if (beginProperties ("Event")) {
			propertyRow ("Tick");
			int time = ev.time;
			if (ImGui::InputInt ("##tick", &time)) {
				if (! clip->setEvent (ev.id, time, ev.what.kind, name, why, FIELD_KEY)) say (why);
			}
			if (ImGui::IsItemDeactivated ()) clip->commands ().stopMerging ();
			if (ev.what.kind == FrameEvent::SOUND) {
				propertyRow ("Plays the sound", "A sound file of the project");
				ImGui::TextUnformatted (name.c_str ());
				propertyRow ("");
				if (ImGui::Button ("Choose...")) askForFile (Pending::SOUND);
			} else {
				propertyRow ("Calls the function", "A function of the project's scripts, without parameters");
				std::string edited = name;
				if (ImGui::InputText ("##function", &edited, ImGuiInputTextFlags_EnterReturnsTrue) && edited != name) {
					if (! clip->setEvent (ev.id, ev.time, FrameEvent::FUNCTION, edited, why)) say (why);
				}
			}
			ImGui::EndTable ();
		}
		if (ImGui::Button ("Remove Event")) removeSelection ();
		return;
	}

	// The selected part's properties, then the selected keys.
	if (havePart) drawPartInspector ();
	if (selectedKeys.size () == 1) {
		drawKeyInspector (* selectedKeys.begin ());
		return;
	}

	if (selectedKeys.size () > 1 || (! selectedEvents.empty () && (selectedEvents.size () > 1 || ! selectedKeys.empty ()))) {
		char heading[48];
		snprintf (heading, sizeof (heading), "Keyframes (%d)", (int) selectedKeys.size ());
		ImGui::SeparatorText (heading);
		if (! selectedEvents.empty ()) ImGui::Text ("And %d event%s", (int) selectedEvents.size (), selectedEvents.size () == 1 ? "" : "s");
		if (selectedKeys.size () > 1) {
			ImGui::TextUnformatted ("Towards the next key:");
			for (int e = 0; e < 3; e ++) {
				if (ImGui::Button (easingLabels[e])) {
					for (std::set<KeyRef>::const_iterator i = selectedKeys.begin (); i != selectedKeys.end (); ++ i) clip->setEasing (* i, (Easing) e);
				}
			}
		}
		if (ImGui::Button ("Remove Them")) removeSelection ();
		return;
	}

	if (havePart) return;
	if (beginProperties ("Clip")) {
		propertyRow ("Rig", "The rig it is made for: choose it in the toolbar");
		ImGui::TextUnformatted (clip->asset ().rigId.isNull () ? "None" : clip->asset ().rigPath.c_str ());
		propertyRow ("Length", "In ticks; keys and events lie from 0 to the length");
		int length = clip->clip ().length;
		ImGui::InputInt ("##length", &length);
		if (ImGui::IsItemDeactivatedAfterEdit ()) requestLength (length);
		propertyRow ("Tracks");
		ImGui::Text ("%d", (int) clip->clip ().tracks.size ());
		propertyRow ("Events");
		ImGui::Text ("%d", (int) clip->clip ().events.size ());
		ImGui::EndTable ();
	}
	if (haveRig) {
		std::vector<std::string> missing = clip->missingParts (rigAsset.rig);
		if (! missing.empty ()) {
			std::string list;
			for (size_t i = 0; i < missing.size (); i ++) list += (i ? ", " : "") + partLabel (missing[i]);
			ImGui::TextColored (ImVec4 (1.0f, 0.6f, 0.3f, 1.0f), "The rig has no part of tracks of: %s. They do nothing.", list.c_str ());
		}
	}
	ImGui::TextWrapped ("Select a part in the tree, on the canvas or on the timeline to see what of it can be keyed. Click a key to edit it, or the ruler to move the playhead; K keys the selected track there.");
}

} // namespace devkit
