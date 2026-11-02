#ifndef SLUDGE_DEVKIT_PLATFORM_SDL_H
#define SLUDGE_DEVKIT_PLATFORM_SDL_H

/*
 * The Platform of the Dev Kit (Core/Platform.h) with SDL 3.
 */

#include <mutex>

#include "Platform.h"

struct SDL_Window;

namespace devkit {

class PlatformSDL : public Platform {
public:
	explicit PlatformSDL (SDL_Window * window);

	void openFileDialog (const std::string & title, const std::vector<FileFilter> & filters,
		const std::string & folder, bool multiple, FilesChosen done);
	void saveFileDialog (const std::string & title, const std::vector<FileFilter> & filters,
		const std::string & defaultPath, FilesChosen done);
	void openFolderDialog (const std::string & title, const std::string & folder, FilesChosen done);
	void showMessage (MessageKind kind, const std::string & title, const std::string & text);
	int ask (const std::string & title, const std::string & text, const std::vector<std::string> & buttons);
	bool startProgram (const std::vector<std::string> & arguments, const std::string & workingFolder, std::string * error);
	Process * runProgram (const std::vector<std::string> & arguments, const std::string & workingFolder, std::string * error);
	bool openWithSystem (const std::string & pathOrURL);
	std::string settingsFolder ();
	std::string cacheFolder ();
	std::string programFolder ();

	// Keep the settings in another folder, as the tests do, so that they
	// leave the user's alone.
	void setSettingsFolder (const std::string & folder) { settingsOverride = folder; }

	// Call the callbacks of the file dialogs that the user answered. The
	// main loop calls this between frames. SDL may answer on another
	// thread, and we push an event then, so that the main loop wakes up.
	void deliverAnswers ();

	struct Request;

private:
	SDL_Window * window;
	std::string settingsOverride;
	std::mutex mutex;
	std::vector<Request *> answered;

	friend void answerDialog (void * userdata, const char * const * files, int filter);
};

// The event that we push when a dialog was answered.
unsigned int dialogAnsweredEvent ();

} // namespace devkit

#endif
