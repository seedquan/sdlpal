# HD Battle Backgrounds (FBP) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Battle backgrounds render in HD — an offline FBP extraction pipeline produces HD background images, and a new per-pixel-diff compositing mode swaps them in during battle.

**Architecture:** Extract each `FBP.MKF` chunk (a flat 320×200 bitmap) offline, colorize + AI-upscale, keyed by `PAL_HashBytes` of the decompressed bytes. At runtime, store the decompressed background + its hash at battle setup; in `VIDEO_HD_Present`, when an HD background matches, compose the base per pixel — HD background where the live screen still equals the stored background, upscaled placeholder where a sprite/UI/effect was drawn on top.

**Tech Stack:** C (matching codebase), the existing standalone extractor/harness tooling, `realesrgan-ncnn-vulkan`, GoogleTest via `tests/run-standalone.sh`.

## Global Constraints

- New identity hash `PAL_HashBytes` = FNV-1a 64 over a flat byte buffer (offset basis `14695981039346656037`, prime `1099511628211`). Both the extractor and the runtime hash the SAME decompressed 64000 bytes.
- FBP chunks are compressed: the extractor must link `yj1.c` and set the global `Decompress = YJ1_Decompress` (DOS data). `YJ1_Decompress`/`YJ2_Decompress` are in `yj1.c`, declared in `palcommon.h`.
- Output contract unchanged: `hd_assets/<016llx-hash>.png` (4× RGBA) + manifest entries; reuse `hdassets.c` (generic by hash) at runtime.
- v1 bakes palette 0 (day). Only the static background is HD; sprites/menus/damage-numbers/animated effects stay placeholder (they show through the "changed-pixel" path).
- When `gConfig.fHDRemaster` is OFF, output must be pixel-identical to before. Missing HD asset / not in battle → normal upscale base (graceful).
- Unit tests use synthetic data (no game data); running the extractor and the visual battle check use the user's own `~/PAL` data.
- HD plane is 1280×800 (`HD_W`/`HD_H`), `HD_SCALE=4`. An FBP background upscales to exactly `HD_W × HD_H`.
- Match existing C style (Linux/K&R braces, `LP*`/`UINT`/`BYTE`/`INT`/`BOOL`).
- App build: `xcodebuild -workspace macos/SDLPal.xcworkspace -scheme SDLPal -configuration Release -derivedDataPath build/macos_dd build`; unit tests: `./tests/run-standalone.sh`.

---

## File Structure

| File | Responsibility | Change |
|------|----------------|--------|
| `palcommon.h/.c` | `PAL_HashBytes`; `HD_PickBgPixel` (per-pixel base choice) | Modify |
| `tools/hd_extract_core.h/.c` | `HDX_RenderBitmap` (flat FBP index → RGBA) | Modify |
| `tools/hd_extract_fbp.c` | FBP extractor `main()` | Create |
| `tools/build-hd-extract-fbp.sh` | Standalone build (links `yj1.c`, sets `Decompress`) | Create |
| `battle.c` | Store background+hash+active at load; clear at battle end | Modify |
| `video.h` | Declare the battle-bg globals | Modify |
| `video.c` | Per-pixel-diff base in `VIDEO_HD_Present` | Modify |
| `tests/run-standalone.sh` / `tests/test_rleblit.cpp` | Compile deps + tests | Modify |

---

## Task 1: `PAL_HashBytes` — flat-buffer content hash

**Files:**
- Modify: `palcommon.h` (declare), `palcommon.c` (implement)
- Test: `tests/test_rleblit.cpp` (append)

**Interfaces:**
- Produces: `uint64_t PAL_HashBytes(const void *data, size_t len);` — FNV-1a 64 over `len` bytes; returns the offset basis unchanged for `len==0`; returns `0` if `data==NULL`.

- [ ] **Step 1: Write the failing test** (append to `tests/test_rleblit.cpp`)

```cpp
TEST(sdlpal, PAL_HashBytes) {
    uint8_t a[4] = { 1, 2, 3, 4 };
    uint8_t b[4] = { 1, 2, 3, 5 };
    EXPECT_EQ(PAL_HashBytes(a, 4), PAL_HashBytes(a, 4));  // stable
    EXPECT_NE(PAL_HashBytes(a, 4), PAL_HashBytes(b, 4));  // sensitive
    EXPECT_EQ(0ull, PAL_HashBytes(NULL, 4));              // NULL guard
    EXPECT_EQ(14695981039346656037ull, PAL_HashBytes(a, 0)); // empty = offset basis
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cd /Users/xiaolin/Projects/sdlpal && ./tests/run-standalone.sh 2>&1 | grep -E "PAL_HashBytes|error:"`
Expected: FAIL — undefined `PAL_HashBytes`.

- [ ] **Step 3: Declare in `palcommon.h`** (near `PAL_HashSprite`)

```c
uint64_t
PAL_HashBytes(
   const void   *data,
   size_t        len
);
```

- [ ] **Step 4: Implement in `palcommon.c`**

```c
uint64_t
PAL_HashBytes(
   const void   *data,
   size_t        len
)
{
   const uint64_t   FNV_OFFSET = 14695981039346656037ull;
   const uint64_t   FNV_PRIME  = 1099511628211ull;
   uint64_t         hash = FNV_OFFSET;
   const uint8_t   *p = (const uint8_t *)data;
   size_t           i;

   if (data == NULL)
   {
      return 0;
   }
   for (i = 0; i < len; i++)
   {
      hash ^= (uint64_t)p[i];
      hash *= FNV_PRIME;
   }
   return hash;
}
```

- [ ] **Step 5: Run to verify pass**

Run: `./tests/run-standalone.sh 2>&1 | tail -4` → all pass incl. `PAL_HashBytes`.

- [ ] **Step 6: Commit**

```bash
git add palcommon.h palcommon.c tests/test_rleblit.cpp
git commit -m "feat(hd-fbp): add PAL_HashBytes (FNV-1a over flat buffers)"
```

---

## Task 2: `HDX_RenderBitmap` — flat FBP index → RGBA

**Files:**
- Modify: `tools/hd_extract_core.h` (declare), `tools/hd_extract_core.c` (implement)
- Test: `tests/test_rleblit.cpp` (append; harness already compiles `hd_extract_core.c`)

**Interfaces:**
- Produces: `void HDX_RenderBitmap(const uint8_t *idx, const SDL_Color *palette, int w, int h, uint8_t *outRGBA);` — colorize `w*h` flat indices into RGBA8 (`[r,g,b,a]`, alpha 255, opaque). Caller provides `outRGBA` sized `w*h*4`.

- [ ] **Step 1: Write the failing test** (append)

```cpp
extern "C" {
    #include "hd_extract_core.h"
}
TEST(sdlpal, HDX_RenderBitmap) {
    SDL_Color pal[256];
    for (int i = 0; i < 256; i++) { pal[i].r = (uint8_t)i; pal[i].g = (uint8_t)(i+1); pal[i].b = (uint8_t)(i+2); pal[i].a = 255; }
    uint8_t idx[2 * 1] = { 5, 200 };
    uint8_t out[2 * 1 * 4];
    HDX_RenderBitmap(idx, pal, 2, 1, out);
    EXPECT_EQ(5,   out[0]);  EXPECT_EQ(6,   out[1]);  EXPECT_EQ(7,   out[2]);  EXPECT_EQ(255, out[3]);
    EXPECT_EQ(200, out[4]);  EXPECT_EQ(201, out[5]);  EXPECT_EQ(202, out[6]);  EXPECT_EQ(255, out[7]);
}
```
(If `hd_extract_core.h` is already `extern "C"`-included earlier in the file, don't duplicate the include block.)

- [ ] **Step 2: Run to verify failure**

Run: `./tests/run-standalone.sh 2>&1 | grep -E "HDX_RenderBitmap|error:"` → FAIL (undefined).

- [ ] **Step 3: Declare in `tools/hd_extract_core.h`**

```c
void HDX_RenderBitmap(const uint8_t *idx, const SDL_Color *palette, int w, int h, uint8_t *outRGBA);
```

- [ ] **Step 4: Implement in `tools/hd_extract_core.c`**

```c
void
HDX_RenderBitmap(
   const uint8_t   *idx,
   const SDL_Color *palette,
   int              w,
   int              h,
   uint8_t         *outRGBA
)
{
   int i, n = w * h;
   for (i = 0; i < n; i++)
   {
      SDL_Color c = palette[idx[i]];
      outRGBA[i * 4 + 0] = c.r;
      outRGBA[i * 4 + 1] = c.g;
      outRGBA[i * 4 + 2] = c.b;
      outRGBA[i * 4 + 3] = 255;
   }
}
```

- [ ] **Step 5: Run to verify pass** → `./tests/run-standalone.sh 2>&1 | tail -4` all pass.

- [ ] **Step 6: Commit**

```bash
git add tools/hd_extract_core.h tools/hd_extract_core.c tests/test_rleblit.cpp
git commit -m "feat(hd-fbp): add HDX_RenderBitmap (flat FBP index -> RGBA)"
```

---

## Task 3: FBP extractor `main` + build + run + upscale

**Files:**
- Create: `tools/hd_extract_fbp.c`, `tools/build-hd-extract-fbp.sh`

**Interfaces:**
- Produces the `tools/hd_extract_fbp` binary. CLI: `hd_extract_fbp <fbp.mkf> <pat.mkf> <out_dir>`. Writes `<out_dir>/src/<hash>.png` (320×200) per non-empty chunk + `<out_dir>/manifest.json`.

- [ ] **Step 1: Create `tools/hd_extract_fbp.c`**

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "palcommon.h"
#include "hd_extract_core.h"
#include "hd_png.h"

/* PAL_MKFDecompressChunk uses the global Decompress pointer; set it in main().
   YJ1_Decompress is declared in palcommon.h and defined in yj1.c. */

int
main(int argc, char *argv[])
{
   static uint8_t  chunk[320 * 200];
   static uint8_t  patbuf[1536];
   static uint8_t  rgba[320 * 200 * 4];
   SDL_Color       palette[256];
   FILE           *fpFBP, *fpPAT, *fpManifest;
   char            path[1024];
   int             count, i, n, first = 1;

   if (argc != 4)
   {
      fprintf(stderr, "usage: %s <fbp.mkf> <pat.mkf> <out_dir>\n", argv[0]);
      return 2;
   }

   Decompress = YJ1_Decompress;   /* DOS data */

   fpPAT = fopen(argv[2], "rb");
   if (!fpPAT) { fprintf(stderr, "cannot open %s\n", argv[2]); return 1; }
   n = PAL_MKFReadChunk(patbuf, sizeof(patbuf), 0, fpPAT);
   fclose(fpPAT);
   if (n < 256 * 3) { fprintf(stderr, "bad palette chunk (%d)\n", n); return 1; }
   HDX_ParsePalette(patbuf, n, palette);

   fpFBP = fopen(argv[1], "rb");
   if (!fpFBP) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
   count = PAL_MKFGetChunkCount(fpFBP);
   if (count <= 0) { fprintf(stderr, "no chunks in %s\n", argv[1]); return 1; }

   snprintf(path, sizeof(path), "%s/manifest.json", argv[3]);
   fpManifest = fopen(path, "w");
   if (!fpManifest) { fprintf(stderr, "cannot write manifest\n"); return 1; }
   fputs("[\n", fpManifest);

   for (i = 0; i < count; i++)
   {
      uint64_t hash;
      n = PAL_MKFDecompressChunk(chunk, sizeof(chunk), i, fpFBP);
      if (n != 320 * 200) continue;   /* not a full-screen background chunk */

      hash = PAL_HashBytes(chunk, 320 * 200);
      HDX_RenderBitmap(chunk, palette, 320, 200, rgba);

      snprintf(path, sizeof(path), "%s/src/%016llx.png", argv[3], (unsigned long long)hash);
      if (HDX_WritePNG(path, rgba, 320, 200) != 0) { fprintf(stderr, "warn: write %s\n", path); continue; }

      fprintf(fpManifest,
              "%s  { \"hash\": \"%016llx\", \"src\": \"src/%016llx.png\", "
              "\"w\": 320, \"h\": 200, \"hd_w\": 1280, \"hd_h\": 800, "
              "\"scale\": 4, \"palette\": 0, \"mkf\": \"fbp\", \"chunk\": %d }",
              first ? "" : ",\n",
              (unsigned long long)hash, (unsigned long long)hash, i);
      first = 0;
   }

   fputs("\n]\n", fpManifest);
   fclose(fpManifest);
   fclose(fpFBP);
   fprintf(stderr, "done: %d chunks scanned\n", count);
   return 0;
}
```

- [ ] **Step 2: Create `tools/build-hd-extract-fbp.sh`** (mirror `build-hd-extract.sh`, add `yj1.c`)

```bash
#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
FW="$ROOT/build/macos_dd/Build/Products/Release"
SDLH="$FW/SDL3.framework/Headers"
[ -d "$SDLH" ] || { echo "Build the Release app first (SDL3 headers missing)"; exit 2; }
OUT="$(mktemp -d)"; trap 'rm -rf "$OUT"' EXIT
INC=(-I"$ROOT" -I"$ROOT/tools" -I"$ROOT/sdl_compat" -I"$ROOT/macos" -I"$ROOT/3rd/SDL/include" -I"$SDLH" -F"$FW")
clang -c "$ROOT/palcommon.c"             -o "$OUT/palcommon.o"  -DUSE_SDL3=1 -DHAVE_CONFIG_H "${INC[@]}"
clang -c "$ROOT/yj1.c"                   -o "$OUT/yj1.o"        -DUSE_SDL3=1 -DHAVE_CONFIG_H "${INC[@]}"
clang -c "$ROOT/sdl_compat/sdl_compat.c" -o "$OUT/sdl_compat.o" -DUSE_SDL3=1 -DHAVE_CONFIG_H "${INC[@]}"
clang -c "$ROOT/tests/standalone-stubs.c" -o "$OUT/stubs.o"    -DUSE_SDL3=1 -DHAVE_CONFIG_H "${INC[@]}"
clang -c "$ROOT/tools/hd_extract_core.c" -o "$OUT/core.o"      -DUSE_SDL3=1 -DHAVE_CONFIG_H "${INC[@]}"
clang -c "$ROOT/tools/hd_png.c"          -o "$OUT/png.o"       "${INC[@]}"
clang -c "$ROOT/tools/hd_extract_fbp.c"  -o "$OUT/main.o"      -DUSE_SDL3=1 -DHAVE_CONFIG_H "${INC[@]}"
clang "$OUT"/*.o -o "$ROOT/tools/hd_extract_fbp" -F"$FW" -framework SDL3 -framework Cocoa -framework OpenGL -Wl,-rpath,"$FW"
echo "built tools/hd_extract_fbp"
```
Note: `tests/standalone-stubs.c` defines the global `Decompress` (as NULL); `main()` reassigns it to `YJ1_Decompress`, and `yj1.o` provides that symbol. No duplicate-definition conflict.

- [ ] **Step 3: Build the extractor**

```bash
cd /Users/xiaolin/Projects/sdlpal && chmod +x tools/build-hd-extract-fbp.sh && ./tools/build-hd-extract-fbp.sh
```
Expected: `built tools/hd_extract_fbp`.

- [ ] **Step 4: Run against real data + upscale**

```bash
mkdir -p ~/PAL/hd_build_fbp/src
cd /Users/xiaolin/Projects/sdlpal
./tools/hd_extract_fbp ~/PAL/fbp.mkf ~/PAL/pat.mkf ~/PAL/hd_build_fbp
echo "PNGs: $(ls ~/PAL/hd_build_fbp/src/*.png | wc -l)"
python3 -c "import json;print('entries', len(json.load(open('$HOME/PAL/hd_build_fbp/manifest.json'))))"
# upscale into the shared hd_assets dir (same model as portraits)
./tools/hd_upscale.sh ~/PAL/hd_build_fbp/src ~/PAL/hd_assets realesrgan-x4plus-anime
# merge the fbp manifest entries into the existing hd_assets manifest (append is fine; C only reads by filename)
echo "HD bg assets present: $(ls ~/PAL/hd_assets/*.png | wc -l)"
```
Expected: a handful-to-dozens of background PNGs (320×200), valid JSON, and 4× (1280×800) HD PNGs in `~/PAL/hd_assets/`. Open one HD background in Preview to confirm it is a recognizable, sharp battlefield image.

- [ ] **Step 5: Commit** (scripts only — NOT ~/PAL data)

```bash
git add tools/hd_extract_fbp.c tools/build-hd-extract-fbp.sh
git commit -m "feat(hd-fbp): FBP background extractor (decompress + hash + upscale to hd_assets)"
```

---

## Task 4: Runtime identity — store background + hash at battle setup

**Files:**
- Modify: `video.h` (declare globals), `video.c` (define globals), `battle.c` (set at load, clear at battle end)

**Interfaces:**
- Produces (globals, defined in `video.c`, declared `extern` in `video.h`):
  - `extern BOOL      g_hdBattleBgActive;`
  - `extern uint64_t  g_hdBattleBgHash;`
  - `extern BYTE      g_hdBattleBg[320 * 200];`

- [ ] **Step 1: Declare in `video.h`** (inside the C-linkage block)

```c
extern BOOL      g_hdBattleBgActive;
extern uint64_t  g_hdBattleBgHash;
extern BYTE      g_hdBattleBg[320 * 200];
```

- [ ] **Step 2: Define in `video.c`** (near the other HD globals, e.g. by `gpHDPixels`)

```c
BOOL      g_hdBattleBgActive = FALSE;
uint64_t  g_hdBattleBgHash   = 0;
BYTE      g_hdBattleBg[320 * 200];
```

- [ ] **Step 3: Set at background load — `battle.c`, in `PAL_LoadBattleBackground`**

Right after `PAL_MKFDecompressChunk(buf, 320 * 200, gpGlobals->wNumBattleField, gpGlobals->f.fpFBP);` (battle.c:988) and before/after `PAL_FBPBlitToSurface(buf, g_Battle.lpBackground);`, add:

```c
   //
   // HD: remember the decompressed battle background + its hash so VIDEO_HD_Present
   // can swap in an HD version. Cleared when the battle ends (PAL_StartBattle).
   //
   memcpy(g_hdBattleBg, buf, 320 * 200);
   g_hdBattleBgHash = PAL_HashBytes(buf, 320 * 200);
   g_hdBattleBgActive = TRUE;
```
Ensure `battle.c` sees the globals (`#include "video.h"`) and `PAL_HashBytes` (`palcommon.h`); both are already in the include chain — verify at build.

- [ ] **Step 4: Clear at battle end — `battle.c`, end of `PAL_StartBattle`**

Find `PAL_StartBattle` (the function that runs a whole battle and returns a `BATTLERESULT`). Immediately before each `return` that ends the battle (or at the single cleanup tail), add:

```c
   g_hdBattleBgActive = FALSE;
```
If `PAL_StartBattle` has one cleanup tail, put it there once. If it returns from multiple points, set it before the function's final `return` and also in any early-out that leaves the battle. (Verify by reading the function; the goal is: FALSE whenever we are not inside an active battle scene.)

- [ ] **Step 5: Build + no-crash battle run**

Run: `xcodebuild -workspace macos/SDLPal.xcworkspace -scheme SDLPal -configuration Release -derivedDataPath build/macos_dd build 2>&1 | tail -3` → `** BUILD SUCCEEDED **`.
Then deploy and confirm no crash entering/leaving a battle:
```bash
rm -rf ~/PAL/Pal.app && cp -R build/macos_dd/Build/Products/Release/Pal.app ~/PAL/Pal.app
```
(Reaching a battle needs play; the definitive visual check is Task 5 Step 6. Here just confirm the build and that `HDRemaster=1` still boots without crashing.)

- [ ] **Step 6: Commit**

```bash
git add video.h video.c battle.c
git commit -m "feat(hd-fbp): capture decompressed battle background + hash at setup, clear at battle end"
```

---

## Task 5: Per-pixel-diff compositing in `VIDEO_HD_Present`

**Files:**
- Modify: `palcommon.h/.c` (`HD_PickBgPixel`, testable), `video.c` (base compose)
- Test: `tests/test_rleblit.cpp` (append)

**Interfaces:**
- Produces: `uint32_t HD_PickBgPixel(BYTE liveIdx, BYTE bgIdx, uint32_t hdBgPx, uint32_t upscaledPx);` — returns `hdBgPx` when `liveIdx == bgIdx` (pixel still background), else `upscaledPx`.
- Consumes: `g_hdBattleBgActive`, `g_hdBattleBgHash`, `g_hdBattleBg` (Task 4); `HDAssets_Get`; `gpScreen`, `gpScreenReal`.

- [ ] **Step 1: Write the failing test** (append)

```cpp
TEST(sdlpal, HD_PickBgPixel) {
    uint32_t hd = 0xFFAABBCC, up = 0xFF112233;
    EXPECT_EQ(hd, HD_PickBgPixel(7, 7, hd, up));   // unchanged -> HD background
    EXPECT_EQ(up, HD_PickBgPixel(7, 9, hd, up));   // changed  -> upscaled placeholder
}
```

- [ ] **Step 2: Run to verify failure** → undefined `HD_PickBgPixel`.

- [ ] **Step 3: Declare + implement in `palcommon.h/.c`**

`palcommon.h`:
```c
uint32_t HD_PickBgPixel(BYTE liveIdx, BYTE bgIdx, uint32_t hdBgPx, uint32_t upscaledPx);
```
`palcommon.c`:
```c
uint32_t
HD_PickBgPixel(
   BYTE      liveIdx,
   BYTE      bgIdx,
   uint32_t  hdBgPx,
   uint32_t  upscaledPx
)
{
   return (liveIdx == bgIdx) ? hdBgPx : upscaledPx;
}
```

- [ ] **Step 4: Run to verify pass** → `./tests/run-standalone.sh 2>&1 | tail -4` all pass.

- [ ] **Step 5: Wire into `VIDEO_HD_Present` (`video.c`) base layer**

In the base-layer section (the `if (!g_hdOverlayOnly)` branch that upscales `gpScreenReal`), add an HD-battle-background variant. Before the base loop, resolve the HD background once:
```c
      const uint8_t *hdbg = NULL; INT hbw = 0, hbh = 0;
      BOOL useHdBg = FALSE;
      if (g_hdBattleBgActive &&
          HDAssets_Get(g_hdBattleBgHash, &hdbg, &hbw, &hbh) == 0 &&
          hbw == HD_W && hbh == HD_H)
      {
         useHdBg = TRUE;
      }
```
Then in the per-pixel base loop, when `useHdBg`, pick per pixel using the live 8-bit `gpScreen` index vs the stored background index:
```c
      {
         const uint8_t *liveIdxRow = (const uint8_t *)gpScreen->pixels;   /* 8-bit, pitch = gpScreen->pitch */
         INT liveStride = gpScreen->pitch;
         INT rowStride = gpScreenReal->pitch / (INT)sizeof(uint32_t);
         uint32_t *src = (uint32_t *)gpScreenReal->pixels;
         for (syi = 0; syi < HD_H; syi++)
         {
            INT sy = syi / HD_SCALE;
            for (sxi = 0; sxi < HD_W; sxi++)
            {
               INT sx = sxi / HD_SCALE;
               uint32_t up = src[sy * rowStride + sx] | 0xFF000000u;
               if (useHdBg)
               {
                  BYTE liveIdx = liveIdxRow[sy * liveStride + sx];
                  BYTE bgIdx   = g_hdBattleBg[sy * 320 + sx];
                  uint32_t hdpx = ((const uint32_t *)hdbg)[syi * HD_W + sxi];  /* hd asset is RGBA8 bytes; read as pixel */
                  gpHDPixels[syi * HD_W + sxi] = HD_PickBgPixel(liveIdx, bgIdx, hdpx, up);
               }
               else
               {
                  gpHDPixels[syi * HD_W + sxi] = up;
               }
            }
         }
      }
```
IMPORTANT byte-order: the HD asset from `HDAssets_Get` is RGBA8 in memory (`[r,g,b,a]`), while `gpHDPixels` is ARGB8888 (`0xAARRGGBB`). Convert when reading the HD background pixel:
```c
const uint8_t *pp = hdbg + (syi * HD_W + sxi) * 4;
uint32_t hdpx = ((uint32_t)pp[3] << 24) | ((uint32_t)pp[0] << 16) | ((uint32_t)pp[1] << 8) | (uint32_t)pp[2];
```
Use THIS `hdpx` (not the raw cast). Keep the existing lock/unlock of `gpScreenReal`; `gpScreen` is also 8-bit — lock it too if `SDL_MUSTLOCK(gpScreen)` before reading its pixels, and unlock after. Leave the `g_hdOverlayOnly == TRUE` branch and the sprite-overlay loop unchanged.

- [ ] **Step 6: Build + visual battle check**

Run the app build, deploy to `~/PAL`, set `HDRemaster=1`, enter a battle:
```bash
xcodebuild -workspace macos/SDLPal.xcworkspace -scheme SDLPal -configuration Release -derivedDataPath build/macos_dd build 2>&1 | tail -3
rm -rf ~/PAL/Pal.app && cp -R build/macos_dd/Build/Products/Release/Pal.app ~/PAL/Pal.app
printf 'HDRemaster=1\n' > ~/PAL/sdlpal.cfg
open ~/PAL/Pal.app
```
Expected (manual): in a battle, the **background is sharp/HD** while battle sprites, menus, and damage numbers stay at placeholder resolution; leaving the battle returns to normal; `HDRemaster=0` is unchanged. (Reaching a battle needs play — hand the final eyeball to the user if automation can't get there; report a screenshot path if it can.)

- [ ] **Step 7: Commit**

```bash
git add palcommon.h palcommon.c video.c tests/test_rleblit.cpp
git commit -m "feat(hd-fbp): per-pixel-diff HD battle background compositing in VIDEO_HD_Present"
```

---

## Self-Review

**Spec coverage:**
- §3.1 `PAL_HashBytes` → Task 1.
- §3.2 FBP extraction (decompress flat chunk, colorize, hash, upscale; yj1.c + Decompress) → Task 2 (`HDX_RenderBitmap`) + Task 3 (main/build/run/upscale).
- §3.3 runtime identity (store bg+hash+active, clear at battle end) → Task 4.
- §3.3 per-pixel-diff compositing → Task 5 (`HD_PickBgPixel` + `VIDEO_HD_Present`).
- §4 static-only / sprites-UI placeholder → Task 5 (changed pixels take the upscaled path).
- §5 palette 0 bake → Task 3 (`HDX_ParsePalette` on pat.mkf chunk 0).
- §6 reuse hdassets/output contract → Task 3 (upscale into `hd_assets/`), Task 5 (`HDAssets_Get`).
- §7 tests → Tasks 1,2,5 unit; Tasks 3,4,5 integration/visual.
- Zero-regression (HDRemaster off) → Task 5 keeps the classic/`useHdBg==FALSE` path identical.

**Placeholder scan:** No TBD/TODO; every code step is complete. The two human-judgment points (Task 3 Step 4 image eyeball, Task 5 Step 6 battle eyeball) are explicit and gated.

**Type consistency:** `PAL_HashBytes(const void*, size_t)->uint64_t`, `HDX_RenderBitmap(const uint8_t*, const SDL_Color*, int, int, uint8_t*)`, `HD_PickBgPixel(BYTE,BYTE,uint32_t,uint32_t)->uint32_t`, globals `g_hdBattleBgActive/Hash/g_hdBattleBg[320*200]` — identical across definition and use. Runtime hashes `PAL_HashBytes(buf,320*200)` (Task 4) exactly as the extractor hashes `PAL_HashBytes(chunk,320*200)` (Task 3) — same bytes, same key. HD asset read converts RGBA8→ARGB8888 consistently with the portrait overlay path.
