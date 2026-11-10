/*
 * The sounds of the animation editor's events: WAV files decoded whole,
 * what can't be played refused with a reason, and a disabled player (as
 * in smoke tests) silent but counting.
 */

#include <stdio.h>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "SoundPreview.h"

using namespace devkit;

static int failures = 0;

#define CHECK(x) do { if (! (x)) { fprintf (stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #x); failures ++; } } while (0)

static void put16 (std::vector<Uint8> & b, int v) { b.push_back ((Uint8) (v & 255)); b.push_back ((Uint8) ((v >> 8) & 255)); }
static void put32 (std::vector<Uint8> & b, int v) { put16 (b, v & 0xffff); put16 (b, (v >> 16) & 0xffff); }
static void putTag (std::vector<Uint8> & b, const char * t) { b.insert (b.end (), t, t + 4); }

static bool writeBytes (const std::string & path, const std::vector<Uint8> & bytes) {
	return SDL_SaveFile (path.c_str (), bytes.data (), bytes.size ());
}

int main (int argc, char ** argv) {
	if (argc != 2) {
		fprintf (stderr, "usage: sound_test <folder for files>\n");
		return 2;
	}
	std::string folder = std::string (argv[1]) + "/";
	SDL_CreateDirectory (folder.c_str ());

	// 100 samples of 16-bit mono at 22050 Hz.
	std::vector<Uint8> wav;
	putTag (wav, "RIFF");
	put32 (wav, 36 + 200);
	putTag (wav, "WAVE");
	putTag (wav, "fmt ");
	put32 (wav, 16);
	put16 (wav, 1);
	put16 (wav, 1);
	put32 (wav, 22050);
	put32 (wav, 22050 * 2);
	put16 (wav, 2);
	put16 (wav, 16);
	putTag (wav, "data");
	put32 (wav, 200);
	for (int i = 0; i < 100; i ++) put16 (wav, (i * 300) & 0xffff);
	CHECK (writeBytes (folder + "beep.WAV", wav));

	SDL_AudioSpec spec;
	std::vector<Uint8> pcm;
	std::string why;
	CHECK (decodeSound (folder + "beep.WAV", spec, pcm, why));
	CHECK (spec.freq == 22050 && spec.channels == 1 && spec.format == SDL_AUDIO_S16LE && pcm.size () == 200);

	CHECK (! decodeSound (folder + "missing.wav", spec, pcm, why) && why.find ("missing.wav") != std::string::npos);
	why.clear ();
	CHECK (writeBytes (folder + "noise.ogg", std::vector<Uint8> (64, 7)));
	CHECK (! decodeSound (folder + "noise.ogg", spec, pcm, why) && ! why.empty ());
	why.clear ();
	CHECK (! decodeSound (folder + "song.mp3", spec, pcm, why) && why.find ("WAV and OGG") != std::string::npos);

	// Disabled, it plays nothing, opens no device, and counts.
	SoundPreview sounds;
	sounds.setEnabled (false);
	CHECK (sounds.play (folder + "beep.WAV", why) && sounds.play (folder + "beep.WAV", why) && sounds.played () == 2);
	sounds.update ();
	sounds.stopAll ();

	// On a device (SDL's dummy one, which plays in real time), a sound plays
	// until its samples are used up, then goes; stopAll silences the rest.
	SDL_SetHint (SDL_HINT_AUDIO_DRIVER, "dummy");
	std::vector<Uint8> tone (wav.begin (), wav.begin () + 44);
	for (int i = 0; i < 4410; i ++) put16 (tone, (i % 50) * 400 - 10000);
	tone[4] = (Uint8) ((36 + 8820) & 255); tone[5] = (Uint8) (((36 + 8820) >> 8) & 255);
	tone[40] = (Uint8) (8820 & 255); tone[41] = (Uint8) ((8820 >> 8) & 255);
	CHECK (writeBytes (folder + "tone.wav", tone));
	{
		SoundPreview heard;
		CHECK (heard.play (folder + "tone.wav", why));
		heard.update ();
		CHECK (heard.playing () == 1);
		for (int i = 0; i < 300 && heard.playing (); i ++) {
			SDL_Delay (10);
			heard.update ();
		}
		CHECK (heard.playing () == 0);
		CHECK (heard.play (folder + "tone.wav", why) && heard.play (folder + "tone.wav", why) && heard.playing () == 2);
		heard.stopAll ();
		CHECK (heard.playing () == 0);
	}

	if (failures) {
		printf ("%d checks failed\n", failures);
		return 1;
	}
	printf ("sound_test: all good\n");
	return 0;
}
