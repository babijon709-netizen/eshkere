// Pin the beta 11981 profile from dumpbeta11981.7z and guard release 11978.
// TypeInfo RVAs come from script.json; managed field offsets are checked against dump.cs.
#include "game_offsets_active.h"
#include "maps_lookup.h"

#include <cassert>

int main() {
    using go_active::GameVersion;

    // --- Релизный профиль не должен пострадать ---
    assert(go_active::selected_version == GameVersion::Release);
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA == 0xE66DEE8);
    assert(go_active::PLAYER_USER_ID == 0x2A8);
    assert(go_active::EVENT_HANDLER_AIM_ACTIVITY == 0x2B0);
    assert(go_active::KCC_MOVE == 0x1AC);
    assert(go_active::FPMOTION_IDLE_OFFSET == 0x40);
    assert(go_active::PLAYERWEAPON_VIEW == 0x100);

    go_active::Select(GameVersion::Beta);

    // --- TypeInfo slots from beta 11981 script.json ---
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA == 0xE8F2D58);
    assert(go_active::PLAYER_LOOP_HELPER_TYPEINFO_RVA == 0xE8F2D20);
    assert(go_active::GAME_CONTROLLER_TYPEINFO_RVA == 0xE8EDEA0);
    assert(go_active::NETWORK_CLIENT_TYPEINFO_RVA == 0xE8F1FD8);
    assert(go_active::WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA == 0xE8F7990);
    // Primary UniTask class slots are in script.json; alternate <>c slots are
    // candidates validated by class name at runtime.
    assert(go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA == 0xE902408);
    assert(go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA_ALT == 0xE9050A0);
    assert(go_active::FASTLOOT_IGNOREDELAY_TYPEINFO_RVA == 0xE902400);
    assert(go_active::FASTLOOT_IGNOREDELAY_TYPEINFO_RVA_ALT == 0xE905098);
    assert(go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA -
               go_active::FASTLOOT_IGNOREDELAY_TYPEINFO_RVA == 8);
    assert(go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA_ALT -
               go_active::FASTLOOT_IGNOREDELAY_TYPEINFO_RVA_ALT == 8);
    // PlayerManager и PlayerLoopHelper в обеих сборках идут через 0x38.
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA -
               go_active::PLAYER_LOOP_HELPER_TYPEINFO_RVA == 0x38);

    // --- Candidate TypeInfo scan span (actual .data bounds unverified) ---
    assert(go_active::TOD_SCAN_RVA_BEGIN == 0xE8E0000);
    assert(go_active::TOD_SCAN_RVA_END == 0xE906000);
    assert(go_active::TOD_SCAN_RVA_BEGIN < go_active::TOD_SCAN_RVA_END);
    assert(go_active::HMJ_TYPEINFO_RVA == 0xE8F95F8); // qTW, TypeDefIndex 6691
    assert(go_active::HMJ_TYPEINFO_RVA > go_active::TOD_SCAN_RVA_BEGIN);
    assert(go_active::HMJ_TYPEINFO_RVA < go_active::TOD_SCAN_RVA_END);
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA > go_active::TOD_SCAN_RVA_BEGIN);
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA < go_active::TOD_SCAN_RVA_END);
    assert(go_active::WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA > go_active::TOD_SCAN_RVA_BEGIN);
    assert(go_active::WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA < go_active::TOD_SCAN_RVA_END);
    assert(go_active::WORLD_STREAMING_GRASS_ENABLED_FIELD == 0x18);
    assert(go_active::WORLD_STREAMING_TREES_ENABLED_FIELD == 0x19);
    // PlayerManager.WtE -> LL is a structural hit-tracker candidate; dictionaries
    // at +0x10/+0x18 match, while the semantic mapping is still tentative.
    assert(go_active::PLAYER_HIT_TRACKER == 0x398);
    assert(go_active::HIT_TRACKER_HIT_DICT == 0x18);
    // TOD sky Sw has List<Self> + Cycle@0x40; TypeInfo slot from script.json.
    constexpr std::uint64_t kTodSkyTypeInfoSlot = 0xE8F5908;
    assert(kTodSkyTypeInfoSlot > go_active::TOD_SCAN_RVA_BEGIN);
    assert(kTodSkyTypeInfoSlot < go_active::TOD_SCAN_RVA_END);
    assert(go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA < go_active::TOD_SCAN_RVA_END);
    // TOD_CycleParameters не обфусцован и не менялся.
    assert(go_active::TOD_SKY_CYCLE == 0x40);
    assert(go_active::TOD_CYCLE_HOUR == 0x10);
    assert(go_active::TOD_CYCLE_DAY == 0x14);

    // --- PlayerManager beta 11981 field offsets ---
    assert(go_active::PLAYER_TRANSFORM == 0xA8);
    assert(go_active::PLAYER_POSITION == 0x234); // lastSavedPosition
    assert(go_active::PLAYER_CHARACTER_MODEL == 0x190);
    assert(go_active::PLAYER_NICKLABEL == 0x170);
    assert(go_active::PLAYER_DISPLAY_NAME == 0x280); // protected string candidate
    assert(go_active::PLAYER_MOUSE_LOOK == 0xB0);
    assert(go_active::PLAYER_EVENT_HANDLER == 0xB8);
    assert(go_active::PLAYER_DEATH_HANDLER == 0xC0);
    assert(go_active::PLAYER_RAYCAST_MANAGER == 0xC8);
    assert(go_active::PLAYER_FP_MANAGER == 0xD0);
    assert(go_active::PLAYER_INVENTORY == 0xD8);
    assert(go_active::PLAYER_KCC_REFERENCE == 0xF0);
    assert(go_active::PLAYER_VITALS == 0x108);
    assert(go_active::PLAYER_WEAPON_REFERENCE == 0x130);
    assert(go_active::PLAYER_WEAPONS_ARRAY == 0x1F8);
    assert(go_active::PLAYER_MANAGER_RESPAWNING == 0x268);
    assert(go_active::PLAYER_AIM_ASSIST == 0x298);
    assert(go_active::PLAYER_UKX == 0x2A0);
    assert(go_active::PLAYER_MODEL_INFO_REF == 0x2A0);
    assert(go_active::PLAYER_INPUT_HANDLER_SPRINT_OVERRIDE == 0x84);
    assert(go_active::PLAYER_USER_ID == 0x2E8);
    assert(go_active::PLAYER_TEAM_NAME == 0x2F0);
    assert(go_active::PLAYER_VEHICLE_ID == 0x2F8);
    assert(go_active::PLAYER_SEAT_ID == 0x2FC);
    assert(go_active::PLAYER_CLAN_ID == 0x300);
    assert(go_active::PLAYER_CLAN_TAG == 0x308);
    assert(go_active::PLAYER_ZIPLINE_ID == 0x31C);
    assert(go_active::PLAYER_VOICE_PLAYER == 0x180);
    assert(go_active::PLAYER_VOICE_COLLIDER == 0x1A8);
    assert(go_active::PLAYER_TRIGGER_PROBE == 0x1B0);
    assert(go_active::PLAYER_AIM_ASSIST_CONFIG == 0x1C0);
    assert(go_active::PLAYER_VOICE_STATE == 0x378);
    assert(go_active::PLAYER_HIT_TRACKER == 0x398);
    // Порядок полей внутри блока сохранён: userID < teamName < vehicleID <
    // seatID < clanId < clanTag, и seatID сразу за vehicleID.
    assert(go_active::PLAYER_SEAT_ID - go_active::PLAYER_VEHICLE_ID == 4);
    assert(go_active::PLAYER_CLAN_TAG - go_active::PLAYER_CLAN_ID == 8);

    // --- Event-handler fields retained their verified offsets ---
    assert(go_active::EVENT_HANDLER_AIM_ACTIVITY == 0x2B0);
    assert(go_active::EVENT_HANDLER_LOOK_DIRECTION == 0x140);
    assert(go_active::EVENT_HANDLER_MOVEMENT_INPUT == 0x128);
    assert(go_active::EVENT_HANDLER_LAST_HIT_POINT == 0x170);
    assert(go_active::EVENT_HANDLER_LAST_HIT_TIME == 0x178);
    assert(go_active::EVENT_HANDLER_MANAGER_BACKREF == 0xD0);

    // --- FPMotion pose slots remain verified for beta 11981 ---
    assert(go_active::FPMOTION_IDLE_OFFSET == 0x40);
    assert(go_active::FPMOTION_RUN_OFFSET == 0x48);
    assert(go_active::FPMOTION_RUN_RELOAD_OFFSET == 0x50);
    assert(go_active::FPMOTION_ATTACK_OFFSET == 0x58);
    assert(go_active::FPMOTION_AIM_OFFSET == 0x60);
    assert(go_active::FPMOTION_LOOK_OFFSET == 0x68);
    assert(go_active::FPMOTION_CLOSE_OFFSET == 0x70);
    // m_OnLadderOffset и m_JumpOffset в 11981 не существуют; контроллер рук
    // обязан видеть в этих слотах профиль прицеливания, чтобы безопасно их
    // пропустить, а не прочитать мусор.
    assert(go_active::FPMOTION_LADDER_OFFSET == go_active::FPMOTION_AIM_OFFSET);
    assert(go_active::FPMOTION_JUMP_OFFSET == go_active::FPMOTION_AIM_OFFSET);
    // Ссылки, по которым контроллер опознаёт «свой» FPMotion.
    assert(go_active::FPMOTION_OBJECT == 0xD0);
    assert(go_active::FPMOTION_PLAYER == 0x120);
    // Oxide.TransformOffset не изменился.
    assert(go_active::TRANSFORMOFFSET_POSITION == 0x24);
    assert(go_active::TRANSFORMOFFSET_CURRENT_POSITION == 0x44);
    assert(go_active::TRANSFORMOFFSET_LERP_SPEED == 0x20);
    // Хвост FPManager (вставлен AssetReference) уехал на +0x10.
    assert(go_active::FPMANAGER_WORLD_FOV_TARGET == 0xAC);
    assert(go_active::FPMANAGER_AIM_BLEND == 0xB8);
    assert(go_active::FPMANAGER_CURRENT_OBJECT == 0x50);
    assert(go_active::FPMANAGER_CURRENT_WEAPON == 0x58);

    // --- KCC beta 11981: verified motor and primary movement fields ---
    assert(go_active::KCC_MOTOR_OFFSET == 0x68);
    assert(go_active::KCC_MOTOR_CAPSULE == 0x28);
    assert(go_active::KCC_MOVE == 0x1B4);
    assert(go_active::KCC_VISIBILITY == 0x310);
    assert(go_active::KCC_MOTOR_ENABLED == 0x315);
    assert(go_active::KCC_HAS_OBSERVER_MOVE == 0x3C8);
    assert(go_active::PLAYER_UKX == 0x2A0);
    assert(go_active::UKX_MODEL_INFO == 0x10);
    assert(go_active::EVENT_HANDLER_HEALTH == 0x98);
    assert(go_active::KCC_CHARACTER_ANIMATION == 0xE0);
    assert(go_active::KCC_HEAD_TRANSFORM == 0x88);
    assert(go_active::KCC_NORMAL_HEIGHT == 0xA0);
    assert(go_active::KCC_CROUCH_HEIGHT == 0xA4);
    assert(go_active::KCC_HITBOX_ROOT == 0x70);
    assert(go_active::KCC_LOOK_HEIGHT_OFFSET == 0x90);

    // --- Vitals and Move struct fields retained their offsets in beta 11981 ---
    assert(go_active::PLAYER_VITALS == 0x108);
    assert(go_active::VITALS_MIN_FALL_SPEED == 0xC0);
    assert(go_active::VITALS_MAX_FALL_SPEED == 0xC4);
    assert(go_active::VITALS_PLAYER_BACKREF == 0x100);
    // struct Move 4452 — те же внутренние offsets, хотя поле в KCC переехало.
    assert(go_active::KCC_MOVE_REAL_VELOCITY == 0x18);
    assert(go_active::KCC_MOVE_REAL_VELOCITY_Y - go_active::KCC_MOVE_REAL_VELOCITY == 4);
    assert(go_active::KCC_MOVE_BASE_VELOCITY_Y - go_active::KCC_MOVE_BASE_VELOCITY == 4);
    assert(go_active::KCC_MOVE_STATE_AIRBORNE == 4);
    assert(go_active::KCC_MOVE_STATE_CLAMP_MAX == 4);
    assert(go_active::KCC_MOVE_STATE_DEAD == 7);

    // --- Ragdoll: вставлен bool m_Interpolate перед m_SurfaceTexture ---
    assert(go_active::RAGDOLL_BONES_ARRAY == 0x90);
    assert(go_active::RAGDOLL_PELVIS_RIGIDBODY == 0x20);
    assert(go_active::RAGDOLL_BODYPART_TRANSFORM == 0x10);

    // --- PlayerWeapon beta 11981 and third-person view members ---
    assert(go_active::PLAYERWEAPON_VIEW == 0x120);
    assert(go_active::PLAYERWEAPON_PIECE == 0x150);
    assert(go_active::PLAYERWEAPON_STATE == 0x16C);
    assert(go_active::PLAYERWEAPON_PLAYER_BACKREF == 0x180);
    assert(go_active::WEAPONVIEW_WEAPON_BASE == 0x50);
    assert(go_active::WEAPONVIEW_PIECE == 0x58);
    assert(go_active::WEAPONVIEW_ROOT_TRANSFORM == 0x70);
    assert(go_active::WEAPONVIEW_INNER == 0x10);

    // --- Melee/tool/spear offsets remain shared with release in beta 11981 ---
    assert(go_active::FPMELEE_MAX_REACH == 0x130);
    assert(go_active::FPMELEE_HIT_RADIUS == 0x134);
    assert(go_active::FPMELEE_TIME_BETWEEN_ATTACKS == 0x138);
    assert(go_active::FPMELEE_PAUSE_AFTER_ATTACK == 0x13C);
    assert(go_active::FPTOOL_TOOL_PURPOSES == 0x168);
    assert(go_active::FPSPEAR_PROJECTILE_PREFAB == 0x178);
    assert(go_active::FPSPEAR_MIN_THROW_PERIOD == 0x170);
    assert(go_active::FPSPEAR_CACHED_THROW_PERIOD == 0x1C0);

    // --- Возврат в релизный профиль обязан восстановить всё ---
    go_active::Select(GameVersion::Release);
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA == 0xE66DEE8);
    assert(go_active::PLAYER_LOOP_HELPER_TYPEINFO_RVA == 0xE66DEB0);
    assert(go_active::GAME_CONTROLLER_TYPEINFO_RVA == 0xE669190);
    assert(go_active::NETWORK_CLIENT_TYPEINFO_RVA == 0xE66D238);
    assert(go_active::WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA == 0xE672A10);
    assert(go_active::PLAYER_USER_ID == 0x2A8);
    assert(go_active::PLAYER_UKX == 0x260);
    assert(go_active::UKX_MODEL_INFO == 0x10);
    assert(go_active::EVENT_HANDLER_AIM_ACTIVITY == 0x2B0);
    assert(go_active::EVENT_HANDLER_HEALTH == 0x98);
    assert(go_active::KCC_MOVE == 0x1AC);
    assert(go_active::KCC_VISIBILITY == 0x308);
    assert(go_active::KCC_MOTOR_ENABLED == 0x30D);
    assert(go_active::KCC_HAS_OBSERVER_MOVE == 0x3C0);
    assert(go_active::VITALS_MIN_FALL_SPEED == 0xC0);
    assert(go_active::KCC_MOVE_BASE_VELOCITY_Y == 0x3C);
    assert(go_active::FPMOTION_IDLE_OFFSET == 0x40);
    assert(go_active::FPMANAGER_WORLD_FOV_TARGET == 0xAC);
    assert(go_active::PLAYERWEAPON_VIEW == 0x100);
    assert(go_active::RAGDOLL_BONES_ARRAY == 0x90);

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
