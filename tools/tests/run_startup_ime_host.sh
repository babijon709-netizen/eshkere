#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/startup_ime_apk.inc" <<'HEADER'
#pragma once
namespace startup_ime_apk {
inline constexpr char kPackage[] = "com.benzware.keyinput.ptest";
inline constexpr unsigned char kData[] = {'t','e','s','t','-','a','p','k'};
}
HEADER
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Werror -I"$tmp" -Ijni/include \
    tools/tests/startup_ime_host_test.cpp -o "$tmp/test"
"$tmp/test" "$tmp"
