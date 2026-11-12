// The docs of the built-ins, from doc/SLUDGEDevKitHelp, which
// tests/devkit/make_script_catalog.cpp wrote: don't change this file,
// but the help, and run that again.
//
// FUNCTION_DOC (name, page, syntax (forms between \n), purpose, return value)
// CONSTANT_DOC (name, heading, what it is for)

FUNCTION_DOC ("addCharacter", "addCharacter.html",
	"addCharacter (thisObject, atX, atY, thisCostume)",
	"Adds a character on the screen which represents thisObject. The character can then be animated using the animate function, moved using moveCharacter and forceCharacter and so on. There is no specific character data type; the character can be controlled by referencing the object type thisObject which the character represents.\n\nThe thisCostume parameter must be a complete costume (the result of a call to the costume function) or a single animation (the result of a call to the anim function).",
	"No return value.")
FUNCTION_DOC ("addOverlay", "addOverlay.html",
	"addOverlay (thisFile, atX, atY)",
	"Loads an image from the file handle thisFile and overlays it onto the screen. Bright magenta (255, 0, 255 or #FF00FF) is used as the transparency colour (except for 32-bit images, where the alpha channel is used!), making it possible to add irregular shapes.\n\nThe values atX and atY define the top left corner of the area into which the image will be added. If the value of atX is CENTRE (or CENTER) the image will be added halfway across the window. If the value of atY is CENTRE (or CENTER) the image will be added halfway down the window.",
	"No return value.")
FUNCTION_DOC ("addPart", "addPart.html",
	"addPart (rig, name, parent, x, y)\naddPart (rig, name, parent, x, y, spriteBank, sprite)",
	"Adds a part called name to rig, hanging from the part called parent, and showing sprite from spriteBank.\n\nUse \"\" as the parent to hang the part from the character itself. The values x and y say where the part's hotspot goes, measured from its parent's hotspot (or from its pivot, if you give it one with setPartPivot). When the parent moves, turns or scales, the part goes with it, just as a hand goes wherever the arm takes it.\n\nspriteBank is a sprite bank file, in single quotes as always, such as 'head.duc'. The part shows that sprite until a clip gives it another one. A negative sprite shows the mirror image of that sprite.\n\nLeave out the sprite bank and the sprite, and the part is an empty joint: it shows nothing, but the parts hanging from it go where it goes, and a clip on a layer can give it a sprite later. That's handy for whatever a character holds in its hand (see the example).\n\nEvery part needs a name of its own, and its parent has to be in the rig already, otherwise there's nothing to hang it from. Get either of these wrong and the game stops with an error. You can't add parts to a rig once a character uses it, so build the whole rig before you call setCharacterRig.",
	"No return value.")
FUNCTION_DOC ("addScreenRegion", "addScreenRegion.html",
	"addScreenRegion (thisObject, left, top, right, bottom, sX, sY, direction)\naddScreenRegion (thisObject, points, sX, sY, direction)",
	"Adds a region on the screen which represents thisObject: either a rectangle, or a shape of your own made from a list of points.\n\nWith four co-ordinates, the region is the rectangle from (left, top) to (right, bottom) inclusive.\n\nNot everything in a scene is a rectangle, though. A rectangle around a lamp post either misses the top of the lamp or takes in half the sky with it. So instead of the four co-ordinates you can give points, an array (a stack, or a fast array made with makeFastArray) of numbers: the x and y of each corner of the shape in turn, such as newStack (100, 300, 140, 120, 180, 300) for a triangle. The shape needs at least 3 corners, so at least 6 numbers, and an even count of them, or the game stops with an error. You don't need to repeat the first corner at the end: the last corner joins up with the first one by itself. The region is everything inside the shape. If the edges cross each other, the parts that are inside twice count as outside.\n\nFor everything else SLUDGE treats a shaped region as the smallest rectangle around it: characters talking for thisObject speak from the top middle of that rectangle, just as they do for a rectangular region.\n\nIf a character is told to move to thisObject using either of the forceCharacter or moveCharacter commands, the position at which the character should try and stand is (sX, sY). If direction is -1, the character will be left facing in whatever direction it was last walking. Otherwise, the character will be turned to face direction (which can either be an angle, in degrees, or one of the enumerations NORTH, NORTHEAST, EAST, SOUTHEAST, SOUTH, SOUTHWEST, WEST or NORTHWEST).\n\nIf characters should not move when told to walk to a certain object, set both sX and sY to 0. The value of direction is not used in this instance, although it must still be provided.",
	"No return value.")
FUNCTION_DOC ("addStatus", "addStatus.html",
	"addStatus ()",
	"Adds a new status bar containing no text.",
	"No return value.")
FUNCTION_DOC ("aimCamera", "aimCamera.html",
	"aimCamera (x, y)",
	"Aims the camera so that the point (x, y) is in the centre of the window. If it is not possible to position the camera so that the point is in the centre of the window without the user seeing an area outside of the scene image, the camera is moved to the closest possible valid position.",
	"No return value.")
FUNCTION_DOC ("alignStatus", "alignStatus.html",
	"alignStatus (alignment)",
	"Changes alignment of status bar text. Valid values for alignment are LEFT, CENTRE (or CENTER) and RIGHT.",
	"No return value.")
FUNCTION_DOC ("anim", "anim.html",
	"anim (spriteBank, frame [, frame [, frame...]])",
	"Creates an animation using the sprites in the spriteBank file. There must be at least one frame specified.\n\nEach frame must be either a number (in which case the sprite will appear for one screen update only) or the return value from the wait function (which can be used to specify that a sprite should appear for longer than one screen update).\n\nValid values for numerical frame parameters (or the first parameter in a call to the wait function) are:\n\nNegative number outside range\n\nHorizontally flipped sprite number 0 from spriteBank\n\nNegative number in range\n\nHorizontally flipped sprite -frame from spriteBank\n\nPositive number in range\n\nSprite number frame from spriteBank\n\nPositive number outside range\n\nNothing\n\n(The range of valid numbers is 0 to the number of sprites in spriteBank minus 1.)\n\nSounds and functions\n\nSound files and functions may also be put in the list of frames. When a sound file or function is encountered, it is played or called, and then the animation moves to the next frame immediately. Do not put more than one sound or function in a row: Each sound or function must be separated by a frame number (or the wait function).",
	"The return value is the animation which has been created. This can then be used as part of a costume or applied to a character using the animate function.")
FUNCTION_DOC ("animate", "animate.html",
	"animate (character, animation)",
	"Makes the specified character perform the selected animation which must be created with the anim function, or be a clip of newClip. The character will continue performing the animation until told to move, change direction or speak.\n\nThe animate function can be used, for example, to provide different speaking animations without having to build a new costume.\n\nanimate (ego, chinHitsFloorAnimation); think (ego, \"WHAT?\"); say (ego, \"Are you kidding?\");\n\nThe above example will give the ego character a different animation while it thinks the line \"WHAT?\" - the ego will then use its standard talking animation to say the like \"Are you kidding?\".",
	"The return value is the total playing time of animation. Consequently, the following code will perform an animation once and once only:\n\nvar a = anim ('pickNose.duc', 0, wait (1, 4), 2, 3, 4); pause (animate (ego, a)); normalCharacter (ego);\n\nIn the above example, the return value of the animate function is 8... 1 + 4 + 1 + 1 + 1. So, the pause function stops the function for 8 frames: just long enough for the animation to run.\n\nFor a clip, the return value is its length.")
FUNCTION_DOC ("blankArea", "blankArea.html",
	"blankArea (left, top, right, bottom)",
	"Clears a rectangular area of the screen, from (left, top) to (right, bottom) inclusive. You can also think of the parameters as x1, y1, x2 and y2 respectively. Each pixel in the area is set to the colour most recently set using setBlankColour.",
	"No return value.")
FUNCTION_DOC ("blankScreen", "blankScreen.html",
	"blankScreen ()",
	"Clears the screen, using the colour most recently set using setBlankColour.",
	"No return value.")
FUNCTION_DOC ("bodgeFilenames", "bodgeFilenames.html",
	"bodgeFilenames (enable)",
	"Turns on and off the fantastic SLUDGE filename bodging feature. The feature affects the following functions:\n\nloadCustomData and saveCustomData\n\nlaunch\n\nloadGame\n\nsaveGame\n\nshowThumbnail\n\ndeleteFile\n\nrenameFile\n\ngetMatchingFiles\n\nfileExists\n\nThe enable value turns filename bodging on and off. With filename bodging disabled - as it is desabled by default - the above functions work as expected, and the parameters they take (and return, in some cases) have to be nice, normal filenames. That means the characters / and \\ mean a file is in another folder, the character : means a file is on another drive and certain symbols like ? and * are just plain disallowed.\n\nWith filename bodging enabled, however, these special characters are all allowed in filenames. This makes it possible for people playing your game to save their games with names like 'Where am I?' and '5: In the jungle' and 'Oh ****' and even names containing multiple odd characters like '8: \"776010\"? Is this good/bad?'.\n\nThe way the system works is to change each illegal or special character into a code before operating on the file (or in the case of getMatchingFiles change each code back into the character it represents, so that in a list of saved games each file is listed with its original name).\n\nYou may have to enable filename bodging in certain parts of your game (for example, letting your player load and save) and disable it in others (for example if you're going to launch a URL, or a help file in a different directory).",
	"The return value is the previous status of the filename bodging option - i.e. if filename bodging was previously enabled the return value is TRUE, otherwise it's FALSE. Hence, the following code will turn off filename bodging, launch a file and then put filename bodging back to whatever state it was in before:\n\nvar old_on_or_off = bodgeFilenames (FALSE); launch (\"manual\\\\welcome.html\"); bodgeFilenames (old_on_or_off);")
FUNCTION_DOC ("burnString", "pasteString_and_burnString.html",
	"burnString (x, y, thisString)",
	"Adds text to the background image. The parameter thisString is added to the screen at the position (x, y).\n\nIf the value of x is CENTRE (or CENTER) the text is added halfway across the screen. Otherwise, the x value is used as the horizontal position of the hotspot of the first character of thisString. The y parameter is always used as the vertical position of the hotspot of every character of thisString - the enumeration CENTRE (or CENTER) shouldn't be used as the y parameter. If it is, the text will not appear on the screen.\n\nBoth pasteString and burnString use the font most recently loaded by the setFont command. pasteString uses the colour specified by the setPasteColour (or setPasteColor) command. burnString adds text in the colour specified by setBurnColour (or setBurnColor), using the differing brightness levels of the font to achieve an anti-aliasing effect.",
	"No return value.")
FUNCTION_DOC ("cacheSound", "cacheSound.html",
	"cacheSound (thisSoundFile)",
	"Loads (and, if required, decompresses) a sound into memory without playing it. The sound can then be played later without needing to load and decompress it at the time. If the parameter isn't a file handle the code has no effect.",
	"No return value.")
FUNCTION_DOC ("callEvent", "callEvent.html",
	"callEvent (thisAction, thisObject)",
	"Calls the function specified inside the thisObject objectType definition by the line \"event thisAction\". Both thisAction and thisObject must be variables of type objectType.",
	"TRUE if a function was called. FALSE if no event was found.")
FUNCTION_DOC ("cancelSub", "cancelSub.html",
	"cancelSub (thisFunction)",
	"Cancels any instances of thisFunction which are running.\n\nThis command will only cancel a function which has been paused by either the pause or one of the say and think commands, and will not cancel a function which is temporarily inactive because of a call to another user-defined function. For example:\n\nsub outer () { loop { say (a, \"Outer.\"); inner (); } } sub inner () { say (a, \"Inner.\"); } sub clicky () { cancelSub (outer); } sub init () { spawnSub (outer); onLeftMouse (clicky); }\n\nIn the above example, pressing the left mouse button will only cancel the loop if the character a is saying \"Outer\" and not \"Inner\"; i.e. while the outer function is active.\n\nThe cancelSub command will also not cancel a function which is suspended because of a character which is currently moving (because of a call to either the moveCharacter or forceCharacter function). In these situations you can use stopCharacter to stop the character in its tracks, automatically cancelling the suspended function. Several other commands will also stop the movement of the character, thereby cancelling the function which was suspended because the character was moving.",
	"This function returns the number of instances of thisFunction which were cancelled.")
FUNCTION_DOC ("captureAllKeys", "captureAllKeys.html",
	"captureAllKeys (onOff)",
	"Turns on and off capturing of all keypresses, including Alt, Shift, Caps Lock, Scroll Lock and so on. Whenever a keypress is detected, your current keyboard handler is called (as specified by the onKeyboard function).\n\nPassing in a Boolean value of FALSE turns off capture of these extended keys... passing in TRUE turns capture of every single keypress on. By default, capture of all keys is off.\n\nIf extended key capture is on, whenever an extended key is pressed your keyboard handler function will be passed a string containing a 4-digit number, representing the key that has been pressed. If extended key capture is off, your game will not react to extended keypresses. In either case, your game will continue to react to standard keypresses, as described on the onKeyboard page.",
	"No return value.\n\nSLUDGE and this SLUDGE documentation are copyright Hungry Software and contributors 2000-2012")
FUNCTION_DOC ("clearPartProperty", "clearPartProperty.html",
	"clearPartProperty (character, part, property)",
	"Hands the property of part of character back to the character's clips, after setPartProperty took it over.\n\nThe part goes back to whatever its clips say at that moment. If the property wasn't set in the first place, nothing happens, and that's not an error either.",
	"Returns TRUE if there is such a character, otherwise FALSE.")
FUNCTION_DOC ("clearStatus", "clearStatus.html",
	"clearStatus ()",
	"Removes all status bars.",
	"No return value.")
FUNCTION_DOC ("clipEvent", "clipEvent.html",
	"clipEvent (clip, tick, soundOrFunction)",
	"Plays a sound, or calls a function, when clip gets to tick.\n\nsoundOrFunction is either a sound file, in single quotes, or the name of a function that takes no parameters. Anything else stops the game with an error. Footsteps are the classic use: a step sound at the two ticks where the feet touch the ground.\n\ntick can be anything from 0 to the length of the clip. An event at tick 0 happens when the clip starts, and an event at the last tick when the clip reaches its end. If the clip loops, they happen again in every round.\n\nEvents happen whether the character is shown or not, which is different from the sounds and functions of anim. So hiding a character doesn't silence its footsteps: stop the clip for that.",
	"No return value.")
FUNCTION_DOC ("clipFrames", "clipFrames.html",
	"clipFrames (clip, part, animation)",
	"Turns an animation of anim into the sprites and events of part in clip.\n\nIf you already have animations made with anim, there's no need to type them all in again as sprite keys: hand them to clipFrames, and they become part of a clip. The anim repeats until the clip ends, so a clip of 10 ticks plays an anim of 5 ticks twice. clipFrames replaces any sprite keys that the part already had in this clip.\n\nThe sounds and functions of the anim become events of the clip. Unlike in anim, they happen even while the character is hidden, so a hidden character can still be heard. An anim with no frames stops the game with an error, and so does a clip that's already in use.",
	"No return value.")
FUNCTION_DOC ("clipKey", "clipKey.html",
	"clipKey (clip, part, property, tick, value)\nclipKey (clip, part, property, tick, value, easing)",
	"Sets the property of part to value at tick in clip, from where it moves smoothly towards the next key.\n\nThis is how a Composite Character moves its parts: you say where the arm is at tick 0, where it is at tick 12, and SLUDGE works out everything in between. tick can be anything from 0 to the length of the clip. A key outside the clip stops the game with an error.\n\nThese are the properties you can key. The value is always a whole number:\n\nThe easing says how the property gets from this key to the next one:\n\nPART_DEPTH, PART_VISIBLE and PART_FLIP always step, whatever easing you give them, because there's nothing in between half visible and visible. The last key is where a movement ends, so its easing does nothing. Before the first key, the property has the value of the first key, and after the last key, the value of the last one.\n\nAdd all the keys before you use the clip: once a character plays it, it can't change any more. In the Animation Editor, the easing of a key is its Towards the next key in the Inspector, and the colour of the key on the timeline.",
	"No return value.")
FUNCTION_DOC ("clipSprite", "clipSprite.html",
	"clipSprite (clip, part, tick, spriteBank, sprite)\nclipSprite (clip, part, tick, NOSPRITE)\nclipSprite (clip, part, tick, PART_DEFAULT)",
	"From tick in clip, makes part show sprite from spriteBank.\n\nThe part keeps that sprite until the next sprite key of the same part. A negative sprite shows its mirror image, as in anim. For a character without parts, or for the character as a whole, use \"\" as the part.\n\nWith NOSPRITE, the part shows nothing at all, which is handy for closed eyes. With PART_DEFAULT, it goes back to the sprite you gave the part with addPart.\n\nSprites never ease: there's no such thing as halfway between two sprites, so the part changes exactly at the key's tick.",
	"No return value.")
FUNCTION_DOC ("completeTimers", "completeTimers.html",
	"completeTimers ()",
	"Continues any functions paused using the pause function. (Note that the completeTimers function does not affect code which is currently paused because of one of the say and think functions.)",
	"No return value.")
FUNCTION_DOC ("copyStack", "copyStack.html",
	"copyStack (theStack)",
	"Copies a stack, creating a new stack with the same contents. The elements will be in reverse order.",
	"The return value is the new stack which has been created.")
FUNCTION_DOC ("costume", "costume.html",
	"costume (still, [still, [still...]], walk, [walk, [walk...]], talk, [talk, [talk...]])",
	"Creates a costume containing an animation for standing still, talking and walking in one or more direction. Therefore the number of parameters must be a multiple of 3. Each of the parameters must be an animation created by the anim function, a clip of newClip, or NULL (indicating that the animation slot will not be used - for example, a character will never be seen in a certain state while facing a certain direction).\n\nThe number of directions (let's call it n for the time being) represented by the costume is the total number of parameters divided by 3. The first n animations (or NULLs) specify how a character will look while standing still. The next n specify how the character will look while walking. The final n determine how a character will appear while talking.\n\nIn each of these three groups, the directions are specified in the same order - clockwise from facing the camera. Therefore, if the costume has 6 parameters, the costume will represent 2 different directions and the parameters will be interpreted as: standing still facing south, standing still facing north, walking south, walking north, talking facing south, talking facing north. If the costume has 12 parameters, the costume will have 4 different directions and the parameters will be interpreted as: standing still facing south, standing still facing west, standing still facing north, standing still facing east, walking south, walking west, walking north, walking east, talking facing south, talking facing west, talking facing north, talking facing east.\n\nYou can also specify an offset with setCharacterAngleOffset to add to the calculations when selecting direction.",
	"The return value is the costume which has been created.")
FUNCTION_DOC ("darkBackground", "darkBackground.html",
	"darkBackground ()",
	"Makes the background image half as bright by dividing the red, green and blue elements of each colour by two.\n\nNote that this doesn't affect characters, speech or status text; neither does it affect any images which are loaded onto the background after the function has been called.\n\nAlso note that there is no command to make the screen bright again (as colour information will have been lost due to the division by two). In order to make the screen bright again, any images which were on the screen must be reloaded (most probably by using the addOverlay function).",
	"No return value.")
FUNCTION_DOC ("deleteAllFromStack", "deleteFromStack_and_deleteAllFromStack.html",
	"deleteAllFromStack (thisStack, thisElement)",
	"These functions remove instances of thisElement from thisStack. The standard SLUDGE equality test is performed on each element of the stack and the value of thisElement - if they are the same, the element is removed from the stack. The deleteFromStack function will stop after finding and removing one match; deleteAllFromStack will remove all instances of thisElement which are found.",
	"The command returns the number of elements removed from the stack.")
FUNCTION_DOC ("deleteFile", "deleteFile.html",
	"deleteFile (thisFilename)",
	"Deletes the file specified by thisFilename - which should be a string, not a file handle. If you're writing a custom save game mechanism, you may require this functionality... so here it is. The string thisFilename shouldn't contain any of the characters \":\", \"/\" and \"\\\" - a precaution to make it more difficult to write malicious code.\n\nThis function supports filename bodging - for details see the bodgeFilenames command.",
	"The command returns TRUE if the deletion was a success, FALSE otherwise.")
FUNCTION_DOC ("deleteFromStack", "deleteFromStack_and_deleteAllFromStack.html",
	"deleteFromStack (thisStack, thisElement)",
	"These functions remove instances of thisElement from thisStack. The standard SLUDGE equality test is performed on each element of the stack and the value of thisElement - if they are the same, the element is removed from the stack. The deleteFromStack function will stop after finding and removing one match; deleteAllFromStack will remove all instances of thisElement which are found.",
	"The command returns the number of elements removed from the stack.")
FUNCTION_DOC ("dequeue", "dequeue.html",
	"dequeue (thisStack)",
	"Removes the first element from a stack. If there were previously n elements in the stack (numbered 0 to n-1) there will now be n-1 elements in the stack (numbered 0 to n-2) with thisStack[0] now containing the old value of thisStack[1], thisStack[1] now containing the old value of thisStack[2] and so on. Calling this function is equivalent to calling the popFromStack command.",
	"The command returns the value which was removed; the old value of thisStack[0].")
FUNCTION_DOC ("doBackgroundEffect", "setBackgroundEffect.html",
	"doBackgroundEffect ()",
	"The setBlankColour function is used to specify an effect which can be applied to the background image. The doBackgroundEffect function applies the current effect to the background. The parameters for the function are an add value, a divide value and one or more arrays (which you can think of as a matrix, should you wish). When an effect is applied, the following procedure happens for each pixel: SLUDGE adds up the colour values of each pixel and its surrounding pixels, weighted using the arrays you've specified. For example, if you specified one array, containing just the value 1, it would get the original red, green and blue values of that pixel. If you specified one array, containing the values 1 three times, it would get red, green and blue totals equivalent to the red, green and blue values of the pixel itself, plus the red, green and blue values of the pixels either side. These values are then divided by the divide value and finally the add value is added to each. These red, green and blue values are then used as the new colour for the pixel (clamped to the range 0 to 255). So to get an average of each pixel and it's left-hand and right-hand neighbours, you could say setBackgroundEffect (0, 3, newStack (1, 1, 1)). If multiple arrays are specified, these are used to also add the red, green and blue values from pixels the line above or below the pixel having its colour changed.",
	"No return values.")
FUNCTION_DOC ("enqueue", "enqueue.html",
	"enqueue (thisStack, newElement)",
	"Adds newElement to the end of thisStack. If there were previously n elements in the stack (numbered 0 to n-1) there will now be n+1 elements in the stack, with newElement at position n.",
	"No return value.")
FUNCTION_DOC ("fetchEvent", "fetchEvent.html",
	"fetchEvent (thisAction, thisObject)",
	"Determines whether the thisObject object type contains a thisAction event. Both thisAction and thisObject must be variables of type objectType.",
	"fetchEvent returns a function handle if the specified event is found. If no event is found, the function returns FALSE.")
FUNCTION_DOC ("fileExists", "fileExists.html",
	"fileExists (nameOfFile)",
	"Checks whether the file nameOfFile exists. The nameOfFile parameter should be a string, not a file handle. Useful for checking whether a saved game exists before attempting to load or overwrite it, or for finding out whether the reason that a file couldn't be launched is because it doesn't exist.\n\nThis function supports filename bodging - for details see the bodgeFilenames command.",
	"No return value.")
FUNCTION_DOC ("floatCharacter", "floatCharacter.html",
	"floatCharacter (thisCharacter, distance)",
	"Changes how far from ground level thisCharacter is floating. The value distance is measured in pixels in the animation frame used by the character, rather than screen pixels. Positive values move a character down the screen; negative values raise a character above ground level.",
	"TRUE if thisCharacter is an object type which is currently represented on the screen as a character, otherwise FALSE (i.e. TRUE if the call to the function was a success).")
FUNCTION_DOC ("forceCharacter", "forceCharacter.html",
	"forceCharacter (thisCharacter, sX, sY)\nforceCharacter (thisCharacter, thisObject)",
	"Forces thisCharacter to walk to a new location on the screen, disregarding any floor which setFloor has currently set. The character will move from its current location to the new location in a straight line.\n\nIf the function call provides three parameters, the second and third parameters are assumed to be the co-ordinates of the new position (sX, sY).\n\nIf the function call provides only two parameters, the second is assumed to be an object thisObject which is already represented by a region on the screen using the addScreenRegion command. If this is the case, upon arrival at the new location the character will turn to face the direction specified in the addScreenRegion call (unless this has been set to -1).\n\nIn either of these cases, if the final co-ordinates to which the character is told to walk are (0, 0) the character will not move.",
	"No return value.")
FUNCTION_DOC ("freeSound", "freeSound.html",
	"freeSound (soundFile)",
	"Removes the soundFile file from the SLUDGE sound cache. Why? Because the more sounds you have in the cache - especially big ones - the more memory is taken up, and the slower your game may run on some older computers. So, if you've just finished using a huge great sound that you don't need again for a while, use this function to reclaim some memory.\n\nPlease note that the cache can only hold 8 sounds at once, so older sounds will be unloaded once they're not needed automatically anyway - you don't need to use freeSound to make your game work, and freeing up memory used by tiny little sounds (up to a few seconds long) won't make much of a difference.",
	"No return value.")
FUNCTION_DOC ("freeze", "freeze.html",
	"freeze ()",
	"Freezes execution of the current scene so that tasks (such as loading, saving, inventories or messages) can be performed in the foreground. A new background image is created which is identical to the original background image, but with characters pasted onto it. Characters, screen regions and floors are remembered but not added to the new background image. Any functions (other than the function which ran the freeze command) are paused - unless they have been declared unfreezable.",
	"No return value.")
FUNCTION_DOC ("getCharacterDirection", "getCharacterDirection.html",
	"getCharacterDirection (thisCharacter)",
	"Get the current direction faced by thisCharacter.",
	"If thisCharacter is represented on-screen as a character, the return value will state which of the directions specified in the character's multi-directional costume the character is facing. For example, if the character has a 4-direction costume, the return value will be 0, 1, 2 or 3 (with 0 being north) or if the character has an 8-direction costume, the return value will be 0, 1, 2, 3, 4, 5, 6 or 7 (with 0 being north). If thisCharacter isn't currently on-screen, the return value will be -1.")
FUNCTION_DOC ("getCharacterScale", "getCharacterScale.html",
	"getCharacterScale (thisCharacter)",
	"Get the current scale used when drawing thisCharacter to the screen as a percentage.",
	"If thisCharacter is represented on-screen as a character, the return value will be the scale used when currently drawing the character on the screen as a percentage. For example, if the character is being drawn at half its original size, the return value will be 50. If thisCharacter isn't currently on-screen, the return value will be 0.")
FUNCTION_DOC ("getFramesPerSecond", "getFramesPerSecond.html",
	"getFramesPerSecond ()",
	"Calculate the frame rate which your game is currently achieving.",
	"The return value is the number of frames which passed during the last second. Ideally this should be the FPS value resulting from the frame speed that was entered in the SLUDGE Project Manager's \"Project Settings\" dialog box. If it's below this value, the machine isn't running your game at the correct speed, possibly because you're using too high a resolution or too high a Frames/sec value or too many processor-intensive graphical effects such as parallax scrolling or anti-aliasing.\n\nSLUDGE and this SLUDGE documentation are copyright Hungry Software and contributors 2000-2012")
FUNCTION_DOC ("getLanguageID", "getLanguageID.html",
	"getLanguageID ()",
	"Determine which translation file is currently being used by the system.",
	"The return value is the ID number of the translation file being used. If no translation file is being used (i.e. the game is using the default language in which the game was originally written) the return value is 0.")
FUNCTION_DOC ("getMatchingFiles", "getMatchingFiles.html",
	"getMatchingFiles (extension)",
	"Finds all files in the current directory which end in the given extension.\n\nThis function supports filename bodging - for details see the bodgeFilenames command.",
	"The function returns a stack containing an element for every file which was found. Each element is the name of a one of the files without the extension. The values in this stack can then be displayed on the screen for a user to choose which saved game to load or overwrite, although programming such an interface is left to the developer.")
FUNCTION_DOC ("getMouseScreenX", "getMouseScreenX_and_getMouseScreenY.html",
	"getMouseScreenX ()",
	"Get the current x or y co-ordinate of the mouse in relation to the SLUDGE window (or screen if the program is running in full-screen mode); that is, how far across or down the window the mouse cursor is currently positioned, regardless of where the camera is aimed in a scrolling scene.",
	"The functions each returns a number between 0 and the width or height of the window or screen minus 1 (inclusive). 0 represents the very left (in the case of getMouseScreenX) or top (in the case of getMouseScreenY) of the window or screen.")
FUNCTION_DOC ("getMouseScreenY", "getMouseScreenX_and_getMouseScreenY.html",
	"getMouseScreenY ()",
	"Get the current x or y co-ordinate of the mouse in relation to the SLUDGE window (or screen if the program is running in full-screen mode); that is, how far across or down the window the mouse cursor is currently positioned, regardless of where the camera is aimed in a scrolling scene.",
	"The functions each returns a number between 0 and the width or height of the window or screen minus 1 (inclusive). 0 represents the very left (in the case of getMouseScreenX) or top (in the case of getMouseScreenY) of the window or screen.")
FUNCTION_DOC ("getMouseX", "getMouseX_and_getMouseY.html",
	"getMouseX ()",
	"Get the current x or y co-ordinate of the mouse in relation to the scene; that is, how far across or down the background image the mouse cursor is currently positioned.",
	"The functions each returns a number between 0 and the width or height of the background image minus 1 (inclusive). 0 represents the very left (in the case of getMouseX) or top (in the case of getMouseY) of the background.")
FUNCTION_DOC ("getMouseY", "getMouseX_and_getMouseY.html",
	"getMouseY ()",
	"Get the current x or y co-ordinate of the mouse in relation to the scene; that is, how far across or down the background image the mouse cursor is currently positioned.",
	"The functions each returns a number between 0 and the width or height of the background image minus 1 (inclusive). 0 represents the very left (in the case of getMouseX) or top (in the case of getMouseY) of the background.")
FUNCTION_DOC ("getObjectX", "getObjectX_and_getObjectY.html",
	"getObjectX (thisObjectType)",
	"Get the x and y co-ordinates of the specified object type thisObjectType.",
	"If the object type is represented on screen as a screen region, the value returned is the x or y co-ordinate of the point at which a character would attempt to stand if told to walk to the object (i.e. the 6th or 7th parameter in the addScreenRegion statement which created the screen region).\n\nIf the object type is represented on screen as a character, the return value is the x or y co-ordinate at which the character is currently standing. If the object type is not on the screen, the return value is 0.")
FUNCTION_DOC ("getObjectY", "getObjectX_and_getObjectY.html",
	"getObjectY (thisObjectType)",
	"Get the x and y co-ordinates of the specified object type thisObjectType.",
	"If the object type is represented on screen as a screen region, the value returned is the x or y co-ordinate of the point at which a character would attempt to stand if told to walk to the object (i.e. the 6th or 7th parameter in the addScreenRegion statement which created the screen region).\n\nIf the object type is represented on screen as a character, the return value is the x or y co-ordinate at which the character is currently standing. If the object type is not on the screen, the return value is 0.")
FUNCTION_DOC ("getOverObject", "getOverObject.html",
	"getOverObject ()",
	"Returns which object at which the mouse is currently pointing.",
	"If the mouse is not pointing at an object, the return value is NULL. If the mouse is pointing at either a screen region (defined by the addScreenRegion function) or a character (defined by the addCharacter function) the return value is the object type of the region or character.\n\nHidden characters and characters with no name (i.e. defined as or renamed to the empty string \"\") do not register.")
FUNCTION_DOC ("getPixelColour", "getPixelColour_(getPixelColor).html",
	"getPixelColour (x, y)",
	"Gets the colour of the pixel at co-ordinates x, y in the current scene.",
	"This function returns a stack with 3 elements. Each of the elements will be a number between 0 and 255 inclusive - the first representing how much red there is in the colour, the second how much green and the third how much blue.\n\nNB: Because SLUDGE may scale the displayed image, these values may not match up exactly with the numbers you used to colour the pixel.")
FUNCTION_DOC ("getSoundCache", "getSoundCache.html",
	"getSoundCache ()",
	"Finds out which samples are currently cached.",
	"The function returns a stack containing up to 8 file handles, one for each sample which is in the cache. You can subsequently loop through this stack to change the volume of or stop all active samples (as a sample must be in the cache in order for it to be playing).")
FUNCTION_DOC ("getStatusText", "getStatusText.html",
	"getStatusText ()",
	"Gets text from the active status bar.",
	"A call to this function returns the text from the active status bar.")
FUNCTION_DOC ("hardScroll", "hardScroll.html",
	"hardScroll (numberOfRows)",
	"Scrolls the background image up and down.\n\nIf numberOfRows is positive, the background image scrolls up: the top numberOfRows lines of the image are lost, and numberOfRows blank lines are added to the bottom. If numberOfRows is negative, the background image scrolls down. If numberOfRows is 0 the function has no effect.\n\nShould the value of numberOfRows be greater than the height of the background image (or less than 0 minus the height of the background image) the background image will be blanked completely, as if blankScreen had been called.\n\nThis function is very useful for scrolling credits, but probably very little else - unless, that is, someone proves me wrong. The function is not for scrolling around scenes which are larger than the screen or window - try setSceneDimensions and aimCamera instead.",
	"No return value.")
FUNCTION_DOC ("hasFlag", "hasFlag.html",
	"hasFlag (thisObject, thisFlag)",
	"Checks to see whether the object thisObject has the object flag thisFlag. Flags are specified when creating object types and cannot subsequently be changed by the game.",
	"TRUE if thisObject has the flag thisFlag set. FALSE if not.")
FUNCTION_DOC ("hideCharacter", "hideCharacter.html",
	"hideCharacter (thisCharacter)",
	"Makes thisCharacter invisible. While the character is invisible, getOverObject and the function specified in onFocusChange will not register when the mouse is passed over the character. However, the character can still speak and think using the say and think functions.\n\nIf thisCharacter is an object type which is not represented on the screen as a character using the addCharacter function the line will have no effect.\n\nThe character can be made visible again using the showCharacter function",
	"No return value.")
FUNCTION_DOC ("howFrozen", "howFrozen.html",
	"howFrozen (howFrozen)",
	"Counts how many more times the game has been frozen than unfrozen: that is, the number of times which the unfreeze command must be called in order to return to a completely unfrozen state (in which it is possible to save the current game - that is to say, saveGame will not work unless the return value from howFrozen is 0).",
	"The return value is a number: 0 if the game isn't frozen, otherwise the number represents how many frozen levels currently exist.")
FUNCTION_DOC ("inFont", "inFont.html",
	"inFont (character)",
	"Checks if character (which should be a string containing a single character) exists in the current font.",
	"Returns true if character exists in the current font, or false if it doesn't.")
FUNCTION_DOC ("isCharacter", "isCharacter.html",
	"isCharacter (thisObject)",
	"Tests whether thisObject has been added as a character (added using the addCharacter function).",
	"If thisObject is represented as a character, the return value is TRUE. Otherwise, the return value is FALSE.")
FUNCTION_DOC ("isClipPlaying", "isClipPlaying.html",
	"isClipPlaying (character, layer)",
	"Checks whether character is playing something on layer that hasn't finished yet.\n\nCareful: a looping clip never finishes, and neither does an animation of anim. So if you wait in a loop like the one in the example for a looping clip or an anim on layer 0, you'll be waiting forever!\n\nA clip that doesn't loop has finished once it gets to its end, and a clip of length 0 has finished straight away. On layers 1 to 99, a finished clip still holds its last pose until you take it away with stopClip, even though isClipPlaying says FALSE.",
	"Returns TRUE if the clip is still playing, otherwise FALSE.")
FUNCTION_DOC ("isInScreenRegion", "isInScreenRegion.html",
	"isInScreenRegion (thisCharacter, thisObject)",
	"Tests whether the hotspot of thisCharacter (the point where it stands) is inside a screen region of thisObject right now.\n\nIt works for any region, rectangle or shape, whether or not watchScreenRegion is watching it. If thisObject has more than one region, being inside any of them is enough.",
	"TRUE if the character is inside, and FALSE if it isn't, or if either the character or the region isn't on the screen at all.")
FUNCTION_DOC ("isMoving", "isMoving.html",
	"isMoving (thisObject)",
	"Tests whether thisObject is a character who is currently moving (due to one of the moveCharacter or forceCharacter functions).",
	"If thisObject is currently represented as a character and the character is moving, the return value is TRUE. Otherwise, the return value is FALSE.")
FUNCTION_DOC ("isScreenRegion", "isScreenRegion.html",
	"isScreenRegion (thisObject)",
	"Tests whether thisObject is currently represented on-screen as a screen region (added using the addScreenRegion function).",
	"If thisObject is currently represented as a screen region, the return value is TRUE. Otherwise, the return value is FALSE.")
FUNCTION_DOC ("jumpCharacter", "jumpCharacter.html",
	"jumpCharacter (thisCharacter, sX, sY)\njumpCharacter (thisCharacter, thisObject)",
	"Changes the location of thisCharacter to a new location on the screen immediately, whether in or out of any floor which setFloor may have currently set.\n\nIf the function call provides three parameters, the second and third parameters are assumed to be the co-ordinates of the new position (sX, sY).\n\nIf the function call provides only two parameters, the second is assumed to be an object thisObject which is already represented by a region on the screen using the addScreenRegion command. If this is the case, the character will turn to face the direction specified in the addScreenRegion call (unless this has been set to -1).\n\nIn either of these cases, if the co-ordinates to which the character is told to jump are (0, 0) the character will not move.",
	"No return value.")
FUNCTION_DOC ("launch", "launch_and_launchWith.html",
	"launch (filename)",
	"Launches a file which isn't contained within the compiled SLUDGE game, such as a help document or an order form. The filename parameter must be a string, not a file handle. The filename given should be relative to the location of the compiled SLUDGE game; that is, if the SLUDGE game is \"C:\\Games\\SLUDGE\\Here\\My game.SLG\", calling launch with a parameter of \"docs\\hello.html\" would try to run \"C:\\Games\\SLUDGE\\Here\\docs\\hello.html\". This is so that, if you distribute your game, it can be installed anywhere on a machine and the files - so long as they're distributed with the compiled game - can still be found and launched.\n\nThis function supports filename bodging - for details see the bodgeFilenames command.",
	"The function returns TRUE if the file could be launched and FALSE if not.")
FUNCTION_DOC ("launchWith", "launch_and_launchWith.html",
	"launchWith (program, filename); *removed starting with SLUDGE 2.0",
	"Launches a file which isn't contained within the compiled SLUDGE game, such as a help document or an order form. The filename parameter must be a string, not a file handle. The filename given should be relative to the location of the compiled SLUDGE game; that is, if the SLUDGE game is \"C:\\Games\\SLUDGE\\Here\\My game.SLG\", calling launch with a parameter of \"docs\\hello.html\" would try to run \"C:\\Games\\SLUDGE\\Here\\docs\\hello.html\". This is so that, if you distribute your game, it can be installed anywhere on a machine and the files - so long as they're distributed with the compiled game - can still be found and launched.\n\nThis function supports filename bodging - for details see the bodgeFilenames command.",
	"The function returns TRUE if the file could be launched and FALSE if not.")
FUNCTION_DOC ("lightStatus", "lightStatus.html",
	"lightStatus (whichBar)",
	"Forces one of the status bars (created using the addStatus command) to be drawn in a different colour to the others. The status bar specified by the parameter whichBar (which should be a number) will, until the next call to lightStatus, be drawn in the colour specified by the setLitStatusColour function rather than the setStatusColour function.\n\nIf you do not want any of the status bars to be lit, call lightStatus with the value of the whichBar parameter being any number less than 0.",
	"No return value.\n\nSLUDGE and this SLUDGE documentation are copyright Hungry Software and contributors 2000-2012")
FUNCTION_DOC ("loadClip", "loadClip.html",
	"loadClip (clipFile)",
	"Loads an animation clip that you made in the Animation Editor, so you don't have to type in every key with clipKey.\n\nclipFile is a .clp file of your project, in single quotes, just like a sprite bank or a sound. You get exactly the same thing as a clip built with newClip, with all its tracks, keys and events, and you use it in the same ways: in a costume, with animate, on a layer with playClip, or mirrored with mirrorClip. Until you use it, you can still add keys and events to it. Give it anything other than a clip, such as a .rig file, and the game stops with an error.\n\nA clip moves parts by their names, so it plays on the rig you made it for, and on any other rig with parts of the same names (see Composite Characters).\n\nThe compiler puts the clip into your game, with the sprite banks and sounds it uses. It tells you, with the script and line that load the clip, when a bank or sound is missing, or when an event calls a function your scripts don't have. It also warns you about tracks for parts that the clip's rig doesn't have, because those tracks would do nothing.",
	"Returns the clip.")
FUNCTION_DOC ("loadCustomData", "loadCustomData_and_saveCustomData.html",
	"loadCustomData (filenameString)",
	"The saveCustomData function creates a file with the name filenameString and stores the contents of the variable thisStack (which must, as you probably guessed, be a stack) in the file. The loadCustomData function loads data back from a file that has been created using saveCustomData.\n\nPlease note that you can't store custom data in the main .ini file. Use another name for your custom settings .ini file.\n\nThe method by which loadCustomData and saveCustomData treat the files they act upon and the types of data which can be stored are determined by the current custom encoding setting - see setCustomEncoding for details. If a game attempts to read a file with a different custom encoding setting than the one which was used when the file was created then a fatal error will occur, in order to stop others reading your custom data files unless they know the setting you used.",
	"The loadCustomData function returns a stack containing the data which was just loaded. The saveCustomData function does not return a value.")
FUNCTION_DOC ("loadGame", "loadGame.html",
	"loadGame (filenameString)",
	"Loads a saved game. When you use this function, all of the current data - which functions are running, what each of the variables contains, the size and contents of the background image, the camera position, the floor and so on - will be forgotten and reset to the state stored in the file with the name filenameString.\n\nTherefore, if the call is successful, your program will not continue from the command following the call to loadGame - instead it will continue after the appropriate saveGame call used to create the file. The only situation in which your program will continue from the line following the call to loadGame is if the file given by filenameString doesn't exist.\n\nThis function supports filename bodging - for details see the bodgeFilenames command.",
	"No return value.")
FUNCTION_DOC ("loadRig", "loadRig.html",
	"loadRig (rigFile)",
	"Loads a rig that you made in the Character Creator, so you don't have to build it part by part with addPart.\n\nrigFile is a .rig file of your project, in single quotes, just like a sprite bank or a sound. You get exactly the same thing as a rig built with newRig: all the parts, with their sprites, their pivots and the pose they stand in. Give it to a character with setCharacterRig. Until you do, you can still add parts to it, or change their pivots or depths.\n\nThe compiler puts the rig into your game, together with the sprite banks it uses. If one of those banks is missing, the compiler tells you which one, and in which script and on which line you load the rig, so you don't find out halfway through the game. Give it anything other than a rig, such as a .clp file, and the game stops with an error. The clips you made for the rig in the Animation Editor load with loadClip.",
	"Returns the rig, ready for setCharacterRig.")
FUNCTION_DOC ("loopSound", "loopSound.html",
	"loopSound (sound [, sound [, sound...]])\nloopSound (sound [, sound [, sound...]], loopStyle)",
	"Plays one or more sounds, starting immediately. The sound parameters must be file handles. Supported sound formats are wave files (.WAV) and Ogg Vorbis (.OGG).\n\nIf loopStyle is zero, the sounds are played in order and then stop. If loopStyle is one, the sounds are played in order and then the final sound is looped. If loopStyle is two, the whole playlist is looped.\n\nA call to stopSound with the currently playing sound will stop the sound, and cancel any sounds waiting in line to be played. But please note that stopSound only affects the currently playing sound: Stopping a sound that has finished playing, or one that hasn't yet started playing will have no effect.",
	"No return value.")
FUNCTION_DOC ("makeFastArray", "makeFastArray.html",
	"makeFastArray (numberOfElements)\nmakeFastArray (thisStack)",
	"Creates a fast array: a list of a fixed size, which you read and set with square brackets just like a stack, but much faster to read by number.\n\nIf you give it a number, the new array has numberOfElements elements, all of them NULL. If you give it a stack, the new array has as many elements as thisStack and holds a copy of each of them, in the same order. Changing the stack afterwards doesn't change the array.\n\nSo why bother? A stack is a chain of elements, so to find myStack[500] SLUDGE has to walk past the 500 elements before it. A fast array goes straight to the element you ask for, however big it is. If you keep a big table of values and read it by number, a fast array is much quicker.\n\nThe catch is that its size never changes. You can read and set its elements with square brackets, and stackSize tells you how many there are, but the other stack functions (pushToStack, popFromStack, enqueue and friends) only work on stacks, and will stop the game with an error if you give them a fast array. Reading an element past the end gives you NULL, but setting one is an error! Like a stack, a fast array counts as TRUE when it has any elements, and copying it into another variable doesn't copy the elements: both variables then point at the same array.",
	"Returns the new fast array.")
FUNCTION_DOC ("mirrorClip", "mirrorClip.html",
	"mirrorClip (clip)",
	"Makes a mirror-image copy of clip, so that a costume can walk WEST with the clip that walks EAST.\n\nWhy draw everything twice? mirrorClip flips the whole character, from the character itself down, so every part is mirrored along with it. A part that the clip flips with PART_FLIP stays flipped compared to its parent, so the copy really is a mirror image. If the clip already flips the whole character, the copy unflips it.\n\nThe copy is a new clip, which you can still add keys to, and the original stays as it was. mirrorClip only takes clips: for an animation of anim, use negative sprite numbers instead, or the game stops with an error. To mirror just one part of a Composite Character, key its PART_FLIP with clipKey.",
	"Returns the new clip.")
FUNCTION_DOC ("mixOverlay", "mixOverlay.html",
	"mixOverlay (thisFile, atX, atY)",
	"Loads an image from the file handle thisFile and overlays it onto the screen with 50% transparency. The colour bright magenta (255, 0, 255 or #FF00FF) is used as the transparency colour (except for 32-bit images, where the alpha channel is used!), making is possible to add irregular shapes.\n\nThe values atX and atY define the top left corner of the area into which the image will be added. If the value of atX is CENTRE (or CENTER) the image will be added halfway across the window. If the value of atY is CENTRE (or CENTER) the image will be added halfway down the window.",
	"No return value.")
FUNCTION_DOC ("moveCharacter", "moveCharacter.html",
	"moveCharacter (thisCharacter, sX, sY)\nmoveCharacter (thisCharacter, thisObject)",
	"Walks thisCharacter to a new location on the screen, following any floor which setFloor has currently set. The execution of the program will continue when the character reaches its desired destination or as close to the destination as the floor will allow. If the location lies outside of the current floor, the position to which the character will actually walk is the closest point on any of the floor borders. If the character is not inside the floor area when the function is called, it will be moved around the floor as if it were starting in the nearest section to its current position.\n\nIf the function call provides three parameters, the second and third parameters are assumed to be the co-ordinates of the new position (sX, sY).\n\nIf the function call provides only two parameters, the second is assumed to be an object thisObject which is already represented by a region on the screen using the addScreenRegion command. If this is the case, upon arrival at the new location the character will turn to face the direction specified in the addScreenRegion call (unless this has been set to -1).\n\nIn either of these cases, if the final co-ordinates to which the character is told to walk are (0, 0) the character will not move.",
	"No return value.")
FUNCTION_DOC ("newClip", "newClip.html",
	"newClip (length)\nnewClip (length, loop)",
	"Creates a new, empty animation clip that lasts length ticks (screen updates).\n\nIf loop is TRUE, the clip plays over and over. Without it, or with FALSE, the clip plays once and then holds its last frame. A clip of length 0 shows one pose and stays there, which is just the thing for standing still. A negative length stops the game with an error.\n\nFill the clip with clipSprite, clipKey, clipFrames and clipEvent. Then use it wherever you would use an animation of anim: with animate, costume, addCharacter, setCursor or pasteImage, or on a layer of its own with playClip.\n\nAdd all its keys and events first! As soon as you use the clip in any of those ways it's fixed, because a character may be playing it, and adding a key or event to it after that gives an error.",
	"Returns the new clip.")
FUNCTION_DOC ("newRig", "newRig.html",
	"newRig ()",
	"Creates a new, empty rig: the list of parts that a Composite Character is made of.\n\nA new rig has just one part, called \"\" - the character itself, which all the other parts hang from. Add the head, arms, legs and so on with addPart, and then give the rig to a character with setCharacterRig.\n\nIf you made your rig in the Character Creator, you don't need this: loadRig gives you the whole rig in one go.",
	"Returns the new rig.")
FUNCTION_DOC ("newStack", "newStack.html",
	"newStack ([firstElement [, secondElement...]])",
	"Creates a new stack with zero or more elements. Internally, the elements are added to the stack in such a way that a call to popFromStack will return (and remove) the firstElement in the stack. The elements can be of any variable type, including other stacks.",
	"The function returns the stack which has been created.")
FUNCTION_DOC ("normalCharacter", "normalCharacter.html",
	"normalCharacter (thisCharacter)",
	"Makes sure thisCharacter is using the normal standing-still-doing-nothing animation for the direction in which it's currently facing, as specified by its current costume (set using either the addCharacter or setCostume function).",
	"No return value.")
FUNCTION_DOC ("onFocusChange", "onFocusChange.html",
	"onFocusChange (handlerFunction)\nonFocusChange ()",
	"Tells the computer which function to call when the mouse cursor moves from one area of the screen to another (screen regions, characters and nothing). The value of handlerFunction must be a pointer to a function which takes one parameters. The function will be called every time the area at which the mouse is pointing changes until another call to onFocusChange. The object type at which the mouse is pointing - or NULL if the mouse is pointing at nothing - will be passed into the handlerFunction when it is called. This parameter will be the same as the return value from the getOverObject function.\n\nIf no parameter is given, the program stops calling the current focus change function. No function will be called when the mouse moves from one area of the screen to another until another call to onFocusChange tells it otherwise.\n\nNote that only one handler for focus changes can be used at any one time. A subsequent call to onFocusChange will replace the current handler function.",
	"No return value.")
FUNCTION_DOC ("onKeyboard", "onKeyboard.html",
	"onKeyboard (handlerFunction)\nonKeyboard ()",
	"Tells the computer which function to call when a button on the keyboard is pressed. The value of handlerFunction must be a pointer to a function which takes one parameter. When a keyboard button is pressed, the function specified in handlerFunction will be called with the key which was pressed passed as a string to the parameter.\n\nIf no parameter is given, the program stops calling the current keyboard handler function. No function will be called when the user presses a keyboard button until another call to onKeyboard tells the machine otherwise.\n\nNote that only one handler for the keyboard can be used at any one time. A subsequent call to onKeyboard will replace the current handler function.\n\nFor normal characters, your keyboard handler will simply by passed the individual character as a string. For some special characters, however, the handler will be passed a longer string, the content of which specifies what key has been pressed:\n\n\"ENTER\" The player pressed either the enter or return key\n\n\"BACKSPACE\" The player pressed the backspace key\n\n\"ESCAPE\" The player pressed the escape key\n\n\"F1\" - \"F12\" The player pressed one of the function keys\n\n\"DELETE\" The player pressed the delete key\n\n\"INSERT\" The player pressed the insert key\n\n\"HOME\" The player pressed the home key\n\n\"END\" The player pressed the end key\n\n\"PAGE UP\" The player pressed the page up key\n\n\"PAGE DOWN\" The player pressed the page down key\n\n\"UP\", \"DOWN\", \"LEFT\", \"RIGHT\" The player pressed one of the cursor keys\n\n\"ALT+A\" - \"ALT+Z\" The player held down Alt and pressed a letter\n\n\"ALT+F1\" - \"ALT+F12\" The player held down Alt and pressed a function key\n\nNote that, at least on the machines on which SLUDGE has been tested so far, pressing F10 will generate an \"ALT+F10\" message, not an \"F10\" message as you'd reasonably expect. Also, it seems to be oblivious to users holding down Alt and hitting F6 for some reason. Go figure.\n\nTo capture a wider range of keypresses, use the captureAllKeys command.",
	"No return value.")
FUNCTION_DOC ("onLeftMouse", "onLeftMouse.html",
	"onLeftMouse (handlerFunction)\nonLeftMouse ()",
	"Tells the computer which function to call when the left mouse button is clicked. The value of handlerFunction must be a pointer to a function which takes no parameters. The function will be called every time the user presses the left mouse button until another call to onLeftMouse.\n\nIf no parameter is given, the program stops calling the current left mouse button handler function. No function will be called when the user presses the left mouse button until another call to onLeftMouse tells it otherwise.\n\nNote that only one handler for the left mouse button can be used at any one time. A subsequent call to onLeftMouse will replace the current handler function.",
	"No return value.")
FUNCTION_DOC ("onLeftMouseUp", "onLeftMouseUp.html",
	"onLeftMouseUp (handlerFunction)\nonLeftMouseUp ()",
	"Tells the computer which function to call when the left mouse button is released. The value of handlerFunction must be a pointer to a function which takes no parameters. The function will be called every time the user releases the left mouse button until another call to onLeftMouseUp.\n\nIf no parameter is given, the program stops calling the current left mouse button release handler function. No function will be called when the user releases the left mouse button until another call to onLeftMouseUp tells it otherwise.\n\nNote that only one handler for releasing the left mouse button can be used at any one time. A subsequent call to onLeftMouseUp will replace the current handler function.",
	"No return value.")
FUNCTION_DOC ("onMoveMouse", "onMoveMouse.html",
	"onMoveMouse (handlerFunction)\nonMoveMouse ()",
	"Tells the computer which function to call whenever the mouse is moved. The value of handlerFunction must be a pointer to a function which takes no parameters. The function will be called every time the user moves the mouse until another call to onMoveMouse.\n\nIf no parameter is given, the program stops calling the current mouse movement handler function. No function will be called when the user moves the mouse until another call to onMoveMouse tells it otherwise.\n\nNote that only one handler for mouse movement can be used at any one time. A subsequent call to onMoveMouse will replace the current handler function.",
	"No return value.")
FUNCTION_DOC ("onRightMouse", "onRightMouse.html",
	"onRightMouse (handlerFunction)\nonRightMouse ()",
	"Tells the computer which function to call when the right mouse button is clicked. The value of handlerFunction must be a pointer to a function which takes no parameters. The function will be called every time the user presses the right mouse button until another call to onRightMouse.\n\nIf no parameter is given, the program stops calling the current right mouse button handler function. No function will be called when the user presses the right mouse button until another call to onRightMouse tells it otherwise.\n\nNote that only one handler for the right mouse button can be used at any one time. A subsequent call to onRightMouse will replace the current handler function.",
	"No return value.")
FUNCTION_DOC ("onRightMouseUp", "onRightMouseUp.html",
	"onRightMouseUp (handlerFunction)\nonRightMouseUp ()",
	"Tells the computer which function to call when the right mouse button is released. The value of handlerFunction must be a pointer to a function which takes no parameters. The function will be called every time the user releases the right mouse button until another call to onRightMouseUp.\n\nIf no parameter is given, the program stops calling the current right mouse button release handler function. No function will be called when the user releases the right mouse button until another call to onRightMouseUp tells it otherwise.\n\nNote that only one handler for releasing the right mouse button can be used at any one time. A subsequent call to onRightMouseUp will replace the current handler function.",
	"No return value.")
FUNCTION_DOC ("parallaxAdd", "parallaxAdd.html",
	"parallaxAdd (thisFile, scrollSpeedX, scrollSpeedY)",
	"Loads an image from the file handle thisFile and uses it as a parallax layer, visible behind the background image. Parallax layers can scroll at different speeds to the background image, giving the illusion of depth to a scene as it moves.\n\nThe value scrollSpeedX specifies how fast the parallax image will move as a percentage of the the background image as it scrolls. For example, if scrollSpeedX is 50, for every 2 pixels the main scene moves the parallax layer will move only 1. If you specify a scroll speed in this way, the width of the image must be a power of two (i.e. the width must be 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048 etc. pixels) in order for proper tiling to occur... that is, the image will repeat horizontally. The same is true of scrollSpeedY which specifies how fast the image moves vertically as a percentage of the movement of the main scene - if a value is specified here, the height of the image must be a power of 2.\n\nThere is one special value which can be used in the place of either scrollSpeedX or scrollSpeedY or neither or both at any time - and that value is the word AUTOFIT. If scrollSpeedX is AUTOFIT, the horizontal scrolling speed of the parallax layer is calculated automatically (and consequently, there's no way the image will need to repeat, so the width need not be a power of 2). However, a new rule is imposed if using this setting - the image must be as wide as, or wider than, the game window. If scrollSpeedY is AUTOFIT, the vertical scrolling speed of the layer is calculated automatically and the height need not be a power of 2 - however, the height of the image must be greater than or equal to the height of the game window. Got all that? Good.\n\nOne more thing - you can have multiple parallax layers at once, each with its own pair of scrolling speeds. The most recently added parallax layer will be visible through any areas of the scenery coloured bright cyan (0, 255, 255 or #00FFFF). (Except for 32-bit images, where the alpha channel is used!) Should there be a further parallax layer, it will be visible through any regions of the most recent coloured bright cyan, and so on, and so on. So, when loading multiple layers using this function, start with the furthest away. Also, for a realistic effect, make any layers that are further away scroll more slowly than those that are nearer the scene.",
	"No return value.")
FUNCTION_DOC ("parallaxClear", "parallaxClear.html",
	"parallaxClear ()",
	"Unloads all parallax layers added using the parallaxAdd function. Note that you may not need to use this function to remove parallax layers when you change rooms as setting the dimensions of the new scene using setSceneDimensions also removes any parallax layers currently added.",
	"No return value.")
FUNCTION_DOC ("pasteCharacter", "pasteCharacter.html",
	"pasteCharacter (thisObject)",
	"If thisObject is currently represented on-screen as a character, the current frame of the animation for the character is pasted onto the background image. The image is obscured, drawn and scaled in exactly the same way as the character itself - including its current light level and transparency settings as determined by the setCharacterDrawMode function.",
	"No return value.")
FUNCTION_DOC ("pasteImage", "pasteImage.html",
	"pasteImage (thisAnimation, x, y)",
	"Pastes the first frame of the animation thisAnimation onto the screen. The parameter thisAnimation must be the result of a call to the anim function. The parameters x and y define the point at which the image's hotspot will appear on the background image.",
	"No return value.")
FUNCTION_DOC ("pasteString", "pasteString_and_burnString.html",
	"pasteString (x, y, thisString)",
	"Adds text to the background image. The parameter thisString is added to the screen at the position (x, y).\n\nIf the value of x is CENTRE (or CENTER) the text is added halfway across the screen. Otherwise, the x value is used as the horizontal position of the hotspot of the first character of thisString. The y parameter is always used as the vertical position of the hotspot of every character of thisString - the enumeration CENTRE (or CENTER) shouldn't be used as the y parameter. If it is, the text will not appear on the screen.\n\nBoth pasteString and burnString use the font most recently loaded by the setFont command. pasteString uses the colour specified by the setPasteColour (or setPasteColor) command. burnString adds text in the colour specified by setBurnColour (or setBurnColor), using the differing brightness levels of the font to achieve an anti-aliasing effect.",
	"No return value.")
FUNCTION_DOC ("pause", "pause.html",
	"pause (numberOfFrames)",
	"Pauses the function for the required numberOfFrames.",
	"No return value.")
FUNCTION_DOC ("pauseMovie", "pauseMovie.html",
	"pauseMovie ()",
	"Pauses a playing movie, started with playMovie, or resumes a paused movie. If no movie is playing, this function does nothing.\n\nSee the Movie Demo (download link) for an example of how to use the movie functions.",
	"Returns 2 if a movie was playing, and now is paused. Returns 1 if a movie was paused, and now is resumed. Returns 0 if no movie was playing.")
FUNCTION_DOC ("peekEnd", "peekStart_and_peekEnd.html",
	"peekEnd (thisStack)",
	"Looks at the element at one end of thisStack without taking it off: peekStart at the first, and peekEnd at the last.\n\npeekStart looks at the first element, the one that popFromStack and dequeue would remove and that pushToStack adds. peekEnd looks at the last element, the one that enqueue adds. The stack itself stays as it was.\n\nThe stack mustn't be empty, or the game stops with an error! If you're not sure, check it first: a stack counts as FALSE when it's empty.",
	"peekStart returns the value of thisStack[0], and peekEnd returns the value of the last element.")
FUNCTION_DOC ("peekStart", "peekStart_and_peekEnd.html",
	"peekStart (thisStack)",
	"Looks at the element at one end of thisStack without taking it off: peekStart at the first, and peekEnd at the last.\n\npeekStart looks at the first element, the one that popFromStack and dequeue would remove and that pushToStack adds. peekEnd looks at the last element, the one that enqueue adds. The stack itself stays as it was.\n\nThe stack mustn't be empty, or the game stops with an error! If you're not sure, check it first: a stack counts as FALSE when it's empty.",
	"peekStart returns the value of thisStack[0], and peekEnd returns the value of the last element.")
FUNCTION_DOC ("pickOne", "pickOne.html",
	"pickOne (first [, second [, third...]])",
	"Randomly selects one of the parameters first, second, third and so on. There must be at least one parameter specified. The parameters can be any type of variable.",
	"The return value is the parameter which was randomly selected. There is an equal chance of choosing any one of the parameters.")
FUNCTION_DOC ("playClip", "playClip.html",
	"playClip (character, layer, clip)\nplayClip (character, layer, clip, LAYER_OVERRIDE)\nplayClip (character, layer, clip, LAYER_ADD)",
	"Plays clip on layer of character, on top of what the character is already doing.\n\nLayers let a character do several things at once: walk on its costume, blink on layer 1 and wave on layer 2. Use layers 1 to 99. Layer 0 belongs to the costume, so playing a clip there gives an error: use animate for that. Higher layers go on top of lower ones.\n\nLAYER_OVERRIDE is what you get if you leave it out. The layer sets whatever its clip keys, and leaves everything else to the layers below it, so a waving clip that only keys the arm leaves the legs walking. With LAYER_ADD, the layer adds its values instead: it adds to the position (PART_X, PART_Y) and angle (PART_ROTATION), and multiplies the sizes and colours, where 100 leaves them as they are. That's how you get a gentle breathing movement on top of any walk. Sprites, visibility, depth and flipping can't be added to: even on an additive layer they are just set, and the highest layer that keys them wins.\n\nPlaying a clip on a layer that already has one replaces it, and sets the layer's speed back to 100% (see setClipSpeed). Each layer has its own time and its own events. A clip that doesn't loop holds its last pose when it ends, until you take it away with stopClip, which stops a looping clip too.\n\nplayClip only plays clips. To play an animation of anim on a layer, turn it into a clip with clipFrames first, or the game stops with an error.",
	"Returns the length of the clip in ticks, at normal speed, so that pause (playClip (...)) waits until it ends. For a looping clip, that's the length of one round.")
FUNCTION_DOC ("playMovie", "playMovie.html",
	"playMovie (thisMovieFile)",
	"Plays a movie. The parameter must be a file handle. The only supported movie format is WebM.\n\nThe movie is played full screen (or full window if the game isn't running full screen), and in the correct aspect ratio, letterboxed if required. The function doesn't return until the movie has ended or is stopped by stopMovie. Only one movie can be played at a time.\n\nSee the Movie Demo (download link) for an example of how to use the movie functions.",
	"Returns 1 if the movie played, or 0 if there was an error.")
FUNCTION_DOC ("playSound", "playSound.html",
	"playSound (thisSoundFile)",
	"Plays a sound once, immediately. If the parameter isn't a file handle the code has no effect. Supported sound formats are Windows wave files (.WAV) and Ogg Vorbis (.OGG).",
	"No return value.")
FUNCTION_DOC ("popFromStack", "popFromStack.html",
	"popFromStack (thisStack)",
	"Removes the first element from a stack. If there were previously n elements in the stack (numbered 0 to n-1) there will now be n-1 elements in the stack (numbered 0 to n-2) with thisStack[0] now containing the old value of thisStack[1], thisStack[1] now containing the old value of thisStack[2] and so on. Calling this function is equivalent to calling the dequeue command.",
	"The command returns the value which was removed; the old value of thisStack[0].")
FUNCTION_DOC ("positionStatus", "positionStatus.html",
	"positionStatus (xValue, yValue)",
	"Changes the values on which the position of status bars are determined. The distance between the edge of the window and the text is given by xValue - if the status bars are centred (see the alignStatus function) this value is not used. The vertical position of the most recently added status bar is given by yValue. A second subsequent status bar, should one exist, is shown at this value minus the height of the font as specified using the setFont command, and so on.",
	"No return value.")
FUNCTION_DOC ("pushToStack", "pushToStack.html",
	"pushToStack (thisStack, thisElement)",
	"Adds the new element thisElement at the top of the stack thisStack.",
	"No return value.")
FUNCTION_DOC ("quitGame", "quitGame.html",
	"quitGame ()",
	"Closes the SLUDGE engine.",
	"No return value.")
FUNCTION_DOC ("quitWithFatalError", "quitWithFatalError.html",
	"quitWithFatalError (errorMessage)",
	"Quits your game with the string errorMessage you specify shown in a message box.",
	"No return value. In fact, it doesn't even return. Because, of course, it quits your game.")
FUNCTION_DOC ("random", "random.html",
	"random (n)",
	"Generates a random number. The value of n must be greater than zero.",
	"The return value is the number generated, from 0 to n - 1 inclusive (i.e. there are n possible outcomes).")
FUNCTION_DOC ("removeAllCharacters", "removeAllCharacters.html",
	"removeAllCharacters ()",
	"Removes all of the characters (created using the addCharacter command) from the scene. Only those that have been given the NOREMOVE setting by setCharacterExtra will remain.",
	"No return value.")
FUNCTION_DOC ("removeAllScreenRegions", "removeAllScreenRegions.html",
	"removeAllScreenRegions ()",
	"Removes all screen regions (created using the addScreenRegion command) from the scene.",
	"No return value.")
FUNCTION_DOC ("removeCharacter", "removeCharacter.html",
	"removeCharacter (thisCharacter)",
	"Removes the character thisCharacter from the scene.\n\nNote that if the character is talking or thinking, the speech will be removed from the screen and the function from which one of the say and think commands was called will continue. However, if the character is moving (because of a call to either the moveCharacter or forceCharacter command) execution of the function which called the movement command will not continue.",
	"No return value.")
FUNCTION_DOC ("removeLastStatus", "removeLastStatus.html",
	"removeLastStatus ()",
	"Removes one status bar from the set of status bars which currently exist. The status bar to be deleted is the lowest status bar on the screen (assuming that a positive value has been used for the font's height when it was loaded using the setFont command): that is, the most recently added of all the status bars.",
	"No return value.")
FUNCTION_DOC ("removeScreenRegion", "removeScreenRegion.html",
	"removeScreenRegion (thisObject)",
	"Removes any screen regions (added using the addScreenRegion command) which represent the object thisObject.",
	"No return value.")
FUNCTION_DOC ("rename", "rename.html",
	"rename (thisObject, newName)",
	"Changes the name of thisObject to the string newName. This will only affect an object if it is currently on the screen either as a character (added using the addCharacter function) or a screen region (added using the addScreenRegion function).",
	"No return value.")
FUNCTION_DOC ("renameFile", "renameFile.html",
	"renameFile (oldFilename, newFilename)",
	"Renames the file specified by oldFilename to newFilename - which should both be strings, not file handles. If you're writing a custom save game mechanism, you may require this functionality... so here it is. The strings oldFilename and newFilename shouldn't contain any of the characters \":\", \"/\" and \"\\\" - a precaution to make it more difficult to write malicious code.\n\nThis function supports filename bodging - for details see the bodgeFilenames command.",
	"The command returns TRUE if the renaming was a success, FALSE otherwise.")
FUNCTION_DOC ("saveCustomData", "loadCustomData_and_saveCustomData.html",
	"saveCustomData (thisStack, filenameString)",
	"The saveCustomData function creates a file with the name filenameString and stores the contents of the variable thisStack (which must, as you probably guessed, be a stack) in the file. The loadCustomData function loads data back from a file that has been created using saveCustomData.\n\nPlease note that you can't store custom data in the main .ini file. Use another name for your custom settings .ini file.\n\nThe method by which loadCustomData and saveCustomData treat the files they act upon and the types of data which can be stored are determined by the current custom encoding setting - see setCustomEncoding for details. If a game attempts to read a file with a different custom encoding setting than the one which was used when the file was created then a fatal error will occur, in order to stop others reading your custom data files unless they know the setting you used.",
	"The loadCustomData function returns a stack containing the data which was just loaded. The saveCustomData function does not return a value.")
FUNCTION_DOC ("saveGame", "saveGame.html",
	"saveGame (text)",
	"Creates a file with the name text which contains the current state of the SLUDGE engine. The text value must be a string not a file handle for this command to work as expected! The loadGame command can then be used to load in one of these files to return the engine to exactly the same state.\n\nAny run-time files which your SLUDGE game uses (i.e. any custom data or saved game files) will be created in (and loaded from) the application data folder of the game. (Documents and Settings/username/Application Data/Game Name on Windows, $HOME/.sludge-engine/Game Name on Linux and ~/Application Support/Game Name on Mac). See also .\n\nYour saved game files, as well as containing all the information needed to restore a player's progress at any time, can also (optionally) contain a thumbnail image. The size of the image generated and saved can be set using setThumbnailSize.\n\nCurrently, games cannot be saved while anything is frozen (using the freeze command).\n\nNote that when the loadGame command is called successfully and the engine returns to its state when the file was created, execution will restart after the call to saveGame, not after the call to loadGame. This is because everything - including the function which called the saveGame command - is saved to the file and therefore restored when loadGame is called. You can tell whether you've just loaded or saved a game by looking at the return value.\n\nThis function supports filename bodging - for details see the bodgeFilenames command.",
	"Returns TRUE if the state of the engine was just saved. Returns FALSE if the game has just restarted at this point after a call to loadGame.")
FUNCTION_DOC ("say", "say_and_think.html",
	"say (thisObject, textToSay)\nsay (thisObject, textToSay, soundFile)",
	"Makes thisObject say the contents of the textToSay parameter (or think the contents of the textToThink parameter). The text is shown on the screen in the speech colour defined in the object type definition. If a soundFile parameter is specified, the sound may be used instead of (or as well as) the text - this depends on the current setSpeechMode setting.\n\nIf thisObject is a character:\n\nAny displayed text will be shown over the head of the character. If the say command is being used, the character's current animation will be changed to the speaking animation (for the character facing its current direction) defined in the costume assigned to the character. When the character has finished speaking, the character's animation will change to the standing still animation. If the think command is being used, the character will not be made to animate.\n\nIf thisObject is a screen region:\n\nAny displayed text will be shown above the last region to have been added which represents the object; that is, if there are three screen regions which represent the same object and the object is told to speak, the text will appear over the third region.\n\nIf thisObject isn't a character or screen region:\n\nAny displayed text will appear centred at the top of the screen.\n\nThe function from which the say command is called will pause until the object stops speaking. The time this takes depends on the number of characters in textToSay or textToThink and is calculated by the SLUDGE engine at runtime. If the soundFile parameter is specified (and used), the time for which the function pauses is the length of time needed to play the sound.\n\nValid file formats for the soundFile parameter are Windows wave files (.WAV) and Ogg Vorbis (.OGG), as in the playSound and loopSound functions.",
	"No return value.")
FUNCTION_DOC ("setBackgroundEffect", "setBackgroundEffect.html",
	"setBackgroundEffect (add, divide, array [, array...])",
	"The setBlankColour function is used to specify an effect which can be applied to the background image. The doBackgroundEffect function applies the current effect to the background. The parameters for the function are an add value, a divide value and one or more arrays (which you can think of as a matrix, should you wish). When an effect is applied, the following procedure happens for each pixel: SLUDGE adds up the colour values of each pixel and its surrounding pixels, weighted using the arrays you've specified. For example, if you specified one array, containing just the value 1, it would get the original red, green and blue values of that pixel. If you specified one array, containing the values 1 three times, it would get red, green and blue totals equivalent to the red, green and blue values of the pixel itself, plus the red, green and blue values of the pixels either side. These values are then divided by the divide value and finally the add value is added to each. These red, green and blue values are then used as the new colour for the pixel (clamped to the range 0 to 255). So to get an average of each pixel and it's left-hand and right-hand neighbours, you could say setBackgroundEffect (0, 3, newStack (1, 1, 1)). If multiple arrays are specified, these are used to also add the red, green and blue values from pixels the line above or below the pixel having its colour changed.",
	"No return values.")
FUNCTION_DOC ("setBlankColour", "setBlankColour_(setBlankColor).html",
	"setBlankColour (red, green, blue)",
	"Changes the colour used by the any functions which blank a portion of the screen. Currently, these functions are blankArea, blankScreen and hardScroll. The values of red, green and blue should all be numbers between 0 and 255 inclusive. The command setBlankColor is identical. The default blank colour is black (0, 0, 0).",
	"No return value.")
FUNCTION_DOC ("setBurnColour", "setBurnColour_(setBurnColor).html",
	"setBurnColour (red, green, blue)",
	"Changes the colour in which the text added to the screen by burnString is drawn. The values of red, green and blue should all be numbers between 0 and 255 inclusive. The command setBurnColor is identical.",
	"No return value.")
FUNCTION_DOC ("setCharacterAngleOffset", "setCharacterAngleOffset.html",
	"setCharacterAngleOffset (thisCharacter, value)",
	"Adds an offset to the angle of thisCharacter. The value is used in the calculations deciding which direction (see costume) a character should face. An offset of 0 (the default) means that the base direction of a character is facing the player.",
	"No return value.")
FUNCTION_DOC ("setCharacterColorise", "setCharacterColourise.html",
	"setCharacterColorise (thisCharacter, red, green, blue, value)",
	"Blends the character with the given colour. A value of 0 draws the character normally. A value of 255 replaces all colour with the given colour. (Alpha values are unaffected.)",
	"TRUE if thisCharacter is an object type which is currently represented on the screen as a character, otherwise FALSE (i.e. TRUE if the call to the function was a success).")
FUNCTION_DOC ("setCharacterDrawMode", "setCharacterDrawMode.html",
	"setCharacterDrawMode (thisCharacter, mode)",
	"Changes the way in which thisCharacter is displayed. Possible values for mode are:\n\nNORMAL The character is drawn at full brightness with full visibility. This is equivalent to calling: setCharacterColourise (char, 0, 0, 0, 0); and setCharacterTransparency (char, 0);\n\nTRANSPARENT1 The character is drawn at full brightness with 75% visibility. This is equivalent to calling: setCharacterColourise (char, 0, 0, 0, 0); and setCharacterTransparency (char, 64);\n\nTRANSPARENT2 The character is drawn at full brightness with 50% visibility. This is equivalent to calling: setCharacterColourise (char, 0, 0, 0, 0); and setCharacterTransparency (char, 128);\n\nTRANSPARENT Identical to TRANSPARENT2.\n\nTRANSPARENT3 The character is drawn at full brightness with 25% visibility. This is equivalent to calling: setCharacterColourise (char, 0, 0, 0, 0); and setCharacterTransparency (char, 192);\n\nINVISIBLE The character is not shown (note that the mouse will still react to it). This is equivalent to calling: setCharacterColourise (char, 0, 0, 0, 0); and setCharacterTransparency (char, 254);\n\nSHADOW1 The character is drawn as a silhouette with 75% visibility. This is equivalent to calling: setCharacterColourise (char, 255, 0, 0, 0); and setCharacterTransparency (char, 64);\n\nSHADOW2 The character is drawn at as a silhouette with 50% visibility. This is equivalent to calling: setCharacterColourise (char, 255, 0, 0, 0); and setCharacterTransparency (char, 128);\n\nSHADOW Identical to SHADOW2.\n\nSHADOW3 The character is drawn at as a silhouette with 25% visibility. This is equivalent to calling: setCharacterColourise (char, 255, 0, 0, 0); and setCharacterTransparency (char, 192);\n\nDARK1 The character is drawn at 75% brightness with full visibility. This is equivalent to calling: setCharacterColourise (char, 64, 0, 0, 0); and setCharacterTransparency (char, 0);\n\nDARK2 The character is drawn at 50% brightness with full visibility. This is equivalent to calling: setCharacterColourise (char, 128, 0, 0, 0); and setCharacterTransparency (char, 0);\n\nDARK Identical to DARK2.\n\nDARK3 The character is drawn at 25% brightness with full visibility. This is equivalent to calling: setCharacterColourise (char, 192, 0, 0, 0); and setCharacterTransparency (char, 0);\n\nDARK4 The character is drawn as a silhouette with full visibility. This is equivalent to calling: setCharacterColourise (char, 255, 0, 0, 0); and setCharacterTransparency (char, 0);\n\nBLACK Identical to DARK4.\n\nFOGGY1 The character is drawn at 75% of its original colour, 25% medium grey. This is equivalent to calling: setCharacterColourise (char, 64, 128, 128, 128); and setCharacterTransparency (char, 0);\n\nFOGGY2 The character is drawn at 50% of its original colour, 50% medium grey. This is equivalent to calling: setCharacterColourise (char, 128, 128, 128, 128); and setCharacterTransparency (char, 0);\n\nFOGGY Identical to FOGGY2.\n\nFOGGY3 The character is drawn at 25% of its original colour, 75% medium grey. This is equivalent to calling: setCharacterColourise (char, 192, 128, 128, 128); and setCharacterTransparency (char, 0);\n\nFOGGY4 The character is drawn as a solid block of medium grey. This is equivalent to calling: setCharacterColourise (char, 255, 128, 128, 128); and setCharacterTransparency (char, 0);\n\nGREY Identical to FOGGY4.\n\nGRAY Identical to FOGGY4.\n\nGLOW1 The character is drawn at 75% of its original colour, 25% bright white. This is equivalent to calling: setCharacterColourise (char, 64, 255, 255, 255); and setCharacterTransparency (char, 0);\n\nGLOW2 The character is drawn at 50% of its original colour, 50% bright white. This is equivalent to calling: setCharacterColourise (char, 128, 255, 255, 255); and setCharacterTransparency (char, 0);\n\nGLOW Identical to GLOW2.\n\nGLOW3 The character is drawn at 25% of its original colour, 75% bright white. This is equivalent to calling: setCharacterColourise (char, 192, 255, 255, 255); and setCharacterTransparency (char, 0);\n\nGLOW4 The character is drawn as a solid block of bright white. This is equivalent to calling: setCharacterColourise (char, 255, 255, 255, 255); and setCharacterTransparency (char, 0);\n\nWHITE Identical to GLOW4.\n\nIf thisCharacter is an object type which is not currently represented on the screen as a character, the code has no effect. If thisCharacter has been hidden using the hideCharacter command, the character's draw mode is changed but the character is not made visible.",
	"TRUE if thisCharacter is an object type which is currently represented on the screen as a character, otherwise FALSE (i.e. TRUE if the call to the function was a success).")
FUNCTION_DOC ("setCharacterExtra", "setCharacterExtra.html",
	"setCharacterExtra (thisCharacter, setting)",
	"Changes the properties for thisCharacter which determine how it is to be drawn. The setting parameter should be one of the following enumerations, or the sum of two or more of the following should you wish to enable more than one of the effects.\n\nNORMAL Use this option on its own if you wish to enable none of the following features.\n\nFIXEDSIZE The character is drawn at a fixed size, regardless of the current scale settings (see the setScale command).\n\nFRONT The character is drawn in front of all non-FRONT characters.\n\nNOZBUFFER The character is drawn in its entirety, regardless of any z-buffer (set using the setZBuffer function).\n\nFIXTOSCREEN The character's x and y positions are used to determine where it is drawn on the screen/window (rather than in relation to the background image). In short... wherever the scene may scroll (using the aimCamera function) the character's image will remain in the same place in relation to the SLUDGE window.\n\nNOLIGHTMAP The character isn't affected by the loaded light-map (set using the setLightMap function).\n\nNOREMOVE The character isn't removed by the removeAllCharacters function (it can still be removed by the removeCharacter function).\n\nRECTANGULAR When working out if the mouse is over this character, use the whole rectangular region not just the visible (non-transparent) pixels.\n\nICON A shorter (and marginally quicker) way to specify all of the following features: FIXEDSIZE + FRONT + NOZBUFFER + FIXTOSCREEN + NOLIGHTMAP\n\nIf thisCharacter is an object type which is not currently represented on the screen as a character, the code has no effect. If thisCharacter has been hidden using the hideCharacter command, the character's properties are changed but the character is not made visible.",
	"TRUE if thisCharacter is an object type which is currently represented on the screen as a character, otherwise FALSE (i.e. TRUE if the call to the function was a success).")
FUNCTION_DOC ("setCharacterRig", "setCharacterRig.html",
	"setCharacterRig (character, rig)\nsetCharacterRig (character, 0)",
	"Gives character the parts of rig, so that it's drawn as a Composite Character instead of as one sprite.\n\nThe rig can come from newRig, if you built it in your script, or from loadRig, if you made it in the Character Creator. Give 0 instead of a rig to turn the character back into an ordinary one, with a single sprite.\n\nFrom now on, the clips of the character's costume, and the clips you give to animate, move its parts by their names. An animation made with anim moves the character as a whole.\n\nAdd the character with addCharacter first: there's nobody to give the rig to otherwise. Once a character uses the rig, the rig can't change any more, so add all its parts, pivots and depths before you call this. To move the parts after that, use clips (see Animation Clips) or setPartProperty.",
	"Returns TRUE if there is such a character, otherwise FALSE.")
FUNCTION_DOC ("setCharacterSpinSpeed", "setCharacterSpinSpeed.html",
	"setCharacterSpinSpeed (thisCharacter, value)",
	"Changes the speed at which thisCharacter spins when facing a new direction, either because of a call to moveCharacter or forceCharacter or a direct change of direction using the spinCharacter command.\n\nThe value parameter specifies the number of degrees through which a character can move each frame. For example, a value parameter of 45 would mean that the appropriate character, when facing south and being told to face north, would be shown facing south, south-east, east, north-east and, finally, north. A value parameter of 90 would mean that the character would only be shown facing south, east and then north.\n\nShould value be 0, the angle which the character turns per frame is calculated automatically from the number of different directions represented in the character's costume.",
	"No return value.")
FUNCTION_DOC ("setCharacterTransparency", "setCharacterTransparency.html",
	"setCharacterTransparency (thisCharacter, value)",
	"Sets the opacity of the character. A value of 0 = solid. A value of 255 = INVISIBLE",
	"TRUE if thisCharacter is an object type which is currently represented on the screen as a character, otherwise FALSE (i.e. TRUE if the call to the function was a success).")
FUNCTION_DOC ("setCharacterWalkSpeed", "setCharacterWalkSpeed.html",
	"setCharacterWalkSpeed (thisCharacter, value)",
	"Changes the speed at which thisCharacter moves around the current scene. The character must already have been added to the screen using the addCharacter command. When a character is added to the screen, its walking speed is set to the default walking speed as defined in the thisCharacter object type - if the object type contains no default walk speed information, the default walk speed is set to 5.\n\nIf thisCharacter is an object type which is not currently represented on the screen as a character, the code has no effect.",
	"TRUE if thisCharacter is an object type which is currently represented on the screen as a character, otherwise FALSE (i.e. TRUE if the call to the function was a success).")
FUNCTION_DOC ("setClipSpeed", "setClipSpeed.html",
	"setClipSpeed (character, layer, percent)",
	"Plays what character is playing on layer at percent of its normal speed.\n\n100 is normal, 50 is half speed, 200 is twice as fast, and 0 pauses it. Anything below 0 or above 100000 gives an error. It works for clips and for animations of anim, which makes slow motion easy.\n\nOn layers 1 to 99, the layer has to be playing already, so call setClipSpeed after playClip. Playing a new clip on the layer sets its speed back to 100%. The speed of layer 0, the costume's layer, stays for the animations that come after it.",
	"On layer 0, returns TRUE if there is such a character, otherwise FALSE. On the other layers, returns FALSE as well when nothing plays on the layer.")
FUNCTION_DOC ("setCostume", "setCostume.html",
	"setCostume (thisCharacter, thisCostume)",
	"Changes a character's costume. The character thisCharacter must have already been added to the screen using the addCharacter function.\n\nThe thisCostume parameter must be a complete costume (the result of a call to the costume function) or a single animation (the result of a call to the anim function).",
	"No return value.")
FUNCTION_DOC ("setCursor", "setCursor.html",
	"setCursor (thisAnimation)",
	"Changes the cursor which represents the mouse to thisAnimation which must be the result of a call to the anim function. If thisAnimation is not an animation (for example, NULL) the mouse is made invisible.",
	"No return value.")
FUNCTION_DOC ("setCustomEncoding", "setCustomEncoding.html",
	"setCustomEncoding (n)",
	"Changes the way in which files are treated when created by the saveCustomData function or read by the loadCustomData function.\n\nIf the value of n is NULL (or FALSE, or NORMAL, or anything else that equates to 0) then saveCustomData creates text files. Text files can only contain strings. Should you use saveCustomData to save a stack which contains anything other than a string, a string representing the value will be stored instead.\n\nIf the value of n is any number between 1 and 65535 then saveCustomData will create encoded data files. Encoded data files can contain strings and numbers but no other variable types. Should you try to use saveCustomData to save a stack which contains anything other than a string or a number the engine will close with a fatal error message. This is because custom data files should be usable by any SLUDGE game and costumes, animations, file handles and so on are all game-specific.\n\nIf the value of n is less than 0 or greater than 65535 then the behaviour of saveCustomData and loadCustomData is not guaranteed. Don't do it. It's just plain dumb.\n\nThe default custom encoding setting before setCustomEncoding is called is 0.",
	"No return value.\n\nSLUDGE and this SLUDGE documentation are copyright Hungry Software and contributors 2000-2012")
FUNCTION_DOC ("setDefaultMusicVolume", "setDefaultMusicVolume.html",
	"setDefaultMusicVolume (volume)",
	"Changes the volume at which music will be played by subsequent calls to the startMusic function. Any music currently playing is not affected - the volume for music which has already been started can be controlled using the setMusicVolume function.",
	"No return value.")
FUNCTION_DOC ("setDefaultSoundVolume", "setDefaultSoundVolume.html",
	"setDefaultSoundVolume (volume)",
	"Changes the volume at which sounds will be played by subsequent calls to the playSound and loopSound functions. Any sounds currently playing are not affected - the volume for sounds which have already been started can be controlled using the setSoundVolume function.",
	"No return value.")
FUNCTION_DOC ("setFloor", "setFloor.html",
	"setFloor (floorFile)",
	"Sets a new floor plan, loading it from the floorFile file. Characters which are told to walk using the moveCharacter function will only be able to move within the specified floor.\n\nIf floorFile is NULL (or anything other than a file handle) the current floor will be removed. With no floor loaded, calls to moveCharacter will have no effect.",
	"No return value.")
FUNCTION_DOC ("setFont", "setFont.html",
	"setFont (spriteBank, charactersSupported, height)",
	"Changes to a new font. The font must be contained in a sprite bank, and the spriteBank parameter should be a file handle for this file - that is, the name of the file in single quotes. The charactersSupported parameter must be a string made up of - in order - each of the characters which are supported in this font. Therefore, if the fifth character in the charactersSupported string is A, the character A will be drawn on the screen using the fifth image in the sprite bank file.\n\nCharacters which are not present in the charactersSupported string are drawn as the first sprite in the sprite bank. It is therefore wise to have the first character in the font be a symbol used to represent this fact, such as an empty box or a question mark. The function inFont can be used to check if a character exists in the current font.\n\nThe height value must be numerical, and will be used as the gap between rows of text when using multiple status bars (created by the addStatus command) and when spoken text (created by the say and think commands) wraps onto more than one line.",
	"No return value.")
FUNCTION_DOC ("setFontSpacing", "setFontSpacing.html",
	"setFontSpacing (spacingValue)",
	"Sets how much of a gap is added between characters when pasting text onto the screen. Positive values for spacingValue mean a gap of the specified number of pixels. Negative values mean characters overlap. A value of 0 means characters are placed snugly next to each other, with no gap nor overlap.\n\nThe initial spacing value... i.e. how much space there is between characters before setFontSpacing has been called... is -1 (each character overlaps the last by 1 pixel).",
	"No return value.")
FUNCTION_DOC ("setLightMap", "setLightMap.html",
	"setLightMap (imageFile)\nsetLightMap (imageFile, lightMapMode)",
	"Sets a new light-map, loading it from the imageFile file. Each pixel of the light-map corresponds to a pixel of the background image. With a light-map loaded, every character's red, green and blue colour values will be multiplied by the red, green and blue values of the pixel in the imageFile at the corresponding location.\n\nThe light-map arithmetic is applied before a character's draw mode (set using the setCharacterDrawMode function) is taken into consideration. This means that light-maps behave as expected even when dealing with transparent characters, but may not give you the results you expect when using FOGGY1 to FOGGY4.\n\nIf imageFile is NULL (or anything other than a file handle) the current light-map will be removed. With no light-map loaded, all characters will be drawn at full colour (after any colour changes due to the character's current drawing mode).\n\nIf you specify a lightMapMode the current method for applying a light map to characters will be changed. If you don't specify this parameter, the current method for applying a light map will stay the same. The default method is PERPIXEL. Valid values for the lightMapMode parameter are:\n\nPERPIXEL: Each pixel of a character is multiplied by the light map colour in the same position - i.e. one character will have many different colours applied to it in different places. In this mode, the light map is stretched to fill the entire scene, regardless of actual size in pixels. You can keep the lightmaps smaller to save memory and still get good results.\n\nHOTSPOT: Each pixel of a character is multipled by the light map colour at the character's hotspot (base point) - i.e. each character will only have one colour applied to it, determined by where it is currently positioned. In HOTSPOT mode, the imageFile image must be the same width and height as the current scene dimensions, as set by the most recent call to the setSceneDimensions function.\n\nCharacters whose extra properties - set using setCharacterExtra - include the NOLIGHTMAP option are not affected by light-maps.",
	"No return value.")
FUNCTION_DOC ("setLitStatusColour", "setLitStatusColour_(setLitStatusColor).html",
	"setLitStatusColour (red, green, blue)",
	"Changes the colour in which the text in a status bar is drawn when the status bar is lit. The values of red, green and blue should all be numbers between 0 and 255 inclusive. The command setLitStatusColor is identical.",
	"No return value.")
FUNCTION_DOC ("setMusicVolume", "setMusicVolume.html",
	"setMusicVolume (volume, channel)",
	"Changes the volume level for the specified channel of music. If there is no music currently playing on the specified channel the code will have no effect. The volume parameter must be a number between 0 and 255 inclusive, and changes take effect immediately.",
	"No return value.")
FUNCTION_DOC ("setPartDepth", "setPartDepth.html",
	"setPartDepth (rig, name, depth)",
	"Sets which parts of a character the part called name is drawn in front of, and which it's drawn behind.\n\nParts with a lower depth are drawn first, so they end up behind the others. Parts of the same depth are drawn in the order you added them. Every part starts at 0, so giving the far arm a depth of -1 puts it behind the body.\n\nThis is where the part sits normally. A clip can still move it for a while with PART_DEPTH (see clipKey), which is handy for bringing the far arm forward when the character turns round. Depth only sorts out the parts of one character: where the character stands among other characters and the scenery still depends on where it is on the screen, as always.\n\nCall it while you build the rig, before you give the rig to a character with setCharacterRig, because after that the rig can't change. A name that isn't a part of the rig stops the game with an error. In the Character Creator, this is the Depth of a part in the Inspector.",
	"No return value.")
FUNCTION_DOC ("setPartPivot", "setPartPivot.html",
	"setPartPivot (rig, name, x, y)",
	"Sets the pivot of the part called name in rig: the point that the part turns, scales and flips around.\n\nNormally a part turns around the hotspot of its sprite, which you set in the Sprite Bank Editor. That's fine until you have an arm whose hotspot is in the middle, and it spins like a propeller instead of swinging from the shoulder. The pivot moves that point by x, y pixels of the sprite (+X is right, +Y is down), without touching the sprite bank, which other parts or characters may be using too.\n\nThe pivot is also the point that the x and y of addPart place. So setPartPivot moves the sprite around that point, and the part's place, and the parts hanging from it, stay where they are. (In the Character Creator it's the other way round: dragging the pivot keeps the sprite where it is, and moves the part.)\n\nCall it while you build the rig, before you give the rig to a character with setCharacterRig, because after that the rig can't change. A name that isn't a part of the rig stops the game with an error.",
	"No return value.")
FUNCTION_DOC ("setPartProperty", "setPartProperty.html",
	"setPartProperty (character, part, property, value)",
	"Sets a property of a part of character straight from your script, and keeps it there, whatever the character's clips are doing.\n\nClips are great for movements you know in advance, but some things depend on the game: a head that turns to follow the mouse, say, or an arm that holds up whatever the player just picked up. For those, set the property here. It wins over every clip on every layer, and stays set until you call clearPartProperty.\n\nThese are the properties, with the same units as in clipKey:\n\nThe value is always a whole number. Sprites can't be set this way: play a clip with clipSprite on a layer instead (see playClip).\n\nWatch the spelling of the part's name! A name that isn't a part of the character doesn't give an error: the property is set, but nothing on the screen changes, and you're left wondering why.",
	"Returns TRUE if there is such a character, otherwise FALSE.")
FUNCTION_DOC ("setPasteColour", "setPasteColour_(setPasteColor).html",
	"setPasteColour (red, green, blue)",
	"Changes the colour in which the text added to the screen by pasteString is drawn. The values of red, green and blue should all be numbers between 0 and 255 inclusive. The command setPasteColor is identical.",
	"No return value.")
FUNCTION_DOC ("setScale", "setScale.html",
	"setScale (horizon, divide)",
	"Changes the scaling for characters. The horizon value represents the level (y co-ordinate) of the horizon on the screen. The divide parameter is how quickly characters grow and shrink as they move towards or away from the horizon. The scale at which a character is drawn is calculated using the algorithm scale = (y position - horizon) / divide.\n\nIf the divide value is 0, no scaling will be applied to characters when they move; each character will continue to be drawn at the size in which it was previously drawn. In this situation, new characters added using addCharacter will be created with a scale value of 1.\n\nChanging the scale does not affect characters which aren't currently moving. Adding a character and then changing the scale will not resize the character, until the character is told to move.",
	"No return value.")
FUNCTION_DOC ("setSceneDimensions", "setSceneDimensions.html",
	"setSceneDimensions (newWidth, newHeight)",
	"Changes the width and height of the background image used for the current scene to newWidth and newHeight. Setting a newWidth larger than the screen or window width, or a newHeight larger than the screen or window height, will enable scrolling around the scene using the aimCamera function. Setting the dimensions to an area either less wide or less tall than the window or screen will stop the program with a fatal error.\n\nThis function also clears the background image.",
	"No return value.")
FUNCTION_DOC ("setSoundLoopPoints", "setSoundLoopPoints.html",
	"setSoundLoopPoints (soundFile, startPoint, endPoint)",
	"Changes the start and end points used when looping a sound. The soundFile file must be looping already (by calling loopSound with the same soundFile parameter). The new start and end points for looping the sound are then changed to startPoint and endPoint respectively. If startPoint is beyond the end of the sample, the start of the sample (position 0) is used instead. If endPoint is 0 or beyond the end of the sample, the end point is set to the end of the sample.",
	"")
FUNCTION_DOC ("setSoundVolume", "setSoundVolume.html",
	"setSoundVolume (soundFile, newVolume)",
	"Changes the volume of any occurrences of the sampled sound soundFile which are currently playing. The volume of each occurrence is set to newVolume, which should be a number from 0 (silence) to 255 (full).\n\nThis function cannot be used to change the volume of track-based music files. For that, use the setMusicVolume function.",
	"No return value.")
FUNCTION_DOC ("setSpeechMode", "setSpeechMode.html",
	"setSpeechMode (newMode)",
	"Changes the way in which the say and think functions is dealt with when both text and a sound file are provided. The parameter newMode should be one of the following:\n\nSOUNDANDTEXT Both text and recorded speech are used.\n\nSOUNDONLY Only recorded speech is used. Text is not displayed.\n\nTEXTONLY No recorded speech is used and text is displayed.",
	"No return value.")
FUNCTION_DOC ("setSpeechSpeed", "setSpeechSpeed.html",
	"setSpeechSpeed (newSpeed)",
	"Changes the rate at which text is displayed by the say and think functions. The newSpeed value is a percentage of the default speed (which is the number of characters in the string to be spoken plus 20). Therefore, a value of 200 will make speech appear for twice as long; a value of 50 will make speech appear for half as long.\n\nCalling this function doesn't affect any speech which is already on the screen.",
	"No return value.")
FUNCTION_DOC ("setStatusColour", "setStatusColour_(setStatusColor).html",
	"setStatusColour (red, green, blue)",
	"Changes the colour in which the text in status bars is drawn. The values of red, green and blue should all be numbers between 0 and 255 inclusive. The command setStatusColor is identical.",
	"No return value.")
FUNCTION_DOC ("setThumbnailSize", "setThumbnailSize.html",
	"setThumbnailSize (width, height)",
	"Sets the width and height of the thumbnail images created by saveGame. You can then retrieve a thumbnail image from a saved game file using the showThumbnail function.\n\nIf either width or height is set to 0, no thumbnail images will be created when games are saved.",
	"No return value.")
FUNCTION_DOC ("setZBuffer", "setZBuffer.html",
	"setZBuffer (zBufferFile)",
	"Sets a new z-buffer, loading it from the zBufferFile file.\n\nIf zBufferFile is NULL (or anything other than a file handle) the current z-buffer will be removed. With no z-buffer loaded, all characters will be visible wherever they are placed on the screen.\n\nIf zBufferFile is a valid file handle, the z-buffer to be loaded must be the same width and height as the current scene, as set using the setSceneDimensions function.",
	"No return value.")
FUNCTION_DOC ("showBoxes", "showBoxes.html",
	"showBoxes ()",
	"Makes the outlines of any screen regions (created using the using the addScreenRegion command) visible on the screen. A shaped region, made from a list of points, shows its own outline rather than a box. The outlines are drawn in such a way that a second call to showBoxes will remove the boxes from the screen.\n\nNote that these outlines are actually drawn onto the background image, so if the background image changes while boxes are visible it may not be possible to remove all traces of the boxes using subsequent calls to the showBoxes function. For this reason it is suggested that the showBoxes command is only used during development of a game and not included in a final version.",
	"No return value.")
FUNCTION_DOC ("showCharacter", "showCharacter.html",
	"showCharacter (thisCharacter)",
	"Makes thisCharacter visible. When characters are added to the screen they are initially visible - you will only need this command if a character has previously been made invisible using the hideCharacter function.",
	"No return value.")
FUNCTION_DOC ("showFloor", "showFloor.html",
	"showFloor ()",
	"Makes the outline of the current floor (loaded using the using the setFloor command) visible on the screen. The outline is drawn in such a way that a second call to showFloor will remove the lines from the screen.\n\nNote that these outlines are actually drawn onto the background image, so if the background image changes while floor outline is visible it may not be possible to remove all traces of the outline using subsequent calls to the showFloor function. For this reason it is suggested that the showFloor command is only used during development of a game and not included in a final version.",
	"No return value.")
FUNCTION_DOC ("showThumbnail", "showThumbnail.html",
	"showThumbnail (filename, x, y)",
	"Retrieves the thumbnail image from a saved game file (filename) and displays the image on the screen at the co-ordinates x, y (top left corner).\n\nIf filename is a valid saved game which contains no thumbnail image, the code has no effect. If the file does not exist your game will exit with the appropriate fatal error - therefore it's a good idea to check whether the fileExists first.\n\nThis function supports filename bodging - for details see the bodgeFilenames command.",
	"No return value.")
FUNCTION_DOC ("skipSpeech", "skipSpeech.html",
	"skipSpeech ()",
	"Removes any spoken text from the screen and continues any function paused because of a call to one of the say and think commands. If the text is being spoken by a character, the character reverts to the animation defined in its current costume for standing still while facing its current direction.",
	"No return value.")
FUNCTION_DOC ("snapshotClear", "snapshotClear.html",
	"snapshotClear ()",
	"Removes from memory a snapshot image taken with the snapshotGrab function.",
	"No return value.")
FUNCTION_DOC ("snapshotGrab", "snapshotGrab.html",
	"snapshotGrab ()",
	"Takes a snapshot of whatever is currently on the screen (or rather, the area of background image that's currently visible, plus any characters on it - in other words, what the screen will look like the next time it is drawn provided nothing changes in the meantime).\n\nThis snapshot can then be used to crossfade one image into another, or even one room into another. To achieve this use the CROSSFADE transition mode and gradually change the transition level from 0 to 256 using the transitionMode and transitionLevel functions.\n\nWhen you no longer need your snapshot image, use snapshotClear to remove it from memory.",
	"No return value.\n\nSLUDGE and this SLUDGE documentation are copyright Hungry Software and contributors 2000-2012")
FUNCTION_DOC ("somethingSpeaking", "somethingSpeaking.html",
	"somethingSpeaking ()",
	"Checks whether anything is speaking or thinking (characters and objects can be made to speak or think using the say and think commands).",
	"Returns FALSE if nothing is speaking or thinking. Otherwise returns the object type of whatever's speaking or thinking.")
FUNCTION_DOC ("spawnSub", "spawnSub.html",
	"spawnSub (thisFunction)",
	"Starts thisFunction while continuing the current function at the same time. The thisFunction function must take no parameters and will be started the next time the screen updates. Execution of the function containing the call to spawnSub will continue first.",
	"No return value.")
FUNCTION_DOC ("spinCharacter", "turnCharacter_and_spinCharacter.html",
	"spinCharacter (thisCharacter, direction)",
	"Both the turnCharacter function and spinCharacter function will turn thisCharacter to face a new direction. However, turnCharacter will make the character face in his/her new direction immediately whereas spinCharacter will make the character rotate on the spot until the new direction is reached (the rate at which this happens being the character's current spin speed). Valid values for direction are any number between 0 and 359 inclusive or one of the constants NORTH, NORTHEAST, EAST, SOUTHEAST, SOUTH, SOUTHWEST, WEST or NORTHWEST (which enumerate to 0, 45, 90, 135, 180, 225, 270 and 315 respectively).\n\nIf using turnCharacter to rotate a character, the current function will continue immediately. If using spinCharacter to rotate a character, the current function will be paused until the action is completed (pausing for a minimum of 1 frame).",
	"No return value.")
FUNCTION_DOC ("stackSize", "stackSize.html",
	"stackSize (thisStack)",
	"Counts the number of elements in the stack thisStack. It works on a fast array too (see makeFastArray).",
	"The function returns the number of elements in the stack or fast array.")
FUNCTION_DOC ("startMusic", "startMusic.html",
	"startMusic (musicFile, channel, startPos)",
	"Starts playing a piece of music at the volume set by the setDefaultMusicVolume function. The musicFile parameter must be a file handle - the channel parameter must be a number from 0 to 3 inclusive. Only one piece of music can be played on each channel at once.\n\nThe startPos defines where in a track-based music file to begin playback. This allows you to start the same piece of music in different places dependant on the game situation... or to store more than one piece of music in the same file.\n\nFile formats supported by this function are .MOD, .XM, .S3M and .IT files. This function cannot play .WAV or .OGG files - to start playback of one of these, you must use either the playSound or loopSound function.",
	"No return value.")
FUNCTION_DOC ("statusText", "statusText.html",
	"statusText (thisString)",
	"Changes the text which is visible in the current status bar to the contents of the thisString parameter. If there is more than one status bar currently on the screen, the text in the most recent status bar is set.",
	"No return value.")
FUNCTION_DOC ("stopCharacter", "stopCharacter.html",
	"stopCharacter (thisCharacter)",
	"Stops thisCharacter from moving. The command does not stop any animation if the character isn't currently moving.",
	"TRUE if thisCharacter is an object type which is currently represented on the screen as a character and was previously moving, otherwise FALSE (i.e. TRUE if the call to the function was a success).")
FUNCTION_DOC ("stopClip", "stopClip.html",
	"stopClip (character, layer)",
	"Stops the clip on layer of character, and takes the layer away.\n\nWhatever the layer did to the character's parts goes with it, so the parts go back to what the layers below say. Use it for layers 1 to 99. Layer 0 belongs to the costume and can't be stopped: trying gives an error. If nothing is playing on the layer, nothing happens.",
	"Returns FALSE if there's no such character, otherwise TRUE, even if nothing was playing on the layer.")
FUNCTION_DOC ("stopMovie", "stopMovie.html",
	"stopMovie ()",
	"Stops a playing movie, started with playMovie. If no movie is playing, this function does nothing.\n\nSee the Movie Demo (download link) for an example of how to use the movie functions.",
	"Returns 1 if a movie was playing, or 0 if not.")
FUNCTION_DOC ("stopMusic", "stopMusic.html",
	"stopMusic (channel)",
	"Stops the music which is currently playing on the specified channel. If there's nothing playing on the specified channel this code will have no effect. The value of channel must be a number between 0 and 3 inclusive and correspond to the number used when the startMusic command started the music playing.",
	"No return value.")
FUNCTION_DOC ("stopSound", "stopSound.html",
	"stopSound (soundFile)",
	"If the sound in the soundFile file is playing, it is stopped.",
	"No return value.")
FUNCTION_DOC ("stringLength", "stringLength.html",
	"stringLength (thisString)",
	"Counts the number of characters in the string thisString.",
	"The function returns the number of characters.")
FUNCTION_DOC ("stringWidth", "stringWidth.html",
	"stringWidth (thisString)",
	"Returns the width, in pixels, of the string thisString using the current font.",
	"The function returns the total number of pixels for each of the characters contained in the string.")
FUNCTION_DOC ("substring", "substring.html",
	"substring (thisString, start, numCharacters)",
	"Makes a new string, containing 0 or more characters from the string thisString. The new string will start with the character at position start and be numCharacters long. Note that the value of start is zero-based... the first character in the string is character 0, the second is character 1 and so on.",
	"The function returns the new string.")
FUNCTION_DOC ("think", "say_and_think.html",
	"think (thisObject, textToThink)\nthink (thisObject, textToThink, soundFile)",
	"Makes thisObject say the contents of the textToSay parameter (or think the contents of the textToThink parameter). The text is shown on the screen in the speech colour defined in the object type definition. If a soundFile parameter is specified, the sound may be used instead of (or as well as) the text - this depends on the current setSpeechMode setting.\n\nIf thisObject is a character:\n\nAny displayed text will be shown over the head of the character. If the say command is being used, the character's current animation will be changed to the speaking animation (for the character facing its current direction) defined in the costume assigned to the character. When the character has finished speaking, the character's animation will change to the standing still animation. If the think command is being used, the character will not be made to animate.\n\nIf thisObject is a screen region:\n\nAny displayed text will be shown above the last region to have been added which represents the object; that is, if there are three screen regions which represent the same object and the object is told to speak, the text will appear over the third region.\n\nIf thisObject isn't a character or screen region:\n\nAny displayed text will appear centred at the top of the screen.\n\nThe function from which the say command is called will pause until the object stops speaking. The time this takes depends on the number of characters in textToSay or textToThink and is calculated by the SLUDGE engine at runtime. If the soundFile parameter is specified (and used), the time for which the function pauses is the length of time needed to play the sound.\n\nValid file formats for the soundFile parameter are Windows wave files (.WAV) and Ogg Vorbis (.OGG), as in the playSound and loopSound functions.",
	"No return value.")
FUNCTION_DOC ("transitionLevel", "transitionMode_and_transitionLevel.html",
	"transitionLevel (level)",
	"The transitionMode function chooses which transition type is used by the game. Valid values for mode are:\n\nFADE Every pixel's brightness is faded to / from black.\n\nDISOLVE A fixed selection of pixels is coloured over in black.\n\nDISOLVE1 The same as DISOLVE.\n\nDISOLVE2 A random selection of pixels is coloured over in black.\n\nTVSTATIC A random selection of pixels is coloured over in random greys.\n\nBLINDS The screen is divided into horizontal slices which open like blinds.\n\nSNAPSHOTFADE The screen is faded to / from the current snapshot. *\n\nCROSSFADE The same as SNAPSHOTFADE.\n\nSNAPSHOTBOX The screen appears in an expanding rectangle over the current snapshot. *\n\nThe transitionLevel function specifies how visible the current screen is, from 0 to 256 inclusive. A level value of 0 means the scene is not visible at all (normally pitch black). A level value of 256 means the scene is completely visible and not affected by the current transition mode at all.\n\nNote that the mouse cursor is not affected by transitions. All other elements - including speech, characters and status text - are affected.\n\n* = See snapshotGrab for more information. This transition mode has no effect if there is currently no snapshot in memory.",
	"No return value.\n\nSLUDGE and this SLUDGE documentation are copyright Hungry Software and contributors 2000-2012")
FUNCTION_DOC ("transitionMode", "transitionMode_and_transitionLevel.html",
	"transitionMode (mode)",
	"The transitionMode function chooses which transition type is used by the game. Valid values for mode are:\n\nFADE Every pixel's brightness is faded to / from black.\n\nDISOLVE A fixed selection of pixels is coloured over in black.\n\nDISOLVE1 The same as DISOLVE.\n\nDISOLVE2 A random selection of pixels is coloured over in black.\n\nTVSTATIC A random selection of pixels is coloured over in random greys.\n\nBLINDS The screen is divided into horizontal slices which open like blinds.\n\nSNAPSHOTFADE The screen is faded to / from the current snapshot. *\n\nCROSSFADE The same as SNAPSHOTFADE.\n\nSNAPSHOTBOX The screen appears in an expanding rectangle over the current snapshot. *\n\nThe transitionLevel function specifies how visible the current screen is, from 0 to 256 inclusive. A level value of 0 means the scene is not visible at all (normally pitch black). A level value of 256 means the scene is completely visible and not affected by the current transition mode at all.\n\nNote that the mouse cursor is not affected by transitions. All other elements - including speech, characters and status text - are affected.\n\n* = See snapshotGrab for more information. This transition mode has no effect if there is currently no snapshot in memory.",
	"No return value.\n\nSLUDGE and this SLUDGE documentation are copyright Hungry Software and contributors 2000-2012")
FUNCTION_DOC ("turnCharacter", "turnCharacter_and_spinCharacter.html",
	"turnCharacter (thisCharacter, direction)",
	"Both the turnCharacter function and spinCharacter function will turn thisCharacter to face a new direction. However, turnCharacter will make the character face in his/her new direction immediately whereas spinCharacter will make the character rotate on the spot until the new direction is reached (the rate at which this happens being the character's current spin speed). Valid values for direction are any number between 0 and 359 inclusive or one of the constants NORTH, NORTHEAST, EAST, SOUTHEAST, SOUTH, SOUTHWEST, WEST or NORTHWEST (which enumerate to 0, 45, 90, 135, 180, 225, 270 and 315 respectively).\n\nIf using turnCharacter to rotate a character, the current function will continue immediately. If using spinCharacter to rotate a character, the current function will be paused until the action is completed (pausing for a minimum of 1 frame).",
	"No return value.")
FUNCTION_DOC ("unfreeze", "unfreeze.html",
	"unfreeze ()",
	"Returns the screen to the state it was in before it was frozen.",
	"No return value.")
FUNCTION_DOC ("wait", "wait.html",
	"wait (spriteNumber, lengthOfTime)",
	"For use while creating an animation. Use this command to display a sprite for longer than one frame.",
	"The function returns a structure containing both the spriteNumber and lengthOfTime data. This structure can then be used in the place of a number in the anim command. Internally, the structure is a 2-element stack.")
FUNCTION_DOC ("watchScreenRegion", "watchScreenRegion.html",
	"watchScreenRegion (thisObject, onEnter, onLeave)",
	"Watches the screen regions of thisObject, so that the function onEnter runs whenever a character steps into one of them, and the function onLeave whenever a character steps out again.\n\nWhat counts is the character's hotspot: the point where it stands, which is the position you give to addCharacter or jumpCharacter. It doesn't matter how the character gets there. Walking, jumping or being forced all count, and so does a character that's hidden at the time.\n\nBoth functions must take two parameters: first the character that went in or out, then the object of the region. So the function can tell who it was, and one function can look after lots of regions at once. If two characters step in at the same time, the function runs twice, once for each of them. If a character steps straight from one watched region into another, the onLeave of the first runs before the onEnter of the second. A function with a different number of parameters stops the game with an error as soon as it's called.\n\nEither function can be NULL if you only care about one of the two. Call watchScreenRegion again for the same object to change its functions, and use NULL for both to stop watching it.\n\nThe watch belongs to the object rather than to one region, so it covers every region of thisObject and carries on after removeScreenRegion. Add the region again, in another room say, and the watch works again. Anything that changes whether a character is inside counts, not just walking: a watch that starts with a character already inside runs onEnter, and removing the region, or the character, while it's inside runs onLeave. Freezing the game (see freeze) puts the watches away with the rest of the scene, and unfreezing brings them back without running anything. Saved games keep them too.",
	"No return value.")
FUNCTION_DOC ("zoomCamera", "zoomCamera.html",
	"zoomCamera (zoom)",
	"Zooms the camera by zoom percentage. (100 is the default.) If it is not possible to zoom out the camera without the user seeing an area outside of the scene image, the camera is zoomed to the closest possible valid position. The camera position is not adjusted to compensate for the changed zoom value (except for avoiding scene edges): Use together with aimCamera.",
	"No return value.")

CONSTANT_DOC ("AUTOFIT", "Positioning constants", "The following constant has a special meaning if used as a parameter for parallaxAdd.")
CONSTANT_DOC ("BLACK", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("BLINDS", "Transition modes", "You'd never guess you used these in transitionMode, would you?")
CONSTANT_DOC ("CENTER", "Positioning constants", "Used in a few places... for example addOverlay.")
CONSTANT_DOC ("CENTRE", "Positioning constants", "Used in a few places... for example addOverlay.")
CONSTANT_DOC ("CROSSFADE", "Transition modes", "You'd never guess you used these in transitionMode, would you?")
CONSTANT_DOC ("DARK", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("DARK1", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("DARK2", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("DARK3", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("DARK4", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("DISOLVE", "Transition modes", "You'd never guess you used these in transitionMode, would you?")
CONSTANT_DOC ("DISOLVE1", "Transition modes", "You'd never guess you used these in transitionMode, would you?")
CONSTANT_DOC ("DISOLVE2", "Transition modes", "You'd never guess you used these in transitionMode, would you?")
CONSTANT_DOC ("EASE_INOUT", "Parts of characters", "For clipKey: the properties of a part, and how they move between keys.")
CONSTANT_DOC ("EASE_LINEAR", "Parts of characters", "For clipKey: the properties of a part, and how they move between keys.")
CONSTANT_DOC ("EASE_STEP", "Parts of characters", "For clipKey: the properties of a part, and how they move between keys.")
CONSTANT_DOC ("EAST", "Directions", "Directions in SLUDGE are measured in degrees. The following values therefore make for more readable code, for example when you're calling addCharacter or turnCharacter.")
CONSTANT_DOC ("FADE", "Transition modes", "You'd never guess you used these in transitionMode, would you?")
CONSTANT_DOC ("FALSE", "General constants", "These guys can be found all over the place.")
CONSTANT_DOC ("FIXEDSIZE", "Extra character settings", "Each character can have any combination of these settings... just add them together and throw them through the setCharacterExtra function.")
CONSTANT_DOC ("FIXTOSCREEN", "Extra character settings", "Each character can have any combination of these settings... just add them together and throw them through the setCharacterExtra function.")
CONSTANT_DOC ("FOGGY", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("FOGGY1", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("FOGGY2", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("FOGGY3", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("FOGGY4", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("FRONT", "Extra character settings", "Each character can have any combination of these settings... just add them together and throw them through the setCharacterExtra function.")
CONSTANT_DOC ("GLOW", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("GLOW1", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("GLOW2", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("GLOW3", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("GLOW4", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("GRAY", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("GREY", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("HOTSPOT", "Light map modes", "When you use setLightMap you can also change how lightmaps are to be applied. Use the following values.")
CONSTANT_DOC ("ICON", "Extra character settings", "The following constant can be used to turn on the first 5 of the above settings in one fell swoop...")
CONSTANT_DOC ("INVISIBLE", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("LAYER_ADD", "Layers of clips", "For playClip: whether a layer sets what its clip keys, or adds to it.")
CONSTANT_DOC ("LAYER_OVERRIDE", "Layers of clips", "For playClip: whether a layer sets what its clip keys, or adds to it.")
CONSTANT_DOC ("LEFT", "Positioning constants", "The following values should only be used by alignStatus.")
CONSTANT_DOC ("NOLIGHTMAP", "Extra character settings", "Each character can have any combination of these settings... just add them together and throw them through the setCharacterExtra function.")
CONSTANT_DOC ("NOREMOVE", "Extra character settings", "Each character can have any combination of these settings... just add them together and throw them through the setCharacterExtra function.")
CONSTANT_DOC ("NORMAL", "General constants", "These guys can be found all over the place.")
CONSTANT_DOC ("NORTH", "Directions", "Directions in SLUDGE are measured in degrees. The following values therefore make for more readable code, for example when you're calling addCharacter or turnCharacter.")
CONSTANT_DOC ("NORTHEAST", "Directions", "Directions in SLUDGE are measured in degrees. The following values therefore make for more readable code, for example when you're calling addCharacter or turnCharacter.")
CONSTANT_DOC ("NORTHWEST", "Directions", "Directions in SLUDGE are measured in degrees. The following values therefore make for more readable code, for example when you're calling addCharacter or turnCharacter.")
CONSTANT_DOC ("NOSPRITE", "Clip sprites", "For clipSprite, in place of a sprite bank and sprite.")
CONSTANT_DOC ("NOZBUFFER", "Extra character settings", "Each character can have any combination of these settings... just add them together and throw them through the setCharacterExtra function.")
CONSTANT_DOC ("NULL", "General constants", "These guys can be found all over the place.")
CONSTANT_DOC ("PART_DEFAULT", "Clip sprites", "For clipSprite, in place of a sprite bank and sprite.")
CONSTANT_DOC ("PART_DEPTH", "Parts of characters", "For clipKey: the properties of a part, and how they move between keys.")
CONSTANT_DOC ("PART_FLIP", "Parts of characters", "For clipKey: the properties of a part, and how they move between keys.")
CONSTANT_DOC ("PART_MODULATE_A", "Parts of characters", "For clipKey: the properties of a part, and how they move between keys.")
CONSTANT_DOC ("PART_MODULATE_B", "Parts of characters", "For clipKey: the properties of a part, and how they move between keys.")
CONSTANT_DOC ("PART_MODULATE_G", "Parts of characters", "For clipKey: the properties of a part, and how they move between keys.")
CONSTANT_DOC ("PART_MODULATE_R", "Parts of characters", "For clipKey: the properties of a part, and how they move between keys.")
CONSTANT_DOC ("PART_ROTATION", "Parts of characters", "For clipKey: the properties of a part, and how they move between keys.")
CONSTANT_DOC ("PART_SCALE", "Parts of characters", "For clipKey: the properties of a part, and how they move between keys.")
CONSTANT_DOC ("PART_SCALE_X", "Parts of characters", "For clipKey: the properties of a part, and how they move between keys.")
CONSTANT_DOC ("PART_SCALE_Y", "Parts of characters", "For clipKey: the properties of a part, and how they move between keys.")
CONSTANT_DOC ("PART_SELF_MODULATE_A", "Parts of characters", "For clipKey: the properties of a part, and how they move between keys.")
CONSTANT_DOC ("PART_SELF_MODULATE_B", "Parts of characters", "For clipKey: the properties of a part, and how they move between keys.")
CONSTANT_DOC ("PART_SELF_MODULATE_G", "Parts of characters", "For clipKey: the properties of a part, and how they move between keys.")
CONSTANT_DOC ("PART_SELF_MODULATE_R", "Parts of characters", "For clipKey: the properties of a part, and how they move between keys.")
CONSTANT_DOC ("PART_VISIBLE", "Parts of characters", "For clipKey: the properties of a part, and how they move between keys.")
CONSTANT_DOC ("PART_X", "Parts of characters", "For clipKey: the properties of a part, and how they move between keys.")
CONSTANT_DOC ("PART_Y", "Parts of characters", "For clipKey: the properties of a part, and how they move between keys.")
CONSTANT_DOC ("PERPIXEL", "Light map modes", "When you use setLightMap you can also change how lightmaps are to be applied. Use the following values.")
CONSTANT_DOC ("RECTANGULAR", "Extra character settings", "Each character can have any combination of these settings... just add them together and throw them through the setCharacterExtra function.")
CONSTANT_DOC ("RIGHT", "Positioning constants", "The following values should only be used by alignStatus.")
CONSTANT_DOC ("SHADOW", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("SHADOW1", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("SHADOW2", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("SHADOW3", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("SNAPSHOTBOX", "Transition modes", "You'd never guess you used these in transitionMode, would you?")
CONSTANT_DOC ("SNAPSHOTFADE", "Transition modes", "You'd never guess you used these in transitionMode, would you?")
CONSTANT_DOC ("SOUNDANDTEXT", "Speech modes", "Use these and setSpeechMode to turn your game into a full talkie version...")
CONSTANT_DOC ("SOUNDONLY", "Speech modes", "Use these and setSpeechMode to turn your game into a full talkie version...")
CONSTANT_DOC ("SOUTH", "Directions", "Directions in SLUDGE are measured in degrees. The following values therefore make for more readable code, for example when you're calling addCharacter or turnCharacter.")
CONSTANT_DOC ("SOUTHEAST", "Directions", "Directions in SLUDGE are measured in degrees. The following values therefore make for more readable code, for example when you're calling addCharacter or turnCharacter.")
CONSTANT_DOC ("SOUTHWEST", "Directions", "Directions in SLUDGE are measured in degrees. The following values therefore make for more readable code, for example when you're calling addCharacter or turnCharacter.")
CONSTANT_DOC ("TEXTONLY", "Speech modes", "Use these and setSpeechMode to turn your game into a full talkie version...")
CONSTANT_DOC ("TRANSPARENT", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("TRANSPARENT1", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("TRANSPARENT2", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("TRANSPARENT3", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
CONSTANT_DOC ("TRUE", "General constants", "These guys can be found all over the place.")
CONSTANT_DOC ("TVSTATIC", "Transition modes", "You'd never guess you used these in transitionMode, would you?")
CONSTANT_DOC ("WEST", "Directions", "Directions in SLUDGE are measured in degrees. The following values therefore make for more readable code, for example when you're calling addCharacter or turnCharacter.")
CONSTANT_DOC ("WHITE", "Character draw modes", "Used by setCharacterDrawMode, curiously.")
