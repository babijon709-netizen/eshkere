#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

ndk_root="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
if [[ -n "$ndk_root" ]]; then
    ndk_build="$ndk_root/ndk-build"
else
    ndk_build="$(command -v ndk-build || true)"
fi
if [[ -z "$ndk_build" || ! -x "$ndk_build" ]]; then
    echo "Android NDK not found. Set ANDROID_NDK_HOME or add ndk-build to PATH." >&2
    exit 1
fi

"$ndk_build" -C "$PWD" -j"$(getconf _NPROCESSORS_ONLN)" "$@"
install -m 755 libs/arm64-v8a/xvcen libs/arm64-v8a/benzware.sh
