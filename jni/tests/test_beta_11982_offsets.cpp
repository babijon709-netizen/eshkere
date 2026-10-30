// Beta 11982 offsets from dumpbeta11982.7z (dump.cs + script.json).
// The release 11978 profile must remain unchanged across runtime switches.
#include "game_offsets_active.h"
#include "maps_lookup.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

namespace {
using go_active::GameVersion;

template<class T, std::size_t N>
void put(std::array<unsigned char, N>& data, std::size_t offset, T value) {
    assert(offset + sizeof(value) <= data.size());
    std::memcpy(data.data() + offset, &value, sizeof(value));
}
template<class T, std::size_t N>
T get(const std::array<unsigned char, N>& data, std::size_t offset) {
    assert(offset + sizeof(T) <= data.size());
    T value{};
    std::memcpy(&value, data.data() + offset, sizeof(value));
    return value;
}

void expect_release() {
    assert(go_active::selected_version == GameVersion::Release);
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA == 0xE9126C8);
    assert(go_active::PLAYER_LOOP_HELPER_TYPEINFO_RVA == 0xE912690);
    assert(go_active::GAME_CONTROLLER_TYPEINFO_RVA == 0xE90D9A8);
    assert(go_active::NETWORK_CLIENT_TYPEINFO_RVA == 0xE9119D8);
    assert(go_active::NETWORK_SERVER_TYPEINFO_RVA == 0xE911AA0);
    assert(go_active::NETWORK_CLIENT_STATICS_RVA == 0);
    assert(go_active::NETWORK_TIME_TYPEINFO_RVA == 0xE911AB8);
    assert(go_active::WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA == 0xE918AC0);
    assert(go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA == 0xE922C70);
    assert(go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA_ALT == 0);
    assert(go_active::HMJ_TYPEINFO_RVA == 0xE917638);
    assert(go_active::TOD_SCAN_RVA_BEGIN == 0xE910000);
    assert(go_active::TOD_SCAN_RVA_END == 0xE924000);
    assert(go_active::PLAYER_TRANSFORM == 0xA8);
    assert(go_active::PLAYER_POSITION == 0x234);
    assert(go_active::PLAYER_CHARACTER_MODEL == 0x190);
    assert(go_active::PLAYER_NICKLABEL == 0x170);
    assert(go_active::PLAYER_DISPLAY_NAME == 0x280);
    assert(go_active::PLAYER_VOICE_STATE == 0x378);
    assert(go_active::PLAYER_VOICE_PLAYER == 0x180);
    assert(go_active::PLAYER_EVENT_HANDLER == 0xB8);
    assert(go_active::PLAYER_MANAGER_RESPAWNING == 0x268);
    assert(go_active::PLAYER_DEATH_HANDLER == 0xC0);
    assert(go_active::PLAYER_VITALS == 0x108);
    assert(go_active::PLAYER_UKX == 0x2A0);
    assert(go_active::PLAYER_MODEL_INFO_REF == 0x2A0);
    assert(go_active::PLAYER_INVENTORY == 0xD8);
    assert(go_active::PLAYER_WEAPON_REFERENCE == 0x130);
    assert(go_active::PLAYER_WEAPONS_ARRAY == 0x1F8);
    assert(go_active::PLAYERWEAPON_VIEW == 0x120);
    assert(go_active::PLAYERWEAPON_PIECE == 0x150);
    assert(go_active::PLAYERWEAPON_PLAYER_BACKREF == 0x180);
    assert(go_active::PLAYER_FP_MANAGER == 0xD0);
    assert(go_active::PLAYER_MOUSE_LOOK == 0xB0);
    assert(go_active::PLAYER_RAYCAST_MANAGER == 0xC8);
    assert(go_active::PLAYER_VOICE_COLLIDER == 0x1A8);
    assert(go_active::PLAYER_TRIGGER_PROBE == 0x1B0);
    assert(go_active::PLAYER_AIM_ASSIST_CONFIG == 0x1C0);
    assert(go_active::PLAYER_AIM_ASSIST == 0x298);
    assert(go_active::PLAYER_HIT_TRACKER == 0x398);
    assert(go_active::PLAYER_KCC_REFERENCE == 0xF0);
    assert(go_active::PLAYER_USER_ID == 0x2E8);
    assert(go_active::PLAYER_TEAM_NAME == 0x2F0);
    assert(go_active::PLAYER_VEHICLE_ID == 0x2F8);
    assert(go_active::PLAYER_SEAT_ID == 0x2FC);
    assert(go_active::PLAYER_CLAN_ID == 0x300);
    assert(go_active::PLAYER_CLAN_TAG == 0x308);
    assert(go_active::PLAYER_ZIPLINE_ID == 0x31C);
    assert(go_active::KCC_MOVE == 0x1B4);
    assert(go_active::KCC_VISIBILITY == 0x310);
    assert(go_active::KCC_MOTOR_ENABLED == 0x315);
    assert(go_active::KCC_HAS_OBSERVER_MOVE == 0x3C8);
    assert(go_active::FPWEAPON_TYPE == 0xD8);
}

void expect_beta() {
    assert(go_active::selected_version == GameVersion::Beta);
    // Inner TypeInfo slots, not outer metadata-usage wrappers.
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA == 0xE8F7750);
    assert(go_active::PLAYER_LOOP_HELPER_TYPEINFO_RVA == 0xE8F7718);
    assert(go_active::GAME_CONTROLLER_TYPEINFO_RVA == 0xE8F2990);
    assert(go_active::NETWORK_CLIENT_TYPEINFO_RVA == 0xE8F6A20);
    assert(go_active::WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA == 0xE8FC318);
    assert(game_offsets_beta::UNITY_CAMERA_TYPEINFO_RVA == 0xE8EFB70);
    assert(go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA == 0xE906EE0);
    assert(go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA_ALT == 0); // <>c is not a promise
    assert(go_active::FASTLOOT_IGNOREDELAY_TYPEINFO_RVA == 0xE906ED8);
    assert(go_active::FASTLOOT_IGNOREDELAY_TYPEINFO_RVA_ALT == 0); // <>c is not a promise
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA == 0xE8F7750);
    assert(go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA - go_active::FASTLOOT_IGNOREDELAY_TYPEINFO_RVA == 8);
    // Getter disassembly and relocations resolve these inner TypeInfo slots.
    assert(go_active::NETWORK_SERVER_TYPEINFO_RVA == 0xE8F6AE8);
    assert(go_active::NETWORK_CLIENT_STATICS_RVA == 0); // use the direct client slot
    assert(go_active::NETWORK_TIME_TYPEINFO_RVA == 0xE8F6B00);
    assert(go_active::NETWORK_CLIENT_BUFFER_MULTIPLIER == 0x108);
    assert(go_active::NETWORK_SERVER_TICK_RATE == 0x8);
    assert(go_active::NETWORK_TIME_RTT == 0x20);
    assert(go_active::TOD_SCAN_RVA_BEGIN == 0xE8F0000);
    assert(go_active::TOD_SCAN_RVA_END == 0xE910000);
    assert(go_active::HMJ_TYPEINFO_RVA == 0xE8FF480); // tzc, runtime signature required
    constexpr std::uint64_t kTodSkyTypeInfo = 0xE8FF6D8; // yY, runtime signature required
    for (std::uint64_t rva : {go_active::HMJ_TYPEINFO_RVA, kTodSkyTypeInfo,
                              go_active::PLAYER_MANAGER_TYPEINFO_RVA,
                              go_active::GAME_CONTROLLER_TYPEINFO_RVA,
                              go_active::NETWORK_CLIENT_TYPEINFO_RVA,
                              go_active::WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA,
                              go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA}) {
        assert(rva >= go_active::TOD_SCAN_RVA_BEGIN && rva < go_active::TOD_SCAN_RVA_END);
    }
    assert(go_active::TOD_SKY_CYCLE == 0x40);
    assert(go_active::TOD_CYCLE_HOUR == 0x10);
    assert(go_active::TOD_CYCLE_DAY == 0x14);
    assert(go_active::WORLD_STREAMING_GRASS_ENABLED_FIELD == 0x18);
    assert(go_active::WORLD_STREAMING_TREES_ENABLED_FIELD == 0x19);

    // The parachute block shifts the established PlayerManager fields by 0x40.
    assert(go_active::PLAYER_TRANSFORM == 0xA8);
    assert(go_active::PLAYER_POSITION == 0x234); // lastSavedPosition; not lastDeathPosition 0x240
    assert(go_active::PLAYER_CHARACTER_MODEL == 0x190);
    assert(go_active::PLAYER_NICKLABEL == 0x170);
    assert(go_active::PLAYER_DISPLAY_NAME == 0x280);
    assert(go_active::PLAYER_VOICE_STATE == 0x378);
    assert(go_active::PLAYER_VOICE_PLAYER == 0x180);
    assert(go_active::PLAYER_EVENT_HANDLER == 0xB8);
    assert(go_active::PLAYER_MANAGER_RESPAWNING == 0x268);
    assert(go_active::PLAYER_DEATH_HANDLER == 0xC0);
    assert(go_active::PLAYER_VITALS == 0x108);
    assert(go_active::PLAYER_UKX == 0x2A0);
    assert(go_active::PLAYER_MODEL_INFO_REF == 0x2A0);
    assert(go_active::UKX_MODEL_INFO == 0x10);
    assert(go_active::PLAYER_INVENTORY == 0xD8);
    assert(go_active::PLAYER_WEAPON_REFERENCE == 0x130);
    assert(go_active::PLAYER_WEAPONS_ARRAY == 0x1F8);
    assert(go_active::PLAYER_FP_MANAGER == 0xD0);
    assert(go_active::PLAYER_MOUSE_LOOK == 0xB0);
    assert(go_active::PLAYER_RAYCAST_MANAGER == 0xC8);
    assert(go_active::PLAYER_VOICE_COLLIDER == 0x1A8);
    assert(go_active::PLAYER_TRIGGER_PROBE == 0x1B0);
    assert(go_active::PLAYER_AIM_ASSIST_CONFIG == 0x1C0);
    assert(go_active::PLAYER_AIM_ASSIST == 0x298);
    assert(go_active::PLAYER_HIT_TRACKER == 0x398);
    assert(go_active::PLAYER_KCC_REFERENCE == 0xF0);
    assert(go_active::PLAYER_USER_ID == 0x2E8);
    assert(go_active::PLAYER_TEAM_NAME == 0x2F0);
    assert(go_active::PLAYER_VEHICLE_ID == 0x2F8);
    assert(go_active::PLAYER_SEAT_ID == 0x2FC);
    assert(go_active::PLAYER_CLAN_ID == 0x300);
    assert(go_active::PLAYER_CLAN_TAG == 0x308);
    assert(go_active::PLAYER_ZIPLINE_ID == 0x31C);
    assert(go_active::PLAYER_SEAT_ID - go_active::PLAYER_VEHICLE_ID == 4);
    assert(go_active::PLAYER_CLAN_TAG - go_active::PLAYER_CLAN_ID == 8);

    // PlayerWeapon has a separate +0x20 insertion, not the player shift.
    assert(go_active::PLAYERWEAPON_VIEW == 0x120);
    assert(go_active::PLAYERWEAPON_PIECE == 0x150);
    assert(go_active::PLAYERWEAPON_PLAYER_BACKREF == 0x180);
    assert(go_active::WEAPONVIEW_WEAPON_BASE == 0x50);
    assert(go_active::WEAPONVIEW_ROOT_TRANSFORM == 0x70);
    assert(go_active::WEAPONVIEW_INNER == 0x10);
    assert(go_active::FPWEAPON_TYPE == 0xD8); // 0xDC is now autoADSMode

    // Existing event, movement, weapon and model internals stay where checked.
    assert(go_active::EVENT_HANDLER_AIM_ACTIVITY == 0x2B0);
    assert(go_active::EVENT_HANDLER_LOOK_DIRECTION == 0x140);
    assert(go_active::EVENT_HANDLER_MOVEMENT_INPUT == 0x128);
    assert(go_active::EVENT_HANDLER_LAST_HIT_POINT == 0x170);
    assert(go_active::EVENT_HANDLER_LAST_HIT_TIME == 0x178);
    assert(go_active::EVENT_HANDLER_MANAGER_BACKREF == 0xD0);
    assert(go_active::EVENT_HANDLER_HEALTH == 0x98);
    assert(go_active::HIT_TRACKER_HIT_DICT == 0x18);
    assert(go_active::PLAYER_INPUT_HANDLER_SPRINT_OVERRIDE == 0x84);
    assert(go_active::KCC_MOVE == 0x1B4);
    assert(go_active::KCC_VISIBILITY == 0x310);
    assert(go_active::KCC_MOTOR_ENABLED == 0x315);
    assert(go_active::KCC_HAS_OBSERVER_MOVE == 0x3C8);
    assert(go_active::KCC_CHARACTER_ANIMATION == 0xE0);
    assert(go_active::KCC_PLAYER_BACKREF == 0x78);
    assert(go_active::KCC_HEAD_TRANSFORM == 0x88);
    assert(go_active::KCC_MOVE_REAL_VELOCITY == 0x18);
    assert(go_active::KCC_MOVE_BASE_VELOCITY == 0x38);
    assert(go_active::KCC_MOVE_STATE_AIRBORNE == 4);
    assert(go_active::KCC_MOVE_STATE_DEAD == 7);
    assert(go_active::VITALS_MIN_FALL_SPEED == 0xC0);
    assert(go_active::VITALS_MAX_FALL_SPEED == 0xC4);
    assert(go_active::VITALS_PLAYER_BACKREF == 0x100);
    assert(go_active::RAGDOLL_BONES_ARRAY == 0x90);
    assert(go_active::MODELINFO_EQUIPMENT_HOLDER == 0x38);
    assert(go_active::FPMOTION_IDLE_OFFSET == 0x40);
    assert(go_active::FPMOTION_RUN_OFFSET == 0x48);
    assert(go_active::FPMOTION_AIM_OFFSET == 0x60);
    assert(go_active::FPMOTION_LADDER_OFFSET == go_active::FPMOTION_AIM_OFFSET);
    assert(go_active::FPMOTION_JUMP_OFFSET == go_active::FPMOTION_AIM_OFFSET);
    assert(go_active::FPMOTION_OBJECT == 0xD0);
    assert(go_active::FPMOTION_PLAYER == 0x120);
    assert(go_active::FPMANAGER_CURRENT_OBJECT == 0x50);
    assert(go_active::FPMANAGER_CURRENT_WEAPON == 0x58);
    assert(go_active::FPMANAGER_WORLD_FOV_TARGET == 0xAC);
    assert(go_active::FPMANAGER_AIM_BLEND == 0xB8);
    assert(go_active::FPMELEE_MAX_REACH == 0x130);
    assert(go_active::FPTOOL_TOOL_PURPOSES == 0x168);
    assert(go_active::FPSPEAR_PROJECTILE_PREFAB == 0x178);
    assert(go_active::WEAPONPIECE_NUMBER == 0x02);
    assert(go_active::ITEMDATA_ID == 0x58);
}

void test_profiled_reads() {
    // 11993 release and 11982 beta ended up with IDENTICAL managed layouts
    // (every field offset in both profiles matches), so the profile switch is
    // only observable through the TypeInfo slots. Keep one real read through
    // go_active to prove the accessors resolve, and pin both facts.
    std::array<unsigned char, 0x500> player{};
    put<std::uint64_t>(player, 0xB0, 0x11111110);
    put<float>(player, 0x234, 24.5F);
    put<std::int32_t>(player, 0xD8, 9);

    go_active::Select(GameVersion::Release);
    const std::uint64_t rel_pm = go_active::PLAYER_MANAGER_TYPEINFO_RVA;
    assert(get<std::uint64_t>(player, go_active::PLAYER_MOUSE_LOOK) == 0x11111110);
    assert(get<float>(player, go_active::PLAYER_POSITION) == 24.5F);
    assert(get<std::int32_t>(player, go_active::FPWEAPON_TYPE) == 9);
    assert(go_active::PLAYER_MOUSE_LOOK == game_offsets_beta::PLAYER_MOUSE_LOOK);
    assert(go_active::PLAYER_POSITION == game_offsets_beta::PLAYER_POSITION);
    assert(go_active::FPWEAPON_TYPE == game_offsets_beta::FPWEAPON_TYPE);
    assert(go_active::KCC_MOVE == game_offsets_beta::KCC_MOVE);

    go_active::Select(GameVersion::Beta);
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA != rel_pm);
    assert(get<std::uint64_t>(player, go_active::PLAYER_MOUSE_LOOK) == 0x11111110);
    assert(get<float>(player, go_active::PLAYER_POSITION) == 24.5F);
    assert(get<std::int32_t>(player, go_active::FPWEAPON_TYPE) == 9);
}
} // namespace

int main() {
    expect_release();
    go_active::Select(GameVersion::Beta);
    expect_beta();
    go_active::Select(GameVersion::Release);
    expect_release();
    test_profiled_reads();
    expect_beta();
    go_active::Select(GameVersion::Release);
    expect_release();

    // Split APK mapping fallback (when extractNativeLibs="false" maps libil2cpp.so
    // directly from split_config.arm64_v8a.apk or base.apk!/lib/arm64-v8a/libil2cpp.so
    // with p_vaddr - p_offset == 0x4000 and non-zero zip entry file_offset)
    {
        const std::string apk_maps =
            "7100000000-71029e8000 r--p 01500000 fe:09 12345 /data/app/com.catsbit.oxidesurvivalisland/split_config.arm64_v8a.apk\n"
            "71029ec000-710b800000 --xp 03ee8000 fe:09 12345 /data/app/com.catsbit.oxidesurvivalisland/split_config.arm64_v8a.apk\n";
        uint64_t bases[4] = {};
        const int n = maps::lookup_library_bases(apk_maps, "libil2cpp.so", bases, 4);
        assert(n >= 1);
        assert(bases[0] == 0x7100000000ULL);

        const std::string bang_maps =
            "7200000000-72029e8000 r--p 01500000 fe:09 12345 /data/app/com.catsbit.oxidesurvivalisland/base.apk!/lib/arm64-v8a/libil2cpp.so\n"
            "72029ec000-720b800000 r-xp 03ee8000 fe:09 12345 /data/app/com.catsbit.oxidesurvivalisland/base.apk!/lib/arm64-v8a/libil2cpp.so\n"
            "720de00000-720e391000 r--p 0f2f4000 fe:09 12345 /data/app/com.catsbit.oxidesurvivalisland/base.apk!/lib/arm64-v8a/libil2cpp.so\n"
            "720e391000-720e9f5000 rw-p 0f885000 fe:09 12345 /data/app/com.catsbit.oxidesurvivalisland/base.apk!/lib/arm64-v8a/libil2cpp.so\n";
        uint64_t bang_bases[8] = {};
        const int nb = maps::lookup_library_bases(bang_maps, "libil2cpp.so", bang_bases, 8);
        assert(nb >= 1);
        assert(bang_bases[0] == 0x7200000000ULL);
        for (int i = 0; i < nb; ++i) {
            assert(bang_bases[i] != 0x720de00000ULL);
        }
    }
    return 0;
}
