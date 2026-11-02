/*
 *  helpers.cpp
 *  Helper functions that don't depend on other source files.
 */

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <string>

#include "helpers.h"

char * sourceDirectory = NULL;

char * joinStrings (const char * a, const char * b) {
	size_t size = strlen (a) + strlen (b) + 1;
	char * nS = new char[size];
	snprintf (nS, size, "%s%s", a, b);
	return nS;
}

char * joinStrings (const char * a, const char * b, const char * c) {
	size_t size = strlen (a) + strlen (b) + strlen (c) + 1;
	char * nS = new char[size];
	snprintf (nS, size, "%s%s%s", a, b, c);
	return nS;
}

char * joinStrings (const char * a, const char * b, const char * c, const char * d) {
	size_t size = strlen (a) + strlen (b) + strlen (c) + strlen (d) + 1;
	char * nS = new char[size];
	snprintf (nS, size, "%s%s%s%s", a, b, c, d);
	return nS;
}

char * joinQuote (char * a, char * b, char q1, char q2) {
	size_t size = strlen (a) + strlen (b) + 3;
	char * nS = new char[size];
	snprintf (nS, size, "%s%c%s%c", a, q1, b, q2);
	return nS;
}


bool getSourceDirFromName (const char * name) {
	char * filename = joinStrings (name, "");

	int a, lastSlash = -1;
	for (a = 0; filename[a]; a ++) {
		if (filename[a] == '/' || filename[a] == '\\') {
			lastSlash = a;
		}
	}
	if (lastSlash != -1) {
		char slashChar = filename[lastSlash];
		filename[lastSlash] = 0;
		if (chdir (filename)) {
			fprintf (stderr, "Can't move to source directory %s", filename);
			return false;
		}
		filename[lastSlash] = slashChar;
	}
	char buff[1000];
	if (! getcwd (buff, 1000)) {
		fprintf (stderr, "I can't work out which directory I'm in...");
		return false;
	}
	sourceDirectory = joinStrings (buff, "");
	fixPath (sourceDirectory, true);
	return true;
}

bool gotoSourceDirectory () {
	bool r = chdir (sourceDirectory);
	if (r) {
		fprintf (stderr, "Can't move to source directory %s", sourceDirectory);
		return false;
	}
	return true;
}


// Fix pathnames, because Windows doesn't use the same path separator
// as the rest of the world
void fixPath (char *filename, bool makeGood) {
	if (! filename) return;
	char * ptr;

	if (makeGood) {
		while (ptr = strstr (filename, "\\")) {
			ptr[0] = '/';
		}
	} else {
		while (ptr = strstr (filename, "/")) {
			ptr[0] = '\\';
		}
	}
}

static bool sameIgnoringCase (const char * a, const char * b) {
	for (; *a && *b; a ++, b ++) {
		if (tolower ((unsigned char) *a) != tolower ((unsigned char) *b)) return false;
	}
	return *a == *b;
}

char * fileNameOnDisk (const char * name, bool * found, bool * caseDiffers) {
	*found = true;
	*caseDiffers = false;
	std::string result, folder;
	if (name[0] == '/' || name[0] == '\\') {
		result = "/";
		folder = "/";
	}
	const char * part = name;
	while (*part) {
		const char * end = part;
		while (*end && *end != '/' && *end != '\\') end ++;
		std::string wanted (part, end - part);
		std::string onDisk = wanted;
		if (! wanted.empty () && wanted != "." && wanted != ".." && *found) {
			// The part as it is in the folder: the same, or else the same
			// but for case.
			DIR * dir = opendir (folder.empty () ? "." : folder.c_str ());
			bool exact = false, other = false;
			std::string otherName;
			if (dir) {
				while (struct dirent * entry = readdir (dir)) {
					if (wanted == entry->d_name) {
						exact = true;
						break;
					}
					if (! other && sameIgnoringCase (wanted.c_str (), entry->d_name)) {
						other = true;
						otherName = entry->d_name;
					}
				}
				closedir (dir);
			}
			if (! exact && other) {
				onDisk = otherName;
				*caseDiffers = true;
			} else if (! exact) {
				*found = false;
			}
		}
		if (! wanted.empty ()) {
			if (! result.empty () && result[result.size () - 1] != '/') result += "/";
			result += onDisk;
			folder = result + "/";
		}
		part = *end ? end + 1 : end;
	}
	char * reply = new char [result.size () + 1];
	strcpy (reply, result.c_str ());
	return reply;
}
