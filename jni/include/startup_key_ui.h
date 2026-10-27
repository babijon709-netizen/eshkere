#pragma once

#include "startup_key.h"
#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <cstring>

// Self-contained touch keyboard for launchers without an interactive console.
// It runs before version/transport selection and never grants access without a
// successful key comparison. No Activity or Android soft keyboard is needed.
namespace startup_key_ui {

enum class Result { Waiting, Accepted, Cancelled };

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

inline bool Key(const char* id, const char* label, ImVec2 pos, ImVec2 size, float font_size,
                ImU32 color = IM_COL32(57, 61, 74, 255)) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, size);
    const bool tapped = TappedItem();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, {pos.x + size.x, pos.y + size.y}, color, 5.f);
    ImFont* font = ImGui::GetFont();
    const ImVec2 text = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, label);
    dl->AddText(font, font_size,
                {pos.x + (size.x - text.x) * 0.5f, pos.y + (size.y - text.y) * 0.5f},
                IM_COL32(242, 244, 249, 255), label);
    return tapped;
}

// Draw once inside an ImGui frame. width/height are the visible display, not
// the square backing surface; all controls remain inside that rectangle.
inline Result Draw(startup_key::Entry& entry, float width, float height) {
    if (width < 100.f || height < 120.f) return Result::Waiting;
    const float margin = 8.f, pad = 10.f, gap = 4.f;
    const float panel_w = std::min(width - margin * 2.f, 700.f);
    const float key_w = (panel_w - pad * 2.f - 9.f * gap) / 10.f;
    const float font_size = std::max(10.f, std::min(25.f, key_w * 0.72f));
    const float title_h = font_size + 7.f;
    const float field_h = font_size + 12.f;
    const float error_h = font_size * 0.8f + 4.f;
    const float room = height - margin * 2.f - pad * 2.f - title_h - field_h - error_h - 7.f * gap;
    const float key_h = std::max(12.f, std::min(55.f, room / 5.f));
    const float panel_h = pad * 2.f + title_h + field_h + error_h + 7.f * gap + 5.f * key_h;
    const float left = (width - panel_w) * 0.5f;
    const float top = (height - panel_h) * 0.5f;

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::SetNextWindowPos({0.f, 0.f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({width, height}, ImGuiCond_Always);
    ImGui::Begin("##startup_key", nullptr, flags);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    dl->AddRectFilled({left, top}, {left + panel_w, top + panel_h}, IM_COL32(24, 26, 33, 248), 10.f);
    float y = top + pad;
    dl->AddText(font, font_size, {left + pad, y}, IM_COL32(245, 246, 250, 255), "Enter key:");
    y += title_h + gap;
    const ImVec2 input_a{left + pad, y}, input_b{left + panel_w - pad, y + field_h};
    dl->AddRectFilled(input_a, input_b, IM_COL32(42, 45, 55, 255), 5.f);
    const char* visible = entry.text.c_str();
    while (*visible && font->CalcTextSizeA(font_size, FLT_MAX, 0.f, visible).x > panel_w - 4.f * pad)
        ++visible;
    dl->PushClipRect(input_a, input_b, true);
    dl->AddText(font, font_size, {input_a.x + 7.f, input_a.y + (field_h - font_size) * 0.5f},
                IM_COL32(245, 246, 250, 255), visible);
    dl->PopClipRect();
    y += field_h + gap;
    if (entry.invalid)
        dl->AddText(font, font_size * 0.8f, {left + pad, y}, IM_COL32(255, 126, 126, 255), "Invalid key");
    y += error_h + gap;

    const char* rows[] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm./-"};
    for (const char* row : rows) {
        const int count = static_cast<int>(std::strlen(row));
        const float row_w = count * key_w + (count - 1) * gap;
        const float x = left + (panel_w - row_w) * 0.5f;
        for (int i = 0; i < count; ++i) {
            char label[2] = {row[i], '\0'};
            char id[16] = "##key_";
            id[6] = row[i]; id[7] = '\0';
            if (Key(id, label, {x + i * (key_w + gap), y}, {key_w, key_h}, font_size))
                entry.Add(row[i]);
        }
        y += key_h + gap;
    }
    const float action_w = (panel_w - 2.f * pad - 2.f * gap) / 3.f;
    Result result = Result::Waiting;
    if (Key("##backspace", "Del", {left + pad, y}, {action_w, key_h}, font_size))
        entry.Backspace();
    if (Key("##cancel", "Cancel", {left + pad + action_w + gap, y},
            {action_w, key_h}, font_size)) result = Result::Cancelled;
    if (Key("##submit", "Enter", {left + pad + 2.f * (action_w + gap), y},
            {action_w, key_h}, font_size, IM_COL32(58, 100, 157, 255)) && entry.Submit())
        result = Result::Accepted;
    ImGui::End();
    return result;
}
} // namespace startup_key_ui
