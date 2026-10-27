#!/usr/bin/env bash
# Production farm scan and controller with fake memory/input, plus ASan/UBSan.
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
flags=(-std=c++17 -O1 -ffast-math -fsanitize=address,undefined -fno-omit-frame-pointer
       -ffunction-sections -fdata-sections -Ijni/src -Ijni/include -Ijni/include/ImGui)
"${CXX:-g++}" "${flags[@]}" tools/tests/farm_scan_test.cpp \
    -Wl,--gc-sections -lpthread -o "$tmp/farm_scan"
"$tmp/farm_scan"
"${CXX:-g++}" -Itools/tests/ui_host "${flags[@]}" \
    tools/tests/farm_controller_test.cpp jni/src/farm.cpp \
    -Wl,--gc-sections -o "$tmp/farm_controller"
"$tmp/farm_controller"
