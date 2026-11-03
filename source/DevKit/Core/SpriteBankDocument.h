#ifndef SLUDGE_DEVKIT_SPRITE_BANK_DOCUMENT_H
#define SLUDGE_DEVKIT_SPRITE_BANK_DOCUMENT_H

/*
 * A sprite bank (.duc): the pictures of a character, an object or a font,
 * each with a hotspot, the point that the game puts where the character
 * stands. The bank is the spriteBank of GraphicalTools/sprites.cpp, which
 * loads, saves and draws it; we change it as the GTK Sprite Bank Editor
 * did, with commands that undo.
 *
 * Inserting, replacing and deleting sprites keep copies of the sprites
 * that they take out and put in, and of the palette, which a picture adds
 * colours to. Hotspots are small, so their commands keep them all.
 * Converting a palette bank to 32-bit can't be undone.
 *
 * A bank is in one of three modes: an open palette, which takes the
 * colours of new pictures, a locked palette, which gives new pictures the
 * closest colours it has, and 32-bit, without a palette. New banks are
 * 32-bit; PNG pictures and fonts need it.
 */

#include <string>
#include <vector>

#include "Document.h"
#include "sprites.h"

namespace devkit {

// A copy of a sprite, for the commands.
struct SpriteCopy {
	int width, height, xhot, yhot;
	std::vector<unsigned char> data;
};

// A copy of the palette of a bank.
struct PaletteCopy {
	bool present;
	int total;
	std::vector<unsigned char> r, g, b;
	std::vector<unsigned short> pal;
	PaletteCopy () : present (false), total (0) {}
};

struct Hotspot {
	int x, y;
	Hotspot () : x (0), y (0) {}
	Hotspot (int x, int y) : x (x), y (y) {}
	bool operator == (const Hotspot & o) const { return x == o.x && y == o.y; }
	bool operator != (const Hotspot & o) const { return ! (*this == o); }
};

class SpriteBankDocument : public Document {
public:
	enum Mode { MODE_OPEN_PALETTE = 0, MODE_LOCKED_PALETTE = 1, MODE_32BIT = 2 };
	// The most sprites that a bank can have.
	static const int MAX_SPRITES = 65535;

	static DocumentType type ();
	SpriteBankDocument ();
	~SpriteBankDocument ();

	const char * kind () const { return "Sprite Bank"; }
	std::string untitledName () const { return "Untitled Sprite Bank.duc"; }

	// The bank, for the views to draw.
	const spriteBank & bank () const { return sprites; }
	int count () const { return sprites.total; }
	Mode mode () const { return (Mode) sprites.type; }
	Hotspot hotspot (int index) const;

	// Insert pictures (PNG or TGA) as new sprites at a place, from 0 to
	// count (), with their hotspots at the middle of their bottom row.
	// Returns how many we inserted; the errors go to errorBox
	// (interface.h). They undo together.
	int insertFiles (int at, const std::vector<std::string> & files);
	// Replace the picture of a sprite, keeping its hotspot.
	bool replaceSprite (int index, const std::string & file);
	void deleteSprites (int from, int howMany);
	// Save a sprite as a PNG picture.
	bool exportSprite (int index, const std::string & file, std::string * error) const;
	// Cut a picture into the letters of a font, at the columns that are
	// empty, and insert them at a place, followed by a space of the given
	// width, as the GTK editor's Fontify did. Needs 32-bit.
	int makeFont (int at, const std::string & file, int spaceWidth);

	// Hotspots. Commands with the same mergeKey (other than 0) merge, as in
	// a drag.
	void setHotspot (int index, Hotspot hotspot, int mergeKey = 0);
	// Give sprites from..from+howMany-1 the same hotspot.
	void setHotspots (int from, int howMany, Hotspot hotspot);
	// The hotspots in the middle of the sprites, or of their bottom rows.
	void centreHotspots (int from, int howMany);
	void baseHotspots (int from, int howMany);

	// Change between the palette modes, or choose any mode while the bank
	// is empty.
	bool canSetMode (Mode to) const;
	void setMode (Mode to);
	// Turn a palette bank into 32-bit, which can't be undone.
	bool convertTo32 ();

protected:
	bool read (const std::string & path, std::string * error);
	bool write (const std::string & path, std::string * error);

private:
	friend class SpriteSplice;
	// Change the hotspots as one command.
	void changeHotspots (const std::vector<Hotspot> & after, const std::string & label, int mergeKey);
	std::vector<Hotspot> hotspots () const;
	void setAllHotspots (const std::vector<Hotspot> & all);
	// Keep a change of sprites that we made already, from..from+removed
	// replaced by inserted, as a command.
	void keepSplice (const std::string & label, int from, const std::vector<SpriteCopy> & removed,
		const std::vector<SpriteCopy> & inserted, const PaletteCopy & before);

	spriteBank sprites;
};

} // namespace devkit

#endif
