#ifndef REGIONWATCH_H
#define REGIONWATCH_H

#include <stdio.h>
#include <vector>

// Watches on screen regions (watchScreenRegion): functions that run when a
// character's hotspot goes into a region of an object, or comes out of it.
// A watch belongs to the object, not to one region, so it covers all the
// regions of the object and outlives them: when a room adds the region
// again, the watch works again.
struct regionWatch {
	int objectNum;							// Of the region
	int enterFunction, leaveFunction;		// 0 for none
	// The characters (object numbers) inside at the last check, which is how
	// we know who went in or out.
	std::vector<int> inside;
	regionWatch * next;
};

struct onScreenPerson;

// Starts, changes or (with two 0s) ends the watch of a region object.
void setRegionWatch (int objectNum, int enterFunction, int leaveFunction);
// Whether the hotspot of the character is in any region of the object.
bool isInRegionOf (const onScreenPerson * person, int objectNum);
// Once a frame: fires the events of characters that went in or out since
// the last check, all leaving before all entering.
bool checkRegionWatches ();
void killAllRegionWatches ();
void saveRegionWatches (FILE * fp);
void loadRegionWatches (FILE * fp);

#endif
