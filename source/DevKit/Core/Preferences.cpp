#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Preferences.h"

namespace devkit {

Preferences::Preferences ()
	: keepImages (true), writeStrings (false), verbose (true), searchSensitive (false) {}

bool Preferences::read (const std::string & file) {
	FILE * fp = fopen (file.c_str (), "rb");
	if (! fp) return false;
	char line[4096];
	while (fgets (line, sizeof (line), fp)) {
		size_t length = strlen (line);
		while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[-- length] = 0;
		char * equals = strchr (line, '=');
		if (! equals) continue;
		*equals = 0;
		std::string key = line, value = equals + 1;
		if (key == "KillImages") keepImages = ! atoi (value.c_str ());
		else if (key == "WriteStrings") writeStrings = atoi (value.c_str ()) != 0;
		else if (key == "Verbose") verbose = atoi (value.c_str ()) != 0;
		else if (key == "SearchSensitive") searchSensitive = atoi (value.c_str ()) != 0;
		else if (key == "Editor") editor = value;
		else if (key == "ImageViewer") imageViewer = value;
		else if (key == "AudioPlayer") audioPlayer = value;
		else if (key == "ModPlayer") modPlayer = value;
		else otherKeys[key] = value;
	}
	fclose (fp);
	return true;
}

bool Preferences::write (const std::string & file) const {
	FILE * fp = fopen (file.c_str (), "wb");
	if (! fp) return false;
	// With the line endings of the GTK tools.
	fprintf (fp, "KillImages=%d\r\n", keepImages ? 0 : 1);
	fprintf (fp, "WriteStrings=%d\r\n", writeStrings ? 1 : 0);
	fprintf (fp, "Verbose=%d\r\n", verbose ? 1 : 0);
	fprintf (fp, "SearchSensitive=%d\r\n", searchSensitive ? 1 : 0);
	fprintf (fp, "Editor=%s\r\n", editor.c_str ());
	fprintf (fp, "ImageViewer=%s\r\n", imageViewer.c_str ());
	fprintf (fp, "AudioPlayer=%s\r\n", audioPlayer.c_str ());
	fprintf (fp, "ModPlayer=%s\r\n", modPlayer.c_str ());
	for (std::map<std::string, std::string>::const_iterator i = otherKeys.begin (); i != otherKeys.end (); ++ i) {
		fprintf (fp, "%s=%s\r\n", i->first.c_str (), i->second.c_str ());
	}
	bool ok = ! ferror (fp);
	fclose (fp);
	return ok;
}

} // namespace devkit
