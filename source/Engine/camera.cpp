#include <math.h>

#include "allfiles.h"
#include "camera.h"
#include "sludger.h"
#include "people.h"
#include "backdrop.h"
#include "graphics.h"
#include "moreio.h"
#include "loadsave.h"

extern int cameraX, cameraY;
extern float cameraZoom;

static CameraState camera;
static CameraSettings settings;
static int deadZoneWidth = 0, deadZoneHeight = 0;
static bool drawingScene = false;

// The furthest the camera goes into the scene, as aimCamera has it.
static float maxCameraX () {
	return sceneWidth - (float) winWidth / cameraZoom;
}

static float maxCameraY () {
	return sceneHeight - (float) winHeight / cameraZoom;
}

void aimedCamera (int x, int y, int & cx, int & cy) {
	cx = x;
	cy = y;
	cx -= (float) (winWidth >> 1) / cameraZoom;
	cy -= (float) (winHeight >> 1) / cameraZoom;

	if (cx < 0) cx = 0;
	else if (cx > maxCameraX ()) cx = maxCameraX ();
	if (cy < 0) cy = 0;
	else if (cy > maxCameraY ()) cy = maxCameraY ();
}

// A place for the camera in the scene, as aimedCamera keeps it there.
static float inScene (float v, float max) {
	if (v < 0) return 0;
	if (v > max) return (float) (int) max;
	return v;
}

static void stopWaiting () {
	if (camera.waiting) abortFunction (camera.waiting);
	camera.waiting = NULL;
}

void trackCharacter (int objectNumber) {
	stopWaiting ();
	camera.mode = CAMERA_TRACKING;
	camera.tracked = objectNumber;
	camera.haveGoal = false;
}

// Glides to a stop as it brakes, on a whole pixel.
static void glideToStop () {
	const CameraMotion & m = camera.motion;
	float d = brakingDistance (m, settings);
	camera.goalX = inScene (roundf (m.x + m.dirX * d), maxCameraX ());
	camera.goalY = inScene (roundf (m.y + m.dirY * d), maxCameraY ());
	camera.mode = CAMERA_GLIDING;
}

void stopTracking () {
	stopWaiting ();
	if (camera.mode == CAMERA_TRACKING) glideToStop ();
}

bool panCamera (int x, int y, loadedFunction * fun) {
	stopWaiting ();
	int cx, cy;
	aimedCamera (x, y, cx, cy);
	camera.goalX = cx;
	camera.goalY = cy;
	camera.jump = false;
	if (camera.motion.x == cx && camera.motion.y == cy && cameraX == cx && cameraY == cy) {
		camera.mode = CAMERA_STILL;
		camera.motion.speed = 0;
		return false;
	}
	camera.mode = CAMERA_GLIDING;
	camera.waiting = fun;
	return true;
}

void cameraAimed () {
	stopWaiting ();
	camera.mode = CAMERA_STILL;
	camera.jump = false;
	camera.motion.x = cameraX;
	camera.motion.y = cameraY;
	camera.motion.speed = 0;
	camera.setX = cameraX;
	camera.setY = cameraY;
}

void setCameraSpeed (float speed, int easeIn, int easeOut) {
	settings.speed = speed < 0 ? 0 : speed;
	settings.easeIn = easeIn < 0 ? 0 : easeIn;
	settings.easeOut = easeOut < 0 ? 0 : easeOut;
}

void setCameraDeadZone (int width, int height) {
	deadZoneWidth = width < 0 ? 0 : width;
	deadZoneHeight = height < 0 ? 0 : height;
}

void cameraNewScene () {
	camera.jump = true;
	camera.haveGoal = false;
}

// Where the camera should be to follow the character, and says whether it
// found it.
static bool trackingTarget (float & tx, float & ty) {
	onScreenPerson * p = findPerson (camera.tracked);
	if (! p || (p -> extra & EXTRA_FIXTOSCREEN)) return false;
	float halfW = (float) winWidth / (2 * cameraZoom);
	float halfH = (float) winHeight / (2 * cameraZoom);
	float gx, gy;
	if (camera.haveGoal && ! camera.jump) {
		gx = followGoal (camera.goalX, p -> x, halfW, deadZoneWidth);
		gy = followGoal (camera.goalY, p -> y, halfH, deadZoneHeight);
	} else {
		gx = p -> x - halfW;
		gy = p -> y - halfH;
	}
	// While the goal moves, the camera goes after it to a fraction of a
	// pixel; when it stays, the camera stops on a whole pixel.
	bool still = camera.haveGoal && gx == camera.goalX && gy == camera.goalY;
	camera.goalX = gx;
	camera.goalY = gy;
	camera.haveGoal = true;
	tx = inScene (still ? roundf (gx) : gx, maxCameraX ());
	ty = inScene (still ? roundf (gy) : gy, maxCameraY ());
	return true;
}

void updateCamera () {
	CameraMotion & m = camera.motion;
	// Something else moved the camera: aimCamera, a new scene, a loaded
	// game. It goes on from there.
	if (cameraX != camera.setX || cameraY != camera.setY) {
		m.x = cameraX;
		m.y = cameraY;
		m.speed = 0;
	}

	switch (camera.mode) {
		case CAMERA_STILL:
		m.x = cameraX;
		m.y = cameraY;
		break;

		case CAMERA_TRACKING: {
			float tx, ty;
			if (! trackingTarget (tx, ty)) {
				// Without the character, it stays, on a whole pixel.
				stepCamera (m, roundf (m.x), roundf (m.y), settings);
				break;
			}
			if (camera.jump) {
				m.x = roundf (tx);
				m.y = roundf (ty);
				m.speed = 0;
				camera.jump = false;
			} else {
				stepCamera (m, tx, ty, settings);
			}
			break;
		}

		case CAMERA_GLIDING:
		if (stepCamera (m, camera.goalX, camera.goalY, settings)) {
			m.speed = 0;
			camera.mode = CAMERA_STILL;
			if (camera.waiting) restartFunction (camera.waiting);
			camera.waiting = NULL;
		}
		break;
	}

	cameraX = (int) roundf (m.x);
	cameraY = (int) roundf (m.y);
	camera.setX = cameraX;
	camera.setY = cameraY;
}

void beginSceneCamera () {
	drawingScene = true;
}

void endSceneCamera () {
	drawingScene = false;
}

// To a pixel of the window, so everything in the scene moves by the same
// pixels.
static float drawPosition (float exact, int whole, float windowPixelsPerPixel) {
	if (exact == whole || windowPixelsPerPixel <= 0) return whole;
	return roundf (exact * windowPixelsPerPixel) / windowPixelsPerPixel;
}

float cameraDrawX () {
	if (! drawingScene || cameraX != camera.setX || cameraY != camera.setY) return cameraX;
	return drawPosition (camera.motion.x, cameraX, viewportWidth * cameraZoom / winWidth);
}

float cameraDrawY () {
	if (! drawingScene || cameraX != camera.setX || cameraY != camera.setY) return cameraY;
	return drawPosition (camera.motion.y, cameraY, viewportHeight * cameraZoom / winHeight);
}

CameraState takeCamera () {
	CameraState taken = camera;
	camera = CameraState ();
	camera.motion.x = camera.setX = cameraX;
	camera.motion.y = camera.setY = cameraY;
	return taken;
}

void restoreCamera (const CameraState & state) {
	stopWaiting ();
	camera = state;
}

void killCamera () {
	stopWaiting ();
	camera = CameraState ();
	camera.motion.x = camera.setX = cameraX;
	camera.motion.y = camera.setY = cameraY;
	settings = CameraSettings ();
	deadZoneWidth = deadZoneHeight = 0;
}

// Since 2.10.
void saveCamera (FILE * fp) {
	putFloat (settings.speed, fp);
	put2bytes (settings.easeIn, fp);
	put2bytes (settings.easeOut, fp);
	put2bytes (deadZoneWidth, fp);
	put2bytes (deadZoneHeight, fp);

	fputc (camera.mode, fp);
	put2bytes (camera.tracked, fp);
	putFloat (camera.motion.x, fp);
	putFloat (camera.motion.y, fp);
	putFloat (camera.motion.speed, fp);
	putFloat (camera.motion.dirX, fp);
	putFloat (camera.motion.dirY, fp);
	putFloat (camera.goalX, fp);
	putFloat (camera.goalY, fp);
	fputc (camera.haveGoal, fp);
	fputc (camera.jump, fp);
	put2bytes (camera.setX, fp);
	put2bytes (camera.setY, fp);
	if (camera.waiting) {
		fputc (1, fp);
		saveFunction (camera.waiting, fp);
	} else {
		fputc (0, fp);
	}
}

bool loadCamera (FILE * fp) {
	killCamera ();
	settings.speed = getFloat (fp);
	settings.easeIn = get2bytes (fp);
	settings.easeOut = get2bytes (fp);
	deadZoneWidth = get2bytes (fp);
	deadZoneHeight = get2bytes (fp);

	int mode = fgetc (fp);
	if (mode < CAMERA_STILL || mode > CAMERA_GLIDING) return false;
	camera.mode = (CameraMode) mode;
	camera.tracked = get2bytes (fp);
	camera.motion.x = getFloat (fp);
	camera.motion.y = getFloat (fp);
	camera.motion.speed = getFloat (fp);
	camera.motion.dirX = getFloat (fp);
	camera.motion.dirY = getFloat (fp);
	camera.goalX = getFloat (fp);
	camera.goalY = getFloat (fp);
	camera.haveGoal = getFlag (fp);
	camera.jump = getFlag (fp);
	camera.setX = get2bytes (fp);
	camera.setY = get2bytes (fp);
	if (getFlag (fp)) {
		camera.waiting = loadFunction (fp);
		if (! camera.waiting) return false;
	}
	return ! feof (fp);
}
