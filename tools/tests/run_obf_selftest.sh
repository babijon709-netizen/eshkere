#!/usr/bin/env bash
# Проверяет весь конвейер обфускации на хосте (x86_64, g++):
#   рандомный ключ сборки -> bw_build_key.h -> PIE -> elf_crypt.py -> запуск.
# Ни игры, ни устройства не нужно. Выполняется и в GitHub Actions.
#
# Два макета сегментов: обычный и 16K-выровненный (как в arm64-сборке с
# -z,max-page-size=16384). Второй ловит случай, когда строки делят страницу
# с исполняемым сегментом и рантайм не может их расшифровать.
set -euo pipefail
cd "$(dirname "$0")/../.."

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

KEY="$(python3 -c 'import secrets; print("0x%016x" % secrets.randbits(64))')"
printf '#pragma once\n#define BW_BUILD_KEY %sULL\n' "$KEY" > "$tmp/bw_build_key.h"

run_case() {
    local name="$1"
    shift
    local bin="$tmp/obf_test_$name"

    "${CXX:-g++}" -std=c++17 -O2 -fPIE -pie -fno-jump-tables \
        -ffunction-sections -fdata-sections \
        -I"$tmp" -Ijni/include -Ijni/src \
        tools/tests/obf_selftest.cpp jni/src/bw_obf.cpp \
        -Wl,--gc-sections "$@" -o "$bin"

    # Рантайм не должен тащить ни dl_iterate_phdr (ненадёжный load bias на
    # bionic), ни mprotect (RW-окно на 16K-ядре снимало X с соседнего R E).
    if command -v readelf >/dev/null 2>&1; then
        if readelf -Ws "$bin" | grep -qE ' (dl_iterate_phdr|mprotect)@'; then
            echo "obf-selftest[$name]: рантайм снова импортирует dl_iterate_phdr/mprotect" >&2
            exit 1
        fi
    fi

    # До патча строка лежит открыто.
    if ! grep -a -q 'BW_OBF_CANARY_7f3a91c4e2' "$bin"; then
        echo "obf-selftest[$name]: контрольная строка не найдена ДО шифрования" >&2
        exit 1
    fi

    python3 tools/obf/elf_crypt.py "$bin" --key "$KEY" --keep-sections
    python3 tools/obf/elf_crypt.py "$bin" --check

    # Структура .bwmeta: новый билдер обязан записать ненулевой meta_vaddr и
    # выставить FLAG_SEGMENTS_RW — иначе конструктор не имеет права писать.
    python3 - "$bin" "$name" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read()
magic = bytes([0xB7, 0x53, 0x1E, 0x9A, 0x44, 0xC0, 0xF1, 0x6D,
               0x2A, 0x88, 0x37, 0xDE, 0x90, 0x0B, 0x65, 0xEC])
assert d.count(magic) == 1, "маркер .bwmeta должен быть ровно один"
i = d.find(magic)
count, meta_va, flags = struct.unpack_from('<QQQ', d, i + 16)
assert 0 < count <= 1024, f"count={count}"
assert 0 < meta_va < (1 << 28), f"meta_vaddr={meta_va:#x}"
assert flags & 1, f"FLAG_SEGMENTS_RW не выставлен: {flags:#x}"
va, sz = struct.unpack_from('<QQ', d, i + 40)
assert va > 0 and sz > 0, (va, sz)
print(f"obf-selftest[{sys.argv[2]}]: метадата count={count} "
      f"meta_vaddr={meta_va:#x} flags={flags:#x} range=[{va:#x},{va + sz:#x})")
PY

    # После патча — ни одной открытой строки.
    if grep -a -q 'BW_OBF_CANARY_7f3a91c4e2' "$bin"; then
        echo "obf-selftest[$name]: строка осталась открытой ПОСЛЕ шифрования" >&2
        exit 1
    fi

    local out
    out="$("$bin")"
    local expect='BW_OBF_CANARY_7f3a91c4e2|C61StIeKasRIz89g3c6xLzIc9L0LaRbI|com.catsbit.oxidesurvivalisland|769|768'
    if [ "$out" != "$expect" ]; then
        echo "obf-selftest[$name]: неверный вывод: $out" >&2
        exit 1
    fi
    echo "obf-selftest[$name]: OK ($out)"
}

run_case default
run_case 16k -Wl,-z,separate-code -Wl,-z,max-page-size=16384
