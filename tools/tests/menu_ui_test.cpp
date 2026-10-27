// Headless integration test. Compile with run_menu_ui.sh, not the header-only
// jni/tests runner. Real production menu/widgets + ImGui, mocked platform I/O.
#include "main.h"
#include "app_state.h"
#include "audio.h"
#include "config.h"
#include "screen_capture.h"
#include "hud.h"
#include "lang.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"
#include "viewmodel.h"
#include "ImGui/Font/Font.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <chrono>
#include <algorithm>
#include <vector>
#include <map>
#include <set>
#include <string>

AppState g_state;
InputState g_input;
bool menu_open = true;
ConfigEntry g_configs[kMaxConfigs];
int g_configCount = 0, g_configToDelete = -1, g_cfgLoadedIdx = -1;
char g_loadedConfigName[64] = {};
float g_cfgLoadAnim[kMaxConfigs] = {};
static int loads = 0, saves = 0, updates = 0, applies = 0;
static int screenshots = 0, videoStarts = 0, videoStops = 0;
static bool videoRunning = false, videoStopping = false;
static screen_capture::VideoOptions lastVideoOptions{};
namespace screen_capture {
StartResult Screenshot() { ++screenshots; return StartResult::Started; }
StartResult StartVideo(VideoOptions options) { lastVideoOptions = options; ++videoStarts; videoRunning = true; return StartResult::Started; }
bool StopVideo() { if (videoStopping) return false; ++videoStops; videoStopping = true; return true; }
bool VideoRunning() { return videoRunning; }
bool VideoStopping() { return videoStopping; }
}
void ShowToast(const char*) {}
void PlaySound(SoundId) {}
void ConfigSave() { ++saves; }
void ConfigLoad(int index, bool) { ++loads; g_cfgLoadedIdx = index; }
void ConfigUpdate(int) { ++updates; }
void esp_viewmodel_tick(const ViewmodelSettings&) { ++applies; }
float AimTouchFracX() { return g_state.aim_tx < 0.f ? 0.72f : g_state.aim_tx; }
float AimTouchFracY() { return g_state.aim_ty < 0.f ? 0.48f : g_state.aim_ty; }
namespace Blur { void Freeze(int, int, int, int, int, int) {} }

static std::map<ImGuiID, ImRect> items;
static std::set<ImGuiID> duplicates;
void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID id, const ImRect& box, const ImGuiLastItemData*) {
    if (!id) return;
    if (items.count(id)) duplicates.insert(id);
    items[id] = box;
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID, const char*, ImGuiItemStatusFlags) {}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }

extern float TabContent(int, float, float);
static std::string windowName = "test";
static float width = 650.f, height = 12000.f;
static float viewportClip = 0.f;
static ImGuiID windowId() { return ImHashStr(windowName.c_str()); }
static ImGuiID id(const char* label) { return ImHashStr(label, 0, windowId()); }
static ImGuiID groupId(const char* group, const char* child = "expand") {
    return ImHashStr(child, 0, id(group));
}
static ImGuiID choiceId(const char* group, int choice, ImGuiID parent = 0) {
    auto seed = ImHashStr(group, 0, parent ? parent : windowId());
    seed = ImHashData(&choice, sizeof(choice), seed);
    return ImHashStr("choice", 0, seed);
}
static float frame(const std::function<void()>& draw, ImVec2 tap = {-100.f, -100.f}, bool released = false,
                   bool consumed = false, bool clipped = false) {
    items.clear(); duplicates.clear();
    auto& io = ImGui::GetIO();
    io.DisplaySize = {width, height}; io.DeltaTime = 1.f / 60.f;
    ImGui::NewFrame();
    g_input.touchConsumed = consumed;
    // Exercise the same release contract populated by the Android backend.
    io.MouseReleased[0] = released;
    io.MousePos = io.MouseClickedPos[0] = tap;
    ImGui::SetNextWindowPos({0.f, 0.f});
    ImGui::SetNextWindowSize({width, height});
    ImGui::Begin(windowName.c_str(), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetCursorPos({0.f, 0.f});
    if (viewportClip > 0.f) ImGui::GetWindowDrawList()->PushClipRect({0.f, 0.f}, {width, viewportClip}, true);
    if (clipped) ImGui::GetWindowDrawList()->PushClipRect({0.f, 0.f}, {width, 10.f}, true);
    draw();
    const float result = ImGui::GetCursorPosY();
    if (clipped) ImGui::GetWindowDrawList()->PopClipRect();
    if (viewportClip > 0.f) ImGui::GetWindowDrawList()->PopClipRect();
    ImGui::End();
    ImGui::Render();
    assert(duplicates.empty());
    return result;
}
static auto tab(int index) { return [index] { TabContent(index, 1.f / 60.f, width); }; }
static void tap(const std::function<void()>& draw, ImGuiID target) {
    frame(draw);
    assert(items.count(target));
    const auto center = items.at(target).GetCenter();
    frame(draw, center, true);
}
static void expand(int index, const char* group) { tap(tab(index), groupId(group)); }
static void fresh(const char* name) {
    windowName = name;
    g_state = {}; g_pop = {}; g_sheet = {}; g_win.closing = false;
    g_scrollMain = {}; g_scrollPop = {}; g_calibMode = 0;
    menu_open = true; lang::set(lang::LANG_RU);
}

static void widgetTests() {
    fresh("headers");
    bool enabled = false, open = false, changed = true;
    auto draw = [&] { open = SettingsHeader("group", "feature", "settings", false, &enabled, 0.f, &changed); };
    frame(draw); assert(!open && !enabled && !changed);
    tap(draw, groupId("group")); assert(open && !enabled && !changed);
    tap(draw, groupId("group", "enable")); assert(open && enabled && changed);
    frame(draw); assert(open && enabled && !changed);
    tap(draw, groupId("group")); assert(!open && enabled);
    const auto point = items.at(groupId("group")).GetCenter();
    g_pop.visible = true; frame(draw, point, true); assert(!open && enabled); g_pop = {};
    g_sheet.visible = true; frame(draw, point, true); assert(!open); g_sheet = {};
    g_scrollMain.dragging = true; frame(draw, point, true); assert(!open); g_scrollMain = {};
    frame(draw, point, true, true); assert(!open);
    frame(draw, point, true, false, true); assert(!open);
    g_win.closing = true; frame(draw, point, true); assert(!open); g_win.closing = false;
    tap(draw, groupId("group")); assert(open && enabled);
    lang::set(lang::LANG_EN); frame(draw); assert(open); // IDs never depend on localized text
    // A switch click is never also an expansion click; a second header cannot
    // see the release consumed by the first one.
    fresh("choices");
    int selected = 0;
    const char* const labels[] = {"one", "two", "three"};
    bool picked = false;
    auto choices = [&] { picked = ChoiceRow("select", &selected, labels, 3); };
    tap(choices, choiceId("select", 2)); assert(selected == 2 && picked);
    tap(choices, choiceId("select", 2)); assert(selected == 2 && !picked);
    frame(choices); const auto second = items.at(choiceId("select", 1)).GetCenter();
    g_sheet.visible = true; frame(choices, second, true); assert(selected == 2); g_sheet = {};
    frame(choices, second, true, true); assert(selected == 2);
    frame(choices, second, true, false, true); assert(selected == 2);
}

static void aimTests() {
    fresh("aim");
    const float compact = frame(tab(1));
    assert(compact < 700.f);
    assert(!items.count(id("##ta2")) && !items.count(id("##afov")));
    tap(tab(1), choiceId("##aim_mode", 2)); assert(g_state.aim_mode == 2);
    assert(!items.count(groupId("##aim_touch_settings")));
    tap(tab(1), choiceId("##aim_mode", 0)); assert(g_state.aim_mode == 0);
    expand(1, "##aim_target");
    tap(tab(1), choiceId("##aim_bone", 2)); assert(g_state.aim_bone == 2);
    tap(tab(1), choiceId("##aim_priority", 1)); assert(g_state.aim_priority == 1);
    expand(1, "##aim_filters");
    for (const auto* label : {"##ta2", "##ta6", "##ta7"}) tap(tab(1), id(label));
    assert(g_state.aim_pos && g_state.aim_scope_only && g_state.aim_fire_only);
    expand(1, "##aim_tuning");
    assert(items.count(id("##afov")) && items.count(id("##asmt")));
    tap(tab(1), id("##ta3")); assert(g_state.aim_special);
    expand(1, "##aim_touch_settings");
    tap(tab(1), id("##aim_pt")); assert(g_calibMode == 3);
    g_calibMode = 0; g_state.aim_tx = 0.6f; g_state.aim_ty = 0.4f;
    tap(tab(1), id("##aim_pt_rst")); assert(g_state.aim_tx == -1.f && g_state.aim_ty == -1.f);
    assert(frame(tab(1)) > compact);
}

static void espTests() {
    fresh("esp");
    const float compact = frame(tab(2)); assert(compact < 950.f);
    expand(2, "##esp_players");
    for (const auto* label : {"##vb", "##vvs", "##v3", "##vn", "##vd", "##vw", "##vtr", "##vsk", "##var", "##vtm",
                              "##esp_stretch", "##esp_thickness"}) assert(items.count(id(label)));
    // Flip only the dedicated arrow switch while leaving its settings closed.
    tap(tab(2), groupId("##esp_arrows", "enable")); assert(g_state.esp_arrows);
    assert(!items.count(id("##va360r")));
    expand(2, "##esp_arrows");
    for (const auto* label : {"##arrow_color", "##va360o", "##va360d", "##va360r"}) assert(items.count(id(label)));
    tap(tab(2), id("##arrow_color")); assert(g_pop.visible && g_pop.sectionId == 4); g_pop = {};
    expand(2, "##esp_radar"); assert(items.count(id("##vrd1")) && items.count(id("##vrd2")));
    expand(2, "##esp_ore");
    for (const auto* label : {"##orvt", "##orvi", "##orvs"}) assert(items.count(id(label)));
    assert(!g_state.esp_ore); // expanding the settings must never enable a feature
    expand(2, "##esp_entities"); expand(2, "##esp_loot"); expand(2, "##esp_distance");
    for (const auto* label : {"##van", "##vbt", "##vvc", "##vsl", "##vlt", "##vhb", "##vsc", "##vpk", "##vtu", "##vbp", "##vcb", "##vpm", "##vmd"})
        assert(items.count(id(label)));
    const float full = frame(tab(2)); assert(full > compact * 3.f);
    // Collapse at a scrolled location, and clamp stale scroll to new content.
    float max = compact - 400.f;
    g_scrollMain.off = full; g_scrollMain.vel = 1000.f;
    ScrollTick(g_scrollMain, false, false, max, 1.f / 60.f);
    assert(g_scrollMain.off <= max && g_scrollMain.vel == 0.f);
}

static void miscTests() {
    fresh("misc"); assert(frame(tab(3)) < 1300.f);
    const char* const groups[] = {"##misc_fov", "##misc_hands", "##misc_freecam", "##misc_xray",
        "##misc_weapon", "##misc_farm", "##misc_world", "##misc_fakelag", "##misc_fps"};
    for (auto group : groups) expand(3, group);
    for (const auto* label : {"##vmfovvalue", "##vmx", "##vmy", "##vmz", "##vmreset", "##fc2", "##fc1", "##fclr", "##fcjs", "##xrbtn", "##xr1",
            "##norecoil", "##nospread", "##fastfire", "##farm_wood", "##farm_stone", "##farm_metal", "##farm_sulfur", "##farm_range",
            "##wd0", "##wd1", "##instantloot", "##flbtn", "##fakelagpct"}) assert(items.count(id(label)));
    for (auto label : {"##norecoil", "##nospread", "##fastfire"}) {
        int before = applies; tap(tab(3), id(label)); assert(applies == before + 1);
    }
    assert(g_state.viewmodel.no_recoil_button && g_state.viewmodel.no_spread && g_state.fast_fire);
    tap(tab(3), choiceId("##fps_limit", 2)); assert(g_state.viewmodel.fps_limit == 120);
    tap(tab(3), choiceId("##fps_limit", 0)); assert(g_state.viewmodel.fps_limit == 0);
    const std::pair<const char*, int> calibration[] = {
        {"##fcjoy4",4}, {"##fclook7",7}, {"##fcbtn6",6}, {"##xrbtn9",9}, {"##flbtn8",8}, {"##farm_fire2",2}};
    for (auto action : calibration) {
        menu_open = true; g_calibMode = 0;
        tap(tab(3), id(action.first)); assert(g_calibMode == action.second && !menu_open);
    }
    g_state.farm_fire_x = .4f; g_state.farm_fire_y = .5f;
    tap(tab(3), choiceId("##farm_fire_reset", 0));
    assert(g_state.farm_fire_x == -1.f && g_state.farm_fire_y == -1.f);
    // FOV/hands reset still applies immediately, without clearing weapon flags.
    tap(tab(3), id("##vmreset"));
    assert(g_state.viewmodel.no_recoil_button && g_state.viewmodel.no_spread);
}

static void configAndOptionsTests() {
    fresh("configs"); g_configCount = 2;
    std::strcpy(g_configs[0].name, "alpha"); std::strcpy(g_configs[1].name, "beta");
    assert(frame(tab(4)) < 500.f);
    const auto alpha = ImHashStr("alpha", 0, windowId());
    const auto profile = ImHashStr("##config", 0, alpha);
    tap(tab(4), ImHashStr("expand", 0, profile));
    tap(tab(4), choiceId("##config_actions", 0, alpha)); assert(loads == 1 && g_cfgLoadedIdx == 0);
    tap(tab(4), choiceId("##config_actions", 1, alpha)); assert(updates == 1);
    tap(tab(4), choiceId("##config_actions", 2, alpha)); assert(g_configToDelete == 0 && g_pop.sectionId == 3);
    g_pop = {};
    tap(tab(4), id("##cfgcreate")); assert(saves == 1);
    fresh("options"); assert(frame(tab(5)) < 750.f);
    assert(g_state.streamer_mode); // preserve legacy full-screen capture behavior
    assert(g_state.capture_video.bitrate_mbps == 0);
    assert(!items.count(groupId("##options_capture", "enable"))); // switch has an explicit label inside
    for (auto group : {"##options_capture", "##options_language", "##options_appearance", "##options_hud", "##options_tabs"}) expand(5, group);
    for (auto label : {"##capture_overlay", "##lang0", "##lang1", "##ud2", "##acc8", "##hud_cnt", "##pstyle0", "##pstyle1", "##exit"}) assert(items.count(id(label)));
    tap(tab(5), id("##capture_overlay")); assert(!g_state.streamer_mode);
    screenshots = videoStarts = videoStops = 0; videoRunning = videoStopping = false;
    tap(tab(5), choiceId("##capture_actions", 0)); assert(screenshots == 1);
    tap(tab(5), choiceId("##capture_actions", 0)); assert(screenshots == 2); // actions aren't sticky choices
    assert(!items.count(choiceId("##capture_duration", 0))); // no overall recording limit
    tap(tab(5), choiceId("##capture_bitrate", 2)); assert(g_state.capture_video.bitrate_mbps == 8);
    tap(tab(5), choiceId("##capture_actions", 1)); assert(videoStarts == 1 && videoRunning);
    assert(lastVideoOptions.bitrate_mbps == 8);
    // Changing bitrate while running must only change the next start.
    tap(tab(5), choiceId("##capture_bitrate", 3)); assert(g_state.capture_video.bitrate_mbps == 16);
    assert(lastVideoOptions.bitrate_mbps == 8);
    tap(tab(5), choiceId("##capture_actions", 1)); assert(videoStops == 1 && videoStopping);
    tap(tab(5), choiceId("##capture_actions", 1)); assert(videoStops == 1); // no repeated stop
    videoRunning = videoStopping = false;
    tap(tab(5), choiceId("##capture_actions", 1)); assert(videoStarts == 2);
    assert(lastVideoOptions.bitrate_mbps == 16);
    tap(tab(5), choiceId("##capture_bitrate", 1)); assert(g_state.capture_video.bitrate_mbps == 4);
    tap(tab(5), choiceId("##capture_bitrate", 0)); assert(g_state.capture_video.bitrate_mbps == 0);
    tap(tab(5), id("##lang1")); assert(lang::english());
    frame(tab(5)); assert(items.count(id("##ud2"))); // expansion survives localization
    for (const auto& entry : {std::pair{"Запись экрана", "Screen recording"},
                              std::pair{"Оверлей в захвате", "Overlay in capture"},
                              std::pair{"Битрейт, Мбит/с", "Bitrate, Mbps"},
                              std::pair{"Авто", "Auto"},
                              std::pair{"Скриншот", "Screenshot"},
                              std::pair{"Начать запись", "Start recording"},
                              std::pair{"Остановить запись", "Stop recording"},
                              std::pair{"Ошибка захвата|см. capture.log", "Capture failed|see capture.log"}})
        assert(std::strcmp(lang::text(entry.first), entry.second) == 0);
    tap(tab(5), id("##capture_overlay")); assert(g_state.streamer_mode);
    tap(tab(5), id("##hud_cnt")); assert(g_calibMode == 5);
    g_calibMode = 0;
    tap(tab(5), id("##pstyle1")); assert(!g_state.ui_panel_left);
    tap(tab(5), id("##exit")); assert(g_pop.visible && g_pop.sectionId == 2);
}

static void cullingTests() {
    fresh("culling");
    bool enabled = false;
    float value = 25.f, toggleAnimation = 0.f;
    AppState::SliderAnim animation;
    ImVec4 color = {1.f, 0.f, 0.f, 1.f};
    const char* const labels[] = {"one", "two", "three"};
    int selected = 0;
    auto draw = [&] {
        if (SettingsHeader("group", "settings", nullptr, true)) {
            CardBg(Layout::RowH * 41.f + Layout::SliderH + Layout::BtnH + 8.f);
            for (int i = 0; i < 40; ++i) {
                ImGui::PushID(i);
                ToggleRow("toggle", "row", &enabled, toggleAnimation);
                ImGui::PopID();
            }
            ColorRow("color", "color", &color);
            SliderRow("slider", "slider", &value, 0.f, 100.f, "%.0f", true, true, animation, 1.f / 60.f);
            ChoiceRow("choice", &selected, labels, 3);
        }
    };
    const float fullHeight = frame(draw);
    const int fullVertices = ImGui::GetDrawData()->TotalVtxCount;
    viewportClip = 450.f;
    const float clippedHeight = frame(draw);
    assert(std::fabs(fullHeight - clippedHeight) < .01f);
    assert(ImGui::GetDrawData()->TotalVtxCount < fullVertices / 3);
    assert(!enabled && value == 25.f && selected == 0);
    // A clipped expanded header must still lay out its entire body. The
    // slider's animation/state updates must not depend on GPU visibility.
    animation = {};
    const float offscreenHeight = frame([&] { ImGui::SetCursorPosY(1000.f); draw(); });
    assert(std::fabs(offscreenHeight - 1000.f - fullHeight) < .01f);
    assert(animation.pos >= 0.f && !enabled);
    viewportClip = 0.f;
    assert(std::fabs(frame(draw) - fullHeight) < .01f);

    fresh("font-probes");
    frame([&] {
        MeasureUiTextScaled("Конфиги", ImGui::GetFontSize());
        const int before = ImGui::GetIO().Fonts->Builder->BakedPool.Size;
        for (int size = 8; size <= 90; ++size) {
            const auto measured = MeasureUiTextScaled("Конфиги", float(size));
            assert(measured.x > 0.f && measured.y > 0.f);
        }
        assert(ImGui::GetIO().Fonts->Builder->BakedPool.Size == before);
    });
}

static void benchmark() {
    fresh("benchmark");
    width = 650.f; viewportClip = 450.f;
    frame(tab(3));
    for (auto group : {"##misc_fov", "##misc_hands", "##misc_freecam", "##misc_xray", "##misc_weapon",
                       "##misc_farm", "##misc_world", "##misc_fakelag", "##misc_fps"}) {
        auto* window = ImGui::FindWindowByName(windowName.c_str());
        window->StateStorage.SetBool(ImHashStr("expanded", 0, id(group)), true);
    }
    ImGui::GetCurrentContext()->TestEngineHookItems = false;
    for (int i = 0; i < 20; ++i) frame(tab(3));
    std::vector<double> times;
    for (int i = 0; i < 500; ++i) {
        const auto start = std::chrono::steady_clock::now();
        frame(tab(3));
        times.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
    }
    std::sort(times.begin(), times.end());
    auto* draw = ImGui::GetDrawData();
    std::printf("host CPU, expanded misc / 450 px clip: median %.1f us, p95 %.1f us, %d vertices, %d indices, %d lists\n",
                times[250], times[475], draw->TotalVtxCount, draw->TotalIdxCount, draw->CmdListsCount);
}

int main(int argc, char** argv) {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.IniFilename = nullptr;
    ImFontConfig config; config.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF((void*)RobotoFont, RobotoFont_size, 28.f, &config, io.Fonts->GetGlyphRangesCyrillic());
    unsigned char* pixels; int w, h; io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    ImGui::GetCurrentContext()->TestEngineHookItems = true;
    ApplyTheme();
    if (argc > 1 && std::strcmp(argv[1], "--bench") == 0) {
        benchmark(); ImGui::DestroyContext(); return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "--measure") == 0) {
        for (int i = 1; i <= 5; ++i) {
            fresh(("measure" + std::to_string(i)).c_str());
            std::printf("tab %d: %.0f px default content height\n", i, frame(tab(i)));
        }
        ImGui::DestroyContext();
        return 0;
    }
    widgetTests(); aimTests(); espTests(); miscTests(); configAndOptionsTests(); cullingTests();
    g_pop = {}; g_sheet = {}; g_calibMode = 0;
    const char* const expandedGroups[] = {
        "##aim_target", "##aim_filters", "##aim_tuning", "##aim_touch_settings",
        "##esp_players", "##esp_arrows", "##esp_radar", "##esp_ore", "##esp_entities", "##esp_loot", "##esp_distance",
        "##misc_fov", "##misc_hands", "##misc_freecam", "##misc_xray", "##misc_weapon", "##misc_farm", "##misc_world", "##misc_fakelag", "##misc_fps",
        "##options_capture", "##options_language", "##options_appearance", "##options_hud", "##options_tabs"};
    auto* window = ImGui::FindWindowByName(windowName.c_str());
    for (const char* group : expandedGroups)
        window->StateStorage.SetBool(ImHashStr("expanded", 0, id(group)), true);
    // Real menu layout on both narrow (left rail) and wide (bottom rail)
    // content widths, in both supported languages and themes, all tabs.
    for (float size : {464.f, 840.f}) for (auto language : {lang::LANG_RU, lang::LANG_EN}) for (bool dark : {false, true}) {
        width = size; g_darkTheme = dark; g_themeT = dark ? 1.f : 0.f;
        lang::set(language);
        for (int i = 1; i <= 5; ++i) {
            frame(tab(i));
            for (const auto& item : items) {
                assert(std::isfinite(item.second.Min.x) && std::isfinite(item.second.Max.y));
                assert(item.second.Min.x >= 0.f && item.second.Max.x <= width + 1.f);
            }
        }
    }
    // ToggleRow clips instead of scaling text; keep the capture switch label
    // readable at the narrow tested content width in both languages.
    fresh("capture-label-width"); width = 464.f;
    frame([&] {
        const float available = width - 2.f * (Layout::Inset + Layout::PadX) - 72.f - 16.f;
        for (const char* label : {"Оверлей в захвате", "Overlay in capture"}) {
            const float measured = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize() * 1.15f,
                                                                   FLT_MAX, 0.f, label).x;
            assert(measured <= available);
        }
    });
    ImGui::DestroyContext();
    std::puts("PASS: compact widgets, all five tabs, feature controls, calibration actions, profiles, localization, clipping, scroll bounds");
    return 0;
}
