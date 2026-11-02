#ifndef SLUDGE_DEVKIT_PLATFORM_H
#define SLUDGE_DEVKIT_PLATFORM_H

/*
 * What the Dev Kit needs from the system: file dialogs, message boxes,
 * starting programs, and where to keep its files. The editor uses only
 * this, so that it doesn't depend on SDL or Dear ImGui; the program gives
 * it an implementation (App/PlatformSDL.cpp), and the tests a fake one.
 *
 * Everything here is for the main thread. File dialogs answer later: the
 * program calls their callbacks on the main thread, between frames.
 */

#include <functional>
#include <string>
#include <vector>

namespace devkit {

// A filter of a file dialog, such as { "SLUDGE projects", "slp" }. The
// pattern is a list of extensions separated by semicolons, or "*".
struct FileFilter {
	std::string name;
	std::string pattern;
};

class Platform {
public:
	virtual ~Platform () {}

	// The chosen files, or none when the user cancelled.
	typedef std::function<void (const std::vector<std::string> & files)> FilesChosen;

	virtual void openFileDialog (const std::string & title, const std::vector<FileFilter> & filters,
		const std::string & folder, bool multiple, FilesChosen done) = 0;
	virtual void saveFileDialog (const std::string & title, const std::vector<FileFilter> & filters,
		const std::string & defaultPath, FilesChosen done) = 0;
	virtual void openFolderDialog (const std::string & title, const std::string & folder, FilesChosen done) = 0;

	enum MessageKind { MESSAGE_INFORMATION, MESSAGE_WARNING, MESSAGE_ERROR };
	virtual void showMessage (MessageKind kind, const std::string & title, const std::string & text) = 0;

	// Ask a question with two or three buttons, and wait for the answer:
	// the number of the button, 0 for the first. Escape chooses the last.
	virtual int ask (const std::string & title, const std::string & text, const std::vector<std::string> & buttons) = 0;

	// Start a program, without waiting for it. arguments[0] is the program,
	// which we look for in the PATH when it has no folder. Returns false,
	// with the reason in *error, when it can't start.
	virtual bool startProgram (const std::vector<std::string> & arguments, const std::string & workingFolder, std::string * error) = 0;

	// Open a file in the program that the system opens it with, or a web
	// page.
	virtual bool openWithSystem (const std::string & pathOrURL) = 0;

	// The folder for the settings of the Dev Kit, ending with a separator.
	virtual std::string settingsFolder () = 0;
	// The folder for temporary files, such as the compiler's.
	virtual std::string cacheFolder () = 0;
	// The folder of the Dev Kit's executable, ending with a separator.
	virtual std::string programFolder () = 0;
};

} // namespace devkit

#endif
