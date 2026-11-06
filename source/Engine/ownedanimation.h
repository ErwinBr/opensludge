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
 * OwnedAnimation instead: a plain pointer that only adopt can set and only
 * release can take back, as an AnimationHandle, which frees it when it goes.
 * unlinkVar releases it; nothing else frees it.
 */

#include <memory>

struct personaAnimation;

typedef std::unique_ptr<personaAnimation> AnimationHandle;

class OwnedAnimation {
public:
	// Take ownership. The variable must not hold a handle already.
	void adopt (AnimationHandle handle);
	// Give ownership back, and hold nothing.
	AnimationHandle release ();
	const personaAnimation * get () const { return handle; }

private:
	personaAnimation * handle;
};

#endif
