#include <stdlib.h>

#include "FloorDocument.h"
#include "floormaker.h"

namespace devkit {

Floor::Floor () : polygons (1) {}

polyList * Floor::toPolyList () const {
	polyList * first = NULL;
	polyList ** next = &first;
	for (size_t p = 0; p < polygons.size (); p ++) {
		polyList * poly = new polyList;
		poly->firstVertex = NULL;
		poly->next = NULL;
		vertexList ** vertex = &poly->firstVertex;
		for (size_t v = 0; v < polygons[p].size (); v ++) {
			vertexList * made = new vertexList;
			made->x = polygons[p][v].x;
			made->y = polygons[p][v].y;
			made->next = NULL;
			*vertex = made;
			vertex = &made->next;
		}
		*next = poly;
		next = &poly->next;
	}
	return first;
}

Floor Floor::fromPolyList (const polyList * list) {
	Floor floor;
	floor.polygons.clear ();
	for (const polyList * poly = list; poly; poly = poly->next) {
		FloorPolygon polygon;
		for (const vertexList * vertex = poly->firstVertex; vertex; vertex = vertex->next) {
			polygon.push_back (FloorPoint (vertex->x, vertex->y));
		}
		floor.polygons.push_back (polygon);
	}
	// The tools draw in the first polygon, so there is always one.
	if (floor.polygons.empty ()) floor.polygons.push_back (FloorPolygon ());
	return floor;
}

void Floor::freePolyList (polyList * list) {
	while (list) {
		while (list->firstVertex) {
			vertexList * vertex = list->firstVertex;
			list->firstVertex = vertex->next;
			delete vertex;
		}
		polyList * poly = list;
		list = list->next;
		delete poly;
	}
}

int Floor::vertexCount () const {
	int count = 0;
	for (size_t p = 0; p < polygons.size (); p ++) {
		// The last vertex of a complete polygon is its first again.
		const FloorPolygon & polygon = polygons[p];
		count += (int) polygon.size ();
		if (polygon.size () > 1 && polygon.front () == polygon.back ()) count --;
	}
	return count;
}

int Floor::concaveCount () const {
	polyList * list = toPolyList ();
	int count = 0;
	for (polyList * poly = list; poly; poly = poly->next) {
		if (polyIsComplete (poly) && ! polyIsConvex (poly)) count ++;
	}
	freePolyList (list);
	return count;
}

FloorPoint Floor::extent () const {
	FloorPoint most;
	for (size_t p = 0; p < polygons.size (); p ++) {
		for (size_t v = 0; v < polygons[p].size (); v ++) {
			if (polygons[p][v].x > most.x) most.x = polygons[p][v].x;
			if (polygons[p][v].y > most.y) most.y = polygons[p][v].y;
		}
	}
	return most;
}

DocumentType FloorDocument::type (Platform & platform) {
	DocumentType type;
	type.name = "Floor";
	type.extensions.push_back ("flo");
	Platform * p = &platform;
	type.create = [p] () -> Document * { return new FloorDocument (p); };
	return type;
}

FloorDocument::FloorDocument (Platform * p) : platform (p) {}

bool FloorDocument::snap (int * x, int * y, int closeness) const {
	int sx = *x < 4 ? 0 : *x;
	int sy = *y < 4 ? 0 : *y;
	for (size_t p = 0; p < theFloor.polygons.size (); p ++) {
		const FloorPolygon & polygon = theFloor.polygons[p];
		for (size_t v = 0; v < polygon.size (); v ++) {
			if (abs (polygon[v].x - sx) < closeness && abs (polygon[v].y - sy) < closeness) {
				*x = polygon[v].x;
				*y = polygon[v].y;
				return true;
			}
		}
	}
	return false;
}

bool FloorDocument::drawing () const {
	const FloorPolygon & polygon = theFloor.polygons[0];
	return ! polygon.empty () && (polygon.size () == 1 || polygon.front () != polygon.back ());
}

void FloorDocument::setFloor (const Floor & floor, const std::string & label) {
	commands ().run (new ValueCommand<Floor> (label, theFloor, floor,
		[this] (const Floor & f) { theFloor = f; }));
}

bool FloorDocument::change (const std::string & label, std::function<void (polyList ** list)> edit) {
	polyList * list = theFloor.toPolyList ();
	edit (&list);
	Floor changed = Floor::fromPolyList (list);
	Floor::freePolyList (list);
	if (changed == theFloor) return false;
	setFloor (changed, label);
	return true;
}

FloorDocument::AddResult FloorDocument::addVertex (int x, int y) {
	AddResult result = ADDED;
	change ("Add Vertex", [x, y, &result] (polyList ** list) {
		int added = ::addVertex (x, y, *list);
		// The polygon is complete, so we start a new one, as the GTK Floor
		// Maker did when the user said yes.
		if (added == 2) {
			*list = addPoly (*list);
			added = ::addVertex (x, y, *list);
		}
		if (added == 3) result = ALREADY_USED;
	});
	return result;
}

bool FloorDocument::removeLastVertex () {
	if (! drawing ()) return false;
	Floor changed = theFloor;
	changed.polygons[0].erase (changed.polygons[0].begin ());
	setFloor (changed, "Remove Vertex");
	return true;
}

bool FloorDocument::cancelDrawing () {
	if (! drawing ()) return false;
	Floor changed = theFloor;
	changed.polygons[0].clear ();
	setFloor (changed, "Cancel Polygon");
	return true;
}

bool FloorDocument::moveVertex (int x1, int y1, int x2, int y2) {
	return change ("Move Vertex", [x1, y1, x2, y2] (polyList ** list) {
		moveVertices (x1, y1, x2, y2, *list);
	});
}

Floor FloorDocument::movedFloor (int x1, int y1, int x2, int y2) const {
	polyList * list = theFloor.toPolyList ();
	moveVertices (x1, y1, x2, y2, list);
	Floor moved = Floor::fromPolyList (list);
	Floor::freePolyList (list);
	return moved;
}

bool FloorDocument::deleteVertex (int x, int y) {
	return change ("Delete Vertex", [x, y] (polyList ** list) {
		killVertex (x, y, list);
	});
}

bool FloorDocument::splitLine (int x1, int y1, int x2, int y2) {
	return change ("Split Line", [x1, y1, x2, y2] (polyList ** list) {
		::splitLine (x1, y1, x2, y2, *list);
	});
}

bool FloorDocument::splitFloor (int x1, int y1, int x2, int y2) {
	return change ("Split Floor", [x1, y1, x2, y2] (polyList ** list) {
		splitPoly (x1, y1, x2, y2, list);
	});
}

bool FloorDocument::fixConcave () {
	bool fixed = true;
	change ("Fix Floor", [&fixed] (polyList ** list) {
		fixed = fixFloor (list);
	});
	return fixed;
}

bool FloorDocument::needsFixing () const {
	polyList * list = theFloor.toPolyList ();
	bool needs = floorNeedsFixing (list);
	Floor::freePolyList (list);
	return needs;
}

bool FloorDocument::readFloor (const std::string & path, Floor * floor, std::string * error) {
	polyList * list = NULL;
	if (! loadFloorFromFile (path.c_str (), &list)) {
		*error = "The file can't be opened.";
		return false;
	}
	*floor = Floor::fromPolyList (list);
	Floor::freePolyList (list);
	return true;
}

bool FloorDocument::writeFloor (const std::string & path, const Floor & floor, std::string * error) {
	polyList * list = floor.toPolyList ();
	bool written = writeFloorToFile (path.c_str (), list);
	Floor::freePolyList (list);
	if (! written) *error = "The file can't be written.";
	return written;
}

bool FloorDocument::read (const std::string & path, std::string * error) {
	return readFloor (path, &theFloor, error);
}

bool FloorDocument::write (const std::string & path, std::string * error) {
	// As the GTK Floor Maker, we offer to fix a floor that the game can't
	// walk on well. The fix is a command, which undoes.
	if (platform && needsFixing ()) {
		std::vector<std::string> buttons;
		buttons.push_back ("Fix and Save");
		buttons.push_back ("Save As It Is");
		if (platform->ask ("Fix the floor?", "A polygon of the floor isn't convex, which will most likely cause "
				"problems in the game. The Dev Kit can split it into convex polygons.", buttons) == 0
				&& ! fixConcave ()) {
			platform->showMessage (Platform::MESSAGE_WARNING, "Can't fix the floor",
				"A polygon is too complicated to fix. We save the floor as it is.");
		}
	}
	return writeFloor (path, theFloor, error);
}

} // namespace devkit
