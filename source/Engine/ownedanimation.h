#ifndef OWNEDANIMATION_H
#define OWNEDANIMATION_H

/*
 * How an animation handle (personaAnimation, see animation.h) is owned.
 *
 * AnimationHandle owns one, wherever a handle is made, given or taken:
 * copyAnim, getAnimationFromVar, animatePerson, the mouse cursor. Costumes,
 * characters (ownAnim), the cursor and the freezer keep theirs in one too.
 *
 * Variables can't: they are a union that the engine copies with memcpy (see
 * addVarToStackQuick). An animation variable keeps its handle in an
 * OwnedAnimation instead: a plain pointer, which a union can hold.
 *
 * It has no constructor, so the pointer is only valid while the variable is
 * of type SVT_ANIM: whatever the union held before is in its place. Only
 * three functions of variable.cpp can touch it, and they keep that rule:
 *  - animationOf reads it, and asserts that the variable is an animation;
 *  - setAnimation makes the variable an animation and gives it a handle,
 *    and so must only be called on a variable that holds nothing to free
 *    (unlinked, or new);
 *  - freeAnimation frees it, for unlinkVar, and asserts the type too.
 */

#include <memory>

struct personaAnimation;
struct variable;

typedef std::unique_ptr<personaAnimation> AnimationHandle;

class OwnedAnimation {
private:
	personaAnimation * handle;

	// Take ownership. The variable must not hold a handle already.
	void adopt (AnimationHandle h);
	// Give ownership back, and hold nothing.
	AnimationHandle release ();

	friend const personaAnimation * animationOf (const variable & v);
	friend void setAnimation (variable & v, AnimationHandle h);
	friend void freeAnimation (variable & v);
};

#endif
