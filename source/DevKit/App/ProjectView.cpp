#include <algorithm>

#include "imgui.h"
#include "imgui_stdlib.h"

#include "Launch.h"
#include "Platform.h"
#include "ProjectDocument.h"
#include "Properties.h"
#include "ProjectView.h"
#include "WorkspaceActions.h"

namespace devkit {

ProjectView::ProjectView (ProjectDocument * p, WorkspaceActions & a)
	: project (p), actions (a), anchor (-1), resourcesStale (true) {}

Document * ProjectView::document () {
	return project;
}

void ProjectView::addFilesWithDialog () {
	std::vector<FileFilter> filters;
	FileFilter all = { "SLUDGE scripts, definitions and translations", "slu;sld;tra" };
	FileFilter scripts = { "SLUDGE scripts", "slu" };
	FileFilter definitions = { "SLUDGE constant definitions", "sld" };
	FileFilter translations = { "SLUDGE translations", "tra" };
	filters.push_back (all);
	filters.push_back (scripts);
	filters.push_back (definitions);
	filters.push_back (translations);
	ProjectDocument * p = project;
	actions.platform ().openFileDialog ("Add files to the project", filters, p->folder (), true,
			[p] (const std::vector<std::string> & files) {
		if (! files.empty ()) p->addFiles (files);
	});
}

void ProjectView::removeSelected () {
	if (selected.empty ()) return;
	std::vector<std::string> buttons;
	buttons.push_back ("Remove");
	buttons.push_back ("Cancel");
	std::string what = selected.size () == 1 ? project->files ()[*selected.begin ()] : "the selected files";
	if (actions.platform ().ask ("Remove files?", "Remove " + what + " from the project? The files stay on disk.", buttons) != 0) return;
	project->removeFiles (std::vector<int> (selected.begin (), selected.end ()));
}

// A click: with Ctrl (Cmd on macOS), add or take away one; with Shift, a
// range; otherwise only this one.
void ProjectView::select (int index) {
	ImGuiIO & io = ImGui::GetIO ();
	if (io.KeyShift && anchor >= 0) {
		if (! io.KeyCtrl) selected.clear ();
		for (int i = std::min (anchor, index); i <= std::max (anchor, index); i ++) selected.insert (i);
	} else if (io.KeyCtrl) {
		if (selected.count (index)) selected.erase (index);
		else selected.insert (index);
		anchor = index;
	} else {
		selected.clear ();
		selected.insert (index);
		anchor = index;
	}
	resourcesStale = true;
}

void ProjectView::updateResources () {
	resources.clear ();
	for (std::set<int>::iterator i = selected.begin (); i != selected.end (); ++ i) {
		std::vector<std::string> found = project->resourcesOf (project->files ()[*i]);
		for (size_t r = 0; r < found.size (); r ++) {
			if (std::find (resources.begin (), resources.end (), found[r]) == resources.end ()) resources.push_back (found[r]);
		}
	}
	resourcesStale = false;
}

void ProjectView::drawToolbar () {
	if (ImGui::Button ("Add...")) addFilesWithDialog ();
	ImGui::SetItemTooltip ("Add scripts, definitions and translations to the project");
	ImGui::SameLine ();
	ImGui::BeginDisabled (selected.empty ());
	if (ImGui::Button ("Remove")) removeSelected ();
	ImGui::EndDisabled ();
	ImGui::SameLine ();
	ImGui::BeginDisabled (actions.compiling () || project->files ().empty ());
	if (ImGui::Button ("Compile")) actions.compile ();
	ImGui::EndDisabled ();
	ImGui::SameLine ();
	if (ImGui::Button ("Run")) actions.runGame ();
	ImGui::SetItemTooltip ("Run the game that the last compile made");
}

void ProjectView::drawFiles (float height) {
	const std::vector<std::string> & files = project->files ();
	if (files != filesSeen) {
		// The files changed, as with undo, so the indices mean other files.
		selected.clear ();
		anchor = -1;
		filesSeen = files;
		resourcesStale = true;
	}
	if (! ImGui::BeginChild ("files", ImVec2 (0, height), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY)) {
		ImGui::EndChild ();
		return;
	}
	if (files.empty ()) {
		ImGui::TextDisabled ("Add the scripts of the game, or drop them here.");
	}
	int moveFrom = -1, moveTo = -1;
	for (int i = 0; i < (int) files.size (); i ++) {
		ImGui::PushID (i);
		bool isSelected = selected.count (i) != 0;
		if (ImGui::Selectable (files[i].c_str (), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
			select (i);
			if (ImGui::IsMouseDoubleClicked (ImGuiMouseButton_Left)) actions.openFile (project->fullPath (files[i]), 0);
		}
		// Drag a file to put it elsewhere in the order.
		if (ImGui::BeginDragDropSource ()) {
			ImGui::SetDragDropPayload ("PROJECT_FILE", &i, sizeof (i));
			ImGui::TextUnformatted (files[i].c_str ());
			ImGui::EndDragDropSource ();
		}
		if (ImGui::BeginDragDropTarget ()) {
			if (const ImGuiPayload * payload = ImGui::AcceptDragDropPayload ("PROJECT_FILE")) {
				moveFrom = *(const int *) payload->Data;
				moveTo = i;
			}
			ImGui::EndDragDropTarget ();
		}
		if (ImGui::BeginPopupContextItem ()) {
			if (! isSelected) select (i);
			if (ImGui::MenuItem ("Open")) actions.openFile (project->fullPath (files[i]), 0);
			if (ImGui::MenuItem ("Show in Folder")) actions.platform ().openWithSystem (folderOf (project->fullPath (files[i])));
			ImGui::Separator ();
			if (ImGui::MenuItem ("Remove from Project")) removeSelected ();
			ImGui::EndPopup ();
		}
		ImGui::PopID ();
	}
	if (ImGui::IsWindowFocused () && ImGui::Shortcut (ImGuiKey_Delete) ) removeSelected ();
	if (ImGui::IsWindowFocused () && ImGui::Shortcut (ImGuiKey_Backspace)) removeSelected ();
	ImGui::EndChild ();
	if (moveFrom >= 0) project->moveFile (moveFrom, moveTo);
}

void ProjectView::drawResources () {
	if (resourcesStale) updateResources ();
	// The heading, with the button at the right, which a narrow panel can
	// push onto the next line.
	ImGui::Spacing ();
	ImGui::TextUnformatted ("Resources of the selected scripts");
	float button = ImGui::CalcTextSize ("Refresh").x + ImGui::GetStyle ().FramePadding.x * 2;
	float right = ImGui::GetContentRegionAvail ().x - button;
	if (ImGui::GetItemRectSize ().x + ImGui::GetStyle ().ItemSpacing.x < right) ImGui::SameLine (right + ImGui::GetStyle ().WindowPadding.x);
	if (ImGui::SmallButton ("Refresh")) resourcesStale = true;
	ImGui::SetItemTooltip ("Read the scripts again, after changing them");
	if (! ImGui::BeginChild ("resources", ImVec2 (0, 0), ImGuiChildFlags_Borders)) {
		ImGui::EndChild ();
		return;
	}
	if (selected.empty ()) ImGui::TextDisabled ("Select scripts to see the files that they use.");
	else if (resources.empty ()) ImGui::TextDisabled ("The selected scripts use no files.");
	for (size_t i = 0; i < resources.size (); i ++) {
		ImGui::PushID ((int) i);
		if (ImGui::Selectable (resources[i].c_str (), false, ImGuiSelectableFlags_AllowDoubleClick)
				&& ImGui::IsMouseDoubleClicked (ImGuiMouseButton_Left)) {
			actions.openFile (project->fullPath (resources[i]), 0);
		}
		ImGui::SetItemTooltip ("Double-click to open");
		ImGui::PopID ();
	}
	ImGui::EndChild ();
}

void ProjectView::draw () {
	ImGui::TextUnformatted (project->settings ().windowName.c_str ());
	ImGui::SameLine ();
	ImGui::TextDisabled ("(%s)", project->displayName ().c_str ());
	if (ImGui::IsItemClicked ()) actions.revealInspector ();
	drawToolbar ();
	drawFiles (ImGui::GetContentRegionAvail ().y * 0.6f);
	drawResources ();
}

// The settings, edited as a copy: each change becomes a command, and the
// changes of one field while the user types in it merge into one.
void ProjectView::drawInspector () {
	ProjectSettings s = project->settings ();
	bool typing = false, done = false, chosen = false;
	// The merge key of the field that the user is in.
	int keyActive = 0, key = 100;

	auto track = [&] () {
		key ++;
		if (ImGui::IsItemDeactivatedAfterEdit ()) done = true;
		if (ImGui::IsItemActive ()) keyActive = key;
	};
	auto text = [&] (const char * label, std::string * value, const char * tooltip) {
		propertyRow (label, tooltip);
		ImGui::PushID (label);
		if (ImGui::InputText ("##value", value)) typing = true;
		track ();
		ImGui::PopID ();
	};
	auto number = [&] (const char * label, int * value, int low, int high, const char * tooltip) {
		propertyRow (label, tooltip);
		ImGui::PushID (label);
		if (ImGui::InputInt ("##value", value)) {
			*value = std::max (low, std::min (high, *value));
			typing = true;
		}
		track ();
		ImGui::PopID ();
	};
	auto check = [&] (const char * label, bool * value, const char * tooltip) {
		propertyRow (label, tooltip);
		ImGui::PushID (label);
		if (ImGui::Checkbox ("##value", value)) chosen = true;
		ImGui::PopID ();
	};
	// A file of the project, with a button to choose it.
	auto file = [&] (const char * label, std::string ProjectSettings::* field, const char * tooltip) {
		propertyRow (label, tooltip);
		ImGui::PushID (label);
		float button = ImGui::GetFrameHeight ();
		ImGui::SetNextItemWidth (ImGui::GetContentRegionAvail ().x - button - ImGui::GetStyle ().ItemInnerSpacing.x);
		if (ImGui::InputText ("##value", &(s.*field))) typing = true;
		track ();
		ImGui::SameLine (0, ImGui::GetStyle ().ItemInnerSpacing.x);
		if (ImGui::Button ("...", ImVec2 (button, 0))) {
			std::vector<FileFilter> filters (1, FileFilter ());
			filters[0].name = "Images";
			filters[0].pattern = "png;tga;ico;icns";
			ProjectDocument * p = project;
			actions.platform ().openFileDialog (label, filters, p->folder (), false, [p, field] (const std::vector<std::string> & files) {
				if (files.empty ()) return;
				ProjectSettings changed = p->settings ();
				changed.*field = p->relativePath (files[0]);
				p->setSettings (changed);
			});
		}
		ImGui::PopID ();
	};

	ImGui::PushID (project);
	if (beginProperties ("Game")) {
		text ("Window title", &s.windowName, NULL);
		text ("Game file", &s.finalFile, "The name of the compiled game, without .slg");
		text ("Language", &s.language, "The language of the texts in the scripts");
		text ("Save folder", &s.dataFolder, "The folder for the saved games, in the folder of the player's settings");
		text ("Quit message", &s.quitMessage, "What the game asks when the player closes its window");
		file ("Icon", &ProjectSettings::customIcon, "The icon of the game's window");
		file ("Logo", &ProjectSettings::customLogo, "A logo of 310 x 88 pixels, which the game shows while it starts");
		ImGui::EndTable ();
	}
	if (beginProperties ("Window")) {
		number ("Width", &s.width, 0, 4000, "The width of the game, in pixels");
		number ("Height", &s.height, 0, 4000, "The height of the game, in pixels");
		number ("Frame time", &s.speed, 0, 400, "The milliseconds of each frame");
		check ("Full screen", &s.fullScreen, "Start in full screen");
		check ("Invisible", &s.invisible, "Start without a window");
		bool showMouse = s.mouse == 1;
		check ("System mouse", &showMouse, "Show the mouse pointer of the system over the game");
		s.mouse = showMouse ? 1 : 2;
		check ("Logo", &s.showLogo, "Show the logo while the game starts");
		check ("Loading", &s.showLoading, "Show the loading screen while the game starts");
		check ("Dither", &s.ditherImages, "Dither the images, for displays with few colours");
		check ("Silent", &s.makeSilent, "Leave out every sound and all music");
		ImGui::EndTable ();
	}
	if (beginProperties ("Character rendering")) {
		check ("Soften", &s.chrDefaultEnabled, "Soften the characters by default");
		number ("Softness X", &s.chrDefaultSoftnessX, 0, 1000, NULL);
		number ("Softness Y", &s.chrDefaultSoftnessY, 0, 1000, NULL);
		check ("Limit", &s.chrMaxEnabled, "Limit the softness");
		check ("Player limit", &s.chrMaxReadIni, "Read the limit from the player's settings");
		number ("Most X", &s.chrMaxSoftnessX, 0, 1000, NULL);
		number ("Most Y", &s.chrMaxSoftnessY, 0, 1000, NULL);
		ImGui::EndTable ();
	}
	ImGui::PopID ();

	if (typing || chosen) project->setSettings (s, typing ? keyActive : 0);
	if (done || chosen) project->commands ().stopMerging ();
}

} // namespace devkit
