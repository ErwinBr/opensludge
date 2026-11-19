#include <cmath>

#include "cameramotion.h"

// The ease-in and the ease-out as a change of speed per tick, or 0 for at
// once.
static float acceleration (const CameraSettings & s) {
	return s.easeIn > 0 ? s.speed / s.easeIn : 0;
}

static float deceleration (const CameraSettings & s) {
	return s.easeOut > 0 ? s.speed / s.easeOut : 0;
}

float brakingDistance (const CameraMotion & m, const CameraSettings & s) {
	float a = deceleration (s);
	if (a <= 0 || m.speed <= 0) return 0;
	// It moves speed - a, speed - 2a, ... until it stops.
	float d = 0;
	for (float v = m.speed - a; v > 0; v -= a) d += v;
	return d;
}

bool stepCamera (CameraMotion & m, float targetX, float targetY, const CameraSettings & s) {
	float dx = targetX - m.x, dy = targetY - m.y;
	float d = std::sqrt (dx * dx + dy * dy);
	if (d == 0) {
		m.speed = 0;
		return true;
	}
	if (s.speed <= 0) {
		m.x = targetX;
		m.y = targetY;
		m.speed = 0;
		return true;
	}

	float v = s.speed;
	float a = acceleration (s);
	if (a > 0 && m.speed + a < v) v = m.speed + a;

	// The fastest it can go and still stop at the target: going v, then
	// v - b, v - 2b, ..., it goes about v (v + b) / 2b.
	float b = deceleration (s);
	if (b > 0) {
		float brake = (std::sqrt (b * b + 8 * b * d) - b) / 2;
		if (brake < v) v = brake;
	}

	m.dirX = dx / d;
	m.dirY = dy / d;
	if (v >= d) {
		// The speed it went, so it keeps up with a target that moves as
		// fast.
		m.x = targetX;
		m.y = targetY;
		m.speed = d;
		return true;
	}
	m.x += m.dirX * v;
	m.y += m.dirY * v;
	m.speed = v;
	return false;
}

float followGoal (float goal, float point, float halfScreen, float zone) {
	float half = zone / 2;
	float centre = goal + halfScreen;
	if (point > centre + half) return point - half - halfScreen;
	if (point < centre - half) return point + half - halfScreen;
	return goal;
}
