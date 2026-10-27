#!/usr/bin/env bash
# Real ImGui startup keyboard with synthetic touches, on a headless host.
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"${CXX:-g++}" -std=c++17 -O1 -DIMGUI_ENABLE_TEST_ENGINE \
    -ffunction-sections -fdata-sections -Ijni/include -Ijni/include/ImGui \
    tools/tests/startup_ui_test.cpp jni/src/ImGui/imgui.cpp jni/src/ImGui/imgui_draw.cpp \
    jni/src/ImGui/imgui_widgets.cpp jni/src/ImGui/imgui_tables.cpp \
    -Wl,--gc-sections -o "$tmp/startup_ui"
"$tmp/startup_ui"
