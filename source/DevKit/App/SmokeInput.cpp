#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"

#include "Document.h"
#include "DocumentManager.h"
#include "Log.h"
#include "SmokeInput.h"
#include "Workspace.h"

namespace devkit {

bool SmokeInput::read (const std::string & file, std::string * error) {
	FILE * fp = fopen (file.c_str (), "rb");
	if (! fp) {
		*error = "Can't open " + file;
		return false;
	}
	char line[1024];
	int number = 0;
	while (fgets (line, sizeof (line), fp)) {
		number ++;
		size_t length = strlen (line);
		while (length && (line[length - 1] == '\n' || line[length - 1] == '\r' || line[length - 1] == ' ')) line[-- length] = 0;
		if (! length || line[0] == '#') continue;
		Step s;
		s.x = s.y = 0;
		s.frames = 1;
		char word[32], rest[1000];
		rest[0] = 0;
		if (sscanf (line, "%31s %999[^\n]", word, rest) < 1) continue;
		s.what = word;
		s.text = rest;
		bool ok = true;
		if (s.what == "move") ok = sscanf (rest, "%f %f", &s.x, &s.y) == 2;
		else if (s.what == "wait") ok = sscanf (rest, "%d", &s.frames) == 1;
		else if (s.what == "documents") ok = sscanf (rest, "%d", &s.frames) == 1;
		else if (s.what == "key" || s.what == "save" || s.what == "new" || s.what == "open" || s.what == "type") ok = ! s.text.empty ();
		else if (s.what == "down") ok = s.text.empty () || s.text == "ctrl";
		else ok = s.what == "up" || s.what == "close";
		if (! ok) {
			char where[64];
			snprintf (where, sizeof (where), "%d", number);
			*error = file + ", line " + where + ": can't read \"" + line + "\"";
			fclose (fp);
			return false;
		}
		steps.push_back (s);
	}
	fclose (fp);
	return true;
}

// The key of the shortcuts that ImGuiMod_Ctrl stands for: on macOS, ImGui
// swaps Cmd (Super) and Ctrl as they come in, so that Ctrl is Cmd there.
static ImGuiKey ctrlKey () {
	return ImGui::GetIO ().ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
}

static ImGuiKey keyNamed (const std::string & name) {
	for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k ++) {
		if (name == ImGui::GetKeyName ((ImGuiKey) k)) return (ImGuiKey) k;
	}
	return ImGuiKey_None;
}

bool SmokeInput::step (Workspace & workspace) {
	ImGuiIO & io = ImGui::GetIO ();
	if (keyUp) {
		io.AddKeyEvent ((ImGuiKey) keyUp, false);
		keyUp = 0;
	}
	if (ctrlUp) {
		io.AddKeyEvent (ctrlKey (), false);
		ctrlUp = false;
	}
	if (waiting > 0) {
		waiting --;
		return true;
	}
	if (! running ()) return true;
	const Step & s = steps[next ++];
	if (s.what == "move") {
		io.AddMousePosEvent (s.x, s.y);
	} else if (s.what == "down") {
		if (s.text == "ctrl") {
			io.AddKeyEvent (ctrlKey (), true);
			ctrlHeld = true;
		}
		io.AddMouseButtonEvent (ImGuiMouseButton_Left, true);
	} else if (s.what == "up") {
		io.AddMouseButtonEvent (ImGuiMouseButton_Left, false);
		if (ctrlHeld) {
			io.AddKeyEvent (ctrlKey (), false);
			ctrlHeld = false;
		}
	} else if (s.what == "wait") {
		waiting = s.frames - 1;
	} else if (s.what == "key") {
		std::string name = s.text;
		bool ctrl = name.compare (0, 5, "ctrl+") == 0;
		if (ctrl) name = name.substr (5);
		ImGuiKey key = keyNamed (name);
		if (key == ImGuiKey_None) {
			Log::get ().add (LogLine::LOG_ERROR, "Smoke input: there is no key %s\n", name.c_str ());
			return false;
		}
		if (ctrl) io.AddKeyEvent (ctrlKey (), true);
		io.AddKeyEvent (key, true);
		keyUp = key;
		ctrlUp = ctrl;
	} else if (s.what == "type") {
		io.AddInputCharactersUTF8 (s.text.c_str ());
	} else if (s.what == "new") {
		const std::vector<DocumentType> & types = workspace.documents ().types ();
		size_t t = 0;
		while (t < types.size () && types[t].name != s.text) t ++;
		if (t == types.size () || ! workspace.documents ().create (types[t])) {
			Log::get ().add (LogLine::LOG_ERROR, "Smoke input: can't make a new %s\n", s.text.c_str ());
			return false;
		}
	} else if (s.what == "open") {
		workspace.openFiles (std::vector<std::string> (1, s.text));
	} else if (s.what == "save") {
		Document * document = workspace.documents ().active ();
		std::string error;
		if (! document || ! document->save (s.text, &error)) {
			Log::get ().add (LogLine::LOG_ERROR, "Smoke input: can't save %s: %s\n", s.text.c_str (),
				document ? error.c_str () : "there is no document");
			return false;
		}
		Log::get ().add (LogLine::LOG_INFO, "Saved %s as %s\n", document->displayName ().c_str (), s.text.c_str ());
	} else if (s.what == "close") {
		workspace.closeActiveTab ();
	} else if (s.what == "documents") {
		int open = (int) workspace.documents ().documents ().size ();
		if (open != s.frames) {
			Log::get ().add (LogLine::LOG_ERROR, "Smoke input: %d documents are open, not %d\n", open, s.frames);
			return false;
		}
	}
	return true;
}

} // namespace devkit
