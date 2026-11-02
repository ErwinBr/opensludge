#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "ProjectDocument.h"

namespace devkit {

ProjectSettings::ProjectSettings ()
	: windowName ("New SLUDGE Game"), finalFile ("Gamedata"), language ("English"), dataFolder ("save"),
	  quitMessage ("Are you sure you want to quit?"),
	  mouse (2), fullScreen (true), makeSilent (false), showLogo (true), showLoading (true), invisible (false),
	  ditherImages (true), width (640), height (480), speed (20),
	  chrDefaultEnabled (true), chrDefaultSoftnessX (4), chrDefaultSoftnessY (4),
	  chrMaxEnabled (true), chrMaxReadIni (true), chrMaxSoftnessX (100), chrMaxSoftnessY (100) {}

bool ProjectSettings::operator == (const ProjectSettings & o) const {
	return windowName == o.windowName && finalFile == o.finalFile && language == o.language
		&& dataFolder == o.dataFolder && quitMessage == o.quitMessage && customIcon == o.customIcon
		&& customLogo == o.customLogo && mouse == o.mouse && fullScreen == o.fullScreen
		&& makeSilent == o.makeSilent && showLogo == o.showLogo && showLoading == o.showLoading
		&& invisible == o.invisible && ditherImages == o.ditherImages && width == o.width
		&& height == o.height && speed == o.speed && chrDefaultEnabled == o.chrDefaultEnabled
		&& chrDefaultSoftnessX == o.chrDefaultSoftnessX && chrDefaultSoftnessY == o.chrDefaultSoftnessY
		&& chrMaxEnabled == o.chrMaxEnabled && chrMaxReadIni == o.chrMaxReadIni
		&& chrMaxSoftnessX == o.chrMaxSoftnessX && chrMaxSoftnessY == o.chrMaxSoftnessY
		&& otherKeys == o.otherKeys;
}

DocumentType ProjectDocument::type () {
	DocumentType t;
	t.name = "Project";
	t.extensions.push_back ("slp");
	t.create = [] () -> Document * { return new ProjectDocument; };
	t.needsFile = true;
	t.single = true;
	return t;
}

// The lines of a file, without their line endings.
static bool readLines (const std::string & path, std::vector<std::string> * lines) {
	FILE * fp = fopen (path.c_str (), "rb");
	if (! fp) return false;
	std::string line;
	int c;
	while ((c = fgetc (fp)) != EOF) {
		if (c == '\n') {
			lines->push_back (line);
			line.clear ();
		} else if (c != '\r') {
			line += (char) c;
		}
	}
	if (! line.empty ()) lines->push_back (line);
	fclose (fp);
	return true;
}

// As stringToInt in settings.cpp: the digits, and nothing else.
static int parseNumber (const std::string & text) {
	unsigned int n = 0;
	for (size_t i = 0; i < text.size (); i ++) n = n * 10 + (text[i] - '0');
	return (int) n;
}

static bool yes (const std::string & text) {
	return ! text.empty () && text[0] == 'Y';
}

bool ProjectDocument::readProject (const std::string & path, ProjectSettings * s, std::vector<std::string> * files, std::string * error) {
	std::vector<std::string> lines;
	if (! readLines (path, &lines)) {
		*error = "The file can't be opened.";
		return false;
	}
	*s = ProjectSettings ();
	files->clear ();
	// The settings go on until the first empty line. As in settings.cpp,
	// a line without = (such as [SETTINGS]) means nothing.
	size_t i = 0;
	for (; i < lines.size () && ! lines[i].empty (); i ++) {
		size_t equals = lines[i].find ('=');
		if (equals == std::string::npos) continue;
		std::string key = lines[i].substr (0, equals), value = lines[i].substr (equals + 1);
		if (key == "quitmessage") s->quitMessage = value;
		else if (key == "customicon") s->customIcon = value;
		else if (key == "customlogo") s->customLogo = value;
		else if (key == "datafolder") s->dataFolder = value;
		else if (key == "finalfile") s->finalFile = value;
		else if (key == "windowname") s->windowName = value;
		else if (key == "language") s->language = value;
		else if (key == "mouse") s->mouse = parseNumber (value);
		else if (key == "hidemouse") s->mouse = yes (value) ? 2 : 1;	// The old setting
		else if (key == "ditherimages") s->ditherImages = yes (value);
		else if (key == "fullscreen") s->fullScreen = yes (value);
		else if (key == "showlogo") s->showLogo = yes (value);
		else if (key == "showloading") s->showLoading = yes (value);
		else if (key == "invisible") s->invisible = yes (value);
		else if (key == "makesilent") s->makeSilent = yes (value);
		else if (key == "height") s->height = parseNumber (value);
		else if (key == "width") s->width = parseNumber (value);
		else if (key == "speed") s->speed = parseNumber (value);
		else if (key == "chrRender_def_enabled") s->chrDefaultEnabled = yes (value);
		else if (key == "chrRender_def_softX") s->chrDefaultSoftnessX = parseNumber (value);
		else if (key == "chrRender_def_softY") s->chrDefaultSoftnessY = parseNumber (value);
		else if (key == "chrRender_max_enabled") s->chrMaxEnabled = yes (value);
		else if (key == "chrRender_max_readIni") s->chrMaxReadIni = yes (value);
		else if (key == "chrRender_max_softX") s->chrMaxSoftnessX = parseNumber (value);
		else if (key == "chrRender_max_softY") s->chrMaxSoftnessY = parseNumber (value);
		else s->otherKeys.push_back (std::make_pair (key, value));
	}
	for (; i < lines.size () && lines[i] != "[FILES]"; i ++) {}
	for (i ++; i < lines.size (); i ++) {
		std::string file = lines[i];
		if (file.empty ()) continue;
		std::replace (file.begin (), file.end (), '\\', '/');
		if (std::find (files->begin (), files->end (), file) == files->end ()) files->push_back (file);
	}
	return true;
}

bool ProjectDocument::writeProject (const std::string & path, const ProjectSettings & s, const std::vector<std::string> & files, std::string * error) {
	FILE * fp = fopen (path.c_str (), "wb");
	if (! fp) {
		*error = "The file can't be written.";
		return false;
	}
	// In the order of writeSettings in settings.cpp.
	fprintf (fp, "[SETTINGS]\nwindowname=%s\n", s.windowName.c_str ());
	fprintf (fp, "finalfile=%s\n", s.finalFile.c_str ());
	fprintf (fp, "language=%s\n", s.language.c_str ());
	fprintf (fp, "datafolder=%s\n", s.dataFolder.c_str ());
	fprintf (fp, "quitmessage=%s\n", s.quitMessage.c_str ());
	fprintf (fp, "customicon=%s\n", s.customIcon.c_str ());
	fprintf (fp, "customlogo=%s\n", s.customLogo.c_str ());
	fprintf (fp, "mouse=%i\n", s.mouse);
	fprintf (fp, "fullscreen=%c\n", s.fullScreen ? 'Y' : 'N');
	fprintf (fp, "makesilent=%c\n", s.makeSilent ? 'Y' : 'N');
	fprintf (fp, "showlogo=%c\n", s.showLogo ? 'Y' : 'N');
	fprintf (fp, "showloading=%c\n", s.showLoading ? 'Y' : 'N');
	fprintf (fp, "invisible=%c\n", s.invisible ? 'Y' : 'N');
	fprintf (fp, "ditherimages=%c\n", s.ditherImages ? 'Y' : 'N');
	fprintf (fp, "width=%i\n", s.width);
	fprintf (fp, "height=%i\n", s.height);
	fprintf (fp, "speed=%i\n", s.speed);
	fprintf (fp, "chrRender_def_enabled=%c\n", s.chrDefaultEnabled ? 'Y' : 'N');
	fprintf (fp, "chrRender_def_softX=%i\n", s.chrDefaultSoftnessX);
	fprintf (fp, "chrRender_def_softY=%i\n", s.chrDefaultSoftnessY);
	fprintf (fp, "chrRender_max_enabled=%c\n", s.chrMaxEnabled ? 'Y' : 'N');
	fprintf (fp, "chrRender_max_readIni=%c\n", s.chrMaxReadIni ? 'Y' : 'N');
	fprintf (fp, "chrRender_max_softX=%i\n", s.chrMaxSoftnessX);
	fprintf (fp, "chrRender_max_softY=%i\n", s.chrMaxSoftnessY);
	for (size_t i = 0; i < s.otherKeys.size (); i ++) {
		fprintf (fp, "%s=%s\n", s.otherKeys[i].first.c_str (), s.otherKeys[i].second.c_str ());
	}
	fprintf (fp, "\n[FILES]\n");
	// Projects have \ between folders, on every system.
	for (size_t i = 0; i < files.size (); i ++) {
		std::string file = files[i];
		std::replace (file.begin (), file.end (), '/', '\\');
		fprintf (fp, "%s\n", file.c_str ());
	}
	bool ok = ! ferror (fp);
	fclose (fp);
	if (! ok) *error = "The file can't be written.";
	return ok;
}

bool ProjectDocument::read (const std::string & path, std::string * error) {
	return readProject (path, &projectSettings, &fileList, error);
}

bool ProjectDocument::write (const std::string & path, std::string * error) {
	return writeProject (path, projectSettings, fileList, error);
}

std::string ProjectDocument::fullPath (const std::string & file) const {
	return folder () + file;
}

// The parts of a path between its separators.
static std::vector<std::string> splitPath (const std::string & path) {
	std::vector<std::string> parts;
	std::string part;
	for (size_t i = 0; i <= path.size (); i ++) {
		if (i == path.size () || path[i] == '/' || path[i] == '\\') {
			if (! part.empty () && part != ".") parts.push_back (part);
			part.clear ();
		} else {
			part += path[i];
		}
	}
	return parts;
}

std::string ProjectDocument::relativePath (const std::string & full) const {
	std::vector<std::string> base = splitPath (folder ()), target = splitPath (full);
	size_t same = 0;
	while (same < base.size () && same + 1 < target.size () && base[same] == target[same]) same ++;
	std::string relative;
	for (size_t i = same; i < base.size (); i ++) relative += "../";
	for (size_t i = same; i < target.size (); i ++) {
		relative += target[i];
		if (i + 1 < target.size ()) relative += "/";
	}
	return relative;
}

void ProjectDocument::setSettings (const ProjectSettings & s, int mergeKey) {
	if (s == projectSettings) return;
	commands ().run (new ValueCommand<ProjectSettings> ("Project Properties", projectSettings, s,
		[this] (const ProjectSettings & value) { projectSettings = value; }, mergeKey));
}

void ProjectDocument::setFiles (const std::vector<std::string> & files, const std::string & label) {
	commands ().run (new ValueCommand<std::vector<std::string> > (label, fileList, files,
		[this] (const std::vector<std::string> & value) { fileList = value; }));
}

int ProjectDocument::addFiles (const std::vector<std::string> & fullPaths) {
	std::vector<std::string> files = fileList;
	int added = 0;
	for (size_t i = 0; i < fullPaths.size (); i ++) {
		std::string file = relativePath (fullPaths[i]);
		if (std::find (files.begin (), files.end (), file) != files.end ()) continue;
		files.push_back (file);
		added ++;
	}
	if (added) setFiles (files, added == 1 ? "Add File" : "Add Files");
	return added;
}

void ProjectDocument::removeFiles (const std::vector<int> & indices) {
	std::vector<std::string> files;
	for (size_t i = 0; i < fileList.size (); i ++) {
		if (std::find (indices.begin (), indices.end (), (int) i) == indices.end ()) files.push_back (fileList[i]);
	}
	if (files.size () != fileList.size ()) setFiles (files, indices.size () == 1 ? "Remove File" : "Remove Files");
}

void ProjectDocument::moveFile (int from, int to) {
	if (from == to || from < 0 || to < 0 || from >= (int) fileList.size () || to >= (int) fileList.size ()) return;
	std::vector<std::string> files = fileList;
	std::string file = files[from];
	files.erase (files.begin () + from);
	files.insert (files.begin () + to, file);
	setFiles (files, "Move File");
}

// As populateResourceList in project.cpp: the text between single quotes,
// outside comments (from #) and strings (in double quotes).
std::vector<std::string> ProjectDocument::resourcesOf (const std::string & file) const {
	std::vector<std::string> resources;
	if (extensionOf (file) != "slu") return resources;
	FILE * fp = fopen (fullPath (file).c_str (), "rb");
	if (! fp) return resources;
	enum { NORMAL, QUOTE, COMMENT, FILENAME } mode = NORMAL;
	int c, last = ' ';
	std::string name;
	while ((c = fgetc (fp)) != EOF) {
		switch (mode) {
			case NORMAL:
				if (c == '\'') {
					mode = FILENAME;
					name.clear ();
				}
				if (c == '"') mode = QUOTE;
				if (c == '#') mode = COMMENT;
				break;
			case COMMENT:
				if (c == '\n') mode = NORMAL;
				break;
			case QUOTE:
				if (c == '"' && last != '\\') mode = NORMAL;
				break;
			case FILENAME:
				if (c == '\'' && last != '\\') {
					mode = NORMAL;
					std::replace (name.begin (), name.end (), '\\', '/');
					if (std::find (resources.begin (), resources.end (), name) == resources.end ()) resources.push_back (name);
				} else {
					name += (char) c;
				}
				break;
		}
		last = c;
	}
	fclose (fp);
	return resources;
}

} // namespace devkit
