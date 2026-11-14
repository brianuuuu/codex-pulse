#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
CXX="${CXX:-x86_64-w64-mingw32-g++}"
WINDRES="${WINDRES:-x86_64-w64-mingw32-windres}"
mkdir -p dist
(cd assets && "$WINDRES" --preprocessor="$CXX" --preprocessor-arg=-E --preprocessor-arg=-xc --preprocessor-arg=-DRC_INVOKED -O coff app.rc ../dist/resources.o)
"$CXX" -std=c++17 -Os -s -static -static-libgcc -static-libstdc++ \
  -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 \
  -Wall -Wextra -Wpedantic -Wno-misleading-indentation \
  -municode -mwindows -Wl,--no-insert-timestamp -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va \
  src/main.cpp dist/resources.o -o dist/CodexPulse.exe \
  -lgdiplus -lwinhttp -lcomctl32 -lshell32 -lole32 -luuid -ladvapi32 -lbcrypt -ldwmapi -lgdi32 -luser32
