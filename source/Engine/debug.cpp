#include <stdarg.h>

#include "allfiles.h"
#include "debug.h"
#include "language.h"

void debugOut(const char * a, ...) {
	va_list argptr;
	va_start(argptr, a);
	debugOutV(a, argptr);
	va_end(argptr);
}

void debugOutV(const char * a, va_list argptr) {
	if (! gameSettings.debugMode) return;

#if defined(__unix__) || defined(__APPLE__)
	vfprintf(stderr, a, argptr);
#elif defined(_WIN32)
	FILE * fp = fopen ("debuggy.txt", "at");
	if (fp) {
		vfprintf (fp, a, argptr);
		fclose (fp);
	}
#endif
}

void debugHeader()
{
    debugOut( "*** Engine compiled " __DATE__ " at " __TIME__ ".\n");
}
