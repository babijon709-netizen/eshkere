#include "overlay_capture.h"

#include <cassert>
#include <cstdint>

int main() {
    using namespace overlay_capture;
    static_assert(kCaptureMask == 0xc0u);
    static_assert(flags_for_visible(true) == 0u);
    static_assert(flags_for_visible(false) == 0x40u);

    // Old configs, with or without the extended trailer, have this bit clear.
    assert(visible_from_config(0u));
    constexpr uint32_t other_flags = (1u << 0) | (1u << 8) | (1u << 16);
    assert(visible_from_config(other_flags | config_flag(true)));
    assert(!visible_from_config(other_flags | config_flag(false)));
    assert((config_flag(false) & other_flags) == 0u);

    Mode mode;
    int updates = 0, recreations = 0;
    bool update_success = true, recreate_success = true;
    bool last_target = true;
    auto update = [&](bool target) { ++updates; last_target = target; return update_success; };
    auto recreate = [&](bool target) { ++recreations; last_target = target; return recreate_success; };

    assert(mode.visible());
    assert(mode.set(true, update, recreate));
    assert(updates == 0 && recreations == 0); // unchanged: no compositor work
    assert(mode.set(false, update, recreate));
    assert(!mode.visible() && !last_target && updates == 1 && recreations == 0);
    update_success = false;
    assert(mode.set(true, update, recreate)); // fallback for Android 8–11 / OEMs
    assert(mode.visible() && last_target && updates == 2 && recreations == 1);
    recreate_success = false;
    assert(!mode.set(false, update, recreate));
    assert(mode.visible() && !last_target && updates == 3 && recreations == 2);
    recreate_success = true;
    assert(mode.set(false, update, recreate)); // retry after a failed switch
    assert(!mode.visible() && updates == 4 && recreations == 3);
    mode.reset();
    assert(mode.visible());
}
