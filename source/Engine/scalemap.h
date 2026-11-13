#ifndef _SCALEMAP_H_
#define _SCALEMAP_H_

#include <vector>
#include <stdint.h>

// The scale map (setScaleMap): a greyscale picture over the scene, which
// gives characters their scale at their feet instead of setScale. Black is
// minPercent, white maxPercent. The picture may be smaller or larger than
// the scene: it is stretched over it, and sampled smoothly.
struct scaleMapData {
	int fileNum = 0;			// 0: no scale map
	int w = 0, h = 0;
	std::vector<uint8_t> grey;	// w * h
	int minPercent = 0, maxPercent = 100;
};

extern scaleMapData scaleMap;

bool loadScaleMap (int fileNum, int minPercent, int maxPercent);
void killScaleMap ();
inline bool scaleMapActive () { return scaleMap.fileNum != 0; }
// The scale (1 is 100%) of the map at a point of the scene.
float scaleMapAt (float x, float y);

#endif
