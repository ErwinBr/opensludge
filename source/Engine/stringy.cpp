#include "allfiles.h"
#include <string.h>

#include "newfatal.h"

char * copyString (const char * copyMe) {
	char * newString = new char [strlen (copyMe) + 1];
	if (! checkNew (newString)) return NULL;
	strcpy (newString, copyMe);
	return newString;
}

char * joinStrings (const char * s1, const char * s2) {
	size_t size = strlen (s1) + strlen (s2) + 1;
	char * newString = new char [size];
	if (! checkNew (newString)) return NULL;
	snprintf (newString, size, "%s%s", s1, s2);
	return newString;
}
