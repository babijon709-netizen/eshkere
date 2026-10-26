#!/usr/bin/env bash
# Все jni/tests/*.cpp собираются и запускаются на хосте (x86_64, C++17).
# Ни Android, ни игры, ни драйвера не нужно. Падение любого теста = exit 1.
set -euo pipefail
cd "$(dirname "$0")/../.."

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

pass=0
fail=0
for src in jni/tests/test_*.cpp; do
    name="$(basename "$src" .cpp)"
    if ! "${CXX:-g++}" -std=c++17 -O1 -w \
            -Ijni/src -Ijni/include -Ijni/include/ImGui \
            "$src" -o "$tmp/$name" -lpthread -lm 2>"$tmp/$name.log"; then
        echo "BUILD FAIL: $name" >&2
        head -20 "$tmp/$name.log" >&2
        fail=$((fail + 1))
        continue
    fi
    if ! "$tmp/$name" >"$tmp/$name.out" 2>&1; then
        echo "RUN FAIL: $name" >&2
        cat "$tmp/$name.out" >&2
        fail=$((fail + 1))
        continue
    fi
    pass=$((pass + 1))
done

echo "host tests: pass=$pass fail=$fail"
[ "$fail" -eq 0 ]
