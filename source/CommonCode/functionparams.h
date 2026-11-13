/*
This is how many parameters each built-in function of functionlist.h
takes, in the same order, or -1 for any number. The engine checks calls
with it (Engine/builtin.cpp), and the script editor of the Dev Kit warns of
calls that don't fit (DevKit/Core/ScriptCatalog.cpp).
*/

-1, 0, 1, 1, -1, -1, 1, 3, 4, 1, 0, 0, -1, -1,		// SAY -> MOVEMOUSE
-1, 0, 0, -1, -1, 1, 1, 1, 1, 4, 1, 1, 2, 1,// FOCUS -> REMOVEREGION
2, 2, 0, 0, 2,								// ANIMATE -> SETSCALE
-1, 2, 1, 0, 0, 0, 1, 0, 3, 				// new/push/pop stack, status stuff
2, 0, 0, 3, 1, 0, 2,						// delFromStack -> completeTimers
-1, -1, -1, 2, 2, 0, 3, 1, 					// anim, costume, pO, setC, wait, sS, substring, stringLength
0, 1, 1, 0, 2,  							// dark, save, load, quit, rename
1, 3, 3, 1, 2, 1, 1, 3, 1, 0, 0, 2, 1,		// stackSize, pasteString, startMusic, defvol, vol, stopmus, stopsound, setfont, alignStatus, show x 2, pos'Status, setFloor
-1, -1, 1, 1, 2, 1, 1, 1, -1, -1, -1, 1, 1,	// force, jump, peekstart, peekend, enqueue, getSavedGames, inFont, loopSound, removeChar, stopCharacter
1, 0, 3, 3, 1, 2, 1, 2, 2,					// launch, howFrozen, pastecol, litcol, checksaved, float, cancelfunc, walkspeed, delAll
2, 3, 1, 2, 2, 0, 0, 1, 2, 3, 1, -1,		// extras, mixoverlay, pastebloke, getMScreenX/Y, setSound(Default/-)Volume, looppoints, speechMode, setLightMap
-1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1,			// think, getCharacterDirection, is(char/region/moving), deleteGame, renameGame, hardScroll, stringWidth, speechSpeed, normalCharacter
2, 1, 2, 1, 3, 1, 1, 2, 1,					// fetchEvent, setBrightness, spin, fontSpace, burnString, captureAll, cacheSound, setSpinSpeed, transitionMode
1, 0, 0, 1, 0, 2, 1, 1, 1,					// movie(Start/Abort/Playing), updateDisplay, getSoundCache, savedata, loaddata, savemode, freeSound
3, 0, 3, 3, 2, 1, 1,						// setParallax, clearParallax, setBlankColour, setBurnColour, getPixelColour, makeFastArray, getCharacterScale
0, 2, 0,									// getLanguage, launchWith, getFramesPerSecond
3, 2, 2, 0, 0, 1,							// readThumbnail, setThumbnailSize, hasFlag, snapshot, clearSnapshot, anyFilename
2, 1,										// regGet, fatal
4, 3, -1, 0,								// chr AA, max AA, setBackgroundEffect, doBackgroundEffect
2,											// setCharacterAngleOffset
2, 5,										// setCharacterTransparency, setCharacterColourise
1,											// zoomCamera
1, 0, 0,									// playMovie, stopMovie, pauseMovie
-1, -1, 3, 3, 3, 2,							// newClip, clipSprite, clipFrames, clipEvent, setClipSpeed, isClipPlaying
0, -1, 3, 2, -1,							// newRig, addPart, setPartDepth, setCharacterRig, clipKey
-1, 2, 4, 3, 1,								// playClip, stopClip, setPartProperty, clearPartProperty, mirrorClip
4, 1, 1,									// setPartPivot, loadRig, loadClip
3, 2,										// watchScreenRegion, isInScreenRegion
2, 1										// setCharacterZ, clearCharacterZ
