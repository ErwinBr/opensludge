// make_ego_assets <folder>: writes ego's rig (ego.rig) and the clips of its
// costume (stand_, walk_ and talk_ of each direction, .clp) as the Dev Kit
// saves them, for CompositeAssetsTest.
//
// They are built here a second way, apart from the script that builds them
// in tests/render/composite.slu (egoRig, pose, standClip, walkClip,
// talkClip): the game that loads these must draw every frame as the one
// that builds them by script, so whatever the two make differently shows.
// Keep the two the same.

#include <stdio.h>
#include <initializer_list>
#include <string>
#include <vector>

#include "animformat.h"
#include "animmodel.h"

namespace {

enum Direction { SOUTH, WEST, NORTH, EAST };
const char * directionNames[4] = { "south", "west", "north", "east" };

bool failed = false;

void check (bool ok, const std::string & what, const std::string & why) {
	if (! ok) {
		fprintf (stderr, "make_ego_assets: %s: %s\n", what.c_str (), why.c_str ());
		failed = true;
	}
}

// An identity of its own for each asset, the same every time.
animformat::AssetId identity (int n) {
	animformat::AssetId id;
	const char * tag = "ego-test-assets";
	for (int i = 0; i < 15; i ++) id.bytes[i] = (unsigned char) tag[i];
	id.bytes[15] = (unsigned char) n;
	return id;
}

// A clip, and the banks it names.
struct ClipAsset {
	animformat::Asset asset;
	std::string why;

	ClipAsset (int length, bool loop) {
		asset.kind = animformat::Asset::CLIP;
		asset.clip.length = length;
		asset.clip.loop = loop;
		asset.rigId = identity (0);
		asset.rigPath = "hd-ego/ego.rig";
	}

	int bank (const std::string & name) {
		std::string path = "hd-ego/" + name + ".duc";
		for (size_t i = 0; i < asset.banks.size (); i ++) {
			if (asset.banks[i] == path) return (int) i;
		}
		asset.banks.push_back (path);
		return (int) asset.banks.size () - 1;
	}

	// As clipKey: scale in percent, visibility and flipping as true or false.
	void key (const char * part, Channel channel, int tick, int value, Easing easing = EASING_LINEAR) {
		Key k;
		k.time = tick;
		k.value = channel == CHANNEL_SCALE ? value / 100.0f : (float) value;
		if (channel == CHANNEL_VISIBLE || channel == CHANNEL_FLIP) k.value = value ? 1.0f : 0.0f;
		k.easing = isDiscrete (channel) ? EASING_STEP : easing;
		check (setKey (asset.clip, part, channel, k, why), std::string ("a key of ") + part, why);
	}

	// As clipSprite.
	void sprite (const char * part, int tick, const std::string & bankName, int number) {
		Key k;
		k.time = tick;
		k.easing = EASING_STEP;
		k.sprite = SpriteRef::sprite (AnimBankRef (), bank (bankName), number, false);
		check (setKey (asset.clip, part, CHANNEL_SPRITE, k, why), std::string ("a sprite of ") + part, why);
	}

	void keyPart (const char * part, int x, int y, int depth) {
		key (part, CHANNEL_X, 0, x, EASING_STEP);
		key (part, CHANNEL_Y, 0, y, EASING_STEP);
		key (part, CHANNEL_DEPTH, 0, depth);
	}

	void keyFrames (const char * part, const std::string & bankName, int step, const std::vector<int> & frames) {
		for (size_t i = 0; i < frames.size (); i ++) sprite (part, (int) i * step, bankName, frames[i]);
	}

	void keyFramesAt (const char * part, const std::string & bankName, const std::vector<int> & ticks, const std::vector<int> & frames) {
		for (size_t i = 0; i < frames.size () && i < ticks.size (); i ++) sprite (part, ticks[i], bankName, frames[i]);
	}

	// pose of composite.slu.
	void pose (Direction dir) {
		if (dir == SOUTH) {
			keyPart ("legs", -4, -1, 0);
			keyPart ("torso", -3, -258, 1);
			keyPart ("head", 3, -213, 2);
			keyPart ("eyes", 0, -100, 2);
			keyPart ("arm1", -79, -153, -1);
			keyPart ("arm2", 70, -159, -1);
			sprite ("legs", 0, "legs_south", 0);
			sprite ("torso", 0, "torso_south", 0);
			sprite ("head", 0, "head_south", 0);
			sprite ("eyes", 0, "head_south", 6);
			sprite ("arm1", 0, "arm1_south", 0);
			sprite ("arm2", 0, "arm2_south", 0);
			key ("eyes", CHANNEL_VISIBLE, 0, 1);
		} else if (dir == NORTH) {
			keyPart ("legs", 0, 0, 0);
			keyPart ("torso", 1, -280, 2);
			keyPart ("head", -1, -181, 1);
			keyPart ("arm1", -72, -133, -1);
			keyPart ("arm2", 75, -137, -1);
			sprite ("legs", 0, "legs_north", 0);
			sprite ("torso", 0, "torso_north", 0);
			sprite ("head", 0, "head_north", 0);
			sprite ("arm1", 0, "arms_north", 0);
			sprite ("arm2", 0, "arms_north", 14);
			key ("eyes", CHANNEL_VISIBLE, 0, 0);
		} else {
			keyPart ("legs", 9, -1, 0);
			keyPart ("torso", -21, -246, 1);
			keyPart ("head", -7, -226, 4);
			keyPart ("eyes", 0, -100, 4);
			keyPart ("arm1", 53, dir == WEST ? -161 : -177, -1);
			keyPart ("arm2", -49, -160, 3);
			sprite ("legs", 0, "legs_east", 0);
			sprite ("torso", 0, "torso_east", 0);
			sprite ("head", 0, "head_east", 0);
			sprite ("eyes", 0, "head_east", 7);
			sprite ("arm1", 0, "arm1_east", 0);
			sprite ("arm2", 0, "arm2_east", 0);
			key ("eyes", CHANNEL_VISIBLE, 0, 1);
			key ("", CHANNEL_FLIP, 0, dir == WEST);
		}
	}

	// bob of composite.slu: SLUDGE's / of whole numbers, as C's.
	void bob (int high, int low, int lean) {
		int middle = (high + low) / 2;
		key ("torso", CHANNEL_Y, 0, middle);
		key ("torso", CHANNEL_Y, 6, high);
		key ("torso", CHANNEL_Y, 18, low);
		key ("torso", CHANNEL_Y, 30, high);
		key ("torso", CHANNEL_Y, 42, low);
		key ("torso", CHANNEL_Y, 48, middle);
		key ("torso", CHANNEL_ROTATION, 0, lean);
		key ("torso", CHANNEL_ROTATION, 24, - lean);
		key ("torso", CHANNEL_ROTATION, 48, lean);
	}

	void nod (const char * part, Channel channel, int first, int second, int third) {
		key (part, channel, 0, first);
		key (part, channel, 40, first);
		key (part, channel, 48, second);
		key (part, channel, 88, second);
		key (part, channel, 96, third);
		key (part, channel, 180, third);
		key (part, channel, 188, first);
	}
};

std::vector<int> countFrames (int first, int last) {
	std::vector<int> s;
	for (int i = first; i <= last; i ++) s.push_back (i);
	return s;
}

std::vector<int> swingFrames (int first, int top) {
	std::vector<int> s = countFrames (first, top);
	for (int i = top - 1; i > first; i --) s.push_back (i);
	return s;
}

ClipAsset standClip (Direction dir) {
	ClipAsset c (0, false);
	c.pose (dir);
	return c;
}

ClipAsset walkClip (Direction dir) {
	ClipAsset c (48, true);
	c.pose (dir);
	if (dir == SOUTH) {
		c.keyFrames ("legs", "legs_south", 2, countFrames (1, 24));
		c.keyFrames ("torso", "torso_south", 2, countFrames (1, 24));
		c.keyFrames ("arm1", "arm1_south", 2, swingFrames (1, 13));
		c.keyFrames ("arm2", "arm2_south", 2, swingFrames (1, 13));
		c.key ("arm1", CHANNEL_DEPTH, 0, 3);
		c.key ("arm1", CHANNEL_DEPTH, 14, -1);
		c.key ("arm1", CHANNEL_DEPTH, 36, 3);
		c.key ("arm2", CHANNEL_DEPTH, 12, 3);
		c.key ("arm2", CHANNEL_DEPTH, 38, -1);
		c.bob (-245, -273, 2);
	} else if (dir == NORTH) {
		c.keyFrames ("legs", "legs_north", 2, countFrames (1, 24));
		c.keyFrames ("torso", "torso_north", 2, countFrames (1, 24));
		c.keyFrames ("arm1", "arms_north", 2, swingFrames (1, 13));
		c.keyFrames ("arm2", "arms_north", 2, swingFrames (15, 27));
		c.key ("arm1", CHANNEL_DEPTH, 14, 3);
		c.key ("arm1", CHANNEL_DEPTH, 36, -1);
		c.key ("arm2", CHANNEL_DEPTH, 0, 3);
		c.key ("arm2", CHANNEL_DEPTH, 12, -1);
		c.key ("arm2", CHANNEL_DEPTH, 38, 3);
		c.bob (-270, -295, -2);
	} else {
		c.keyFrames ("legs", "legs_east", 2, countFrames (2, 25));
		c.keyFrames ("torso", "torso_east", 2, countFrames (2, 25));
		c.keyFrames ("arm1", "arm1_east", 2, countFrames (2, 25));
		c.keyFrames ("arm2", "arm2_east", 2, countFrames (2, 25));
		c.sprite ("head", 0, "head_east", 1);
		c.key ("eyes", CHANNEL_VISIBLE, 0, 0);
		c.bob (-230, -266, -2);
	}
	return c;
}

std::vector<int> list (std::initializer_list<int> l) { return std::vector<int> (l); }

ClipAsset talkClip (Direction dir) {
	ClipAsset c (192, true);
	c.pose (dir);
	if (dir == SOUTH) {
		c.keyFrames ("head", "head_south", 4, list ({ 1, 2, 3, 4, 5, 3, 1, 3, 2, 5, 2, 4, 3, 1, 2, 1, 3, 5, 3, 4, 5, 2, 4, 3,
			5, 1, 2, 3, 4, 5, 3, 1, 3, 2, 5, 2, 4, 3, 1, 2, 1, 3, 5, 3, 4, 5, 2, 4 }));
		c.nod ("head", CHANNEL_Y, -213, -210, -205);
	} else if (dir == NORTH) {
		c.keyFramesAt ("head", "head_north",
			list ({ 0, 4, 8, 20, 24, 28, 32, 40, 44, 48, 52, 68, 84, 88, 92, 96, 100, 104, 116, 120, 124, 128, 136, 140, 144, 148, 164, 180, 184, 188 }),
			list ({ 0, 1, 2, 0, 1, 0, 1, 2, 0, 2, 0, 1, 0, 1, 2, 0, 1, 2, 0, 1, 0, 1, 2, 0, 2, 0, 1, 0, 1, 2 }));
		c.nod ("head", CHANNEL_Y, -181, -178, -175);
	} else {
		c.keyFramesAt ("head", "head_east",
			list ({ 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 116,
				120, 124, 128, 132, 136, 140, 144, 148, 152, 156, 160, 164, 168, 172, 180, 184, 188 }),
			list ({ 2, 3, 4, 5, 6, 2, 3, 4, 5, 6, 4, 2, 3, 5, 6, 0, 5, 2, 6, 0, 6, 5, 6, 3, 2, 3, 4, 5, 6, 2, 3, 4, 5, 6, 4, 2, 3, 5, 6, 0,
				5, 2, 6, 0, 5, 6, 3 }));
		c.nod ("head", CHANNEL_ROTATION, 0, -3, -5);
	}
	return c;
}

// egoRig of composite.slu.
animformat::Asset egoRig () {
	animformat::Asset a;
	a.kind = animformat::Asset::RIG;
	a.id = identity (0);
	const char * banks[] = { "legs_south", "torso_south", "head_south", "arm1_south", "arm2_south" };
	for (int i = 0; i < 5; i ++) a.banks.push_back (std::string ("hd-ego/") + banks[i] + ".duc");
	struct Part { const char * name, * parent; int x, y, bank, sprite; } parts[] = {
		{ "legs", "", -4, -1, 0, 0 },
		{ "torso", "", -3, -258, 1, 0 },
		{ "head", "torso", 3, -213, 2, 0 },
		{ "eyes", "head", 0, -100, 2, 6 },
		{ "arm1", "torso", -79, -153, 3, 0 },
		{ "hand1", "arm1", -30, 222, -1, 0 },
		{ "arm2", "torso", 70, -159, 4, 0 },
		{ "hand2", "arm2", -9, 213, -1, 0 },
	};
	std::string why;
	for (size_t i = 0; i < sizeof (parts) / sizeof (parts[0]); i ++) {
		PartPose rest;
		rest.x = (float) parts[i].x;
		rest.y = (float) parts[i].y;
		SpriteRef sprite;
		if (parts[i].bank >= 0) sprite = SpriteRef::sprite (AnimBankRef (), parts[i].bank, parts[i].sprite, false);
		check (addPart (a.rig, parts[i].name, parts[i].parent, rest, sprite, why), std::string ("the part ") + parts[i].name, why);
	}
	RigPart * eyes = & a.rig.parts[a.rig.findPart ("eyes")];
	eyes -> pivotX = 0;
	eyes -> pivotY = -100;
	struct Depth { const char * name; int depth; } depths[] = { { "torso", 1 }, { "head", 2 }, { "eyes", 2 }, { "arm1", -1 }, { "arm2", -1 } };
	for (size_t i = 0; i < sizeof (depths) / sizeof (depths[0]); i ++) a.rig.parts[a.rig.findPart (depths[i].name)].depth = depths[i].depth;
	return a;
}

void write (const std::string & folder, const std::string & name, const animformat::Asset & asset) {
	animformat::Bytes bytes;
	std::string why;
	if (! animformat::encodeAsset (asset, bytes, why)) {
		check (false, name, why);
		return;
	}
	std::string path = folder + "/" + name;
	FILE * f = fopen (path.c_str (), "wb");
	bool ok = f && fwrite (bytes.data (), 1, bytes.size (), f) == bytes.size ();
	if (f) ok = fclose (f) == 0 && ok;
	check (ok, path, "can't be written");
}

}

int main (int argc, char ** argv) {
	if (argc != 2) {
		fprintf (stderr, "Usage: make_ego_assets <folder>\n");
		return 2;
	}
	std::string folder = argv[1];
	write (folder, "ego.rig", egoRig ());
	int n = 1;
	for (int d = 0; d < 4; d ++) {
		Direction dir = (Direction) d;
		std::string name = directionNames[d];
		ClipAsset stand = standClip (dir), talk = talkClip (dir);
		stand.asset.id = identity (n ++);
		talk.asset.id = identity (n ++);
		write (folder, "stand_" + name + ".clp", stand.asset);
		write (folder, "talk_" + name + ".clp", talk.asset);
		// West walks as east's mirror image (mirrorClip), as in composite.slu.
		if (dir != WEST) {
			ClipAsset walk = walkClip (dir);
			walk.asset.id = identity (n ++);
			write (folder, "walk_" + name + ".clp", walk.asset);
		}
	}
	return failed ? 1 : 0;
}
