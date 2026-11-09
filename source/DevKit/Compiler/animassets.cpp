#include "animassets.h"

#include <stdio.h>
#include <string.h>
#include <map>

#include "animformat.h"
#include "checkused.h"
#include "dumpfiles.h"
#include "helpers.h"
#include "messbox.h"
#include "settings.h"
#include "splitter.hpp"

namespace {

struct AnimAsset {
	std::string name;			// As on disk
	std::string script;			// The first script that uses it, for messages
	unsigned int line;
	animformat::Asset asset;
	std::vector<int> bankFiles;	// The game's file of each bank of the asset
	std::vector<int> eventNumbers;	// Of each event: its sound's file, or its function
};

std::map<std::string, AnimAsset> assets;

// The last 4 characters of a name, in lower case.
std::string extension (const std::string & name) {
	std::string end = name.size () >= 4 ? name.substr (name.size () - 4) : std::string ();
	for (size_t i = 0; i < end.size (); i ++) {
		if (end[i] >= 'A' && end[i] <= 'Z') end[i] += 'a' - 'A';
	}
	return end;
}

const char * channelName (Channel c) {
	static const char * names[NUM_CHANNELS] = { "x", "y", "rotation", "scale", "sprite", "visibility", "depth", "flip" };
	return c >= 0 && c < NUM_CHANNELS ? names[c] : "?";
}

std::string onDisk (const std::string & name, bool & found) {
	bool caseDiffers;
	char * n = fileNameOnDisk (name.c_str (), & found, & caseDiffers);
	std::string s (n);
	delete [] n;
	return s;
}

bool readFile (const std::string & name, std::vector<unsigned char> & bytes) {
	FILE * f = fopen (name.c_str (), "rb");
	if (! f) return false;
	bytes.clear ();
	unsigned char buffer[4096];
	size_t n;
	while ((n = fread (buffer, 1, sizeof (buffer), f)) > 0) bytes.insert (bytes.end (), buffer, buffer + n);
	bool ok = ! ferror (f);
	fclose (f);
	return ok;
}

bool error (const AnimAsset & a, const std::string & what) {
	addCommentWithLine (ERRORTYPE_PROJECTERROR, (a.name + ": " + what).c_str (), a.script.c_str (), a.line);
	return false;
}

void warning (const AnimAsset & a, const std::string & what) {
	addCommentWithLine (ERRORTYPE_PROJECTWARNING, (a.name + ": " + what).c_str (), a.script.c_str (), a.line);
}

// A file that the asset needs, as one of the game's files: once, however
// many assets and scripts use it.
bool addFile (AnimAsset & a, const std::string & path, const char * kind, stringArray * & fileHandles, int & number) {
	bool found;
	std::string name = onDisk (path, found);
	if (! found) return error (a, std::string ("the ") + kind + " '" + path + "' doesn't exist");
	if (audioFile (name.c_str ()) && settings.forceSilent) name.clear ();
	number = findOrAdd (fileHandles, name.c_str (), false);
	return true;
}

// The parts of the rig that a clip is bound to, if it is there.
void checkBinding (const AnimAsset & a) {
	const animformat::Asset & clip = a.asset;
	if (clip.rigId.isNull ()) return;
	bool found;
	std::string rigName = onDisk (clip.rigPath, found);
	std::vector<unsigned char> bytes;
	animformat::Asset rig;
	std::string why;
	if (! found || ! readFile (rigName, bytes)) {
		warning (a, "its rig '" + clip.rigPath + "' isn't there, so the parts of its tracks aren't checked");
		return;
	}
	if (! animformat::decodeAsset (bytes.data (), bytes.size (), rig, why) || rig.kind != animformat::Asset::RIG) {
		warning (a, "its rig '" + clip.rigPath + "' isn't a rig that can be read (" + why + "), so the parts of its tracks aren't checked");
		return;
	}
	if (rig.id != clip.rigId) {
		warning (a, "'" + clip.rigPath + "' is another rig than the one it was made for, so the parts of its tracks aren't checked");
		return;
	}
	for (size_t t = 0; t < clip.clip.tracks.size (); t ++) {
		const Track & track = clip.clip.tracks[t];
		if (rig.rig.findPart (track.part) < 0) {
			warning (a, "the rig '" + clip.rigPath + "' has no part \"" + track.part + "\" for its track of the " + channelName (track.channel)
				+ "; the track does nothing");
		}
	}
}

}

bool isAnimAssetName (const char * name) {
	std::string end = extension (name);
	return end == ".rig" || end == ".clp";
}

void resetAnimAssets () {
	assets.clear ();
}

bool registerAnimAsset (const char * name, stringArray * & fileHandles, const char * scriptFile, unsigned int line) {
	bool found;
	std::string key = onDisk (name, found);
	if (assets.count (key)) return true;
	AnimAsset & a = assets[key];
	a.name = key;
	a.script = scriptFile ? scriptFile : "";
	a.line = line;
	if (! found) return error (a, "the file doesn't exist");

	std::vector<unsigned char> bytes;
	if (! readFile (key, bytes)) return error (a, "the file can't be read");
	std::string why;
	bool namedClip = extension (key) == ".clp";
	if (! animformat::decodeAsset (bytes.data (), bytes.size (), a.asset, why))
		return error (a, std::string ("not a valid ") + (namedClip ? "clip" : "rig") + ": " + why);
	bool isClip = a.asset.kind == animformat::Asset::CLIP;
	if (isClip != namedClip) return error (a, isClip ? "a clip, in a file of a rig" : "a rig, in a file of a clip");

	a.bankFiles.resize (a.asset.banks.size ());
	for (size_t i = 0; i < a.asset.banks.size (); i ++) {
		if (! addFile (a, a.asset.banks[i], "sprite bank", fileHandles, a.bankFiles[i])) return false;
	}
	if (! isClip) return true;

	a.eventNumbers.assign (a.asset.clip.events.size (), -1);
	for (size_t e = 0; e < a.asset.clip.events.size (); e ++) {
		if (a.asset.clip.events[e].what.kind != FrameEvent::SOUND) continue;
		if (! addFile (a, a.asset.eventNames[e], "sound", fileHandles, a.eventNumbers[e])) return false;
	}
	checkBinding (a);
	return true;
}

bool resolveAnimAssetFunctions (stringArray * functionNames) {
	for (std::map<std::string, AnimAsset>::iterator i = assets.begin (); i != assets.end (); ++ i) {
		AnimAsset & a = i -> second;
		if (a.asset.kind != animformat::Asset::CLIP) continue;
		for (size_t e = 0; e < a.asset.clip.events.size (); e ++) {
			const ClipEvent & ev = a.asset.clip.events[e];
			if (ev.what.kind != FrameEvent::FUNCTION) continue;
			const std::string & function = a.asset.eventNames[e];
			int number = findElement (functionNames, function.c_str ());
			if (number < 0) {
				char tick[16];
				snprintf (tick, sizeof (tick), "%d", ev.time);
				return error (a, std::string ("the event at tick ") + tick + " calls '" + function + "', which isn't a function");
			}
			a.eventNumbers[e] = number;
			setUsed (CHECKUSED_FUNCTIONS, number);
		}
	}
	return true;
}

bool packAnimAsset (const char * name, std::vector<unsigned char> & out) {
	std::map<std::string, AnimAsset>::const_iterator i = assets.find (name);
	if (i == assets.end ()) {
		addComment (ERRORTYPE_INTERNALERROR, "An asset that wasn't read before", name, NULL, 0);
		return false;
	}
	const AnimAsset & a = i -> second;
	animformat::Document doc;
	if (a.asset.kind == animformat::Asset::RIG) {
		doc.rigs.push_back (a.asset.rig);
		std::vector<RigPart> & parts = doc.rigs[0].parts;
		for (size_t p = 0; p < parts.size (); p ++) {
			if (parts[p].sprite.kind == SpriteRef::SPRITE) parts[p].sprite.bankFile = a.bankFiles[parts[p].sprite.bankFile];
		}
	} else {
		doc.clips.push_back (a.asset.clip);
		ClipDefinition & clip = doc.clips[0];
		for (size_t t = 0; t < clip.tracks.size (); t ++) {
			for (size_t k = 0; k < clip.tracks[t].keys.size (); k ++) {
				SpriteRef & s = clip.tracks[t].keys[k].sprite;
				if (s.kind == SpriteRef::SPRITE) s.bankFile = a.bankFiles[s.bankFile];
			}
		}
		for (size_t e = 0; e < clip.events.size (); e ++) clip.events[e].what.number = a.eventNumbers[e];
	}
	std::string why;
	if (! animformat::encode (doc, out, why)) return error (a, "can't be packed: " + why);
	return true;
}
