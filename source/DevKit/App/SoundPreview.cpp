#include <string.h>

#include <vorbis/vorbisfile.h>

#include "SoundPreview.h"

namespace devkit {

namespace {

// An OGG file in memory, as vorbisfile reads it.
struct OggSource {
	const Uint8 * data;
	size_t length, pos;
};

size_t oggRead (void * ptr, size_t size, size_t nmemb, void * source) {
	OggSource * s = (OggSource *) source;
	size_t bytes = size * nmemb;
	if (bytes > s->length - s->pos) bytes = s->length - s->pos;
	memcpy (ptr, s->data + s->pos, bytes);
	s->pos += bytes;
	return size ? bytes / size : 0;
}

int oggSeek (void * source, ogg_int64_t offset, int whence) {
	OggSource * s = (OggSource *) source;
	ogg_int64_t to;
	switch (whence) {
		case SEEK_SET: to = offset; break;
		case SEEK_CUR: to = (ogg_int64_t) s->pos + offset; break;
		case SEEK_END: to = (ogg_int64_t) s->length + offset; break;
		default: return -1;
	}
	if (to < 0 || to > (ogg_int64_t) s->length) return -1;
	s->pos = (size_t) to;
	return 0;
}

long oggTell (void * source) {
	return (long) ((OggSource *) source)->pos;
}

bool decodeOgg (const Uint8 * data, size_t length, SDL_AudioSpec & spec, std::vector<Uint8> & pcm) {
	OggSource source = { data, length, 0 };
	OggVorbis_File vf;
	ov_callbacks callbacks = { oggRead, oggSeek, NULL, oggTell };
	if (ov_open_callbacks (&source, &vf, NULL, 0, callbacks) != 0) return false;
	vorbis_info * info = ov_info (&vf, -1);
	if (! info) {
		ov_clear (&vf);
		return false;
	}
	spec.format = SDL_AUDIO_S16;
	spec.channels = info->channels;
	spec.freq = (int) info->rate;
	int bigEndian = SDL_BYTEORDER == SDL_BIG_ENDIAN;
	char buffer[16384];
	pcm.clear ();
	for (;;) {
		int bitstream;
		long got = ov_read (&vf, buffer, sizeof (buffer), bigEndian, 2, 1, &bitstream);
		if (got == OV_HOLE) continue;
		if (got <= 0) break;
		pcm.insert (pcm.end (), buffer, buffer + got);
	}
	ov_clear (&vf);
	return true;
}

bool endsWith (const std::string & s, const char * end) {
	size_t n = strlen (end);
	if (s.size () < n) return false;
	for (size_t i = 0; i < n; i ++) {
		char c = s[s.size () - n + i];
		if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
		if (c != end[i]) return false;
	}
	return true;
}

}

bool decodeSound (const std::string & path, SDL_AudioSpec & spec, std::vector<Uint8> & pcm, std::string & why) {
	if (! endsWith (path, ".wav") && ! endsWith (path, ".ogg")) {
		why = "Only WAV and OGG sounds can be heard here: " + path;
		return false;
	}
	size_t length = 0;
	Uint8 * data = (Uint8 *) SDL_LoadFile (path.c_str (), &length);
	if (! data) {
		why = "Can't read the sound " + path;
		return false;
	}
	bool ok;
	if (endsWith (path, ".wav")) {
		Uint8 * samples = NULL;
		Uint32 bytes = 0;
		ok = SDL_LoadWAV_IO (SDL_IOFromConstMem (data, length), true, &spec, &samples, &bytes);
		if (ok) pcm.assign (samples, samples + bytes);
		SDL_free (samples);
	} else {
		ok = decodeOgg (data, length, spec, pcm);
	}
	SDL_free (data);
	if (! ok) why = "The sound " + path + " isn't a WAV or OGG file that can be played";
	return ok;
}

SoundPreview::SoundPreview () : enabled (true), failed (false), playCount (0), device (0) {
}

SoundPreview::~SoundPreview () {
	stopAll ();
	if (device) SDL_CloseAudioDevice (device);
	if (device) SDL_QuitSubSystem (SDL_INIT_AUDIO);
}

bool SoundPreview::open (std::string & why) {
	if (device) return true;
	if (failed) return false;
	if (SDL_InitSubSystem (SDL_INIT_AUDIO)) {
		device = SDL_OpenAudioDevice (SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
		if (! device) SDL_QuitSubSystem (SDL_INIT_AUDIO);
	}
	if (device) return true;
	// Said once: after that, sounds are silent.
	failed = true;
	why = std::string ("No sound can be heard: ") + SDL_GetError ();
	return false;
}

bool SoundPreview::play (const std::string & path, std::string & why) {
	playCount ++;
	if (! enabled) {
		SDL_Log ("Sound preview: %s", path.c_str ());
		return true;
	}
	SDL_PathInfo info;
	Sint64 modified = SDL_GetPathInfo (path.c_str (), &info) ? (Sint64) info.modify_time : -1;
	std::map<std::string, Sound>::iterator found = sounds.find (path);
	if (found == sounds.end () || found->second.modified != modified) {
		Sound sound;
		if (! decodeSound (path, sound.spec, sound.pcm, why)) {
			if (found != sounds.end ()) sounds.erase (found);
			return false;
		}
		sound.modified = modified;
		sounds[path] = sound;
		found = sounds.find (path);
	}
	if (! open (why)) return false;
	const Sound & sound = found->second;
	SDL_AudioStream * stream = SDL_CreateAudioStream (&sound.spec, NULL);
	if (! stream) {
		why = std::string ("Can't play the sound: ") + SDL_GetError ();
		return false;
	}
	// Bound first: a stream takes data only once it has the device's format.
	if (! SDL_BindAudioStream (device, stream) || ! SDL_PutAudioStreamData (stream, sound.pcm.data (), (int) sound.pcm.size ())
			|| ! SDL_FlushAudioStream (stream)) {
		why = std::string ("Can't play the sound: ") + SDL_GetError ();
		SDL_DestroyAudioStream (stream);
		return false;
	}
	voices.push_back (stream);
	return true;
}

void SoundPreview::stopAll () {
	for (size_t i = 0; i < voices.size (); i ++) SDL_DestroyAudioStream (voices[i]);
	voices.clear ();
}

void SoundPreview::update () {
	for (size_t i = voices.size (); i -- > 0; ) {
		// Played out: nothing left to convert, nor to hand the device.
		if (SDL_GetAudioStreamQueued (voices[i]) <= 0 && SDL_GetAudioStreamAvailable (voices[i]) <= 0) {
			SDL_DestroyAudioStream (voices[i]);
			voices.erase (voices.begin () + i);
		}
	}
}

} // namespace devkit
