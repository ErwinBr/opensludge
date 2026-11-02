#include <stdio.h>
#include <string.h>

#include "FileMemory.h"

namespace devkit {

FileMemory::FileMemory (const std::string & f) : file (f) {
	if (file.empty ()) return;
	FILE * fp = fopen (file.c_str (), "rb");
	if (! fp) return;
	char line[8192];
	while (fgets (line, sizeof (line), fp)) {
		size_t length = strlen (line);
		while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[-- length] = 0;
		std::string text (line, length);
		size_t tab1 = text.find ('\t');
		size_t tab2 = tab1 == std::string::npos ? std::string::npos : text.find ('\t', tab1 + 1);
		if (tab2 == std::string::npos) continue;
		std::string path = text.substr (0, tab1);
		if (entries.empty () || entries.back ().path != path) {
			if (entries.size () == MAX_FILES) break;
			Entry entry;
			entry.path = path;
			entries.push_back (entry);
		}
		entries.back ().values.push_back (std::make_pair (text.substr (tab1 + 1, tab2 - tab1 - 1), text.substr (tab2 + 1)));
	}
	fclose (fp);
}

std::string FileMemory::get (const std::string & path, const std::string & key) const {
	for (size_t e = 0; e < entries.size (); e ++) {
		if (entries[e].path != path) continue;
		for (size_t v = 0; v < entries[e].values.size (); v ++) {
			if (entries[e].values[v].first == key) return entries[e].values[v].second;
		}
	}
	return std::string ();
}

// Tabs and line ends would break the file, so we leave them out.
static std::string clean (const std::string & text) {
	std::string cleaned;
	for (size_t i = 0; i < text.size (); i ++) {
		if (text[i] != '\t' && text[i] != '\n' && text[i] != '\r') cleaned += text[i];
	}
	return cleaned;
}

void FileMemory::set (const std::string & path, const std::string & key, const std::string & value) {
	if (path.empty ()) return;
	std::string p = clean (path), k = clean (key), v = clean (value);
	Entry entry;
	entry.path = p;
	for (size_t e = 0; e < entries.size (); e ++) {
		if (entries[e].path == p) {
			entry = entries[e];
			entries.erase (entries.begin () + e);
			break;
		}
	}
	bool found = false;
	for (size_t i = 0; i < entry.values.size (); i ++) {
		if (entry.values[i].first != k) continue;
		found = true;
		if (v.empty ()) entry.values.erase (entry.values.begin () + i);
		else entry.values[i].second = v;
		break;
	}
	if (! found && ! v.empty ()) entry.values.push_back (std::make_pair (k, v));
	if (! entry.values.empty ()) entries.insert (entries.begin (), entry);
	if (entries.size () > MAX_FILES) entries.resize (MAX_FILES);
	write ();
}

void FileMemory::write () const {
	if (file.empty ()) return;
	FILE * fp = fopen (file.c_str (), "wb");
	if (! fp) return;
	for (size_t e = 0; e < entries.size (); e ++) {
		for (size_t v = 0; v < entries[e].values.size (); v ++) {
			fprintf (fp, "%s\t%s\t%s\n", entries[e].path.c_str (), entries[e].values[v].first.c_str (), entries[e].values[v].second.c_str ());
		}
	}
	fclose (fp);
}

} // namespace devkit
