#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"${CXX:-g++}" -std=c++17 -O1 -g -DIMGUI_ENABLE_TEST_ENGINE \
    -ffunction-sections -fdata-sections \
    -Itools/tests/ui_host -Ijni/include -Ijni/include/ImGui -Ijni/src \
    tools/tests/menu_ui_test.cpp jni/src/menu.cpp jni/src/widgets.cpp \
    jni/src/theme.cpp jni/src/lang.cpp \
    jni/src/ImGui/imgui.cpp jni/src/ImGui/imgui_draw.cpp \
    jni/src/ImGui/imgui_widgets.cpp jni/src/ImGui/imgui_tables.cpp \
    -Wl,--gc-sections -o "$tmp/menu_ui_test"
"$tmp/menu_ui_test" "$@"
