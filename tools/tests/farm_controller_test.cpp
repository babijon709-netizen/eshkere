// Actual UpdateFarm controller with fake game snapshots and input writes.
#include "main.h"
#include "app_state.h"
#include "farm.h"
#include "farm_joy.h"
#include "game.h"
#include "esp_draw.h"
#include "runtime_guard.h"
#include <cassert>
#include <cstdio>
#include <vector>

AppState g_state;
pid_t g_target_pid = 100;
HostDisplayInfo displayInfo;
int native_window_screen_x = 1920, native_window_screen_y = 1080;
bool g_esp_attached = true, s_fingerDown = false, g_aimActive = false;
int g_calibMode = 0;
static FarmTarget node;
static unsigned long long last_preferred = 0;
static std::vector<unsigned long long> blacklisted;
static bool byte_held = false, fail_press = false;
static int attacks_on = 0, attacks_off = 0, releases = 0;
static int load_calls = 0;

const std::vector<EspBox>& FrameBoxes(float, float) { static std::vector<EspBox> boxes; return boxes; }
void esp_farm_set_resources(unsigned) {}
void esp_farm_set_range(float) {}
void esp_farm_debug(int& count, int& reason) { count = 1; reason = 0; }
void esp_farm_tool_info(int& have, int& need) { have = need = 0; }
bool esp_farm_get_target(FarmTarget& out, unsigned long long preferred) {
    last_preferred = preferred; out = node; return node.valid;
}
bool esp_silent_target(int) { return false; }
bool esp_silent_read(uint64_t, void*, size_t) { return false; }
bool esp_silent_write(uint64_t, const void*, size_t) { return false; }
void esp_farm_blacklist(unsigned long long id, float) { blacklisted.push_back(id); }
bool esp_mem_aim_look_params(float&, bool&, bool&) { return false; }
bool esp_mem_aim_read_angles(float&, float&) { return false; }
bool esp_mem_aim_write_angles(float, float) { return false; }
namespace farmjoy {
void tick(float) {}
bool command(float, float) { return true; }
bool sprint(bool) { return true; }
void observe(float, bool) {}
bool attack(bool on) {
    if (on) { ++attacks_on; if (fail_press) return false; }
    else ++attacks_off;
    byte_held = on;
    return true;
}
void release() { ++releases; byte_held = false; }
}
// on_screen=false in this rig, so rendering functions are never called.
ImDrawList* ImGui::GetForegroundDrawList() { return nullptr; }
void ImDrawList::AddCircle(const ImVec2&, float, ImU32, int, float) {}
void ImDrawList::AddCircleFilled(const ImVec2&, float, ImU32, int) {}
void ImDrawList::AddLine(const ImVec2&, const ImVec2&, ImU32, float) {}

static void step(int n = 1) { for (int i = 0; i < n; ++i) UpdateFarm(0.016f); }

int main() {
    g_state.farm_on = true;
    g_state.farm_wood = true;
    node.valid = true;
    node.id = 0x100;
    node.kind = 1;
    node.node_health = 0.1f;
    node.node_health_max = 1000.f;
    node.fraction = 0.0001f; // below the old false-depletion cutoff
    node.aim_dist = node.aim_3d = 0.5f;
    node.walk_dist = 0.5f;
    node.melee_reach = 2.f;
    node.ray_valid = node.ray_self = true;
    node.ray_distance = 0.5f;
    node.continuous_tool = true;
    step(3);
    assert(g_farmPhase == 3 && byte_held && attacks_on > 0);
    assert(last_preferred == node.id && blacklisted.empty());

    // A prior self ray disappears for far longer than 0.25 s while HP stays
    // positive. Stop immediately, never declare the node harvested, and resume
    // when ownership comes back.
    node.ray_valid = node.ray_self = false;
    const int calls = attacks_on;
    step();
    assert(!byte_held);
    step(40);
    assert(attacks_on == calls && blacklisted.empty());
    node.ray_valid = node.ray_self = true;
    step();
    assert(byte_held && attacks_on > calls && blacklisted.empty());

    node.ray_self = false;
    node.ray_foreign = true;
    step();
    assert(!byte_held && blacklisted.empty());
    const int calls_foreign = attacks_on;
    step(5);
    assert(attacks_on == calls_foreign);
    node.ray_foreign = false;
    node.ray_self = true;
    step();
    assert(byte_held);

    // A movement release also clears the attack byte; the same frame must
    // refresh it. Keep the target in range while moving in/out of pressAt.
    node.aim_3d = node.aim_dist = node.ray_distance = 2.2f;
    node.walk_dist = 2.2f;
    step(3);
    node.aim_3d = node.aim_dist = node.ray_distance = 0.5f;
    node.walk_dist = 0.5f;
    const int release_before = releases;
    step(20);
    assert(releases > release_before && byte_held);

    // Normal pulsed attacks retry a failed initial write and stop immediately
    // when the next ray is foreign (rather than waiting for hold timeout).
    g_state.farm_on = false;
    step();
    assert(!byte_held);
    g_state.farm_on = true;
    node.continuous_tool = false;
    fail_press = true;
    step();
    assert(!byte_held);
    fail_press = false;
    step();
    assert(byte_held);
    node.ray_self = false;
    node.ray_foreign = true;
    step();
    assert(!byte_held && blacklisted.empty());
    g_state.farm_on = false;
    step();

    // Appdome preflight: a mapped libloader.so pauses both controllers and
    // refuses Magic injection. Clearing the snapshot resumes the normal path.
    node.ray_foreign = false;
    node.ray_self = node.ray_valid = true;
    node.continuous_tool = true;
    g_state.farm_on = true;
    runtime_guard::reset();
    step(2);
    assert(byte_held);
    const int releases_before = releases;
    runtime_guard::observe_maps(g_target_pid,
        "7000000000-7000001000 r-xp 00000000 fd:00 123 /data/app/lib/arm64/libloader.so\n");
    step(3);
    assert(g_farmProtected && g_farmPaused && !byte_held);
    assert(releases > releases_before);
    runtime_guard::observe_maps(g_target_pid,
        "7000000000-7000001000 r-xp 00000000 fd:00 123 /data/app/lib/arm64/libil2cpp.so\n");
    step(3);
    assert(!g_farmProtected && byte_held);
    runtime_guard::reset();
    std::puts("farm controller ray loss, remaining HP, attack safety and Appdome guard: PASS");
}
