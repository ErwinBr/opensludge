/*
 *  helpers.h
 *  Helper functions that don't depend on other source files.
 */

// Making something with a char *
char * joinStrings (const char * a, const char * b);
char * joinStrings (const char * a, const char * b, const char * c);
char * joinStrings (const char * a, const char * b, const char * c, const char * d);

char * joinQuote (char * a, char * b, char q1, char q2);

#ifdef __cplusplus
extern "C" {
#endif

extern char * sourceDirectory;

bool getSourceDirFromName (const char * filename);
bool gotoSourceDirectory ();

void fixPath (char *filename, bool makeGood);

// The name of a file of the project as it is on disk, relative to the
// current folder: with / between folders, and each part with the case that
// it has on disk. We read the folders instead of trusting the file system to
// ignore case, so that the name is the same on every system. Sets *found to
// whether the file exists, and *caseDiffers to whether the case on disk is
// another than in name. Free the name with delete [].
char * fileNameOnDisk (const char * name, bool * found, bool * caseDiffers);

#ifdef __cplusplus
}
#endif
