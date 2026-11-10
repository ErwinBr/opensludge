#ifndef SLUDGE_DEVKIT_PREVIEW_PLAYER_H
#define SLUDGE_DEVKIT_PREVIEW_PLAYER_H

/*
 * The playback of the animation editor's preview: the clip being edited on
 * layer 0, and maybe a second clip over it on layer 1, as a character plays
 * them in a game. Time is the animation library's (AnimationTime), and moves
 * by its rules: a loop goes round at its length, any other clip stops
 * there.
 *
 * Playing moves time on by the time that has passed, at the game's speed
 * and the preview's; it gives the events of the edited clip that it passes.
 * Scrubbing puts time somewhere and gives nothing: no events, no state.
 */

#include <vector>

#include "animmodel.h"
#include "animpose.h"

namespace devkit {

class PreviewPlayer {
public:
	PreviewPlayer ();

	// The clips of the layers: 0 the one being edited, 1 a second (NULL for
	// none). The view gives them again each frame as they change; that
	// keeps their time. reset puts time back to the start, for another clip.
	void setClip (int layer, const ClipDefinitionRef & clip, LayerBlend blend);
	void reset ();

	void play ();
	void pause () { isPlaying = false; }
	bool playing () const { return isPlaying; }
	// Percent of the game's speed; it changes how fast time goes, not the
	// times of the keys.
	void setSpeed (int percent) { speedPercent = percent < 1 ? 1 : percent; }
	int speed () const { return speedPercent; }

	// Move on by milliseconds of real time, with ticks of tickMilliseconds:
	// the events of the edited clip that it passed, in order.
	std::vector<const ClipEvent *> advance (double milliseconds, double tickMilliseconds);
	// Time to a tick of the edited clip, and the second with it.
	void scrub (int tick);

	// The time within the clip of a layer (see clipTime), and as ticks. A
	// paused playhead scrubbed to the very end of a loop stays there, rather
	// than at its start, so that the editor shows and keys its last tick.
	AnimationTime time (int layer) const;
	double ticks () const { return (double) time (0) / timeSubTicks; }
	// The layers, to pose: each with its samples at its time.
	void layers (std::vector<LayerSamples> & out, std::vector<TrackSamples> & samples, PoseBinding bindings[2]) const;

private:
	ClipDefinitionRef clips[2];
	LayerBlend blends[2];
	AnimationTime positions[2];
	double spare;			// Milliseconds not yet a sub-tick
	bool isPlaying;
	int speedPercent;
};

} // namespace devkit

#endif
