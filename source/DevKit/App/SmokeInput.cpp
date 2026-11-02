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
		else if (s.what == "key" || s.what == "save") ok = ! s.text.empty ();
		else ok = s.what == "down" || s.what == "up";
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
	if (waiting > 0) {
		waiting --;
		return true;
	}
	if (! running ()) return true;
	const Step & s = steps[next ++];
	if (s.what == "move") {
		io.AddMousePosEvent (s.x, s.y);
	} else if (s.what == "down" || s.what == "up") {
		io.AddMouseButtonEvent (ImGuiMouseButton_Left, s.what == "down");
	} else if (s.what == "wait") {
		waiting = s.frames - 1;
	} else if (s.what == "key") {
		ImGuiKey key = keyNamed (s.text);
		if (key == ImGuiKey_None) {
			Log::get ().add (LogLine::LOG_ERROR, "Smoke input: there is no key %s\n", s.text.c_str ());
			return false;
		}
		io.AddKeyEvent (key, true);
		keyUp = key;
	} else if (s.what == "save") {
		Document * document = workspace.documents ().active ();
		std::string error;
		if (! document || ! document->save (s.text, &error)) {
			Log::get ().add (LogLine::LOG_ERROR, "Smoke input: can't save %s: %s\n", s.text.c_str (),
				document ? error.c_str () : "there is no document");
			return false;
		}
		Log::get ().add (LogLine::LOG_INFO, "Saved %s as %s\n", document->displayName ().c_str (), s.text.c_str ());
	}
	return true;
}

} // namespace devkit
