#define MAJOR_VERSION 2
#define MINOR_VERSION 2
#define RELEASE_VERSION 2
#define BUILD_VERSION 209
#define TEXT_VERSION "2.3.1"
#define WHOLE_VERSION (MAJOR_VERSION * 256 + MINOR_VERSION)	// This version
#define MINIM_VERSION (1 			 * 256 + 2)				// Earliest version of games the engine can run

#define COPYRIGHT_TEXT "\251 Hungry Software and contributors 2000-2014"

#define VERSION(a,b)	(a * 256 + b)

// Saved games have a version of their own, so that games keep the version
// above, and run on the engines they ran on, while saves change. 2.3 adds
// TIMELINE clips (newClip) and the playback of characters' clips.
#define SAVE_MAJOR_VERSION 2
#define SAVE_MINOR_VERSION 3
#define SAVE_VERSION	VERSION (SAVE_MAJOR_VERSION, SAVE_MINOR_VERSION)

extern int gameVersion;
