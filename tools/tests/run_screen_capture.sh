#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"${CXX:-g++}" -std=c++17 -O1 -g tools/tests/fake_capture.cpp -o "$tmp/fake_capture"
"${CXX:-g++}" -std=c++17 -O1 -g -pthread -Ijni/include \
    tools/tests/screen_capture_test.cpp jni/src/screen_capture.cpp -o "$tmp/screen_capture_test"
"$tmp/screen_capture_test" "$tmp/fake_capture" "$tmp/output"
