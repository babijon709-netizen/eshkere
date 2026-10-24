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
    "${CXX:-g++}" -std=c++17 "${flags[@]}" -Ijni/src \
        jni/tests/test_network_aa.cpp -o "$tmp/test"
    "$tmp/test"
    echo "external AA: $mode PASS"
done
