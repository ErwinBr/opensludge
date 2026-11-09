// make_asset_project <case> <folder>: writes a project of rigs and clips (the
// .rig and .clp assets of the Dev Kit) for asset_compile_test.cmake, which
// copies bank.duc and pop.wav into it and compiles it.
//
//   shared            two rigs and two clips that share a bank and a sound,
//                     which the script uses too: the game must have each once
//   missing-bank      a clip of a bank that doesn't exist
//   missing-sound     a clip with a sound that doesn't exist
//   missing-function  a clip with an event that calls no function
//   damaged           a clip cut short
//   wrong-kind        a rig in a .clp file
//   missing-part      a clip with a track for a part its rig doesn't have

#include <stdio.h>
#include <string>

#include "animformat.h"
#include "animmodel.h"

namespace {

std::string folder;
bool failed = false;

animformat::AssetId identity (int n) {
	animformat::AssetId id;
	id.bytes[0] = 0xa5;
	id.bytes[15] = (unsigned char) n;
	return id;
}

void writeText (const std::string & name, const std::string & text) {
	FILE * f = fopen ((folder + "/" + name).c_str (), "wb");
	if (! f || fwrite (text.data (), 1, text.size (), f) != text.size ()) failed = true;
	if (f && fclose (f)) failed = true;
}

void writeAsset (const std::string & name, const animformat::Asset & asset, size_t cut = 0) {
	animformat::Bytes bytes;
	std::string why;
	if (! animformat::encodeAsset (asset, bytes, why)) {
		fprintf (stderr, "make_asset_project: %s: %s\n", name.c_str (), why.c_str ());
		failed = true;
		return;
	}
	if (cut) bytes.resize (bytes.size () - cut);
	writeText (name, std::string (bytes.begin (), bytes.end ()));
}

void writeProject (const std::string & script) {
	writeText ("Assets.slp",
		"[SETTINGS]\nwindowname=SLUDGE Assets\nfinalfile=Assets\nlanguage=English\ndatafolder=\nquitmessage=Quit?\n"
		"customicon=\ncustomlogo=\nmouse=0\nfullscreen=N\nmakesilent=N\nshowlogo=N\nshowloading=N\ninvisible=N\n"
		"ditherimages=N\nwidth=320\nheight=240\nspeed=50\n\n[FILES]\nassets.slu\n");
	writeText ("assets.slu", script);
}

// A rig of a body, of sprite 0 of bank.duc.
animformat::Asset rig (int n) {
	animformat::Asset a;
	a.kind = animformat::Asset::RIG;
	a.id = identity (n);
	a.banks.push_back ("bank.duc");
	std::string why;
	addPart (a.rig, "body", "", PartPose (), SpriteRef::sprite (AnimBankRef (), 0, 0, false), why);
	return a;
}

// A clip of the body, bound to rig 1, with a sprite of bank and, if sound or
// function aren't empty, events that play and call them.
animformat::Asset clip (int n, const std::string & bank, const std::string & sound, const std::string & function) {
	animformat::Asset a;
	a.kind = animformat::Asset::CLIP;
	a.id = identity (n);
	a.rigId = identity (1);
	a.rigPath = "one.rig";
	a.clip.length = 16;
	a.clip.loop = true;
	a.banks.push_back (bank);
	std::string why;
	Key k;
	k.sprite = SpriteRef::sprite (AnimBankRef (), 0, 1, false);
	setKey (a.clip, "body", CHANNEL_SPRITE, k, why);
	k.time = 4;
	k.value = 10;
	k.easing = EASING_LINEAR;
	setKey (a.clip, "body", CHANNEL_ROTATION, k, why);
	FrameEvent e;
	if (! sound.empty ()) {
		e.kind = FrameEvent::SOUND;
		addEvent (a.clip, 2, e, why);
		a.eventNames.push_back (sound);
	}
	if (! function.empty ()) {
		e.kind = FrameEvent::FUNCTION;
		addEvent (a.clip, 8, e, why);
		a.eventNames.push_back (function);
	}
	return a;
}

// A script that plays the clip on a character of the rig, then quits.
std::string script (const std::string & clipFile) {
	return "objectType thing (\"thing\") {\n}\n\n"
		"sub init () {\n"
		"\taddCharacter (thing, 160, 200, anim ('bank.duc', 0));\n"
		"\tsetCharacterRig (thing, loadRig ('one.rig'));\n"
		"\tanimate (thing, loadClip ('" + clipFile + "'));\n"
		"\tpause (40);\n"
		"\tquitGame ();\n"
		"}\n";
}

}

int main (int argc, char ** argv) {
	if (argc != 3) {
		fprintf (stderr, "Usage: make_asset_project <case> <folder>\n");
		return 2;
	}
	std::string which = argv[1];
	folder = argv[2];
	writeAsset ("one.rig", rig (1));
	if (which == "shared") {
		writeAsset ("two.rig", rig (2));
		writeAsset ("walk.clp", clip (3, "bank.duc", "pop.wav", "footstep"));
		writeAsset ("wave.clp", clip (4, "bank.duc", "pop.wav", ""));
		writeProject ("objectType thing (\"thing\") {\n}\n\n"
			"# footstep is called only by the events of walk.clp.\n"
			"sub footstep () {\n}\n\n"
			"sub init () {\n"
			"\taddCharacter (thing, 160, 200, anim ('bank.duc', 0));\n"
			"\tsetCharacterRig (thing, loadRig ('one.rig'));\n"
			"\tvar other = loadRig ('two.rig');\n"
			"\tsetCharacterRig (thing, other);\n"
			"\tplaySound ('pop.wav');\n"
			"\tanimate (thing, loadClip ('walk.clp'));\n"
			"\tpause (20);\n"
			"\tanimate (thing, loadClip ('wave.clp'));\n"
			"\tpause (20);\n"
			"\tquitGame ();\n"
			"}\n");
	} else if (which == "missing-bank") {
		writeAsset ("walk.clp", clip (3, "sprites/nothing.duc", "", ""));
		writeProject (script ("walk.clp"));
	} else if (which == "missing-sound") {
		writeAsset ("walk.clp", clip (3, "bank.duc", "sounds/nothing.wav", ""));
		writeProject (script ("walk.clp"));
	} else if (which == "missing-function") {
		writeAsset ("walk.clp", clip (3, "bank.duc", "", "nothing"));
		writeProject (script ("walk.clp"));
	} else if (which == "damaged") {
		writeAsset ("walk.clp", clip (3, "bank.duc", "", ""), 5);
		writeProject (script ("walk.clp"));
	} else if (which == "wrong-kind") {
		writeAsset ("walk.clp", rig (3));
		writeProject (script ("walk.clp"));
	} else if (which == "missing-part") {
		animformat::Asset c = clip (3, "bank.duc", "", "");
		std::string why;
		Key k;
		k.value = 5;
		k.easing = EASING_LINEAR;
		setKey (c.clip, "tail", CHANNEL_ROTATION, k, why);
		writeAsset ("walk.clp", c);
		writeProject (script ("walk.clp"));
	} else {
		fprintf (stderr, "make_asset_project: no case %s\n", which.c_str ());
		return 2;
	}
	return failed ? 1 : 0;
}
