#ifndef SLUDGE_DEVKIT_PREFERENCES_H
#define SLUDGE_DEVKIT_PREFERENCES_H

/*
 * The preferences of the Dev Kit, as the GTK Project Manager kept them in
 * SLUDGE.ini: lines of Key=Value. We read and write the same keys, so that
 * the old file still works, and keep any other keys as they were.
 */

#include <map>
#include <string>

namespace devkit {

struct Preferences {
	// Of the compiler.
	bool keepImages;		// Keep the compressed images (KillImages=0)
	bool writeStrings;		// Write the strings of the game to a text file
	bool verbose;			// Verbose errors, for development
	bool searchSensitive;	// Search case-sensitively

	// The programs that open files, with {file} and {line} where the file
	// and the line go. Empty means the program that the system opens the
	// file with. Scripts open in the Dev Kit's own editor; the Editor key of
	// the GTK Project Manager stays in the file as it was.
	std::string imageViewer;
	std::string audioPlayer;
	std::string modPlayer;

	// Of the animation editor: whether a change to a part keys it at once.
	bool autoKey;
	// How much of its height the picture takes, over the timeline.
	float clipSplit;

	Preferences ();

	// Read the file, or keep the defaults when there is none.
	bool read (const std::string & file);
	bool write (const std::string & file) const;

private:
	std::map<std::string, std::string> otherKeys;
};

} // namespace devkit

#endif
