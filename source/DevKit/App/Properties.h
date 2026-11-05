#ifndef DEVKIT_PROPERTIES_H
#define DEVKIT_PROPERTIES_H

#include <float.h>

#include "imgui.h"

namespace devkit {

// A row of the property grid: the label on the left, and the field, as
// wide as the rest, on the right.
inline void propertyRow (const char * label, const char * tooltip = nullptr) {
	ImGui::TableNextRow ();
	ImGui::TableNextColumn ();
	ImGui::AlignTextToFramePadding ();
	ImGui::TextUnformatted (label);
	if (tooltip) ImGui::SetItemTooltip ("%s", tooltip);
	ImGui::TableNextColumn ();
	ImGui::SetNextItemWidth (-FLT_MIN);
}

// A titled property grid; end it with ImGui::EndTable when it returns true.
inline bool beginProperties (const char * title) {
	ImGui::SeparatorText (title);
	if (! ImGui::BeginTable (title, 2, ImGuiTableFlags_SizingStretchProp)) return false;
	ImGui::TableSetupColumn ("label", ImGuiTableColumnFlags_WidthStretch, 0.4f);
	ImGui::TableSetupColumn ("field", ImGuiTableColumnFlags_WidthStretch, 0.6f);
	return true;
}

} // namespace devkit

#endif
