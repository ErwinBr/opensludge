#ifndef OWNEDRIG_H
#define OWNEDRIG_H

/*
 * How a rig variable (SVT_RIG) holds its rig, as OwnedAnimation holds an
 * animation (see ownedanimation.h): variables are a union that the engine
 * copies with memcpy, so they can't hold a shared_ptr. An OwnedRig holds a
 * plain pointer to one, which it owns: each copy of the variable has a box
 * of its own, and the boxes share the rig.
 *
 * It has no constructor, so the pointer is only valid while the variable is
 * of type SVT_RIG. Only three functions of variable.cpp touch it:
 *  - rigOf reads it, and asserts that the variable is a rig;
 *  - setRig makes the variable a rig, and so must only be called on a
 *    variable that holds nothing to free (unlinked, or new);
 *  - freeRig frees the box, for unlinkVar.
 */

#include <memory>

struct RigDefinition;
struct variable;

typedef std::shared_ptr<const RigDefinition> RigDefinitionRef;	// As animmodel.h has it

class OwnedRig {
private:
	RigDefinitionRef * box;

	friend const RigDefinitionRef & rigOf (const variable & v);
	friend void setRig (variable & v, const RigDefinitionRef & rig);
	friend void freeRig (variable & v);
};

#endif
