#!/usr/bin/env bash
# Production native-surface wrapper against fake libgui symbols; no device needed.
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"${CXX:-g++}" -std=c++17 -O1 -g -Itools/tests/capture_host -Ijni/include \
    tools/tests/capture_surface_test.cpp -ldl -o "$tmp/capture_surface_test"
for pair in '8 26' '8 27' '9 28' '10 29' '11 30' '12 31' '12 32' '13 33' '14 34' '15 35' '16 36'; do
    # Intentional splitting into version and SDK arguments.
    "$tmp/capture_surface_test" $pair
done
for missing in flags apply constructor fail; do
    "$tmp/capture_surface_test" 12 31 "$missing"
    "$tmp/capture_surface_test" 16 36 "$missing"
done
for version in 10 11; do
    sdk=$((version + 19))
    for missing in metadata_ctor metadata_set; do
        "$tmp/capture_surface_test" "$version" "$sdk" "$missing"
    done
done
for pair in '8 26' '11 30' '12 31' '13 33' '16 36'; do
    "$tmp/capture_surface_test" $pair none codename
done
"${CXX:-g++}" -std=c++17 -O1 -g -ffunction-sections -fdata-sections \
    -Itools/tests/capture_draw_host -Itools/tests/capture_host -Ijni/include -Ijni/include/ImGui -Ijni/src \
    tools/tests/capture_draw_test.cpp jni/src/Android_draw/draw.cpp \
    -Wl,--gc-sections -ldl -o "$tmp/capture_draw_test"
"$tmp/capture_draw_test"
"${CXX:-g++}" -std=c++17 -O1 -g -ffunction-sections -fdata-sections \
    -Itools/tests/ui_host -Ijni/include -Ijni/include/ImGui -Ijni/src \
    tools/tests/capture_config_test.cpp -Wl,--gc-sections -ldl -lpthread -o "$tmp/capture_config_test"
"$tmp/capture_config_test"
