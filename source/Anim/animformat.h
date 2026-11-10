#ifndef ANIMFORMAT_H
#define ANIMFORMAT_H

/*
 * The file format of rigs and clips (animmodel.h): what saved games store
 * of them, and what the animation editor will read and write.
 *
 * Everything is little-endian. A file is the magic "SLAN", a 2-byte
 * version, then chunks to the end: a 4-byte tag, a 4-byte size, and that
 * many bytes. A decoder skips the chunks it doesn't know, and must use the
 * whole of each it knows.
 *
 *   string:  2-byte length, then the bytes (UTF-8, no 0 byte)
 *   sprite:  kind byte (0 DEFAULT, 1 NONE, 2 SPRITE); for SPRITE the bank's
 *            file number and the sprite number (4 bytes, signed each) and a
 *            mirror byte (0 or 1)
 *   float:   4 bytes, IEEE 754, finite
 *
 *   "RIG ":  next part ID (4), part count (4), then per part: ID (4),
 *            name, parent index (4, signed: -1 for the root), x, y,
 *            rotation, scale x (floats), from version 3 scale y (float;
 *            before, the same as x), flags (1: flip, 2: visible), depth
 *            (4, signed), sprite; from version 2, then the pivot x, y
 *            (floats); from version 4, then modulate R, G, B, A and self
 *            modulate R, G, B, A (floats, 0 to 2)
 *   "CLIP":  length (4, signed), flags (1: loop), next track ID (4), next
 *            event ID (4), track count (4), then per track: ID (4), part
 *            name, channel byte (SCALE_X and SCALE_Y from version 3, the
 *            MODULATE and SELF_MODULATE channels from version 4),
 *            key count (4), then per key: time (4,
 *            signed), easing byte, and a sprite for SPRITE tracks, a float
 *            for the others; then the event count (4), and per event: ID
 *            (4), time (4, signed), kind byte (1 sound, 2 function), number
 *            (4, signed)
 *
 * Only TIMELINE clips are stored here: animations of anim () keep their
 * legacy record in saved games (saveAnim). Sprites are stored by their
 * bank's file number; resolveBanks loads the banks after decoding.
 *
 * Decoding checks every count against the bytes left, then the whole rig or
 * clip with validRig or validClip, so a damaged file fails with a reason
 * rather than crashing or allocating without end.
 */

#include <stddef.h>
#include <string>
#include <vector>

#include "animmodel.h"

namespace animformat {

// The version encode writes: the lowest that holds what it writes, so that
// what was written before stays the same to the byte. 1; 2 when a part of a
// rig has a pivot; 3 when a part has different X and Y scales, or a clip
// has a track of SCALE_X or SCALE_Y; 4 when a part of a rig has a colour,
// or a clip a track of one. decode reads them all.
const int version = 4;

typedef std::vector<unsigned char> Bytes;

struct Document {
	std::vector<RigDefinition> rigs;
	std::vector<ClipDefinition> clips;
};

// False, with why, if a rig or clip isn't valid, or a clip is LEGACY.
bool encode (const Document & doc, Bytes & out, std::string & why);
bool decode (const unsigned char * data, size_t size, Document & doc, std::string & why);

/*
 * Project assets: one rig (a .rig file) or one clip (a .clp file) in a
 * project's folder, as the Dev Kit saves them. An asset is a file of this
 * format, with one RIG or CLIP chunk and these:
 *
 *   "ASID":  the asset's identity, 16 bytes, made once and kept whatever the
 *            file is called or wherever it moves
 *   "BANK":  count (4), then the path of each sprite bank (strings, from the
 *            project's folder, with /); an asset's sprites refer to a bank by
 *            its place in this list, not by a file number of a game
 *   "RREF":  a clip's only, when it is bound to a rig: the rig's identity (16)
 *            and its path, a hint for finding it
 *   "EVNM":  a clip's only: count (4), then per event its ID (4) and the path
 *            of its sound or the name of its function; every event has one,
 *            and the events' numbers mean nothing in an asset
 *   "EDIT":  how the Dev Kit shows parts, which games don't use: count (4),
 *            then per part, a rig's by its ID (4) and a clip's by its name,
 *            flags (1: locked, 2: scales of X and Y apart, 4: scaled
 *            uniformly; a clip's part with neither follows its rig).
 *            Only parts that aren't as by default (unlocked, following the
 *            rig or uniform) are written, and none at all leaves the chunk
 *            out.
 *
 * A game has no files of paths and names: the compiler turns an asset into a
 * file of the format above, with the numbers of the game's
 * files and functions. decode skips the chunks of assets, as it skips any it
 * doesn't know.
 */

struct AssetId {
	unsigned char bytes[16];

	AssetId () { for (int i = 0; i < 16; i ++) bytes[i] = 0; }
	bool isNull () const;
	bool operator== (const AssetId & o) const;
	bool operator!= (const AssetId & o) const { return ! (* this == o); }
	// 32 hexadecimal digits, for messages and logs.
	std::string text () const;
};

// A new, random identity.
AssetId newAssetId ();

// How the Dev Kit shows a part of a rig or clip ("EDIT" above).
struct EditorPart {
	enum Scaling { SCALING_DEFAULT, SCALING_UNIFORM, SCALING_FREE };
	PartId id;			// A rig's part
	std::string name;	// A clip's part ("" is the character)
	bool locked;		// It can't be selected or changed on the canvas
	Scaling scaling;	// How the canvas and fields scale it

	EditorPart () : id (0), locked (false), scaling (SCALING_DEFAULT) {}
	bool isDefault () const { return ! locked && scaling == SCALING_DEFAULT; }
};

struct Asset {
	enum Kind { RIG, CLIP };
	Kind kind;
	AssetId id;
	std::vector<std::string> banks;
	RigDefinition rig;			// RIG
	ClipDefinition clip;		// CLIP
	AssetId rigId;				// CLIP: the rig it is bound to, or null
	std::string rigPath;		// CLIP: where that rig was, if bound
	// CLIP: of each event of clip, in the order of clip.events, the path of
	// its sound or the name of its function.
	std::vector<std::string> eventNames;
	// One per part at most; written only for parts that a rig has (any name
	// of a clip's), and that aren't as by default.
	std::vector<EditorPart> editor;

	Asset () : kind (RIG) {}
	// Of a rig's part, or a clip's: as by default when it has none.
	EditorPart editorPart (PartId id) const;
	EditorPart editorPart (const std::string & name) const;
	// Set it, or forget it when it is as by default.
	void setEditorPart (const EditorPart & part);
};

// False, with why, if the asset isn't whole: a valid rig or clip, a
// non-null identity, every sprite of a bank in banks, a name for every
// event, a binding only for a clip. decodeAsset checks the same, and that a
// file is an asset of one rig or one clip.
bool encodeAsset (const Asset & asset, Bytes & out, std::string & why);
bool decodeAsset (const unsigned char * data, size_t size, Asset & asset, std::string & why);

}

#endif
