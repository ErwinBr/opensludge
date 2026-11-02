#include <string>

#include "imgui.h"

#include "BuildPanel.h"
#include "CompileJob.h"
#include "ProjectDocument.h"
#include "WorkspaceActions.h"

namespace devkit {

BuildPanel::BuildPanel (WorkspaceActions & a)
	: actions (a), showErrors (true), showWarnings (true), showOutput (true), selected (-1) {}

static const char * typeName (BuildMessage::Type type) {
	switch (type) {
		case BuildMessage::WARNING: return "Warning";
		case BuildMessage::ERROR: return "Error";
		case BuildMessage::SYSTEM: return "System";
		case BuildMessage::INTERNAL: return "Internal";
		default: return "Output";
	}
}

static ImVec4 typeColour (BuildMessage::Type type) {
	switch (type) {
		case BuildMessage::WARNING: return ImVec4 (1.0f, 0.8f, 0.3f, 1.0f);
		case BuildMessage::OUTPUT: return ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled);
		default: return ImVec4 (1.0f, 0.45f, 0.4f, 1.0f);
	}
}

void BuildPanel::draw (CompileJob * job, ProjectDocument * project) {
	ImGui::BeginDisabled (! project || actions.compiling ());
	if (ImGui::Button ("Compile")) actions.compile ();
	ImGui::EndDisabled ();
	ImGui::SameLine ();
	if (job && job->running ()) {
		if (ImGui::Button ("Cancel")) job->cancel ();
	} else {
		ImGui::BeginDisabled (! project);
		if (ImGui::Button ("Run Game")) actions.runGame ();
		ImGui::EndDisabled ();
	}

	if (! job) {
		ImGui::Spacing ();
		ImGui::TextDisabled (project ? "Compile the project to see its errors and warnings here."
			: "Open a project to compile it.");
		return;
	}

	// The state, and the two progress bars of the compiler: of the whole
	// compile, and of its current step.
	ImGui::SameLine ();
	if (job->running ()) {
		ImGui::Text ("%s %s %s", job->task.c_str (), job->file.c_str (), job->item.c_str ());
	} else if (job->cancelled) {
		ImGui::TextUnformatted ("Cancelled.");
	} else if (job->succeeded) {
		ImGui::TextColored (ImVec4 (0.5f, 0.9f, 0.5f, 1.0f), "Compiled, with %d warnings.", job->warningCount);
		ImGui::SetItemTooltip ("%s", job->gameFile.c_str ());
	} else {
		ImGui::TextColored (typeColour (BuildMessage::ERROR), "Failed, with %d errors and %d warnings.", job->errorCount, job->warningCount);
	}
	if (job->running ()) {
		float half = (ImGui::GetContentRegionAvail ().x - ImGui::GetStyle ().ItemSpacing.x) * 0.5f;
		ImGui::ProgressBar ((float) job->progress[0], ImVec2 (half, 0));
		ImGui::SameLine ();
		ImGui::ProgressBar ((float) job->progress[1], ImVec2 (half, 0));
	}
	ImGui::TextDisabled ("%d functions, %d object types, %d globals, %d strings, %d resources",
		job->functions, job->objectTypes, job->globals, job->strings, job->resources);

	// Text that isn't UTF-8 and names in another case, the Dev Kit fixes.
	bool fixable = false;
	for (size_t i = 0; i < job->messages.size (); i ++) {
		const std::string & text = job->messages[i].text;
		if (text.find ("not UTF-8") != std::string::npos || text.find ("another case") != std::string::npos) fixable = true;
	}
	if (fixable && ! job->running ()) {
		if (ImGui::Button ("Check Files...")) actions.checkFiles ();
		ImGui::SameLine ();
		ImGui::TextWrapped ("Some files are not UTF-8, or name files in another case than they have on disk. "
			"The Dev Kit can fix that.");
	}

	ImGui::Checkbox ("Errors", &showErrors);
	ImGui::SameLine ();
	ImGui::Checkbox ("Warnings", &showWarnings);
	ImGui::SameLine ();
	ImGui::Checkbox ("Other output", &showOutput);
	ImGui::SameLine ();
	bool copy = ImGui::Button ("Copy");
	ImGui::SetItemTooltip ("Copy the messages that we show");
	std::string copied;

	ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable
		| ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
	if (ImGui::BeginTable ("messages", 4, flags)) {
		ImGui::TableSetupScrollFreeze (0, 1);
		ImGui::TableSetupColumn ("Type", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize () * 5);
		ImGui::TableSetupColumn ("File", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn ("Line", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize () * 3);
		ImGui::TableSetupColumn ("Message", ImGuiTableColumnFlags_WidthStretch, 3.0f);
		ImGui::TableHeadersRow ();
		for (int i = 0; i < (int) job->messages.size (); i ++) {
			const BuildMessage & m = job->messages[i];
			bool shown = m.type == BuildMessage::WARNING ? showWarnings
				: m.type == BuildMessage::OUTPUT ? showOutput : showErrors;
			if (! shown) continue;
			if (copy) copied += m.fullText + "\n";
			ImGui::TableNextRow ();
			ImGui::TableNextColumn ();
			ImGui::PushID (i);
			// The whole row selects, and a double click opens the file.
			if (ImGui::Selectable ("##row", selected == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
				selected = i;
				if (ImGui::IsMouseDoubleClicked (ImGuiMouseButton_Left) && project && ! m.file.empty ()) {
					actions.openFile (project->fullPath (m.file), m.line);
				}
			}
			if (! m.fullText.empty ()) ImGui::SetItemTooltip ("%s", m.fullText.c_str ());
			ImGui::SameLine ();
			ImGui::TextColored (typeColour (m.type), "%s", typeName (m.type));
			ImGui::TableNextColumn ();
			ImGui::TextUnformatted (m.file.c_str ());
			ImGui::TableNextColumn ();
			if (m.line) ImGui::Text ("%d", m.line);
			ImGui::TableNextColumn ();
			ImGui::TextUnformatted (m.text.c_str ());
			ImGui::PopID ();
		}
		// Follow new messages while compiling.
		if (job->running () && ImGui::GetScrollY () >= ImGui::GetScrollMaxY ()) ImGui::SetScrollHereY (1.0f);
		ImGui::EndTable ();
	}
	if (copy) ImGui::SetClipboardText (copied.c_str ());
}

} // namespace devkit
