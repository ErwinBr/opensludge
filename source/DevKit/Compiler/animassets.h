#ifndef ANIMASSETS_H
#define ANIMASSETS_H

/*
 * The rigs (.rig) and clips (.clp) of a project, as the Dev Kit saves them
 * (assets: see animformat.h). A game has no paths or names, so the
 * compiler packs each as a file of rigs and clips with the numbers of the
 * game's files and functions.
 *
 * It works in three steps, because the numbers come at different times:
 *  - registerAnimAsset, when the preprocessor meets an asset's name in a
 *    script: it reads the asset and adds its sprite banks and sounds to the
 *    files of the game, before their count is fixed. A bank or sound that
 *    several assets (or scripts) use is one file of the game.
 *  - resolveAnimAssetFunctions, once every script is compiled: the numbers
 *    of the functions that clips' events call.
 *  - packAnimAsset, as the files are put in the game: the asset's bytes.
 *
 * Every message names the asset, with the script and line that use it.
 */

#include <string>
#include <vector>

struct stringArray;

bool isAnimAssetName (const char * name);

// Forget the assets of the last compilation.
void resetAnimAssets ();

// False after an error message: an asset that doesn't exist or isn't whole,
// or a bank or sound of it that doesn't exist.
bool registerAnimAsset (const char * name, stringArray * & fileHandles, const char * scriptFile, unsigned int line);

// False after an error message: an event that calls a function the project
// doesn't have.
bool resolveAnimAssetFunctions (stringArray * functionNames);

// The bytes that the game holds for the asset of name (a file's name as the
// game's list of files has it). False after an error message.
bool packAnimAsset (const char * name, std::vector<unsigned char> & out);

#endif
