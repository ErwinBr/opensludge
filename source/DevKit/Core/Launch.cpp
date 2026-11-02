#include <stdio.h>

#include "Document.h"
#include "Launch.h"

namespace devkit {

static void replaceAll (std::string * text, const std::string & from, const std::string & to) {
	size_t at = 0;
	while ((at = text->find (from, at)) != std::string::npos) {
		text->replace (at, from.size (), to);
		at += to.size ();
	}
}

std::vector<std::string> commandLine (const std::string & command, const std::string & file, int line) {
	std::vector<std::string> args;
	std::string arg;
	bool inQuotes = false, haveArg = false;
	for (size_t i = 0; i <= command.size (); i ++) {
		char c = i < command.size () ? command[i] : ' ';
		if (c == '"') {
			inQuotes = ! inQuotes;
			haveArg = true;
		} else if ((c == ' ' || c == '\t') && ! inQuotes) {
			if (haveArg) args.push_back (arg);
			arg.clear ();
			haveArg = false;
		} else {
			arg += c;
			haveArg = true;
		}
	}
	char number[16];
	snprintf (number, sizeof (number), "%d", line > 0 ? line : 1);
	bool usedFile = false;
	for (size_t i = 0; i < args.size (); i ++) {
		if (args[i].find ("{file}") != std::string::npos) usedFile = true;
		replaceAll (&args[i], "{file}", file);
		replaceAll (&args[i], "{line}", number);
	}
	if (! usedFile) args.push_back (file);
	return args;
}

FileKind fileKind (const std::string & path) {
	std::string e = extensionOf (path);
	if (e == "slu" || e == "sld") return FILE_SCRIPT;
	if (e == "tra") return FILE_TRANSLATION;
	if (e == "png" || e == "tga" || e == "bmp" || e == "jpg" || e == "jpeg") return FILE_IMAGE;
	if (e == "wav" || e == "ogg" || e == "flac") return FILE_AUDIO;
	if (e == "mod" || e == "s3m" || e == "it" || e == "xm") return FILE_MODULE;
	if (e == "flo" || e == "zbu" || e == "duc" || e == "slp") return FILE_DOCUMENT;
	return FILE_OTHER;
}

} // namespace devkit
