#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "allfiles.h"
#include "sprites.h"
#include "sprbanks.h"
#include "newfatal.h"
#include "debug.h"

namespace {

struct Entry {
	SpriteBankRef bank;
	uint64_t lastUsed;		// The frame in which an animation last used it
	size_t gpuBytes;
};

std::unordered_map<int, Entry> allLoadedBanks;
size_t budget = (size_t) 128 * 1024 * 1024;
uint64_t frame = 0;
uint64_t hits = 0, misses = 0, evictions = 0;

// With SLUDGE_SPRITEBANK_LOG set, we write what the cache does on stderr.
bool logging () {
	static int on = -1;
	if (on < 0) on = getenv ("SLUDGE_SPRITEBANK_LOG") != NULL;
	return on != 0;
}

void logStats (const char * what, int ID) {
	if (! logging ()) return;
	SpriteBankStats s = spriteBankStats ();
	fprintf (stderr, "Sprite banks: %s %d; %d loaded, %d in use, %d unused; %llu KB, %llu KB unused (budget %llu KB); %llu hits, %llu misses, %llu evictions\n",
		what, ID, s.loaded, s.inUse, s.unused, (unsigned long long) s.gpuBytes / 1024, (unsigned long long) s.unusedGpuBytes / 1024, (unsigned long long) budget / 1024,
		(unsigned long long) s.hits, (unsigned long long) s.misses, (unsigned long long) s.evictions);
}

bool isUnused (const Entry & e) {
	return e.bank.use_count () == 1;
}

// Unload unused banks, the longest unused first, until those left take at
// most keep bytes.
void evictUnused (size_t keep) {
	std::vector<std::pair<uint64_t, int> > unused;
	size_t unusedBytes = 0;
	for (std::unordered_map<int, Entry>::iterator i = allLoadedBanks.begin (); i != allLoadedBanks.end (); ++ i) {
		if (isUnused (i->second)) {
			unused.push_back (std::make_pair (i->second.lastUsed, i->first));
			unusedBytes += i->second.gpuBytes;
		}
	}
	if (unusedBytes <= keep) return;
	std::sort (unused.begin (), unused.end ());
	for (size_t i = 0; i < unused.size () && unusedBytes > keep; i ++) {
		std::unordered_map<int, Entry>::iterator e = allLoadedBanks.find (unused[i].second);
		unusedBytes -= e->second.gpuBytes;
		// The textures go with releaseTexture, once the GPU is done with them.
		allLoadedBanks.erase (e);
		evictions ++;
		logStats ("unloaded", unused[i].second);
	}
}

}

SpriteBankRef loadBankForAnim (int ID) {
	std::unordered_map<int, Entry>::iterator found = allLoadedBanks.find (ID);
	if (found != allLoadedBanks.end ()) {
		found->second.lastUsed = frame;
		hits ++;
		return found->second.bank;
	}

	SpriteBankRef bank = std::make_shared<loadedSpriteBank> ();
	bank->ID = ID;
	if (! loadSpriteBank (ID, bank->bank, false)) {
		debugOut ("loadBankForAnim: I guess I couldn't load the sprites...\n");
		return SpriteBankRef ();
	}
	misses ++;
	Entry e = { bank, frame, bank->bank.gpuBytes () };
	allLoadedBanks[ID] = e;
	debugOut ("loadBankForAnim: New sprite bank created OK\n");
	logStats ("loaded", ID);
	return bank;
}

void trimSpriteBanks () {
	frame ++;
	for (std::unordered_map<int, Entry>::iterator i = allLoadedBanks.begin (); i != allLoadedBanks.end (); ++ i) {
		if (! isUnused (i->second)) i->second.lastUsed = frame;
	}
	evictUnused (budget);
}

void setSpriteBankCacheBudget (size_t bytes) {
	budget = bytes;
}

size_t spriteBankCacheBudget () {
	return budget;
}

void forgetUnusedSpriteBanks () {
	evictUnused (0);
}

SpriteBankStats spriteBankStats () {
	SpriteBankStats s = { 0, 0, 0, 0, 0, hits, misses, evictions };
	for (std::unordered_map<int, Entry>::const_iterator i = allLoadedBanks.begin (); i != allLoadedBanks.end (); ++ i) {
		s.loaded ++;
		s.gpuBytes += i->second.gpuBytes;
		if (isUnused (i->second)) {
			s.unused ++;
			s.unusedGpuBytes += i->second.gpuBytes;
		} else {
			s.inUse ++;
		}
	}
	return s;
}
