#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
FW="$ROOT/build/macos_dd/Build/Products/Release"
SDLH="$FW/SDL3.framework/Headers"
[ -d "$SDLH" ] || { echo "Build the Release app first (SDL3 headers missing)"; exit 2; }
OUT="$(mktemp -d)"
INC=(-I"$ROOT" -I"$ROOT/tools" -I"$ROOT/sdl_compat" -I"$ROOT/macos" -I"$ROOT/3rd/SDL/include" -I"$SDLH" -F"$FW")
clang -c "$ROOT/palcommon.c"           -o "$OUT/palcommon.o"  -DUSE_SDL3=1 -DHAVE_CONFIG_H "${INC[@]}"
clang -c "$ROOT/sdl_compat/sdl_compat.c" -o "$OUT/sdl_compat.o" -DUSE_SDL3=1 -DHAVE_CONFIG_H "${INC[@]}"
clang -c "$ROOT/tests/standalone-stubs.c" -o "$OUT/stubs.o"   -DUSE_SDL3=1 -DHAVE_CONFIG_H "${INC[@]}"
clang -c "$ROOT/tools/hd_extract_core.c" -o "$OUT/core.o"     -DUSE_SDL3=1 -DHAVE_CONFIG_H "${INC[@]}"
clang -c "$ROOT/tools/hd_png.c"          -o "$OUT/png.o"      "${INC[@]}"
clang -c "$ROOT/tools/hd_extract.c"      -o "$OUT/main.o"     -DUSE_SDL3=1 -DHAVE_CONFIG_H "${INC[@]}"
clang "$OUT"/*.o -o "$ROOT/tools/hd_extract" -F"$FW" -framework SDL3 -framework Cocoa -framework OpenGL -Wl,-rpath,"$FW"
echo "built tools/hd_extract"
