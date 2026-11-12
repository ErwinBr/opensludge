#include <vector>

struct screenRegion {
	int x1, y1, x2, y2, sX, sY, di;
	objectType * thisType;
	screenRegion * next;
	// The x, y pairs of a shaped region, or empty for a rectangle. Then
	// x1, y1, x2, y2 are the box around the shape.
	std::vector<int> points;
};

bool addScreenRegion (int x1, int y1, int x2, int y2, int, int, int, int objectNum);
bool addScreenRegion (const std::vector<int> & points, int sX, int sY, int di, int objectNum);
bool pointInRegion (const screenRegion * region, int x, int y);
void getOverRegion ();
screenRegion * getRegionForObject (int obj);
void removeScreenRegion (int objectNum);
void killAllRegions ();
void loadRegions (FILE *);
void saveRegions (FILE *);
void showBoxes ();
