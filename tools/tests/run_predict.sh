#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -O2 \
    -Ijni/src -Ijni/include tools/tests/predict_test.cpp -o "$tmp/predict_test"
"$tmp/predict_test"
