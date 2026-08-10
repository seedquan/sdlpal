#!/bin/bash
#
# Standalone unit-test runner for SDLPAL pure functions.
#
# Builds a plain gtest binary from palcommon.c + tests/test_rleblit.cpp,
# WITHOUT XCTest or the game host app (which requires game data). Use this
# for TDD on pure functions (RLE blit, sprite identity/hash, HD render helpers).
#
# Prereq: the macOS app must have been built once in Release so the SDL3
# framework headers exist:
#   xcodebuild -workspace macos/SDLPal.xcworkspace -scheme SDLPal \
#     -configuration Release -derivedDataPath build/macos_dd build
#
# Usage: tests/run-standalone.sh   (run from repo root)
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

FW="$ROOT/build/macos_dd/Build/Products/Release"
SDLH="$FW/SDL3.framework/Headers"
GT="$ROOT/3rd/googletest/googletest"

if [ ! -d "$SDLH" ]; then
  echo "ERROR: SDL3 framework headers not found at $SDLH" >&2
  echo "Build the Release app first (see header of this script)." >&2
  exit 2
fi

OUT="$(mktemp -d)"
INC=(-I"$ROOT" -I"$ROOT/sdl_compat" -I"$ROOT/macos" -I"$ROOT/3rd/SDL/include" -I"$SDLH" -F"$FW" -I"$ROOT/tools")
GTINC=(-I"$GT/include" -I"$GT")

echo "[1/6] compile palcommon.c"
clang -c "$ROOT/palcommon.c" -o "$OUT/palcommon.o" -DUSE_SDL3=1 -DHAVE_CONFIG_H "${INC[@]}"

echo "[2b/6] compile tools/hd_extract_core.c"
clang -c "$ROOT/tools/hd_extract_core.c" -o "$OUT/hd_extract_core.o" -DUSE_SDL3=1 -DHAVE_CONFIG_H "${INC[@]}"

echo "[2c/6] compile tools/hd_png.c"
clang -c "$ROOT/tools/hd_png.c" -o "$OUT/hd_png.o" -I"$ROOT/tools"

echo "[2d/6] compile stb_image reader impl TU"
printf '#define STB_IMAGE_IMPLEMENTATION\n#include "stb_image.h"\n' > "$OUT/stbi_impl.c"
clang -c "$OUT/stbi_impl.c" -o "$OUT/stbi_impl.o" -I"$ROOT"

echo "[2/6] compile sdl_compat.c + stubs"
clang -c "$ROOT/sdl_compat/sdl_compat.c" -o "$OUT/sdl_compat.o" -DUSE_SDL3=1 -DHAVE_CONFIG_H "${INC[@]}"
clang -c "$ROOT/tests/standalone-stubs.c" -o "$OUT/stubs.o" -DUSE_SDL3=1 -DHAVE_CONFIG_H "${INC[@]}"

echo "[3/6] compile tests/test_rleblit.cpp"
clang++ -std=c++14 -c "$ROOT/tests/test_rleblit.cpp" -o "$OUT/test_rleblit.o" \
  -DUSE_SDL3=1 -DHAVE_CONFIG_H "${INC[@]}" "${GTINC[@]}"

echo "[4/6] compile gtest"
clang++ -std=c++14 -c "$GT/src/gtest-all.cc" -o "$OUT/gtest-all.o" "${GTINC[@]}"

echo "[5/6] compile main shim"
printf '#include <gtest/gtest.h>\nint main(int argc,char**argv){testing::InitGoogleTest(&argc,argv);return RUN_ALL_TESTS();}\n' > "$OUT/tmain.cpp"
clang++ -std=c++14 -c "$OUT/tmain.cpp" -o "$OUT/tmain.o" "${GTINC[@]}"

echo "[6/6] link + run"
clang++ "$OUT/palcommon.o" "$OUT/hd_extract_core.o" "$OUT/hd_png.o" "$OUT/stbi_impl.o" \
  "$OUT/sdl_compat.o" "$OUT/stubs.o" "$OUT/test_rleblit.o" \
  "$OUT/gtest-all.o" "$OUT/tmain.o" \
  -o "$OUT/paltests" -F"$FW" -framework SDL3 -framework Cocoa -framework OpenGL
DYLD_FRAMEWORK_PATH="$FW" "$OUT/paltests" "$@"
