#ifndef SPRBANKS_H
#define SPRBANKS_H

/*
 * The sprite banks that animations use. Each animation holds a reference to
 * its bank, so a bank lives as long as an animation uses it. The cache keeps
 * a reference too: a bank that no animation uses stays loaded while the
 * unused banks fit in the budget, because games often make the same
 * animation again and again. Once a frame, trimSpriteBanks unloads the
 * unused banks that have been unused the longest, until they fit.
 *
 * Saved games refer to a bank by its file number, ID.
 */

#include <memory>
#include <stddef.h>
#include <stdint.h>

#include "animbank.h"
#include "sprites.h"

// A bank, as the animation library sees it too (AnimSpriteBank).
struct loadedSpriteBank : public AnimSpriteBank {
	int ID;
	spriteBank bank;

	loadedSpriteBank () : ID (0) {}
	int animSpriteCount () const { return bank.total; }
	AnimSpriteSize animSpriteSize (int number) const {
		const sprite & s = bank.sprites[number];
		AnimSpriteSize size = { s.width, s.height, s.xhot, s.yhot };
		return size;
	}
};

typedef std::shared_ptr<loadedSpriteBank> SpriteBankRef;

// The engine's bank behind a bank of the animation library. Every bank that
// the engine gives the library is a loadedSpriteBank.
inline loadedSpriteBank * engineBank (const AnimBankRef & bank) {
	return static_cast<loadedSpriteBank *> (bank.get ());
}

// The bank of file ID, loaded if it isn't yet. NULL when it can't be
// loaded; fatal has been called then.
SpriteBankRef loadBankForAnim (int ID);

// Unload unused banks over the budget. Call it once a frame, between frames.
void trimSpriteBanks ();
// The most GPU memory that banks that no animation uses may keep.
void setSpriteBankCacheBudget (size_t bytes);
size_t spriteBankCacheBudget ();
// Unload every unused bank, whatever the budget.
void forgetUnusedSpriteBanks ();

struct SpriteBankStats {
	int loaded;					// Banks in the cache
	int inUse;					// ... that an animation uses
	int unused;					// ... that only the cache keeps
	size_t gpuBytes;			// Of the textures of all of them
	size_t unusedGpuBytes;		// Of the unused banks
	uint64_t hits, misses, evictions;
};

SpriteBankStats spriteBankStats ();

#endif
