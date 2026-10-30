/*
 *  helpers.h
 *  Helper functions that don't depend on other source files.
 */

bool fileExists(const char * file);

// The random numbers of the game, from 0 to n - 1. We don't use rand,
// because system libraries can call it too, which would make two test runs
// with the same seed differ.
void seedRandom (unsigned int seed);
int getRandom (int n);
