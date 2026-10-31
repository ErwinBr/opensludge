#include <SDL3/SDL.h>

#include "sound.h"
#include "capture.h"

int desiredfps = 300;				//holds desired frames per second
static Uint64 starttime;
static Uint64 desired_frame_time;	// in nanoseconds

void Init_Timer(void)
{
	desired_frame_time = SDL_NS_PER_SECOND / desiredfps;
	starttime = engineTicksNS();
}

void Init_Special_Timer(double framesPerSecond)
{
	desired_frame_time = (Uint64) (SDL_NS_PER_SECOND / framesPerSecond);
	starttime = engineTicksNS();
}

void Wait_Frame (void (* whileWaiting) (Uint32 millisecondsLeft))
{
	// When a frame takes longer than it should, we make the next one
	// shorter, by up to a whole frame.
	static Uint64 addNextTime = 0;
	Uint64 endtime, timetaken;

	updateSounds ();
	
	for (;;) {
		endtime = engineTicksNS();
		timetaken = addNextTime + endtime - starttime;
		if (timetaken >= desired_frame_time) break;
		Uint64 left = desired_frame_time - timetaken;
		if (whileWaiting) whileWaiting ((Uint32) SDL_NS_TO_MS (left));
		// We wake up every millisecond for whileWaiting, and wait for the
		// last two precisely.
		if (left > 2 * SDL_NS_PER_MS) {
			engineDelayNS (SDL_NS_PER_MS, false);
		} else {
			engineDelayNS (left, true);
		}
	}

	addNextTime = timetaken - desired_frame_time;
	if (addNextTime > desired_frame_time) addNextTime = desired_frame_time;
	
	starttime = endtime;
}
