#include <stdio.h>

#include <algorithm>

#include "imgui.h"
#include "imgui_stdlib.h"

#include "Log.h"
#include "Platform.h"
#include "TranslationView.h"
#include "WorkspaceActions.h"

namespace devkit {

// How long a notice stays, in seconds.
static const double NOTICE_SECONDS = 8.0;
// The merge keys of the language fields.
static const int ID_KEY = 1, NAME_KEY = 2;

static const char * const filterNames[] = {
	"All strings", "Strings to translate", "Translated strings", "Strings that need no translation"
};

TranslationView::TranslationView (TranslationDocument * t, WorkspaceActions & a)
	: translation (t), actions (a), filter (SHOW_ALL), sortColumn (0), sortAscending (true), selected (-1),
	  editRow (-1), focusRow (-1), chosenProject (new std::string), noticeUntil (0) {}

void TranslationView::say (const std::string & text) {
	notice = text;
	noticeUntil = ImGui::GetTime () + NOTICE_SECONDS;
}

void TranslationView::loadStrings (const std::string & projectFile) {
	StringsUpdate update = translation->loadStrings (projectFile);
	for (size_t i = 0; i < update.problems.size (); i ++) {
		Log::get ().add (LogLine::LOG_WARNING, "%s\n", update.problems[i].c_str ());
	}
	if (! update.loaded) {
		say (update.problems.empty () ? "Can't read the project." : update.problems[0]);
		return;
	}
	char text[200];
	if (! update.added && ! update.removed) {
		snprintf (text, sizeof (text), "The translation has every string of %s.", fileNameOf (projectFile).c_str ());
	} else {
		snprintf (text, sizeof (text), "%d new string%s, and %d that the project no longer has removed.",
			update.added, update.added == 1 ? "" : "s", update.removed);
	}
	std::string message = text;
	if (! update.problems.empty ()) message += " Some scripts couldn't be read; see the Log.";
	Log::get ().add (LogLine::LOG_INFO, "%s: %s\n", translation->displayName ().c_str (), message.c_str ());
	say (message);
}

void TranslationView::chooseProject () {
	std::vector<FileFilter> filters;
	FileFilter projects = { "SLUDGE projects", "slp" };
	filters.push_back (projects);
	std::shared_ptr<std::string> chosen = chosenProject;
	actions.platform ().openFileDialog ("Load the strings of a project", filters, folderOf (translation->path ()), false,
			[chosen] (const std::vector<std::string> & files) {
		if (! files.empty ()) *chosen = files[0];
	});
}

void TranslationView::updateShown () {
	const std::vector<TranslationLine> & lines = translation->lines ();
	shown.clear ();
	for (int i = 0; i < (int) lines.size (); i ++) {
		if (translationLineShown (lines[i], (TranslationFilter) filter, search)) shown.push_back (i);
	}
	if (sortColumn == 1 || sortColumn == 3) {
		bool original = sortColumn == 1;
		std::stable_sort (shown.begin (), shown.end (), [&lines, original] (int a, int b) {
			const std::string & x = original ? lines[a].original : lines[a].translation;
			const std::string & y = original ? lines[b].original : lines[b].translation;
			return x < y;
		});
	}
	if (! sortAscending) std::reverse (shown.begin (), shown.end ());
}

void TranslationView::drawToolbar () {
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 14);
	ImGui::Combo ("##filter", &filter, filterNames, 4);
	ImGui::SameLine ();
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 14);
	ImGui::InputTextWithHint ("##search", "Find text", &search);
	if (ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiKey_F)) ImGui::SetKeyboardFocusHere (-1);
}

void TranslationView::drawTable (float height) {
	ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable
		| ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingStretchProp;
	if (! ImGui::BeginTable ("strings", 4, flags, ImVec2 (0, height))) return;
	ImGui::TableSetupScrollFreeze (0, 1);
	ImGui::TableSetupColumn ("#", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, ImGui::GetFontSize () * 3);
	ImGui::TableSetupColumn ("Original", ImGuiTableColumnFlags_WidthStretch, 1.0f);
	ImGui::TableSetupColumn ("Translate", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, ImGui::GetFontSize () * 5.5f);
	ImGui::TableSetupColumn ("Translation", ImGuiTableColumnFlags_WidthStretch, 1.0f);
	ImGui::TableHeadersRow ();
	if (ImGuiTableSortSpecs * specs = ImGui::TableGetSortSpecs ()) {
		if (specs->SpecsCount > 0) {
			sortColumn = specs->Specs[0].ColumnIndex;
			sortAscending = specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
		}
		specs->SpecsDirty = false;
	}
	updateShown ();

	const std::vector<TranslationLine> & lines = translation->lines ();
	int focusAt = -1;
	for (size_t s = 0; s < shown.size (); s ++) {
		if (shown[s] == focusRow) focusAt = (int) s;
	}
	ImGuiListClipper clipper;
	clipper.Begin ((int) shown.size ());
	if (focusAt >= 0) clipper.IncludeItemByIndex (focusAt);
	int enterAt = -1;
	while (clipper.Step ()) {
		for (int s = clipper.DisplayStart; s < clipper.DisplayEnd; s ++) {
			int index = shown[s];
			const TranslationLine & line = lines[index];
			ImGui::PushID (index);
			ImGui::TableNextRow ();

			ImGui::TableNextColumn ();
			char number[16];
			snprintf (number, sizeof (number), "%d", index);
			if (ImGui::Selectable (number, selected == index, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
				selected = index;
			}

			ImGui::TableNextColumn ();
			if (line.type == TranslationLine::NOT_NEEDED) ImGui::TextDisabled ("%s", line.original.c_str ());
			else ImGui::TextUnformatted (line.original.c_str ());
			if (ImGui::IsItemHovered () && ImGui::CalcTextSize (line.original.c_str ()).x > ImGui::GetColumnWidth ()) {
				ImGui::SetTooltip ("%s", line.original.c_str ());
			}

			ImGui::TableNextColumn ();
			bool needs = line.type != TranslationLine::NOT_NEEDED;
			if (ImGui::Checkbox ("##translate", &needs)) {
				translation->setNeedsTranslation (index, needs);
				selected = index;
			}
			ImGui::SetItemTooltip (needs ? "The string needs a translation" : "The string needs no translation, such as a name");

			ImGui::TableNextColumn ();
			ImGui::SetNextItemWidth (-1);
			// The focus scrolls the table when the row isn't in view.
			if (focusRow == index) {
				ImGui::SetKeyboardFocusHere ();
				focusRow = -1;
			}
			std::string copy;
			std::string & text = editRow == index ? editBuffer : (copy = line.translation);
			const char * hint = line.type == TranslationLine::NOT_NEEDED ? "" : "To translate";
			bool enter = ImGui::InputTextWithHint ("##translation", hint, &text, ImGuiInputTextFlags_EnterReturnsTrue);
			if (ImGui::IsItemActivated ()) {
				editRow = index;
				editBuffer = line.translation;
				selected = index;
			}
			if (enter || ImGui::IsItemDeactivatedAfterEdit ()) {
				if (line.type == TranslationLine::NOT_NEEDED && ! text.empty ()) say ("That string needs a translation now.");
				translation->setTranslation (index, text);
			}
			if (enter) enterAt = s;
			if (ImGui::IsItemDeactivated ()) editRow = -1;
			ImGui::PopID ();
		}
	}
	ImGui::EndTable ();
	// Enter goes on to the next string that is shown.
	if (enterAt >= 0 && enterAt + 1 < (int) shown.size ()) {
		focusRow = shown[enterAt + 1];
		selected = focusRow;
	}
}

void TranslationView::drawPreview () {
	if (selected < 0 || selected >= (int) translation->lines ().size ()) {
		ImGui::TextDisabled ("Select a string to see all of it here.");
		return;
	}
	const TranslationLine & line = translation->lines ()[selected];
	if (ImGui::BeginTable ("preview", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
		ImGui::TableNextRow ();
		ImGui::TableNextColumn ();
		ImGui::TextDisabled ("Original");
		ImGui::TextWrapped ("%s", line.original.c_str ());
		ImGui::TableNextColumn ();
		ImGui::TextDisabled ("Translation");
		if (line.type == TranslationLine::NOT_NEEDED) ImGui::TextDisabled ("Needs none");
		else if (line.type == TranslationLine::MISSING) ImGui::TextColored (ImVec4 (1.0f, 0.8f, 0.3f, 1.0f), "Not yet");
		else ImGui::TextWrapped ("%s", line.translation.c_str ());
		ImGui::EndTable ();
	}
}

void TranslationView::draw () {
	if (! chosenProject->empty ()) {
		std::string project = *chosenProject;
		chosenProject->clear ();
		loadStrings (project);
	}
	if (selected >= (int) translation->lines ().size ()) selected = -1;

	drawToolbar ();
	if (translation->lines ().empty ()) {
		ImGui::Spacing ();
		ImGui::TextDisabled ("The translation has no strings yet. Load them from a project in the Inspector.");
		return;
	}
	float previewHeight = ImGui::GetTextLineHeightWithSpacing () * 5;
	float statusHeight = ImGui::GetTextLineHeightWithSpacing ();
	drawTable (ImGui::GetContentRegionAvail ().y - previewHeight - statusHeight - ImGui::GetStyle ().ItemSpacing.y);
	if (ImGui::BeginChild ("preview", ImVec2 (0, previewHeight))) drawPreview ();
	ImGui::EndChild ();
	if (! notice.empty () && ImGui::GetTime () < noticeUntil) {
		ImGui::TextColored (ImVec4 (1.0f, 0.8f, 0.3f, 1.0f), "%s", notice.c_str ());
	} else {
		ImGui::TextDisabled ("%d strings: %d to translate, %d translated, %d that need none.    %d shown",
			(int) translation->lines ().size (), translation->count (TranslationLine::MISSING),
			translation->count (TranslationLine::TRANSLATED), translation->count (TranslationLine::NOT_NEEDED), (int) shown.size ());
	}
}

static void hintText (const char * text) {
	ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
	ImGui::TextWrapped ("%s", text);
	ImGui::PopStyleColor ();
}

void TranslationView::drawInspector () {
	ImGui::SeparatorText ("Language");
	Language language = translation->language ();
	int id = (int) language.id;
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 8);
	if (ImGui::InputInt ("ID", &id)) {
		language.id = id < 0 ? 0 : (unsigned int) id;
		translation->setLanguage (language, ID_KEY);
	}
	if (ImGui::IsItemDeactivated ()) translation->commands ().stopMerging ();
	ImGui::SetNextItemWidth (ImGui::GetFontSize () * 12);
	if (ImGui::InputText ("Name", &language.name)) translation->setLanguage (language, NAME_KEY);
	if (ImGui::IsItemDeactivated ()) translation->commands ().stopMerging ();
	hintText ("The game chooses a language by its ID. The name is what the game shows for it.");

	ImGui::SeparatorText ("Strings");
	ImGui::Text ("%d strings", (int) translation->lines ().size ());
	ImGui::Text ("%d to translate", translation->count (TranslationLine::MISSING));
	ImGui::Text ("%d translated", translation->count (TranslationLine::TRANSLATED));
	ImGui::Text ("%d that need none", translation->count (TranslationLine::NOT_NEEDED));
	std::string project = actions.projectFile ();
	if (! project.empty ()) {
		std::string label = "Load Strings from " + fileNameOf (project);
		if (ImGui::Button (label.c_str ())) loadStrings (project);
		if (ImGui::Button ("From Another Project...")) chooseProject ();
	} else {
		if (ImGui::Button ("Load Strings from a Project...")) chooseProject ();
	}
	hintText ("Adds the strings of the scripts, the window name and the quit message that the translation doesn't "
		"have yet, and removes those that the project no longer has. Translations stay.");
}

} // namespace devkit
