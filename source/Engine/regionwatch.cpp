#include <algorithm>

#include "allfiles.h"
#include "objtypes.h"
#include "region.h"
#include "people.h"
#include "variable.h"
#include "sludger.h"
#include "moreio.h"
#include "newfatal.h"
#include "regionwatch.h"

regionWatch * allRegionWatches = NULL;
extern screenRegion * allScreenRegions;
extern onScreenPerson * allPeople;

void setRegionWatch (int objectNum, int enterFunction, int leaveFunction) {
	regionWatch * * w = & allRegionWatches;
	while (* w && (* w) -> objectNum != objectNum) w = & (* w) -> next;
	if (! enterFunction && ! leaveFunction) {
		if (* w) {
			regionWatch * killMe = * w;
			* w = killMe -> next;
			delete killMe;
		}
		return;
	}
	if (! * w) {
		// Added at the end, so that events fire in the order of the watches.
		* w = new regionWatch;
		(* w) -> objectNum = objectNum;
		(* w) -> next = NULL;
	}
	(* w) -> enterFunction = enterFunction;
	(* w) -> leaveFunction = leaveFunction;
}

bool isInRegionOf (const onScreenPerson * person, int objectNum) {
	for (screenRegion * r = allScreenRegions; r; r = r -> next) {
		if (r -> thisType -> objectNum == objectNum && pointInRegion (r, (int) person -> x, (int) person -> y)) return true;
	}
	return false;
}

namespace {
struct watchEvent {
	int function, character, region;
};
}

// Runs function (character, region).
static bool fireWatchEvent (const watchEvent & e) {
	// The parameters come off the stack last first.
	variableStack * region = new variableStack;
	if (! checkNew (region)) return false;
	initVarNew (region -> thisVar);
	setVariable (region -> thisVar, SVT_OBJTYPE, e.region);
	variableStack * character = new variableStack;
	if (! checkNew (character)) return false;
	initVarNew (character -> thisVar);
	setVariable (character -> thisVar, SVT_OBJTYPE, e.character);
	character -> next = NULL;
	region -> next = character;
	variableStack * stack = region;
	bool ok = startNewFunctionNum (e.function, 2, NULL, stack);
	while (stack) trimStack (stack);
	return ok;
}

bool checkRegionWatches () {
	std::vector<watchEvent> leaving, entering;
	for (regionWatch * w = allRegionWatches; w; w = w -> next) {
		std::vector<int> now;
		for (onScreenPerson * p = allPeople; p; p = p -> next) {
			if (p -> thisType && isInRegionOf (p, w -> objectNum)) now.push_back (p -> thisType -> objectNum);
		}
		for (size_t i = 0; i < w -> inside.size (); i ++) {
			if (std::find (now.begin (), now.end (), w -> inside[i]) == now.end () && w -> leaveFunction) {
				watchEvent e = { w -> leaveFunction, w -> inside[i], w -> objectNum };
				leaving.push_back (e);
			}
		}
		for (size_t i = 0; i < now.size (); i ++) {
			if (std::find (w -> inside.begin (), w -> inside.end (), now[i]) == w -> inside.end () && w -> enterFunction) {
				watchEvent e = { w -> enterFunction, now[i], w -> objectNum };
				entering.push_back (e);
			}
		}
		w -> inside.swap (now);
	}
	// A function that starts goes to the front of the running ones, so the
	// last we start runs first: we start them backwards, the enters before
	// the leaves, so that they run in order with the leaves first.
	for (size_t i = entering.size (); i -- > 0; ) {
		if (! fireWatchEvent (entering[i])) return false;
	}
	for (size_t i = leaving.size (); i -- > 0; ) {
		if (! fireWatchEvent (leaving[i])) return false;
	}
	return true;
}

void killAllRegionWatches () {
	while (allRegionWatches) {
		regionWatch * killMe = allRegionWatches;
		allRegionWatches = killMe -> next;
		delete killMe;
	}
}

void saveRegionWatches (FILE * fp) {
	int count = 0;
	for (regionWatch * w = allRegionWatches; w; w = w -> next) count ++;
	put2bytes (count, fp);
	for (regionWatch * w = allRegionWatches; w; w = w -> next) {
		put2bytes (w -> objectNum, fp);
		put2bytes (w -> enterFunction, fp);
		put2bytes (w -> leaveFunction, fp);
		put2bytes ((int) w -> inside.size (), fp);
		for (size_t i = 0; i < w -> inside.size (); i ++) put2bytes (w -> inside[i], fp);
	}
}

void loadRegionWatches (FILE * fp) {
	killAllRegionWatches ();
	regionWatch * * pointy = & allRegionWatches;
	int count = get2bytes (fp);
	// A cut-off save gives -257, at the end of the file.
	while (count -- > 0 && ! feof (fp)) {
		regionWatch * w = new regionWatch;
		w -> objectNum = get2bytes (fp);
		w -> enterFunction = get2bytes (fp);
		w -> leaveFunction = get2bytes (fp);
		int inside = get2bytes (fp);
		for (int i = 0; i < inside && ! feof (fp); i ++) w -> inside.push_back (get2bytes (fp));
		w -> next = NULL;
		* pointy = w;
		pointy = & w -> next;
	}
}
