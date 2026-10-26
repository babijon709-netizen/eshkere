#pragma once
#include "game_offsets.h"

// Beta profile, snapshot 2026-10-21, build 11977.
// Source: dumpbeta11977.7z (dump.cs, script.json, libil2cpp.so).
// TypeInfo RVAs below come from script.json TypeInfoPointers; field layouts are
// read from dump.cs. Shared offsets continue to use game_offsets.h unless this
// profile records a beta-specific difference.
//
// The 11977 .data section spans [0xE3AFC20, 0xEA5A668). TOD_SCAN_RVA_* stays in
// that section and covers candidate layer-mask class IWt (TypeDefIndex 6610,
// TypeInfo slot 0xE6431D8) and TOD sky Gf (TypeDefIndex 706, slot 0xE640D88).
//
// ВНИМАНИЕ (2026-10-23): релизный профиль обновлён на build 11978, который
// сошёлся с бетой 11977 почти по всем раскладкам (хвост PlayerManager +0x10,
// AssetReference в FPManager, WeaponPiece 0x18, KCC, ragdoll, вьюхи, FPMotion,
// Aim 0x2B0, gate 0xF0...). Комментарии ниже, говорящие о расхождении «бета
// против релиза», описывают СТАРЫЙ релиз 11966; расхождения с релизом 11978
// остались только в TypeInfo-слотах, FASTLOOT ALT-слотах, FPMOTION-именах
// полей и мелочах вроде CANCELLATION/типов. Значения-алиасы game_offsets::
// после обновления релиза стали совпадать с бета-значениями автоматически.

namespace game_offsets_beta {
inline constexpr std::uint64_t CAMERA_PROJECTION_MATRIX = game_offsets::CAMERA_PROJECTION_MATRIX;
inline constexpr std::uint64_t CAMERA_VIEW_MATRIX = game_offsets::CAMERA_VIEW_MATRIX;
inline constexpr std::uint64_t CAMERA_WORLD_TO_CLIP = game_offsets::CAMERA_WORLD_TO_CLIP;
inline constexpr std::uint64_t CAMERA_PREV_VIEW_PROJ = game_offsets::CAMERA_PREV_VIEW_PROJ;
inline constexpr std::uint64_t CAMERA_NATIVE_TRANSFORM = game_offsets::CAMERA_NATIVE_TRANSFORM;
inline constexpr std::uint64_t CAMERA_FOV_DEGREES = game_offsets::CAMERA_FOV_DEGREES;
inline constexpr std::uint64_t CAMERA_ASPECT = game_offsets::CAMERA_ASPECT;
inline constexpr std::uint64_t CAMERA_NEAR_CLIP = game_offsets::CAMERA_NEAR_CLIP;
inline constexpr std::uint64_t CAMERA_FAR_CLIP = game_offsets::CAMERA_FAR_CLIP;
inline constexpr std::uint64_t CAMERA_VIEW_DIRTY = game_offsets::CAMERA_VIEW_DIRTY;
inline constexpr std::uint64_t CAMERA_PROJ_DIRTY = game_offsets::CAMERA_PROJ_DIRTY;
inline constexpr std::uint64_t MANAGED_CACHED_PTR = game_offsets::MANAGED_CACHED_PTR;
inline constexpr std::uint64_t NETWORK_BEHAVIOUR_IDENTITY = game_offsets::NETWORK_BEHAVIOUR_IDENTITY;
inline constexpr std::uint64_t NETWORK_IDENTITY_IS_LOCAL_PLAYER = game_offsets::NETWORK_IDENTITY_IS_LOCAL_PLAYER;
inline constexpr float PLAYER_HEIGHT = game_offsets::PLAYER_HEIGHT;
inline constexpr float PLAYER_BOX_WIDTH_RATIO = game_offsets::PLAYER_BOX_WIDTH_RATIO;
inline constexpr float MIN_PLAYER_DISTANCE = game_offsets::MIN_PLAYER_DISTANCE;
inline constexpr float MAX_PLAYER_DISTANCE = game_offsets::MAX_PLAYER_DISTANCE;
// Oxide.PlayerManager, Cysharp.Threading.Tasks.PlayerLoopHelper, Oxide.GameControllerBase,
// Mirror.NetworkClient: внутренние .data-слоты из TypeInfoPointers сборки 11977.
inline constexpr std::uint64_t PLAYER_MANAGER_TYPEINFO_RVA = 0xE647138;
inline constexpr std::uint64_t PLAYER_LOOP_HELPER_TYPEINFO_RVA = 0xE647100;
inline constexpr std::uint64_t PLAYER_MANAGER_STATIC_FIELDS_ACTIVE_LIST = game_offsets::PLAYER_MANAGER_STATIC_FIELDS_ACTIVE_LIST;
inline constexpr std::uint64_t PLAYER_MANAGER_STATIC_FIELDS_LIST = game_offsets::PLAYER_MANAGER_STATIC_FIELDS_LIST;
inline constexpr std::uint64_t IL2CPP_CLASS_STATIC_FIELDS_OFFSET = game_offsets::IL2CPP_CLASS_STATIC_FIELDS_OFFSET;
inline constexpr std::uint64_t PLAYERLOOPHELPER_RUNNERS = game_offsets::PLAYERLOOPHELPER_RUNNERS;
inline constexpr std::uint64_t LOOPRUNNER_ITEMS = game_offsets::LOOPRUNNER_ITEMS;
inline constexpr std::uint64_t LOOPRUNNER_TAIL = game_offsets::LOOPRUNNER_TAIL;
inline constexpr std::uint64_t DELAYPROMISE_TIMESPAN = game_offsets::DELAYPROMISE_TIMESPAN;
inline constexpr std::uint64_t DELAYPROMISE_ELAPSED = game_offsets::DELAYPROMISE_ELAPSED;
inline constexpr std::uint64_t DELAYPROMISE_INITIAL_FRAME = game_offsets::DELAYPROMISE_INITIAL_FRAME;
inline constexpr std::uint64_t DELAYPROMISE_SCALED_TIMESPAN = game_offsets::DELAYPROMISE_SCALED_TIMESPAN;
inline constexpr std::uint64_t DELAYPROMISE_SCALED_ELAPSED = game_offsets::DELAYPROMISE_SCALED_ELAPSED;
inline constexpr std::uint64_t DELAYPROMISE_SCALED_INITIAL_FRAME = game_offsets::DELAYPROMISE_SCALED_INITIAL_FRAME;
// UniTask delay-promise TypeInfo slots for beta 11977: основной класс и его
// вложенный <>c (запасной кандидат; имя проверяется в рантайме, см.
// fast_loot_memory.h). Разница основного и запасного слотов та же, что в
// прошлых сборках: DelayPromise идёт на 8 байт позже DelayIgnoreTimeScalePromise.
inline constexpr std::uint64_t FASTLOOT_DELAYPROMISE_TYPEINFO_RVA = 0xE655EB0;      // Cysharp...UniTask.DelayPromise
inline constexpr std::uint64_t FASTLOOT_DELAYPROMISE_TYPEINFO_RVA_ALT = 0xE657CB8; // DelayPromise.<>c
inline constexpr std::uint64_t FASTLOOT_IGNOREDELAY_TYPEINFO_RVA = 0xE655EA8;      // Cysharp...UniTask.DelayIgnoreTimeScalePromise
inline constexpr std::uint64_t FASTLOOT_IGNOREDELAY_TYPEINFO_RVA_ALT = 0xE657CB0;  // DelayIgnoreTimeScalePromise.<>c

// ---- Оружие: в сборке 11977 бета-ветка совпадает с релизом -----------------
// В 11959 копьё/ближний бой/инструмент отставали от релиза на 8 байт, поэтому
// профиль держал «до-11966» значения. В 11977 раскладки этих классов
// (Oxide.FPMelee, Oxide.FPTool, Oxide.FPSpear) поимённо совпали с релизом:
//   FPMelee  m_MaxReach 0x130, hitRadius 0x134, m_TimeBetweenAttacks 0x138,
//            pauseAfterAttack 0x13C (перед ними hitsPerAttack 0x128 и int 0x12C)
//   FPTool   m_ToolPurposes 0x168
//   FPSpear  m_MinTimeBetweenThrows 0x170, m_SpearPrefab 0x178, <...> 0x1C0
// Поэтому все эти группы берутся из релизного профиля без отличий.
inline constexpr std::uint64_t FPHITSCAN_CONFIG = game_offsets::FPHITSCAN_CONFIG;
inline constexpr std::uint64_t FPBOW_PROJECTILE_PREFAB = game_offsets::FPBOW_PROJECTILE_PREFAB;
inline constexpr std::uint64_t FPCROSSBOW_CONFIG = game_offsets::FPCROSSBOW_CONFIG;
inline constexpr std::uint64_t FPCROSSBOW_PROJECTILE_PREFAB = game_offsets::FPCROSSBOW_PROJECTILE_PREFAB;
inline constexpr std::uint64_t FPROCKETLAUNCHER_CONFIG = game_offsets::FPROCKETLAUNCHER_CONFIG;
inline constexpr std::uint64_t FPROCKETLAUNCHER_PROJECTILE_PREFAB = game_offsets::FPROCKETLAUNCHER_PROJECTILE_PREFAB;
inline constexpr std::uint64_t FPEXPLOSIVECHARGE_PROJECTILE_PREFAB = game_offsets::FPEXPLOSIVECHARGE_PROJECTILE_PREFAB;
inline constexpr std::uint64_t FPSNOWBALL_PROJECTILE_PREFAB = game_offsets::FPSNOWBALL_PROJECTILE_PREFAB;
inline constexpr std::uint64_t FPSPEAR_PROJECTILE_PREFAB = game_offsets::FPSPEAR_PROJECTILE_PREFAB;
inline constexpr std::uint64_t FPWEAPON_EFFECTIVE_RELOAD_PERIOD = game_offsets::FPWEAPON_EFFECTIVE_RELOAD_PERIOD;
inline constexpr std::uint64_t FPWEAPON_SHOT_PERIOD = game_offsets::FPWEAPON_SHOT_PERIOD;
inline constexpr std::uint64_t FPHITSCAN_FIRE_MODE = game_offsets::FPHITSCAN_FIRE_MODE;
inline constexpr std::uint64_t FPHITSCAN_FIRE_DURATION = game_offsets::FPHITSCAN_FIRE_DURATION;
inline constexpr std::uint64_t FPHITSCAN_BURST_LENGTH = game_offsets::FPHITSCAN_BURST_LENGTH;
inline constexpr std::uint64_t FPHITSCAN_BURST_DURATION = game_offsets::FPHITSCAN_BURST_DURATION;
inline constexpr std::uint64_t FPHITSCAN_BURST_PAUSE = game_offsets::FPHITSCAN_BURST_PAUSE;
inline constexpr std::uint64_t FPHITSCAN_BURST_WAIT = game_offsets::FPHITSCAN_BURST_WAIT;
inline constexpr std::uint64_t WAITFORSECONDS_SECONDS = game_offsets::WAITFORSECONDS_SECONDS;
inline constexpr std::uint64_t FPBOW_MIN_SHOT_PERIOD = game_offsets::FPBOW_MIN_SHOT_PERIOD;
inline constexpr std::uint64_t FPCROSSBOW_MIN_SHOT_PERIOD = game_offsets::FPCROSSBOW_MIN_SHOT_PERIOD;
inline constexpr std::uint64_t FPROCKETLAUNCHER_MIN_SHOT_PERIOD = game_offsets::FPROCKETLAUNCHER_MIN_SHOT_PERIOD;
inline constexpr std::uint64_t FPSNOWBALL_MIN_THROW_PERIOD = game_offsets::FPSNOWBALL_MIN_THROW_PERIOD;
inline constexpr std::uint64_t FPEXPLOSIVECHARGE_MIN_THROW_PERIOD = game_offsets::FPEXPLOSIVECHARGE_MIN_THROW_PERIOD;
inline constexpr std::uint64_t FPSPEAR_MIN_THROW_PERIOD = game_offsets::FPSPEAR_MIN_THROW_PERIOD;
inline constexpr std::uint64_t FPSPEAR_CACHED_THROW_PERIOD = game_offsets::FPSPEAR_CACHED_THROW_PERIOD;
inline constexpr std::uint64_t FPHITSCAN_EFFECTIVE_LOOP_RELOAD_PERIOD = game_offsets::FPHITSCAN_EFFECTIVE_LOOP_RELOAD_PERIOD;
inline constexpr std::uint64_t FPHITSCAN_CALCULATOR = game_offsets::FPHITSCAN_CALCULATOR;
inline constexpr std::uint64_t CALCULATOR_DISPERSION_CONFIG = game_offsets::CALCULATOR_DISPERSION_CONFIG;
inline constexpr std::uint64_t CALCULATOR_HITSCAN_CONFIG = game_offsets::CALCULATOR_HITSCAN_CONFIG;
inline constexpr std::uint64_t CALCULATOR_CACHED_BASE_DISPERSION = game_offsets::CALCULATOR_CACHED_BASE_DISPERSION;
inline constexpr std::uint64_t HITSCANCONFIG_MAX_DISTANCE = game_offsets::HITSCANCONFIG_MAX_DISTANCE;
inline constexpr std::uint64_t HITSCANCONFIG_BULLET_SPEED = game_offsets::HITSCANCONFIG_BULLET_SPEED;
inline constexpr std::uint64_t HITSCANCONFIG_FIRE_RATE = game_offsets::HITSCANCONFIG_FIRE_RATE;
inline constexpr std::uint64_t HITSCANCONFIG_PROJECTILES_PER_SHOT = game_offsets::HITSCANCONFIG_PROJECTILES_PER_SHOT;
inline constexpr std::uint64_t HITSCANCONFIG_BASE_DISPERSION = game_offsets::HITSCANCONFIG_BASE_DISPERSION;
inline constexpr std::uint64_t HITSCANCONFIG_FULL_RELOAD = game_offsets::HITSCANCONFIG_FULL_RELOAD;
inline constexpr std::uint64_t HITSCANCONFIG_LOOP_RELOAD = game_offsets::HITSCANCONFIG_LOOP_RELOAD;
inline constexpr std::uint64_t HITSCANCONFIG_BALLISTIC_PROFILE = game_offsets::HITSCANCONFIG_BALLISTIC_PROFILE;
inline constexpr std::uint64_t HITSCANCONFIG_DISPERSION_CONFIG = game_offsets::HITSCANCONFIG_DISPERSION_CONFIG;
inline constexpr std::uint64_t BALLISTICPROFILE_GRAVITY = game_offsets::BALLISTICPROFILE_GRAVITY;
inline constexpr std::uint64_t BALLISTICPROFILE_AIR_DRAG = game_offsets::BALLISTICPROFILE_AIR_DRAG;
inline constexpr std::uint64_t BALLISTICPROFILE_UNDERWATER_DRAG = game_offsets::BALLISTICPROFILE_UNDERWATER_DRAG;
inline constexpr std::uint64_t SHAFTEDPROJECTILE_MAX_DISTANCE = game_offsets::SHAFTEDPROJECTILE_MAX_DISTANCE;
inline constexpr std::uint64_t SHAFTEDPROJECTILE_LAUNCH_SPEED = game_offsets::SHAFTEDPROJECTILE_LAUNCH_SPEED;
inline constexpr std::uint64_t SHAFTEDPROJECTILE_GRAVITY = game_offsets::SHAFTEDPROJECTILE_GRAVITY;
inline constexpr std::uint64_t SHAFTEDPROJECTILE_AIR_DRAG = game_offsets::SHAFTEDPROJECTILE_AIR_DRAG;
inline constexpr std::uint64_t SHAFTEDPROJECTILE_UNDERWATER_DRAG = game_offsets::SHAFTEDPROJECTILE_UNDERWATER_DRAG;
inline constexpr std::uint64_t DISPERSIONCONFIG_MIN_DISPERSION = game_offsets::DISPERSIONCONFIG_MIN_DISPERSION;
inline constexpr std::uint64_t DISPERSIONCONFIG_MAX_DISPERSION = game_offsets::DISPERSIONCONFIG_MAX_DISPERSION;
inline constexpr std::uint64_t HITSCANCONFIG_RECOIL_CONFIG = game_offsets::HITSCANCONFIG_RECOIL_CONFIG;
inline constexpr std::uint64_t RECOILCONFIG_MIN_RECOIL = game_offsets::RECOILCONFIG_MIN_RECOIL;
inline constexpr std::uint64_t RECOILCONFIG_MAX_RECOIL = game_offsets::RECOILCONFIG_MAX_RECOIL;
inline constexpr std::uint64_t RECOILCONFIG_MIN_SIDE = game_offsets::RECOILCONFIG_MIN_SIDE;
inline constexpr std::uint64_t RECOILCONFIG_MAX_SIDE = game_offsets::RECOILCONFIG_MAX_SIDE;
inline constexpr std::uint64_t FPMELEE_MAX_REACH = game_offsets::FPMELEE_MAX_REACH;
inline constexpr std::uint64_t FPMELEE_HIT_RADIUS = game_offsets::FPMELEE_HIT_RADIUS;
inline constexpr std::uint64_t FPMELEE_TIME_BETWEEN_ATTACKS = game_offsets::FPMELEE_TIME_BETWEEN_ATTACKS;
inline constexpr std::uint64_t FPMELEE_PAUSE_AFTER_ATTACK = game_offsets::FPMELEE_PAUSE_AFTER_ATTACK;
inline constexpr std::uint64_t FPTOOL_TOOL_PURPOSES = game_offsets::FPTOOL_TOOL_PURPOSES;

// Oxide.GameControllerBase (static_fields) — раскладка поимённо совпала с релизом.
inline constexpr std::uint64_t GAME_CONTROLLER_TYPEINFO_RVA = 0xE640C70;
// HyperHug.Games.Oxide.Features.WorldStreaming.WorldStreamingController.
inline constexpr std::uint64_t WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA = 0xE64BC38;
inline constexpr std::uint64_t WORLD_STREAMING_GRASS_ENABLED_FIELD = 0x18; // static bool GrassEnabled
inline constexpr std::uint64_t WORLD_STREAMING_TREES_ENABLED_FIELD = 0x19; // static bool TreesEnabled / terrain bushes
inline constexpr std::uint64_t GAME_CONTROLLER_LOCAL_PLAYER_FIELD = game_offsets::GAME_CONTROLLER_LOCAL_PLAYER_FIELD;
inline constexpr std::uint64_t GAME_CONTROLLER_CAMERA_MANAGER_FIELD = game_offsets::GAME_CONTROLLER_CAMERA_MANAGER_FIELD;
inline constexpr std::uint64_t GAME_CONTROLLER_INPUT_HANDLER_FIELD = game_offsets::GAME_CONTROLLER_INPUT_HANDLER_FIELD;
inline constexpr std::uint64_t GAME_CONTROLLER_EVENT_HANDLER_FIELD = game_offsets::GAME_CONTROLLER_EVENT_HANDLER_FIELD;
inline constexpr std::uint64_t GAME_CONTROLLER_STATIC_SELF_FIELD = game_offsets::GAME_CONTROLLER_STATIC_SELF_FIELD;
inline constexpr std::uint64_t GAME_CONTROLLER_SCREEN_CONTROLS_FIELD = game_offsets::GAME_CONTROLLER_SCREEN_CONTROLS_FIELD;
inline constexpr std::uint64_t JOYSTICK_INPUT_VALUE = game_offsets::JOYSTICK_INPUT_VALUE;
inline constexpr std::uint64_t JOYSTICK_HANDLE_RANGE = game_offsets::JOYSTICK_HANDLE_RANGE;
inline constexpr std::uint64_t JOYSTICK_DEAD_ZONE = game_offsets::JOYSTICK_DEAD_ZONE;
inline constexpr std::uint64_t JOYSTICK_BACKGROUND = game_offsets::JOYSTICK_BACKGROUND;
inline constexpr std::uint64_t JOYSTICK_HANDLE = game_offsets::JOYSTICK_HANDLE;
inline constexpr std::uint64_t JOYSTICK_BASE_JOYSTICK = game_offsets::JOYSTICK_BASE_JOYSTICK;
inline constexpr std::uint64_t SCREEN_CONTROLS_HOLDER_FIXED = game_offsets::SCREEN_CONTROLS_HOLDER_FIXED;
inline constexpr std::uint64_t SCREEN_CONTROLS_HOLDER_FLOATING = game_offsets::SCREEN_CONTROLS_HOLDER_FLOATING;
inline constexpr std::uint64_t SCREEN_CONTROLS_HOLDER_FIELD = game_offsets::SCREEN_CONTROLS_HOLDER_FIELD;
inline constexpr std::uint64_t PLAYER_INPUT_HANDLER_OWNER_BACKREF = game_offsets::PLAYER_INPUT_HANDLER_OWNER_BACKREF;
inline constexpr std::uint64_t PLAYER_INPUT_MOVEMENT_INPUT = game_offsets::PLAYER_INPUT_MOVEMENT_INPUT;
inline constexpr std::uint64_t PLAYER_INPUT_YROTATION = game_offsets::PLAYER_INPUT_YROTATION;
inline constexpr std::uint64_t PLAYER_INPUT_TICK = game_offsets::PLAYER_INPUT_TICK;
inline constexpr std::uint64_t PLAYER_INPUT_HANDLER_ATTACK_HELD = game_offsets::PLAYER_INPUT_HANDLER_ATTACK_HELD;
inline constexpr std::uint64_t PLAYER_INPUT_HANDLER_OVERRIDE_A = game_offsets::PLAYER_INPUT_HANDLER_OVERRIDE_A;
inline constexpr std::uint64_t PLAYER_INPUT_HANDLER_OVERRIDE_B = game_offsets::PLAYER_INPUT_HANDLER_OVERRIDE_B;
// PlayerInputHandler.cGP is Nullable<bool> at 0x84 in dump.cs 11977 (same as release).
inline constexpr std::uint64_t PLAYER_INPUT_HANDLER_SPRINT_OVERRIDE = game_offsets::PLAYER_INPUT_HANDLER_SPRINT_OVERRIDE;
inline constexpr std::uint64_t NULLABLE_HAS_VALUE = game_offsets::NULLABLE_HAS_VALUE;
inline constexpr std::uint64_t NULLABLE_VALUE = game_offsets::NULLABLE_VALUE;
inline constexpr std::uint64_t NULLABLE_STRIDE = game_offsets::NULLABLE_STRIDE;
inline constexpr std::uint64_t PLAYER_INPUT_HANDLER_INPUT_ACTIONS = game_offsets::PLAYER_INPUT_HANDLER_INPUT_ACTIONS;
inline constexpr std::uint64_t PLAYER_INPUT_SPRINT = game_offsets::PLAYER_INPUT_SPRINT;
inline constexpr std::uint64_t LD_SPRINT_ACTION = game_offsets::LD_SPRINT_ACTION;
inline constexpr std::uint64_t INPUT_ACTION_INDEX = game_offsets::INPUT_ACTION_INDEX;
inline constexpr std::uint64_t INPUT_ACTION_STATE_HOLDER = game_offsets::INPUT_ACTION_STATE_HOLDER;
inline constexpr std::uint64_t INPUT_ACTION_STATE_HOLDER_STATE = game_offsets::INPUT_ACTION_STATE_HOLDER_STATE;
inline constexpr std::uint64_t INPUT_ACTION_STATE_STATES = game_offsets::INPUT_ACTION_STATE_STATES;
inline constexpr std::uint64_t INPUT_ACTION_STATE_STRIDE = game_offsets::INPUT_ACTION_STATE_STRIDE;
inline constexpr std::uint64_t INPUT_ACTION_PHASE_OFFSET = game_offsets::INPUT_ACTION_PHASE_OFFSET;
inline constexpr std::uint8_t INPUT_ACTION_PHASE_WAITING = game_offsets::INPUT_ACTION_PHASE_WAITING;
inline constexpr std::uint8_t INPUT_ACTION_PHASE_STARTED = game_offsets::INPUT_ACTION_PHASE_STARTED;
inline constexpr std::uint64_t CAMERA_MANAGER_CAMERA_FIELD = game_offsets::CAMERA_MANAGER_CAMERA_FIELD;

// ---- Oxide.PlayerManager ---------------------------------------------------
// В 11977 layout до animator (0x1B0) совпадал с релизом 11966. После
// playerFlags добавлены cnN (GameObject, 0x270) и cFH (hp, 0x278), поэтому
// хвост сдвинут на +0x10 (в релизе 11978 та же вставка: KSu 0x270, KSj 0x278 —
// хвосты беты и релиза теперь identical). Сверка по читаемым полям 11966->11977:
//   playerFlags     0x270 -> 0x280
//   prime / kicked  0x274/0x275 -> 0x284/0x285
//   userID          0x298 -> 0x2A8      teamName  0x2A0 -> 0x2B0
//   vehicleID       0x2A8 -> 0x2B8      seatID    0x2AC -> 0x2BC
//   clanId          0x2B0 -> 0x2C0      clanTag   0x2B8 -> 0x2C8
//   ziplineID       0x2CC -> 0x2DC      isImmortal 0x2D1 -> 0x2E1
//   VoicePlayerState 0x328 -> 0x338     hit-tracker service 0x348 -> 0x358
// observedPlayer/observedId/observedNetId/observeCameraDist/originalPosition
// and _Mirror_SyncVarHookDelegate_* confirm the same +0x10 tail shift.
// AimAssist and PlayerModelInfo reference remain at 0x258 and 0x260, before
// the two inserted fields.
inline constexpr std::uint64_t PLAYER_TRANSFORM = game_offsets::PLAYER_TRANSFORM;
inline constexpr std::uint64_t PLAYER_POSITION = game_offsets::PLAYER_POSITION;
inline constexpr std::uint64_t PLAYER_CHARACTER_MODEL = game_offsets::PLAYER_CHARACTER_MODEL;
inline constexpr std::uint64_t PLAYER_NICKLABEL = game_offsets::PLAYER_NICKLABEL;
inline constexpr std::uint64_t PLAYER_DISPLAY_NAME = game_offsets::PLAYER_DISPLAY_NAME;
inline constexpr std::uint64_t NICKLABEL_PLAYER_BACKREF = game_offsets::NICKLABEL_PLAYER_BACKREF;
inline constexpr std::uint64_t NICKLABEL_NICKNAME_TEXT = game_offsets::NICKLABEL_NICKNAME_TEXT;
inline constexpr std::uint64_t UI_TEXT_MTEXT = game_offsets::UI_TEXT_MTEXT;
inline constexpr std::uint64_t FPOBJECT_OBJECT_NAME = game_offsets::FPOBJECT_OBJECT_NAME;
inline constexpr std::uint64_t PLAYER_VOICE_STATE = 0x338;   // PlayerManager.cFu (Dissonance.VoicePlayerState)
inline constexpr std::uint64_t VOICE_STATE_NAME = game_offsets::VOICE_STATE_NAME;
inline constexpr std::uint64_t PLAYER_VOICE_PLAYER = game_offsets::PLAYER_VOICE_PLAYER;
inline constexpr std::uint64_t VOICE_PLAYER_TAG = game_offsets::VOICE_PLAYER_TAG;
inline constexpr std::uint64_t FPOBJECT_ITEM = game_offsets::FPOBJECT_ITEM;
// FPObject в бете 11977 совпадает с релизом: bool <cqS>k__BackingField @0x38
// (обфусцированное uhq — «вюха показана»), Item <cqZ>k__BackingField @0x40,
// Player @0xC0 (dumpbeta11977, класс FPObject TypeDefIndex 9897).
inline constexpr std::uint64_t FPOBJECT_SHOWN = game_offsets::FPOBJECT_SHOWN;
inline constexpr std::uint64_t ITEM_DATA = game_offsets::ITEM_DATA;
// Oxide.Item в бете 11977 побайтово совпадает с релизом (dumpbeta11977:
// cOq short @0x38, mods @0x40, stack @0x48) — имена обфусцированы иначе, раскладка та же.
inline constexpr std::uint64_t ITEMDATA_NAME = game_offsets::ITEMDATA_NAME;
inline constexpr std::uint64_t ITEMDATA_SHORTNAME = game_offsets::ITEMDATA_SHORTNAME;
// ItemData.m_Id @0x58 в бете 11977 на том же месте (dumpbeta11977, класс
// ItemData TypeDefIndex 10161) — карантин по id ствола переносим как есть.
inline constexpr std::uint64_t ITEMDATA_ID = game_offsets::ITEMDATA_ID;
inline constexpr std::uint64_t IL2CPP_STRING_LENGTH = game_offsets::IL2CPP_STRING_LENGTH;
inline constexpr std::uint64_t IL2CPP_STRING_CHARS = game_offsets::IL2CPP_STRING_CHARS;
inline constexpr std::uint64_t PLAYER_EVENT_HANDLER = game_offsets::PLAYER_EVENT_HANDLER;
inline constexpr std::uint64_t EVENT_HANDLER_MANAGER_BACKREF = game_offsets::EVENT_HANDLER_MANAGER_BACKREF;
// PlayerManager.playerEventHandler (IZq): Run 0x288, Crouch 0x290, Footstep 0x298,
// StepLength 0x2A0, Jump 0x2A8, Aim 0x2B0. Перед Run вставлен SetVehicleLock
// (0x280, +0x8), а между Crouch и Jump — Footstep и StepLength (+0x10),
// поэтому Aim относительно релиза 11966 уехал на +0x18. В релизе 11978 те же
// вставки (Footstep kAe<bool> 0x298, Aim kAj 0x2B0) — значение совпало.
inline constexpr std::uint64_t EVENT_HANDLER_AIM_ACTIVITY = 0x2B0;
inline constexpr std::uint64_t EVENT_HANDLER_MOVEMENT_INPUT = game_offsets::EVENT_HANDLER_MOVEMENT_INPUT;
inline constexpr std::uint64_t PLAYER_MANAGER_RESPAWNING = game_offsets::PLAYER_MANAGER_RESPAWNING;
inline constexpr std::uint64_t PLAYER_DEATH_HANDLER = game_offsets::PLAYER_DEATH_HANDLER;
inline constexpr std::uint64_t DEATH_HANDLER_REF_TARGET = game_offsets::DEATH_HANDLER_REF_TARGET;
inline constexpr std::uint64_t DEATH_HANDLER_DEATH = game_offsets::DEATH_HANDLER_DEATH;
inline constexpr std::uint64_t PLAYER_VITALS = game_offsets::PLAYER_VITALS;
inline constexpr std::uint64_t VITALS_HEALTH = game_offsets::VITALS_HEALTH;
inline constexpr std::uint64_t EVENT_HANDLER_HEALTH = game_offsets::EVENT_HANDLER_HEALTH;
inline constexpr std::uint64_t PLAYER_UKX = game_offsets::PLAYER_UKX; // 0x260 (лежит ДО вставки 0x270, совпадает с PLAYER_MODEL_INFO_REF)
inline constexpr std::uint64_t UKX_MODEL_INFO = game_offsets::UKX_MODEL_INFO;
// В dump.cs 11977 Oxide.EntityVitals сохраняет m_MinFallSpeed 0xC0 и
// m_MaxFallSpeed 0xC4; PlayerVitals.Player стоит на 0x100, PlayerEvent на
// 0x108, а PlayerManager.vitals — на 0xC8, как и в релизном профиле.
inline constexpr std::uint64_t VITALS_MIN_FALL_SPEED = game_offsets::VITALS_MIN_FALL_SPEED;
inline constexpr std::uint64_t VITALS_MAX_FALL_SPEED = game_offsets::VITALS_MAX_FALL_SPEED;
inline constexpr std::uint64_t VITALS_PLAYER_BACKREF = game_offsets::VITALS_PLAYER_BACKREF;
inline constexpr std::uint64_t PLAYER_INVENTORY = game_offsets::PLAYER_INVENTORY;
inline constexpr std::uint64_t PLAYER_WEAPON_REFERENCE = game_offsets::PLAYER_WEAPON_REFERENCE;
inline constexpr std::uint64_t PLAYER_WEAPONS_ARRAY = game_offsets::PLAYER_WEAPONS_ARRAY;
inline constexpr std::uint64_t INV_PLAYER_INVENTORY_DATA = game_offsets::INV_PLAYER_INVENTORY_DATA;
inline constexpr std::uint64_t INV_PLAYER_INVENTORY_CLIENT = game_offsets::INV_PLAYER_INVENTORY_CLIENT;

// ---- HyperHug...Weapons.PlayerWeapon (третье лицо) --------------------------
// В сборке 11977: animator 0xF8, playerWeaponViewReference 0x100, fPManager
// 0x110, SyncVar WeaponPiece 0x130, WeaponState 0x14C, а <player> — 0x160.
// Между WeaponPiece и WeaponState расположен новый int attackType (0x148).
inline constexpr std::uint64_t PLAYERWEAPON_VIEW = 0x100;            // hS playerWeaponViewReference
inline constexpr std::uint64_t PLAYERWEAPON_PIECE = 0x130;           // jUh (SyncVar weaponPiece)
inline constexpr std::uint64_t PLAYERWEAPON_PLAYER_BACKREF = 0x160;  // <player>k__BackingField
inline constexpr std::uint64_t WEAPONPIECE_ENABLED = game_offsets::WEAPONPIECE_ENABLED;
inline constexpr std::uint64_t WEAPONPIECE_NUMBER = game_offsets::WEAPONPIECE_NUMBER;
// Вьюха третьего лица (класс nM, был Eg/ON; в релизе 11978 — YU): WeaponBase
// 0x50, WeaponPiece 0x58, корневой Transform 0x70 — сдвиг +0x8 относительно
// релиза 11966 (релиз 11978 сошёлся с этими значениями). Оборачивающий
// декоратор по-прежнему лежит в первом поле (+0x10).
inline constexpr std::uint64_t WEAPONVIEW_WEAPON_BASE = 0x50;
inline constexpr std::uint64_t WEAPONVIEW_ROOT_TRANSFORM = 0x70;
inline constexpr std::uint64_t WEAPONVIEW_INNER = game_offsets::WEAPONVIEW_INNER;
inline constexpr std::uint64_t MODELINFO_RIGHT_WEAPON_HOLDER = game_offsets::MODELINFO_RIGHT_WEAPON_HOLDER;
inline constexpr std::uint64_t MODELINFO_LEFT_WEAPON_HOLDER = game_offsets::MODELINFO_LEFT_WEAPON_HOLDER;
inline constexpr std::uint64_t MODELINFO_HEAD = game_offsets::MODELINFO_HEAD;
inline constexpr std::uint64_t MODELINFO_EQUIPMENT_HOLDER = game_offsets::MODELINFO_EQUIPMENT_HOLDER;
inline constexpr std::uint64_t NPC_BRAIN_MODEL_INFO = game_offsets::NPC_BRAIN_MODEL_INFO;
inline constexpr std::uint64_t CHARANIM_PLAYER_MODEL_INFO = game_offsets::CHARANIM_PLAYER_MODEL_INFO;
inline constexpr std::uint64_t INVDATA_PLAYER_MODEL_INFO = game_offsets::INVDATA_PLAYER_MODEL_INFO;
inline constexpr std::uint64_t IL2CPP_CLASS_NAME = game_offsets::IL2CPP_CLASS_NAME;
inline constexpr std::uint64_t IL2CPP_CLASS_NAMESPACE = game_offsets::IL2CPP_CLASS_NAMESPACE;
inline constexpr std::uint64_t EVENT_HANDLER_LOOK_DIRECTION = game_offsets::EVENT_HANDLER_LOOK_DIRECTION;
inline constexpr std::uint64_t EVENT_HANDLER_LAST_HIT_POINT = game_offsets::EVENT_HANDLER_LAST_HIT_POINT;
inline constexpr std::uint64_t EVENT_HANDLER_LAST_HIT_TIME = game_offsets::EVENT_HANDLER_LAST_HIT_TIME;
inline constexpr std::uint64_t SYNC_VALUE_OFFSET = game_offsets::SYNC_VALUE_OFFSET;
inline constexpr std::uint64_t ACTIVITY_ACTIVE_FLAG = game_offsets::ACTIVITY_ACTIVE_FLAG;
inline constexpr std::uint64_t PLAYER_FP_MANAGER = game_offsets::PLAYER_FP_MANAGER;
inline constexpr std::uint64_t FPMANAGER_CURRENT_WEAPON = game_offsets::FPMANAGER_CURRENT_WEAPON;
inline constexpr std::uint64_t FPMANAGER_CURRENT_OBJECT = game_offsets::FPMANAGER_CURRENT_OBJECT;
inline constexpr std::uint64_t FPMANAGER_UZL = game_offsets::FPMANAGER_UZL;
inline constexpr std::uint64_t FPMANAGER_CTS = game_offsets::FPMANAGER_CTS;
inline constexpr std::uint64_t FPMANAGER_CTS2 = game_offsets::FPMANAGER_CTS2;
inline constexpr std::uint64_t FPMANAGER_UTCS = game_offsets::FPMANAGER_UTCS;
// §4.26: два bool-поля идут по релизной раскладке. Релиз 11978 получил тот же
// хвост FPManager (AssetReference 0x80, KLo 0xA0, <KHk> 0xC0), поэтому алиасы
// теперь дают бете точные значения (сверено с dump11978: релиз и бета 11977
// идентичны от 0x50 до 0xC1).
inline constexpr std::uint64_t FPMANAGER_UZJ = game_offsets::FPMANAGER_UZJ;
inline constexpr std::uint64_t FPMANAGER_UHF = game_offsets::FPMANAGER_UHF;
// Хвост Oxide.FPManager: AssetReference на 0x80, поле cdf на 0xAC и
// backing field <cdg>k__BackingField на 0xB8.
inline constexpr std::uint64_t FPMANAGER_AIM_BLEND = 0xB8;          // <cdg>k__BackingField
inline constexpr std::uint64_t FPOBJECT_PLAYER_BACKREF = game_offsets::FPOBJECT_PLAYER_BACKREF;
inline constexpr std::uint64_t FPWEAPON_IS_AIMING = game_offsets::FPWEAPON_IS_AIMING;
inline constexpr std::uint64_t FPOBJECT_RAYCAST_MANAGER = game_offsets::FPOBJECT_RAYCAST_MANAGER;
inline constexpr std::uint64_t FPOBJECT_EVENT_HANDLER = game_offsets::FPOBJECT_EVENT_HANDLER;
inline constexpr std::uint64_t RAYCASTMAN_PLAYER = game_offsets::RAYCASTMAN_PLAYER;
inline constexpr std::uint64_t RAYCASTMAN_RAY_LENGTH = game_offsets::RAYCASTMAN_RAY_LENGTH;
inline constexpr std::uint64_t RAYCASTMAN_AIM_RAY_LENGTH = game_offsets::RAYCASTMAN_AIM_RAY_LENGTH;
inline constexpr std::uint64_t RAYCASTMAN_SPHERE_RADIUS = game_offsets::RAYCASTMAN_SPHERE_RADIUS;
inline constexpr std::uint64_t RAYCASTMAN_TOO_CLOSE = game_offsets::RAYCASTMAN_TOO_CLOSE;
inline constexpr std::uint64_t RAYCASTMAN_LAYER_MASK = game_offsets::RAYCASTMAN_LAYER_MASK;
inline constexpr std::uint64_t RAYCASTMAN_AIM_LAYER_MASK = game_offsets::RAYCASTMAN_AIM_LAYER_MASK;
inline constexpr std::uint64_t GUM_RAYCAST_DATA = game_offsets::GUM_RAYCAST_DATA;
inline constexpr std::uint64_t GUM_AIM_RAYCAST = game_offsets::GUM_AIM_RAYCAST;
inline constexpr std::uint64_t GUI_VALUE = game_offsets::GUI_VALUE;
inline constexpr std::uint64_t GKO_HIT_OBJECT = game_offsets::GKO_HIT_OBJECT;
inline constexpr std::uint64_t GKO_HIT_ENTITY = game_offsets::GKO_HIT_ENTITY;
inline constexpr std::uint64_t GKO_HIT_PLAYER = game_offsets::GKO_HIT_PLAYER;
inline constexpr std::uint64_t GKO_RAYCAST_HIT = game_offsets::GKO_RAYCAST_HIT;
inline constexpr std::uint64_t GKO_RAY_ORIGIN = game_offsets::GKO_RAY_ORIGIN;
inline constexpr std::uint64_t PLAYER_MOUSE_LOOK = game_offsets::PLAYER_MOUSE_LOOK;
inline constexpr std::uint64_t MOUSELOOK_PITCH_YAW = game_offsets::MOUSELOOK_PITCH_YAW;
inline constexpr std::uint64_t PLAYER_RAYCAST_MANAGER = game_offsets::PLAYER_RAYCAST_MANAGER;
inline constexpr std::uint64_t PLAYER_VOICE_COLLIDER = game_offsets::PLAYER_VOICE_COLLIDER;
inline constexpr std::uint64_t PLAYER_TRIGGER_PROBE = game_offsets::PLAYER_TRIGGER_PROBE;
inline constexpr std::uint64_t PLAYER_AIM_ASSIST_CONFIG = game_offsets::PLAYER_AIM_ASSIST_CONFIG;
inline constexpr std::uint64_t PLAYER_AIM_ASSIST = game_offsets::PLAYER_AIM_ASSIST;
inline constexpr std::uint64_t PLAYER_MODEL_INFO_REF = game_offsets::PLAYER_MODEL_INFO_REF;
inline constexpr std::uint64_t FPOBJECT_ENABLE_AIM_ASSIST = game_offsets::FPOBJECT_ENABLE_AIM_ASSIST;
inline constexpr std::uint64_t KCC_MOTOR_OFFSET = game_offsets::KCC_MOTOR_OFFSET;
inline constexpr std::uint64_t KCC_MOTOR_CAPSULE = game_offsets::KCC_MOTOR_CAPSULE;
inline constexpr std::uint64_t NPC_BRAIN_ENTITY = game_offsets::NPC_BRAIN_ENTITY;
inline constexpr std::uint64_t NPC_DEATH_COLLIDERS = game_offsets::NPC_DEATH_COLLIDERS;
inline constexpr std::uint64_t AIMTARGET_ENTITY_HANDLER = game_offsets::AIMTARGET_ENTITY_HANDLER;
inline constexpr std::uint64_t AIMCONFIG_DISTANCE = game_offsets::AIMCONFIG_DISTANCE;
inline constexpr std::uint64_t AIMCONFIG_OUTER_SPEED = game_offsets::AIMCONFIG_OUTER_SPEED;
inline constexpr std::uint64_t AIMCONFIG_INNER_SPEED = game_offsets::AIMCONFIG_INNER_SPEED;
inline constexpr std::uint64_t AIMCONFIG_OUTER_ABS_SPEED = game_offsets::AIMCONFIG_OUTER_ABS_SPEED;
inline constexpr std::uint64_t AIMCONFIG_INNER_ABS_SPEED = game_offsets::AIMCONFIG_INNER_ABS_SPEED;
inline constexpr std::uint64_t AIMCONFIG_SWITCH_DELAY = game_offsets::AIMCONFIG_SWITCH_DELAY;
// Класс ассиста (AZT, было hwR) и AimAssistConfig/AimSensorData/AimTarget
// поимённо совпали с релизом: player 0x10, config 0x18, init 0x20, enabled 0x21,
// camera 0x28, raycastHit 0x30, cur 0x60/0x68, prev 0x98/0xA0, hit 0xDC,
// deltaRot 0x108.
inline constexpr std::uint64_t AIMASSIST_PLAYER = game_offsets::AIMASSIST_PLAYER;
inline constexpr std::uint64_t AIMASSIST_CONFIG = game_offsets::AIMASSIST_CONFIG;
inline constexpr std::uint64_t AIMASSIST_INIT_FLAG = game_offsets::AIMASSIST_INIT_FLAG;
inline constexpr std::uint64_t AIMASSIST_ENABLED = game_offsets::AIMASSIST_ENABLED;
inline constexpr std::uint64_t AIMASSIST_CAMERA = game_offsets::AIMASSIST_CAMERA;
inline constexpr std::uint64_t AIMASSIST_RAYCAST_HIT = game_offsets::AIMASSIST_RAYCAST_HIT;
inline constexpr std::uint64_t AIMASSIST_CUR_TARGET = game_offsets::AIMASSIST_CUR_TARGET;
inline constexpr std::uint64_t AIMASSIST_CUR_SENSOR = game_offsets::AIMASSIST_CUR_SENSOR;
inline constexpr std::uint64_t AIMASSIST_PREV_TARGET = game_offsets::AIMASSIST_PREV_TARGET;
inline constexpr std::uint64_t AIMASSIST_PREV_SENSOR = game_offsets::AIMASSIST_PREV_SENSOR;
inline constexpr std::uint64_t AIMASSIST_HIT_FLAG = game_offsets::AIMASSIST_HIT_FLAG;
inline constexpr std::uint64_t AIMASSIST_DELTA_ROT = game_offsets::AIMASSIST_DELTA_ROT;
inline constexpr std::uint64_t AIMSENSOR_COLLIDER = game_offsets::AIMSENSOR_COLLIDER;
inline constexpr std::uint64_t AIMSENSOR_START_LOOK_TIME = game_offsets::AIMSENSOR_START_LOOK_TIME;
inline constexpr std::uint64_t AIMSENSOR_LAST_SEEN_TIME = game_offsets::AIMSENSOR_LAST_SEEN_TIME;
inline constexpr std::uint64_t AIMSENSOR_LAST_RECT_TIME = game_offsets::AIMSENSOR_LAST_RECT_TIME;
inline constexpr std::uint64_t AIMSENSOR_DISTANCE = game_offsets::AIMSENSOR_DISTANCE;
inline constexpr std::uint64_t AIMSENSOR_POSITION = game_offsets::AIMSENSOR_POSITION;
inline constexpr std::uint64_t AIMSENSOR_DIRECTION = game_offsets::AIMSENSOR_DIRECTION;
inline constexpr std::uint64_t GAME_CONTROLLER_MAP_MANAGER = game_offsets::GAME_CONTROLLER_MAP_MANAGER;
inline constexpr std::uint64_t MAP_MANAGER_MAP_VIEW = game_offsets::MAP_MANAGER_MAP_VIEW;
inline constexpr std::uint64_t MAP_MANAGER_MARKER_SERVICE = game_offsets::MAP_MANAGER_MARKER_SERVICE;
inline constexpr std::uint64_t MAP_MANAGER_MAP_PRESENTER = game_offsets::MAP_MANAGER_MAP_PRESENTER;
inline constexpr std::uint64_t MARKER_INSTALLER_MAP_VIEW = game_offsets::MARKER_INSTALLER_MAP_VIEW;
inline constexpr std::uint64_t MAP_VIEW_ON_ENABLE_ACTION = game_offsets::MAP_VIEW_ON_ENABLE_ACTION;
inline constexpr std::uint64_t MAP_PRESENTER_MARKER_SERVICE = game_offsets::MAP_PRESENTER_MARKER_SERVICE;
inline constexpr std::uint64_t MARKER_SERVICE_ADDED_ACTION = game_offsets::MARKER_SERVICE_ADDED_ACTION;
inline constexpr std::uint64_t SCREEN_WINDOW_RECT_TRANSFORM = game_offsets::SCREEN_WINDOW_RECT_TRANSFORM;
inline constexpr std::uint64_t SCREEN_PRESENTER_VIEW_DICT = game_offsets::SCREEN_PRESENTER_VIEW_DICT;
inline constexpr std::uint64_t SCREEN_PRESENTER_MARKER_LIST = game_offsets::SCREEN_PRESENTER_MARKER_LIST;
inline constexpr std::uint64_t SCREEN_PRESENTER_INST_DICT = game_offsets::SCREEN_PRESENTER_INST_DICT;
inline constexpr std::uint64_t MARKER_DATA = game_offsets::MARKER_DATA;
inline constexpr std::uint64_t MARKER_ACTIVE = game_offsets::MARKER_ACTIVE;
inline constexpr std::uint64_t MARKER_DATA_TYPE = game_offsets::MARKER_DATA_TYPE;
inline constexpr std::uint64_t MARKER_DATA_WORLD_POS = game_offsets::MARKER_DATA_WORLD_POS;
inline constexpr std::uint64_t MARKER_DATA_HEAD_POS = game_offsets::MARKER_DATA_HEAD_POS;
inline constexpr std::uint64_t MARKER_DATA_TRANSFORM = game_offsets::MARKER_DATA_TRANSFORM;
inline constexpr std::uint64_t MARKER_DATA_USER_ID = game_offsets::MARKER_DATA_USER_ID;
inline constexpr std::uint64_t MARKER_DATA_IS_HIT = game_offsets::MARKER_DATA_IS_HIT;
inline constexpr std::uint64_t PLAYER_HIT_TRACKER = 0x358;   // PlayerManager.cFm (Yf, build 11977)
// Yf has Dictionary<uint, float> fields at +0x10 and +0x18; +0x18 remains
// the beta hit-map assumption used by this profile.
inline constexpr std::uint64_t HIT_TRACKER_HIT_DICT = game_offsets::HIT_TRACKER_HIT_DICT;
inline constexpr std::uint64_t SCREEN_MARKER_VIEW_CONTEXT = game_offsets::SCREEN_MARKER_VIEW_CONTEXT;
inline constexpr std::uint64_t SCREEN_MARKER_VIEW_MODULES = game_offsets::SCREEN_MARKER_VIEW_MODULES;
inline constexpr std::uint64_t SCREEN_MARKER_VIEW_MARKER = game_offsets::SCREEN_MARKER_VIEW_MARKER;
inline constexpr std::uint64_t NEARBY_VIEW_MODULE_CTX = game_offsets::NEARBY_VIEW_MODULE_CTX;
inline constexpr std::uint64_t NEARBY_VIEW_DATA = game_offsets::NEARBY_VIEW_DATA;
inline constexpr std::uint64_t NEARBY_VIEW_CACHED_HIT = game_offsets::NEARBY_VIEW_CACHED_HIT;
inline constexpr std::uint64_t MARKER_CTX_RAY_TARGET = game_offsets::MARKER_CTX_RAY_TARGET;
inline constexpr std::uint64_t MARKER_CTX_SKIP_OCCLUSION = game_offsets::MARKER_CTX_SKIP_OCCLUSION;
inline constexpr std::uint64_t MARKER_CTX_HIDDEN = game_offsets::MARKER_CTX_HIDDEN;
inline constexpr std::uint64_t OCCLUSION_MOD_CTX = game_offsets::OCCLUSION_MOD_CTX;
inline constexpr std::uint64_t OCCLUSION_MOD_MASK = game_offsets::OCCLUSION_MOD_MASK;
inline constexpr std::uint64_t OCCLUSION_MOD_INTERVAL = game_offsets::OCCLUSION_MOD_INTERVAL;
inline constexpr std::uint64_t OCCLUSION_MOD_NEXT_TIME = game_offsets::OCCLUSION_MOD_NEXT_TIME;
inline constexpr std::uint64_t OCCLUSION_MOD_HAS_VAL = game_offsets::OCCLUSION_MOD_HAS_VAL;
inline constexpr std::uint64_t OCCLUSION_MOD_IS_OCCLUDED = game_offsets::OCCLUSION_MOD_IS_OCCLUDED;
// Beta 11977 candidate: IWt (TypeDefIndex 6610), 25 static LayerMask fields
// at 0x00..0x60. HMJ_TYPEINFO_RVA keeps its legacy name for the shared validator.
inline constexpr std::uint64_t HMJ_TYPEINFO_RVA = 0xE6431D8;
inline constexpr std::int32_t MARKER_TYPE_NEARBY_PLAYER = game_offsets::MARKER_TYPE_NEARBY_PLAYER;
inline constexpr std::uint64_t RAYCASTHIT_DISTANCE = game_offsets::RAYCASTHIT_DISTANCE;
inline constexpr std::uint64_t RAYCASTHIT_POINT = game_offsets::RAYCASTHIT_POINT;
inline constexpr std::uint64_t RAYCASTHIT_NORMAL = game_offsets::RAYCASTHIT_NORMAL;
inline constexpr std::uint64_t RAYCASTHIT_COLLIDER = game_offsets::RAYCASTHIT_COLLIDER;
inline constexpr std::uint64_t PLAYER_KCC_REFERENCE = game_offsets::PLAYER_KCC_REFERENCE;
inline constexpr std::uint64_t KCC_PLAYER_BACKREF = game_offsets::KCC_PLAYER_BACKREF;
inline constexpr std::uint64_t KCC_HEAD_TRANSFORM = game_offsets::KCC_HEAD_TRANSFORM;
inline constexpr std::uint64_t KCC_NORMAL_HEIGHT = game_offsets::KCC_NORMAL_HEIGHT;
inline constexpr std::uint64_t KCC_CROUCH_HEIGHT = game_offsets::KCC_CROUCH_HEIGHT;
inline constexpr std::uint64_t KCC_HITBOX_ROOT = game_offsets::KCC_HITBOX_ROOT;
inline constexpr std::uint64_t HITBOX_ROOT_ARRAY = game_offsets::HITBOX_ROOT_ARRAY;
inline constexpr std::uint64_t HITBOX_SIZE = game_offsets::HITBOX_SIZE;
inline constexpr std::uint64_t HITBOX_CENTER = game_offsets::HITBOX_CENTER;
inline constexpr std::uint64_t HITBOX_AREA = game_offsets::HITBOX_AREA;
inline constexpr std::uint64_t KCC_LOOK_HEIGHT_OFFSET = game_offsets::KCC_LOOK_HEIGHT_OFFSET;
// KCC: весь «хвост» класса уехал на +0x70. Ориентир — структурная привязка
// Move: в обеих сборках он идёт сразу за PlayerInput и занимает 0x6C байт до
// следующего поля (Terrain). Релиз: PlayerInput 0x114 + 0x28 = Move 0x13C,
// Move 0x13C + 0x6C = Terrain 0x1A8. Бета: PlayerInput 0x184 + 0x28 =
// Move 0x1AC, Move 0x1AC + 0x6C = Terrain 0x218. (В бету добавлен ещё один
// Move — pLm 0xF0 — но он стоит ДО полей PlayerInput, а не после них.)
inline constexpr std::uint64_t KCC_MOVE = 0x1AC;
inline constexpr std::uint64_t KCC_VISIBILITY = 0x308; // public Visibility visibility; // 0x308 (0 = Visible, 1 = Hidden)
inline constexpr std::uint64_t KCC_MOTOR_ENABLED = 0x30D; // KCC.plc in dump.cs 11977
inline constexpr std::uint64_t KCC_HAS_OBSERVER_MOVE = 0x3C0; // KCC.plV in dump.cs 11977
inline constexpr std::int32_t KCC_MOVE_STATE_DEAD = game_offsets::KCC_MOVE_STATE_DEAD;
// Значение Move не изменилось (struct Move, TypeDefIndex 4452): State 0x00,
// Position 0x0C, RealVelocity 0x18 — dump.cs 11977.
inline constexpr std::uint64_t KCC_MOVE_STATE = game_offsets::KCC_MOVE_STATE;
inline constexpr std::uint64_t KCC_MOVE_REAL_VELOCITY = game_offsets::KCC_MOVE_REAL_VELOCITY;
inline constexpr std::uint64_t KCC_MOVE_REAL_VELOCITY_Y = game_offsets::KCC_MOVE_REAL_VELOCITY_Y;
inline constexpr std::uint64_t KCC_MOVE_BASE_VELOCITY = game_offsets::KCC_MOVE_BASE_VELOCITY;
inline constexpr std::uint64_t KCC_MOVE_BASE_VELOCITY_Y = game_offsets::KCC_MOVE_BASE_VELOCITY_Y;
inline constexpr std::int32_t KCC_MOVE_STATE_AIRBORNE = game_offsets::KCC_MOVE_STATE_AIRBORNE;
inline constexpr std::int32_t KCC_MOVE_STATE_CLAMP_MAX = game_offsets::KCC_MOVE_STATE_CLAMP_MAX;
inline constexpr std::uint64_t KCC_CHARACTER_ANIMATION = game_offsets::KCC_CHARACTER_ANIMATION;
inline constexpr std::uint64_t CHAR_ANIM_PLAYER_BACKREF = game_offsets::CHAR_ANIM_PLAYER_BACKREF;
inline constexpr std::uint64_t CHAR_ANIM_RAGDOLL = game_offsets::CHAR_ANIM_RAGDOLL;
inline constexpr std::uint64_t RAGDOLL_PELVIS_RIGIDBODY = game_offsets::RAGDOLL_PELVIS_RIGIDBODY;
// Ragdoll: перед m_SurfaceTexture вставлен bool m_Interpolate (0x60), поэтому
// m_SurfaceTexture, m_AutoAssignHitboxes, m_Bones и floatingObject уехали на
// +0x8. Из этого хвоста читается только m_Bones (Rigidbody[]), и он же
// проверяется по IL2CPP_ARRAY_LENGTH как массив.
inline constexpr std::uint64_t RAGDOLL_BONES_ARRAY = 0x90;   // было 0x88; m_Bones (Rigidbody[])
inline constexpr std::uint64_t RAGDOLL_BODYPART_TRANSFORM = game_offsets::RAGDOLL_BODYPART_TRANSFORM;
inline constexpr std::uint64_t IL2CPP_ARRAY_LENGTH = game_offsets::IL2CPP_ARRAY_LENGTH;
inline constexpr std::uint64_t TRANSFORM_CHILDREN_ARRAY = game_offsets::TRANSFORM_CHILDREN_ARRAY;
inline constexpr std::uint64_t TRANSFORM_CHILD_COUNT = game_offsets::TRANSFORM_CHILD_COUNT;
inline constexpr std::uint64_t TRANSFORM_HIERARCHY_DATA = game_offsets::TRANSFORM_HIERARCHY_DATA;
inline constexpr std::uint64_t TRANSFORM_HIERARCHY_INDEX = game_offsets::TRANSFORM_HIERARCHY_INDEX;
inline constexpr std::uint64_t TRANSFORM_HIERARCHY_MATRICES = game_offsets::TRANSFORM_HIERARCHY_MATRICES;
inline constexpr std::uint64_t TRANSFORM_HIERARCHY_INDICES = game_offsets::TRANSFORM_HIERARCHY_INDICES;
inline constexpr std::uint64_t COMPONENT_GAMEOBJECT = game_offsets::COMPONENT_GAMEOBJECT;
inline constexpr std::uint64_t GAMEOBJECT_COMPONENT_ARRAY = game_offsets::GAMEOBJECT_COMPONENT_ARRAY;
inline constexpr std::uint64_t COMPONENT_PAIR_PTR = game_offsets::COMPONENT_PAIR_PTR;
inline constexpr std::uint64_t GAMEOBJECT_NAME = game_offsets::GAMEOBJECT_NAME;
inline constexpr std::uint64_t GAMEOBJECT_NAME_GUESS = game_offsets::GAMEOBJECT_NAME_GUESS;
inline constexpr std::uint64_t IL2CPP_LIST_ITEMS = game_offsets::IL2CPP_LIST_ITEMS;
inline constexpr std::uint64_t IL2CPP_LIST_SIZE = game_offsets::IL2CPP_LIST_SIZE;
inline constexpr std::uint64_t IL2CPP_ARRAY_FIRST_ELEMENT = game_offsets::IL2CPP_ARRAY_FIRST_ELEMENT;
inline constexpr std::uint64_t NETWORK_CLIENT_TYPEINFO_RVA = 0xE646398;
inline constexpr std::uint64_t NETWORK_CLIENT_SPAWNED = game_offsets::NETWORK_CLIENT_SPAWNED;
// Таймнги Mirror: слота в бете нет — дампа беты не делали, а слоты metadata
// usage двигаются каждый билд. Поля те же (Mirror не пересобирали), поэтому
// смещения берём из release. Ноль в RVA = читать неоткуда, game.cpp остаётся
// на прежних константах задержки.
inline constexpr std::uint64_t NETWORK_SERVER_TYPEINFO_RVA = 0;
inline constexpr std::uint64_t NETWORK_CLIENT_STATICS_RVA = 0;
inline constexpr std::uint64_t NETWORK_TIME_TYPEINFO_RVA = 0;
inline constexpr std::uint64_t NETWORK_CLIENT_BUFFER_MULTIPLIER = game_offsets::NETWORK_CLIENT_BUFFER_MULTIPLIER;
inline constexpr std::uint64_t NETWORK_SERVER_TICK_RATE = game_offsets::NETWORK_SERVER_TICK_RATE;
inline constexpr std::uint64_t NETWORK_TIME_RTT = game_offsets::NETWORK_TIME_RTT;
inline constexpr std::uint64_t CHAR_SPEED_CONFIG_KCC = game_offsets::CHAR_SPEED_CONFIG_KCC;
inline constexpr std::uint64_t CHAR_SPEED_CONFIG_SINGLE_KCC = game_offsets::CHAR_SPEED_CONFIG_SINGLE_KCC;
inline constexpr std::uint64_t CHAR_SPEED_SPRINT = game_offsets::CHAR_SPEED_SPRINT;
inline constexpr std::uint64_t CHAR_SPEED_JOG = game_offsets::CHAR_SPEED_JOG;
inline constexpr std::uint64_t CHAR_SPEED_WALK = game_offsets::CHAR_SPEED_WALK;
inline constexpr std::uint64_t CHAR_SPEED_BACK_FACTOR = game_offsets::CHAR_SPEED_BACK_FACTOR;
inline constexpr std::uint64_t CHAR_SPEED_JUMP_HEIGHT = game_offsets::CHAR_SPEED_JUMP_HEIGHT;
inline constexpr std::uint64_t CHAR_SPEED_GRAVITY = game_offsets::CHAR_SPEED_GRAVITY;
inline constexpr std::uint64_t DICT_ENTRIES = game_offsets::DICT_ENTRIES;
inline constexpr std::uint64_t DICT_COUNT = game_offsets::DICT_COUNT;
inline constexpr std::uint64_t DICT_ENTRY_STRIDE = game_offsets::DICT_ENTRY_STRIDE;
inline constexpr std::uint64_t DICT_ENTRY_VALUE = game_offsets::DICT_ENTRY_VALUE;
inline constexpr std::uint64_t NETID_NET_ID = game_offsets::NETID_NET_ID;
inline constexpr std::uint64_t NETID_BEHAVIOURS = game_offsets::NETID_BEHAVIOURS;
// Oxide.Cupboard, Oxide.LootObject, Oxide.ItemPickup, Oxide.MineableObject,
// Oxide.LootItem, Oxide.HitBox, Oxide.GenericVitals, Oxide.PlayerDeathHandler,
// Oxide.RaycastManager, Oxide.Item, Oxide.ItemData, Oxide.PlayerInventory,
// Oxide.PlayerInventoryData, Mirror.NetworkClient, Oxide.WeaponPiece — все
// поимённо совпали с релизом (isLootable 0xB0, panelName 0xE0, m_Piece 0xF8,
// item 0xD8, amount 0xE0, m_CurrentHealth 0x78, m_RequiredToolPurpose 0x70,
// m_Experience 0xD4, size 0x24, center 0x30, m_HitArea 0x68, m_MaxHealth 0x88,
// death 0xF0, m_LayerMask 0x48, m_Id 0x58, spawned 0x30).
inline constexpr std::uint64_t CUPBOARD_IS_LOOTABLE = game_offsets::CUPBOARD_IS_LOOTABLE;
inline constexpr std::uint64_t CUPBOARD_AUTHORIZED_PLAYERS = game_offsets::CUPBOARD_AUTHORIZED_PLAYERS;
inline constexpr std::uint64_t CUPBOARD_PROTECTED_TIME = game_offsets::CUPBOARD_PROTECTED_TIME;
inline constexpr std::uint64_t CUPBOARD_LUXURY_BLOCKS = game_offsets::CUPBOARD_LUXURY_BLOCKS;
inline constexpr std::uint64_t PMP_ENTITY = game_offsets::PMP_ENTITY;
inline constexpr std::uint64_t PMK_HEALTH = game_offsets::PMK_HEALTH;
inline constexpr std::uint64_t ARP_LATEST_VALUE = game_offsets::ARP_LATEST_VALUE;
inline constexpr std::uint64_t HEALTH_VALUE = game_offsets::HEALTH_VALUE;
inline constexpr std::uint64_t VITALS_MAX_HEALTH = game_offsets::VITALS_MAX_HEALTH;
// Окно скана держится внутри .data и покрывает PlayerManager, IWt, Gf,
// WorldStreamingController и UniTask delay-promise TypeInfo slots.
inline constexpr std::uint64_t TOD_SCAN_RVA_BEGIN = 0xE640000;
inline constexpr std::uint64_t TOD_SCAN_RVA_END = 0xE660000;
// В 11977 TOD_Sky — Gf: static List<Gf> Mpd at 0x0, Cycle at 0x40.
// TOD_CycleParameters остаётся Hour 0x10, Day 0x14, Month 0x18, Year 0x1C.
// script.json TypeInfoPointers: Gf_TypeInfo at 0xE640D88.
inline constexpr std::uint64_t TOD_SKY_CYCLE = game_offsets::TOD_SKY_CYCLE;
inline constexpr std::uint64_t TOD_CYCLE_HOUR = game_offsets::TOD_CYCLE_HOUR;
inline constexpr std::uint64_t TOD_CYCLE_DAY = game_offsets::TOD_CYCLE_DAY;
inline constexpr std::uint64_t TOD_CYCLE_MONTH = game_offsets::TOD_CYCLE_MONTH;
inline constexpr std::uint64_t TOD_CYCLE_YEAR = game_offsets::TOD_CYCLE_YEAR;
inline constexpr std::uint64_t MINEABLE_LOOT = game_offsets::MINEABLE_LOOT;
inline constexpr std::uint64_t MINEABLE_FINISH_BONUS = game_offsets::MINEABLE_FINISH_BONUS;
inline constexpr std::uint64_t MINEABLE_CURRENT_HEALTH = game_offsets::MINEABLE_CURRENT_HEALTH;
inline constexpr std::uint64_t MINEABLE_HIT_ANCHOR = game_offsets::MINEABLE_HIT_ANCHOR;
inline constexpr std::uint64_t LOOTITEM_ITEM_NAME = game_offsets::LOOTITEM_ITEM_NAME;
inline constexpr std::uint64_t MINEABLE_MAX_HEALTH = game_offsets::MINEABLE_MAX_HEALTH;
inline constexpr std::uint64_t MINEABLE_FRACTION = game_offsets::MINEABLE_FRACTION;
inline constexpr std::uint64_t MINEABLE_ENTITY_TYPE = game_offsets::MINEABLE_ENTITY_TYPE;
inline constexpr std::uint64_t MINEABLE_REQUIRED_TOOL_PURPOSE = game_offsets::MINEABLE_REQUIRED_TOOL_PURPOSE;
inline constexpr std::uint64_t MINEABLE_EXPERIENCE = game_offsets::MINEABLE_EXPERIENCE;
inline constexpr std::uint64_t MINEABLE_EXTENSIONS = game_offsets::MINEABLE_EXTENSIONS;
// Экстеншены «крестика» (OreHitstreaks / OreHitstreaksMarker / TreeHitstreaks /
// HitMarkerItem) поимённо совпали с релизом: hitstreakIndex 0x20, шаблон 0x28,
// живой клон 0x30, collider 0x38, узел 0x40; у маркера руды meshRenderer 0x20,
// владелец 0x38, сглаживание 0x48, возраст 0x58; у дерева серия 0x48,
// маркер 0x50, точки X 0x88 и 0xA4; у HitMarkerItem lifetime 0x20,
// mFilter 0x28, renderer 0x30, mark 0x38, возраст 0xC8.
inline constexpr std::uint64_t OREHS_STREAK_INDEX = game_offsets::OREHS_STREAK_INDEX;
inline constexpr std::uint64_t OREHS_MARKER_TEMPLATE = game_offsets::OREHS_MARKER_TEMPLATE;
inline constexpr std::uint64_t OREHS_MARKER = game_offsets::OREHS_MARKER;
inline constexpr std::uint64_t OREHS_COLLIDER = game_offsets::OREHS_COLLIDER;
inline constexpr std::uint64_t OREHS_MINEABLE = game_offsets::OREHS_MINEABLE;
inline constexpr std::uint64_t OREMARK_RENDERER = game_offsets::OREMARK_RENDERER;
inline constexpr std::uint64_t OREMARK_OWNER = game_offsets::OREMARK_OWNER;
inline constexpr std::uint64_t OREMARK_SMOOTH = game_offsets::OREMARK_SMOOTH;
inline constexpr std::uint64_t OREMARK_AGE = game_offsets::OREMARK_AGE;
inline constexpr float OREMARK_LIFETIME = game_offsets::OREMARK_LIFETIME;
inline constexpr std::uint64_t TREEHS_MARKER_TEMPLATE = game_offsets::TREEHS_MARKER_TEMPLATE;
inline constexpr std::uint64_t TREEHS_STREAK = game_offsets::TREEHS_STREAK;
inline constexpr std::uint64_t TREEHS_MARKER = game_offsets::TREEHS_MARKER;
inline constexpr std::uint64_t TREEHS_SPOT_A = game_offsets::TREEHS_SPOT_A;
inline constexpr std::uint64_t TREEHS_SPOT_B = game_offsets::TREEHS_SPOT_B;
inline constexpr std::uint64_t HITMARK_LIFETIME = game_offsets::HITMARK_LIFETIME;
inline constexpr std::uint64_t HITMARK_FILTER = game_offsets::HITMARK_FILTER;
inline constexpr std::uint64_t HITMARK_RENDERER = game_offsets::HITMARK_RENDERER;
inline constexpr std::uint64_t HITMARK_MARK = game_offsets::HITMARK_MARK;
inline constexpr std::uint64_t HITMARK_AGE = game_offsets::HITMARK_AGE;
inline constexpr std::uint64_t PLAYER_USER_ID = 0x2A8;    // было 0x298
inline constexpr std::uint64_t PLAYER_TEAM_NAME = 0x2B0;  // было 0x2A0
inline constexpr std::uint64_t PLAYER_CLAN_ID = 0x2C0;    // было 0x2B0
inline constexpr std::uint64_t PLAYER_CLAN_TAG = 0x2C8;   // было 0x2B8
inline constexpr std::uint64_t PLAYER_VEHICLE_ID = 0x2B8; // было 0x2A8
inline constexpr std::uint64_t PLAYER_SEAT_ID = 0x2BC;    // было 0x2AC
inline constexpr std::uint64_t PLAYER_ZIPLINE_ID = 0x2DC; // было 0x2CC
inline constexpr std::uint64_t LOOTOBJECT_INVENTORY = game_offsets::LOOTOBJECT_INVENTORY;
inline constexpr std::uint64_t LOOTOBJECT_IS_LOOTABLE = game_offsets::LOOTOBJECT_IS_LOOTABLE;
inline constexpr std::uint64_t LOOTOBJECT_PANEL_NAME = game_offsets::LOOTOBJECT_PANEL_NAME;
inline constexpr std::uint64_t LOOTOBJECT_BUILDING_PIECE = game_offsets::LOOTOBJECT_BUILDING_PIECE;
inline constexpr std::uint64_t ITEMPICKUP_ITEM_OBJECT = game_offsets::ITEMPICKUP_ITEM_OBJECT;
inline constexpr std::uint64_t ITEMPICKUP_SHORTNAME = game_offsets::ITEMPICKUP_SHORTNAME;
inline constexpr std::uint64_t ITEMPICKUP_AMOUNT = game_offsets::ITEMPICKUP_AMOUNT;
inline constexpr std::uint64_t GAME_CONTROLLER_NET_IDENTITY_FIELD = game_offsets::GAME_CONTROLLER_NET_IDENTITY_FIELD;

// ---- VIEWMODEL: кастомный FOV и положение рук (Oxide.TransformOffset) -------
// Oxide.TransformOffset поимённо совпал с релизом (m_LerpSpeed 0x20,
// m_Position 0x24, кэш текущей позиции 0x44), поэтому правятся только слоты
// самого Oxide.FPMotion — его перестройка сохраняется в dump.cs 11977:
//   убраны m_MovementSway/m_RotationSway/aimScale/m_WalkBob/m_AimBob/m_RunBob/
//   m_LandBob/m_MaxLandSpeed, на их место встал aimMotion (0x38);
//   позы сдвинулись в начало класса: Idle 0x40, Run 0x48, RunReload 0x50,
//   Attack 0x58, Aim 0x60, Look 0x68, TooClose 0x70;
//   m_OnLadderOffset и m_JumpOffset ИСЧЕЗЛИ вовсе (их роль уехала в
//   weaponBody 0x78);
//   Player 0xE8 -> 0x120, ссылка на оружие 0xC8 -> 0xD0.
inline constexpr std::uint64_t FPMANAGER_WORLD_FOV_TARGET = 0xAC; // was 0x9C; FPManager.cdf
inline constexpr std::uint64_t FPOBJECT_DRAW_EVENT = game_offsets::FPOBJECT_DRAW_EVENT;
inline constexpr std::uint64_t FPDRAW_ACTION = game_offsets::FPDRAW_ACTION;
inline constexpr std::uint64_t DELEGATE_TARGET = game_offsets::DELEGATE_TARGET;
inline constexpr std::uint64_t MULTICAST_DELEGATES = game_offsets::MULTICAST_DELEGATES;
inline constexpr std::uint64_t FPMOTION_OBJECT = 0xD0;  // FPMotion.cqp (FPObject)
inline constexpr std::uint64_t FPMOTION_PLAYER = 0x120; // FPMotion.Player
inline constexpr std::uint64_t TRANSFORMOFFSET_POSITION = game_offsets::TRANSFORMOFFSET_POSITION;
inline constexpr std::uint64_t TRANSFORMOFFSET_CURRENT_POSITION = game_offsets::TRANSFORMOFFSET_CURRENT_POSITION;
inline constexpr std::uint64_t FPMOTION_IDLE_OFFSET = 0x40;
inline constexpr std::uint64_t FPMOTION_RUN_OFFSET = 0x48;
inline constexpr std::uint64_t FPMOTION_RUN_RELOAD_OFFSET = 0x50;
inline constexpr std::uint64_t FPMOTION_ATTACK_OFFSET = 0x58;
inline constexpr std::uint64_t FPMOTION_AIM_OFFSET = 0x60;
// m_OnLadderOffset и m_JumpOffset в 11977 не существуют. Слоты намеренно
// указывают на профиль прицеливания: контроллер рук уже умеет отличать такую
// ситуацию («префаб может делить TransformOffset между позами») и просто
// пропускает слот, сняв с него владение, — вместо того, чтобы читать мусор и
// помечать кастомные руки как Failed.
inline constexpr std::uint64_t FPMOTION_LADDER_OFFSET = FPMOTION_AIM_OFFSET;
inline constexpr std::uint64_t FPMOTION_JUMP_OFFSET = FPMOTION_AIM_OFFSET;
inline constexpr std::uint64_t FPMOTION_LOOK_OFFSET = 0x68;
inline constexpr std::uint64_t FPMOTION_CLOSE_OFFSET = 0x70;
// «Активный» TransformOffset в перестроенном классе не опознан; слот не читает
// ни один контроллер, значение оставлено позиционно первым TransformOffset
// после ссылок на оружие.
inline constexpr std::uint64_t FPMOTION_ACTIVE_OFFSET = 0xE8;
inline constexpr std::uint64_t TRANSFORMOFFSET_LERP_SPEED = game_offsets::TRANSFORMOFFSET_LERP_SPEED;

// PlayerWeapon 11977: перед playerWeaponViewReference вставлены новые поля,
// поэтому FP-вьюха уехала. SyncVar-тройка определена позиционно в этом же
// профиле (piece 0x130, state 0x14C); playerWeaponViewReference 0x100.
// WeaponPiece в 11977 — 0x18 байт (добавлен bool @0x10), поэтому передаётся
// по ссылке; CancellationTokenSource в PlayerFPWeaponView уехал с 0x38.

} // namespace game_offsets_beta
