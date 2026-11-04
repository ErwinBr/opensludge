#include <stdio.h>
#include <string.h>

#include <png.h>

#include "interface.h"
#include "SpriteBankDocument.h"

namespace devkit {

// The bytes of the picture of a sprite: one for each pixel with a palette,
// four without.
static size_t bytesOf (const spriteBank & bank, const sprite & single) {
	return (size_t) single.width * single.height * (bank.type < 2 ? 1 : 4);
}

static SpriteCopy copyOf (const spriteBank & bank, int index) {
	const sprite & single = bank.sprites[index];
	SpriteCopy copy;
	copy.width = single.width;
	copy.height = single.height;
	copy.xhot = single.xhot;
	copy.yhot = single.yhot;
	if (single.data) copy.data.assign (single.data, single.data + bytesOf (bank, single));
	return copy;
}

static std::vector<SpriteCopy> copiesOf (const spriteBank & bank, int from, int howMany) {
	std::vector<SpriteCopy> copies;
	for (int i = from; i < from + howMany; i ++) copies.push_back (copyOf (bank, i));
	return copies;
}

// Put copies into the bank at a place.
static void putCopies (spriteBank & bank, int at, const std::vector<SpriteCopy> & copies) {
	for (size_t k = 0; k < copies.size (); k ++) {
		addSprite (at + (int) k, &bank);
		sprite & single = bank.sprites[at + k];
		const SpriteCopy & copy = copies[k];
		single.width = copy.width;
		single.height = copy.height;
		single.xhot = copy.xhot;
		single.yhot = copy.yhot;
		single.special = 0;
		// A palette sprite has an empty row more, as loadSpriteBank makes it.
		size_t size = copy.data.size () + (bank.type < 2 ? copy.width : 0);
		single.data = new unsigned char [size ? size : 1];
		memset (single.data, 0, size ? size : 1);
		if (! copy.data.empty ()) memcpy (single.data, copy.data.data (), copy.data.size ());
	}
	spriteBankChanged (&bank);
}

static void takeOut (spriteBank & bank, int at, int howMany) {
	for (int i = 0; i < howMany; i ++) deleteSprite (at, &bank);
}

static PaletteCopy paletteOf (const spriteBank & bank) {
	PaletteCopy copy;
	const spritePalette & p = bank.myPalette;
	if (! p.pal) return copy;
	copy.present = true;
	copy.total = p.total;
	copy.r.assign (p.r, p.r + 256);
	copy.g.assign (p.g, p.g + 256);
	copy.b.assign (p.b, p.b + 256);
	copy.pal.assign (p.pal, p.pal + 256);
	return copy;
}

static void setPalette (spriteBank & bank, const PaletteCopy & copy) {
	if (! copy.present) return;
	spritePalette & p = bank.myPalette;
	reserveSpritePal (&p, copy.total);
	memcpy (p.r, copy.r.data (), 256);
	memcpy (p.g, copy.g.data (), 256);
	memcpy (p.b, copy.b.data (), 256);
	memcpy (p.pal, copy.pal.data (), 256 * sizeof (unsigned short));
	spriteBankChanged (&bank);
}

// A palette for a bank that has none yet. Colour 0 is transparent, so the
// colours of pictures start at 1.
static void ensurePalette (spriteBank & bank) {
	if (! bank.myPalette.pal) {
		reserveSpritePal (&bank.myPalette, 1);
		memset (bank.myPalette.r, 0, 256);
		memset (bank.myPalette.g, 0, 256);
		memset (bank.myPalette.b, 0, 256);
		memset (bank.myPalette.pal, 0, 256 * sizeof (unsigned short));
	}
	if (! bank.myPalette.total) bank.myPalette.total = 1;
}

// Sprites from..from+removed replaced by inserted, and the palette before
// and after.
class SpriteSplice : public Command {
public:
	SpriteSplice (SpriteBankDocument * d, const std::string & l, int f,
			const std::vector<SpriteCopy> & r, const std::vector<SpriteCopy> & i, const PaletteCopy & b, const PaletteCopy & a)
		: document (d), text (l), from (f), removed (r), inserted (i), before (b), after (a) {}

	std::string label () const { return text; }
	void apply () {
		takeOut (document->sprites, from, (int) removed.size ());
		putCopies (document->sprites, from, inserted);
		setPalette (document->sprites, after);
	}
	void revert () {
		takeOut (document->sprites, from, (int) inserted.size ());
		putCopies (document->sprites, from, removed);
		setPalette (document->sprites, before);
	}

private:
	SpriteBankDocument * document;
	std::string text;
	int from;
	std::vector<SpriteCopy> removed, inserted;
	PaletteCopy before, after;
};

// Sprites at some places taken out, and the palette before and after.
class SpriteRemoval : public Command {
public:
	SpriteRemoval (SpriteBankDocument * d, const std::string & l, const std::vector<int> & i,
			const std::vector<SpriteCopy> & r, const PaletteCopy & b, const PaletteCopy & a)
		: document (d), text (l), indices (i), removed (r), before (b), after (a) {}

	std::string label () const { return text; }
	void apply () {
		// From the end, so that the places before stay where they were.
		for (size_t k = indices.size (); k -- > 0; ) deleteSprite (indices[k], &document->sprites);
		spriteBankChanged (&document->sprites);
		setPalette (document->sprites, after);
	}
	void revert () {
		for (size_t k = 0; k < indices.size (); k ++) {
			putCopies (document->sprites, indices[k], std::vector<SpriteCopy> (1, removed[k]));
		}
		setPalette (document->sprites, before);
	}

private:
	SpriteBankDocument * document;
	std::string text;
	std::vector<int> indices;		// Ascending
	std::vector<SpriteCopy> removed;
	PaletteCopy before, after;
};

// The sprites in a new order: the sprite at n was at order[n].
class SpriteOrder : public Command {
public:
	SpriteOrder (SpriteBankDocument * d, const std::string & l, const std::vector<int> & o)
		: document (d), text (l), order (o) {}

	std::string label () const { return text; }
	void apply () {
		spriteBank & bank = document->sprites;
		std::vector<sprite> old (bank.sprites, bank.sprites + bank.total);
		for (size_t n = 0; n < order.size (); n ++) bank.sprites[n] = old[order[n]];
		spriteBankChanged (&bank);
	}
	void revert () {
		spriteBank & bank = document->sprites;
		std::vector<sprite> now (bank.sprites, bank.sprites + bank.total);
		for (size_t n = 0; n < order.size (); n ++) bank.sprites[order[n]] = now[n];
		spriteBankChanged (&bank);
	}

private:
	SpriteBankDocument * document;
	std::string text;
	std::vector<int> order;
};

// The places that are in the bank, ascending, each once.
static std::vector<int> validIndices (const std::vector<int> & indices, int total) {
	std::vector<bool> chosen (total > 0 ? total : 0, false);
	for (size_t k = 0; k < indices.size (); k ++) {
		if (indices[k] >= 0 && indices[k] < total) chosen[indices[k]] = true;
	}
	std::vector<int> valid;
	for (int i = 0; i < total; i ++) if (chosen[i]) valid.push_back (i);
	return valid;
}

DocumentType SpriteBankDocument::type () {
	DocumentType type;
	type.name = "Sprite Bank";
	type.extensions.push_back ("duc");
	type.create = [] () -> Document * { return new SpriteBankDocument; };
	return type;
}

SpriteBankDocument::SpriteBankDocument () {
	memset (&sprites, 0, sizeof (sprites));
	sprites.type = MODE_32BIT;
	spriteBankChanged (&sprites);
}

SpriteBankDocument::~SpriteBankDocument () {
	forgetSpriteBank (&sprites);
}

Hotspot SpriteBankDocument::hotspot (int index) const {
	if (index < 0 || index >= sprites.total) return Hotspot ();
	return Hotspot (sprites.sprites[index].xhot, sprites.sprites[index].yhot);
}

void SpriteBankDocument::keepSplice (const std::string & label, int from, const std::vector<SpriteCopy> & removed,
		const std::vector<SpriteCopy> & inserted, const PaletteCopy & before) {
	commands ().add (new SpriteSplice (this, label, from, removed, inserted, before, paletteOf (sprites)));
}

static bool loadPicture (const std::string & file, spriteBank & bank, int index) {
	std::string extension = extensionOf (file);
	if (extension == "png") return loadSpriteFromPNG (file.c_str (), &bank, index);
	if (extension == "tga") return loadSpriteFromTGA (file.c_str (), &bank, index);
	errorBox ("Can't load the picture", (fileNameOf (file) + "\n\nTGA and PNG are the kinds of picture that a sprite can be.").c_str ());
	return false;
}

int SpriteBankDocument::insertFiles (int at, const std::vector<std::string> & files) {
	if (at < 0) at = 0;
	if (at > sprites.total) at = sprites.total;
	PaletteCopy before = paletteOf (sprites);
	int loaded = 0;
	for (size_t f = 0; f < files.size () && sprites.total < MAX_SPRITES; f ++) {
		int index = at + loaded;
		addSprite (index, &sprites);
		if (loadPicture (files[f], sprites, index)) {
			sprite & single = sprites.sprites[index];
			single.xhot = single.width / 2;
			single.yhot = single.height - 1;
			loaded ++;
		} else {
			deleteSprite (index, &sprites);
		}
	}
	if (loaded) keepSplice (loaded == 1 ? "Insert Sprite" : "Insert Sprites", at, std::vector<SpriteCopy> (), copiesOf (sprites, at, loaded), before);
	return loaded;
}

bool SpriteBankDocument::replaceSprite (int index, const std::string & file) {
	if (index < 0 || index >= sprites.total) return false;
	PaletteCopy before = paletteOf (sprites);
	std::vector<SpriteCopy> removed = copiesOf (sprites, index, 1);
	// The loaders change only the picture, so the hotspot stays.
	if (! loadPicture (file, sprites, index)) return false;
	keepSplice ("Replace Sprite", index, removed, copiesOf (sprites, index, 1), before);
	return true;
}

void SpriteBankDocument::deleteSprites (int from, int howMany) {
	if (from < 0) from = 0;
	if (from + howMany > sprites.total) howMany = sprites.total - from;
	if (howMany <= 0) return;
	PaletteCopy before = paletteOf (sprites);
	std::vector<SpriteCopy> removed = copiesOf (sprites, from, howMany);
	takeOut (sprites, from, howMany);
	keepSplice (howMany == 1 ? "Delete Sprite" : "Delete Sprites", from, removed, std::vector<SpriteCopy> (), before);
}

void SpriteBankDocument::deleteSprites (const std::vector<int> & indices) {
	std::vector<int> valid = validIndices (indices, sprites.total);
	if (valid.empty ()) return;
	PaletteCopy before = paletteOf (sprites);
	std::vector<SpriteCopy> removed;
	for (size_t k = 0; k < valid.size (); k ++) removed.push_back (copyOf (sprites, valid[k]));
	commands ().run (new SpriteRemoval (this, valid.size () == 1 ? "Delete Sprite" : "Delete Sprites", valid, removed, before, before));
}

int SpriteBankDocument::moveSprites (const std::vector<int> & indices, int to) {
	std::vector<int> valid = validIndices (indices, sprites.total);
	if (valid.empty ()) return to;
	if (to < 0) to = 0;
	if (to > sprites.total) to = sprites.total;
	std::vector<bool> moving (sprites.total, false);
	for (size_t k = 0; k < valid.size (); k ++) moving[valid[k]] = true;
	std::vector<int> order;
	for (int i = 0; i < to; i ++) if (! moving[i]) order.push_back (i);
	int at = (int) order.size ();
	order.insert (order.end (), valid.begin (), valid.end ());
	for (int i = to; i < sprites.total; i ++) if (! moving[i]) order.push_back (i);
	bool same = true;
	for (size_t n = 0; n < order.size (); n ++) same = same && order[n] == (int) n;
	if (! same) commands ().run (new SpriteOrder (this, valid.size () == 1 ? "Move Sprite" : "Move Sprites", order));
	return at;
}

int SpriteBankDocument::makeFont (int at, const std::string & file, int spaceWidth) {
	if (sprites.type < MODE_32BIT) {
		errorBox ("Can't make a font", "A font needs a 32-bit sprite bank. Convert the bank to 32-bit first.");
		return 0;
	}
	if (at < 0) at = 0;
	if (at > sprites.total) at = sprites.total;
	spriteBank picture;
	memset (&picture, 0, sizeof (picture));
	picture.type = MODE_32BIT;
	addSprite (0, &picture);
	if (! loadPicture (file, picture, 0)) {
		forgetSpriteBank (&picture);
		return 0;
	}
	doFontification (&picture, spaceWidth < 1 ? 1 : spaceWidth);
	int howMany = picture.total;
	if (sprites.total + howMany > MAX_SPRITES) howMany = MAX_SPRITES - sprites.total;
	std::vector<SpriteCopy> letters = copiesOf (picture, 0, howMany);
	forgetSpriteBank (&picture);
	PaletteCopy before = paletteOf (sprites);
	putCopies (sprites, at, letters);
	keepSplice ("Make Font", at, std::vector<SpriteCopy> (), letters, before);
	return howMany;
}

// The part of a picture from (x, y), width x height, as a sprite.
static SpriteCopy cutOut (const SpriteCopy & picture, int bytes, int x, int y, int width, int height) {
	SpriteCopy cell;
	cell.width = width;
	cell.height = height;
	cell.xhot = cell.yhot = 0;
	cell.data.resize ((size_t) width * height * bytes);
	for (int row = 0; row < height; row ++) {
		const unsigned char * from = &picture.data[((size_t) (y + row) * picture.width + x) * bytes];
		memcpy (&cell.data[(size_t) row * width * bytes], from, (size_t) width * bytes);
	}
	return cell;
}

// Whether a pixel shows: colour 0 is transparent with a palette, and alpha
// 0 without.
static bool shows (const SpriteCopy & cell, int bytes, int x, int y) {
	const unsigned char * p = &cell.data[((size_t) y * cell.width + x) * bytes];
	return bytes == 1 ? p[0] != 0 : p[3] != 0;
}

int SpriteBankDocument::insertSheet (int at, const std::string & file, const SheetLayout & layout) {
	if (layout.columns < 1 || layout.rows < 1) return 0;
	if (at < 0) at = 0;
	if (at > sprites.total) at = sprites.total;
	PaletteCopy before = paletteOf (sprites);
	// Load it as a sprite of the bank, so that it gets the colours of the
	// palette as inserted pictures do, and take it out again.
	addSprite (at, &sprites);
	if (! loadPicture (file, sprites, at)) {
		deleteSprite (at, &sprites);
		setPalette (sprites, before);
		return 0;
	}
	SpriteCopy picture = copyOf (sprites, at);
	deleteSprite (at, &sprites);
	spriteBankChanged (&sprites);
	if (picture.width % layout.columns || picture.height % layout.rows) {
		setPalette (sprites, before);
		char text[200];
		snprintf (text, sizeof (text), "The picture is %d \xC3\x97 %d pixels, which can't be cut into %d columns and %d rows of the same size.",
			picture.width, picture.height, layout.columns, layout.rows);
		errorBox ("Can't cut the sprite sheet", text);
		return 0;
	}
	int bytes = sprites.type < MODE_32BIT ? 1 : 4;
	int cellWidth = picture.width / layout.columns, cellHeight = picture.height / layout.rows;
	int cells = layout.columns * layout.rows;
	if (layout.count < cells) cells = layout.count;
	std::vector<SpriteCopy> frames;
	for (int c = 0; c < cells && sprites.total + (int) frames.size () < MAX_SPRITES; c ++) {
		SpriteCopy cell = cutOut (picture, bytes, (c % layout.columns) * cellWidth, (c / layout.columns) * cellHeight, cellWidth, cellHeight);
		cell.xhot = cellWidth / 2;
		cell.yhot = cellHeight - 1;
		// The box of the pixels that show.
		int left = cellWidth, top = cellHeight, right = -1, bottom = -1;
		for (int y = 0; y < cellHeight; y ++) {
			for (int x = 0; x < cellWidth; x ++) {
				if (! shows (cell, bytes, x, y)) continue;
				if (x < left) left = x;
				if (x > right) right = x;
				if (y < top) top = y;
				if (y > bottom) bottom = y;
			}
		}
		bool empty = right < 0;
		if (empty && layout.skipEmpty) continue;
		if (layout.trim && ! empty) {
			SpriteCopy trimmed = cutOut (cell, bytes, left, top, right - left + 1, bottom - top + 1);
			trimmed.xhot = cell.xhot - left;
			trimmed.yhot = cell.yhot - top;
			cell = trimmed;
		}
		frames.push_back (cell);
	}
	if (frames.empty ()) {
		setPalette (sprites, before);
		errorBox ("Can't cut the sprite sheet", "Every cell of the picture is transparent.");
		return 0;
	}
	putCopies (sprites, at, frames);
	keepSplice ("Import Sheet", at, std::vector<SpriteCopy> (), frames, before);
	return (int) frames.size ();
}

bool SpriteBankDocument::exportSprite (int index, const std::string & file, std::string * error) const {
	if (index < 0 || index >= sprites.total || sprites.sprites[index].width < 1 || sprites.sprites[index].height < 1) {
		*error = "The sprite has no picture.";
		return false;
	}
	const sprite & single = sprites.sprites[index];
	unsigned char * rgba = spritePixels (&sprites, index);
	png_image png;
	memset (&png, 0, sizeof (png));
	png.version = PNG_IMAGE_VERSION;
	png.width = single.width;
	png.height = single.height;
	png.format = PNG_FORMAT_RGBA;
	bool written = png_image_write_to_file (&png, file.c_str (), 0, rgba, 0, NULL) != 0;
	delete [] rgba;
	if (! written) *error = png.message[0] ? png.message : "The file can't be written.";
	return written;
}

std::vector<Hotspot> SpriteBankDocument::hotspots () const {
	std::vector<Hotspot> all;
	for (int i = 0; i < sprites.total; i ++) all.push_back (Hotspot (sprites.sprites[i].xhot, sprites.sprites[i].yhot));
	return all;
}

void SpriteBankDocument::setAllHotspots (const std::vector<Hotspot> & all) {
	for (int i = 0; i < sprites.total && i < (int) all.size (); i ++) {
		sprites.sprites[i].xhot = all[i].x;
		sprites.sprites[i].yhot = all[i].y;
	}
}

void SpriteBankDocument::changeHotspots (const std::vector<Hotspot> & after, const std::string & label, int mergeKey) {
	std::vector<Hotspot> before = hotspots ();
	if (after == before) return;
	commands ().run (new ValueCommand<std::vector<Hotspot> > (label, before, after,
		[this] (const std::vector<Hotspot> & all) { setAllHotspots (all); }, mergeKey));
}

void SpriteBankDocument::setHotspot (int index, Hotspot hotspot, int mergeKey) {
	if (index < 0 || index >= sprites.total) return;
	std::vector<Hotspot> after = hotspots ();
	after[index] = hotspot;
	changeHotspots (after, "Hotspot", mergeKey);
}

void SpriteBankDocument::setHotspots (const std::vector<int> & indices, Hotspot hotspot) {
	std::vector<Hotspot> after = hotspots ();
	std::vector<int> valid = validIndices (indices, sprites.total);
	for (size_t k = 0; k < valid.size (); k ++) after[valid[k]] = hotspot;
	changeHotspots (after, "Hotspots", 0);
}

void SpriteBankDocument::centreHotspots (const std::vector<int> & indices) {
	std::vector<Hotspot> after = hotspots ();
	std::vector<int> valid = validIndices (indices, sprites.total);
	for (size_t k = 0; k < valid.size (); k ++) {
		const sprite & single = sprites.sprites[valid[k]];
		after[valid[k]] = Hotspot (single.width / 2, single.height / 2);
	}
	changeHotspots (after, "Centre Hotspot", 0);
}

void SpriteBankDocument::baseHotspots (const std::vector<int> & indices) {
	std::vector<Hotspot> after = hotspots ();
	std::vector<int> valid = validIndices (indices, sprites.total);
	for (size_t k = 0; k < valid.size (); k ++) {
		const sprite & single = sprites.sprites[valid[k]];
		after[valid[k]] = Hotspot (single.width / 2, single.height - 1);
	}
	changeHotspots (after, "Hotspot at Base", 0);
}

bool SpriteBankDocument::canSetMode (Mode to) const {
	if (! sprites.total) return true;
	if (sprites.type < MODE_32BIT) return to < MODE_32BIT;
	return to == MODE_32BIT;
}

void SpriteBankDocument::setMode (Mode to) {
	if (! canSetMode (to) || to == mode ()) return;
	commands ().run (new ValueCommand<int> ("Mode", sprites.type, to, [this] (const int & m) {
		sprites.type = m;
		if (m < MODE_32BIT) ensurePalette (sprites);
		spriteBankChanged (&sprites);
	}));
}

bool SpriteBankDocument::convertTo32 () {
	if (sprites.type == MODE_32BIT) return true;
	if (! sprites.total) {
		setMode (MODE_32BIT);
		return true;
	}
	if (! convertSpriteBank8to32 (&sprites)) return false;
	commands ().clearAfterChange ();
	return true;
}

bool SpriteBankDocument::read (const std::string & path, std::string * error) {
	spriteBank loaded;
	memset (&loaded, 0, sizeof (loaded));
	// The errors go to errorBox.
	if (! loadSpriteBank (path.c_str (), &loaded)) {
		forgetSpriteBank (&loaded);
		error->clear ();
		return false;
	}
	forgetSpriteBank (&sprites);
	sprites = loaded;
	// As in the GTK editor, a palette bank opens with its palette locked.
	if (sprites.type < MODE_32BIT) {
		sprites.type = MODE_LOCKED_PALETTE;
		ensurePalette (sprites);
	}
	spriteBankChanged (&sprites);
	return true;
}

bool SpriteBankDocument::write (const std::string & path, std::string * error) {
	if (! sprites.total) {
		*error = "There are no sprites to save. Insert some pictures first.";
		return false;
	}
	if (! saveSpriteBank (path.c_str (), &sprites)) {
		*error = "The file can't be written.";
		return false;
	}
	return true;
}

} // namespace devkit
