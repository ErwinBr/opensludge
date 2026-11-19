#ifndef SLUDGE_CAMERA_H
#define SLUDGE_CAMERA_H

#include <stdio.h>

#include "cameramotion.h"

struct loadedFunction;

// The camera as it follows a character (trackCharacter) or glides to a point
// (panCamera). cameraX and cameraY (builtin.cpp) stay where the camera is to
// a whole pixel, for everything but drawing the scene; while the camera
// moves, the scene is drawn where it is to a fraction of a pixel
// (cameraDrawX). When it stops, it is on a whole pixel.

enum CameraMode { CAMERA_STILL, CAMERA_TRACKING, CAMERA_GLIDING };

struct CameraState {
	CameraMode mode = CAMERA_STILL;
	CameraMotion motion;
	// The character it follows, for CAMERA_TRACKING.
	int tracked = 0;
	// Where it goes (CAMERA_GLIDING), or where it should be to follow the
	// character, before rounding (CAMERA_TRACKING).
	float goalX = 0, goalY = 0;
	bool haveGoal = false;
	// To jump to the character, the first time it finds it in a new scene.
	bool jump = false;
	// The function panCamera keeps waiting until the camera is there, or
	// NULL.
	loadedFunction * waiting = NULL;
	// cameraX and cameraY as the camera set them: if they are different,
	// something else moved the camera.
	int setX = 0, setY = 0;
};

// Where the camera goes for aimCamera (x, y): with (x, y) in the middle of
// the screen, but in the scene.
void aimedCamera (int x, int y, int & cameraX, int & cameraY);

void trackCharacter (int objectNumber);
void stopTracking ();
// Glides to centre on (x, y). Says whether fun is to wait until it's there.
bool panCamera (int x, int y, loadedFunction * fun);
// aimCamera set the camera: it stops at once.
void cameraAimed ();
void setCameraSpeed (float speed, int easeIn, int easeOut);
void setCameraDeadZone (int width, int height);
// A new scene: the camera jumps to the character it follows.
void cameraNewScene ();
// A tick: the camera moves on.
void updateCamera ();

// Draws the scene from where the camera is, to a fraction of a pixel, in
// between these two calls.
void beginSceneCamera ();
void endSceneCamera ();
// Where to draw the scene from: cameraX and cameraY, or while the camera
// moves and the scene is drawn, where it is to a pixel of the window.
float cameraDrawX ();
float cameraDrawY ();

// For freezing: the camera of the scene, which it gives up, and gets back.
CameraState takeCamera ();
void restoreCamera (const CameraState & state);

// For loading: the camera stands still, and its function stops waiting.
void killCamera ();
void saveCamera (FILE * fp);
bool loadCamera (FILE * fp);

#endif
