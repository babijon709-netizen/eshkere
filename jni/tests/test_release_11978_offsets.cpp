// Пин release-профиля сборки 11978 (dump11978.7z).
//
// Закрепляет TypeInfo-слоты (script.json 11978) и структурные offsets,
// сверенные с dump.cs 11978 трёхсторонне (11966 -> beta 11977 -> 11978) и
// свежим RE дроу-пути (FPManager.LTI 0x6D59828, вызывающий 0x7BB70B4,
// обёртка 0x7BB7088). Релиз 11978 сошёлся с бетой 11977 по всем спорным
// раскладкам; расхождение профилей теперь — только TypeInfo-слоты, FASTLOOT
// ALT-слоты, имена полей и TOD-окно (см. test_beta_11977_offsets.cpp).
#include "game_offsets_active.h"

#include <cassert>
#include <cstdio>

int main() {
    using go_active::GameVersion;

    // --- Профиль по умолчанию ---
    assert(go_active::selected_version == GameVersion::Release);

    // --- TypeInfo-слоты (внутренние .data, одно разыменование) ---
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA == 0xE66DEE8);
    assert(go_active::PLAYER_LOOP_HELPER_TYPEINFO_RVA == 0xE66DEB0);
    assert(go_active::GAME_CONTROLLER_TYPEINFO_RVA == 0xE669190);
    assert(go_active::NETWORK_CLIENT_TYPEINFO_RVA == 0xE66D238);
    assert(go_active::WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA == 0xE672A10);
    // Инвариант PLH -> PM = 0x38 пережил уже пять ротаций имён.
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA -
               go_active::PLAYER_LOOP_HELPER_TYPEINFO_RVA == 0x38);
    // Промисы UniTask: пара классов через 8 байт, вложенный <>c в релизе не нужен.
    assert(go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA == 0xE67D4E8);
    assert(go_active::FASTLOOT_IGNOREDELAY_TYPEINFO_RVA == 0xE67D4E0);
    assert(go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA -
               go_active::FASTLOOT_IGNOREDELAY_TYPEINFO_RVA == 8);
    assert(go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA_ALT == 0);
    assert(go_active::FASTLOOT_IGNOREDELAY_TYPEINFO_RVA_ALT == 0);
    // Раскладка полей обоих промисов (dump.cs 11978: TDI 21933/21935).
    assert(go_active::DELAYPROMISE_TIMESPAN == 0x18);
    assert(go_active::DELAYPROMISE_ELAPSED == 0x1C);
    assert(go_active::DELAYPROMISE_INITIAL_FRAME == 0x20);
    assert(go_active::DELAYPROMISE_SCALED_INITIAL_FRAME == 0x18);
    assert(go_active::DELAYPROMISE_SCALED_TIMESPAN == 0x1C);
    assert(go_active::DELAYPROMISE_SCALED_ELAPSED == 0x20);

    // --- Окно структурного скана .data (11978: .data 0xE3D7800..0xEA3C4E0) ---
    // Должно накрывать QA_TypeInfo (TOD_Sky, 0xE66E548) и kiQ_TypeInfo
    // (25 LayerMask-статик, 0xE674BE8); остальные рабочие слоты тоже внутри.
    assert(go_active::TOD_SCAN_RVA_BEGIN == 0xE660000);
    assert(go_active::TOD_SCAN_RVA_END == 0xE680000);
    assert(go_active::TOD_SCAN_RVA_BEGIN < go_active::TOD_SCAN_RVA_END);
    constexpr std::uint64_t kTodSkyTypeInfoSlot = 0xE66E548;  // QA_TypeInfo
    assert(kTodSkyTypeInfoSlot > go_active::TOD_SCAN_RVA_BEGIN);
    assert(kTodSkyTypeInfoSlot < go_active::TOD_SCAN_RVA_END);
    assert(go_active::HMJ_TYPEINFO_RVA == 0xE674BE8);
    assert(go_active::HMJ_TYPEINFO_RVA > go_active::TOD_SCAN_RVA_BEGIN);
    assert(go_active::HMJ_TYPEINFO_RVA < go_active::TOD_SCAN_RVA_END);
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA > go_active::TOD_SCAN_RVA_BEGIN);
    assert(go_active::PLAYER_MANAGER_TYPEINFO_RVA < go_active::TOD_SCAN_RVA_END);
    assert(go_active::GAME_CONTROLLER_TYPEINFO_RVA > go_active::TOD_SCAN_RVA_BEGIN);
    assert(go_active::GAME_CONTROLLER_TYPEINFO_RVA < go_active::TOD_SCAN_RVA_END);
    assert(go_active::NETWORK_CLIENT_TYPEINFO_RVA > go_active::TOD_SCAN_RVA_BEGIN);
    assert(go_active::NETWORK_CLIENT_TYPEINFO_RVA < go_active::TOD_SCAN_RVA_END);
    assert(go_active::WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA > go_active::TOD_SCAN_RVA_BEGIN);
    assert(go_active::WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA < go_active::TOD_SCAN_RVA_END);
    assert(go_active::FASTLOOT_DELAYPROMISE_TYPEINFO_RVA < go_active::TOD_SCAN_RVA_END);
    assert(go_active::TOD_SKY_CYCLE == 0x40);
    assert(go_active::TOD_CYCLE_HOUR == 0x10);
    assert(go_active::TOD_CYCLE_DAY == 0x14);
    assert(go_active::TOD_CYCLE_MONTH == 0x18);
    assert(go_active::TOD_CYCLE_YEAR == 0x1C);
    assert(go_active::WORLD_STREAMING_GRASS_ENABLED_FIELD == 0x18);
    assert(go_active::WORLD_STREAMING_TREES_ENABLED_FIELD == 0x19);

    // --- Статики GameControllerBase (dump.cs 11978) ---
    assert(go_active::GAME_CONTROLLER_NET_IDENTITY_FIELD == 0x8);
    assert(go_active::GAME_CONTROLLER_LOCAL_PLAYER_FIELD == 0x10);
    assert(go_active::GAME_CONTROLLER_EVENT_HANDLER_FIELD == 0x18);
    assert(go_active::GAME_CONTROLLER_INPUT_HANDLER_FIELD == 0x20);
    assert(go_active::GAME_CONTROLLER_CAMERA_MANAGER_FIELD == 0x38);
    assert(go_active::GAME_CONTROLLER_SCREEN_CONTROLS_FIELD == 0x50);
    assert(go_active::GAME_CONTROLLER_STATIC_SELF_FIELD == 0x60);
    assert(go_active::GAME_CONTROLLER_MAP_MANAGER == 0x170);
    assert(go_active::CAMERA_MANAGER_CAMERA_FIELD == 0x20);

    // --- PlayerManager: голова без изменений ---
    assert(go_active::PLAYER_MANAGER_STATIC_FIELDS_ACTIVE_LIST == 0x08);
    assert(go_active::PLAYER_MANAGER_STATIC_FIELDS_LIST == 0x10);
    assert(go_active::PLAYER_TRANSFORM == 0x68);
    assert(go_active::PLAYER_MOUSE_LOOK == 0x70);
    assert(go_active::PLAYER_EVENT_HANDLER == 0x78);
    assert(go_active::PLAYER_DEATH_HANDLER == 0x80);
    assert(go_active::PLAYER_RAYCAST_MANAGER == 0x88);
    assert(go_active::PLAYER_FP_MANAGER == 0x90);
    assert(go_active::PLAYER_INVENTORY == 0x98);
    assert(go_active::PLAYER_KCC_REFERENCE == 0xB0);
    assert(go_active::PLAYER_VITALS == 0xC8);
    assert(go_active::PLAYER_WEAPON_REFERENCE == 0xF0);
    assert(go_active::PLAYER_NICKLABEL == 0x130);
    assert(go_active::PLAYER_VOICE_PLAYER == 0x140);
    assert(go_active::PLAYER_CHARACTER_MODEL == 0x150);
    assert(go_active::PLAYER_VOICE_COLLIDER == 0x168);
    assert(go_active::PLAYER_TRIGGER_PROBE == 0x170);
    assert(go_active::PLAYER_AIM_ASSIST_CONFIG == 0x180);
    assert(go_active::PLAYER_WEAPONS_ARRAY == 0x1B8);
    assert(go_active::PLAYER_POSITION == 0x1F4);
    assert(go_active::PLAYER_MANAGER_RESPAWNING == 0x228);
    assert(go_active::PLAYER_DISPLAY_NAME == 0x240);
    assert(go_active::PLAYER_AIM_ASSIST == 0x258);
    assert(go_active::PLAYER_UKX == 0x260);
    assert(go_active::UKX_MODEL_INFO == 0x10);
    assert(go_active::PLAYER_MODEL_INFO_REF == 0x260);

    // --- Хвост PlayerManager: вставка KSu/KSj (0x270/0x278) сдвинула его на +0x10 ---
    assert(go_active::PLAYER_USER_ID == 0x2A8);
    assert(go_active::PLAYER_TEAM_NAME == 0x2B0);
    assert(go_active::PLAYER_VEHICLE_ID == 0x2B8);
    assert(go_active::PLAYER_SEAT_ID == 0x2BC);
    assert(go_active::PLAYER_CLAN_ID == 0x2C0);
    assert(go_active::PLAYER_CLAN_TAG == 0x2C8);
    assert(go_active::PLAYER_ZIPLINE_ID == 0x2DC);
    assert(go_active::PLAYER_VOICE_STATE == 0x338);
    assert(go_active::PLAYER_HIT_TRACKER == 0x358);
    assert(go_active::HIT_TRACKER_HIT_DICT == 0x18);
    assert(go_active::PLAYER_SEAT_ID - go_active::PLAYER_VEHICLE_ID == 4);
    assert(go_active::PLAYER_CLAN_TAG - go_active::PLAYER_CLAN_ID == 8);
    assert(go_active::VOICE_STATE_NAME == 0x38);
    assert(go_active::VOICE_PLAYER_TAG == 0x78);
    assert(go_active::NICKLABEL_PLAYER_BACKREF == 0x20);
    assert(go_active::NICKLABEL_NICKNAME_TEXT == 0x38);
    assert(go_active::UI_TEXT_MTEXT == 0xE0);

    // --- Хендлер событий (kAu : kAH : kAg) ---
    assert(go_active::EVENT_HANDLER_MANAGER_BACKREF == 0xD0);
    assert(go_active::EVENT_HANDLER_HEALTH == 0x98);
    assert(go_active::HEALTH_VALUE == 0x20);
    assert(go_active::EVENT_HANDLER_MOVEMENT_INPUT == 0x128);
    assert(go_active::EVENT_HANDLER_LOOK_DIRECTION == 0x140);
    assert(go_active::EVENT_HANDLER_LAST_HIT_POINT == 0x170);
    assert(go_active::EVENT_HANDLER_LAST_HIT_TIME == 0x178);
    // Aim уехал с 0x298 на 0x2B0 (вставки Footstep/StepLength/SetVehicleLock),
    // AttackContinuously на 0x1F0 не сдвинут.
    assert(go_active::EVENT_HANDLER_AIM_ACTIVITY == 0x2B0);
    assert(go_active::GUM_RAYCAST_DATA == 0x160);
    assert(go_active::GUM_AIM_RAYCAST == 0x168);
    assert(go_active::GUI_VALUE == 0x20);
    assert(go_active::SYNC_VALUE_OFFSET == 0x20);
    assert(go_active::ACTIVITY_ACTIVE_FLAG == 0x10);
    assert(go_active::GKO_HIT_OBJECT == 0x18);
    assert(go_active::GKO_HIT_ENTITY == 0x20);
    assert(go_active::GKO_HIT_PLAYER == 0x40);
    assert(go_active::GKO_RAYCAST_HIT == 0x48);
    assert(go_active::GKO_RAY_ORIGIN == 0x74);
    assert(go_active::RAYCASTHIT_DISTANCE == 0x1C);

    // --- KCC: Move встал сразу за PlayerInput (как в бете 11977) ---
    assert(go_active::KCC_HITBOX_ROOT == 0x70);
    assert(go_active::KCC_PLAYER_BACKREF == 0x78);
    assert(go_active::KCC_HEAD_TRANSFORM == 0x88);
    assert(go_active::KCC_LOOK_HEIGHT_OFFSET == 0x90);
    assert(go_active::KCC_NORMAL_HEIGHT == 0xA0);
    assert(go_active::KCC_CROUCH_HEIGHT == 0xA4);
    assert(go_active::KCC_CHARACTER_ANIMATION == 0xE0);
    assert(go_active::KCC_MOVE == 0x1AC);
    assert(go_active::KCC_VISIBILITY == 0x308);
    assert(go_active::KCC_MOTOR_ENABLED == 0x30D);
    assert(go_active::KCC_HAS_OBSERVER_MOVE == 0x3C0);
    assert(go_active::KCC_MOTOR_CAPSULE == 0x28);
    assert(go_active::KCC_MOVE_STATE == 0x00);
    assert(go_active::KCC_MOVE_REAL_VELOCITY == 0x18);
    assert(go_active::KCC_MOVE_REAL_VELOCITY_Y - go_active::KCC_MOVE_REAL_VELOCITY == 4);
    assert(go_active::KCC_MOVE_BASE_VELOCITY == 0x38);
    assert(go_active::KCC_MOVE_BASE_VELOCITY_Y - go_active::KCC_MOVE_BASE_VELOCITY == 4);
    assert(go_active::KCC_MOVE_STATE_AIRBORNE == 4);
    assert(go_active::KCC_MOVE_STATE_CLAMP_MAX == 4);
    assert(go_active::KCC_MOVE_STATE_DEAD == 7);
    assert(go_active::CHAR_ANIM_PLAYER_BACKREF == 0x78);
    assert(go_active::CHAR_ANIM_RAGDOLL == 0x38);
    assert(go_active::CHARANIM_PLAYER_MODEL_INFO == 0x30);
    assert(go_active::RAGDOLL_PELVIS_RIGIDBODY == 0x20);
    assert(go_active::RAGDOLL_BONES_ARRAY == 0x90);
    assert(go_active::RAGDOLL_BODYPART_TRANSFORM == 0x10);

    // --- Виталы / смерть / инвентарь / NPC ---
    assert(go_active::VITALS_HEALTH == 0xB8);
    assert(go_active::VITALS_MAX_HEALTH == 0x88);
    assert(go_active::VITALS_MIN_FALL_SPEED == 0xC0);
    assert(go_active::VITALS_MAX_FALL_SPEED == 0xC4);
    assert(go_active::VITALS_PLAYER_BACKREF == 0x100);
    assert(go_active::PMP_ENTITY == 0x68);
    assert(go_active::PMK_HEALTH == 0x98);
    assert(go_active::DEATH_HANDLER_DEATH == 0xF0);
    assert(go_active::DEATH_HANDLER_REF_TARGET == 0x18);
    assert(go_active::INV_PLAYER_INVENTORY_DATA == 0x20);
    assert(go_active::INV_PLAYER_INVENTORY_CLIENT == 0x28);
    assert(go_active::INVDATA_PLAYER_MODEL_INFO == 0x18);
    assert(go_active::NPC_BRAIN_ENTITY == 0x68);
    assert(go_active::NPC_BRAIN_MODEL_INFO == 0x148);
    assert(go_active::NPC_DEATH_COLLIDERS == 0xC8);
    assert(go_active::MODELINFO_HEAD == 0x20);
    assert(go_active::MODELINFO_RIGHT_WEAPON_HOLDER == 0x28);
    assert(go_active::MODELINFO_LEFT_WEAPON_HOLDER == 0x30);
    assert(go_active::HITBOX_ROOT_ARRAY == 0x68);
    assert(go_active::HITBOX_SIZE == 0x24);
    assert(go_active::HITBOX_CENTER == 0x30);
    assert(go_active::HITBOX_AREA == 0x68);

    // --- AimAssist (kwY; класс переехал в Features.AimAssist, offsets на месте) ---
    assert(go_active::MOUSELOOK_PITCH_YAW == 0x4C);
    assert(go_active::RAYCASTMAN_RAY_LENGTH == 0x38);
    assert(go_active::RAYCASTMAN_AIM_RAY_LENGTH == 0x3C);
    assert(go_active::RAYCASTMAN_SPHERE_RADIUS == 0x40);
    assert(go_active::RAYCASTMAN_LAYER_MASK == 0x48);
    assert(go_active::RAYCASTMAN_AIM_LAYER_MASK == 0x4C);
    assert(go_active::AIMTARGET_ENTITY_HANDLER == 0x20);
    assert(go_active::AIMCONFIG_DISTANCE == 0x18);
    assert(go_active::AIMCONFIG_OUTER_SPEED == 0x38);
    assert(go_active::AIMCONFIG_INNER_SPEED == 0x3C);
    assert(go_active::AIMCONFIG_OUTER_ABS_SPEED == 0x40);
    assert(go_active::AIMCONFIG_INNER_ABS_SPEED == 0x44);
    assert(go_active::AIMCONFIG_SWITCH_DELAY == 0x48);
    assert(go_active::AIMASSIST_PLAYER == 0x10);
    assert(go_active::AIMASSIST_CONFIG == 0x18);
    assert(go_active::AIMASSIST_INIT_FLAG == 0x20);
    assert(go_active::AIMASSIST_ENABLED == 0x21);
    assert(go_active::AIMASSIST_CAMERA == 0x28);
    assert(go_active::AIMASSIST_RAYCAST_HIT == 0x30);
    assert(go_active::AIMASSIST_CUR_TARGET == 0x60);
    assert(go_active::AIMASSIST_CUR_SENSOR == 0x68);
    assert(go_active::AIMASSIST_PREV_TARGET == 0x98);
    assert(go_active::AIMASSIST_PREV_SENSOR == 0xA0);
    assert(go_active::AIMASSIST_HIT_FLAG == 0xDC);
    assert(go_active::AIMASSIST_DELTA_ROT == 0x108);
    assert(go_active::AIMSENSOR_COLLIDER == 0x00);
    assert(go_active::AIMSENSOR_START_LOOK_TIME == 0x08);
    assert(go_active::AIMSENSOR_LAST_SEEN_TIME == 0x0C);
    assert(go_active::AIMSENSOR_LAST_RECT_TIME == 0x10);
    assert(go_active::AIMSENSOR_DISTANCE == 0x14);
    assert(go_active::AIMSENSOR_POSITION == 0x18);
    assert(go_active::AIMSENSOR_DIRECTION == 0x24);

    // --- Маркеры: вся цепочка без изменений (имена ротированы) ---
    assert(go_active::MAP_MANAGER_MAP_VIEW == 0x28);
    assert(go_active::MAP_MANAGER_MARKER_SERVICE == 0x60);
    assert(go_active::MAP_MANAGER_MAP_PRESENTER == 0x68);
    assert(go_active::MARKER_INSTALLER_MAP_VIEW == 0x30);
    assert(go_active::MAP_VIEW_ON_ENABLE_ACTION == 0x60);
    assert(go_active::MAP_PRESENTER_MARKER_SERVICE == 0x10);
    assert(go_active::MARKER_SERVICE_ADDED_ACTION == 0x20);
    assert(go_active::SCREEN_WINDOW_RECT_TRANSFORM == 0x30);
    assert(go_active::SCREEN_PRESENTER_VIEW_DICT == 0x48);
    assert(go_active::SCREEN_PRESENTER_MARKER_LIST == 0x50);
    assert(go_active::SCREEN_PRESENTER_INST_DICT == 0x60);
    assert(go_active::MARKER_DATA == 0x10);
    assert(go_active::MARKER_ACTIVE == 0x18);
    assert(go_active::MARKER_DATA_TYPE == 0x18);
    assert(go_active::MARKER_DATA_WORLD_POS == 0x1C);
    assert(go_active::MARKER_DATA_HEAD_POS == 0x28);
    assert(go_active::MARKER_DATA_TRANSFORM == 0x40);
    assert(go_active::MARKER_DATA_USER_ID == 0x58);
    assert(go_active::MARKER_DATA_IS_HIT == 0x73);
    assert(go_active::MARKER_TYPE_NEARBY_PLAYER == 10);
    assert(go_active::SCREEN_MARKER_VIEW_CONTEXT == 0x28);
    assert(go_active::SCREEN_MARKER_VIEW_MODULES == 0x30);
    assert(go_active::SCREEN_MARKER_VIEW_MARKER == 0x40);
    assert(go_active::NEARBY_VIEW_MODULE_CTX == 0x80);
    assert(go_active::NEARBY_VIEW_DATA == 0xA8);
    assert(go_active::NEARBY_VIEW_CACHED_HIT == 0xB1);
    assert(go_active::MARKER_CTX_RAY_TARGET == 0x44);
    assert(go_active::MARKER_CTX_SKIP_OCCLUSION == 0x50);
    assert(go_active::MARKER_CTX_HIDDEN == 0x90);
    assert(go_active::OCCLUSION_MOD_CTX == 0x20);
    assert(go_active::OCCLUSION_MOD_MASK == 0x28);
    assert(go_active::OCCLUSION_MOD_INTERVAL == 0x2C);
    assert(go_active::OCCLUSION_MOD_NEXT_TIME == 0x38);
    assert(go_active::OCCLUSION_MOD_HAS_VAL == 0x3C);
    assert(go_active::OCCLUSION_MOD_IS_OCCLUDED == 0x3D);

    // --- Слой ввода ---
    assert(go_active::PLAYER_INPUT_HANDLER_OWNER_BACKREF == 0x30);
    assert(go_active::PLAYER_INPUT_HANDLER_ATTACK_HELD == 0x68);
    assert(go_active::PLAYER_INPUT_HANDLER_OVERRIDE_A == 0x6C);
    assert(go_active::PLAYER_INPUT_HANDLER_OVERRIDE_B == 0x78);
    assert(go_active::PLAYER_INPUT_HANDLER_SPRINT_OVERRIDE == 0x84);
    assert(go_active::PLAYER_INPUT_HANDLER_INPUT_ACTIONS == 0x98);
    assert(go_active::PLAYER_INPUT_MOVEMENT_INPUT == 0x40);
    assert(go_active::PLAYER_INPUT_YROTATION == 0x54);
    assert(go_active::PLAYER_INPUT_SPRINT == 0x59);
    assert(go_active::PLAYER_INPUT_TICK == 0x60);
    assert(go_active::LD_SPRINT_ACTION == 0x48);
    assert(go_active::INPUT_ACTION_INDEX == 0xC0);
    assert(go_active::INPUT_ACTION_STATE_HOLDER == 0xC8);
    assert(go_active::INPUT_ACTION_STATE_HOLDER_STATE == 0x60);
    assert(go_active::INPUT_ACTION_STATE_STATES == 0x60);
    assert(go_active::INPUT_ACTION_STATE_STRIDE == 0x44);
    assert(go_active::INPUT_ACTION_PHASE_OFFSET == 0x00);
    assert(go_active::JOYSTICK_HANDLE_RANGE == 0x54);
    assert(go_active::JOYSTICK_DEAD_ZONE == 0x58);
    assert(go_active::JOYSTICK_INPUT_VALUE == 0x98);
    assert(go_active::SCREEN_CONTROLS_HOLDER_FIELD == 0x28);
    assert(go_active::SCREEN_CONTROLS_HOLDER_FIXED == 0x78);
    assert(go_active::SCREEN_CONTROLS_HOLDER_FLOATING == 0x90);
    assert(go_active::NULLABLE_HAS_VALUE == 0x0);
    assert(go_active::NULLABLE_VALUE == 0x4);
    assert(go_active::NULLABLE_STRIDE == 0x0C);

    // --- Mirror / BCL / runtime-структуры ---
    assert(go_active::NETWORK_CLIENT_SPAWNED == 0x30);
    assert(go_active::NETWORK_BEHAVIOUR_IDENTITY == 0x40);
    assert(go_active::NETWORK_IDENTITY_IS_LOCAL_PLAYER == 0x4A);
    assert(go_active::DICT_ENTRIES == 0x18);
    assert(go_active::DICT_COUNT == 0x20);
    assert(go_active::DELEGATE_TARGET == 0x20);
    assert(go_active::MULTICAST_DELEGATES == 0x78);
    assert(go_active::IL2CPP_ARRAY_LENGTH == 0x18);
    assert(go_active::IL2CPP_ARRAY_FIRST_ELEMENT == 0x20);
    assert(go_active::MANAGED_CACHED_PTR == 0x10);
    assert(go_active::PLAYERLOOPHELPER_RUNNERS == 0x30);
    assert(go_active::LOOPRUNNER_ITEMS == 0x38);
    assert(go_active::LOOPRUNNER_TAIL == 0x30);

    // --- FP-слой: FPObject/FPWeaponBase/вьюхи/FPMotion ---
    assert(go_active::FPOBJECT_DRAW_EVENT == 0x20);
    assert(go_active::FPDRAW_ACTION == 0x10);
    assert(go_active::FPOBJECT_SHOWN == 0x38);
    assert(go_active::FPOBJECT_ITEM == 0x40);
    assert(go_active::FPOBJECT_OBJECT_NAME == 0x78);
    assert(go_active::FPOBJECT_PLAYER_BACKREF == 0xC0);
    assert(go_active::FPOBJECT_EVENT_HANDLER == 0xC8);
    assert(go_active::FPOBJECT_ENABLE_AIM_ASSIST == 0xF8);
    assert(go_active::FPWEAPON_EFFECTIVE_RELOAD_PERIOD == 0xE8);
    assert(go_active::FPWEAPON_IS_AIMING == 0x120);
    assert(go_active::FPWEAPON_SHOT_PERIOD == 0x124);
    assert(go_active::FPMANAGER_CURRENT_OBJECT == 0x50);
    assert(go_active::FPMANAGER_CURRENT_WEAPON == 0x58);
    assert(go_active::FPMANAGER_UZL == 0x68);
    // AssetReference KLE @0x80 сдвинул хвост FPManager на +0x10.
    assert(go_active::FPMANAGER_CTS == 0x88);
    assert(go_active::FPMANAGER_CTS2 == 0x90);
    assert(go_active::FPMANAGER_UTCS == 0x98);
    assert(go_active::FPMANAGER_UZJ == 0xA0);
    assert(go_active::FPMANAGER_WORLD_FOV_TARGET == 0xAC);
    assert(go_active::FPMANAGER_AIM_BLEND == 0xB8);
    assert(go_active::FPMANAGER_UHF == 0xC0);
    // FPMotion перестроен как в бете: позы 0x40.., Object 0xD0, Player 0x120.
    assert(go_active::FPMOTION_IDLE_OFFSET == 0x40);
    assert(go_active::FPMOTION_RUN_OFFSET == 0x48);
    assert(go_active::FPMOTION_RUN_RELOAD_OFFSET == 0x50);
    assert(go_active::FPMOTION_ATTACK_OFFSET == 0x58);
    assert(go_active::FPMOTION_AIM_OFFSET == 0x60);
    assert(go_active::FPMOTION_LOOK_OFFSET == 0x68);
    assert(go_active::FPMOTION_CLOSE_OFFSET == 0x70);
    assert(go_active::FPMOTION_LADDER_OFFSET == go_active::FPMOTION_AIM_OFFSET);
    assert(go_active::FPMOTION_JUMP_OFFSET == go_active::FPMOTION_AIM_OFFSET);
    assert(go_active::FPMOTION_OBJECT == 0xD0);
    assert(go_active::FPMOTION_PLAYER == 0x120);
    assert(go_active::FPMOTION_ACTIVE_OFFSET == 0xE8);
    assert(go_active::TRANSFORMOFFSET_LERP_SPEED == 0x20);
    assert(go_active::TRANSFORMOFFSET_POSITION == 0x24);
    assert(go_active::TRANSFORMOFFSET_CURRENT_POSITION == 0x44);

    // --- Оружейные подклассы и конфиги (без изменений) ---
    assert(go_active::FPHITSCAN_CONFIG == 0x130);
    assert(go_active::FPHITSCAN_FIRE_MODE == 0x138);
    assert(go_active::FPHITSCAN_BURST_WAIT == 0x1F0);
    assert(go_active::FPHITSCAN_CALCULATOR == 0x1F8);
    assert(go_active::CALCULATOR_DISPERSION_CONFIG == 0x10);
    assert(go_active::CALCULATOR_HITSCAN_CONFIG == 0x18);
    assert(go_active::CALCULATOR_CACHED_BASE_DISPERSION == 0x20);
    assert(go_active::HITSCANCONFIG_MAX_DISTANCE == 0x2C);
    assert(go_active::HITSCANCONFIG_BULLET_SPEED == 0x30);
    assert(go_active::HITSCANCONFIG_FIRE_RATE == 0x34);
    assert(go_active::HITSCANCONFIG_PROJECTILES_PER_SHOT == 0x38);
    assert(go_active::HITSCANCONFIG_BASE_DISPERSION == 0x3C);
    assert(go_active::HITSCANCONFIG_FULL_RELOAD == 0x48);
    assert(go_active::HITSCANCONFIG_LOOP_RELOAD == 0x4C);
    assert(go_active::HITSCANCONFIG_BALLISTIC_PROFILE == 0x60);
    assert(go_active::HITSCANCONFIG_DISPERSION_CONFIG == 0x68);
    assert(go_active::BALLISTICPROFILE_GRAVITY == 0x10);
    assert(go_active::BALLISTICPROFILE_AIR_DRAG == 0x14);
    assert(go_active::BALLISTICPROFILE_UNDERWATER_DRAG == 0x18);
    assert(go_active::DISPERSIONCONFIG_MIN_DISPERSION == 0x20);
    assert(go_active::DISPERSIONCONFIG_MAX_DISPERSION == 0x24);
    assert(go_active::SHAFTEDPROJECTILE_MAX_DISTANCE == 0x88);
    assert(go_active::SHAFTEDPROJECTILE_LAUNCH_SPEED == 0xA8);
    assert(go_active::SHAFTEDPROJECTILE_GRAVITY == 0xAC);
    assert(go_active::SHAFTEDPROJECTILE_AIR_DRAG == 0xB0);
    assert(go_active::SHAFTEDPROJECTILE_UNDERWATER_DRAG == 0xB4);
    assert(go_active::FPBOW_PROJECTILE_PREFAB == 0x150);
    assert(go_active::FPCROSSBOW_CONFIG == 0x138);
    assert(go_active::FPCROSSBOW_PROJECTILE_PREFAB == 0x148);
    assert(go_active::FPROCKETLAUNCHER_CONFIG == 0x128);
    assert(go_active::FPROCKETLAUNCHER_PROJECTILE_PREFAB == 0x148);
    assert(go_active::FPEXPLOSIVECHARGE_PROJECTILE_PREFAB == 0x130);
    assert(go_active::FPSNOWBALL_PROJECTILE_PREFAB == 0x130);
    assert(go_active::FPSPEAR_PROJECTILE_PREFAB == 0x178);
    assert(go_active::FPSPEAR_MIN_THROW_PERIOD == 0x170);
    assert(go_active::FPSPEAR_CACHED_THROW_PERIOD == 0x1C0);
    assert(go_active::FPMELEE_MAX_REACH == 0x130);
    assert(go_active::FPMELEE_HIT_RADIUS == 0x134);
    assert(go_active::FPMELEE_TIME_BETWEEN_ATTACKS == 0x138);
    assert(go_active::FPMELEE_PAUSE_AFTER_ATTACK == 0x13C);
    assert(go_active::FPTOOL_TOOL_PURPOSES == 0x168);
    assert(go_active::WAITFORSECONDS_SECONDS == 0x10);

    // --- PlayerWeapon: сдвиг +0x10/+0x20 от вставленных полей; гейт дроу
    // переехал с 0xDC (теперь середина wDT 0xD0..0xE8) на 0xF0 — доказано RE:
    // метод 0x7BB70B4 читает `ldr w2,[x19,#0xF0]` и передаёт слотом в LTI,
    // FPManager берёт из `ldr x0,[x19,#0x110]`. ---
    assert(go_active::PLAYERWEAPON_SKIN_HANDLE == 0xC0);
    assert(go_active::PLAYERWEAPON_PIECE_LOCAL == 0xD0);
    assert(go_active::PLAYERWEAPON_DRAW_GATE == 0xF0);
    assert(go_active::PLAYERWEAPON_VIEW == 0x100);
    assert(go_active::PLAYERWEAPON_FP_VIEW == 0x108);
    assert(go_active::PLAYERWEAPON_FP_MANAGER == 0x110);
    assert(go_active::PLAYERWEAPON_PIECE == 0x130);
    assert(go_active::PLAYERWEAPON_STATE == 0x14C);
    assert(go_active::PLAYERWEAPON_PLAYER_BACKREF == 0x160);
    // Гейт обязан лежать ВНЕ working-копии piece (0xD0..0xE8) — иначе срез
    // §4.26 читал бы мусор из середины структуры.
    assert(go_active::PLAYERWEAPON_DRAW_GATE >= go_active::PLAYERWEAPON_PIECE_LOCAL +
                                                    go_active::WEAPONPIECE_SIZE);
    assert(go_active::WEAPONVIEW_WEAPON_BASE == 0x50);
    assert(go_active::WEAPONVIEW_PIECE == 0x58);
    assert(go_active::WEAPONVIEW_ROOT_TRANSFORM == 0x70);
    assert(go_active::WEAPONVIEW_INNER == 0x10);
    assert(go_active::FPWEAPONVIEW_VISUAL == 0x20);
    assert(go_active::FPWEAPONVIEW_PIECE == 0x28);
    assert(go_active::FPWEAPONVIEW_CANCELLATION == 0x40);

    // --- Скины: WeaponPiece вырос до 0x18 (EmptyHands @0x10) ---
    assert(go_active::WEAPONPIECE_ENABLED == 0x00);
    assert(go_active::WEAPONPIECE_NUMBER == 0x02);
    assert(go_active::WEAPONPIECE_SKIN == 0x04);
    assert(go_active::WEAPONPIECE_SKIN_LEVEL == 0x06);
    assert(go_active::WEAPONPIECE_LOADED == 0x07);
    assert(go_active::WEAPONPIECE_MODS == 0x08);
    assert(go_active::WEAPONPIECE_EMPTY_HANDS == 0x10);
    assert(go_active::WEAPONPIECE_SIZE == 0x18);
    assert(go_active::ITEM_DATA == 0x20);
    assert(go_active::ITEM_SKIN == 0x30);
    assert(go_active::ITEM_SKIN_LEVEL == 0x34);
    assert(go_active::ITEMDATA_ID == 0x58);
    assert(go_active::ITEMDATA_NAME == 0x18);
    assert(go_active::ITEMDATA_SHORTNAME == 0x20);
    assert(go_active::ITEMDATA_SKIN_DATA == 0xD0);
    assert(go_active::SKINDATA_ID == 0x18);
    assert(go_active::SKINDATA_BASE_ID == 0x20);
    assert(go_active::SKINDATA_ICON_REF == 0x28);
    assert(go_active::WEAPONSKIN_RIGHT_HAND_REF == 0x48);
    assert(go_active::WEAPONSKIN_LEFT_HAND_REF == 0x50);
    assert(go_active::WEAPONSKIN_FP_REF == 0x58);
    assert(go_active::WEAPONSKIN_PROG_RIGHT_HAND_REFS == 0x60);
    assert(go_active::WEAPONSKIN_PROG_LEFT_HAND_REFS == 0x68);
    assert(go_active::WEAPONSKIN_PROG_FP_REFS == 0x70);
    assert(go_active::WEAPONSKIN_RECOLOR_MATERIAL == 0x48);
    assert(go_active::WEAPONSKIN_RECOLOR_MATERIAL_FP == 0x50);
    assert(go_active::WEAPONSKIN_PREVIEW_REF == 0x60);
    assert(go_active::EQUIPSKIN_PROPERTY_BLOCK == 0x48);
    assert(go_active::EQUIPSKIN_OBJECT_REF == 0x50);
    assert(go_active::EQUIPSKIN_FP_OBJECT_REF == 0x58);
    assert(go_active::EQUIPSKIN_HIDE_BODY == 0x60);
    assert(go_active::EQUIPSKIN_HIDE_HEAD == 0x61);
    assert(go_active::ASSETREF_GUID == 0x10);
    assert(go_active::ASSETREF_SUBOBJ_NAME == 0x18);
    assert(go_active::ASSETREF_SUBOBJ_TYPE == 0x20);
    assert(go_active::ASSETREF_OPERATION == 0x28);
    assert(go_active::SKINHANDLE_MODEL == 0x00);
    assert(go_active::SKINHANDLE_SLOT == 0x08);
    assert(go_active::SKINHANDLE_GENERATION == 0x0C);
    assert(go_active::SKIN_INDEX_MAX == 63);
    assert(go_active::SKIN_LEVEL_MAX == 7);
    // TypeInfo-слоты скинов (script.json 11978): «пятёрка» с прежними шагами
    // (object +8 preview +8 progression +8 recolor +0x10 abstract) и «тройка»
    // экипировки (object +8 abstract +8 recolor).
    assert(go_active::EG_TYPEINFO_RVA == 0xE673558);
    assert(go_active::EN_TYPEINFO_RVA == 0);
    assert(go_active::WEAPONOBJECTSKIN_TYPEINFO_RVA == 0xE6727B0);
    assert(go_active::WEAPONPREVIEWPREFABSKIN_TYPEINFO_RVA == 0xE6727B8);
    assert(go_active::WEAPONPROGRESSIONSKIN_TYPEINFO_RVA == 0xE6727C0);
    assert(go_active::WEAPONRECOLORSKIN_TYPEINFO_RVA == 0xE6727C8);
    assert(go_active::WEAPONSKINDATA_TYPEINFO_RVA == 0xE6727D8);
    assert(go_active::WEAPONPREVIEWPREFABSKIN_TYPEINFO_RVA -
               go_active::WEAPONOBJECTSKIN_TYPEINFO_RVA == 8);
    assert(go_active::WEAPONPROGRESSIONSKIN_TYPEINFO_RVA -
               go_active::WEAPONPREVIEWPREFABSKIN_TYPEINFO_RVA == 8);
    assert(go_active::WEAPONRECOLORSKIN_TYPEINFO_RVA -
               go_active::WEAPONPROGRESSIONSKIN_TYPEINFO_RVA == 8);
    assert(go_active::WEAPONSKINDATA_TYPEINFO_RVA -
               go_active::WEAPONRECOLORSKIN_TYPEINFO_RVA == 0x10);
    assert(go_active::EQUIPOBJECTSKIN_TYPEINFO_RVA == 0xE6684F8);
    assert(go_active::EQUIPSKINDATA_TYPEINFO_RVA == 0xE668500);
    assert(go_active::EQUIPRECOLORSKIN_TYPEINFO_RVA == 0xE668508);

    // --- Фарм/лут: MineableObject и расширения (offsets без изменений) ---
    assert(go_active::MINEABLE_REQUIRED_TOOL_PURPOSE == 0x70);
    assert(go_active::MINEABLE_CURRENT_HEALTH == 0x78);
    assert(go_active::MINEABLE_LOOT == 0xA0);
    assert(go_active::MINEABLE_FINISH_BONUS == 0xA8);
    assert(go_active::MINEABLE_MAX_HEALTH == 0xC0);
    assert(go_active::MINEABLE_HIT_ANCHOR == 0xC8);
    assert(go_active::MINEABLE_FRACTION == 0xD0);
    assert(go_active::MINEABLE_EXPERIENCE == 0xD4);
    assert(go_active::MINEABLE_ENTITY_TYPE == 0xD8);
    assert(go_active::MINEABLE_EXTENSIONS == 0xE8);
    assert(go_active::LOOTITEM_ITEM_NAME == 0x10);
    assert((int)go_active::MineableEntityType::Rat == 25);
    assert((int)go_active::MineableEntityType::Wolf == 26);
    assert(go_active::OREHS_STREAK_INDEX == 0x20);
    assert(go_active::OREHS_MARKER_TEMPLATE == 0x28);
    assert(go_active::OREHS_MARKER == 0x30);
    assert(go_active::OREHS_COLLIDER == 0x38);
    assert(go_active::OREHS_MINEABLE == 0x40);
    assert(go_active::OREMARK_RENDERER == 0x20);
    assert(go_active::OREMARK_OWNER == 0x38);
    assert(go_active::OREMARK_SMOOTH == 0x48);
    assert(go_active::OREMARK_AGE == 0x58);
    assert(go_active::TREEHS_MARKER_TEMPLATE == 0x28);
    assert(go_active::TREEHS_STREAK == 0x48);
    assert(go_active::TREEHS_MARKER == 0x50);
    assert(go_active::TREEHS_SPOT_A == 0x88);
    assert(go_active::TREEHS_SPOT_B == 0xA4);
    assert(go_active::HITMARK_LIFETIME == 0x20);
    assert(go_active::HITMARK_FILTER == 0x28);
    assert(go_active::HITMARK_RENDERER == 0x30);
    assert(go_active::HITMARK_MARK == 0x38);
    assert(go_active::HITMARK_AGE == 0xC8);
    assert(go_active::CUPBOARD_IS_LOOTABLE == 0xB0);
    assert(go_active::CUPBOARD_AUTHORIZED_PLAYERS == 0xD8);
    assert(go_active::CUPBOARD_PROTECTED_TIME == 0xE8);
    assert(go_active::CUPBOARD_LUXURY_BLOCKS == 0x108);
    assert(go_active::LOOTOBJECT_INVENTORY == 0xA0);
    assert(go_active::LOOTOBJECT_IS_LOOTABLE == 0xA8);
    assert(go_active::LOOTOBJECT_PANEL_NAME == 0xE0);
    assert(go_active::LOOTOBJECT_BUILDING_PIECE == 0xF8);
    assert(go_active::ITEMPICKUP_ITEM_OBJECT == 0xA8);
    assert(go_active::ITEMPICKUP_SHORTNAME == 0xD8);
    assert(go_active::ITEMPICKUP_AMOUNT == 0xE0);

    std::printf("release 11978 offsets pinned: TypeInfo slots, PM tail +0x10,\n"
                "FPManager +0x10, PlayerWeapon/view +0x10..0x20, gate 0xF0,\n"
                "WeaponPiece 0x18, KCC/FPMotion/ragdoll like beta 11977\n");
    return 0;
}
