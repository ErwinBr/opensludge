#include <errno.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <iconv.h>
#endif

#include "cp1250.h"

#if defined(_WIN32)

char * convertCP1250ToUTF8 (const char * s) {
	int wideLength = MultiByteToWideChar (1250, 0, s, -1, NULL, 0);
	if (! wideLength) return NULL;
	WCHAR * wide = new WCHAR[wideLength];
	MultiByteToWideChar (1250, 0, s, -1, wide, wideLength);

	char * result = NULL;
	int length = WideCharToMultiByte (CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
	if (length) {
		result = new char[length];
		WideCharToMultiByte (CP_UTF8, 0, wide, -1, result, length, NULL, NULL);
	}
	delete [] wide;
	return result;
}

#else

char * convertCP1250ToUTF8 (const char * s) {
	iconv_t convert = iconv_open ("UTF-8", "CP1250");
	if (convert == (iconv_t) -1) {
		fprintf (stderr, "Can't convert from CP1250: %s\n", strerror (errno));
		return NULL;
	}

	// Each character of CP1250 takes up to 3 bytes in UTF-8.
	size_t inLeft = strlen (s);
	size_t outLeft = 3 * inLeft;
	char * result = new char[outLeft + 1];
	char * in = const_cast<char *> (s);
	char * out = result;
	if (iconv (convert, &in, &inLeft, &out, &outLeft) == (size_t) -1) {
		fprintf (stderr, "Can't convert from CP1250: %s\n", strerror (errno));
		delete [] result;
		result = NULL;
	} else {
		*out = 0;
	}
	iconv_close (convert);
	return result;
}

#endif
