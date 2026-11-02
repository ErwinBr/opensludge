#include <ctype.h>

#include "Document.h"

namespace devkit {

Document::Document () {
	static unsigned int documents = 0;
	number = ++ documents;
}

std::string Document::displayName () const {
	return filePath.empty () ? untitledName () : fileNameOf (filePath);
}

bool Document::load (const std::string & path, std::string * error) {
	if (! read (path, error)) return false;
	filePath = path;
	commandStack.clear ();
	commandStack.markSaved ();
	return true;
}

bool Document::save (const std::string & path, std::string * error) {
	if (! write (path, error)) return false;
	filePath = path;
	commandStack.markSaved ();
	return true;
}

FileFilter DocumentType::filter () const {
	FileFilter f;
	f.name = name + " files";
	for (size_t i = 0; i < extensions.size (); i ++) {
		if (i) f.pattern += ";";
		f.pattern += extensions[i];
	}
	return f;
}

bool DocumentType::opens (const std::string & path) const {
	std::string extension = extensionOf (path);
	for (size_t i = 0; i < extensions.size (); i ++) {
		if (extensions[i] == extension) return true;
	}
	return false;
}

static size_t lastSeparator (const std::string & path) {
#if defined(_WIN32)
	return path.find_last_of ("/\\");
#else
	return path.rfind ('/');
#endif
}

std::string fileNameOf (const std::string & path) {
	size_t separator = lastSeparator (path);
	return separator == std::string::npos ? path : path.substr (separator + 1);
}

std::string folderOf (const std::string & path) {
	size_t separator = lastSeparator (path);
	return separator == std::string::npos ? std::string () : path.substr (0, separator + 1);
}

std::string extensionOf (const std::string & path) {
	std::string name = fileNameOf (path);
	size_t dot = name.rfind ('.');
	if (dot == std::string::npos) return std::string ();
	std::string extension = name.substr (dot + 1);
	for (size_t i = 0; i < extension.size (); i ++) extension[i] = (char) tolower ((unsigned char) extension[i]);
	return extension;
}

} // namespace devkit
