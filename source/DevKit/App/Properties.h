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

// Two fields on one row, "X" and "Y", sharing its width but trailing (for
// a button after them): true when either changed; done when either was
// let go.
inline bool xyFields (const char * id, float * x, float * y, float speed, const char * format,
		float min = 0, float max = 0, bool * done = nullptr, float trailing = 0) {
	ImGui::PushID (id);
	const ImGuiStyle & style = ImGui::GetStyle ();
	float labels = ImGui::CalcTextSize ("X").x + ImGui::CalcTextSize ("Y").x;
	float width = (ImGui::GetContentRegionAvail ().x - labels - trailing - style.ItemInnerSpacing.x * 2 - style.ItemSpacing.x * (trailing > 0 ? 2 : 1)) * 0.5f;
	if (width < 20) width = 20;
	bool changed = false;
	ImGui::AlignTextToFramePadding ();
	ImGui::TextUnformatted ("X");
	ImGui::SameLine (0, style.ItemInnerSpacing.x);
	ImGui::SetNextItemWidth (width);
	changed |= ImGui::DragFloat ("##x", x, speed, min, max, format);
	if (done) * done |= ImGui::IsItemDeactivated ();
	ImGui::SameLine ();
	ImGui::TextUnformatted ("Y");
	ImGui::SameLine (0, style.ItemInnerSpacing.x);
	ImGui::SetNextItemWidth (width);
	changed |= ImGui::DragFloat ("##y", y, speed, min, max, format);
	if (done) * done |= ImGui::IsItemDeactivated ();
	if (trailing > 0) ImGui::SameLine ();
	ImGui::PopID ();
	return changed;
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
