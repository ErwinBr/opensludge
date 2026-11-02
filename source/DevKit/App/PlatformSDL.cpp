#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "Log.h"
#include "PlatformSDL.h"

namespace devkit {

// A file dialog that is open. SDL needs the filters until it answers.
struct PlatformSDL::Request {
	PlatformSDL * platform;
	std::vector<std::string> names, patterns;
	std::vector<SDL_DialogFileFilter> filters;
	FilesChosen done;
	std::vector<std::string> files;
};

unsigned int dialogAnsweredEvent () {
	static Uint32 event = SDL_RegisterEvents (1);
	return event;
}

static PlatformSDL::Request * makeRequest (PlatformSDL * platform, const std::vector<FileFilter> & filters, Platform::FilesChosen done) {
	PlatformSDL::Request * request = new PlatformSDL::Request;
	request->platform = platform;
	request->done = done;
	for (size_t i = 0; i < filters.size (); i ++) {
		request->names.push_back (filters[i].name);
		request->patterns.push_back (filters[i].pattern);
	}
	for (size_t i = 0; i < filters.size (); i ++) {
		SDL_DialogFileFilter filter = { request->names[i].c_str (), request->patterns[i].c_str () };
		request->filters.push_back (filter);
	}
	return request;
}

void answerDialog (void * userdata, const char * const * files, int filter) {
	(void) filter;
	PlatformSDL::Request * request = (PlatformSDL::Request *) userdata;
	if (! files) {
		Log::get ().add (LogLine::LOG_ERROR, "The file dialog failed: %s\n", SDL_GetError ());
	} else {
		for (int i = 0; files[i]; i ++) request->files.push_back (files[i]);
	}
	{
		std::lock_guard<std::mutex> lock (request->platform->mutex);
		request->platform->answered.push_back (request);
	}
	SDL_Event event;
	SDL_zero (event);
	event.type = dialogAnsweredEvent ();
	SDL_PushEvent (&event);
}

PlatformSDL::PlatformSDL (SDL_Window * w) : window (w) {
	dialogAnsweredEvent ();
}

static const char * location (const std::string & path) {
	return path.empty () ? NULL : path.c_str ();
}

void PlatformSDL::openFileDialog (const std::string & title, const std::vector<FileFilter> & filters,
		const std::string & folder, bool multiple, FilesChosen done) {
	(void) title;
	Request * request = makeRequest (this, filters, done);
	SDL_ShowOpenFileDialog (answerDialog, request, window, request->filters.data (), (int) request->filters.size (),
		location (folder), multiple);
}

void PlatformSDL::saveFileDialog (const std::string & title, const std::vector<FileFilter> & filters,
		const std::string & defaultPath, FilesChosen done) {
	(void) title;
	Request * request = makeRequest (this, filters, done);
	SDL_ShowSaveFileDialog (answerDialog, request, window, request->filters.data (), (int) request->filters.size (),
		location (defaultPath));
}

void PlatformSDL::openFolderDialog (const std::string & title, const std::string & folder, FilesChosen done) {
	(void) title;
	Request * request = makeRequest (this, std::vector<FileFilter> (), done);
	SDL_ShowOpenFolderDialog (answerDialog, request, window, location (folder), false);
}

void PlatformSDL::deliverAnswers () {
	std::vector<Request *> requests;
	{
		std::lock_guard<std::mutex> lock (mutex);
		requests.swap (answered);
	}
	for (size_t i = 0; i < requests.size (); i ++) {
		if (requests[i]->done) requests[i]->done (requests[i]->files);
		delete requests[i];
	}
}

void PlatformSDL::showMessage (MessageKind kind, const std::string & title, const std::string & text) {
	SDL_MessageBoxFlags flags = kind == MESSAGE_ERROR ? SDL_MESSAGEBOX_ERROR
		: kind == MESSAGE_WARNING ? SDL_MESSAGEBOX_WARNING : SDL_MESSAGEBOX_INFORMATION;
	SDL_ShowSimpleMessageBox (flags, title.c_str (), text.c_str (), window);
}

int PlatformSDL::ask (const std::string & title, const std::string & text, const std::vector<std::string> & buttons) {
	std::vector<SDL_MessageBoxButtonData> data;
	for (size_t i = 0; i < buttons.size (); i ++) {
		SDL_MessageBoxButtonData button;
		SDL_zero (button);
		if (i == 0) button.flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
		if (i + 1 == buttons.size ()) button.flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
		button.buttonID = (int) i;
		button.text = buttons[i].c_str ();
		data.push_back (button);
	}
	SDL_MessageBoxData box;
	SDL_zero (box);
	box.flags = SDL_MESSAGEBOX_WARNING | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
	box.window = window;
	box.title = title.c_str ();
	box.message = text.c_str ();
	box.numbuttons = (int) data.size ();
	box.buttons = data.data ();
	int answer = -1;
	if (! SDL_ShowMessageBox (&box, &answer) || answer < 0) answer = (int) buttons.size () - 1;
	return answer;
}

bool PlatformSDL::startProgram (const std::vector<std::string> & arguments, const std::string & workingFolder, std::string * error) {
	std::vector<const char *> args;
	for (size_t i = 0; i < arguments.size (); i ++) args.push_back (arguments[i].c_str ());
	args.push_back (NULL);

	SDL_PropertiesID props = SDL_CreateProperties ();
	SDL_SetPointerProperty (props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *) args.data ());
	if (! workingFolder.empty ()) SDL_SetStringProperty (props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, workingFolder.c_str ());
	// The program goes on when we quit.
	SDL_SetBooleanProperty (props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
	SDL_Process * process = SDL_CreateProcessWithProperties (props);
	SDL_DestroyProperties (props);
	if (! process) {
		if (error) *error = SDL_GetError ();
		return false;
	}
	SDL_DestroyProcess (process);
	return true;
}

// A file URL, with the characters that a URL can't have encoded.
static std::string fileURL (const std::string & path) {
	static const char hex[] = "0123456789ABCDEF";
	std::string url = "file://";
#if defined(_WIN32)
	url += '/';
#endif
	for (size_t i = 0; i < path.size (); i ++) {
		unsigned char c = path[i];
		if (c == '\\') c = '/';
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr ("/-_.~:", c)) {
			url += (char) c;
		} else {
			url += '%';
			url += hex[c >> 4];
			url += hex[c & 15];
		}
	}
	return url;
}

bool PlatformSDL::openWithSystem (const std::string & pathOrURL) {
	std::string url = pathOrURL.find ("://") == std::string::npos ? fileURL (pathOrURL) : pathOrURL;
	if (SDL_OpenURL (url.c_str ())) return true;
	Log::get ().add (LogLine::LOG_ERROR, "Can't open %s: %s\n", url.c_str (), SDL_GetError ());
	return false;
}

std::string PlatformSDL::settingsFolder () {
	char * path = SDL_GetPrefPath ("OpenSLUDGE", "Dev Kit");
	std::string folder = path ? path : "";
	SDL_free (path);
	return folder;
}

std::string PlatformSDL::cacheFolder () {
	std::string folder;
#if defined(__APPLE__)
	const char * home = SDL_getenv ("HOME");
	if (home) folder = std::string (home) + "/Library/Caches/OpenSLUDGE/";
#elif defined(_WIN32)
	const char * local = SDL_getenv ("LOCALAPPDATA");
	if (local) folder = std::string (local) + "\\OpenSLUDGE\\Cache\\";
#else
	const char * cache = SDL_getenv ("XDG_CACHE_HOME");
	const char * home = SDL_getenv ("HOME");
	if (cache && *cache) folder = std::string (cache) + "/opensludge/";
	else if (home) folder = std::string (home) + "/.cache/opensludge/";
#endif
	if (folder.empty ()) return settingsFolder ();
	SDL_CreateDirectory (folder.c_str ());
	return folder;
}

std::string PlatformSDL::programFolder () {
	const char * base = SDL_GetBasePath ();
	return base ? base : "";
}

} // namespace devkit
