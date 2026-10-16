#!/usr/bin/env bash
# build_payload.sh — собирает payload.bin (flat arm64 blob) из inject_demo.c.
#
# Usage:  ANDROID_NDK_HOME=/opt/android-ndk ./payload/build_payload.sh [src.c]
# Output: payload/payload.bin
#
# Требования к blob (проверяются здесь же):
#   entry  ровно на 0x40 — контракт inject.h (kPayloadHdr);
#   релокаций нет — blob грузится по произвольному адресу без обработки;
#   .bss нет — blob не проходит загрузчиком, нули в файле должны быть явно.
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

SRC="${1:-inject_demo.c}"
ELF="$(mktemp /tmp/inj_payload_XXXX.elf)"
trap 'rm -f "$ELF"' EXIT

find_clang() {
    local c
    for c in "${ANDROID_NDK_HOME:-}" "${ANDROID_NDK_ROOT:-}" "${NDK_HOME:-}"; do
        [ -n "$c" ] && [ -x "$c/toolchains/llvm/prebuilt/linux-x86_64/bin/clang" ] && {
            printf '%s\n' "$c/toolchains/llvm/prebuilt/linux-x86_64/bin/clang"; return 0; }
    done
    command -v clang >/dev/null 2>&1 && command -v ld.lld >/dev/null 2>&1 && {
        printf 'clang\n'; return 0; }
    return 1
}

CC="$(find_clang)" || { echo "build_payload.sh: нужен NDK clang (ANDROID_NDK_HOME)" >&2; exit 1; }
echo "build_payload.sh: $CC"

if [ "$CC" = "clang" ]; then
    TARGET="--target=aarch64-linux-gnu"
else
    TARGET="--target=aarch64-linux-android24"
fi

"$CC" $TARGET -O2 -fPIC -nostdlib -ffreestanding -fno-stack-protector -fno-builtin \
      -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident \
      -Wall -Wextra -c "$SRC" -o "${ELF%.elf}.o"
"$CC" $TARGET -nostdlib -no-pie -Wl,-T,link.ld -Wl,--build-id=none -Wl,--no-eh-frame-hdr \
      -o "$ELF" "${ELF%.elf}.o"
rm -f "${ELF%.elf}.o"

# ---- проверки контракта -----------------------------------------------------
if command -v llvm-nm >/dev/null 2>&1; then NM=llvm-nm; else NM=nm; fi
if command -v llvm-readelf >/dev/null 2>&1; then RE=llvm-readelf; else RE=readelf; fi

ENTRY=$("$NM" "$ELF" | awk '$3=="inj_entry"{print $1}')
[ "$ENTRY" = "0000000000000040" ] || [ "$ENTRY" = "40" ] || {
    echo "build_payload.sh: inj_entry на 0x$ENTRY, контракт требует 0x40" >&2; exit 1; }

if "$RE" -r "$ELF" | grep -q 'R_AARCH64'; then
    echo "build_payload.sh: в blob есть релокации — так нельзя" >&2
    "$RE" -r "$ELF" >&2
    exit 1
fi
if "$NM" "$ELF" | awk '$2 ~ /^[bBdD]$/{exit 1}'; then :; else
    echo "build_payload.sh: есть .data/.bss символы — blob должен быть чистым кодом" >&2
    exit 1
fi

if command -v llvm-objcopy >/dev/null 2>&1; then OC=llvm-objcopy; else OC=objcopy; fi
"$OC" -O binary "$ELF" payload.bin
echo "build_payload.sh: ok -> payload/payload.bin ($(stat -c%s payload.bin) байт)"
