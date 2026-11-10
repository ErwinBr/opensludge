#ifndef DEVKIT_PROPERTIES_H
#define DEVKIT_PROPERTIES_H

#include <float.h>
#include <stdio.h>

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

// A colour of factors from 0 to max (2: above 1 brightens): a swatch as wide
// as the row but trailing, which opens a picker and the components in
// percent. True when it changed; done when an edit was let go.
inline bool colourField (const char * id, float rgba[4], float max = 2.0f, bool * done = nullptr, float trailing = 0) {
	ImGui::PushID (id);
	const ImGuiStyle & style = ImGui::GetStyle ();
	float width = ImGui::GetContentRegionAvail ().x - trailing - (trailing > 0 ? style.ItemSpacing.x : 0);
	if (width < 20) width = 20;
	ImVec4 shown (rgba[0] < 1 ? rgba[0] : 1, rgba[1] < 1 ? rgba[1] : 1, rgba[2] < 1 ? rgba[2] : 1, rgba[3] < 1 ? rgba[3] : 1);
	bool changed = false;
	if (ImGui::ColorButton ("##swatch", shown, ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoTooltip,
			ImVec2 (width, ImGui::GetFrameHeight ())))
		ImGui::OpenPopup ("colour");
	if (ImGui::IsItemHovered ()) {
		ImGui::SetTooltip ("R %.0f%%  G %.0f%%  B %.0f%%  A %.0f%%\nClick to change it", rgba[0] * 100, rgba[1] * 100, rgba[2] * 100, rgba[3] * 100);
	}
	if (ImGui::BeginPopup ("colour")) {
		ImGui::SetNextItemWidth (ImGui::GetFontSize () * 14);
		if (ImGui::ColorPicker4 ("##picker", rgba, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_AlphaBar
				| ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoInputs)) changed = true;
		if (done) * done |= ImGui::IsItemDeactivated ();
		const char * names[4] = { "R", "G", "B", "A" };
		float field = (ImGui::GetFontSize () * 14 - ImGui::CalcTextSize ("RGBA").x - style.ItemInnerSpacing.x * 4 - style.ItemSpacing.x * 3) / 4;
		for (int c = 0; c < 4; c ++) {
			if (c > 0) ImGui::SameLine ();
			ImGui::AlignTextToFramePadding ();
			ImGui::TextUnformatted (names[c]);
			ImGui::SameLine (0, style.ItemInnerSpacing.x);
			ImGui::SetNextItemWidth (field);
			float percent = rgba[c] * 100.0f;
			char fieldId[8];
			snprintf (fieldId, sizeof (fieldId), "##%d", c);
			if (ImGui::DragFloat (fieldId, &percent, 0.5f, 0, max * 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
				rgba[c] = percent / 100.0f;
				changed = true;
			}
			if (done) * done |= ImGui::IsItemDeactivated ();
		}
		ImGui::EndPopup ();
	}
	for (int c = 0; c < 4; c ++) rgba[c] = rgba[c] < 0 ? 0 : rgba[c] > max ? max : rgba[c];
	if (trailing > 0) ImGui::SameLine ();
	ImGui::PopID ();
	return changed;
}

// A titled property grid; end it with ImGui::EndTable when it returns true.
// The labels take labels of the width, the fields the rest.
inline bool beginProperties (const char * title, float labels = 0.4f) {
	ImGui::SeparatorText (title);
	if (! ImGui::BeginTable (title, 2, ImGuiTableFlags_SizingStretchProp)) return false;
	ImGui::TableSetupColumn ("label", ImGuiTableColumnFlags_WidthStretch, labels);
	ImGui::TableSetupColumn ("field", ImGuiTableColumnFlags_WidthStretch, 1.0f - labels);
	return true;
}

} // namespace devkit

#endif
