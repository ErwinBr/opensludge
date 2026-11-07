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
 *            rotation, scale (floats), flags (1: flip, 2: visible), depth
 *            (4, signed), sprite
 *   "CLIP":  length (4, signed), flags (1: loop), next track ID (4), next
 *            event ID (4), track count (4), then per track: ID (4), part
 *            name, channel byte, key count (4), then per key: time (4,
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

const int version = 1;

typedef std::vector<unsigned char> Bytes;

struct Document {
	std::vector<RigDefinition> rigs;
	std::vector<ClipDefinition> clips;
};

// False, with why, if a rig or clip isn't valid, or a clip is LEGACY.
bool encode (const Document & doc, Bytes & out, std::string & why);
bool decode (const unsigned char * data, size_t size, Document & doc, std::string & why);

}

#endif
