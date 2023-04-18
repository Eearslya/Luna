#include <imgui_internal.h>

#include <Luna/UI/UI.hpp>

using namespace ImGui;

namespace Luna {
namespace UI {
void BeginDockspace(bool menuBar) {
	ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;
	ImGuiWindowFlags windowFlags      = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
	                               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
	                               ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	if (menuBar) { windowFlags |= ImGuiWindowFlags_MenuBar; }

	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(viewport->Size);
	ImGui::SetNextWindowViewport(viewport->ID);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::Begin("Dockspace", nullptr, windowFlags);
	ImGui::PopStyleVar(3);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(370.0f, 64.0f));
	ImGuiID dockspaceId = ImGui::GetID("Dockspace");
	ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);
	ImGui::PopStyleVar();
}

void EndDockspace() {
	ImGui::End();
}

bool ButtonExRounded(const char* label, const ImVec2& size_arg, ImGuiButtonFlags flags, ImDrawFlags drawFlags) {
	ImGuiWindow* window = GetCurrentWindow();
	if (window->SkipItems) return false;

	ImGuiContext& g         = *GImGui;
	const ImGuiStyle& style = g.Style;
	const ImGuiID id        = window->GetID(label);
	const ImVec2 label_size = CalcTextSize(label, NULL, true);

	ImVec2 pos = window->DC.CursorPos;
	if ((flags & ImGuiButtonFlags_AlignTextBaseLine) &&
	    style.FramePadding.y <
	      window->DC.CurrLineTextBaseOffset)  // Try to vertically align buttons that are smaller/have no padding so that
	                                          // text baseline matches (bit hacky, since it shouldn't be a flag)
		pos.y += window->DC.CurrLineTextBaseOffset - style.FramePadding.y;
	ImVec2 size =
		CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

	const ImRect bb(pos, pos + size);
	ItemSize(size, style.FramePadding.y);
	if (!ItemAdd(bb, id)) return false;

	if (g.LastItemData.InFlags & ImGuiItemFlags_ButtonRepeat) flags |= ImGuiButtonFlags_Repeat;

	bool hovered, held;
	bool pressed = ButtonBehavior(bb, id, &hovered, &held, flags);

	// Render
	const ImU32 col = GetColorU32((held && hovered) ? ImGuiCol_ButtonActive
	                              : hovered         ? ImGuiCol_ButtonHovered
	                                                : ImGuiCol_Button);
	RenderNavHighlight(bb, id);
	RenderFrameRounded(bb.Min, bb.Max, col, true, style.FrameRounding, drawFlags);

	if (g.LogEnabled) LogSetNextTextDecoration("[", "]");
	RenderTextClipped(
		bb.Min + style.FramePadding, bb.Max - style.FramePadding, label, NULL, &label_size, style.ButtonTextAlign, &bb);

	// Automatically close popups
	// if (pressed && !(flags & ImGuiButtonFlags_DontClosePopups) && (window->Flags & ImGuiWindowFlags_Popup))
	//    CloseCurrentPopup();

	IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);
	return pressed;
}

void RenderFrameRounded(
	ImVec2 p_min, ImVec2 p_max, ImU32 fill_col, bool border, float rounding, ImDrawFlags drawFlags) {
	ImGuiContext& g     = *GImGui;
	ImGuiWindow* window = g.CurrentWindow;
	window->DrawList->AddRectFilled(p_min, p_max, fill_col, rounding, drawFlags);
	const float border_size = g.Style.FrameBorderSize;
	if (border && border_size > 0.0f) {
		window->DrawList->AddRect(
			p_min + ImVec2(1, 1), p_max + ImVec2(1, 1), GetColorU32(ImGuiCol_BorderShadow), rounding, drawFlags, border_size);
		window->DrawList->AddRect(p_min, p_max, GetColorU32(ImGuiCol_Border), rounding, drawFlags, border_size);
	}
}

}  // namespace UI
}  // namespace Luna
