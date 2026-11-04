/*
 *  FloorMaker.h
 *  Sludge Dev Kit
 *
 *  Created by Rikard Peterson on 2009-07-27.
 *  Copyright 2009 __MyCompanyName__. All rights reserved.
 *
 */

class Draw2D;

struct vertexList {
	int x, y;
	struct vertexList * next;
};

struct polyList {
	struct vertexList * firstVertex;
	struct polyList * next;
};

bool loadFloorFromFile (const char * name, struct polyList **firstPoly);
// Save a floor, after asking to fix it when it needs fixing.
bool saveFloorToFile (char * filename, struct polyList **firstPoly);

// Whether a complete polygon of the floor isn't convex, which most likely
// causes problems in the game.
bool floorNeedsFixing (struct polyList * firstPoly);
// Split the polygons that aren't convex into convex ones, where we can.
// Returns false when a polygon is too complicated.
bool fixFloor (struct polyList **firstPoly);
// Save a floor as it is.
bool writeFloorToFile (const char * filename, struct polyList * firstPoly);

bool polyIsConvex (struct polyList * poly);
bool polyIsComplete (struct polyList * poly);

void noFloor (struct polyList **firstPoly);
int addVertex (int x, int y, struct polyList *firstPoly);
bool moveVertices (int x1, int y1, int x2, int y2, struct polyList *firstPoly);
void killVertex (int x, int y, struct polyList **firstPoly);

struct polyList * addPoly (struct polyList *firstPoly);

void splitPoly (int x1, int y1, int x2, int y2, struct polyList **firstPoly);
void splitLine (int x1, int y1, int x2, int y2, struct polyList *firstPoly);

// Draw a floor in the colour r, g, b, with a dot on each vertex that is
// pointPixels pixels of the picture for each point of the window across.
void drawFloor (Draw2D & draw, struct polyList * pL, float r, float g, float b, float pointPixels);

struct spriteBank;

// In the floor maker view, we draw the backdrop, the floor in the colour
// r, g, b, a larger circle on the vertex at (litX, litY) when lit is set,
// and the line from (selx1, sely1) to (selx2, sely2) in red when selecting
// is set. The selected vertices, selectedCount x, y pairs, get large red
// circles, and the box from (boxX1, boxY1) to (boxX2, boxY2) is drawn in red
// when boxing is set. The dots and circles keep their size in the window: a
// point of it is pointPixels pixels of the picture.
struct floorMakerView {
	struct spriteBank * backdrop;
	struct polyList * floor;
	float r, g, b;
	bool lit;
	int litX, litY;
	bool selecting;
	int selx1, sely1, selx2, sely2;
	float pointPixels;
	const int * selectedXY;
	int selectedCount;
	bool boxing;
	int boxX1, boxY1, boxX2, boxY2;
};

void drawFloorMakerView (Draw2D & draw, const struct floorMakerView * view);

bool snapToClosest (int *x, int *y, struct polyList * firstPoly);
