#pragma once

#include <imgui.h>

#include <Luna/UI/IconsFontAwesome6.hpp>

namespace Luna {
namespace UI {
void BeginDockspace(bool menuBar = false);
void EndDockspace();

bool ButtonExRounded(const char* label,
                     const ImVec2& size_arg = ImVec2(0, 0),
                     ImGuiButtonFlags flags = 0,
                     ImDrawFlags drawFlags  = 0);
void RenderFrameRounded(ImVec2 p_min, ImVec2 p_max, ImU32 fill_col, bool border, float rounding, ImDrawFlags drawFlags);
}  // namespace UI
}  // namespace Luna
