#ifndef SLUDGE_DEVKIT_SOUND_PREVIEW_H
#define SLUDGE_DEVKIT_SOUND_PREVIEW_H

/*
 * The sounds of a clip's events, heard while the animation editor plays it.
 * WAV and OGG files, the kinds the engine plays for events, are decoded
 * whole the first time they're played, and kept until they change on disk.
 * Each sound plays on a stream of its own, so that sounds overlap as in
 * the game. The audio device opens with the first sound; without one, or
 * when disabled (smoke tests), it is silent.
 */

#include <map>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

namespace devkit {

// The samples of a WAV or OGG file, in its own format.
bool decodeSound (const std::string & path, SDL_AudioSpec & spec, std::vector<Uint8> & pcm, std::string & why);

class SoundPreview {
public:
	SoundPreview ();
	~SoundPreview ();

	// Silent, but still counting and logging what it would play.
	void setEnabled (bool on) { enabled = on; }
	// Plays the file from its start; false, with why, when it can't.
	bool play (const std::string & path, std::string & why);
	void stopAll ();
	// Frees the streams that have finished.
	void update ();
	// How many sounds play() was asked for, for tests.
	int played () const { return playCount; }
	// How many sounds are playing.
	int playing () const { return (int) voices.size (); }

private:
	struct Sound {
		SDL_AudioSpec spec;
		std::vector<Uint8> pcm;
		Sint64 modified;
	};
	bool open (std::string & why);

	bool enabled, failed;
	int playCount;
	SDL_AudioDeviceID device;
	std::map<std::string, Sound> sounds;
	std::vector<SDL_AudioStream *> voices;
};

} // namespace devkit

#endif
