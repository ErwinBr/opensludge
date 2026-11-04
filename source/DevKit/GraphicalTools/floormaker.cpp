/*
 *  FloorMaker.cpp
 *  Sludge Dev Kit
 *
 *  Created by Rikard Peterson on 2009-07-27.
 *  Copyright 2009 __MyCompanyName__. All rights reserved.
 *
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include "draw2d.h"
#include "floormaker.h"
#include "sprites.h"
#include "interface.h"

bool polyIsComplete (struct polyList *firstPoly) {
	if (firstPoly -> firstVertex == NULL) return false;
	if (firstPoly -> firstVertex -> next == NULL) return false;
	vertexList * newVertex = firstPoly -> firstVertex;
	while (newVertex -> next) newVertex = newVertex -> next;
	return firstPoly -> firstVertex -> x == newVertex -> x && firstPoly -> firstVertex -> y == newVertex -> y;
}

int ccw (struct vertexList *p0, struct vertexList *p1, struct vertexList *p2) {
	int dx1, dx2, dy1, dy2;
	dx1 = p1->x - p0->x; dy1 = p1->y - p0->y;
	dx2 = p2->x - p0->x; dy2 = p2->y - p0->y;
	if (dx1*dy2 > dy1*dx2) return +1;
	if (dx1*dy2 < dy1*dx2) return -1;
	if ((dx1*dx2 < 0) || (dy1*dy2 < 0)) return -1;
	if ((dx1*dx1+dy1*dy1) < (dx2*dx2+dy2*dy2)) return +1;
	return 0;
}

bool intersect(struct vertexList *l1a, struct vertexList *l1b, struct vertexList *l2) {
	// Check the lines
	if (! l1a) return true;
	if (! l1b) return true;
	if (! l2) return true;
	if (! l2->next) return true;
	
	// Lines sharing a vertex doesn't count...
	if ((l1a->x == l2->x && l1a->y == l2->y) ||
		(l1b->x == l2->x && l1b->y == l2->y) ||
		(l1b->x == l2->next->x && l1b->y == l2->next->y) ||
		(l1a->x == l2->next->x && l1a->y == l2->next->y)) {
		return false;
	}
		
	// Check for intersection
	return ((ccw(l1a, l1b, l2) * ccw(l1a, l1b, l2->next)) <=0)
		&& ((ccw(l2, l2->next, l1a) * ccw(l2, l2->next, l1b)) <=0);
}

// Which way the corner at p1 turns: 1 or -1, or 0 when the three points
// are on a line, as after Split Line, which doesn't make a polygon concave.
// (ccw says which way a point lies, and calls points on a line a turn.)
static int turn (struct vertexList *p0, struct vertexList *p1, struct vertexList *p2) {
	long long cross = (long long) (p1->x - p0->x) * (p2->y - p0->y) - (long long) (p1->y - p0->y) * (p2->x - p0->x);
	return cross > 0 ? 1 : cross < 0 ? -1 : 0;
}

bool polyIsConvex (struct polyList *firstPoly) {
	if (firstPoly -> firstVertex == NULL) return false;
	if (firstPoly -> firstVertex -> next == NULL) return false;
	if (firstPoly -> firstVertex -> next -> next == NULL) return false;
	
	int dir = 0, dir1 = 0;
	
	vertexList * newVertex1 = firstPoly -> firstVertex;
	vertexList * newVertex2 = newVertex1 -> next;
	vertexList * newVertex3 = newVertex2 -> next;
	while (newVertex3) {
		dir1 = turn(newVertex1, newVertex2, newVertex3);
		if (dir1) {
			if (dir && (dir != dir1)) {
				return false;
			}
			dir = dir1;
		}
		
		newVertex1 = newVertex2;
		newVertex2 = newVertex3;
		newVertex3 = newVertex3->next;
	}
	dir1 = turn(newVertex1, newVertex2, newVertex3 = firstPoly->firstVertex->next);
	if (dir1) {
		if (dir && (dir != dir1)) {
			return false;
		}
		dir = dir1;
	}
	dir1 = turn(newVertex2, newVertex3, newVertex3->next);
	if (dir1) {
		if (dir && (dir != dir1)) {
			return false;
		}
		dir = dir1;
	}
	return true;
}

int getPolyDirection (struct polyList *firstPoly) {
	if (firstPoly -> firstVertex == NULL) return false;
	if (firstPoly -> firstVertex -> next == NULL) return false;
	if (firstPoly -> firstVertex -> next -> next == NULL) return false;
	
	int dir = 0;
	
	vertexList * newVertex1 = firstPoly -> firstVertex;
	vertexList * newVertex2 = newVertex1 -> next;
	vertexList * newVertex3 = newVertex2 -> next;
	while (newVertex3) {
		dir += ccw(newVertex1, newVertex2, newVertex3);

		newVertex1 = newVertex2;
		newVertex2 = newVertex3;
		newVertex3 = newVertex3->next;
	}
	dir += ccw(newVertex1, newVertex2, newVertex3 = firstPoly->firstVertex->next);
	dir += ccw(newVertex2, newVertex3, newVertex3->next);
	if (dir > 0) return 1;
	if (dir < 0) return -1;
	return 0;
}

struct vertexList * findConcaveVertex (struct polyList *firstPoly, int dir) {
	if (firstPoly -> firstVertex == NULL) return NULL;
	if (firstPoly -> firstVertex -> next == NULL) return NULL;
	if (firstPoly -> firstVertex -> next -> next == NULL) return NULL;
	if (! dir) return NULL;
	
	int dir1 = 0;
	
	vertexList * newVertex1 = firstPoly -> firstVertex;
	vertexList * newVertex2 = newVertex1 -> next;
	vertexList * newVertex3 = newVertex2 -> next;
	vertexList * test;
	while (newVertex3) {
		dir1 = ccw(newVertex1, newVertex2, newVertex3);
		if (dir1) {
			if (dir != dir1) {
				test = (newVertex3->next) ? newVertex3->next : firstPoly -> firstVertex->next;
				dir1 = ccw(newVertex2, newVertex3, test);
				if (dir == dir1)
					return newVertex2;
			}
		}
		
		newVertex1 = newVertex2;
		newVertex2 = newVertex3;
		newVertex3 = newVertex3->next;
	}
	dir1 = ccw(newVertex1, newVertex2, newVertex3 = firstPoly->firstVertex->next);
	if (dir1) {
		if (dir != dir1) {
			test = (newVertex3->next) ? newVertex3->next : firstPoly -> firstVertex;
			dir1 = ccw(newVertex2, newVertex3, test);
			if (dir == dir1)
				return newVertex2;
		}
	}
	dir1 = ccw(newVertex2, newVertex3, newVertex3->next);
	if (dir1) {
		if (dir != dir1) {
			test = (newVertex3->next->next) ? newVertex3->next->next : firstPoly -> firstVertex;
			dir1 = ccw(newVertex3, newVertex3->next, test);
			if (dir == dir1)
				return newVertex3;
		}
	}
	return NULL;
}


polyList * addPoly (struct polyList *firstPoly) {
	polyList * newPoly = new polyList;
	if (newPoly == NULL) return NULL;
	newPoly -> next = firstPoly;
	newPoly -> firstVertex = NULL;
	firstPoly = newPoly;
	return newPoly;
}

#define CLOSENESS 6
bool snapToClosest (int *x1, int *y1, polyList * firstPoly) {
	int x = *x1;
	int y = *y1;
	polyList * pL =  firstPoly;
	if (x < 4) x = 0;
	if (y < 4) y = 0;
	while (pL) {
		vertexList * vL = pL -> firstVertex;
		while (vL) {
			if (abs (vL -> x - x) < CLOSENESS && abs (vL -> y - y) < CLOSENESS) {
				*x1 = vL -> x;
				*y1 = vL -> y;
				return true;
			}
			vL = vL -> next;
		}
		pL = pL -> next;
	}
	return false;
}

int addVertex (int x, int y, struct polyList *firstPoly) {
	
	// Let's return 2 if the floor's complete...
	if (polyIsComplete (firstPoly)) return 2;
	
	// Let's return 3 if the chosen point is already used here...
	vertexList * newVertex = firstPoly -> firstVertex;
	while (newVertex) {
		if (newVertex -> next && x == newVertex -> x && y == newVertex -> y) return 3;
		newVertex = newVertex -> next;
	}
	
	// Let's return 0 if we can't create a new vertexList thingy...
	newVertex = new vertexList;
	if (newVertex == NULL) return 0;
	
	if (x<0) x =0 ;
	if (y<0) y =0;
	
	// Wow! Now all we need to do is set the values and update the list!
	newVertex -> x = x;
	newVertex -> y = y;
	newVertex -> next = firstPoly -> firstVertex;
	firstPoly -> firstVertex = newVertex;
	
	// It worked, so...
	return 1;
}

bool moveVertices (int x1, int y1, int x2, int y2, struct polyList *firstPoly) {
	polyList * pL;
	vertexList * vL;
	bool got1, got2;
	
	if (x2<0) x2 = 0;
	if (y2<0) y2 = 0;
	
	// Check we're not doubling up...
	
	pL = firstPoly;
	while (pL) {
		got1 = false;
		got2 = false;
		vL = pL -> firstVertex;
		while (vL) {
			if (vL -> x == x1 && vL -> y == y1) got1 = true;
			if (vL -> x == x2 && vL -> y == y2) got2 = true;
			vL = vL -> next;
		}
		if (got1 && got2) return false;
		pL = pL -> next;
	}
	
	// Now more all appropriate points...
	
	pL = firstPoly;
	while (pL) {
		vL = pL -> firstVertex;
		while (vL) {
			if (vL -> x == x1 && vL -> y == y1) {
				vL -> x = x2;
				vL -> y = y2;
			}
			vL = vL -> next;
		}
		pL = pL -> next;
	}
	return true;
}

void removePoly (polyList * killMe, struct polyList **firstPoly) {
	polyList * * hunt = firstPoly;
	while (* hunt) {
		if (* hunt == killMe) {
			polyList * killer = (* hunt);
			* hunt = (* hunt) -> next;
			delete killer;
			if (! (*firstPoly)) *firstPoly = addPoly (*firstPoly);
			break;
		} else {
			hunt = & ((* hunt) -> next);
		}
	}
}


void killVertex (int x, int y, struct polyList **firstPoly) {
	polyList * pL =  *firstPoly;
	while (pL) {
		vertexList * * changeMe = & (pL -> firstVertex);
		bool killedAlready = false;
		while (* changeMe) {
			if ((* changeMe) -> x == x && (* changeMe) -> y == y) {
				if (killedAlready) {
					
					// It was the first and final (with more stuff in between), so fix the new ends instead of deleting
					(* changeMe) -> x = pL -> firstVertex -> x;
					(* changeMe) -> y = pL -> firstVertex -> y;
					changeMe = & ((* changeMe) -> next);
				} else {
					
					// It's just a normal situation
					vertexList * killMe = (* changeMe);
					(* changeMe) = (* changeMe) -> next;
					delete killMe;
					killedAlready = true;
				}
			} else {
				changeMe = & ((* changeMe) -> next);
			}				
		}
		
		// If we're down to 0 corners, destroy the polygon
		polyList * killMe = ((pL -> firstVertex == NULL) ? pL : NULL);
		
		// Or 1 corner, for that matter
		if (! killMe) {
			if (pL -> firstVertex -> next == NULL) {
				delete pL -> firstVertex;
				killMe = pL;
			}
		}
		
		// Or 2 the same...
		if (! killMe) {
			if (pL -> firstVertex -> x == pL -> firstVertex -> next -> x &&
				pL -> firstVertex -> y == pL -> firstVertex -> next -> y) {
				delete pL -> firstVertex -> next;
				delete pL -> firstVertex;
				killMe = pL;
			}
		}
		
		// OK, we're done! Next polygon please!
		pL = pL -> next;
		if (killMe) removePoly (killMe, firstPoly);
	}
}

void splitLine (int x1, int y1, int x2, int y2, struct polyList *firstPoly) {
	polyList * pL = firstPoly;
	while (pL) {
		vertexList * vL = pL -> firstVertex;
		if (vL) {
			while (vL -> next) {
				if ((vL -> x == x1 && vL -> y == y1 && vL -> next -> x == x2 && vL -> next -> y == y2) ||
					(vL -> x == x2 && vL -> y == y2 && vL -> next -> x == x1 && vL -> next -> y == y1)) {
					vertexList * newV = new vertexList;
					if (! newV) return;
					newV -> x = (x1 + x2) >> 1;
					newV -> y = (y1 + y2) >> 1;
					newV -> next = vL -> next;
					vL -> next = newV;
					break;
				}
				vL = vL -> next;
			}
		}
		pL = pL -> next;
	}
}




void splitPoly (int x1, int y1, int x2, int y2, struct polyList **firstPoly) {
	
	if (x1 == x2 && y1 == y2) return;

	vertexList * gotCorner1, * gotCorner2, * vTemp;
	bool swap;
	
	polyList * pL = *firstPoly;
	while (pL) {
		gotCorner1 = NULL;
		gotCorner2 = NULL;
		swap = false;
		vertexList * vL = pL -> firstVertex;
		if (vL) {
			while (vL -> next) {
				if (vL -> x == x1 && vL -> y == y1) {
					// If selected vertices are already connected, return.
					if (vL -> next -> x == x2 && vL -> next -> y == y2) return;
					gotCorner1 = vL;
				}
				if (vL -> x == x2 && vL -> y == y2) {
					if (vL -> next -> x == x1 && vL -> next -> y == y1) return;
					gotCorner2 = vL;
					if (gotCorner1 == NULL) swap = true;
				}
				vL = vL -> next;
			}
			if (gotCorner1 && gotCorner2) {
				if (swap) {
					vTemp = gotCorner2;
					gotCorner2 = gotCorner1;
					gotCorner1 = vTemp;
				}
				
				polyList * p2 = (*firstPoly) = addPoly (*firstPoly);
				
				vTemp = gotCorner1 -> next;
				gotCorner1 -> next = gotCorner2;
				p2 -> firstVertex = vTemp;
				while (vTemp -> next != gotCorner2) {
					vTemp = vTemp -> next;
				}
				vTemp -> next = NULL;
				
				addVertex (gotCorner1 -> x, gotCorner1 -> y, *firstPoly);
				addVertex (gotCorner2 -> x, gotCorner2 -> y, *firstPoly);
				addVertex (vTemp -> x, vTemp -> y, *firstPoly);
			}
		}
		pL = pL -> next;
	}
}




void noFloor (struct polyList **firstPoly) {
	while (*firstPoly) {
		while ((*firstPoly) -> firstVertex) {
			vertexList * killMe = (*firstPoly) -> firstVertex;
			(*firstPoly) -> firstVertex = killMe -> next;
			delete killMe;
		}
		polyList * killPoly = *firstPoly;
		*firstPoly = (*firstPoly) -> next;
		delete killPoly;
	}
	*firstPoly = addPoly (*firstPoly);
}


bool loadFloorFromFile (const char * name, struct polyList **firstPoly) {
	bool adding = false;
	int numGot = 0, gotX, firstX, firstY;
	
	FILE * fp = fopen (name, "rb");
	if (! fp) return false;
	char c;
	
	noFloor (firstPoly);
	delete *firstPoly;
	*firstPoly = NULL;
	
	while (! feof (fp)) {
		
		c = fgetc (fp);
		if (feof (fp)) c = '\n';
		
		switch (c) {
			case '*':
				adding = true;
				firstX = -1;
				*firstPoly = addPoly (*firstPoly);
				numGot = 0;
				break;
				
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				if (adding) numGot = numGot * 10 + (c - '0');
				break;
				
			case ',':
				if (adding) {
					gotX = numGot;
					numGot = 0;
				}
				break;
				
			case '\n':
			case ';':
				if (adding) {
					addVertex (gotX, numGot, *firstPoly);
					if (firstX == -1) {
						firstX = gotX;
						firstY = numGot;
					}
					if (c == '\n') {
						addVertex (firstX, firstY, *firstPoly);
						adding = false;
					}
					numGot = 0;
				}
				break;
				
			default:
				break;
		}
	}
	fclose (fp);
	return true;
}

// A circle of the given radius around (x, y), filled or as an outline.
static void drawCircle (Draw2D & draw, float x, float y, float radius, bool filled) {
	const int SEGMENTS = 20;
	float points[SEGMENTS * 2];
	for (int i = 0; i < SEGMENTS; i ++) {
		float angle = (float) i * 6.2831853f / SEGMENTS;
		points[i * 2] = x + radius * cosf (angle);
		points[i * 2 + 1] = y + radius * sinf (angle);
	}
	if (filled) draw.drawPolygon (points, SEGMENTS);
	else draw.drawLineLoop (points, SEGMENTS);
}

// The radii, in points of the window, of the dot on a vertex and of the
// circle on the vertex under the cursor.
static const float VERTEX_POINTS = 3.5f;
static const float LIT_POINTS = 7.0f;

void drawFloor (Draw2D & draw, struct polyList * floor, float r, float g, float b, float pointPixels) {
	struct polyList * pL = floor;
	
	struct vertexList * vL;
	std::vector<vertexList> drawnVertices;
	
	if (! floor) return;

	// The first vertex of the polygon that we draw in, while it isn't
	// complete, is an open circle; we don't draw a dot on it.
	vertexList * start = NULL;
	if (floor -> firstVertex && ! polyIsComplete (floor)) {
		start = floor -> firstVertex;
		while (start -> next) start = start -> next;
		drawnVertices.push_back (*start);
	}
	float radius = VERTEX_POINTS * pointPixels;
	
	while (pL) {
		std::vector<float> points;
		for (vL = pL -> firstVertex; vL; vL = vL -> next) {
			points.push_back (vL -> x);
			points.push_back (vL -> y);
		}
		if (polyIsConvex(pL)) {
			draw.setDrawingColour(r, g, b, 0.25);
		} else {
			draw.setDrawingColour(1.0, 0.0, 0.0, 0.25);
		}
		draw.drawPolygon(points.data(), points.size() / 2);
			
		draw.setDrawingColour(r, g, b, 1.0);
		for (vL = pL -> firstVertex; vL; vL = vL -> next) {
			// Draw the line from here to the next point
			if (vL -> next) {
				const float line[] = { (float) vL -> x, (float) vL -> y, (float) vL -> next -> x, (float) vL -> next -> y };
				draw.drawLines(line, 2);
			}
			// A dot on each vertex, once.
			bool drawn = false;
			for (size_t i = 0; i < drawnVertices.size () && ! drawn; i ++) {
				drawn = drawnVertices[i].x == vL -> x && drawnVertices[i].y == vL -> y;
			}
			if (! drawn) {
				drawnVertices.push_back (*vL);
				drawCircle (draw, vL -> x, vL -> y, radius, true);
			}
		}
		pL = pL -> next;
	}
	if (start) {
		draw.setDrawingColour(r, g, b, 1.0);
		drawCircle (draw, start -> x, start -> y, radius, false);
	}
}


// A polygon that isn't complete yet isn't for fixing. (The GTK tools once
// checked only floors of more than one polygon, and the unfinished ones
// too, which they then couldn't fix.)
bool floorNeedsFixing (struct polyList * firstPoly) {
	for (polyList * pL = firstPoly; pL; pL = pL -> next) {
		if (polyIsComplete (pL) && ! polyIsConvex (pL)) return true;
	}
	return false;
}

bool fixFloor (struct polyList **firstPoly) {
	int dir;
	polyList * pL = *firstPoly;
	// A split that changes nothing would make us try again for ever.
	int splits = 0;
	while (pL) {
		if (polyIsComplete (pL) && ! polyIsConvex (pL)) {
			// Fix it!
			if (! (dir = getPolyDirection(pL)) || ++ splits > 1000) {
				return false;
			}

			vertexList * vL1 = findConcaveVertex (pL, dir);
			if (vL1) {
				vertexList * vL2;
				if (vL1->next) vL2 = vL1->next; else vL2 = pL->firstVertex->next;
				if (vL2->next) vL2 = vL2->next; else vL2 = pL->firstVertex->next;

				// Check that the line doesn't cross any other lines
				vertexList * vL = pL -> firstVertex;
				bool doIt = true;
				while (vL -> next) {
					if (intersect (vL1, vL2, vL)) {
						vL2 = (vL2->next) ? vL2->next : pL -> firstVertex;
						if (vL2 == vL1) {
							doIt = false;
							break;
						} else {
							vL = pL -> firstVertex;
						}
					} else {
						vL = vL -> next;
					}
				}

				if (doIt) {
					splitPoly (vL1->x, vL1->y, vL2->x, vL2->y, firstPoly);
					pL = *firstPoly;
					continue;
				}
			}
		}
		pL = pL -> next;
	}
	return true;
}

bool writeFloorToFile (const char * filename, struct polyList * firstPoly) {
	FILE * fp = fopen (filename, "wt");
	if (! fp) return false;
	for (polyList * pL = firstPoly; pL; pL = pL -> next) {
		vertexList * vL = pL -> firstVertex;
		if (vL) {
			fprintf (fp, "* ");
			while (vL -> next) {
				fprintf (fp, "%i, %i%s", vL -> x < 0 ? 0 : vL -> x, vL -> y < 0 ? 0 : vL -> y, vL -> next -> next ? "; " : "\n");
				vL = vL -> next;
			}
		}
	}
	return fclose (fp) == 0;
}

bool saveFloorToFile (char * filename, struct polyList **firstPoly) {
	// We open the file first, so that we don't ask about a file that we
	// can't write.
	FILE * fp = fopen (filename, "wt");
	if (! fp) return false;
	fclose (fp);

	if (floorNeedsFixing (*firstPoly)
			&& askAQuestion("Error in floor.", "I found a polygon in the floor that isn't convex. That will most likely cause problems. Do you want me to try and fix it for you?")
			&& ! fixFloor (firstPoly)) {
		errorBox ("Error", "I can't fix the error. The floor is too complicated, but I'll save it anyway.");
	}
	return writeFloorToFile (filename, *firstPoly);
}

void drawFloorMakerView (Draw2D & draw, const struct floorMakerView * view) {
	draw.clear ();

	draw.setDrawingColour(1.0f, 1.00f, 1.00f, 1.0f);
	pasteSprite(draw, view->backdrop, 0, false, false);

	draw.setDrawingColour(1.0f, 0.35f, 0.35f, 1.0f);
	const float axes[] = { 0.0, 0.0, 1000.0, 0.0, 0.0, 0.0, 0.0, 1000.0 };
	draw.drawLines(axes, 4);

	if (view->lit) {
		draw.setDrawingColour(view->r, view->g, view->b, 1.0f);
		drawCircle (draw, view->litX, view->litY, LIT_POINTS * view->pointPixels, true);
	}

	drawFloor(draw, view->floor, view->r, view->g, view->b, view->pointPixels);

	draw.setDrawingColour(1.0f, 0.00f, 0.00f, 1.0f);
	for (int i = 0; i < view->selectedCount; i ++) {
		drawCircle (draw, view->selectedXY[i * 2], view->selectedXY[i * 2 + 1], LIT_POINTS * view->pointPixels, true);
	}

	if (view->boxing) {
		const float box[] = {
			(float) view->boxX1, (float) view->boxY1, (float) view->boxX2, (float) view->boxY1,
			(float) view->boxX2, (float) view->boxY2, (float) view->boxX1, (float) view->boxY2
		};
		draw.setDrawingColour(1.0f, 0.00f, 0.00f, 0.15f);
		draw.drawPolygon(box, 4);
		draw.setDrawingColour(1.0f, 0.00f, 0.00f, 1.0f);
		draw.drawLineLoop(box, 4);
	}

	if (view->selecting) {
		draw.setDrawingColour(1.0f, 0.00f, 0.00f, 1.0f);
		const float line[] = { (float) view->selx1, (float) view->sely1, (float) view->selx2, (float) view->sely2 };
		draw.drawLines(line, 2);
	}
	draw.finish ();
}
