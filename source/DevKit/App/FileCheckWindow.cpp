#include <map>
#include <string>

#include "imgui.h"

#include "FileCheckWindow.h"
#include "Log.h"
#include "Platform.h"
#include "ProjectDocument.h"
#include "WorkspaceActions.h"

namespace devkit {

FileCheckWindow::FileCheckWindow (WorkspaceActions & a)
	: actions (a), visible (false), codePage (CODEPAGE_WINDOWS_1252) {}

void FileCheckWindow::open (ProjectDocument * project) {
	visible = true;
	check (project);
}

void FileCheckWindow::check (ProjectDocument * project) {
	problems = project ? checkProject (*project) : std::vector<FileProblem> ();
	// We choose every problem that we can fix.
	chosen.assign (problems.size (), true);
	for (size_t i = 0; i < problems.size (); i ++) {
		if (problems[i].kind == FileProblem::MISSING) chosen[i] = false;
	}
}

void FileCheckWindow::fix (ProjectDocument * project) {
	int changes = 0;
	for (size_t i = 0; i < problems.size (); i ++) {
		if (chosen[i]) changes ++;
	}
	if (! changes) return;
	std::vector<std::string> buttons;
	buttons.push_back ("Fix");
	buttons.push_back ("Cancel");
	if (actions.platform ().ask ("Fix the files?",
			"This changes the scripts on disk, which Undo can't take back. "
			"Of each file that we convert to UTF-8, we keep the old one, with .orig after its name.", buttons) != 0) return;

	std::map<std::string, std::vector<FileProblem> > namesByScript;
	std::string error;
	for (size_t i = 0; i < problems.size (); i ++) {
		if (! chosen[i]) continue;
		const FileProblem & p = problems[i];
		if (p.kind == FileProblem::NOT_UTF8) {
			if (convertFileToUTF8 (project->fullPath (p.file), codePage, &error)) {
				Log::get ().add (LogLine::LOG_INFO, "Converted %s from %s to UTF-8\n", p.file.c_str (), codePageName (codePage));
			} else {
				Log::get ().add (LogLine::LOG_ERROR, "Can't convert %s: %s\n", p.file.c_str (), error.c_str ());
			}
		} else if (p.kind == FileProblem::WRONG_CASE && p.inProject) {
			const std::vector<std::string> & files = project->files ();
			for (size_t f = 0; f < files.size (); f ++) {
				if (files[f] == p.name) project->renameFile ((int) f, p.onDisk);
			}
		} else if (p.kind == FileProblem::WRONG_CASE) {
			namesByScript[p.file].push_back (p);
		}
	}
	for (std::map<std::string, std::vector<FileProblem> >::iterator i = namesByScript.begin (); i != namesByScript.end (); ++ i) {
		if (fixNamesInScript (project->fullPath (i->first), i->second, &error)) {
			Log::get ().add (LogLine::LOG_INFO, "Wrote %d names in %s as they are on disk\n", (int) i->second.size (), i->first.c_str ());
		} else {
			Log::get ().add (LogLine::LOG_ERROR, "Can't change %s: %s\n", i->first.c_str (), error.c_str ());
		}
	}
	check (project);
}

static const char * problemName (FileProblem::Kind kind) {
	switch (kind) {
		case FileProblem::NOT_UTF8: return "Not UTF-8";
		case FileProblem::WRONG_CASE: return "Other case";
		default: return "Missing";
	}
}

void FileCheckWindow::draw (ProjectDocument * project) {
	if (! visible) return;
	ImGui::SetNextWindowSize (ImVec2 (ImGui::GetFontSize () * 52, ImGui::GetFontSize () * 26), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos (ImGui::GetMainViewport ()->GetCenter (), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));
	if (! ImGui::Begin ("Check Files", &visible, ImGuiWindowFlags_NoDocking)) {
		ImGui::End ();
		return;
	}
	if (! project) {
		ImGui::TextDisabled ("Open a project to check its files.");
		ImGui::End ();
		return;
	}

	ImGui::TextWrapped ("A project builds on every system when its scripts are UTF-8 and name their files in "
		"the case that the files have on disk. Windows and macOS forgive another case; Linux doesn't.");
	ImGui::Spacing ();
	if (problems.empty ()) {
		ImGui::TextColored (ImVec4 (0.5f, 0.9f, 0.5f, 1.0f), "The files of %s are fine.", project->displayName ().c_str ());
	} else {
		ImGui::Text ("%d problems:", (int) problems.size ());
	}

	bool anyText = false;
	for (size_t i = 0; i < problems.size (); i ++) {
		if (problems[i].kind == FileProblem::NOT_UTF8) anyText = true;
	}
	if (anyText) {
		ImGui::SetNextItemWidth (ImGui::GetFontSize () * 18);
		if (ImGui::BeginCombo ("Code page of the old text", codePageName (codePage))) {
			const CodePage pages[] = { CODEPAGE_WINDOWS_1252, CODEPAGE_WINDOWS_1250 };
			for (int p = 0; p < 2; p ++) {
				if (ImGui::Selectable (codePageName (pages[p]), codePage == pages[p])) codePage = pages[p];
			}
			ImGui::EndCombo ();
		}
		ImGui::SetItemTooltip ("Western European for English, French, German and the like; Central European for "
			"Czech, Polish, Slovak and the like. The lines below show the text as it will be.");
	}

	float buttons = ImGui::GetFrameHeightWithSpacing () + ImGui::GetStyle ().ItemSpacing.y;
	ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable
		| ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
	if (ImGui::BeginTable ("problems", 5, flags, ImVec2 (0, - buttons))) {
		ImGui::TableSetupScrollFreeze (0, 1);
		ImGui::TableSetupColumn ("Fix", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight ());
		ImGui::TableSetupColumn ("Problem", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize () * 6);
		ImGui::TableSetupColumn ("File", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn ("Line", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize () * 3);
		ImGui::TableSetupColumn ("What", ImGuiTableColumnFlags_WidthStretch, 3.0f);
		ImGui::TableHeadersRow ();
		for (size_t i = 0; i < problems.size (); i ++) {
			const FileProblem & p = problems[i];
			ImGui::PushID ((int) i);
			ImGui::TableNextRow ();
			ImGui::TableNextColumn ();
			bool c = chosen[i];
			ImGui::BeginDisabled (p.kind == FileProblem::MISSING);
			if (ImGui::Checkbox ("##fix", &c)) chosen[i] = c;
			ImGui::EndDisabled ();
			ImGui::TableNextColumn ();
			ImGui::TextUnformatted (problemName (p.kind));
			ImGui::TableNextColumn ();
			// A double click opens the file at the line.
			if (ImGui::Selectable (p.file.c_str (), false, ImGuiSelectableFlags_AllowDoubleClick)
					&& ImGui::IsMouseDoubleClicked (ImGuiMouseButton_Left) && ! p.inProject) {
				actions.openFile (project->fullPath (p.file), p.line);
			}
			ImGui::TableNextColumn ();
			if (p.line) ImGui::Text ("%d", p.line);
			ImGui::TableNextColumn ();
			if (p.kind == FileProblem::NOT_UTF8) {
				ImGui::TextUnformatted (toUTF8 (p.sample, codePage).c_str ());
			} else if (p.kind == FileProblem::WRONG_CASE) {
				ImGui::Text ("'%s' is '%s' on disk", p.name.c_str (), p.onDisk.c_str ());
			} else {
				ImGui::Text ("There is no '%s'", p.name.c_str ());
			}
			ImGui::PopID ();
		}
		ImGui::EndTable ();
	}

	bool any = false;
	for (size_t i = 0; i < chosen.size (); i ++) any = any || chosen[i];
	ImGui::BeginDisabled (! any);
	if (ImGui::Button ("Fix the Chosen Problems")) fix (project);
	ImGui::EndDisabled ();
	ImGui::SameLine ();
	if (ImGui::Button ("Check Again")) check (project);
	ImGui::SameLine ();
	if (ImGui::Button ("Close")) visible = false;
	ImGui::End ();
}

} // namespace devkit
