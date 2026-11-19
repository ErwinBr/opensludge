#ifndef SLUDGE_CAMERAMOTION_H
#define SLUDGE_CAMERAMOTION_H

// How the camera glides to where it should be: a top speed, with an ease-in
// and an ease-out. It knows nothing of the engine, so it can be tested on its
// own (tests/camera_test.cpp). camera.cpp uses it.

struct CameraSettings {
	// Pixels per tick. 0 locks the camera to where it should be.
	float speed = 0;
	// The ticks it takes to reach the top speed, and to brake from it to a
	// stop. 0 is at once.
	int easeIn = 0, easeOut = 0;
};

struct CameraMotion {
	// Where the camera is: its top left in the scene, to a fraction of a
	// pixel.
	float x = 0, y = 0;
	// How fast it moves, in pixels per tick, and which way (a unit vector,
	// or 0, 0 when it hasn't moved).
	float speed = 0;
	float dirX = 0, dirY = 0;
};

// Moves the camera a tick towards (targetX, targetY), and says whether it is
// there. It never passes the target, and gets there exactly, so a target on
// a whole pixel is where it comes to rest.
bool stepCamera (CameraMotion & m, float targetX, float targetY, const CameraSettings & s);

// How far the camera goes before it stops, if it brakes now.
float brakingDistance (const CameraMotion & m, const CameraSettings & s);

// Where the camera should be on one axis to follow a point: goal is its top
// left now, halfScreen half the width (or height) of the screen. The point
// can move in a dead zone of zone pixels around the middle of the screen;
// out of it, the camera goes along so the point is on its edge.
float followGoal (float goal, float point, float halfScreen, float zone);

#endif
