#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "RecentFiles.h"

namespace devkit {

RecentFiles::RecentFiles (const std::string & f) : file (f) {
	if (file.empty ()) return;
	FILE * fp = fopen (file.c_str (), "rb");
	if (! fp) return;
	char line[4096];
	while (list.size () < MAX_FILES && fgets (line, sizeof (line), fp)) {
		size_t length = strlen (line);
		while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[-- length] = 0;
		if (length) list.push_back (line);
	}
	fclose (fp);
}

void RecentFiles::add (const std::string & path) {
	list.erase (std::remove (list.begin (), list.end (), path), list.end ());
	list.insert (list.begin (), path);
	if (list.size () > MAX_FILES) list.resize (MAX_FILES);
	write ();
}

void RecentFiles::remove (const std::string & path) {
	list.erase (std::remove (list.begin (), list.end (), path), list.end ());
	write ();
}

void RecentFiles::clear () {
	list.clear ();
	write ();
}

void RecentFiles::write () const {
	if (file.empty ()) return;
	FILE * fp = fopen (file.c_str (), "wb");
	if (! fp) return;
	for (size_t i = 0; i < list.size (); i ++) fprintf (fp, "%s\n", list[i].c_str ());
	fclose (fp);
}

} // namespace devkit
