#include <stdio.h>

#include "RigDocument.h"
#include "animedit.h"

namespace devkit {

namespace {

// Keep only the banks that sprites use, in the order they are first used.
void compactBanks (animformat::Asset & a) {
	std::vector<std::string> used;
	std::vector<int> newIndex (a.banks.size (), -1);
	for (size_t i = 0; i < a.rig.parts.size (); i ++) {
		SpriteRef & s = a.rig.parts[i].sprite;
		if (s.kind != SpriteRef::SPRITE || s.bankFile < 0 || (size_t) s.bankFile >= a.banks.size ()) continue;
		if (newIndex[s.bankFile] < 0) {
			newIndex[s.bankFile] = (int) used.size ();
			used.push_back (a.banks[s.bankFile]);
		}
		s.bankFile = newIndex[s.bankFile];
	}
	a.banks = used;
}

bool fail (std::string & why, const char * what) {
	why = what;
	return false;
}

}

DocumentType RigDocument::type () {
	DocumentType type;
	type.name = "Rig";
	type.extensions.push_back ("rig");
	type.create = [] () -> Document * { return new RigDocument; };
	return type;
}

RigDocument::RigDocument () {
	state.kind = animformat::Asset::RIG;
	state.id = animformat::newAssetId ();
}

const RigPart * RigDocument::part (PartId id) const {
	int i = partWithId (state.rig, id);
	return i < 0 ? NULL : & state.rig.parts[i];
}

std::string RigDocument::bankOf (const SpriteRef & sprite) const {
	if (sprite.kind != SpriteRef::SPRITE || sprite.bankFile < 0 || (size_t) sprite.bankFile >= state.banks.size ()) return std::string ();
	return state.banks[sprite.bankFile];
}

std::string RigDocument::freeName (const std::string & base) const {
	if (state.rig.findPart (base) < 0) return base;
	for (int n = 2; ; n ++) {
		char name[32];
		snprintf (name, sizeof (name), " %d", n);
		if (state.rig.findPart (base + name) < 0) return base + name;
	}
}

std::string RigDocument::labelFor (const char * what, PartId id) const {
	const RigPart * p = part (id);
	if (! p || p -> name.empty ()) return std::string (what) + " of the Character";
	return std::string (what) + " of " + p -> name;
}

void RigDocument::change (const std::string & label, const animformat::Asset & after, int mergeKey) {
	commands ().run (new ValueCommand<animformat::Asset> (label, state, after,
		[this] (const animformat::Asset & a) { state = a; }, mergeKey));
}

bool RigDocument::addPart (PartId parent, const std::string & name, PartId * added, std::string & why) {
	const RigPart * above = part (parent);
	if (! above) return fail (why, "There is no such part to attach it to");
	animformat::Asset after = state;
	if (! ::addPart (after.rig, name, above -> name, PartPose (), SpriteRef (), why)) return false;
	if (added) * added = after.rig.parts.back ().id;
	change ("Add Part", after);
	return true;
}

bool RigDocument::renamePart (PartId id, const std::string & name, std::string & why) {
	animformat::Asset after = state;
	if (! ::renamePart (after.rig, partWithId (state.rig, id), name, why)) return false;
	if (part (id) -> name == name) return true;
	change ("Rename Part", after);
	return true;
}

bool RigDocument::removePart (PartId id, std::string & why) {
	animformat::Asset after = state;
	std::string label = labelFor ("Removal", id);
	if (! ::removePart (after.rig, partWithId (state.rig, id), why)) return false;
	compactBanks (after);
	change (label, after);
	return true;
}

bool RigDocument::placePart (PartId id, PartId parent, int position, std::string & why) {
	animformat::Asset after = state;
	if (! ::placePart (after.rig, partWithId (state.rig, id), partWithId (state.rig, parent), position, why)) return false;
	change (labelFor ("Place", id), after);
	return true;
}

void RigDocument::setRest (PartId id, const PartPose & rest, int mergeKey) {
	int i = partWithId (state.rig, id);
	if (i < 0) return;
	animformat::Asset after = state;
	after.rig.parts[i].rest = rest;
	std::string why;
	if (! validRig (after.rig, why)) return;
	change (labelFor ("Rest Pose", id), after, mergeKey);
}

void RigDocument::setDepth (PartId id, int depth, int mergeKey) {
	int i = partWithId (state.rig, id);
	if (i < 0 || state.rig.parts[i].depth == depth) return;
	animformat::Asset after = state;
	after.rig.parts[i].depth = depth;
	change (labelFor ("Depth", id), after, mergeKey);
}

void RigDocument::setPivot (PartId id, float x, float y, bool keepInPlace, int mergeKey) {
	int i = partWithId (state.rig, id);
	if (i < 0 || (state.rig.parts[i].pivotX == x && state.rig.parts[i].pivotY == y)) return;
	animformat::Asset after = state;
	if (keepInPlace) {
		movePivot (after.rig, i, x, y);
	} else {
		after.rig.parts[i].pivotX = x;
		after.rig.parts[i].pivotY = y;
	}
	std::string why;
	if (! validRig (after.rig, why)) return;
	change (labelFor ("Pivot", id), after, mergeKey);
}

void RigDocument::setSprite (PartId id, const std::string & bank, int number, bool mirror) {
	int i = partWithId (state.rig, id);
	if (i < 0 || number < 0) return;
	animformat::Asset after = state;
	SpriteRef & s = after.rig.parts[i].sprite;
	if (bank.empty ()) {
		s = SpriteRef ();
	} else {
		int index = -1;
		for (size_t b = 0; b < after.banks.size (); b ++) {
			if (after.banks[b] == bank) index = (int) b;
		}
		if (index < 0) {
			index = (int) after.banks.size ();
			after.banks.push_back (bank);
		}
		s = SpriteRef::sprite (AnimBankRef (), index, number, mirror);
	}
	compactBanks (after);
	const SpriteRef & now = state.rig.parts[i].sprite;
	if (bankOf (now) == bank && (bank.empty () || (now.number == number && now.mirror == mirror))) return;
	change (labelFor ("Sprite", id), after);
}

bool RigDocument::read (const std::string & path, std::string * error) {
	FILE * f = fopen (path.c_str (), "rb");
	if (! f) {
		* error = "The file can't be opened.";
		return false;
	}
	animformat::Bytes bytes;
	unsigned char buffer[4096];
	size_t n;
	while ((n = fread (buffer, 1, sizeof (buffer), f)) > 0) bytes.insert (bytes.end (), buffer, buffer + n);
	fclose (f);
	animformat::Asset loaded;
	std::string why;
	if (! animformat::decodeAsset (bytes.data (), bytes.size (), loaded, why)) {
		* error = "It isn't a rig that can be read: " + why + ".";
		return false;
	}
	if (loaded.kind != animformat::Asset::RIG) {
		* error = "It is a clip, not a rig.";
		return false;
	}
	state = loaded;
	return true;
}

bool RigDocument::write (const std::string & path, std::string * error) {
	animformat::Bytes bytes;
	std::string why;
	if (! animformat::encodeAsset (state, bytes, why)) {
		* error = "The rig can't be saved: " + why + ".";
		return false;
	}
	FILE * f = fopen (path.c_str (), "wb");
	bool ok = f && fwrite (bytes.data (), 1, bytes.size (), f) == bytes.size ();
	if (f && fclose (f)) ok = false;
	if (! ok) * error = "The file can't be written.";
	return ok;
}

} // namespace devkit
