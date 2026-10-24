#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
for mode in normal fast sanitizer; do
    case "$mode" in
      normal) flags=(-O2 -Wall -Wextra -Werror);;
      fast) flags=(-O3 -ffast-math);;
      sanitizer) flags=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer);;
    esac
    "${CXX:-g++}" -std=c++17 "${flags[@]}" -Ijni/src -Ijni/include \
        jni/tests/test_third_person.cpp -o "$tmp/test"
    "$tmp/test"
    echo "third-person: $mode PASS"
done
