#!/usr/bin/env bash
# Build benzware (jni/Android.mk, arm64-v8a PIE executable) with ndk-build.
#
# Output:
#   libs/arm64-v8a/benzware.sh        <- main artifact (what CI uploads)
#   libs/arm64-v8a/benzware           <- raw ndk-build output (LOCAL_MODULE := benzware)
#
# Usage:  ./build.sh          (CI)   NDK from ANDROID_NDK_HOME
#         ANDROID_NDK_HOME=/opt/android-ndk ./build.sh   (local)
set -euo pipefail

cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

find_ndk_build() {
    local c
    for c in "${ANDROID_NDK_HOME:-}" "${ANDROID_NDK_ROOT:-}" "${NDK_HOME:-}" "${NDK:-}"; do
        [ -n "$c" ] && [ -x "$c/ndk-build" ] && { printf '%s\n' "$c/ndk-build"; return 0; }
    done
    if command -v ndk-build >/dev/null 2>&1; then
        command -v ndk-build
        return 0
    fi
    local sdk="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}"
    if [ -d "$sdk/ndk" ]; then
        c=$(ls -1d "$sdk"/ndk/*/ 2>/dev/null | sort -V | tail -n1 || true)
        [ -n "${c:-}" ] && [ -x "${c}ndk-build" ] && { printf '%s\n' "${c}ndk-build"; return 0; }
    fi
    return 1
}

NDK_BUILD="$(find_ndk_build || true)"
if [ -z "$NDK_BUILD" ]; then
    echo "build.sh: ndk-build not found — set ANDROID_NDK_HOME" >&2
    exit 1
fi
echo "build.sh: using $NDK_BUILD"
echo "build.sh: ndk $(cat "$(dirname "$NDK_BUILD")/source.properties" 2>/dev/null | tr -d '\r' | tail -n1)"

# ---- ключ сборки ---------------------------------------------------------
# Случайный на каждую сборку: два артефакта одной и той же ревизии имеют
# разные шифротексты .rodata. Ключ живёт только в сгенерированном заголовке
# (в git не попадает) и в immediates кода splitmix64 — готовой строки/таблицы
# с ключом в бинаре нет.
if ! command -v python3 >/dev/null 2>&1; then
    echo "build.sh: нужен python3 (генерация ключа + elf_crypt.py)" >&2
    exit 1
fi
OBF_KEY="$(python3 -c 'import secrets; print("0x%016x" % secrets.randbits(64))')"
printf '#pragma once\n// Сгенерировано build.sh. Не коммитить: ключ уникален для сборки.\n#define BW_BUILD_KEY %sULL\n' \
    "$OBF_KEY" > jni/include/bw_build_key.h

rm -rf obj libs

JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
ABI=arm64-v8a
NDK_ARGS=(-j"$JOBS" NDK_PROJECT_PATH=.
          NDK_APPLICATION_MK=jni/Application.mk APP_BUILD_SCRIPT=jni/Android.mk)

"$NDK_BUILD" "${NDK_ARGS[@]}" APP_MODULES=benzware

BIN="libs/$ABI/benzware"
if [ ! -f "$BIN" ]; then
    echo "build.sh: expected $BIN, got:" >&2
    ls -l "libs/$ABI" 2>/dev/null >&2 || true
    exit 1
fi

cp -f "$BIN" "libs/$ABI/benzware.sh"
chmod 0755 "libs/$ABI/benzware.sh"

# ---- хвост обфускации -------------------------------------------------------
# Сборка уже идёт со strip и -fno-ident, но в бинаре остаются мелочи, по которым
# удобно цепляться: .comment (версия тулчейна), .note.gnu.build-id и — если
# какой-то TU собрал путь через __FILE__ — абсолютный путь исходника. Чистим и
# проверяем, что наружу ничего не торчит: без проверки «обфускация» остаётся
# обещанием.
BIN_SH="libs/$ABI/benzware.sh"
NDK_BIN="$(dirname "$NDK_BUILD")/toolchains/llvm/prebuilt/linux-x86_64/bin"

find_tool() {
    local c
    for c in "$@"; do
        [ -n "$c" ] && [ -x "$c" ] && { printf '%s\n' "$c"; return 0; }
    done
    return 1
}

STRIP_TOOL="$(find_tool "$NDK_BIN/llvm-strip" "$(command -v llvm-strip || true)" "$(command -v strip || true)" || true)"
READELF_TOOL="$(find_tool "$NDK_BIN/llvm-readelf" "$(command -v llvm-readelf || true)" "$(command -v readelf || true)" || true)"

if [ -n "$STRIP_TOOL" ]; then
    "$STRIP_TOOL" --strip-all \
        --remove-section=.comment \
        --remove-section=.note.gnu.build-id \
        "$BIN_SH"
else
    echo "build.sh: strip не найден — .comment останется в бинаре" >&2
fi

# set -e не прощает `grep -q ... && ...`: промах grep роняет скрипт, поэтому
# только if-формы.
FAILED=""
if [ -n "$READELF_TOOL" ]; then
    SECTIONS="$("$READELF_TOOL" -S "$BIN_SH" 2>/dev/null || true)"
    if printf '%s' "$SECTIONS" | grep -q '\.symtab'; then FAILED="$FAILED symtab"; fi
    if printf '%s' "$SECTIONS" | grep -q '\.comment'; then FAILED="$FAILED comment"; fi
fi
# Пути исходников: каталог сборки и абсолютный путь проекта.
for needle in "jni/src/" "$(pwd)/jni" "jni/Android.mk"; do
    if grep -a -q -- "$needle" "$BIN_SH"; then FAILED="$FAILED path:${needle}"; fi
done
if [ -n "$FAILED" ]; then
    echo "build.sh: в бинаре остались следы сборки:$FAILED" >&2
    exit 1
fi

# ---- пост-линковочное шифрование .rodata --------------------------------
# Делаем ПОСЛЕ readelf-проверок: elf_crypt.py вырезает таблицу секций, и
# llvm-strip/readelf после него уже бесполезны. Внутри — собственная проверка
# контрольных строк; любая уцелевшая открытая строка роняет сборку.
python3 tools/obf/elf_crypt.py "$BIN_SH" --key "$OBF_KEY" || exit 1
python3 tools/obf/elf_crypt.py "$BIN_SH" --check || exit 1

echo "build.sh: ok -> $(ls -l "libs/$ABI/benzware.sh")"
