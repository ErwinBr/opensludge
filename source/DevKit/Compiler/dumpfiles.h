bool dumpFiles (FILE *, stringArray * & theSA);
bool saveStrings (FILE *, FILE *, stringArray *);
bool dumpFileInto (FILE * writer, const char * thisFile);
int getFileType (const char * filename);

enum {FILETYPE_UNKNOWN,
	  FILETYPE_MIDI,
	  FILETYPE_AUDIO,
	  FILETYPE_TGA,
	  FILETYPE_FLOOR,
	  FILETYPE_RAW,
	  FILETYPE_ANIMASSET};	// A rig or clip of the project (animassets.h)

#define audioFile(n) 		(getFileType(n) == FILETYPE_AUDIO)
