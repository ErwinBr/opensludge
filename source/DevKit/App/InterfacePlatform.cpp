/*
 * The functions of Common/interface.h, which the models of the Dev Kit
 * call, through the Platform of the program. They are for the main thread.
 */

#include <string>
#include <vector>

#include "interface.h"
#include "InterfacePlatform.h"
#include "Log.h"
#include "Platform.h"

namespace devkit {

static Platform * interfacePlatform = NULL;

void setInterfacePlatform (Platform * platform) {
	interfacePlatform = platform;
}

} // namespace devkit

using namespace devkit;

const char * getTempDir () {
	static std::string folder;
	if (interfacePlatform) folder = interfacePlatform->cacheFolder ();
	return folder.c_str ();
}

bool askAQuestion (const char * head, const char * msg) {
	if (! interfacePlatform) return false;
	std::vector<std::string> buttons;
	buttons.push_back ("Yes");
	buttons.push_back ("No");
	return interfacePlatform->ask (head, msg, buttons) == 0;
}

bool errorBox (const char * head, const char * msg) {
	Log::get ().add (LogLine::LOG_ERROR, "%s: %s\n", head, msg);
	if (interfacePlatform) interfacePlatform->showMessage (Platform::MESSAGE_ERROR, head, msg);
	return false;
}
