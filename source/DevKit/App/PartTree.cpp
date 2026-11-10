#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "animedit.h"
#include "Icons.h"
#include "PartTree.h"

namespace devkit {

namespace {

void drawPart (const RigDefinition & r, int index, bool haveSelection, PartId selected, const PartTree & tree) {
	if (index < 0 || index >= (int) r.parts.size ()) return;
	const RigPart & part = r.parts[index];
	std::vector<int> children = partChildren (r, index);
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
		| ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap
		| ImGuiTreeNodeFlags_FramePadding;
	PartId id = part.id;
	bool locked = tree.isLocked && tree.isLocked (id);
	if (children.empty ()) flags |= ImGuiTreeNodeFlags_Leaf;
	if (haveSelection && id == selected) flags |= ImGuiTreeNodeFlags_Selected;
	std::string label = index == 0 ? std::string ("Character") : part.name;
	if (index > 0 && part.sprite.kind != SpriteRef::SPRITE) label += "  (joint)";
	// A drop below changes the parts: we find the children again by their IDs.
	std::vector<PartId> ids;
	for (size_t i = 0; i < children.size (); i ++) ids.push_back (r.parts[children[i]].id);
	ImGui::PushID ((int) id);
	// The lock goes at the end of the row, over the node, which ends before
	// it. Rows are a field high, as the timeline's are, and so is the lock.
	float right = ImGui::GetCursorScreenPos ().x + ImGui::GetContentRegionAvail ().x;
	float top = ImGui::GetCursorScreenPos ().y;
	float lockSide = ImGui::GetFrameHeight ();
	ImGui::PushClipRect (ImVec2 (ImGui::GetWindowPos ().x, top), ImVec2 (right - lockSide - ImGui::GetStyle ().ItemInnerSpacing.x, top + ImGui::GetFrameHeight ()), true);
	if (locked) ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
	bool open = ImGui::TreeNodeEx ("##part", flags, "%s", label.c_str ());
	if (locked) ImGui::PopStyleColor ();
	ImGui::PopClipRect ();
	if (ImGui::IsItemClicked () && ! ImGui::IsItemToggledOpen () && ! locked && tree.select) tree.select (id);
	if (tree.drop && index > 0 && ! locked && ImGui::BeginDragDropSource ()) {
		ImGui::SetDragDropPayload ("RIG_PART", &id, sizeof (id));
		ImGui::Text ("Attach %s to...", part.name.c_str ());
		ImGui::EndDragDropSource ();
	}
	if (tree.drop && ImGui::BeginDragDropTarget ()) {
		if (const ImGuiPayload * payload = ImGui::AcceptDragDropPayload ("RIG_PART")) tree.drop (* (const PartId *) payload->Data, id);
		ImGui::EndDragDropTarget ();
	}
	// On the node's line, at its end.
	ImGui::SameLine ();
	ImGui::SetCursorScreenPos (ImVec2 (right - lockSide, top));
	if (iconButton ("lock", locked ? ICON_LOCKED : ICON_UNLOCKED,
			locked ? "Locked: it can't be selected or changed. Click to unlock it." : "Lock it, so that it can't be selected or changed by accident",
			lockSide) && tree.setLocked) {
		tree.setLocked (id, ! locked);
	}
	if (open) {
		for (size_t i = 0; i < ids.size (); i ++) drawPart (r, partWithId (r, ids[i]), haveSelection, selected, tree);
		ImGui::TreePop ();
	}
	ImGui::PopID ();
}

}

bool foldArrow (const char * id, bool open) {
	ImVec2 at = ImGui::GetCursorScreenPos ();
	bool clicked = ImGui::InvisibleButton (id, ImVec2 (foldWidth (), ImGui::GetFrameHeight ()));
	// Where, and how big, an unframed tree node with frame padding puts it.
	const ImGuiStyle & style = ImGui::GetStyle ();
	ImGui::RenderArrow (ImGui::GetWindowDrawList (),
		ImVec2 (at.x + style.FramePadding.x, at.y + style.FramePadding.y + ImGui::GetFontSize () * 0.15f),
		ImGui::GetColorU32 (ImGuiCol_Text), open ? ImGuiDir_Down : ImGuiDir_Right, 0.70f);
	return clicked;
}

float foldWidth () {
	return ImGui::GetFontSize () + ImGui::GetStyle ().FramePadding.x * 2;
}

void drawPartTree (const RigDefinition & rig, bool haveSelection, PartId selected, const PartTree & tree) {
	drawPart (rig, 0, haveSelection, selected, tree);
}

} // namespace devkit
