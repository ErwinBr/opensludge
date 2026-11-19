/*
 * Tests of how the camera glides (source/Engine/cameramotion.cpp): it gets
 * to its target exactly, never passes it, and keeps to its top speed, its
 * ease-in and its ease-out.
 */

#include <math.h>
#include <stdio.h>

#include "cameramotion.h"

static int failures = 0;

static void expect (bool ok, const char * what) {
	if (! ok) {
		fprintf (stderr, "FAIL: %s\n", what);
		failures ++;
	}
}

static CameraSettings settings (float speed, int easeIn, int easeOut) {
	CameraSettings s;
	s.speed = speed;
	s.easeIn = easeIn;
	s.easeOut = easeOut;
	return s;
}

// Steps until the camera is at the target, and gives the number of ticks, or
// -1 if it doesn't get there in 10000.
static int glide (CameraMotion & m, float tx, float ty, const CameraSettings & s, float * speeds = NULL, int maxSpeeds = 0) {
	for (int tick = 1; tick <= 10000; tick ++) {
		bool there = stepCamera (m, tx, ty, s);
		if (speeds && tick <= maxSpeeds) speeds[tick - 1] = m.speed;
		if (there) return tick;
	}
	return -1;
}

static void testEndsExactly () {
	const CameraSettings cases[] = {
		settings (3, 0, 0), settings (3, 10, 0), settings (3, 0, 10),
		settings (3, 10, 10), settings (0.7f, 25, 40), settings (12, 1, 1),
	};
	for (const CameraSettings & s : cases) {
		CameraMotion m;
		m.x = 10;
		m.y = 5;
		int ticks = glide (m, 317, 203, s);
		expect (ticks > 0, "it gets to the target");
		expect (m.x == 317 && m.y == 203, "it ends exactly on the target");
		expect (stepCamera (m, 317, 203, s) && m.speed == 0, "at the target it stands still");
	}
}

static void testNeverPasses () {
	CameraSettings s = settings (5, 8, 8);
	CameraMotion m;
	float last = 0;
	bool ok = true;
	for (int tick = 0; tick < 1000; tick ++) {
		stepCamera (m, 400, 0, s);
		if (m.x > 400 || m.x < last) ok = false;
		last = m.x;
	}
	expect (ok, "it never passes the target or turns back");
	expect (m.x == 400, "it gets there in the end");
}

static void testConstantSpeed () {
	CameraSettings s = settings (4, 0, 0);
	CameraMotion m;
	float speeds[25];
	int ticks = glide (m, 100, 0, s, speeds, 25);
	expect (ticks == 25, "100 pixels at 4 a tick takes 25 ticks");
	bool same = true;
	for (int i = 0; i < 25; i ++) if (speeds[i] != 4) same = false;
	expect (same, "without easing it goes at its top speed all the way");
}

static void testEaseIn () {
	CameraSettings s = settings (4, 8, 0);
	CameraMotion m;
	float speeds[20];
	glide (m, 1000, 0, s, speeds, 20);
	expect (fabsf (speeds[0] - 0.5f) < 1e-4f, "the first tick goes an eighth of the top speed");
	bool rising = true;
	for (int i = 1; i < 8; i ++) if (speeds[i] <= speeds[i - 1]) rising = false;
	expect (rising, "it speeds up over the ease-in");
	expect (fabsf (speeds[7] - 4) < 1e-4f && speeds[12] == 4, "it is at its top speed after the ease-in");
}

static void testEaseOut () {
	CameraSettings s = settings (4, 0, 10);
	CameraMotion m;
	float speeds[200];
	int ticks = glide (m, 300, 0, s, speeds, 200);
	expect (ticks > 0 && ticks < 200, "it gets there with an ease-out");
	// Over the last ticks it slows down steadily, with no jump at the end.
	bool falling = true;
	for (int i = ticks - 9; i < ticks; i ++) if (speeds[i] > speeds[i - 1] + 1e-4f) falling = false;
	expect (falling, "it slows down over the ease-out");
	expect (speeds[ticks - 1] < 1, "the last tick is slow");
	expect (speeds[0] == 4, "without an ease-in it starts at its top speed");
}

static void testLocked () {
	CameraSettings s = settings (0, 10, 10);
	CameraMotion m;
	expect (stepCamera (m, 123.5f, 77, s) && m.x == 123.5f && m.y == 77, "speed 0 goes to the target at once");
}

static void testFollowsMovingTarget () {
	// Without an ease-out, the camera catches up with a target slower than
	// its top speed, and then keeps up exactly.
	CameraSettings s = settings (3, 5, 0);
	CameraMotion m;
	float target = 0;
	for (int tick = 0; tick < 200; tick ++) {
		target += 2;
		stepCamera (m, target, 0, s);
	}
	expect (m.x == target, "it keeps up with a target slower than its top speed");
	expect (fabsf (m.speed - 2) < 1e-4f, "it goes as fast as the target");
	expect (glide (m, target, 0, s) == 1 && m.x == target, "it stops with the target");

	// With an ease-out, it stays a little behind, ready to brake, and
	// goes as fast as the target.
	s = settings (3, 5, 5);
	for (int tick = 0; tick < 200; tick ++) {
		target += 2;
		stepCamera (m, target, 0, s);
	}
	float behind = target - m.x;
	expect (behind > 0 && behind < 10, "with an ease-out it follows a little behind");
	expect (fabsf (m.speed - 2) < 1e-3f, "and goes as fast as the target");
	float speeds[100];
	int ticks = glide (m, target, 0, s, speeds, 100);
	expect (ticks > 1 && m.x == target, "when the target stops, it eases to it");

	// A target faster than the top speed: it lags, at its top speed.
	for (int tick = 0; tick < 100; tick ++) {
		target += 5;
		stepCamera (m, target, 0, s);
	}
	expect (m.x < target, "it lags behind a target faster than it");
	expect (m.speed <= 3 + 1e-4f, "it never goes faster than its top speed");
	glide (m, target, 0, s);
	expect (m.x == target, "it catches up when the target stops");
}

static void testStraightLine () {
	CameraSettings s = settings (2.5f, 6, 6);
	CameraMotion m;
	bool straight = true;
	for (int tick = 0; tick < 500; tick ++) {
		stepCamera (m, 300, 100, s);
		if (fabsf (m.y * 3 - m.x) > 1e-2f) straight = false;
	}
	expect (straight, "a diagonal glide keeps to the straight line");
	expect (m.x == 300 && m.y == 100, "and ends on its target");
}

static void testBrakingDistance () {
	CameraSettings s = settings (4, 0, 4);
	CameraMotion m;
	m.speed = 4;
	expect (brakingDistance (m, s) == 3 + 2 + 1, "braking from 4 by 1 a tick goes 6 pixels");
	expect (brakingDistance (m, settings (4, 0, 0)) == 0, "without an ease-out it stops at once");
}

static void testDeadZone () {
	// A 320 pixel screen: the middle is 160 pixels from the left.
	expect (followGoal (0, 200, 160, 0) == 40, "without a dead zone the point is in the middle");
	expect (followGoal (0, 180, 160, 60) == 0, "in the dead zone the camera stays");
	expect (followGoal (0, 130, 160, 60) == 0, "on the edge of the dead zone it stays");
	expect (followGoal (0, 200, 160, 60) == 10, "out of it to the right, the point is on its right edge");
	expect (followGoal (100, 200, 160, 60) == 70, "out of it to the left, the point is on its left edge");
}

int main () {
	testEndsExactly ();
	testNeverPasses ();
	testConstantSpeed ();
	testEaseIn ();
	testEaseOut ();
	testLocked ();
	testFollowsMovingTarget ();
	testStraightLine ();
	testBrakingDistance ();
	testDeadZone ();

	if (failures) {
		fprintf (stderr, "%d failures\n", failures);
		return 1;
	}
	printf ("The camera glides right.\n");
	return 0;
}
