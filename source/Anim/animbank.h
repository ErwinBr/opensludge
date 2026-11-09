#ifndef ANIMBANK_H
#define ANIMBANK_H

/*
 * A sprite bank, as the animation library sees one: how many sprites it
 * has, and each one's size and hotspot. That is all a pose needs of a bank.
 *
 * The library loads no banks and knows no bank's type: the engine and the
 * Dev Kit each derive their own banks from AnimSpriteBank, and load them
 * for resolveBanks (animmodel.h). The library depends on nothing but the
 * C++ standard library, and must stay that way: the engine, the compiler
 * and the Dev Kit all build it.
 */

#include <memory>

struct AnimSpriteSize {
	int width, height;
	int xhot, yhot;		// The hotspot, from the sprite's top left
};

class AnimSpriteBank {
public:
	virtual ~AnimSpriteBank () {}
	virtual int animSpriteCount () const = 0;
	// For 0 <= number < animSpriteCount ().
	virtual AnimSpriteSize animSpriteSize (int number) const = 0;
};

typedef std::shared_ptr<AnimSpriteBank> AnimBankRef;

#endif
