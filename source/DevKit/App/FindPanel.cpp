#include <stdio.h>

#include "imgui.h"
#include "imgui_stdlib.h"

#include "Document.h"
#include "FindPanel.h"
#include "ProjectDocument.h"
#include "WorkspaceActions.h"

namespace devkit {

// At most this many places, which is more than anyone reads.
static const size_t MOST_HITS = 2000;

FindPanel::FindPanel (WorkspaceActions & a)
	: actions (a), more (false), focusField (false), searchedOnce (false), selected (-1) {}

void FindPanel::focus (const std::string & text) {
	if (! text.empty () && text.find ('\n') == std::string::npos) wanted = text;
	focusField = true;
}

void FindPanel::search (ProjectDocument * project, OpenText openText) {
	results.clear ();
	unread.clear ();
	more = false;
	selected = -1;
	searched = wanted;
	searchedOnce = true;
	if (! project || wanted.empty ()) return;
	std::vector<std::string> files;
	const std::vector<std::string> & names = project->files ();
	for (size_t f = 0; f < names.size (); f ++) {
		std::string extension = extensionOf (names[f]);
		if (extension == "slu" || extension == "sld") files.push_back (project->fullPath (names[f]));
	}
	results = searchFiles (files, openText, wanted, options, MOST_HITS, &more, &unread);
}

void FindPanel::draw (ProjectDocument * project, OpenText openText) {
	if (! project) {
		ImGui::TextDisabled ("Open a project to find text in its scripts.");
		return;
	}
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 18);
	if (focusField) {
		ImGui::SetKeyboardFocusHere ();
		focusField = false;
	}
	bool enter = ImGui::InputTextWithHint ("##wanted", "Find in the scripts of the project", &wanted, ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::SameLine ();
	bool changed = ImGui::Checkbox ("Match case", &options.matchCase);
	ImGui::SameLine ();
	changed |= ImGui::Checkbox ("Whole words", &options.wholeWord);
	ImGui::SameLine ();
	if (ImGui::Button ("Find") || enter || (changed && searchedOnce)) search (project, openText);

	if (searchedOnce && ! searched.empty ()) {
		ImGui::SameLine ();
		if (results.empty ()) {
			ImGui::TextColored (ImVec4 (1.0f, 0.5f, 0.4f, 1.0f), "\"%s\" isn't in the scripts.", searched.c_str ());
		} else {
			// In how many files.
			int files = 0;
			for (size_t i = 0; i < results.size (); i ++) {
				if (i == 0 || results[i].file != results[i - 1].file) files ++;
			}
			ImGui::TextDisabled ("%s%d found in %d script%s", more ? "The first " : "", (int) results.size (), files, files == 1 ? "" : "s");
		}
	}
	for (size_t u = 0; u < unread.size (); u ++) {
		ImGui::TextColored (ImVec4 (1.0f, 0.8f, 0.3f, 1.0f), "Can't read %s: it isn't UTF-8 (Project \xE2\x86\x92 Check Files converts it).",
			project->relativePath (unread[u]).c_str ());
	}
	if (results.empty ()) return;

	ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
	if (! ImGui::BeginTable ("hits", 3, flags, ImGui::GetContentRegionAvail ())) return;
	ImGui::TableSetupScrollFreeze (0, 1);
	ImGui::TableSetupColumn ("Script", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize () * 12);
	ImGui::TableSetupColumn ("Line", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize () * 3);
	ImGui::TableSetupColumn ("Text", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableHeadersRow ();
	ImGuiListClipper clipper;
	clipper.Begin ((int) results.size ());
	while (clipper.Step ()) {
		for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i ++) {
			const SearchHit & hit = results[i];
			ImGui::PushID (i);
			ImGui::TableNextRow ();
			ImGui::TableNextColumn ();
			std::string name = project->relativePath (hit.file);
			if (ImGui::Selectable (name.c_str (), selected == i, ImGuiSelectableFlags_SpanAllColumns)) {
				selected = i;
				actions.showInScript (hit.file, hit.at.line, hit.at.column, hit.length);
			}
			ImGui::TableNextColumn ();
			ImGui::Text ("%d", hit.at.line + 1);
			ImGui::TableNextColumn ();
			// The line without its indent, with the text that was found
			// in the colour of the selection's.
			size_t start = hit.text.find_first_not_of (" \t");
			if (start == std::string::npos) start = 0;
			if ((int) start > hit.at.column) start = hit.at.column;
			std::string before = hit.text.substr (start, hit.at.column - start);
			std::string match = hit.text.substr (hit.at.column, hit.length);
			std::string after = hit.text.substr (hit.at.column + hit.length);
			ImGui::TextUnformatted (before.c_str ());
			ImGui::SameLine (0, 0);
			ImGui::TextColored (ImVec4 (1.0f, 0.75f, 0.35f, 1.0f), "%s", match.c_str ());
			ImGui::SameLine (0, 0);
			ImGui::TextUnformatted (after.c_str ());
			ImGui::PopID ();
		}
	}
	ImGui::EndTable ();
}

} // namespace devkit
