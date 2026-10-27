#!/usr/bin/env bash
# Run the actual gate in a host PIE before and after the production ELF patcher.
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
key="$(python3 -c 'import secrets; print("0x%016x" % secrets.randbits(64))')"
printf '#pragma once\n#define BW_BUILD_KEY %sULL\n' "$key" > "$tmp/bw_build_key.h"
"${CXX:-g++}" -std=c++17 -O2 -fPIE -pie -flto -fno-jump-tables \
    -ffunction-sections -fdata-sections -I"$tmp" -Ijni/include \
    tools/tests/startup_cli.cpp jni/src/bw_obf.cpp \
    -Wl,--gc-sections,-z,separate-code,-z,max-page-size=16384 -o "$tmp/plain"
cp "$tmp/plain" "$tmp/encrypted"
python3 tools/obf/elf_crypt.py "$tmp/encrypted" --key "$key"
python3 tools/obf/elf_crypt.py "$tmp/encrypted" --check
python3 tools/tests/startup_cli_test.py "$tmp/plain" "$tmp/encrypted"
