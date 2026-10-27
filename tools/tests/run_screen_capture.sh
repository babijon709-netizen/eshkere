#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
# Exercise the actual merger against a deterministic MediaNDK mock, including
# sample validation, incompatible tracks, failed I/O and preservation.
"${CXX:-g++}" -std=c++17 -O1 -g -Wall -Wextra -Werror -Ijni/include -Itools/tests/media_mock \
    tools/tests/capture_merge_test.cpp tools/tests/media_mock/mock_media.cpp jni/src/capture_merge.cpp \
    -o "$tmp/capture_merge_test"
if ! "$tmp/capture_merge_test" "$tmp/merge-output" 2>"$tmp/merge-errors.log"; then
    cat "$tmp/merge-errors.log" >&2
    exit 1
fi
"${CXX:-g++}" -std=c++17 -O1 -g tools/tests/fake_capture.cpp -o "$tmp/fake_capture"
"${CXX:-g++}" -std=c++17 -O1 -g -pthread -Ijni/include \
    tools/tests/screen_capture_test.cpp jni/src/screen_capture.cpp -o "$tmp/screen_capture_test"
"$tmp/screen_capture_test" "$tmp/fake_capture" "$tmp/output"
# The writer test uses real H.264 and tests seeking, crash recovery and sparse
# >4 GiB offsets. Other capture tests do not require this optional dependency.
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Werror -Ijni/include \
    jni/src/record_mp4.cpp tools/tests/record_mp4_driver.cpp -o "$tmp/record_mp4_driver"
if python3 -c 'import av' 2>/dev/null; then
    python3 tools/tests/record_mp4_real_test.py "$tmp/record_mp4_driver"
else
    echo "SKIP: real H.264 MP4 test (install PyAV 16.0.1)"
fi
