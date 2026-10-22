// Пин beta-профиля сборки 11977 (dumpbeta11977.7z).
//
// Закрепляет TypeInfo RVAs и отличающиеся от release offsets, сверенные с
// script.json и dump.cs 11977; также проверяет, что release-профиль не меняется.
#include "game_offsets_active.h"
#include "maps_lookup.h"

#include <cassert>

int main() {
    using go_active::GameVersion;

    // --- Релизный профиль не должен пострадать ---
    assert(go_active::selected_version == GameVersion::Release);
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA == 0xDEA09F8);
    assert(go_active::PLAYER_USER_ID == 0x298);
    assert(go_active::EVENT_HANDLER_AIM_ACTIVITY == 0x298);
    assert(go_active::KCC_MOVE == 0x13C);
    assert(go_active::FPMOTION_IDLE_OFFSET == 0x78);
    assert(go_active::PLAYERWEAPON_VIEW == 0xE8);

    go_active::Select(GameVersion::Beta);

    // --- Слоты TypeInfo (внутренние .data, одно разыменование) ---
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA == 0xE647138);
    assert(go_active::PLAYER_LOOP_HELPER_TYPEINFO_RVA == 0xE647100);
    assert(go_active::GAME_CONTROLLER_TYPEINFO_RVA == 0xE640C70);
    assert(go_active::NETWORK_CLIENT_TYPEINFO_RVA == 0xE646398);
    assert(go_active::WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA == 0xE64BC38);
    // Промисы UniTask: основной класс и его вложенный <>c, шаг 8 байт.
    assert(go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA == 0xE655EB0);
    assert(go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA_ALT == 0xE657CB8);
    assert(go_active::FASTLOOT_IGNOREDELAY_TYPEINFO_RVA == 0xE655EA8);
    assert(go_active::FASTLOOT_IGNOREDELAY_TYPEINFO_RVA_ALT == 0xE657CB0);
    assert(go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA -
               go_active::FASTLOOT_IGNOREDELAY_TYPEINFO_RVA == 8);
    assert(go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA_ALT -
               go_active::FASTLOOT_IGNOREDELAY_TYPEINFO_RVA_ALT == 8);
    // PlayerManager и PlayerLoopHelper в обеих сборках идут через 0x38.
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA -
               go_active::PLAYER_LOOP_HELPER_TYPEINFO_RVA == 0x38);

    // --- Окно структурного скана .data ---
    // Накрывает beta layer-mask candidate IWt, PlayerManager, Gf (TOD_Sky),
    // WorldStreamingController и UniTask delay-promise TypeInfo slots.
    assert(go_active::TOD_SCAN_RVA_BEGIN == 0xE640000);
    assert(go_active::TOD_SCAN_RVA_END == 0xE660000);
    assert(go_active::TOD_SCAN_RVA_BEGIN < go_active::TOD_SCAN_RVA_END);
    assert(go_active::HMJ_TYPEINFO_RVA == 0xE6431D8);
    assert(go_active::HMJ_TYPEINFO_RVA > go_active::TOD_SCAN_RVA_BEGIN);
    assert(go_active::HMJ_TYPEINFO_RVA < go_active::TOD_SCAN_RVA_END);
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA > go_active::TOD_SCAN_RVA_BEGIN);
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA < go_active::TOD_SCAN_RVA_END);
    assert(go_active::WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA > go_active::TOD_SCAN_RVA_BEGIN);
    assert(go_active::WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA < go_active::TOD_SCAN_RVA_END);
    assert(go_active::WORLD_STREAMING_GRASS_ENABLED_FIELD == 0x18);
    assert(go_active::WORLD_STREAMING_TREES_ENABLED_FIELD == 0x19);
    // PlayerManager.cFm -> Yf; +0x18 remains the beta hit-map assumption.
    assert(go_active::PLAYER_HIT_TRACKER == 0x358);
    assert(go_active::HIT_TRACKER_HIT_DICT == 0x18);
    // TOD_Sky (Gf) is found by its List<Self> + Cycle@0x40 signature; script.json
    // places its TypeInfo slot inside the scan window.
    constexpr std::uint64_t kTodSkyTypeInfoSlot = 0xE640D88;
    assert(kTodSkyTypeInfoSlot > go_active::TOD_SCAN_RVA_BEGIN);
    assert(kTodSkyTypeInfoSlot < go_active::TOD_SCAN_RVA_END);
    assert(go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA < go_active::TOD_SCAN_RVA_END);
    // TOD_CycleParameters не обфусцован и не менялся.
    assert(go_active::TOD_SKY_CYCLE == 0x40);
    assert(go_active::TOD_CYCLE_HOUR == 0x10);
    assert(go_active::TOD_CYCLE_DAY == 0x14);

    // --- Хвост PlayerManager: сдвиг ровно +0x10, начиная с playerFlags ---
    // Вставлены GameObject cnN (0x270) и hp cFH (0x278). Всё, что лежало
    // ДО вставки (AimAssist 0x258, ссылка на PlayerModelInfo 0x260), стоит
    // на месте.
    assert(go_active::PLAYER_POSITION == 0x1F4);
    assert(go_active::PLAYER_INPUT_HANDLER_SPRINT_OVERRIDE == 0x84);
    assert(go_active::PLAYER_AIM_ASSIST == 0x258);
    assert(go_active::PLAYER_MODEL_INFO_REF == 0x260);
    assert(go_active::PLAYER_USER_ID == 0x2A8);
    assert(go_active::PLAYER_TEAM_NAME == 0x2B0);
    assert(go_active::PLAYER_VEHICLE_ID == 0x2B8);
    assert(go_active::PLAYER_SEAT_ID == 0x2BC);
    assert(go_active::PLAYER_CLAN_ID == 0x2C0);
    assert(go_active::PLAYER_CLAN_TAG == 0x2C8);
    assert(go_active::PLAYER_VOICE_STATE == 0x338);
    assert(go_active::PLAYER_HIT_TRACKER == 0x358);
    // Порядок полей внутри блока сохранён: userID < teamName < vehicleID <
    // seatID < clanId < clanTag, и seatID сразу за vehicleID.
    assert(go_active::PLAYER_SEAT_ID - go_active::PLAYER_VEHICLE_ID == 4);
    assert(go_active::PLAYER_CLAN_TAG - go_active::PLAYER_CLAN_ID == 8);

    // --- Хендлер событий: Footstep/StepLength/SetVehicleLock сдвинули Aim ---
    assert(go_active::EVENT_HANDLER_AIM_ACTIVITY == 0x2B0);
    assert(go_active::EVENT_HANDLER_LOOK_DIRECTION == 0x140);
    assert(go_active::EVENT_HANDLER_MOVEMENT_INPUT == 0x128);
    assert(go_active::EVENT_HANDLER_LAST_HIT_POINT == 0x170);
    assert(go_active::EVENT_HANDLER_LAST_HIT_TIME == 0x178);
    assert(go_active::EVENT_HANDLER_MANAGER_BACKREF == 0xD0);

    // --- FPMotion перестроен: позы уехали в начало класса ---
    assert(go_active::FPMOTION_IDLE_OFFSET == 0x40);
    assert(go_active::FPMOTION_RUN_OFFSET == 0x48);
    assert(go_active::FPMOTION_RUN_RELOAD_OFFSET == 0x50);
    assert(go_active::FPMOTION_ATTACK_OFFSET == 0x58);
    assert(go_active::FPMOTION_AIM_OFFSET == 0x60);
    assert(go_active::FPMOTION_LOOK_OFFSET == 0x68);
    assert(go_active::FPMOTION_CLOSE_OFFSET == 0x70);
    // m_OnLadderOffset и m_JumpOffset в 11977 не существуют; контроллер рук
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

    // --- KCC: Move стоит сразу за PlayerInput и занимает 0x6C байт ---
    assert(go_active::KCC_MOVE == 0x1AC);
    assert(go_active::KCC_VISIBILITY == 0x308);
    assert(go_active::KCC_MOTOR_ENABLED == 0x30D);
    assert(go_active::KCC_HAS_OBSERVER_MOVE == 0x3C0);
    assert(go_active::PLAYER_UKX == 0x260);
    assert(go_active::UKX_MODEL_INFO == 0x10);
    assert(go_active::EVENT_HANDLER_HEALTH == 0x98);
    assert(go_active::KCC_CHARACTER_ANIMATION == 0xE0);
    assert(go_active::KCC_HEAD_TRANSFORM == 0x88);
    assert(go_active::KCC_NORMAL_HEIGHT == 0xA0);
    assert(go_active::KCC_CROUCH_HEIGHT == 0xA4);
    assert(go_active::KCC_HITBOX_ROOT == 0x70);
    assert(go_active::KCC_LOOK_HEIGHT_OFFSET == 0x90);

    // --- Падение: виталы и раскладка значения Move в бете совпали с релизом ---
    // Oxide.EntityVitals в dump.cs 11977: m_MinFallSpeed 0xC0, m_MaxFallSpeed 0xC4;
    // PlayerVitals.Player 0x100, PlayerEvent 0x108; PlayerManager.vitals 0xC8.
    assert(go_active::PLAYER_VITALS == 0xC8);
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

    // --- PlayerWeapon и вьюха третьего лица ---
    assert(go_active::PLAYERWEAPON_VIEW == 0x100);
    assert(go_active::PLAYERWEAPON_PIECE == 0x130);
    assert(go_active::PLAYERWEAPON_PIECE_LOCAL == 0xD0);
    assert(go_active::PLAYERWEAPON_STATE == 0x14C);
    assert(go_active::PLAYERWEAPON_PLAYER_BACKREF == 0x160);
    assert(go_active::WEAPONVIEW_WEAPON_BASE == 0x50);
    assert(go_active::WEAPONVIEW_PIECE == 0x58);
    assert(go_active::WEAPONVIEW_ROOT_TRANSFORM == 0x70);
    assert(go_active::WEAPONVIEW_INNER == 0x10);

    // --- Ближний бой/инструмент/копьё: бета снова совпала с релизом ---
    assert(go_active::FPMELEE_MAX_REACH == 0x130);
    assert(go_active::FPMELEE_HIT_RADIUS == 0x134);
    assert(go_active::FPMELEE_TIME_BETWEEN_ATTACKS == 0x138);
    assert(go_active::FPMELEE_PAUSE_AFTER_ATTACK == 0x13C);
    assert(go_active::FPTOOL_TOOL_PURPOSES == 0x168);
    assert(go_active::FPSPEAR_PROJECTILE_PREFAB == 0x178);
    assert(go_active::FPSPEAR_MIN_THROW_PERIOD == 0x170);
    assert(go_active::FPSPEAR_CACHED_THROW_PERIOD == 0x1C0);

    // --- Скины: WeaponPiece вырос, остальная цепочка совпала с релизом ---
    // Oxide.WeaponPiece 10228 в dump.cs 11977: Enabled 0x0, Number 0x2,
    // Skin 0x4, SkinLevel 0x6, Loaded 0x7, Mods 0x8, EmptyHands 0x10.
    // Структура стала 0x18 байт, значит передаётся по ссылке, а не одним
    // 64-битным регистром, как в релизе (см. EN.XLy в game_offsets.h).
    assert(go_active::WEAPONPIECE_SIZE == 0x18);
    assert(go_active::WEAPONPIECE_EMPTY_HANDS == 0x10);
    assert(go_active::WEAPONPIECE_SKIN == 0x4);
    assert(go_active::WEAPONPIECE_SKIN_LEVEL == 0x6);
    assert(go_active::WEAPONPIECE_LOADED == 0x7);
    // SkinData и все наследники в 11977 поимённо и по адресам те же.
    assert(go_active::ITEMDATA_SKIN_DATA == 0xD0);
    assert(go_active::SKINDATA_ID == 0x18);
    assert(go_active::SKINDATA_BASE_ID == 0x20);
    assert(go_active::WEAPONSKIN_FP_REF == 0x58);
    assert(go_active::WEAPONSKIN_PROG_FP_REFS == 0x70);
    assert(go_active::EQUIPSKIN_FP_OBJECT_REF == 0x58);
    assert(go_active::ASSETREF_GUID == 0x10);
    assert(go_active::ASSETREF_OPERATION == 0x28);
    // PlayerWeapon 11977: перед viewReference вставлены поля, FP-вьюха уехала.
    assert(go_active::PLAYERWEAPON_FP_VIEW == 0x108);
    assert(go_active::PLAYERWEAPON_SKIN_HANDLE == 0xC0);
    assert(go_active::FPWEAPONVIEW_PIECE == 0x28);
    // WeaponPiece 0x18 байт — CancellationTokenSource сдвинулся с 0x38.
    assert(go_active::FPWEAPONVIEW_CANCELLATION == 0x40);
    // TypeInfo-слоты классов скинов (script.json 11977). EG в списке беты нет:
    // 0 означает «резолвить по имени в рантайме», а не «слот нулевой».
    assert(go_active::EG_TYPEINFO_RVA == 0);
    assert(go_active::EN_TYPEINFO_RVA == 0xE63FC68);
    assert(go_active::WEAPONSKINDATA_TYPEINFO_RVA == 0xE64BA08);
    assert(go_active::WEAPONOBJECTSKIN_TYPEINFO_RVA == 0xE64B9E0);
    assert(go_active::WEAPONRECOLORSKIN_TYPEINFO_RVA == 0xE64B9F8);
    assert(go_active::WEAPONPROGRESSIONSKIN_TYPEINFO_RVA == 0xE64B9F0);
    assert(go_active::WEAPONPREVIEWPREFABSKIN_TYPEINFO_RVA == 0xE64B9E8);
    assert(go_active::EQUIPSKINDATA_TYPEINFO_RVA == 0xE640018);
    assert(go_active::EQUIPOBJECTSKIN_TYPEINFO_RVA == 0xE640010);
    assert(go_active::EQUIPRECOLORSKIN_TYPEINFO_RVA == 0xE640020);
    // Слоты прогрессии и превью лежат между object и recolor, шаг 8 байт —
    // та же картина, что у TypeInfo-слотов UniTask-промисов.
    assert(go_active::WEAPONPROGRESSIONSKIN_TYPEINFO_RVA -
               go_active::WEAPONOBJECTSKIN_TYPEINFO_RVA == 0x10);
    assert(go_active::WEAPONRECOLORSKIN_TYPEINFO_RVA -
               go_active::WEAPONPROGRESSIONSKIN_TYPEINFO_RVA == 8);

    // --- Возврат в релизный профиль обязан восстановить всё ---
    go_active::Select(GameVersion::Release);
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA == 0xDEA09F8);
    assert(go_active::PLAYER_USER_ID == 0x298);
    assert(go_active::PLAYER_UKX == 0x260);
    assert(go_active::UKX_MODEL_INFO == 0x10);
    assert(go_active::EVENT_HANDLER_AIM_ACTIVITY == 0x298);
    assert(go_active::EVENT_HANDLER_HEALTH == 0x98);
    assert(go_active::KCC_MOVE == 0x13C);
    assert(go_active::KCC_VISIBILITY == 0x298);
    assert(go_active::KCC_MOTOR_ENABLED == 0x29D);
    assert(go_active::KCC_HAS_OBSERVER_MOVE == 0x350);
    assert(go_active::VITALS_MIN_FALL_SPEED == 0xC0);
    assert(go_active::KCC_MOVE_BASE_VELOCITY_Y == 0x3C);
    assert(go_active::FPMOTION_IDLE_OFFSET == 0x78);
    assert(go_active::FPMANAGER_WORLD_FOV_TARGET == 0x9C);
    assert(go_active::PLAYERWEAPON_VIEW == 0xE8);
    assert(go_active::RAGDOLL_BONES_ARRAY == 0x88);

    // --- Скины в релизном профиле (dump11966 + script.json 11966) ---
    // Oxide.WeaponPiece 9800: Skin 0x4 (short), SkinLevel 0x6 (byte),
    // структура 0x10 байт, поля EmptyHands НЕТ.
    assert(go_active::WEAPONPIECE_SIZE == 0x10);
    assert(go_active::WEAPONPIECE_EMPTY_HANDS == 0);
    assert(go_active::WEAPONPIECE_SKIN == 0x4);
    assert(go_active::WEAPONPIECE_SKIN_LEVEL == 0x6);
    assert(go_active::ITEMDATA_SKIN_DATA == 0xD0);
    assert(go_active::SKINDATA_ID == 0x18);
    assert(go_active::WEAPONSKIN_RIGHT_HAND_REF == 0x48);
    assert(go_active::WEAPONSKIN_FP_REF == 0x58);
    assert(go_active::WEAPONSKIN_RECOLOR_MATERIAL == 0x48);
    assert(go_active::PLAYERWEAPON_FP_VIEW == 0xF0);
    assert(go_active::PLAYERWEAPON_SKIN_HANDLE == 0xB0);
    assert(go_active::PLAYERWEAPON_PIECE_LOCAL == 0xC0);
    assert(go_active::FPWEAPONVIEW_PIECE == 0x28);
    assert(go_active::FPWEAPONVIEW_CANCELLATION == 0x38);
    assert(go_active::EG_TYPEINFO_RVA == 0xDE9ACA0);
    assert(go_active::EN_TYPEINFO_RVA == 0);
    assert(go_active::WEAPONSKINDATA_TYPEINFO_RVA == 0xDEA5300);
    assert(go_active::WEAPONOBJECTSKIN_TYPEINFO_RVA == 0xDEA52D8);
    assert(go_active::WEAPONRECOLORSKIN_TYPEINFO_RVA == 0xDEA52F0);
    assert(go_active::WEAPONPROGRESSIONSKIN_TYPEINFO_RVA == 0xDEA52E8);
    assert(go_active::WEAPONPREVIEWPREFABSKIN_TYPEINFO_RVA == 0xDEA52E0);
    assert(go_active::EQUIPSKINDATA_TYPEINFO_RVA == 0xDE9B080);
    assert(go_active::EQUIPOBJECTSKIN_TYPEINFO_RVA == 0xDE9B078);
    assert(go_active::EQUIPRECOLORSKIN_TYPEINFO_RVA == 0xDE9B088);
    // Пределы индекса/уровня общие для обоих профилей.
    assert(go_active::SKIN_INDEX_MAX == 63);
    assert(go_active::SKIN_LEVEL_MAX == 7);

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
