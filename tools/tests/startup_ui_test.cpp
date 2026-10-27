// Headless test of the real ImGui startup keyboard and its tap handling.
#include "startup_key_ui.h"
#include "imgui_internal.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <string>

static std::map<ImGuiID, ImRect> items;
void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID id, const ImRect& box, const ImGuiLastItemData*) {
    if (id) items[id] = box;
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID, const char*, ImGuiItemStatusFlags) {}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }

static float screen_w, screen_h;
static startup_key::Entry entry;
static ImGuiID item_id(const char* label) {
    return ImHashStr(label, 0, ImHashStr("##startup_key"));
}
static startup_key_ui::Result frame(ImVec2 tap = {-100, -100}, bool release = false) {
    items.clear();
    ImGuiIO& io = ImGui::GetIO();
    const float side = screen_w > screen_h ? screen_w : screen_h;
    io.DisplaySize = {side, side}; io.DeltaTime = 1.f / 60.f;
    ImGui::NewFrame();
    io.MouseReleased[0] = release;
    io.MousePos = io.MouseClickedPos[0] = tap;
    const auto result = startup_key_ui::Draw(entry, screen_w, screen_h);
    ImGui::Render();
    return result;
}
static startup_key_ui::Result tap(const char* label) {
    auto found = items.find(item_id(label));
    assert(found != items.end());
    const ImRect box = found->second;
    return frame({(box.Min.x + box.Max.x) * 0.5f, (box.Min.y + box.Max.y) * 0.5f}, true);
}
static void layout_and_typing(float width, float height) {
    screen_w = width; screen_h = height; entry = {};
    assert(frame() == startup_key_ui::Result::Waiting);
    // 39 character keys plus three actions; all fully within the visible
    // display, including on a square backing surface in portrait.
    assert(items.size() >= 42);
    for (char c : std::string("1234567890qwertyuiopasdfghjklzxcvbnm./-")) {
        char id[] = "##key_x"; id[6] = c;
        assert(items.count(item_id(id)));
        const ImRect& r = items.at(item_id(id));
        assert(r.Min.x >= 0.f && r.Min.y >= 0.f);
        assert(r.Max.x <= width && r.Max.y <= height);
    }
    for (const char* id : {"##backspace", "##cancel", "##submit"}) {
        assert(items.count(item_id(id)));
        const ImRect& r = items.at(item_id(id));
        assert(r.Min.x >= 0.f && r.Min.y >= 0.f);
        assert(r.Max.x <= width && r.Max.y <= height);
    }
    assert(tap("##key_x") == startup_key_ui::Result::Waiting);
    assert(tap("##submit") == startup_key_ui::Result::Waiting);
    assert(entry.invalid && entry.text.empty());
    for (char c : std::string("t.me/benzwarex")) {
        char id[] = "##key_x";
        id[6] = c;
        assert(tap(id) == startup_key_ui::Result::Waiting);
    }
    assert(tap("##backspace") == startup_key_ui::Result::Waiting);
    assert(entry.text == startup_key::kLaunchKey);
    assert(tap("##submit") == startup_key_ui::Result::Accepted);
    assert(entry.text.empty());
    assert(frame() == startup_key_ui::Result::Waiting);
    assert(tap("##cancel") == startup_key_ui::Result::Cancelled);
}
int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::GetCurrentContext()->TestEngineHookItems = true;
    io.IniFilename = nullptr;
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    assert(pixels && w && h);
    layout_and_typing(1280, 720);
    layout_and_typing(720, 1280);
    layout_and_typing(320, 240);
    ImGui::DestroyContext();
    std::puts("startup overlay layout and key taps: PASS");
}
