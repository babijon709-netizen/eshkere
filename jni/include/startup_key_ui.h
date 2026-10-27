#pragma once
#include "imgui.h"
#include <algorithm>
#include <cfloat>

// Error-only fallback when Android refuses to install/open its IME helper. It
// never draws a replacement keyboard. A terminal key can still be supplied.
namespace startup_key_ui {
enum class Result { Waiting, Cancelled };

inline bool TappedItem() {
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.MouseReleased[0]) return false;
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const ImVec2 start = io.MouseClickedPos[0], end = io.MousePos;
    const float dx = end.x - start.x, dy = end.y - start.y;
    return start.x >= a.x && start.x <= b.x && start.y >= a.y && start.y <= b.y &&
           end.x >= a.x && end.x <= b.x && end.y >= a.y && end.y <= b.y &&
           dx * dx + dy * dy <= 900.f;
}

inline Result Draw(float width, float height) {
    if (width < 100.f || height < 120.f) return Result::Waiting;
    const float panel_w = std::min(width - 16.f, 560.f);
    const float panel_h = std::min(height - 16.f, 180.f);
    const float left = (width - panel_w) * 0.5f, top = (height - panel_h) * 0.5f;
    const float title_size = std::max(12.f, std::min(22.f, panel_w / 22.f));
    const float text_size = std::max(10.f, title_size * 0.75f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::SetNextWindowPos({0.f, 0.f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({width, height}, ImGuiCond_Always);
    ImGui::Begin("##startup_key", nullptr, flags);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    dl->AddRectFilled({left, top}, {left + panel_w, top + panel_h}, IM_COL32(24, 26, 33, 248), 10.f);
    dl->AddText(font, title_size, {left + 12.f, top + 12.f}, IM_COL32(245, 246, 250, 255),
                "Android keyboard unavailable");
    dl->AddText(font, text_size, {left + 12.f, top + 20.f + title_size}, IM_COL32(215, 220, 230, 255),
                "Allow the temporary input app to install,\nor enter the key in a root terminal.");
    const float button_w = std::min(150.f, panel_w - 24.f);
    const float button_h = std::min(40.f, panel_h * 0.25f);
    const ImVec2 a{left + (panel_w - button_w) * 0.5f, top + panel_h - button_h - 10.f};
    ImGui::SetCursorScreenPos(a);
    ImGui::InvisibleButton("##cancel", {button_w, button_h});
    const bool cancelled = TappedItem();
    dl->AddRectFilled(a, {a.x + button_w, a.y + button_h}, IM_COL32(57, 61, 74, 255), 5.f);
    const ImVec2 label = font->CalcTextSizeA(title_size, FLT_MAX, 0.f, "Cancel");
    dl->AddText(font, title_size,
                {a.x + (button_w - label.x) * 0.5f, a.y + (button_h - title_size) * 0.5f},
                IM_COL32(242, 244, 249, 255), "Cancel");
    ImGui::End();
    return cancelled ? Result::Cancelled : Result::Waiting;
}
}
