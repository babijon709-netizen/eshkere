#!/usr/bin/env python3
"""Verify release 11985 offsets against dump.cs and script.json.

Usage: python3 tools/offsets/verify_release_11985.py /path/to/dump.cs /path/to/script.json
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
    },
    "GameControllerBase": {
        "gLI": 0x0, "mainCamera": 0x80, "playerInputHandler": 0x150,
        "screenControlsManager": 0x158, "mapManager": 0x170,
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
    "Oxide.PlayerManager": 0xE5A5908,
    "Cysharp.Threading.Tasks.PlayerLoopHelper": 0xE5A58D0,
    "Oxide.GameControllerBase": 0xE59F970,
    "Mirror.NetworkClient": 0xE5A3980,
    "Mirror.NetworkServer": 0xE5A3A48,
    "Mirror.NetworkTime": 0xE5A3A60,
    "HyperHug.Games.Oxide.Features.WorldStreaming.WorldStreamingController": 0xE5AAA28,
    "Cysharp.Threading.Tasks.UniTask.DelayPromise": 0xE5B4CD0,
    "Cysharp.Threading.Tasks.UniTask.DelayIgnoreTimeScalePromise": 0xE5B4CC8,
    "Pee": 0xE5A5260,
    "Hi": 0xE59FF08,
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
    typeinfos = {item["DotNetType"]: item["Address"] for item in script["TypeInfoPointers"]}
    for name, offset in TYPEINFOS.items():
        if typeinfos.get(name) != offset:
            actual = typeinfos.get(name)
            errors.append(f"{name} TypeInfo: expected {offset:#x}, got {actual and hex(actual)}")
    if errors:
        print("\n".join(errors))
        return 1
    print("release 11985: core layouts and TypeInfo slots verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
