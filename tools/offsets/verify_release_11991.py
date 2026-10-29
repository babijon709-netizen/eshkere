#!/usr/bin/env python3
"""Verify release 11991 offsets against dump.cs and script.json.

Usage: python3 tools/offsets/verify_release_11991.py /path/to/dump.cs /path/to/script.json
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from offsets_diff import load, find

# Managed offsets used by the release profile's core object traversal.
FIELDS = {
    "PlayerManager": {
        "worldCameraRoot": 0x68, "mouseLook": 0x70,
        "playerEventHandler": 0x78, "playerDeathHandlerReference": 0x80,
        "raycastManager": 0x88, "fpManager": 0x90, "inventory": 0x98,
        "kccReference": 0xB0, "vitals": 0xC8, "weaponReference": 0xF0,
        "nicklabel": 0x130, "voicePlayer": 0x140, "characterModel": 0x150,
        "aimAssistConfig": 0x180, "weapons": 0x1B8,
        "lastSavedPosition": 0x1F4, "respawning": 0x228,
        "userID": 0x2A8, "teamName": 0x2B0, "vehicleID": 0x2B8,
        "seatID": 0x2BC, "clanId": 0x2C0, "clanTag": 0x2C8,
        "ziplineID": 0x2DC,
        # statics (parser folds them into fields)
        "activePlayerList": 0x8, "clientPlayerList": 0x10,
    },
    "GameControllerBase": {
        # 11989 lhV (GameNetwork static) -> 11991 GwS, тот же слот 0x0
        "GwS": 0x0, "mainCamera": 0x80, "playerInputHandler": 0x150,
        "screenControlsManager": 0x158, "mapManager": 0x170,
        "TODSky": 0x178,
        "<GwM>k__BackingField": 0x10,   # local PlayerManager
        "<GwZ>k__BackingField": 0x18,   # event handler (11989 kAu, 11991 rIf)
        "<Gwx>k__BackingField": 0x20,   # PlayerInputHandler
        "<GwY>k__BackingField": 0x38,   # CameraManager
        "<Gwp>k__BackingField": 0x50,   # ScreenControlsManager
        "<GwN>k__BackingField": 0x60,   # self
    },
    "WorldStreamingController": {
        "GrassEnabled": 0x18, "TreesEnabled": 0x19,
    },
    "FPMotion": {
        "aimMotion": 0x38, "m_IdleOffset": 0x40, "m_RunOffset": 0x48,
        "runReloadOffset": 0x50, "m_AttackOffset": 0x58,
        "m_AimOffset": 0x60, "m_LookOffset": 0x68,
        "m_TooCloseOffset": 0x70, "Player": 0x120,
    },
    "KCC": {
        "<Motor>k__BackingField": 0x68, "player": 0x78,
        "characterSpeedConfig": 0x80, "head": 0x88,
        "normalHeight": 0xA0, "m_Animator": 0xD8,
        "speedModifier": 0x2FC, "visibility": 0x308,
    },
}
TYPEINFOS = {
    "Oxide.PlayerManager": 0xE60B7B8,
    "Cysharp.Threading.Tasks.PlayerLoopHelper": 0xE60B780,
    "Oxide.GameControllerBase": 0xE606888,
    "Mirror.NetworkClient": 0xE60AA70,
    "Mirror.NetworkServer": 0xE60AB38,
    "Mirror.NetworkTime": 0xE60AB50,
    "HyperHug.Games.Oxide.Features.WorldStreaming.WorldStreamingController": 0xE610328,
    "Cysharp.Threading.Tasks.UniTask.DelayPromise": 0xE61AB58,
    "Cysharp.Threading.Tasks.UniTask.DelayIgnoreTimeScalePromise": 0xE61AB50,
    "ryl": 0xE612F58,  # was yCC in 11989 / Pee in 11985 (25 LayerMask statics)
    "lI": 0xE611560,   # TOD_Sky (TypeDefIndex 706; was gF/Hi in 11989/11985)
}


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__.strip())
    dump = load(sys.argv[1])
    errors = []
    for cls_name, expected_fields in FIELDS.items():
        cls = find(dump, cls_name)
        if not cls:
            errors.append(f"missing class {cls_name}")
            continue
        actual = {name: off for off, _typ, name in cls["fields"]}
        for name, offset in expected_fields.items():
            if actual.get(name) != offset:
                errors.append(f"{cls_name}.{name}: expected {offset:#x}, got {actual.get(name)!r}")
    script = json.load(open(sys.argv[2], encoding="utf-8"))
    typeinfos = {}
    for item in script["TypeInfoPointers"]:
        typeinfos.setdefault(item["DotNetType"], []).append(item["Address"])
    for name, offset in TYPEINFOS.items():
        addrs = typeinfos.get(name, [])
        if addrs != [offset]:
            errors.append(f"{name} TypeInfo: expected [{offset:#x}], got {[hex(a) for a in addrs]}")
    # Инварианты профиля: PLH -> PM = 0x38, промисы через 8 байт.
    if typeinfos.get("Oxide.PlayerManager") != [0xE60B7B8] or \
       typeinfos.get("Cysharp.Threading.Tasks.PlayerLoopHelper") != [0xE60B780]:
        errors.append("PLH/PM slot invariant broken")
    elif 0xE60B7B8 - 0xE60B780 != 0x38:
        errors.append("PLH -> PM delta is not 0x38")
    if 0xE61AB58 - 0xE61AB50 != 8:
        errors.append("promise pair delta is not 8")
    # TOD_Sky обязан быть TypeDefIndex 706 (имя ротируется, TDI — нет).
    tod = find(dump, "lI")
    if not tod or "TypeDefIndex: 706" not in tod["bases"]:
        errors.append("lI is not TOD_Sky (TypeDefIndex 706)")
    elif not any(o == 0x40 and t == "TOD_CycleParameters" for o, t, _n in tod["fields"]):
        errors.append("lI.Cycle is not at 0x40")
    # 25 LayerMask-статиков ryl 0x00..0x60.
    ryl = find(dump, "ryl")
    if not ryl:
        errors.append("missing class ryl")
    else:
        masks = sorted(o for o, t, _n in ryl["fields"] if t == "LayerMask")
        if len(masks) != 25 or masks[0] != 0 or masks[-1] != 0x60:
            errors.append(f"ryl LayerMask statics: {len(masks)} entries, "
                          f"range {hex(masks[0]) if masks else '-'}..{hex(masks[-1]) if masks else '-'}")
    if errors:
        print("\n".join(errors))
        return 1
    print("release 11991: core layouts and TypeInfo slots verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
