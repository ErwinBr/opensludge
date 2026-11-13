#include <math.h>

#include "allfiles.h"
#include "newfatal.h"
#include "backdrop.h"
#include "scalemap.h"

scaleMapData scaleMap;

bool loadScaleMap (int fileNum, int minPercent, int maxPercent) {
	int w, h;
	std::vector<GLubyte> rgba;
	setResourceForFatal (fileNum);
	if (! loadImageFile (fileNum, "scale map", w, h, rgba)) return false;
	setResourceForFatal (-1);

	scaleMap.w = w;
	scaleMap.h = h;
	scaleMap.grey.resize ((size_t) w * h);
	for (size_t i = 0; i < scaleMap.grey.size (); i ++) {
		const GLubyte * p = & rgba[i * 4];
		scaleMap.grey[i] = (uint8_t) ((p[0] + p[1] + p[2] + 1) / 3);
	}
	scaleMap.fileNum = fileNum;
	scaleMap.minPercent = minPercent;
	scaleMap.maxPercent = maxPercent;
	return true;
}

void killScaleMap () {
	scaleMap = scaleMapData ();
}

float scaleMapAt (float x, float y) {
	if (! scaleMapActive () || ! sceneWidth || ! sceneHeight) return 1.0f;
	// The centres of the map's pixels, in the scene, are where their value
	// is exact; in between, we blend the four around.
	float u = x * scaleMap.w / sceneWidth - 0.5f;
	float v = y * scaleMap.h / sceneHeight - 0.5f;
	if (! (u > 0)) u = 0;	// Also NaN
	if (! (v > 0)) v = 0;
	if (u > scaleMap.w - 1) u = scaleMap.w - 1;
	if (v > scaleMap.h - 1) v = scaleMap.h - 1;
	int x0 = (int) u, y0 = (int) v;
	int x1 = x0 + 1 < scaleMap.w ? x0 + 1 : x0;
	int y1 = y0 + 1 < scaleMap.h ? y0 + 1 : y0;
	float fx = u - x0, fy = v - y0;
	const uint8_t * g = scaleMap.grey.data ();
	float top = g[y0 * scaleMap.w + x0] * (1 - fx) + g[y0 * scaleMap.w + x1] * fx;
	float bottom = g[y1 * scaleMap.w + x0] * (1 - fx) + g[y1 * scaleMap.w + x1] * fx;
	float t = (top * (1 - fy) + bottom * fy) / 255.0f;
	return (scaleMap.minPercent + (scaleMap.maxPercent - scaleMap.minPercent) * t) / 100.0f;
}
