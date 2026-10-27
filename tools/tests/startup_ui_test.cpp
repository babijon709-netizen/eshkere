// The unavailable fallback is a message and cancel button, never a fake keyboard.
#include "startup_key_ui.h"
#include "imgui_internal.h"
#include <cassert>
#include <cstdio>
#include <map>

static std::map<ImGuiID, ImRect> items;
void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID id, const ImRect& box, const ImGuiLastItemData*) {
    if (id) items[id] = box;
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID, const char*, ImGuiItemStatusFlags) {}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }

static float screen_w, screen_h;
static ImGuiID item_id(const char* label) { return ImHashStr(label, 0, ImHashStr("##startup_key")); }
static startup_key_ui::Result frame(ImVec2 tap = {-100, -100}, bool release = false) {
    items.clear();
    ImGuiIO& io = ImGui::GetIO();
    const float side = screen_w > screen_h ? screen_w : screen_h;
    io.DisplaySize = {side, side}; io.DeltaTime = 1.f / 60.f;
    ImGui::NewFrame();
    io.MouseReleased[0] = release;
    io.MousePos = io.MouseClickedPos[0] = tap;
    const auto result = startup_key_ui::Draw(screen_w, screen_h);
    ImGui::Render();
    return result;
}
static void layout(float width, float height) {
    screen_w = width; screen_h = height;
    assert(frame() == startup_key_ui::Result::Waiting);
    assert(items.count(item_id("##cancel")) && !items.count(item_id("##key_q")));
    const ImRect box = items.at(item_id("##cancel"));
    assert(box.Min.x >= 0 && box.Min.y >= 0 && box.Max.x <= width && box.Max.y <= height);
    assert(frame({(box.Min.x + box.Max.x) * 0.5f, (box.Min.y + box.Max.y) * 0.5f}, true) ==
           startup_key_ui::Result::Cancelled);
}
int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::GetCurrentContext()->TestEngineHookItems = true;
    io.IniFilename = nullptr;
    unsigned char* pixels = nullptr; int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    assert(pixels && w && h);
    layout(1280, 720); layout(720, 1280); layout(320, 240);
    ImGui::DestroyContext();
    std::puts("startup keyboard-unavailable fallback: PASS");
}
