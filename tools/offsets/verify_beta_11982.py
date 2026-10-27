#!/usr/bin/env python3
"""Check the beta 11982 profile against an extracted Il2CppDumper dump.

Usage: python3 tools/offsets/verify_beta_11982.py /path/to/dump.cs /path/to/script.json [--libil2cpp /path/to/libil2cpp.so]
Obfuscated names and positional matches are build-specific. This checks the
layout and TypeInfo slots, not game runtime behavior or LayerMask semantics.
"""
import argparse
from pathlib import Path
import re
import struct
import sys

from offsets_diff import load

ROOT = Path(__file__).resolve().parents[2]

# Name -> (class, field) in dumpbeta11982.7z. Obfuscated fields are matched to
# their previous counterparts by type/order; their runtime meaning may still
# require validation on a device.
PM = "Oxide.PlayerManager"
KCC = "HyperHug.Games.Oxide.Features.Player.KCC"
PW = "HyperHug.Games.Oxide.Features.Weapons.PlayerWeapon"
WSC = "HyperHug.Games.Oxide.Features.WorldStreaming.WorldStreamingController"
FIELDS = {
    "PLAYER_TRANSFORM": (PM, "worldCameraRoot"),
    "PLAYER_POSITION": (PM, "lastSavedPosition"),
    "PLAYER_CHARACTER_MODEL": (PM, "characterModel"),
    "PLAYER_NICKLABEL": (PM, "nicklabel"),
    "PLAYER_DISPLAY_NAME": (PM, "Gvz"),
    "PLAYER_VOICE_STATE": (PM, "Gvv"),
    "PLAYER_VOICE_PLAYER": (PM, "voicePlayer"),
    "PLAYER_EVENT_HANDLER": (PM, "playerEventHandler"),
    "PLAYER_MANAGER_RESPAWNING": (PM, "respawning"),
    "PLAYER_DEATH_HANDLER": (PM, "playerDeathHandlerReference"),
    "PLAYER_VITALS": (PM, "vitals"),
    "PLAYER_UKX": (PM, "GvL"),
    "PLAYER_MODEL_INFO_REF": (PM, "GvL"),
    "PLAYER_INVENTORY": (PM, "inventory"),
    "PLAYER_WEAPON_REFERENCE": (PM, "weaponReference"),
    "PLAYER_WEAPONS_ARRAY": (PM, "weapons"),
    "PLAYER_FP_MANAGER": (PM, "fpManager"),
    "PLAYER_MOUSE_LOOK": (PM, "mouseLook"),
    "PLAYER_RAYCAST_MANAGER": (PM, "raycastManager"),
    "PLAYER_VOICE_COLLIDER": (PM, "voiceCollider"),
    "PLAYER_TRIGGER_PROBE": (PM, "triggerProbe"),
    "PLAYER_AIM_ASSIST_CONFIG": (PM, "aimAssistConfig"),
    "PLAYER_AIM_ASSIST": (PM, "<Gvf>k__BackingField"),
    "PLAYER_HIT_TRACKER": (PM, "GvC"),
    "PLAYER_KCC_REFERENCE": (PM, "kccReference"),
    "PLAYER_USER_ID": (PM, "userID"),
    "PLAYER_TEAM_NAME": (PM, "teamName"),
    "PLAYER_VEHICLE_ID": (PM, "vehicleID"),
    "PLAYER_SEAT_ID": (PM, "seatID"),
    "PLAYER_CLAN_ID": (PM, "clanId"),
    "PLAYER_CLAN_TAG": (PM, "clanTag"),
    "PLAYER_ZIPLINE_ID": (PM, "ziplineID"),
    "PLAYERWEAPON_VIEW": (PW, "playerWeaponViewReference"),
    "PLAYERWEAPON_PIECE": (PW, "RAt"),
    "PLAYERWEAPON_PLAYER_BACKREF": (PW, "<player>k__BackingField"),
    "FPWEAPON_TYPE": ("Oxide.FPWeaponBase", "weaponType"),
    "KCC_MOVE": (KCC, "TfG"),
    "KCC_VISIBILITY": (KCC, "visibility"),
    "KCC_MOTOR_ENABLED": (KCC, "TLg"),
    "KCC_HAS_OBSERVER_MOVE": (KCC, "TLT"),
    "KCC_CHARACTER_ANIMATION": (KCC, "TfL"),
    "KCC_PLAYER_BACKREF": (KCC, "player"),
    "KCC_HEAD_TRANSFORM": (KCC, "head"),
    "KCC_HITBOX_ROOT": (KCC, "hitBoxRecorderRoot"),
    "CHAR_SPEED_CONFIG_KCC": (KCC, "characterSpeedConfig"),
    "EVENT_HANDLER_MANAGER_BACKREF": ("tev", "manager"),
    "EVENT_HANDLER_AIM_ACTIVITY": ("tev", "Aim"),
    "EVENT_HANDLER_MOVEMENT_INPUT": ("tev", "MovementInput"),
    "EVENT_HANDLER_LOOK_DIRECTION": ("tev", "LookDirection"),
    "EVENT_HANDLER_LAST_HIT_POINT": ("tev", "LastLocalHitPoint"),
    "EVENT_HANDLER_LAST_HIT_TIME": ("tev", "LastLocalHitTime"),
    "EVENT_HANDLER_HEALTH": ("tek", "Health"),
    "HIT_TRACKER_HIT_DICT": ("Ol", "TSI"),
    "UKX_MODEL_INFO": ("Qk", "<ThG>k__BackingField"),
    "PLAYER_INPUT_HANDLER_SPRINT_OVERRIDE": ("Oxide.PlayerInputHandler", "GcZ"),
    "FPMANAGER_CURRENT_OBJECT": ("Oxide.FPManager", "_currentWeapon"),
    "FPMANAGER_CURRENT_WEAPON": ("Oxide.FPManager", "GpL"),
    "FPMANAGER_WORLD_FOV_TARGET": ("Oxide.FPManager", "GpM"),
    "FPMANAGER_AIM_BLEND": ("Oxide.FPManager", "<GpG>k__BackingField"),
    "FPMOTION_IDLE_OFFSET": ("Oxide.FPMotion", "m_IdleOffset"),
    "FPMOTION_RUN_OFFSET": ("Oxide.FPMotion", "m_RunOffset"),
    "FPMOTION_AIM_OFFSET": ("Oxide.FPMotion", "m_AimOffset"),
    "FPMOTION_OBJECT": ("Oxide.FPMotion", "Gez"),
    "FPMOTION_PLAYER": ("Oxide.FPMotion", "Player"),
    "WORLD_STREAMING_GRASS_ENABLED_FIELD": (WSC, "GrassEnabled"),
    "WORLD_STREAMING_TREES_ENABLED_FIELD": (WSC, "TreesEnabled"),
    "NETWORK_CLIENT_SPAWNED": ("Mirror.NetworkClient", "spawned"),
    "NETWORK_CLIENT_BUFFER_MULTIPLIER": ("Mirror.NetworkClient", "bufferTimeMultiplier"),
    "NETWORK_SERVER_TICK_RATE": ("Mirror.NetworkServer", "tickRate"),
    "TOD_SKY_CYCLE": ("yY", "Cycle"),
}
SLOTS = {
    "PLAYER_MANAGER_TYPEINFO_RVA": "Oxide.PlayerManager_TypeInfo",
    "PLAYER_LOOP_HELPER_TYPEINFO_RVA": "Cysharp.Threading.Tasks.PlayerLoopHelper_TypeInfo",
    "GAME_CONTROLLER_TYPEINFO_RVA": "Oxide.GameControllerBase_TypeInfo",
    "NETWORK_CLIENT_TYPEINFO_RVA": "Mirror.NetworkClient_TypeInfo",
    "NETWORK_SERVER_TYPEINFO_RVA": "Mirror.NetworkServer_TypeInfo",
    "NETWORK_TIME_TYPEINFO_RVA": "Mirror.NetworkTime_TypeInfo",
    "UNITY_CAMERA_TYPEINFO_RVA": "UnityEngine.Camera_TypeInfo",
    "WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA": WSC + "_TypeInfo",
    "HMJ_TYPEINFO_RVA": "tzc_TypeInfo",
    "FASTLOOT_DELAYPROMISE_TYPEINFO_RVA": "Cysharp.Threading.Tasks.UniTask.DelayPromise_TypeInfo",
    "FASTLOOT_IGNOREDELAY_TYPEINFO_RVA": "Cysharp.Threading.Tasks.UniTask.DelayIgnoreTimeScalePromise_TypeInfo",
}
# These are nested helper classes, not interchangeable promise TypeInfos.
DISABLED_ALTS = {
    "FASTLOOT_DELAYPROMISE_TYPEINFO_RVA_ALT":
        ("Cysharp.Threading.Tasks.UniTask.DelayPromise.<>c_TypeInfo", 0xE909C38),
    "FASTLOOT_IGNOREDELAY_TYPEINFO_RVA_ALT":
        ("Cysharp.Threading.Tasks.UniTask.DelayIgnoreTimeScalePromise.<>c_TypeInfo", 0xE909C30),
}


def constants(path):
    return dict(re.findall(r"^inline constexpr std::uint64_t (\w+)\s*=\s*([^;]+);",
                           path.read_text(encoding="utf-8"), re.MULTILINE))


def value(name, beta, release):
    seen = set()
    table = beta
    while True:
        marker = (name, table is beta)
        if marker in seen or name not in table:
            raise ValueError("missing/cyclic profile constant: " + name)
        seen.add(marker)
        expr = table[name].strip()
        if expr.startswith("game_offsets::"):
            table, name = release, expr.split("::", 1)[1]
        elif re.fullmatch(r"[A-Za-z_]\w*", expr):
            name = expr
        else:
            return int(expr, 0)


def typeinfo_slots(path, wanted):
    # script.json is hundreds of MB; Il2CppDumper writes Address immediately
    # before Name. Stream rather than loading all metadata into RAM.
    address = None
    found = {}
    address_re = re.compile(r'^\s*"Address":\s*(\d+)\s*,?\s*$')
    name_re = re.compile(r'^\s*"Name":\s*"([^"]+)"')
    with open(path, encoding="utf-8") as f:
        for line in f:
            if m := address_re.match(line):
                address = int(m.group(1))
            elif m := name_re.match(line):
                name = m.group(1)
                if name in wanted and address is not None:
                    found.setdefault(name, set()).add(address)
                address = None
    return found


def verify_elf(path, beta, release):
    # Verify the 11982 getters' AArch64 instructions and their RELATIVE GOT
    # relocations. This establishes the slot -> class -> static-field path
    # without requiring Capstone or pyelftools on the user's machine.
    code = {
        # NetworkClient.get_sendRate: NetworkServer GOT, class, statics, tickRate.
        0xCB26160: "b3d200b0", 0xCB26168: "737243f9", 0xCB26184: "600240f9",
        0xCB261C8: "085c40f9", 0xCB261D0: "000940b9",
        # NetworkTime.get_rtt: NetworkTime GOT, class, statics, _rtt.Value.
        0xCB4BAD0: "93d10090", 0xCB4BAD8: "73e646f9", 0xCB4BAF4: "600240f9",
        0xCB4BB08: "085c40f9", 0xCB4BB10: "001140fd",
        # NetworkClient.get_bufferTime: NetworkClient GOT, class, statics, multiplier.
        0xCB32D24: "53d200b0", 0xCB32D2C: "73ee46f9", 0xCB32D40: "600240f9",
        0xCB32D58: "085c40f9", 0xCB32D68: "018540fd",
    }
    wanted = {
        0xE57B6E0: value("NETWORK_SERVER_TYPEINFO_RVA", beta, release),
        0xE57BDC8: value("NETWORK_TIME_TYPEINFO_RVA", beta, release),
        0xE57BDD8: value("NETWORK_CLIENT_TYPEINFO_RVA", beta, release),
    }
    errors = []
    with open(path, "rb") as f:
        hdr = struct.unpack("<16sHHIQQQIHHHHHH", f.read(64))
        if hdr[0][:6] != b"\x7fELF\x02\x01" or hdr[2] != 183:
            raise ValueError("expected a little-endian AArch64 ELF64")
        phoff, shoff = hdr[5], hdr[6]
        phentsize, phnum, shentsize, shnum = hdr[9:13]
        if phentsize < 56 or shentsize < 64 or not shnum:
            raise ValueError("unsupported ELF header table")
        segments = []
        for n in range(phnum):
            f.seek(phoff + n * phentsize)
            ph = struct.unpack("<IIQQQQQQ", f.read(56))
            if ph[0] == 1:  # PT_LOAD
                segments.append((ph[3], ph[5], ph[2]))
        for addr, expected in code.items():
            file_offsets = [off + addr - va for va, size, off in segments
                            if va <= addr and addr + 4 <= va + size]
            if not file_offsets:
                errors.append(f"ELF: instruction {addr:#x} not in a load segment")
                continue
            f.seek(file_offsets[0])
            if f.read(4).hex() != expected:
                errors.append(f"ELF: getter instruction changed at {addr:#x}")
        found = {}
        for n in range(shnum):
            f.seek(shoff + n * shentsize)
            sh = struct.unpack("<IIQQQQIIQQ", f.read(64))
            if sh[1] != 4:  # SHT_RELA
                continue
            offset, size, entsize = sh[4], sh[5], sh[9]
            if entsize != 24 or size % entsize:
                raise ValueError("unsupported ELF RELA entry size")
            f.seek(offset)
            for got, info, addend in struct.iter_unpack("<QQq", f.read(size)):
                if got in wanted:
                    found[got] = (info, addend)
        for got, target in wanted.items():
            if found.get(got) != (1027, target):  # R_AARCH64_RELATIVE, symbol 0
                errors.append(f"ELF: relocation {got:#x} is {found.get(got)}, expected {target:#x}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump_cs")
    parser.add_argument("script_json")
    parser.add_argument("--libil2cpp", help="also verify the beta arm64 timing getters and GOT relocations")
    args = parser.parse_args()
    beta = constants(ROOT / "jni/src/game_offsets_beta.h")
    release = constants(ROOT / "jni/src/game_offsets.h")
    dump = load(args.dump_cs)
    errors = []
    for constant, (class_name, field_name) in FIELDS.items():
        klass = dump.get(class_name)
        matches = [] if klass is None else [off for off, _, name in
                  klass["fields"] + klass["statics"] if name == field_name]
        expected = value(constant, beta, release)
        if matches != [expected]:
            errors.append(f"{constant}: profile {expected:#x}, {class_name}.{field_name} = {matches}")

    rtt = [off for off, _, name in dump.get("Mirror.NetworkTime", {}).get("fields", [])
           if name == "_rtt"]
    ema = [off for off, _, name in dump.get("Mirror.ExponentialMovingAverage", {}).get("fields", [])
           if name == "Value"]
    if len(rtt) != 1 or len(ema) != 1 or rtt[0] + ema[0] != value("NETWORK_TIME_RTT", beta, release):
        errors.append("NETWORK_TIME_RTT: _rtt.Value layout changed")

    # Check the shape of the candidate classes; values and semantics are still
    # checked by the real runtime validator before using a mask or TOD object.
    masks = dump.get("tzc", {}).get("fields", [])
    if [(off, typ) for off, typ, _ in masks] != [(n * 4, "LayerMask") for n in range(25)]:
        errors.append("tzc: LayerMask field signature changed")
    sky = dump.get("yY", {}).get("fields", [])
    if not any(off == 0 and typ == "List<yY>" for off, typ, _ in sky):
        errors.append("yY: List<yY> static signature changed")

    wanted = set(SLOTS.values()) | {"yY_TypeInfo"} | {name for name, _ in DISABLED_ALTS.values()}
    actual = typeinfo_slots(args.script_json, wanted)
    for constant, name in SLOTS.items():
        expected = value(constant, beta, release)
        if actual.get(name) != {expected}:
            errors.append(f"{constant}: profile {expected:#x}, {name} = {sorted(actual.get(name, []))}")
    for constant, (name, address) in DISABLED_ALTS.items():
        if value(constant, beta, release) != 0:
            errors.append(f"{constant}: must be disabled; {name} is not a promise")
        if actual.get(name) != {address}:
            errors.append(f"{name}: missing or changed")
    if actual.get("yY_TypeInfo") != {0xE8FF6D8}:
        errors.append("yY_TypeInfo: missing or changed")
    begin, end = value("TOD_SCAN_RVA_BEGIN", beta, release), value("TOD_SCAN_RVA_END", beta, release)
    if not all(begin <= rva < end for name in wanted - {"UnityEngine.Camera_TypeInfo"}
               for rva in actual.get(name, ())):
        errors.append("a required TypeInfo slot falls outside the scan window")
    if args.libil2cpp:
        try:
            errors.extend(verify_elf(args.libil2cpp, beta, release))
        except (OSError, ValueError, struct.error) as exc:
            errors.append(f"ELF: {exc}")
    if errors:
        print("\n".join("FAIL: " + error for error in errors), file=sys.stderr)
        return 1
    print(f"PASS: {len(FIELDS)} field offsets, {len(SLOTS)} TypeInfo slots, disabled nested-class alternatives, TOD/mask signatures")
    if args.libil2cpp:
        print("PASS: timing getter instructions and three TypeInfo GOT relocations")
    print("Runtime mask meanings, obfuscated positional candidates and device behavior remain unverified.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
