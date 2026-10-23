#pragma once
#include <imgui.h>

inline bool bandwidthLockButton(const char* id, bool locked) {
    const float h = ImGui::GetFrameHeight();
    const bool clicked = ImGui::Button(id, ImVec2(h, h));
    const auto p = ImGui::GetItemRectMin();
    auto* draw = ImGui::GetWindowDrawList();
    const auto color = ImGui::GetColorU32(ImGuiCol_Text);
    draw->AddRectFilled(ImVec2(p.x + h * .27f, p.y + h * .46f),
        ImVec2(p.x + h * .73f, p.y + h * .81f), color, h * .06f);
    draw->PathArcTo(ImVec2(p.x + h * (locked ? .5f : .67f), p.y + h * .46f),
        h * .18f, 3.14159265f, 6.28318531f, 12);
    draw->PathStroke(color, false, h * .07f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", locked
        ? "Spectrum bandwidth locked.\nChange bandwidth here or by selecting a modulation.\nClick to allow dragging the spectrum filter edges."
        : "Spectrum bandwidth unlocked.\nClick to lock the spectrum filter edges.");
    return clicked;
}
