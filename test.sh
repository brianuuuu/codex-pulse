#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p dist
"${HOST_CXX:-g++}" -std=c++17 -O2 -Wall -Wextra tests/core_tests.cpp -o dist/core_tests
TZ=Asia/Shanghai dist/core_tests
TZ=America/New_York dist/core_tests
