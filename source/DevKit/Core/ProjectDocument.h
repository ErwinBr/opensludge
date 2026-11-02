#ifndef SLUDGE_DEVKIT_PROJECT_DOCUMENT_H
#define SLUDGE_DEVKIT_PROJECT_DOCUMENT_H

/*
 * A SLUDGE project (.slp): the settings of the game and the list of its
 * scripts, constant definitions and translations. We read and write the
 * files as the GTK Project Manager did (ProjectManager/settings.cpp and
 * project.cpp), without the global state and the changes of folder of that
 * code, which the compiler still uses.
 */

#include <string>
#include <utility>
#include <vector>

#include "Document.h"

namespace devkit {

struct ProjectSettings {
	std::string windowName, finalFile, language, dataFolder, quitMessage, customIcon, customLogo;
	int mouse;				// 1 shows the mouse of the system, 2 hides it
	bool fullScreen, makeSilent, showLogo, showLoading, invisible, ditherImages;
	int width, height, speed;	// speed is the milliseconds of a frame
	// The settings of the rendering of characters.
	bool chrDefaultEnabled;
	int chrDefaultSoftnessX, chrDefaultSoftnessY;
	bool chrMaxEnabled, chrMaxReadIni;
	int chrMaxSoftnessX, chrMaxSoftnessY;
	// Keys that we don't know, which we keep as they are.
	std::vector<std::pair<std::string, std::string> > otherKeys;

	// The settings of a new project.
	ProjectSettings ();
	bool operator == (const ProjectSettings & other) const;
	bool operator != (const ProjectSettings & other) const { return ! (*this == other); }
};

class ProjectDocument : public Document {
public:
	static DocumentType type ();

	const char * kind () const { return "Project"; }
	std::string untitledName () const { return "Untitled Project.slp"; }

	const ProjectSettings & settings () const { return projectSettings; }
	// The files, relative to the folder of the project, with / between
	// folders, in the order in which the compiler takes them.
	const std::vector<std::string> & files () const { return fileList; }

	// The folder of the project, ending with a separator.
	std::string folder () const { return folderOf (path ()); }
	// A file of the project with its folder.
	std::string fullPath (const std::string & file) const;
	// A file as the project refers to it: relative to its folder, with /
	// between folders.
	std::string relativePath (const std::string & fullPath) const;

	// Changes, as commands that undo.
	void setSettings (const ProjectSettings & settings, int mergeKey = 0);
	// Add files, given with their folders. Returns how many were new.
	int addFiles (const std::vector<std::string> & fullPaths);
	void removeFiles (const std::vector<int> & indices);
	void moveFile (int from, int to);
	// Give a file of the project another name, such as its case on disk.
	void renameFile (int index, const std::string & name);

	// The resources that a script names, in quotes such as 'image.tga', in
	// the order in which it names them. Scripts are .slu files.
	std::vector<std::string> resourcesOf (const std::string & file) const;

	// Read and write projects, for the document and the tests.
	static bool readProject (const std::string & path, ProjectSettings * settings, std::vector<std::string> * files, std::string * error);
	static bool writeProject (const std::string & path, const ProjectSettings & settings, const std::vector<std::string> & files, std::string * error);

protected:
	bool read (const std::string & path, std::string * error);
	bool write (const std::string & path, std::string * error);

private:
	void setFiles (const std::vector<std::string> & files, const std::string & label);

	ProjectSettings projectSettings;
	std::vector<std::string> fileList;
};

} // namespace devkit

#endif
