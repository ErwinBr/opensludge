#ifndef SLUDGE_DEVKIT_PROJECT_CHECK_H
#define SLUDGE_DEVKIT_PROJECT_CHECK_H

/*
 * What keeps a project from building on every system: text that isn't
 * UTF-8, as in projects from the old Windows versions of SLUDGE, and names
 * of files in another case than on disk, which Windows and macOS forgive
 * and Linux doesn't. We find these, and fix them.
 */

#include <string>
#include <vector>

namespace devkit {

class ProjectDocument;

// The code pages of the old Windows versions of SLUDGE.
enum CodePage {
	CODEPAGE_WINDOWS_1252,	// Western European
	CODEPAGE_WINDOWS_1250,	// Central European
};
const char * codePageName (CodePage page);

// The offset of the first byte of text that isn't UTF-8, or -1.
long invalidUTF8Offset (const std::string & text);
// Text in a code page, in UTF-8. Text that is UTF-8 already stays the same.
std::string toUTF8 (const std::string & text, CodePage page);

// The name of a file as it is on disk, in folder: with / between folders
// and each part in its case on disk. Sets *found and *caseDiffers.
std::string nameOnDisk (const std::string & folder, const std::string & name, bool * found, bool * caseDiffers);

struct FileProblem {
	enum Kind {
		NOT_UTF8,		// The text of file isn't UTF-8, from line on
		WRONG_CASE,		// file names name, which is onDisk on disk, at line
		MISSING,		// file names name, which isn't there, at line
	};
	Kind kind;
	std::string file;		// The file of the project with the problem
	int line;				// Where, or 0
	std::string name;		// The name that file has
	std::string onDisk;		// For WRONG_CASE
	std::string sample;		// For NOT_UTF8: the line, as it is
	bool inProject;			// The name is in the project's list of files

	FileProblem () : kind (NOT_UTF8), line (0), inProject (false) {}
};

// Check the files of a project, and the files that its scripts name.
std::vector<FileProblem> checkProject (const ProjectDocument & project);

// Convert a file of the project to UTF-8, and keep the old file as
// <file>.orig. Returns false, with the reason in *error, when it can't.
bool convertFileToUTF8 (const std::string & path, CodePage page, std::string * error);

// Write the names that a script has in another case as they are on disk,
// in every place where the script names them in quotes. Returns false,
// with the reason in *error, when it can't.
bool fixNamesInScript (const std::string & path, const std::vector<FileProblem> & problems, std::string * error);

} // namespace devkit

#endif
