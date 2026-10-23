// Pins the release 11978 offsets extracted from dump11978.7z.
#include "game_offsets_active.h"

#include <cassert>
#include <cstdint>

int main() {
    using go_active::GameVersion;

    assert(go_active::selected_version == GameVersion::Release);

    // TypeInfoPointers entries are inner .data slots (one dereference).
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA == 0xE66DEE8);
    assert(go_active::PLAYER_LOOP_HELPER_TYPEINFO_RVA == 0xE66DEB0);
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA -
               go_active::PLAYER_LOOP_HELPER_TYPEINFO_RVA == 0x38);
    assert(go_active::GAME_CONTROLLER_TYPEINFO_RVA == 0xE669190);
    assert(go_active::NETWORK_CLIENT_TYPEINFO_RVA == 0xE66D238);
    assert(go_active::WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA == 0xE672A10);
    assert(go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA == 0xE67D4E8);
    assert(go_active::FASTLOOT_IGNOREDELAY_TYPEINFO_RVA == 0xE67D4E0);
    assert(go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA_ALT == 0);
    assert(go_active::FASTLOOT_IGNOREDELAY_TYPEINFO_RVA_ALT == 0);

    // The TOD/layer-mask scan stays inside this build's .data and includes the
    // TOD_Sky, LayerMask candidate, and nested UniTask delay-promise TypeInfos.
    assert(go_active::TOD_SCAN_RVA_BEGIN == 0xE660000);
    assert(go_active::TOD_SCAN_RVA_END == 0xE690000);
    assert(go_active::TOD_SCAN_RVA_BEGIN >= 0xE3D7800);
    assert(go_active::TOD_SCAN_RVA_END <= 0xEA3C4E0);
    constexpr std::uint64_t kTodSkyTypeInfo = 0xE66E548;  // QA
    constexpr std::uint64_t kLayerMaskTypeInfo = 0xE674BE8; // kiQ
    constexpr std::uint64_t kDelayPromiseCTypeInfo = 0xE6800B0;
    assert(kTodSkyTypeInfo >= go_active::TOD_SCAN_RVA_BEGIN &&
           kTodSkyTypeInfo < go_active::TOD_SCAN_RVA_END);
    assert(kLayerMaskTypeInfo >= go_active::TOD_SCAN_RVA_BEGIN &&
           kLayerMaskTypeInfo < go_active::TOD_SCAN_RVA_END);
    assert(kDelayPromiseCTypeInfo >= go_active::TOD_SCAN_RVA_BEGIN &&
           kDelayPromiseCTypeInfo < go_active::TOD_SCAN_RVA_END);
    assert(go_active::HMJ_TYPEINFO_RVA == kLayerMaskTypeInfo);
    assert(go_active::TOD_SKY_CYCLE == 0x40);
    assert(go_active::TOD_CYCLE_HOUR == 0x10);
    assert(go_active::TOD_CYCLE_DAY == 0x14);
    assert(go_active::TOD_CYCLE_MONTH == 0x18);
    assert(go_active::TOD_CYCLE_YEAR == 0x1C);

    // PlayerManager and the event handler's moved fields.
    assert(go_active::PLAYER_POSITION == 0x1F4);
    assert(go_active::PLAYER_NICKLABEL == 0x130);
    assert(go_active::PLAYER_DISPLAY_NAME == 0x240);
    assert(go_active::NICKLABEL_PLAYER_BACKREF == 0x20);
    assert(go_active::NICKLABEL_NICKNAME_TEXT == 0x38);
    assert(go_active::PLAYER_USER_ID == 0x2A8);
    assert(go_active::PLAYER_TEAM_NAME == 0x2B0);
    assert(go_active::PLAYER_VEHICLE_ID == 0x2B8);
    assert(go_active::PLAYER_SEAT_ID == 0x2BC);
    assert(go_active::PLAYER_CLAN_ID == 0x2C0);
    assert(go_active::PLAYER_CLAN_TAG == 0x2C8);
    assert(go_active::PLAYER_ZIPLINE_ID == 0x2DC);
    assert(go_active::PLAYER_VOICE_STATE == 0x338);
    assert(go_active::VOICE_STATE_NAME == 0x38);
    assert(go_active::PLAYER_VOICE_PLAYER == 0x140);
    assert(go_active::VOICE_PLAYER_TAG == 0x78);
    assert(go_active::PLAYER_HIT_TRACKER == 0x358);
    assert(go_active::HIT_TRACKER_HIT_DICT == 0x18);
    assert(go_active::PLAYER_INPUT_HANDLER_SPRINT_OVERRIDE == 0x84);
    assert(go_active::PLAYER_INPUT_HANDLER_INPUT_ACTIONS == 0x98);
    assert(go_active::PLAYER_INPUT_SPRINT == 0x59);
    assert(go_active::LD_SPRINT_ACTION == 0x48);
    assert(go_active::INPUT_ACTION_INDEX == 0xC0);
    assert(go_active::INPUT_ACTION_STATE_HOLDER == 0xC8);
    assert(go_active::INPUT_ACTION_STATE_STRIDE == 0x44);
    assert(go_active::EVENT_HANDLER_MANAGER_BACKREF == 0xD0);
    assert(go_active::EVENT_HANDLER_AIM_ACTIVITY == 0x2B0);
    assert(go_active::EVENT_HANDLER_MOVEMENT_INPUT == 0x128);
    assert(go_active::EVENT_HANDLER_LOOK_DIRECTION == 0x140);
    assert(go_active::EVENT_HANDLER_LAST_HIT_POINT == 0x170);
    assert(go_active::EVENT_HANDLER_LAST_HIT_TIME == 0x178);

    // First-person manager/motion fields and non-ADS transform targets.
    assert(go_active::FPMANAGER_CURRENT_OBJECT == 0x50);
    assert(go_active::FPMANAGER_CURRENT_WEAPON == 0x58);
    assert(go_active::FPMANAGER_WORLD_FOV_TARGET == 0xAC);
    assert(go_active::FPMANAGER_AIM_BLEND == 0xB8);
    assert(go_active::FPMOTION_OBJECT == 0xD0);
    assert(go_active::FPMOTION_PLAYER == 0x120);
    assert(go_active::FPMOTION_IDLE_OFFSET == 0x40);
    assert(go_active::FPMOTION_RUN_OFFSET == 0x48);
    assert(go_active::FPMOTION_RUN_RELOAD_OFFSET == 0x50);
    assert(go_active::FPMOTION_ATTACK_OFFSET == 0x58);
    assert(go_active::FPMOTION_AIM_OFFSET == 0x60);
    assert(go_active::FPMOTION_LADDER_OFFSET == go_active::FPMOTION_AIM_OFFSET);
    assert(go_active::FPMOTION_JUMP_OFFSET == go_active::FPMOTION_AIM_OFFSET);
    assert(go_active::FPMOTION_LOOK_OFFSET == 0x68);
    assert(go_active::FPMOTION_CLOSE_OFFSET == 0x70);
    assert(go_active::FPMOTION_ACTIVE_OFFSET == 0xE8);

    // Remote weapon view and the moved KCC/ragdoll fields.
    assert(go_active::PLAYERWEAPON_VIEW == 0x100);
    assert(go_active::PLAYERWEAPON_PIECE == 0x130);
    assert(go_active::PLAYERWEAPON_STATE == 0x14C);
    assert(go_active::PLAYERWEAPON_PLAYER_BACKREF == 0x160);
    assert(go_active::WEAPONVIEW_WEAPON_BASE == 0x50);
    assert(go_active::WEAPONVIEW_PIECE == 0x58);
    assert(go_active::WEAPONVIEW_ROOT_TRANSFORM == 0x70);
    assert(go_active::WEAPONVIEW_INNER == 0x10);
    assert(go_active::KCC_MOVE == 0x1AC);
    assert(go_active::KCC_VISIBILITY == 0x308);
    assert(go_active::KCC_MOTOR_ENABLED == 0x30D);
    assert(go_active::KCC_HAS_OBSERVER_MOVE == 0x3C0);
    assert(go_active::RAGDOLL_BONES_ARRAY == 0x90);

    assert(go_active::WORLD_STREAMING_GRASS_ENABLED_FIELD == 0x18);
    assert(go_active::WORLD_STREAMING_TREES_ENABLED_FIELD == 0x19);
    return 0;
}
