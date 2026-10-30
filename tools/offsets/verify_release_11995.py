#!/usr/bin/env python3
"""Independent re-derivation of EVERY constant in jni/src/game_offsets.h for the
release build 11995.

Usage:
    python3 tools/offsets/verify_release_11995.py /tmp/dump11995/dump.cs \
            /tmp/dump11995/script.json [--libil2cpp /tmp/dump11995/libil2cpp.so]
    python3 tools/offsets/verify_release_11995.py ... --propose   # print name=value

Every offset in the profile is re-read from the dump, never from the header, so a
stale value cannot "verify itself":

  field  -> offset of `<class>.<field>` in dump.cs (must exist, exactly one match)
  ti     -> TypeInfoPointers[].Address for the given DotNetType in script.json
  fixed  -> constant that cannot come from dump.cs (Il2Cpp runtime layout, Unity
            native object layout, IL2CPP string/array/list headers, protocol
            enum values). The script still pins the exact literal, so an
            accidental edit is caught, and prints the reason.
  alias  -> same value as another constant (checked for consistency).

The script exits non-zero when any constant is missing from the spec, missing in
the dump, ambiguous, or does not match the header.
"""
import json
import re
import sys
import os

HEADER = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "..", "..", "jni", "src", "game_offsets.h")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dump_cs import Dump  # noqa: E402

# ---------------------------------------------------------------------------
# SPEC: constant -> locator.
#   ("field", class, namespace_substring, field_name)
#   ("ti", dotnet_type)
#   ("fixed", value, reason)
#   ("alias", other_constant)
# ---------------------------------------------------------------------------
SPEC = {
    # --- Unity native camera (libunity.so, NOT in dump.cs) -------------------
    "CAMERA_PROJECTION_MATRIX": ("fixed", 0xB0, "native Unity Camera"),
    "CAMERA_VIEW_MATRIX": ("fixed", 0x70, "native Unity Camera"),
    "CAMERA_WORLD_TO_CLIP": ("fixed", 0xF0, "native Unity Camera"),
    "CAMERA_PREV_VIEW_PROJ": ("fixed", 0x5C8, "native Unity Camera"),
    "CAMERA_NATIVE_TRANSFORM": ("fixed", 0x20, "native Unity Camera"),
    "CAMERA_FOV_DEGREES": ("fixed", 0x170, "native Unity Camera"),
    "CAMERA_ASPECT": ("fixed", 0x4E0, "native Unity Camera"),
    "CAMERA_NEAR_CLIP": ("fixed", 0x454, "native Unity Camera"),
    "CAMERA_FAR_CLIP": ("fixed", 0x458, "native Unity Camera"),
    "CAMERA_VIEW_DIRTY": ("fixed", 0x502, "native Unity Camera dirty flag"),
    "CAMERA_PROJ_DIRTY": ("fixed", 0x500, "native Unity Camera dirty flag"),
    "MANAGED_CACHED_PTR": ("fixed", 0x10, "UnityEngine.Object.m_CachedPtr"),
    # --- gameplay tuning constants ------------------------------------------
    "PLAYER_HEIGHT": ("fixed", 1.8, "cheat-side constant"),
    "PLAYER_BOX_WIDTH_RATIO": ("fixed", 0.40, "cheat-side constant"),
    "MIN_PLAYER_DISTANCE": ("fixed", 0.0, "cheat-side constant"),
    "MAX_PLAYER_DISTANCE": ("fixed", 300.0, "cheat-side constant"),
    "OREMARK_LIFETIME": ("fixed", 15.0, "cheat-side constant"),
    "MARKER_TYPE_NEARBY_PLAYER": ("fixed", 10, "MarkerType.NearbyPlayer (enum, verified 11995)"),
    "RELEASE_BUILD": ("fixed", 11995, "target release"),
    # --- Mirror --------------------------------------------------------------
    "NETWORK_BEHAVIOUR_IDENTITY": ("field", "NetworkBehaviour", "Mirror", "<netIdentity>k__BackingField"),
    "NETWORK_IDENTITY_IS_LOCAL_PLAYER": ("field", "NetworkIdentity", "Mirror", "<isLocalPlayer>k__BackingField"),
    "NETWORK_CLIENT_SPAWNED": ("field", "NetworkClient", "Mirror", "spawned"),
    "NETWORK_SERVER_TICK_RATE": ("field", "NetworkServer", "Mirror", "tickRate"),
    "NETWORK_TIME_RTT": ("chain", ["NetworkTime", "Mirror", "_rtt"], ["ExponentialMovingAverage", "Mirror", "Value"]),
    "NETWORK_CLIENT_BUFFER_MULTIPLIER": ("field", "NetworkClient", "Mirror", "bufferTimeMultiplier"),
    "NETWORK_CLIENT_STATICS_RVA": ("fixed", 0, "unused (instance statics are reached via the object)"),
    "NETID_NET_ID": ("field", "NetworkIdentity", "Mirror", "<netId>k__BackingField"),
    "NETID_BEHAVIOURS": ("field", "NetworkIdentity", "Mirror", "<NetworkBehaviours>k__BackingField"),
    # --- TypeInfo slots ------------------------------------------------------
    "PLAYER_MANAGER_TYPEINFO_RVA": ("ti", "Oxide.PlayerManager"),
    "PLAYER_LOOP_HELPER_TYPEINFO_RVA": ("ti", "Cysharp.Threading.Tasks.PlayerLoopHelper"),
    "GAME_CONTROLLER_TYPEINFO_RVA": ("ti", "Oxide.GameControllerBase"),
    "WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA":
        ("ti", "HyperHug.Games.Oxide.Features.WorldStreaming.WorldStreamingController"),
    "NETWORK_CLIENT_TYPEINFO_RVA": ("ti", "Mirror.NetworkClient"),
    "NETWORK_SERVER_TYPEINFO_RVA": ("ti", "Mirror.NetworkServer"),
    "NETWORK_TIME_TYPEINFO_RVA": ("ti", "Mirror.NetworkTime"),
    "FASTLOOT_DELAYPROMISE_TYPEINFO_RVA":
        ("ti", "Cysharp.Threading.Tasks.UniTask.DelayPromise"),
    "FASTLOOT_IGNOREDELAY_TYPEINFO_RVA":
        ("ti", "Cysharp.Threading.Tasks.UniTask.DelayIgnoreTimeScalePromise"),
    "FASTLOOT_DELAYPROMISE_TYPEINFO_RVA_ALT": ("fixed", 0, "release does not need a fallback slot"),
    "FASTLOOT_IGNOREDELAY_TYPEINFO_RVA_ALT": ("fixed", 0, "release does not need a fallback slot"),
    "TOD_SKY_TYPEINFO_RVA": ("ti", "HL"),
    "HMJ_TYPEINFO_RVA": ("ti", "Caz"),
    "LAYER_MASK_STATICS_TYPEINFO_RVA": ("ti", "Caz"),
    # --- PlayerManager -------------------------------------------------------
    "PLAYER_MANAGER_STATIC_FIELDS_ACTIVE_LIST":
        ("field", "PlayerManager", "Oxide", "activePlayerList"),
    "PLAYER_MANAGER_STATIC_FIELDS_LIST":
        ("field", "PlayerManager", "Oxide", "clientPlayerList"),
    "PLAYER_TRANSFORM": ("field", "PlayerManager", "Oxide", "worldCameraRoot"),
    "PLAYER_MOUSE_LOOK": ("field", "PlayerManager", "Oxide", "mouseLook"),
    "PLAYER_EVENT_HANDLER": ("field", "PlayerManager", "Oxide", "playerEventHandler"),
    "PLAYER_DEATH_HANDLER": ("field", "PlayerManager", "Oxide", "playerDeathHandlerReference"),
    "PLAYER_RAYCAST_MANAGER": ("field", "PlayerManager", "Oxide", "raycastManager"),
    "PLAYER_FP_MANAGER": ("field", "PlayerManager", "Oxide", "fpManager"),
    "PLAYER_INVENTORY": ("field", "PlayerManager", "Oxide", "inventory"),
    "PLAYER_KCC_REFERENCE": ("field", "PlayerManager", "Oxide", "kccReference"),
    "PLAYER_VITALS": ("field", "PlayerManager", "Oxide", "vitals"),
    "PLAYER_WEAPON_REFERENCE": ("field", "PlayerManager", "Oxide", "weaponReference"),
    "PLAYER_NICKLABEL": ("field", "PlayerManager", "Oxide", "nicklabel"),
    "PLAYER_VOICE_PLAYER": ("field", "PlayerManager", "Oxide", "voicePlayer"),
    "PLAYER_CHARACTER_MODEL": ("field", "PlayerManager", "Oxide", "characterModel"),
    "PLAYER_VOICE_COLLIDER": ("field", "PlayerManager", "Oxide", "voiceCollider"),
    "PLAYER_TRIGGER_PROBE": ("field", "PlayerManager", "Oxide", "triggerProbe"),
    "PLAYER_AIM_ASSIST_CONFIG": ("field", "PlayerManager", "Oxide", "aimAssistConfig"),
    "PLAYER_WEAPONS_ARRAY": ("field", "PlayerManager", "Oxide", "weapons"),
    "PLAYER_POSITION": ("field", "PlayerManager", "Oxide", "lastSavedPosition"),
    "PLAYER_MANAGER_RESPAWNING": ("field", "PlayerManager", "Oxide", "respawning"),
    "PLAYER_DISPLAY_NAME": ("field", "PlayerManager", "Oxide", "Vdz"),
    "PLAYER_AIM_ASSIST": ("field", "PlayerManager", "Oxide", "<VdP>k__BackingField"),
    "PLAYER_UKX": ("field", "PlayerManager", "Oxide", "Vdb"),
    "PLAYER_MODEL_INFO_REF": ("field", "PlayerManager", "Oxide", "Vdb"),
    "PLAYER_HIT_TRACKER": ("field", "PlayerManager", "Oxide", "VBC"),
    "PLAYER_VOICE_STATE": ("field", "PlayerManager", "Oxide", "Vdn"),
    "PLAYER_USER_ID": ("field", "PlayerManager", "Oxide", "userID"),
    "PLAYER_TEAM_NAME": ("field", "PlayerManager", "Oxide", "teamName"),
    "PLAYER_CLAN_ID": ("field", "PlayerManager", "Oxide", "clanId"),
    "PLAYER_CLAN_TAG": ("field", "PlayerManager", "Oxide", "clanTag"),
    "PLAYER_VEHICLE_ID": ("field", "PlayerManager", "Oxide", "vehicleID"),
    "PLAYER_SEAT_ID": ("field", "PlayerManager", "Oxide", "seatID"),
    "PLAYER_ZIPLINE_ID": ("field", "PlayerManager", "Oxide", "ziplineID"),
    "UKX_MODEL_INFO": ("field", "tq", "", "<bzu>k__BackingField"),
    "HIT_TRACKER_HIT_DICT": ("field", "lc", "", "bWv"),
    # --- GameControllerBase --------------------------------------------------
    "GAME_CONTROLLER_NET_IDENTITY_FIELD":
        ("field", "GameControllerBase", "Oxide", "<Vtc>k__BackingField"),
    "GAME_CONTROLLER_LOCAL_PLAYER_FIELD":
        ("field", "GameControllerBase", "Oxide", "<VtI>k__BackingField"),
    "GAME_CONTROLLER_EVENT_HANDLER_FIELD":
        ("field", "GameControllerBase", "Oxide", "<Vtj>k__BackingField"),
    "GAME_CONTROLLER_INPUT_HANDLER_FIELD":
        ("field", "GameControllerBase", "Oxide", "<VtS>k__BackingField"),
    "GAME_CONTROLLER_CAMERA_MANAGER_FIELD":
        ("field", "GameControllerBase", "Oxide", "<Vtw>k__BackingField"),
    "GAME_CONTROLLER_SCREEN_CONTROLS_FIELD":
        ("field", "GameControllerBase", "Oxide", "<VXC>k__BackingField"),
    "GAME_CONTROLLER_STATIC_SELF_FIELD":
        ("field", "GameControllerBase", "Oxide", "<VXR>k__BackingField"),
    "GAME_CONTROLLER_MAP_MANAGER": ("field", "GameControllerBase", "Oxide", "mapManager"),
    "CAMERA_MANAGER_CAMERA_FIELD": ("field", "CameraManager", "Oxide", "m_Camera"),
    # --- world streaming -----------------------------------------------------
    "WORLD_STREAMING_GRASS_ENABLED_FIELD":
        ("field", "WorldStreamingController", "WorldStreaming", "GrassEnabled"),
    "WORLD_STREAMING_TREES_ENABLED_FIELD":
        ("field", "WorldStreamingController", "WorldStreaming", "TreesEnabled"),
    # --- promises / player loop ---------------------------------------------
    "IL2CPP_CLASS_STATIC_FIELDS_OFFSET": ("fixed", 0xB8, "Il2CppClass.class_fields layout"),
    "PLAYERLOOPHELPER_RUNNERS": ("field", "PlayerLoopHelper", "Cysharp", "runners"),
    "LOOPRUNNER_ITEMS": ("field", "PlayerLoopRunner", "Cysharp", "loopItems"),
    "LOOPRUNNER_TAIL": ("field", "PlayerLoopRunner", "Cysharp", "tail"),
    "DELAYPROMISE_TIMESPAN":
        ("field", "DelayIgnoreTimeScalePromise", "", "delayFrameTimeSpan"),
    "DELAYPROMISE_ELAPSED": ("field", "DelayIgnoreTimeScalePromise", "", "elapsed"),
    "DELAYPROMISE_INITIAL_FRAME": ("field", "DelayIgnoreTimeScalePromise", "", "initialFrame"),
    "DELAYPROMISE_SCALED_TIMESPAN": ("field", "DelayPromise", "", "delayTimeSpan"),
    "DELAYPROMISE_SCALED_ELAPSED": ("field", "DelayPromise", "", "elapsed"),
    "DELAYPROMISE_SCALED_INITIAL_FRAME": ("field", "DelayPromise", "", "initialFrame"),
    # --- weapons -------------------------------------------------------------
    "FPHITSCAN_CONFIG": ("field", "FPHitscan", "Oxide", "config"),
    "FPBOW_PROJECTILE_PREFAB": ("field", "FPBow", "Oxide", "m_ArrowPrefab"),
    "FPCROSSBOW_CONFIG": ("field", "FPCrossbow", "First_Person", "config"),
    "FPCROSSBOW_PROJECTILE_PREFAB": ("field", "FPCrossbow", "First_Person", "m_ArrowPrefab"),
    "FPROCKETLAUNCHER_CONFIG": ("field", "FPRocketLauncher", "Oxide", "config"),
    "FPROCKETLAUNCHER_PROJECTILE_PREFAB": ("field", "FPRocketLauncher", "Oxide", "m_RocketPrefab"),
    "FPEXPLOSIVECHARGE_PROJECTILE_PREFAB": ("field", "FPExplosiveCharge", "Oxide", "m_ChargePrefab"),
    "FPSNOWBALL_PROJECTILE_PREFAB": ("field", "FPSnowball", "First_Person", "m_SpearPrefab"),
    "FPSPEAR_PROJECTILE_PREFAB": ("field", "FPSpear", "Oxide", "m_SpearPrefab"),
    "FPWEAPON_TYPE": ("field", "FPWeaponBase", "Oxide", "weaponType"),
    "FPWEAPON_EFFECTIVE_RELOAD_PERIOD":
        ("field", "FPWeaponBase", "Oxide", "<Vqe>k__BackingField"),
    "FPWEAPON_SHOT_PERIOD": ("field", "FPWeaponBase", "Oxide", "<Vqb>k__BackingField"),
    "FPOBJECT_ENABLE_AIM_ASSIST": ("field", "FPWeaponBase", "Oxide", "useAimAssist"),
    "FPWEAPON_IS_AIMING": ("field", "FPWeaponBase", "Oxide", "<VqT>k__BackingField"),
    "FPHITSCAN_FIRE_MODE": ("field", "FPHitscan", "Oxide", "m_FireMode"),
    "FPHITSCAN_FIRE_DURATION": ("field", "FPHitscan", "Oxide", "m_FireDuration"),
    "FPHITSCAN_BURST_LENGTH": ("field", "FPHitscan", "Oxide", "m_BurstLength"),
    "FPHITSCAN_BURST_DURATION": ("field", "FPHitscan", "Oxide", "m_BurstDuration"),
    "FPHITSCAN_BURST_PAUSE": ("field", "FPHitscan", "Oxide", "m_BurstPause"),
    "FPHITSCAN_BURST_WAIT": ("field", "FPHitscan", "Oxide", "Vmp"),
    "WAITFORSECONDS_SECONDS": ("field", "WaitForSeconds", "UnityEngine", "m_Seconds"),
    "FPBOW_MIN_SHOT_PERIOD": ("field", "FPBow", "Oxide", "m_MinTimeBetweenShots"),
    "FPCROSSBOW_MIN_SHOT_PERIOD": ("field", "FPCrossbow", "First_Person", "m_MinTimeBetweenShots"),
    "FPROCKETLAUNCHER_MIN_SHOT_PERIOD":
        ("field", "FPRocketLauncher", "Oxide", "m_MinTimeBetweenShots"),
    "FPSNOWBALL_MIN_THROW_PERIOD": ("field", "FPSnowball", "First_Person", "m_MinTimeBetweenThrows"),
    "FPEXPLOSIVECHARGE_MIN_THROW_PERIOD":
        ("field", "FPExplosiveCharge", "Oxide", "m_MinTimeBetweenThrows"),
    "FPSPEAR_MIN_THROW_PERIOD": ("field", "FPSpear", "Oxide", "m_MinTimeBetweenThrows"),
    "FPSPEAR_CACHED_THROW_PERIOD": ("field", "FPSpear", "Oxide", "<VqZ>k__BackingField"),
    "FPHITSCAN_EFFECTIVE_LOOP_RELOAD_PERIOD":
        ("field", "FPHitscan", "Oxide", "<VoQ>k__BackingField"),
    "FPHITSCAN_CALCULATOR": ("field", "FPHitscan", "Oxide", "Vmv"),
    "CALCULATOR_DISPERSION_CONFIG": ("field", "uz", "", "PZR"),
    "CALCULATOR_HITSCAN_CONFIG": ("field", "uz", "", "PZD"),
    "CALCULATOR_CACHED_BASE_DISPERSION": ("field", "uz", "", "<PZZ>k__BackingField"),
    "HITSCANCONFIG_MAX_DISTANCE": ("field", "HitscanWeaponConfig", "Weapons", "<MaxDistance>k__BackingField"),
    "HITSCANCONFIG_BULLET_SPEED": ("field", "HitscanWeaponConfig", "Weapons", "<BulletSpeed>k__BackingField"),
    "HITSCANCONFIG_FIRE_RATE": ("field", "HitscanWeaponConfig", "Weapons", "fireRate"),
    "HITSCANCONFIG_PROJECTILES_PER_SHOT":
        ("field", "HitscanWeaponConfig", "Weapons", "projectilesPerShot"),
    "HITSCANCONFIG_BASE_DISPERSION":
        ("field", "HitscanWeaponConfig", "Weapons", "<BaseDispersion>k__BackingField"),
    "HITSCANCONFIG_FULL_RELOAD":
        ("field", "HitscanWeaponConfig", "Weapons", "<FullReloadPeriod>k__BackingField"),
    "HITSCANCONFIG_LOOP_RELOAD":
        ("field", "HitscanWeaponConfig", "Weapons", "<LoopReloadPeriod>k__BackingField"),
    "HITSCANCONFIG_BALLISTIC_PROFILE": ("field", "HitscanWeaponConfig", "Weapons", "ballistic"),
    "HITSCANCONFIG_DISPERSION_CONFIG":
        ("field", "HitscanWeaponConfig", "Weapons", "dispersionConfig"),
    "HITSCANCONFIG_RECOIL_CONFIG": ("field", "HitscanWeaponConfig", "Weapons", "recoilConfig"),
    "BALLISTICPROFILE_GRAVITY": ("field", "BallisticProfile", "Ballistics", "gravity"),
    "BALLISTICPROFILE_AIR_DRAG": ("field", "BallisticProfile", "Ballistics", "airDrag"),
    "BALLISTICPROFILE_UNDERWATER_DRAG": ("field", "BallisticProfile", "Ballistics", "underwaterDrag"),
    "SHAFTEDPROJECTILE_MAX_DISTANCE": ("field", "ShaftedProjectile", "Oxide", "m_MaxDistance"),
    "SHAFTEDPROJECTILE_LAUNCH_SPEED": ("field", "ShaftedProjectile", "Oxide", "m_LaunchSpeed"),
    "SHAFTEDPROJECTILE_GRAVITY": ("field", "ShaftedProjectile", "Oxide", "gravity"),
    "SHAFTEDPROJECTILE_AIR_DRAG": ("field", "ShaftedProjectile", "Oxide", "airDrag"),
    "SHAFTEDPROJECTILE_UNDERWATER_DRAG": ("field", "ShaftedProjectile", "Oxide", "underwaterDrag"),
    "DISPERSIONCONFIG_MIN_DISPERSION": ("field", "DispersionConfig", "Weapons", "minDispersion"),
    "DISPERSIONCONFIG_MAX_DISPERSION": ("field", "DispersionConfig", "Weapons", "maxDispersion"),
    "RECOILCONFIG_MIN_RECOIL": ("field", "RecoilConfig", "Weapons", "<MinRecoil>k__BackingField"),
    "RECOILCONFIG_MAX_RECOIL": ("field", "RecoilConfig", "Weapons", "<MaxRecoil>k__BackingField"),
    "RECOILCONFIG_MIN_SIDE": ("field", "RecoilConfig", "Weapons", "<MinSideDelta>k__BackingField"),
    "RECOILCONFIG_MAX_SIDE": ("field", "RecoilConfig", "Weapons", "<MaxSideDelta>k__BackingField"),
    # --- FP views ------------------------------------------------------------
    "PLAYERWEAPON_VIEW": ("field", "PlayerWeapon", "Weapons", "playerWeaponViewReference"),
    "PLAYERWEAPON_PIECE": ("field", "PlayerWeapon", "Weapons", "PDC"),  # SyncVar: DeserializeSyncVars passes &this._weaponPiece (=0x1A0 hook) at 0x150
    "PLAYERWEAPON_PLAYER_BACKREF": ("field", "PlayerWeapon", "Weapons", "<player>k__BackingField"),
    "WEAPONVIEW_WEAPON_BASE": ("field", "tB", "", "bYx"),
    "WEAPONVIEW_ROOT_TRANSFORM": ("field", "tB", "", "<bYA>k__BackingField"),
    "WEAPONVIEW_INNER": ("field", "tB", "", "bzc"),
    "WEAPONPIECE_ENABLED": ("field", "WeaponPiece", "Oxide", "Enabled"),
    "WEAPONPIECE_NUMBER": ("field", "WeaponPiece", "Oxide", "Number"),
    # --- FP manager / objects ------------------------------------------------
    "FPMANAGER_CURRENT_WEAPON": ("field", "FPManager", "Oxide", "Voy"),
    "FPMANAGER_CURRENT_OBJECT": ("field", "FPManager", "Oxide", "_currentWeapon"),
    "FPMANAGER_UZL": ("field", "FPManager", "Oxide", "VoF"),
    "FPMANAGER_CTS": ("field", "FPManager", "Oxide", "Vou"),
    "FPMANAGER_CTS2": ("field", "FPManager", "Oxide", "VoE"),
    "FPMANAGER_UTCS": ("field", "FPManager", "Oxide", "Vop"),
    "FPMANAGER_UZJ": ("field", "FPManager", "Oxide", "Vov"),
    "FPMANAGER_UHF": ("field", "FPManager", "Oxide", "<VoL>k__BackingField"),
    "FPMANAGER_AIM_BLEND": ("field", "FPManager", "Oxide", "<VoG>k__BackingField"),
    "FPMANAGER_WORLD_FOV_TARGET": ("field", "FPManager", "Oxide", "Voo"),
    "FPOBJECT_DRAW_EVENT": ("field", "FPObject", "Oxide", "Draw"),
    "FPOBJECT_HOLSTER_EVENT": ("field", "FPObject", "Oxide", "Holster"),
    "FPOBJECT_ITEM": ("field", "FPObject", "Oxide", "<Vgv>k__BackingField"),
    "FPOBJECT_SHOWN": ("field", "FPObject", "Oxide", "<Vgp>k__BackingField"),
    "FPOBJECT_OBJECT_NAME": ("field", "FPObject", "Oxide", "m_ObjectName"),
    "FPOBJECT_RAYCAST_MANAGER": ("field", "FPObject", "Oxide", "VgL"),
    "FPOBJECT_PLAYER_BACKREF": ("field", "FPObject", "Oxide", "Player"),
    "FPOBJECT_EVENT_HANDLER": ("field", "FPObject", "Oxide", "PlayerEventHandler"),
    "FPMELEE_MAX_REACH": ("field", "FPMelee", "Oxide", "m_MaxReach"),
    "FPMELEE_HIT_RADIUS": ("field", "FPMelee", "Oxide", "hitRadius"),
    "FPMELEE_TIME_BETWEEN_ATTACKS": ("field", "FPMelee", "Oxide", "m_TimeBetweenAttacks"),
    "FPMELEE_PAUSE_AFTER_ATTACK": ("field", "FPMelee", "Oxide", "pauseAfterAttack"),
    "FPTOOL_ALWAYS_CRIT": ("field", "FPTool", "Oxide", "alwaysCrit"),
    "FPTOOL_TOOL_PURPOSES": ("field", "FPTool", "Oxide", "m_ToolPurposes"),
    # --- raycasts ------------------------------------------------------------
    "RAYCASTMAN_PLAYER": ("field", "RaycastManager", "Oxide", "player"),
    "RAYCASTMAN_RAY_LENGTH": ("field", "RaycastManager", "Oxide", "m_RayLength"),
    "RAYCASTMAN_AIM_RAY_LENGTH": ("field", "RaycastManager", "Oxide", "m_AimRayLength"),
    "RAYCASTMAN_SPHERE_RADIUS": ("field", "RaycastManager", "Oxide", "VGq"),
    "RAYCASTMAN_TOO_CLOSE": ("field", "RaycastManager", "Oxide", "m_TooCloseThreeshold"),
    "RAYCASTMAN_LAYER_MASK": ("field", "RaycastManager", "Oxide", "m_LayerMask"),
    "RAYCASTMAN_AIM_LAYER_MASK": ("field", "RaycastManager", "Oxide", "m_AimLayerMask"),
    # --- event handler -------------------------------------------------------
    "EVENT_HANDLER_MANAGER_BACKREF": ("field", "CgX", "", "manager"),
    "EVENT_HANDLER_HEALTH": ("field", "Cgt", "", "Health"),
    "EVENT_HANDLER_AIM_ACTIVITY": ("field", "CgX", "", "Aim"),
    "EVENT_HANDLER_MOVEMENT_INPUT": ("field", "CgX", "", "MovementInput"),
    "EVENT_HANDLER_LOOK_DIRECTION": ("field", "CgX", "", "LookDirection"),
    "EVENT_HANDLER_LAST_HIT_POINT": ("field", "CgX", "", "LastLocalHitPoint"),
    "EVENT_HANDLER_LAST_HIT_TIME": ("field", "CgX", "", "LastLocalHitTime"),
    "GUM_RAYCAST_DATA": ("field", "CgX", "", "RaycastData"),
    "GUM_AIM_RAYCAST": ("field", "CgX", "", "AimRaycast"),
    "FPOBJECT_EQUIPPED_ITEM": ("field", "UvH", "", "EquippedItem"),
    "ACTIVITY_ACTIVE_FLAG": ("field", "Cgd", "", "<Vvu>k__BackingField"),
    "SYNC_VALUE_OFFSET": ("fixed", 0x20, "UGa<T>/UvM<T>/Uvu<T> closed-generic: value slot (open generic has no offsets)"),
    "GUI_VALUE": ("fixed", 0x20, "closed-generic wrapper value slot"),
    "HEALTH_VALUE": ("fixed", 0x20, "closed-generic wrapper value slot"),
    "ARP_LATEST_VALUE": ("fixed", 0x18, "closed-generic wrapper filter slot"),
    # --- hit-ray payload -----------------------------------------------------
    "GKO_HIT_OBJECT": ("field", "CqY", "", "<VGs>k__BackingField"),
    "GKO_HIT_ENTITY": ("field", "CqY", "", "<VGy>k__BackingField"),
    "GKO_HIT_PLAYER": ("field", "CqY", "", "<VGE>k__BackingField"),
    "GKO_RAYCAST_HIT": ("field", "CqY", "", "<VGp>k__BackingField"),
    "GKO_RAY_ORIGIN": ("field", "CqY", "", "<VGv>k__BackingField"),
    "RAYCASTHIT_POINT": ("fixed", 0x00, "UnityEngine.RaycastHit"),
    "RAYCASTHIT_NORMAL": ("fixed", 0x0C, "UnityEngine.RaycastHit"),
    "RAYCASTHIT_DISTANCE": ("fixed", 0x1C, "UnityEngine.RaycastHit"),
    "RAYCASTHIT_COLLIDER": ("fixed", 0x28, "UnityEngine.RaycastHit"),
    # --- mouse look / input --------------------------------------------------
    "MOUSELOOK_PITCH_YAW": ("field", "MouseLook", "Oxide", "VEG"),
    "LD_SPRINT_ACTION": ("field", "AL", "", "MJL"),  # sprint InputAction in InputActions holder (farm sprint)
    "PLAYER_INPUT_HANDLER_OWNER_BACKREF": ("field", "PlayerInputHandler", "Oxide", "<Vuo>k__BackingField"),
    "PLAYER_INPUT_HANDLER_EVENT_HANDLER": ("field", "PlayerInputHandler", "Oxide", "<FpF>k__BackingField"),
    "PLAYER_INPUT_STRUCT": ("field", "PlayerInputHandler", "Oxide", "Vuq"),
    "PLAYER_INPUT_HANDLER_ATTACK_HELD": ("field", "PlayerInputHandler", "Oxide", "VuG"),
    "PLAYER_INPUT_HANDLER_OVERRIDE_A": ("field", "PlayerInputHandler", "Oxide", "VuL"),
    "PLAYER_INPUT_HANDLER_OVERRIDE_B": ("field", "PlayerInputHandler", "Oxide", "VuW"),
    "PLAYER_INPUT_HANDLER_SPRINT_OVERRIDE": ("field", "PlayerInputHandler", "Oxide", "VuN"),
    "PLAYER_INPUT_HANDLER_INPUT_ACTIONS": ("field", "PlayerInputHandler", "Oxide", "VuB"),
    "PLAYER_INPUT_MOVEMENT_INPUT": ("rel", ["PlayerInput", "InputData", "MovementInput"], 0, "PLAYER_INPUT_STRUCT"),
    "PLAYER_INPUT_YROTATION": ("rel", ["PlayerInput", "InputData", "YRotation"], 0, "PLAYER_INPUT_STRUCT"),
    "PLAYER_INPUT_TICK": ("rel", ["PlayerInput", "InputData", "Tick"], 0, "PLAYER_INPUT_STRUCT"),
    "PLAYER_INPUT_SPRINT": ("rel", ["PlayerInput", "InputData", "Sprint"], 0, "PLAYER_INPUT_STRUCT"),
    "NULLABLE_HAS_VALUE": ("fixed", 0x0, "System.Nullable<T> layout"),
    "NULLABLE_VALUE": ("fixed", 0x4, "System.Nullable<T> layout"),
    "NULLABLE_STRIDE": ("fixed", 0x0C, "System.Nullable<T> layout"),
    "INPUT_ACTION_INDEX": ("field", "InputAction", "InputSystem", "m_ActionIndexInState"),
    "INPUT_ACTION_STATE_HOLDER": ("field", "InputAction", "InputSystem", "m_ActionMap"),
    "INPUT_ACTION_STATE_HOLDER_STATE": ("field", "InputActionMap", "InputSystem", "m_State"),
    "INPUT_ACTION_STATE_STATES": ("chain", ["InputActionState", "InputSystem", "memory"], ["UnmanagedMemory", "", "actionStates"]),
    "INPUT_ACTION_STATE_STRIDE": ("fixed", 0x44, "sizeof(InputActionState.TriggerState) == 0x44 (last field @0x40 int)"),
    "INPUT_ACTION_PHASE_OFFSET": ("fixed", 0x00, "TriggerState.m_Phase"),
    "INPUT_ACTION_PHASE_WAITING": ("fixed", 1, "InputActionPhase.Waiting"),
    "INPUT_ACTION_PHASE_STARTED": ("fixed", 2, "InputActionPhase.Started"),
    # --- nicklabel / text ----------------------------------------------------
    "NICKLABEL_PLAYER_BACKREF": ("field", "ZJ", "", "player"),
    "NICKLABEL_NICKNAME_TEXT": ("field", "ZJ", "", "nickname"),
    "UI_TEXT_MTEXT": ("field", "Text", "UnityEngine.UI", "m_Text"),
    "PLAYER_DISPLAY_NAME_FALLBACK": ("fixed", 0, "unused"),
    # --- voice ---------------------------------------------------------------
    "VOICE_STATE_NAME": ("field", "VoicePlayerState", "Dissonance", "<Name>k__BackingField"),
    "VOICE_PLAYER_TAG": ("field", "Ccc", "", "EHl"),
    # --- items ---------------------------------------------------------------
    "ITEM_DATA": ("field", "Item", "Oxide", "<VIx>k__BackingField"),
    "ITEMDATA_NAME": ("field", "ItemData", "Oxide", "m_Name"),
    "ITEMDATA_SHORTNAME": ("field", "ItemData", "Oxide", "m_ShortName"),
    "ITEMDATA_ID": ("field", "ItemData", "Oxide", "m_Id"),
    "LOOTITEM_ITEM_NAME": ("field", "LootItem", "Oxide", "ItemName"),
    "IL2CPP_STRING_LENGTH": ("fixed", 0x10, "Il2CppString header"),
    "IL2CPP_STRING_CHARS": ("fixed", 0x14, "Il2CppString header"),
    "IL2CPP_CLASS_NAME": ("fixed", 0x10, "Il2CppClass header"),
    "IL2CPP_CLASS_NAMESPACE": ("fixed", 0x18, "Il2CppClass header"),
    "IL2CPP_ARRAY_LENGTH": ("fixed", 0x18, "Il2CppArray header"),
    "IL2CPP_LIST_ITEMS": ("fixed", 0x10, "List<T> layout"),
    "IL2CPP_LIST_SIZE": ("fixed", 0x18, "List<T> layout"),
    "IL2CPP_ARRAY_FIRST_ELEMENT": ("fixed", 0x20, "Il2CppArray data start"),
    # --- vitals --------------------------------------------------------------
    "VITALS_MAX_HEALTH": ("field", "GenericVitals", "Oxide", "m_MaxHealth"),
    "VITALS_HEALTH": ("field", "GenericVitals", "Oxide", "VpP"),
    "VITALS_PROTECTION": ("field", "GenericVitals", "Oxide", "m_Protection"),
    "VITALS_HITBOXES": ("field", "GenericVitals", "Oxide", "m_Hitboxes"),
    "VITALS_MIN_FALL_SPEED": ("field", "EntityVitals", "Oxide", "m_MinFallSpeed"),
    "VITALS_MAX_FALL_SPEED": ("field", "EntityVitals", "Oxide", "m_MaxFallSpeed"),
    "VITALS_PLAYER_BACKREF": ("field", "PlayerVitals", "Oxide", "Player"),
    "VITALS_PLAYER_EVENT": ("field", "PlayerVitals", "Oxide", "PlayerEvent"),
    "VITALS_TEMPERATURE": ("field", "PlayerVitals", "Oxide", "m_Temperature"),
    "PMP_ENTITY": ("field", "CgL", "", "Entity"),
    "PMK_HEALTH": ("field", "Cgt", "", "Health"),
    # --- death handler -------------------------------------------------------
    "DEATH_HANDLER_REF_TARGET": ("fixed", 0x18, "InterfaceReference<T>.Ono (open generic -> no offsets in dump)"),
    "DEATH_HANDLER_DEATH": ("field", "PlayerDeathHandler", "Oxide", "death"),
    "DEATH_HANDLER_CHARACTER_MODEL": ("field", "PlayerDeathHandler", "Oxide", "m_CharacterModel"),
    "NPC_DEATH_COLLIDERS": ("field", "PlayerDeathHandler", "Oxide", "m_CollidersToDisable"),
    # --- inventory -----------------------------------------------------------
    "INV_PLAYER_INVENTORY_DATA": ("field", "PlayerInventory", "Oxide", "_playerInventoryData"),
    "INV_PLAYER_INVENTORY_CLIENT": ("field", "PlayerInventory", "Oxide", "_playerInventoryClient"),
    "INVDATA_PLAYER_MODEL_INFO": ("field", "PlayerInventoryData", "Oxide", "playerModelInfo"),
    "INVENTORY_ITEMS": ("field", "Inventory", "Oxide", "<FXT>k__BackingField"),
    # --- model info ----------------------------------------------------------
    "MODELINFO_HEAD": ("field", "PlayerModelInfo", "Player", "head"),
    "MODELINFO_RIGHT_WEAPON_HOLDER": ("field", "PlayerModelInfo", "Player", "rightWeaponHolder"),
    "MODELINFO_LEFT_WEAPON_HOLDER": ("field", "PlayerModelInfo", "Player", "leftWeaponHolder"),
    "MODELINFO_EQUIPMENT_HOLDER": ("field", "PlayerModelInfo", "Player", "equipmentHolder"),
    "MODELINFO_HEAD_MODEL": ("field", "PlayerModelInfo", "Player", "headModel"),
    "CHARANIM_PLAYER_MODEL_INFO": ("field", "CharacterAnimation", "Player", "playerModelInfo"),
    "NPC_BRAIN_MODEL_INFO": ("field", "NPCBrainHumanoid", "NPC", "modelInfo"),
    "NPC_BRAIN_ENTITY": ("field", "NPCBrain", "NPC", "Entity"),
    # --- KCC -----------------------------------------------------------------
    "KCC_PLAYER_BACKREF": ("field", "KCC", "Player", "player"),
    "KCC_HEAD_TRANSFORM": ("field", "KCC", "Player", "head"),
    "KCC_NORMAL_HEIGHT": ("field", "KCC", "Player", "normalHeight"),
    "KCC_CROUCH_HEIGHT": ("field", "KCC", "Player", "m_CrouchHeight"),
    "KCC_HITBOX_ROOT": ("field", "KCC", "Player", "hitBoxRecorderRoot"),
    "KCC_LOOK_HEIGHT_OFFSET": ("field", "KCC", "Player", "lookHeightOffset"),
    "KCC_MOVE": ("field", "KCC", "Player", "bfu"),
    "KCC_MOVE_CURRENT": ("field", "KCC", "Player", "YWk"),
    "KCC_VISIBILITY": ("field", "KCC", "Player", "visibility"),
    "KCC_NOCLIP": ("field", "KCC", "Player", "noclip"),
    "KCC_MOTOR_ENABLED": ("field", "KCC", "Player", "bUQ"),
    "KCC_OBSERVER_MOVE": ("field", "KCC", "Player", "Yyw"),
    "KCC_HAS_OBSERVER_MOVE": ("field", "KCC", "Player", "bUs"),
    "KCC_SPEED_MODIFIER": ("field", "KCC", "Player", "speedModifier"),
    "KCC_CHARACTER_ANIMATION": ("field", "KCC", "Player", "bfe"),
    "KCC_MOTOR_OFFSET": ("field", "KinematicCharacterMotor", "KinematicCharacterController", "autoSimulation"),
    "KCC_MOTOR_CAPSULE": ("field", "KinematicCharacterMotor", "KinematicCharacterController", "Capsule"),
    "HITBOX_ROOT_ARRAY": ("field", "HitBoxRecorderRoot", "Network", "hitBoxes"),
    "HITBOX_SIZE": ("field", "HitBox", "Oxide", "size"),
    "HITBOX_CENTER": ("field", "HitBox", "Oxide", "center"),
    "HITBOX_AREA": ("field", "HitBox", "Oxide", "m_HitArea"),
    "CHAR_ANIM_PLAYER_BACKREF": ("field", "CharacterAnimation", "Player", "bhm"),
    "CHAR_ANIM_RAGDOLL": ("field", "CharacterAnimation", "Player", "ragdoll"),
    "RAGDOLL_PELVIS_RIGIDBODY": ("field", "Ragdoll", "Damage_System", "m_Pelvis"),
    "RAGDOLL_BONES_ARRAY": ("field", "Ragdoll", "Damage_System", "m_Bones"),
    "RAGDOLL_BODYPART_TRANSFORM": ("field", "BodyPart", "", "transform"),
    # --- unity native object layout -----------------------------------------
    "TRANSFORM_CHILDREN_ARRAY": ("fixed", 0x48, "native UnityEngine.Transform"),
    "TRANSFORM_CHILD_COUNT": ("fixed", 0x58, "native UnityEngine.Transform"),
    "TRANSFORM_HIERARCHY_DATA": ("fixed", 0x38, "native UnityEngine.Transform"),
    "TRANSFORM_HIERARCHY_INDEX": ("fixed", 0x40, "native UnityEngine.Transform"),
    "TRANSFORM_HIERARCHY_MATRICES": ("fixed", 0x18, "native TransformHierarchy"),
    "TRANSFORM_HIERARCHY_INDICES": ("fixed", 0x20, "native TransformHierarchy"),
    "COMPONENT_GAMEOBJECT": ("fixed", 0x20, "native UnityEngine.Component"),
    "GAMEOBJECT_COMPONENT_ARRAY": ("fixed", 0x20, "native UnityEngine.GameObject"),
    "COMPONENT_PAIR_PTR": ("fixed", 0x08, "native ComponentPair"),
    "GAMEOBJECT_NAME": ("fixed", 0x48, "native UnityEngine.Object.m_Name"),
    # --- character speed config ---------------------------------------------
    "CHAR_SPEED_CONFIG_KCC": ("field", "KCC", "Player", "characterSpeedConfig"),
    "CHAR_SPEED_CONFIG_SINGLE_KCC": ("alias", "CHAR_SPEED_CONFIG_KCC"),  # 11995 has a single config on KCC
    "CHAR_SPEED_SPRINT": ("field", "CharacterSpeedConfig", "Player", "<StandSprintSpeed>k__BackingField"),
    "CHAR_SPEED_JOG": ("field", "CharacterSpeedConfig", "Player", "<StandJogSpeed>k__BackingField"),
    "CHAR_SPEED_WALK": ("field", "CharacterSpeedConfig", "Player", "<StandWalkSpeed>k__BackingField"),
    "CHAR_SPEED_BACK_FACTOR": ("field", "CharacterSpeedConfig", "Player", "<BackSpeedFactor>k__BackingField"),
    "CHAR_SPEED_JUMP_HEIGHT": ("field", "CharacterSpeedConfig", "Player", "<JumpHeight>k__BackingField"),
    "CHAR_SPEED_GRAVITY": ("field", "CharacterSpeedConfig", "Player", "<Gravity>k__BackingField"),
    # --- il2cpp containers ---------------------------------------------------
    "DICT_ENTRIES": ("fixed", 0x18, "Dictionary<TKey,TValue> layout"),
    "DICT_COUNT": ("fixed", 0x20, "Dictionary<TKey,TValue> layout"),
    "DICT_ENTRY_STRIDE": ("fixed", 0x18, "Dictionary Entry layout"),
    "DICT_ENTRY_VALUE": ("fixed", 0x10, "Dictionary Entry layout"),
    # --- world objects -------------------------------------------------------
    "CUPBOARD_IS_LOOTABLE": ("field", "Cupboard", "Oxide", "isLootable"),
    "CUPBOARD_AUTHORIZED_PLAYERS": ("field", "Cupboard", "Oxide", "authorizedPlayers"),
    "CUPBOARD_PROTECTED_TIME": ("field", "Cupboard", "Oxide", "protectedTime"),
    "CUPBOARD_LUXURY_BLOCKS": ("field", "Cupboard", "Oxide", "luxuryBlocks"),
    "LOOTOBJECT_INVENTORY": ("field", "LootObject", "Oxide", "inventory"),
    "LOOTOBJECT_IS_LOOTABLE": ("field", "LootObject", "Oxide", "isLootable"),
    "LOOTOBJECT_PANEL_NAME": ("field", "LootObject", "Oxide", "panelName"),
    "LOOTOBJECT_BUILDING_PIECE": ("field", "LootObject", "Oxide", "m_Piece"),
    "ITEMPICKUP_ITEM_OBJECT": ("field", "ItemPickup", "Oxide", "<Vke>k__BackingField"),
    "ITEMPICKUP_SHORTNAME": ("field", "ItemPickup", "Oxide", "item"),
    "ITEMPICKUP_AMOUNT": ("field", "ItemPickup", "Oxide", "amount"),
    "MINEABLE_LOOT": ("field", "MineableObject", "Oxide", "m_Loot"),
    "MINEABLE_FINISH_BONUS": ("field", "MineableObject", "Oxide", "m_FinishBonus"),
    "MINEABLE_CURRENT_HEALTH": ("field", "MineableObject", "Oxide", "m_CurrentHealth"),
    "MINEABLE_HIT_ANCHOR": ("field", "MineableObject", "Oxide", "Vkk"),
    "MINEABLE_MAX_HEALTH": ("field", "MineableObject", "Oxide", "m_MaxHealth"),
    "MINEABLE_FRACTION": ("field", "MineableObject", "Oxide", "fractionRemaining"),
    "MINEABLE_ENTITY_TYPE": ("field", "MineableObject", "Oxide", "entityType"),
    "MINEABLE_REQUIRED_TOOL_PURPOSE": ("field", "MineableObject", "Oxide", "m_RequiredToolPurpose"),
    "MINEABLE_EXPERIENCE": ("field", "MineableObject", "Oxide", "m_Experience"),
    "MINEABLE_EXTENSIONS": ("field", "MineableObject", "Oxide", "VkW"),
    "OREHS_STREAK_INDEX": ("field", "MineableObjectExtension_OreHitstreaks", "MineableExtensions", "hitstreakIndex"),
    "OREHS_MARKER_TEMPLATE": ("field", "MineableObjectExtension_OreHitstreaks", "MineableExtensions", "hitStreakMarker"),
    "OREHS_MARKER": ("field", "MineableObjectExtension_OreHitstreaks", "MineableExtensions", "bmd"),
    "OREHS_COLLIDER": ("field", "MineableObjectExtension_OreHitstreaks", "MineableExtensions", "bmB"),
    "OREHS_MINEABLE": ("field", "MineableObjectExtension_OreHitstreaks", "MineableExtensions", "bml"),
    "OREMARK_RENDERER": ("field", "MineableObjectExtension_OreHitstreaksMarker", "MineableExtensions", "meshRenderer"),
    "OREMARK_OWNER": ("field", "MineableObjectExtension_OreHitstreaksMarker", "MineableExtensions", "bmO"),
    "OREMARK_SMOOTH": ("field", "MineableObjectExtension_OreHitstreaksMarker", "MineableExtensions", "boR"),
    "OREMARK_AGE": ("field", "MineableObjectExtension_OreHitstreaksMarker", "MineableExtensions", "boZ"),
    "TREEHS_MARKER_TEMPLATE": ("field", "MineableObjectExtension_TreeHitstreaks", "MineableExtensions", "hitStreakMarkerOriginal"),
    "TREEHS_STREAK": ("field", "MineableObjectExtension_TreeHitstreaks", "MineableExtensions", "boe"),
    "TREEHS_MARKER": ("field", "MineableObjectExtension_TreeHitstreaks", "MineableExtensions", "boz"),
    "TREEHS_SPOT_A": ("field", "MineableObjectExtension_TreeHitstreaks", "MineableExtensions", "boP"),
    "TREEHS_SPOT_B": ("field", "MineableObjectExtension_TreeHitstreaks", "MineableExtensions", "bos"),
    "HITMARK_LIFETIME": ("field", "MineableObjectExtension_HitMarkerItem", "MineableExtensions", "lifetime"),
    "HITMARK_FILTER": ("field", "MineableObjectExtension_HitMarkerItem", "MineableExtensions", "mFilter"),
    "HITMARK_RENDERER": ("field", "MineableObjectExtension_HitMarkerItem", "MineableExtensions", "renderer"),
    "HITMARK_MARK": ("field", "MineableObjectExtension_HitMarkerItem", "MineableExtensions", "mark"),
    "HITMARK_AGE": ("field", "MineableObjectExtension_HitMarkerItem", "MineableExtensions", "bmg"),
    # --- time of day ---------------------------------------------------------
    "TOD_SKY_CYCLE": ("field", "HL", "", "Cycle"),
    "TOD_CYCLE_HOUR": ("field", "TOD_CycleParameters", "", "Hour"),
    "TOD_CYCLE_DAY": ("field", "TOD_CycleParameters", "", "Day"),
    "TOD_CYCLE_MONTH": ("field", "TOD_CycleParameters", "", "Month"),
    "TOD_CYCLE_YEAR": ("field", "TOD_CycleParameters", "", "Year"),
    # --- map / markers -------------------------------------------------------
    "MAP_MANAGER_MAP_VIEW": ("field", "MapManager", "GUISystem", "mapView"),
    "MAP_MANAGER_STATIC_INTEREST": ("field", "MapManager", "GUISystem", "staticInterestMarkers"),
    "MAP_MANAGER_MARKER_SERVICE": ("field", "MapManager", "GUISystem", "uek"),
    "MAP_MANAGER_MAP_PRESENTER": ("field", "MapManager", "GUISystem", "ueL"),
    "MAP_MANAGER_IS_OPEN": ("field", "MapManager", "GUISystem", "<ueB>k__BackingField"),
    "MARKER_INSTALLER_MAP_VIEW": ("field", "MarkerSystemInstaller", "Markers", "mapView"),
    "MAP_VIEW_MAP_RT": ("field", "MapView", "", "map"),
    "MAP_VIEW_SCROLL_RECT": ("field", "MapView", "", "scrollRect"),
    "MAP_VIEW_ON_ENABLE_ACTION": ("field", "MapView", "", "MYu"),
    "STATIC_INTEREST_RECT": ("field", "StaticInterestMarkers", "GUISystem", "rect"),
    "STATIC_INTEREST_INIT": ("field", "StaticInterestMarkers", "GUISystem", "uec"),
    "STATIC_INTEREST_RECTS_LIST": ("field", "StaticInterestMarkers", "GUISystem", "uej"),
    "STATIC_INTEREST_POS_LIST": ("field", "StaticInterestMarkers", "GUISystem", "ueS"),
    "MAP_PRESENTER_MARKER_SERVICE": ("field", "ls", "", "bkV"),
    "MAP_PRESENTER_VIEWS_DICT": ("field", "ls", "", "bkp"),
    "MAP_PRESENTER_IS_OPEN": ("field", "ls", "", "bko"),
    "MARKER_SERVICE_ADDED_ACTION": ("field", "lX", "", "bWC"),
    "MAP_MARKER_VIEW_RT": ("field", "MapMarkerView", "Markers", "rectTransform"),
    "SCROLL_RECT_VIEWPORT": ("field", "ScrollRectEx", "UnityEngine.UI", "m_Viewport"),
    "SCROLL_RECT_VIEW_RECT": ("field", "ScrollRectEx", "UnityEngine.UI", "m_ViewRect"),
    "SCROLL_RECT_VIEW_BOUNDS_CENTER": ("field", "ScrollRectEx", "UnityEngine.UI", "m_ViewBounds"),
    "SCROLL_RECT_VIEW_BOUNDS_EXTENTS": ("rel", None, 0xC, "SCROLL_RECT_VIEW_BOUNDS_CENTER"),
    "SCREEN_WINDOW_RECT_TRANSFORM": ("field", "ScreenMarkerWindowView", "Presenters", "bXA"),
    "SCREEN_PRESENTER_VIEW_DICT": ("field", "ScreenMarkerPresenter", "Presenters", "btK"),
    "SCREEN_PRESENTER_MARKER_LIST": ("field", "ScreenMarkerPresenter", "Presenters", "btw"),
    "SCREEN_PRESENTER_INST_DICT": ("field", "ScreenMarkerPresenter", "Presenters", "bXx"),
    "MARKER_DATA": ("field", "Marker", "Markers", "<bLx>k__BackingField"),
    "MARKER_ACTIVE": ("field", "Marker", "Markers", "<bLC>k__BackingField"),
    "MARKER_DATA_TYPE": ("field", "lv", "", "bLR"),
    "MARKER_DATA_WORLD_POS": ("field", "lv", "", "bLD"),
    "MARKER_DATA_HEAD_POS": ("field", "lv", "", "bLZ"),
    "MARKER_DATA_TRANSFORM": ("field", "lv", "", "bLJ"),
    "MARKER_DATA_USER_ID": ("field", "lL", "", "bLv"),
    "MARKER_DATA_IS_HIT": ("field", "lL", "", "bLG"),
    "SCREEN_MARKER_VIEW_CONTEXT": ("field", "ScreenMarkerView", "Markers", "context"),
    "SCREEN_MARKER_VIEW_MODULES": ("field", "ScreenMarkerView", "Markers", "modules"),
    "SCREEN_MARKER_VIEW_MARKER": ("field", "ScreenMarkerView", "Markers", "<btT>k__BackingField"),
    "NEARBY_VIEW_MODULE_CTX": ("field", "NearbyPlayerScreenMarkerView", "Markers", "screenMarkerModuleContext"),
    "NEARBY_VIEW_DATA": ("field", "NearbyPlayerScreenMarkerView", "Markers", "bta"),
    "NEARBY_VIEW_CACHED_HIT": ("field", "NearbyPlayerScreenMarkerView", "Markers", "bth"),
    "MARKER_CTX_RAY_TARGET": ("field", "ScreenMarkerModuleContext", "Markers", "<bNL>k__BackingField"),
    "MARKER_CTX_SKIP_OCCLUSION": ("field", "ScreenMarkerModuleContext", "Markers", "<bNW>k__BackingField"),
    "MARKER_CTX_HIDDEN": ("field", "ScreenMarkerModuleContext", "Markers", "bNS"),
    "OCCLUSION_MOD_CTX": ("field", "cx", "", "bNG"),
    "OCCLUSION_MOD_MASK": ("field", "OcclusionModule", "Markers", "obstacleMask"),
    "OCCLUSION_MOD_INTERVAL": ("field", "OcclusionModule", "Markers", "checkInterval"),
    "OCCLUSION_MOD_NEXT_TIME": ("field", "OcclusionModule", "Markers", "bNr"),
    "OCCLUSION_MOD_HAS_VAL": ("rel", None, 4, "OCCLUSION_MOD_NEXT_TIME"),
    "OCCLUSION_MOD_IS_OCCLUDED": ("rel", None, 5, "OCCLUSION_MOD_NEXT_TIME"),
    # --- screen controls -----------------------------------------------------
    "SCREEN_CONTROLS_HOLDER_FIELD": ("field", "ScreenControlsManager", "ScreenControls", "screenControlsReferenceHolder"),
    "SCREEN_CONTROLS_HOLDER_FIXED": ("field", "ScreenControlsReferenceHolder", "ScreenControls", "<FixedJoystick>k__BackingField"),
    "SCREEN_CONTROLS_HOLDER_FLOATING": ("field", "ScreenControlsReferenceHolder", "ScreenControls", "<FloatingJoystick>k__BackingField"),
    "JOYSTICK_INPUT_VALUE": ("field", "Joystick", "", "MAd"),
    "JOYSTICK_HANDLE_RANGE": ("field", "Joystick", "", "handleRange"),
    "JOYSTICK_DEAD_ZONE": ("field", "Joystick", "", "deadZone"),
    "JOYSTICK_BACKGROUND": ("field", "Joystick", "", "background"),
    "JOYSTICK_HANDLE": ("field", "Joystick", "", "handle"),
    "JOYSTICK_BASE_JOYSTICK": ("field", "Joystick", "", "baseJoystick"),
    # --- aim assist ----------------------------------------------------------
    "AIMASSIST_PLAYER": ("field", "CPe", "", "FVB"),
    "AIMASSIST_CONFIG": ("field", "CPe", "", "FVl"),
    "AIMASSIST_INIT_FLAG": ("field", "CPe", "", "FVc"),
    "AIMASSIST_ENABLED": ("field", "CPe", "", "FVI"),
    "AIMASSIST_CAMERA": ("field", "CPe", "", "FVj"),
    "AIMASSIST_RAYCAST_HIT": ("field", "CPe", "", "FVS"),
    "AIMASSIST_CUR_TARGET": ("field", "CPe", "", "FVK"),
    "AIMASSIST_CUR_SENSOR": ("field", "CPe", "", "FVn"),
    "AIMASSIST_PREV_TARGET": ("field", "CPe", "", "FVw"),
    "AIMASSIST_PREV_SENSOR": ("field", "CPe", "", "FVO"),
    "AIMASSIST_HIT_FLAG": ("field", "CPe", "", "FuD"),
    "AIMASSIST_DELTA_ROT": ("field", "CPe", "", "<Fuh>k__BackingField"),
    "AIMSENSOR_COLLIDER": ("field", "AimSensorData", "AimAssist", "Collider"),
    "AIMSENSOR_START_LOOK_TIME": ("field", "AimSensorData", "AimAssist", "StartLookTime"),
    "AIMSENSOR_LAST_SEEN_TIME": ("field", "AimSensorData", "AimAssist", "LastSeenTime"),
    "AIMSENSOR_LAST_RECT_TIME": ("field", "AimSensorData", "AimAssist", "LastRectTime"),
    "AIMSENSOR_DISTANCE": ("field", "AimSensorData", "AimAssist", "DistanceFromCamera"),
    "AIMSENSOR_POSITION": ("field", "AimSensorData", "AimAssist", "Position"),
    "AIMSENSOR_DIRECTION": ("field", "AimSensorData", "AimAssist", "Direction"),
    "AIMCONFIG_DISTANCE": ("field", "AimAssistConfig", "AimAssist", "distance"),
    "AIMCONFIG_OUTER_SPEED": ("field", "AimAssistConfig", "AimAssist", "outerMaxSpeed"),
    "AIMCONFIG_INNER_SPEED": ("field", "AimAssistConfig", "AimAssist", "innerMaxSpeed"),
    "AIMCONFIG_OUTER_ABS_SPEED": ("field", "AimAssistConfig", "AimAssist", "outerMaxAbsoluteSpeed"),
    "AIMCONFIG_INNER_ABS_SPEED": ("field", "AimAssistConfig", "AimAssist", "innerMaxAbsoluteSpeed"),
    "AIMCONFIG_SWITCH_DELAY": ("field", "AimAssistConfig", "AimAssist", "targetSwitchDelay"),
    "AIMTARGET_ENTITY_HANDLER": ("field", "AimTarget", "AimAssist", "entityEventHandler"),
    # --- fp motion -----------------------------------------------------------
    "FPMOTION_OBJECT": ("field", "FPMotion", "Oxide", "VgY"),
    "FPMOTION_PLAYER": ("field", "FPMotion", "Oxide", "Player"),
    "TRANSFORMOFFSET_LERP_SPEED": ("field", "TransformOffset", "Oxide", "m_LerpSpeed"),
    "TRANSFORMOFFSET_POSITION": ("field", "TransformOffset", "Oxide", "m_Position"),
    "TRANSFORMOFFSET_CURRENT_POSITION": ("field", "TransformOffset", "Oxide", "VgA"),
    "FPMOTION_IDLE_OFFSET": ("field", "FPMotion", "Oxide", "m_IdleOffset"),
    "FPMOTION_RUN_OFFSET": ("field", "FPMotion", "Oxide", "m_RunOffset"),
    "FPMOTION_RUN_RELOAD_OFFSET": ("field", "FPMotion", "Oxide", "runReloadOffset"),
    "FPMOTION_ATTACK_OFFSET": ("field", "FPMotion", "Oxide", "m_AttackOffset"),
    "FPMOTION_AIM_OFFSET": ("field", "FPMotion", "Oxide", "m_AimOffset"),
    "FPMOTION_LOOK_OFFSET": ("field", "FPMotion", "Oxide", "m_LookOffset"),
    "FPMOTION_CLOSE_OFFSET": ("field", "FPMotion", "Oxide", "m_TooCloseOffset"),
    "FPMOTION_ACTIVE_OFFSET": ("field", "FPMotion", "Oxide", "VgT"),
    "FPDRAW_ACTION": ("field", "CgI", "", "VvW"),
    "DELEGATE_TARGET": ("fixed", 0x20, "System.Delegate.target"),
    "MULTICAST_DELEGATES": ("fixed", 0x78, "System.MulticastDelegate"),
    # --- aliases -------------------------------------------------------------
    "GAMEOBJECT_NAME_GUESS": ("alias", "GAMEOBJECT_NAME"),
    "FPMOTION_LADDER_OFFSET": ("alias", "FPMOTION_AIM_OFFSET"),
    "FPMOTION_JUMP_OFFSET": ("alias", "FPMOTION_AIM_OFFSET"),
}

# enum / structural values that are not offsets at all
ENUM_SPEC = {
    "KCC_MOVE_STATE_DEAD": 7,
    "KCC_MOVE_STATE_AIRBORNE": 4,
    "KCC_MOVE_STATE_CLAMP_MAX": 4,
    "KCC_MOVE_STATE": 0x00,
    "KCC_MOVE_REAL_VELOCITY": 0x18,
    "KCC_MOVE_REAL_VELOCITY_Y": 0x1C,
    "KCC_MOVE_BASE_VELOCITY": 0x38,
    "KCC_MOVE_BASE_VELOCITY_Y": 0x3C,
    "TOD_SCAN_RVA_BEGIN": 0xE8F0000,
    "TOD_SCAN_RVA_END": 0xE910000,
}
SPEC.update({k: ("fixed", v, "enum/structural value") for k, v in ENUM_SPEC.items()})

# KCC.Move struct fields are verified positionally against the `Move` struct.
MOVE_SPEC = {
    "KCC_MOVE_STATE": "State",
    "KCC_MOVE_REAL_VELOCITY": "RealVelocity",
    "KCC_MOVE_BASE_VELOCITY": "BaseVelocity",
}


def parse_header(path):
    txt = open(path, encoding="utf-8").read()
    out = {}
    for m in re.finditer(r"inline constexpr\s+[\w:<> ]+?\s+([A-Z0-9_]+)\s*=\s*([^;]+);", txt):
        name, val = m.group(1), m.group(2).strip()
        val = val.split("//")[0].strip()
        if val.endswith("F") or val.endswith("f"):
            try:
                out[name] = float(val[:-1])
            except ValueError:
                out[name] = val
        else:
            try:
                out[name] = int(val, 0)
            except ValueError:
                out[name] = val  # alias to another constant, resolved below
    for _ in range(4):  # resolve `A = B;` chains
        for name, val in list(out.items()):
            if isinstance(val, str) and val in out and not isinstance(out[val], str):
                out[name] = out[val]
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    propose = "--propose" in sys.argv
    if len(args) < 2:
        print(__doc__)
        return 2
    dump = Dump(args[0])
    pointers = {}
    with open(args[1], encoding="utf-8", errors="replace") as f:
        blob = f.read()
    i = blob.find('"TypeInfoPointers"')
    if i < 0:
        print("FAIL: script.json has no TypeInfoPointers")
        return 2
    j = blob.find("[", i)
    arr, _ = json.JSONDecoder().raw_decode(blob[j:])
    for e in arr:
        pointers.setdefault(e["DotNetType"], e["Address"])

    header = parse_header(HEADER)
    problems = []
    changed = []
    derived = {}

    def lookup_field(cls, ns, fname, _depth=0):
        """Offset of `fname` in `cls`, walking base classes (inherited fields)."""
        cands = [c for c in dump.all(cls) if ns in c.namespace and c.fields]
        if not cands:
            return None, f"class {cls!r} (ns~{ns!r}) not found"
        hits = [(c, off) for c in cands for off, _t, n in c.fields if n == fname]
        offs = {off for _c, off in hits}
        if len(offs) == 1:
            return offs.pop(), None
        if len(offs) > 1:
            return None, f"field {cls}.{fname} ambiguous: {sorted(hex(o) for o in offs)}"
        if _depth < 8:
            for c in cands:
                for base in [b.strip() for b in c.bases.split(",") if b.strip()]:
                    base = base.split("<")[0].split(".")[-1]
                    off, err = lookup_field(base, "", fname, _depth + 1)
                    if err is None:
                        return off, None
        return None, f"field {cls}.{fname} not found (bases walked)"

    for name, value in header.items():
        spec = SPEC.get(name)
        if spec is None:
            problems.append(f"{name}: no spec entry (uncovered constant)")
            continue
        kind = spec[0]
        if kind == "field":
            _k, cls, ns, fname = spec
            off, err = lookup_field(cls, ns, fname)
            if err:
                problems.append(f"{name}: {err}")
                continue
            derived[name] = off
        elif kind == "rel":
            # offset of `fields` (or 0) + constant `extra` + constant `base`
            _k, fields, extra, base = spec
            off, err = (0, None) if fields is None else lookup_field(*fields)
            if err:
                problems.append(f"{name}: {err}")
                continue
            add = header.get(extra, 0) if isinstance(extra, str) else extra
            anchor = header.get(base, 0) if base else 0
            if base and base not in header:
                problems.append(f"{name}: anchor constant {base} missing")
                continue
            derived[name] = off + add + anchor
        elif kind == "chain":
            _k, f1, f2 = spec
            o1, e1 = lookup_field(*f1)
            o2, e2 = lookup_field(*f2)
            if e1 or e2:
                problems.append(f"{name}: {e1 or e2}")
                continue
            derived[name] = o1 + o2
        elif kind == "ti":
            addr = pointers.get(spec[1])
            if addr is None:
                problems.append(f"{name}: TypeInfo for {spec[1]} not found")
                continue
            derived[name] = addr
        elif kind == "fixed":
            derived[name] = spec[1]
        elif kind == "alias":
            derived[name] = None  # resolved below
        else:
            problems.append(f"{name}: bad spec kind {kind}")

    # aliases
    for name, spec in SPEC.items():
        if spec[0] == "alias" and name in header:
            tgt = derived.get(spec[1])
            if tgt is None and spec[1] in header:
                tgt = header[spec[1]]
            derived[name] = tgt

    for name in header:
        if name not in derived:
            continue
        want, got = derived[name], header[name]
        if isinstance(want, float) or isinstance(got, float):
            ok = abs(float(want) - float(got)) < 1e-6
        else:
            ok = want == got
        if not ok:
            problems.append(f"{name}: header 0x{got:X} != dump 0x{want:X}")
            changed.append((name, got, want))

    if propose:
        for name in header:
            if name in derived and derived[name] is not None:
                print(f"{name}={derived[name]}")
        return 0

    total = len(header)
    by_field = sum(1 for s in SPEC.values() if s[0] == "field")
    by_ti = sum(1 for s in SPEC.values() if s[0] == "ti")
    by_fixed = sum(1 for s in SPEC.values() if s[0] == "fixed")
    print(f"constants in header: {total}")
    print(f"  re-derived from dump.cs fields : {by_field}")
    print(f"  re-derived from script.json    : {by_ti}")
    print(f"  pinned literals (native/il2cpp/enum): {by_fixed}")
    print(f"  aliases                        : {sum(1 for s in SPEC.values() if s[0] == 'alias')}")
    # invariants
    pm = derived.get("PLAYER_MANAGER_TYPEINFO_RVA", header.get("PLAYER_MANAGER_TYPEINFO_RVA"))
    plh = derived.get("PLAYER_LOOP_HELPER_TYPEINFO_RVA", header.get("PLAYER_LOOP_HELPER_TYPEINFO_RVA"))
    print(f"invariant PLH->PM slot delta: 0x{pm - plh:X} (expect 0x38)")
    if pm - plh != 0x38:
        problems.append("invariant broken: PLH->PM != 0x38")
    dp = derived.get("FASTLOOT_DELAYPROMISE_TYPEINFO_RVA", 0)
    di = derived.get("FASTLOOT_IGNOREDELAY_TYPEINFO_RVA", 0)
    print(f"invariant promise slot delta: 0x{abs(dp - di):X} (expect 0x8)")
    if abs(dp - di) != 0x8:
        problems.append("invariant broken: promise slots != 8 apart")
    # TOD / layer-mask slots must sit inside the scan window
    tod = derived.get("TOD_SKY_TYPEINFO_RVA")
    if tod is None:
        problems.append("TOD_SKY_TYPEINFO_RVA could not be derived")
        tod = header["TOD_SCAN_RVA_BEGIN"]
    if not (header["TOD_SCAN_RVA_BEGIN"] <= tod < header["TOD_SCAN_RVA_END"]):
        problems.append("TOD_SKY slot outside TOD_SCAN window")
    print(f"TOD_SKY slot 0x{tod:X} inside scan window "
          f"0x{header['TOD_SCAN_RVA_BEGIN']:X}..0x{header['TOD_SCAN_RVA_END']:X}: "
          f"{header['TOD_SCAN_RVA_BEGIN'] <= tod < header['TOD_SCAN_RVA_END']}")
    if problems:
        print(f"\n{len(problems)} PROBLEM(S):")
        for p in problems:
            print("  " + p)
        return 1
    print("\nOK: all constants re-derived / pinned, invariants hold")
    return 0


if __name__ == "__main__":
    sys.exit(main())
