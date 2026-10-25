#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -Ijni/src \
    jni/tests/test_touch_device_filter.cpp -o "$tmp/touch_device_filter_test"
"$tmp/touch_device_filter_test"
