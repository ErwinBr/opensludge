#include "allfiles.h"
#include "objtypes.h"
#include "region.h"
#include "newfatal.h"
#include "sludger.h"
#include "moreio.h"
#include "backdrop.h"
#include "line.h"
#include "regionshape.h"
#include "version.h"

screenRegion * allScreenRegions = NULL;
screenRegion * overRegion = NULL;
extern inputType input;
extern int cameraX, cameraY;
extern int ssgVersion;

void showBoxes () {
	screenRegion * huntRegion = allScreenRegions;

	while (huntRegion) {
		const std::vector<int> & p = huntRegion -> points;
		if (! p.empty ()) {
			int n = (int) p.size () / 2;
			for (int i = 0, j = n - 1; i < n; j = i ++) {
				drawLine (p[j * 2], p[j * 2 + 1], p[i * 2], p[i * 2 + 1]);
			}
			huntRegion = huntRegion -> next;
			continue;
		}
		drawVerticalLine (huntRegion -> x1, huntRegion -> y1, huntRegion -> y2);
		drawVerticalLine (huntRegion -> x2, huntRegion -> y1, huntRegion -> y2);
		drawHorizontalLine (huntRegion -> x1, huntRegion -> y1, huntRegion -> x2);
		drawHorizontalLine (huntRegion -> x1, huntRegion -> y2, huntRegion -> x2);
		huntRegion = huntRegion -> next;
	}
}

void removeScreenRegion (int objectNum) {
	screenRegion * * huntRegion = & allScreenRegions;
	screenRegion * killMe;

	while (* huntRegion) {
		if ((* huntRegion) -> thisType -> objectNum == objectNum) {
			killMe = * huntRegion;
			* huntRegion = killMe -> next;
			removeObjectType (killMe -> thisType);
			if (killMe == overRegion) overRegion = NULL;
			delete killMe;
			killMe = NULL;
		} else {
			huntRegion = & ((* huntRegion) -> next);
		}
	}
}

void saveRegions (FILE * fp) {
	int numRegions = 0;
	screenRegion * thisRegion = allScreenRegions;
	while (thisRegion) {
		thisRegion = thisRegion -> next;
		numRegions ++;
	}
	put2bytes (numRegions, fp);
	thisRegion = allScreenRegions;
	while (thisRegion) {
		put2bytes (thisRegion -> x1, fp);
		put2bytes (thisRegion -> y1, fp);
		put2bytes (thisRegion -> x2, fp);
		put2bytes (thisRegion -> y2, fp);
		put2bytes (thisRegion -> sX, fp);
		put2bytes (thisRegion -> sY, fp);
		put2bytes (thisRegion -> di, fp);
		saveObjectRef (thisRegion -> thisType, fp);
		// Since saves of version 2.4: the points of a shaped region.
		put2bytes ((int) thisRegion -> points.size () / 2, fp);
		for (size_t i = 0; i < thisRegion -> points.size (); i ++) {
			put2bytes (thisRegion -> points[i], fp);
		}

		thisRegion = thisRegion -> next;
	}
}

void loadRegions (FILE * fp) {
	int numRegions = get2bytes (fp);

	screenRegion * newRegion;
	screenRegion * * pointy = & allScreenRegions;

	// A cut-off save gives -257, at the end of the file.
	while (numRegions -- > 0 && ! feof (fp)) {
		newRegion = new screenRegion;
		* pointy = newRegion;
		pointy = & (newRegion -> next);

		newRegion -> x1 = get2bytes (fp);
		newRegion -> y1 = get2bytes (fp);
		newRegion -> x2 = get2bytes (fp);
		newRegion -> y2 = get2bytes (fp);
		newRegion -> sX = get2bytes (fp);
		newRegion -> sY = get2bytes (fp);
		newRegion -> di = get2bytes (fp);
		newRegion -> thisType = loadObjectRef (fp);
		if (ssgVersion >= VERSION (2, 4)) {
			int numPoints = get2bytes (fp);
			for (int i = 0; i < numPoints * 2 && ! feof (fp); i ++) {
				newRegion -> points.push_back (get2bytes (fp));
			}
		}
	}
	* pointy = NULL;
}

void killAllRegions () {
	screenRegion * killRegion;
	while (allScreenRegions) {
		killRegion = allScreenRegions;
		allScreenRegions = allScreenRegions -> next;
		removeObjectType (killRegion -> thisType);
		delete killRegion;
	}
	overRegion = NULL;
}

bool addScreenRegion (int x1, int y1, int x2, int y2, int sX, int sY, int di, int objectNum) {
	screenRegion * newRegion = new screenRegion;
	if (! checkNew (newRegion)) return false;
	newRegion -> di = di;
	newRegion -> x1 = x1;
	newRegion -> y1 = y1;
	newRegion -> x2 = x2;
	newRegion -> y2 = y2;
	newRegion -> sX = sX;
	newRegion -> sY = sY;
	newRegion -> thisType = loadObjectType (objectNum);
	newRegion -> next = allScreenRegions;
	allScreenRegions = newRegion;
	return (bool) (newRegion -> thisType != NULL);
}

// A shaped region with the box around its points, which is what a character
// talks above and what the code that only knows rectangles sees.
bool addScreenRegion (const std::vector<int> & points, int sX, int sY, int di, int objectNum) {
	int x1 = points[0], y1 = points[1], x2 = x1, y2 = y1;
	for (size_t i = 2; i + 1 < points.size (); i += 2) {
		x1 = std::min (x1, points[i]);
		x2 = std::max (x2, points[i]);
		y1 = std::min (y1, points[i + 1]);
		y2 = std::max (y2, points[i + 1]);
	}
	if (! addScreenRegion (x1, y1, x2, y2, sX, sY, di, objectNum)) return false;
	allScreenRegions -> points = points;
	return true;
}

// Whether the point (x, y) of the scene is in the region. A rectangle
// includes its edges; a shape is tested inside its box first.
bool pointInRegion (const screenRegion * r, int x, int y) {
	if (x < r -> x1 || y < r -> y1 || x > r -> x2 || y > r -> y2) return false;
	return r -> points.empty () || pointInShape (r -> points, (float) x, (float) y);
}

void getOverRegion () {
	screenRegion * thisRegion = allScreenRegions;
	while (thisRegion) {
		if (pointInRegion (thisRegion, input.mouseX + cameraX, input.mouseY + cameraY)) {
			overRegion = thisRegion;
			return;
		}
		thisRegion = thisRegion -> next;
	}
	overRegion = NULL;
	return;
}

screenRegion * getRegionForObject (int obj) {
	screenRegion * thisRegion = allScreenRegions;

	while (thisRegion) {
		if (obj == thisRegion -> thisType -> objectNum) {
			return thisRegion;
		}
		thisRegion = thisRegion -> next;
	}

	return NULL;
}
