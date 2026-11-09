#ifndef SLUDGE_DEVKIT_DOCUMENT_H
#define SLUDGE_DEVKIT_DOCUMENT_H

/*
 * A file that the Dev Kit edits: a project, a floor, a z-buffer, a sprite
 * bank or a translation. Each kind is a subclass, with a DocumentType that
 * says which files it opens.
 */

#include <functional>
#include <string>
#include <vector>

#include "CommandStack.h"
#include "Platform.h"

namespace devkit {

class Document {
public:
	Document ();
	virtual ~Document () {}

	// The file, or empty while the document has never been saved.
	const std::string & path () const { return filePath; }
	// The name of the file, or a name such as "Untitled Floor.flo".
	std::string displayName () const;
	// A number that no other document of this run has, for ImGui.
	unsigned int id () const { return number; }

	CommandStack & commands () { return commandStack; }
	bool isModified () const { return commandStack.isModified (); }

	bool load (const std::string & path, std::string * error);
	bool save (const std::string & path, std::string * error);

	// The name of the kind of document, such as "Floor".
	virtual const char * kind () const = 0;
	virtual std::string untitledName () const = 0;
	// The folder that Save As starts in for a document that has no file
	// yet, such as that of the picture that it was made from, or empty.
	virtual std::string startFolder () const { return std::string (); }

protected:
	// Read or write the file. Return false with the reason in *error.
	virtual bool read (const std::string & path, std::string * error) = 0;
	virtual bool write (const std::string & path, std::string * error) = 0;

private:
	std::string filePath;
	CommandStack commandStack;
	unsigned int number;
};

// A kind of document, and the files that it opens.
struct DocumentType {
	std::string name;						// "Floor"
	std::vector<std::string> extensions;	// "flo", without the dot
	std::function<Document * ()> create;
	// A new one is saved in a file that we ask for first, as a project,
	// whose files are relative to its folder.
	bool needsFile;
	// Only one is open at a time: opening another closes it.
	bool single;

	DocumentType () : needsFile (false), single (false) {}

	FileFilter filter () const;
	bool opens (const std::string & path) const;
};

// The extension of a file name, in lower case and without the dot.
std::string extensionOf (const std::string & path);
// The name of a file without its folder.
std::string fileNameOf (const std::string & path);
// The folder of a file, ending with a separator, or empty.
std::string folderOf (const std::string & path);
// The path of a file from a folder, with / between folders, and ../ to go
// up.
std::string relativePathFrom (const std::string & folder, const std::string & fullPath);

} // namespace devkit

#endif
