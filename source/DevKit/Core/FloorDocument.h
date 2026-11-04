#ifndef SLUDGE_DEVKIT_FLOOR_DOCUMENT_H
#define SLUDGE_DEVKIT_FLOOR_DOCUMENT_H

/*
 * A floor (.flo): the polygons that the characters of a room walk on. The
 * editing is that of the GTK Floor Maker (GraphicalTools/floormaker.cpp),
 * on a copy of the floor that we can keep: every change is a command that
 * holds the floor before and after it, so that it undoes.
 *
 * The polygons and their vertices are in the order of the linked lists of
 * floormaker.cpp: the first polygon is the one that we draw in, and the
 * first vertex of a polygon the one that we drew last. A polygon is
 * complete when its last vertex is its first again.
 */

#include <string>
#include <vector>

#include "Document.h"

struct polyList;

namespace devkit {

struct FloorPoint {
	int x, y;
	FloorPoint () : x (0), y (0) {}
	FloorPoint (int x, int y) : x (x), y (y) {}
	bool operator == (const FloorPoint & o) const { return x == o.x && y == o.y; }
	bool operator != (const FloorPoint & o) const { return ! (*this == o); }
};

typedef std::vector<FloorPoint> FloorPolygon;

struct Floor {
	std::vector<FloorPolygon> polygons;

	// An empty floor, with one polygon without vertices to draw in.
	Floor ();
	bool operator == (const Floor & o) const { return polygons == o.polygons; }
	bool operator != (const Floor & o) const { return ! (*this == o); }

	// The linked lists of floormaker.cpp. Free them with freePolyList.
	polyList * toPolyList () const;
	static Floor fromPolyList (const polyList * list);
	static void freePolyList (polyList * list);

	int vertexCount () const;
	// The polygons that aren't convex, which the game can't walk on well.
	int concaveCount () const;
	// The largest x and y of the vertices.
	FloorPoint extent () const;
};

class FloorDocument : public Document {
public:
	// Saving asks through the platform whether to fix a floor that needs it.
	static DocumentType type (Platform & platform);
	explicit FloorDocument (Platform * platform = NULL);

	const char * kind () const { return "Floor"; }
	std::string untitledName () const { return "Untitled Floor.flo"; }

	const Floor & floor () const { return theFloor; }

	// Move (x, y) to the vertex within closeness of it, if there is one,
	// and return whether there was. As in the GTK Floor Maker, a point
	// under 4 from the top or left edge looks from the edge.
	bool snap (int * x, int * y, int closeness) const;
	// Whether the polygon that we draw in has vertices but isn't complete.
	bool drawing () const;

	// The tools. Each returns whether it changed the floor, as one command.
	enum AddResult { ADDED, ALREADY_USED };
	// Add a vertex to the polygon that we draw in, or start a new polygon
	// when that one is complete. A vertex that the polygon has already, but
	// isn't its start, isn't allowed.
	AddResult addVertex (int x, int y);
	// Move a vertex, in every polygon that has it. A polygon that has both
	// places can't take it.
	bool moveVertex (int x1, int y1, int x2, int y2);
	bool deleteVertex (int x, int y);
	// While we draw a polygon: take away its last vertex, so that the one
	// before it is the last again, or take away the whole polygon.
	bool removeLastVertex ();
	bool cancelDrawing ();
	// Add a vertex in the middle of the edge between two vertices.
	bool splitLine (int x1, int y1, int x2, int y2);
	// Split the polygon that has both vertices in two, along the line
	// between them.
	bool splitFloor (int x1, int y1, int x2, int y2);
	// Split the polygons that aren't convex, where we can. Returns false
	// when a polygon is too complicated; what we could fix stays fixed.
	bool fixConcave ();
	// Whether saving would offer to fix the floor, as fixConcave does.
	bool needsFixing () const;

	// What the floor is after a move, to show it while the user drags.
	Floor movedFloor (int x1, int y1, int x2, int y2) const;

	// Change the floor as a command, as the tools do.
	void setFloor (const Floor & floor, const std::string & label);

	static bool readFloor (const std::string & path, Floor * floor, std::string * error);
	static bool writeFloor (const std::string & path, const Floor & floor, std::string * error);

protected:
	bool read (const std::string & path, std::string * error);
	bool write (const std::string & path, std::string * error);

private:
	// Change the floor with a function on its linked lists, as a command
	// when the floor changed.
	bool change (const std::string & label, std::function<void (polyList ** list)> edit);

	Platform * platform;
	Floor theFloor;
};

} // namespace devkit

#endif
