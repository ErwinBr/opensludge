#ifndef REGIONSHAPE_H
#define REGIONSHAPE_H

// The shapes of screen regions made from a list of points (addScreenRegion
// with an array): a closed path of x, y pairs, whose last point joins the
// first. These have no state, so that gamepad.cpp and its test can use them
// without the rest of region.cpp.

#include <algorithm>
#include <vector>

// Whether (x, y) is inside the path, by the even-odd rule: a line from the
// point to the right crosses the edges an odd number of times. This is the
// test of pointInFloorPolygon (floor.cpp), on a plain list of points.
inline bool pointInShape (const std::vector<int> & points, float x, float y) {
	int n = (int) points.size () / 2;
	bool inside = false;
	for (int i = 0, j = n - 1; i < n; j = i ++) {
		float xi = (float) points[i * 2], yi = (float) points[i * 2 + 1];
		float xj = (float) points[j * 2], yj = (float) points[j * 2 + 1];
		if (((yi <= y && y < yj) || (yj <= y && y < yi)) &&
			x < (xj - xi) * (y - yi) / (yj - yi) + xi) {
			inside = ! inside;
		}
	}
	return inside;
}

// A point inside the shape for the controller to move the pointer to: the
// middle of the box around it if that is inside, as for a rectangle, and
// else the middle of the first stretch inside the shape on the line across
// the middle, so that an L or a U still gets a point that finds it.
inline void shapeHotspot (const std::vector<int> & points, int x1, int y1, int x2, int y2, float & x, float & y) {
	x = (x1 + x2) / 2.0f;
	y = (y1 + y2) / 2.0f;
	if (points.empty () || pointInShape (points, x, y)) return;
	std::vector<float> crossings;
	int n = (int) points.size () / 2;
	for (int i = 0, j = n - 1; i < n; j = i ++) {
		float xi = (float) points[i * 2], yi = (float) points[i * 2 + 1];
		float xj = (float) points[j * 2], yj = (float) points[j * 2 + 1];
		if ((yi <= y && y < yj) || (yj <= y && y < yi)) {
			crossings.push_back ((xj - xi) * (y - yi) / (yj - yi) + xi);
		}
	}
	if (crossings.size () >= 2) {
		std::sort (crossings.begin (), crossings.end ());
		x = (crossings[0] + crossings[1]) / 2.0f;
	} else {
		x = (float) points[0];
		y = (float) points[1];
	}
}

#endif
