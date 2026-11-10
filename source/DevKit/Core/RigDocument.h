#ifndef SLUDGE_DEVKIT_RIG_DOCUMENT_H
#define SLUDGE_DEVKIT_RIG_DOCUMENT_H

/*
 * A rig (.rig): the parts of a character, what each is attached to, and
 * its rest pose, as the character creator edits it. The rig is the very
 * RigDefinition that the engine plays (source/Anim/animmodel.h), in a
 * project asset (animformat.h): with an identity, which stays the same
 * whatever the file is called, and the paths of its sprite banks, from the
 * project's folder.
 *
 * Every change is a command that undoes. Parts are named by their IDs,
 * which stay the same as parts are added, removed and moved; the functions
 * that can fail change nothing then, and say why.
 */

#include <functional>
#include <string>
#include <vector>

#include "Document.h"
#include "animformat.h"

namespace devkit {

class ClipDocument;

class RigDocument : public Document {
public:
	static DocumentType type ();
	// A new rig, of the root only, with a new identity.
	RigDocument ();

	const char * kind () const { return "Rig"; }
	std::string untitledName () const { return "Untitled Rig.rig"; }

	const animformat::Asset & asset () const { return state; }
	const RigDefinition & rig () const { return state.rig; }
	// The part of an ID, or NULL.
	const RigPart * part (PartId id) const;
	// The path of the bank of a sprite, or empty for none.
	std::string bankOf (const SpriteRef & sprite) const;
	// A name that no part has, from base: base, then base 2, base 3...
	std::string freeName (const std::string & base) const;

	bool addPart (PartId parent, const std::string & name, PartId * added, std::string & why);
	bool renamePart (PartId part, const std::string & name, std::string & why);
	// The same, and the part's tracks in clips: those made for this rig, which
	// the caller has opened. It is one change: undo and redo take the clips'
	// renames with them, as long as each is the last change of its clip,
	// and its clip is still open (stillOpen). It fails, changing nothing,
	// when a clip has tracks of the new name. renamed gets the clips that
	// changed.
	typedef std::function<bool (ClipDocument *)> StillOpen;
	bool renamePart (PartId part, const std::string & name, const std::vector<ClipDocument *> & clips,
		StillOpen stillOpen, std::vector<ClipDocument *> * renamed, std::string & why);
	// With every part attached to it.
	bool removePart (PartId part, std::string & why);
	// Under parent, at position among its other parts (see animedit.h).
	bool placePart (PartId part, PartId parent, int position, std::string & why);

	// Commands with the same mergeKey (other than 0) merge, as in a drag.
	void setRest (PartId part, const PartPose & rest, int mergeKey = 0);
	void setDepth (PartId part, int depth, int mergeKey = 0);
	// Its pivot, from its sprite's hotspot; with keepInPlace, the sprite and
	// the parts attached to it stay where they are (movePivot).
	void setPivot (PartId part, float x, float y, bool keepInPlace, int mergeKey = 0);
	// Sprite number of the bank at path (from the project's folder), or no
	// sprite (a joint) with an empty path.
	void setSprite (PartId part, const std::string & bank, int number, bool mirror);

	// How the editor shows a part, saved with the rig and undone like any
	// change, though games don't use it. A locked part can't be selected
	// or changed on the canvas. Its X and Y scale together (uniformly) by
	// default; making them so again gives Y the scale of X.
	bool isLocked (PartId part) const { return state.editorPart (part).locked; }
	bool scalesUniformly (PartId part) const { return state.editorPart (part).scaling != animformat::EditorPart::SCALING_FREE; }
	void setLocked (PartId part, bool locked);
	void setUniformScale (PartId part, bool uniform);

protected:
	bool read (const std::string & path, std::string * error);
	bool write (const std::string & path, std::string * error);

private:
	void change (const std::string & label, const animformat::Asset & after, int mergeKey = 0);
	std::string labelFor (const char * what, PartId part) const;

	animformat::Asset state;
};

} // namespace devkit

#endif
