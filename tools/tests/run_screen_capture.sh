#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"${CXX:-g++}" -std=c++17 -O1 -g tools/tests/fake_capture.cpp -o "$tmp/fake_capture"
"${CXX:-g++}" -std=c++17 -O1 -g -pthread -Ijni/include \
    tools/tests/screen_capture_test.cpp jni/src/screen_capture.cpp -o "$tmp/screen_capture_test"
"$tmp/screen_capture_test" "$tmp/fake_capture" "$tmp/output"
# Android caps each recording command at 180s; shorten only the test build so
# several real supervisor rollovers and failures can be checked quickly.
"${CXX:-g++}" -std=c++17 -O1 -g -pthread -DSCREEN_CAPTURE_TEST_SEGMENT_SECONDS=2 -Ijni/include \
    tools/tests/screen_capture_rollover_test.cpp jni/src/screen_capture.cpp -o "$tmp/screen_capture_rollover_test"
"$tmp/screen_capture_rollover_test" "$tmp/fake_capture" "$tmp/rollover-output"
