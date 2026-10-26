#!/usr/bin/env bash
# Проверяет весь конвейер обфускации на хосте (x86_64, g++):
#   рандомный ключ сборки -> bw_build_key.h -> PIE -> elf_crypt.py -> запуск.
# Ни игры, ни устройства не нужно. Выполняется и в GitHub Actions.
set -euo pipefail
cd "$(dirname "$0")/../.."

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

KEY="$(python3 -c 'import secrets; print("0x%016x" % secrets.randbits(64))')"
printf '#pragma once\n#define BW_BUILD_KEY %sULL\n' "$KEY" > "$tmp/bw_build_key.h"

"${CXX:-g++}" -std=c++17 -O2 -fPIE -pie -fno-jump-tables \
    -ffunction-sections -fdata-sections \
    -I"$tmp" -Ijni/include -Ijni/src \
    tools/tests/obf_selftest.cpp jni/src/bw_obf.cpp \
    -Wl,--gc-sections -o "$tmp/obf_test"

# До патча строка лежит открыто.
if ! grep -a -q 'BW_OBF_CANARY_7f3a91c4e2' "$tmp/obf_test"; then
    echo "obf-selftest: контрольная строка не найдена ДО шифрования" >&2
    exit 1
fi

python3 tools/obf/elf_crypt.py "$tmp/obf_test" --key "$KEY" --keep-sections > "$tmp/obf.log"
cat "$tmp/obf.log"

# После патча — ни одной открытой строки.
if grep -a -q 'BW_OBF_CANARY_7f3a91c4e2' "$tmp/obf_test"; then
    echo "obf-selftest: строка осталась открытой ПОСЛЕ шифрования" >&2
    exit 1
fi

OUT="$("$tmp/obf_test")"
EXPECT='BW_OBF_CANARY_7f3a91c4e2|C61StIeKasRIz89g3c6xLzIc9L0LaRbI|com.catsbit.oxidesurvivalisland|769|768'
if [ "$OUT" != "$EXPECT" ]; then
    echo "obf-selftest: неверный вывод: $OUT" >&2
    exit 1
fi
echo "obf-selftest: OK ($OUT)"
