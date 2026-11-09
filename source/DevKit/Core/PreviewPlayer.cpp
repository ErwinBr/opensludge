#include <math.h>

#include "PreviewPlayer.h"

namespace devkit {

PreviewPlayer::PreviewPlayer () : spare (0), isPlaying (false), speedPercent (100) {
	blends[0] = blends[1] = LAYER_BLEND_OVERRIDE;
	positions[0] = positions[1] = 0;
}

void PreviewPlayer::setClip (int layer, const ClipDefinitionRef & clip, LayerBlend blend) {
	if (layer < 0 || layer > 1) return;
	clips[layer] = clip;
	blends[layer] = blend;
}

void PreviewPlayer::reset () {
	positions[0] = positions[1] = 0;
	spare = 0;
}

void PreviewPlayer::play () {
	// A clip that played to its end starts again.
	if (clips[0] && clipFinished (* clips[0], positions[0])) reset ();
	isPlaying = true;
}

std::vector<const ClipEvent *> PreviewPlayer::advance (double milliseconds, double tickMilliseconds) {
	std::vector<const ClipEvent *> passed;
	if (! isPlaying || tickMilliseconds <= 0 || milliseconds <= 0) return passed;
	spare += milliseconds * speedPercent / 100.0 * timeSubTicks / tickMilliseconds;
	AnimationTime by = (AnimationTime) floor (spare);
	spare -= (double) by;
	if (by <= 0) return passed;
	if (clips[0]) {
		std::vector<CrossedEvent> crossed;
		if (positions[0] == 0) eventsAtStart (* clips[0], crossed);
		eventsCrossed (* clips[0], positions[0], positions[0] + by, crossed);
		for (size_t i = 0; i < crossed.size (); i ++) passed.push_back (crossed[i].event);
		// One that doesn't loop stops at its end.
		if (clipFinished (* clips[0], positions[0] + by)) isPlaying = false;
	}
	positions[0] += by;
	positions[1] += by;
	return passed;
}

void PreviewPlayer::scrub (int tick) {
	positions[0] = positions[1] = ticksToTime (tick < 0 ? 0 : tick);
	spare = 0;
}

AnimationTime PreviewPlayer::time (int layer) const {
	if (layer < 0 || layer > 1 || ! clips[layer]) return 0;
	return clipTime (* clips[layer], positions[layer]);
}

void PreviewPlayer::layers (std::vector<LayerSamples> & out, std::vector<TrackSamples> & samples, PoseBinding bindings[2]) const {
	out.clear ();
	samples.assign (2, TrackSamples ());
	for (int l = 0; l < 2; l ++) {
		if (! clips[l]) continue;
		evaluateClip (* clips[l], time (l), samples[l]);
		LayerSamples layer = { clips[l], & samples[l], blends[l], & bindings[l] };
		out.push_back (layer);
	}
}

} // namespace devkit
