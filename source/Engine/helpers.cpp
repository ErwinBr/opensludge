/*
 *  helpers.cpp
 *  Helper functions that don't depend on other source files.
 */

#include <stdio.h>
#include <stdint.h>
#include "helpers.h"

bool fileExists(const char * file) {
	FILE * tester;
	bool retval = false;
	tester = fopen (file, "rb");
	if (tester) {
		retval = true;
		fclose (tester);
	}
	return retval;
}

static uint64_t randomState = 1;

void seedRandom (unsigned int seed) {
	randomState = seed;
}

int getRandom (int n) {
	// A linear congruential generator with the constants of Knuth's MMIX.
	// The upper bits are the most random ones.
	randomState = randomState * 6364136223846793005ULL + 1442695040888963407ULL;
	return (int) ((randomState >> 33) % (uint64_t) n);
}
