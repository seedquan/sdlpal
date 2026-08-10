# HD Asset Pipeline (Sub-project B) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an offline pipeline that extracts RGM portrait sprites from the user's own game data, AI-upscales them 4× locally, and stores them as HD RGBA keyed by the same content hash sub-project A's runtime uses.

**Architecture:** A C extractor (reusing `palcommon.c`, so its `PAL_HashSprite` / decode match the runtime exactly) dumps each `RGM.MKF` chunk to an RGBA PNG colorized with palette 0, plus a `manifest.json` keyed by content hash. A shell stage runs `realesrgan-ncnn-vulkan` over those PNGs to produce `hd_assets/<hash>.png` at 4×. Model selection happens on a small sample before the full batch.

**Tech Stack:** C (compiled standalone like `tests/run-standalone.sh`), the vendored public-domain `stb_image_write.h`, GoogleTest via the existing standalone harness, and the `realesrgan-ncnn-vulkan` binary (Metal on Apple Silicon).

## Global Constraints

- **Local only.** Assets never leave the machine; no cloud upload. Personal-use; upscaled SoftStar art is not distributable.
- **Hash consistency is the contract.** The extractor MUST reuse `PAL_HashSprite` and `PAL_HDRenderSprite` from `palcommon.c` — do NOT reimplement hashing or RLE decoding. The extractor's hash for a sprite must equal `PAL_HashSprite(sameBytes)`.
- **No Xcode project changes.** The extractor builds standalone via a shell script mirroring `tests/run-standalone.sh` (include paths + SDL3 framework headers + `tests/standalone-stubs.c`).
- **Only new vendored dependency:** the single-header public-domain `stb_image_write.h`. No torch/python.
- **Bake palette 0 (day).** Portraits render under the standard palette; night colors ignored for this pass.
- **Output contract (for sub-project C):** `hd_assets/<16-hex-hash>.png` (4× RGBA) + `manifest.json` array of `{ "hash", "src", "w", "h", "hd_w", "hd_h", "scale":4, "palette":0, "mkf":"rgm", "chunk":<n> }`.
- **Model selection first:** run 2–3 models on ~8 sample portraits and pick before batching; xBRZ is the fallback if AI smears the art.
- **Unit tests** run via the standalone harness and must NOT require game data — use the synthetic `bitmap[]` sample already in `tests/test_rleblit.cpp`. Running the *extractor itself* uses the user's own `~/PAL` data (rgm.mkf + pat.mkf).
- Match existing C style (Linux/K&R braces, `LP*`/`UINT`/`BYTE` typedefs).

---

## File Structure

| File | Responsibility | Change |
|------|----------------|--------|
| `tools/hd_extract_core.h/.c` | Pure-ish core: `HDX_RenderSprite` (hash + decode→RGBA via A's funcs), `HDX_ParsePalette` | Create |
| `tools/hd_extract.c` | `main()`: open rgm/pat, loop chunks, write PNGs + manifest.json | Create |
| `tools/stb_image_write.h` | Vendored public-domain PNG writer | Create |
| `tools/build-hd-extract.sh` | Standalone compile of the extractor binary | Create |
| `tools/hd_upscale.sh` | Batch upscale via realesrgan-ncnn-vulkan | Create |
| `tests/run-standalone.sh` | Extend to also compile `tools/hd_extract_core.c` + `-Itools` | Modify |
| `tests/test_rleblit.cpp` | Append `HDX_*` unit tests | Modify |

---

## Task 1: Extractor core — hash + decode (the consistency lock)

**Files:**
- Create: `tools/hd_extract_core.h`, `tools/hd_extract_core.c`
- Modify: `tests/run-standalone.sh` (compile `tools/hd_extract_core.c`, add `-I<root>/tools`)
- Test: `tests/test_rleblit.cpp` (append two tests)

**Interfaces:**
- Produces:
  - `void HDX_ParsePalette(const uint8_t *buf, int len, SDL_Color out[256]);` — fills 256 colors from a `pat.mkf`-style chunk: `out[i].{r,g,b} = buf[i*3+{0,1,2}] << 2`, `a = 255`. `len` is accepted for future night-palette handling but day colors always come from offset 0.
  - `int HDX_RenderSprite(const uint8_t *rle, const SDL_Color *palette, uint64_t *outHash, uint8_t *outRGBA, int *outW, int *outH);` — sets `*outHash = PAL_HashSprite(rle)`; decodes+colorizes via `PAL_HDRenderSprite(rle, palette, 1, tmpArgb, outW, outH)`; converts ARGB8888→RGBA bytes into caller-provided `outRGBA` (must be ≥ `outW*outH*4`, bounded by 320*200*4). Returns 0 on success, -1 on NULL/decode failure.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_rleblit.cpp` (reuse the existing `static const BYTE bitmap[]`):

```cpp
extern "C" {
    #include "hd_extract_core.h"
}

TEST(sdlpal, HDX_HashMatchesRuntime) {
    SDL_Color pal[256];
    for (int i = 0; i < 256; i++) { pal[i].r = i; pal[i].g = i; pal[i].b = i; pal[i].a = 255; }
    static uint8_t rgba[320 * 200 * 4];
    uint64_t hash = 0; int w = 0, h = 0;
    ASSERT_EQ(0, HDX_RenderSprite(bitmap, pal, &hash, rgba, &w, &h));
    // Extractor hash MUST equal the runtime hash for the same bytes.
    EXPECT_EQ(PAL_HashSprite(bitmap), hash);
    EXPECT_EQ((int)(bitmap[0] | (bitmap[1] << 8)), w);
    EXPECT_EQ((int)(bitmap[2] | (bitmap[3] << 8)), h);
    EXPECT_EQ(-1, HDX_RenderSprite(NULL, pal, &hash, rgba, &w, &h));
}

TEST(sdlpal, HDX_ParsePaletteScales) {
    uint8_t buf[768];
    for (int i = 0; i < 768; i++) buf[i] = (uint8_t)(i & 0x3F);
    SDL_Color out[256];
    HDX_ParsePalette(buf, 768, out);
    EXPECT_EQ((uint8_t)(buf[0] << 2), out[0].r);
    EXPECT_EQ((uint8_t)(buf[3 * 5 + 1] << 2), out[5].g);
    EXPECT_EQ((uint8_t)(buf[3 * 7 + 2] << 2), out[7].b);
    EXPECT_EQ(255, out[10].a);
}
```

- [ ] **Step 2: Extend the harness, run, verify failure**

Edit `tests/run-standalone.sh`: add `-I"$ROOT/tools"` to the `INC=(...)` array, and add a compile+link of the core. In the compile section add:
```bash
echo "[2b/6] compile tools/hd_extract_core.c"
clang -c "$ROOT/tools/hd_extract_core.c" -o "$OUT/hd_extract_core.o" -DUSE_SDL3=1 -DHAVE_CONFIG_H "${INC[@]}"
```
and add `"$OUT/hd_extract_core.o"` to the final `clang++ ... link` object list.

Run: `cd /Users/xiaolin/Projects/sdlpal && ./tests/run-standalone.sh`
Expected: FAIL — `hd_extract_core.h` not found / undefined `HDX_*`.

- [ ] **Step 3: Create `tools/hd_extract_core.h`**

```c
#ifndef HD_EXTRACT_CORE_H
#define HD_EXTRACT_CORE_H

#include <stdint.h>
#include "palcommon.h"   /* SDL_Color, LPCBITMAPRLE, PAL_HashSprite, PAL_HDRenderSprite */

#ifdef __cplusplus
extern "C" {
#endif

void HDX_ParsePalette(const uint8_t *buf, int len, SDL_Color out[256]);

int  HDX_RenderSprite(const uint8_t *rle, const SDL_Color *palette,
                      uint64_t *outHash, uint8_t *outRGBA, int *outW, int *outH);

#ifdef __cplusplus
}
#endif

#endif
```

- [ ] **Step 4: Create `tools/hd_extract_core.c`**

```c
#include "hd_extract_core.h"

void
HDX_ParsePalette(
   const uint8_t *buf,
   int            len,
   SDL_Color      out[256]
)
{
   int i;
   (void)len;
   for (i = 0; i < 256; i++)
   {
      out[i].r = (uint8_t)(buf[i * 3 + 0] << 2);
      out[i].g = (uint8_t)(buf[i * 3 + 1] << 2);
      out[i].b = (uint8_t)(buf[i * 3 + 2] << 2);
      out[i].a = 255;
   }
}

int
HDX_RenderSprite(
   const uint8_t   *rle,
   const SDL_Color *palette,
   uint64_t        *outHash,
   uint8_t         *outRGBA,
   int             *outW,
   int             *outH
)
{
   static uint32_t argb[320 * 200];
   int w = 0, h = 0, i, n;

   if (rle == NULL || palette == NULL || outHash == NULL ||
       outRGBA == NULL || outW == NULL || outH == NULL)
   {
      return -1;
   }

   *outHash = PAL_HashSprite((LPCBITMAPRLE)rle);

   if (PAL_HDRenderSprite((LPCBITMAPRLE)rle, palette, 1, argb, &w, &h) != 0)
   {
      return -1;
   }

   n = w * h;
   for (i = 0; i < n; i++)
   {
      uint32_t p = argb[i];
      outRGBA[i * 4 + 0] = (uint8_t)((p >> 16) & 0xFF); /* R */
      outRGBA[i * 4 + 1] = (uint8_t)((p >> 8) & 0xFF);  /* G */
      outRGBA[i * 4 + 2] = (uint8_t)(p & 0xFF);         /* B */
      outRGBA[i * 4 + 3] = (uint8_t)((p >> 24) & 0xFF); /* A */
   }
   *outW = w;
   *outH = h;
   return 0;
}
```

- [ ] **Step 5: Run tests to verify pass**

Run: `cd /Users/xiaolin/Projects/sdlpal && ./tests/run-standalone.sh 2>&1 | tail -6`
Expected: PASS — all prior tests + `HDX_HashMatchesRuntime` + `HDX_ParsePaletteScales`.

- [ ] **Step 6: Commit**

```bash
git add tools/hd_extract_core.h tools/hd_extract_core.c tests/run-standalone.sh tests/test_rleblit.cpp
git commit -m "feat(hd-pipeline): extractor core reusing PAL_HashSprite/PAL_HDRenderSprite for hash consistency"
```

---

## Task 2: Vendored PNG writer + write helper

**Files:**
- Create: `tools/stb_image_write.h` (public-domain single header)
- Create: `tools/hd_png.h`, `tools/hd_png.c` (thin `HDX_WritePNG` wrapper)
- Modify: `tests/run-standalone.sh` (compile `tools/hd_png.c`)
- Test: `tests/test_rleblit.cpp` (append a round-trip test using the repo's existing `stb_image.h` reader)

**Interfaces:**
- Produces: `int HDX_WritePNG(const char *path, const uint8_t *rgba, int w, int h);` — writes an RGBA8 PNG via `stbi_write_png`. Returns 0 on success, -1 on failure.

- [ ] **Step 1: Add `tools/stb_image_write.h`**

Download the canonical public-domain single-header writer (Sean Barrett / nothings, `stb_image_write.h`, ~v1.16):
```bash
cd /Users/xiaolin/Projects/sdlpal
curl -fsSL https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h -o tools/stb_image_write.h
head -20 tools/stb_image_write.h   # confirm it is the stb public-domain header
```
Expected: file begins with the `stb_image_write - v1.xx` banner and public-domain dedication.

- [ ] **Step 2: Write the failing round-trip test**

Append to `tests/test_rleblit.cpp` (the repo already vendors `stb_image.h` for reading):

```cpp
extern "C" {
    #include "hd_png.h"
}
// stb_image.h reader is already in the repo root; declare the one function we use.
extern "C" unsigned char *stbi_load(const char *filename, int *x, int *y, int *comp, int req_comp);
extern "C" void stbi_image_free(void *retval_from_stbi_load);

TEST(sdlpal, HDX_WritePNGRoundTrip) {
    // 2x1 RGBA: pixel0 = (10,20,30,255), pixel1 = (40,50,60,128)
    uint8_t px[2 * 1 * 4] = { 10,20,30,255,  40,50,60,128 };
    const char *path = "/tmp/hdx_test.png";
    ASSERT_EQ(0, HDX_WritePNG(path, px, 2, 1));
    int x = 0, y = 0, comp = 0;
    unsigned char *img = stbi_load(path, &x, &y, &comp, 4);
    ASSERT_NE((unsigned char *)NULL, img);
    EXPECT_EQ(2, x); EXPECT_EQ(1, y);
    EXPECT_EQ(10, img[0]); EXPECT_EQ(20, img[1]); EXPECT_EQ(30, img[2]); EXPECT_EQ(255, img[3]);
    EXPECT_EQ(128, img[7]);
    stbi_image_free(img);
}
```

For the reader symbol, the harness must compile `stb_image.h`'s implementation once. Add to `tests/run-standalone.sh` a tiny impl TU (see Step 4).

- [ ] **Step 3: Create `tools/hd_png.h` and `tools/hd_png.c`**

`tools/hd_png.h`:
```c
#ifndef HD_PNG_H
#define HD_PNG_H
#ifdef __cplusplus
extern "C" {
#endif
int HDX_WritePNG(const char *path, const unsigned char *rgba, int w, int h);
#ifdef __cplusplus
}
#endif
#endif
```

`tools/hd_png.c`:
```c
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "hd_png.h"

int
HDX_WritePNG(
   const char          *path,
   const unsigned char *rgba,
   int                  w,
   int                  h
)
{
   /* stride = w*4 for tightly packed RGBA */
   return stbi_write_png(path, w, h, 4, rgba, w * 4) ? 0 : -1;
}
```

- [ ] **Step 4: Extend harness to compile the writer + a reader impl TU, run, verify pass**

In `tests/run-standalone.sh`:
- Add `clang -c "$ROOT/tools/hd_png.c" -o "$OUT/hd_png.o" -I"$ROOT/tools"` and link `"$OUT/hd_png.o"`.
- Add a reader implementation TU so `stbi_load` links (the repo root has `stb_image.h`):
```bash
printf '#define STB_IMAGE_IMPLEMENTATION\n#include "stb_image.h"\n' > "$OUT/stbi_impl.c"
clang -c "$OUT/stbi_impl.c" -o "$OUT/stbi_impl.o" -I"$ROOT"
```
and link `"$OUT/stbi_impl.o"`.

Run: `cd /Users/xiaolin/Projects/sdlpal && ./tests/run-standalone.sh 2>&1 | tail -6`
Expected: PASS including `HDX_WritePNGRoundTrip`.

- [ ] **Step 5: Commit**

```bash
git add tools/stb_image_write.h tools/hd_png.h tools/hd_png.c tests/run-standalone.sh tests/test_rleblit.cpp
git commit -m "feat(hd-pipeline): vendor stb_image_write and add HDX_WritePNG round-trip test"
```

---

## Task 3: Extractor main + build script (run against real data)

**Files:**
- Create: `tools/hd_extract.c` (`main()`)
- Create: `tools/build-hd-extract.sh`
- Depends on: Task 1 (`hd_extract_core`), Task 2 (`hd_png`), and `palcommon.c` + `tests/standalone-stubs.c`.

**Interfaces:**
- Produces the `tools/hd_extract` binary. CLI: `hd_extract <rgm.mkf> <pat.mkf> <out_dir>`. Writes `<out_dir>/src/<hash>.png` for each non-empty chunk and `<out_dir>/manifest.json`.

- [ ] **Step 1: Create `tools/hd_extract.c`**

Uses `PAL_MKFGetChunkCount` / `PAL_MKFReadChunk` (in `palcommon.c`) to read raw RGM chunks (same call the runtime uses at `uigame.c:1132`), reads palette 0 from `pat.mkf` chunk 0, and writes PNG + manifest.

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "palcommon.h"
#include "hd_extract_core.h"
#include "hd_png.h"

/* PAL_MKFReadChunk / PAL_MKFGetChunkCount are declared in palcommon.h */

int
main(int argc, char *argv[])
{
   static uint8_t   chunk[PAL_RLEBUFSIZE];
   static uint8_t   patbuf[1536];
   static uint8_t   rgba[320 * 200 * 4];
   SDL_Color        palette[256];
   FILE            *fpRGM, *fpPAT, *fpManifest;
   char             path[1024];
   int              count, i, n, first = 1;

   if (argc != 4)
   {
      fprintf(stderr, "usage: %s <rgm.mkf> <pat.mkf> <out_dir>\n", argv[0]);
      return 2;
   }

   fpPAT = fopen(argv[2], "rb");
   if (!fpPAT) { fprintf(stderr, "cannot open %s\n", argv[2]); return 1; }
   n = PAL_MKFReadChunk(patbuf, sizeof(patbuf), 0, fpPAT);
   fclose(fpPAT);
   if (n < 256 * 3) { fprintf(stderr, "bad palette chunk (%d)\n", n); return 1; }
   HDX_ParsePalette(patbuf, n, palette);

   fpRGM = fopen(argv[1], "rb");
   if (!fpRGM) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
   count = PAL_MKFGetChunkCount(fpRGM);
   if (count <= 0) { fprintf(stderr, "no chunks in %s\n", argv[1]); return 1; }

   snprintf(path, sizeof(path), "%s/manifest.json", argv[3]);
   fpManifest = fopen(path, "w");
   if (!fpManifest) { fprintf(stderr, "cannot write manifest\n"); return 1; }
   fputs("[\n", fpManifest);

   for (i = 0; i < count; i++)
   {
      uint64_t hash;
      int w = 0, h = 0, len;

      len = PAL_MKFReadChunk(chunk, sizeof(chunk), i, fpRGM);
      if (len < 4) continue;   /* empty / non-sprite chunk */

      if (HDX_RenderSprite(chunk, palette, &hash, rgba, &w, &h) != 0) continue;
      if (w <= 0 || h <= 0) continue;

      snprintf(path, sizeof(path), "%s/src/%016llx.png", argv[3],
               (unsigned long long)hash);
      if (HDX_WritePNG(path, rgba, w, h) != 0) continue;

      fprintf(fpManifest,
              "%s  { \"hash\": \"%016llx\", \"src\": \"src/%016llx.png\", "
              "\"w\": %d, \"h\": %d, \"hd_w\": %d, \"hd_h\": %d, "
              "\"scale\": 4, \"palette\": 0, \"mkf\": \"rgm\", \"chunk\": %d }",
              first ? "" : ",\n",
              (unsigned long long)hash, (unsigned long long)hash,
              w, h, w * 4, h * 4, i);
      first = 0;
   }

   fputs("\n]\n", fpManifest);
   fclose(fpManifest);
   fclose(fpRGM);
   fprintf(stderr, "done: %d chunks scanned\n", count);
   return 0;
}
```

- [ ] **Step 2: Create `tools/build-hd-extract.sh`**

Mirror `tests/run-standalone.sh`'s include/framework/stub setup (no test/gtest).

```bash
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
clang "$OUT"/*.o -o "$ROOT/tools/hd_extract" -F"$FW" -framework SDL3 -framework Cocoa -framework OpenGL
echo "built tools/hd_extract"
```

- [ ] **Step 3: Build the extractor**

Run:
```bash
cd /Users/xiaolin/Projects/sdlpal && chmod +x tools/build-hd-extract.sh && ./tools/build-hd-extract.sh
```
Expected: `built tools/hd_extract` (uses the already-built Release SDL3 framework; if missing, build the app first per Global Constraints).

- [ ] **Step 4: Run against the user's real data**

```bash
mkdir -p ~/PAL/hd_build/src
cd /Users/xiaolin/Projects/sdlpal
./tools/hd_extract ~/PAL/rgm.mkf ~/PAL/pat.mkf ~/PAL/hd_build
echo "=== PNG count ==="; ls ~/PAL/hd_build/src/*.png | wc -l
echo "=== manifest head ==="; head -5 ~/PAL/hd_build/manifest.json
echo "=== spot-check one PNG opens ==="; ls -la ~/PAL/hd_build/src | head -3
```
Expected: a plausible number of portrait PNGs (dozens), a well-formed `manifest.json` (valid JSON array — verify with `python3 -c "import json;json.load(open('$HOME/PAL/hd_build/manifest.json'))" && echo JSON_OK`), and PNGs that open. Open a couple in Preview to confirm they are recognizable portraits with clean transparency.

- [ ] **Step 5: Commit**

```bash
git add tools/hd_extract.c tools/build-hd-extract.sh
git commit -m "feat(hd-pipeline): extractor main + standalone build; dumps RGM portraits to PNG + manifest"
```
(Do NOT commit anything under `~/PAL` — that is game data outside the repo.)

---

## Task 4: Install upscaler + model-selection spike

**Files:**
- Create: `tools/hd_upscale.sh`

- [ ] **Step 1: Install `realesrgan-ncnn-vulkan`**

No Homebrew formula exists. Download the official macOS release binary + bundled models:
```bash
mkdir -p ~/tools/realesrgan && cd ~/tools/realesrgan
curl -fsSL -o realesrgan.zip https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan/releases/download/v0.2.0/realesrgan-ncnn-vulkan-20220424-macos.zip
unzip -o realesrgan.zip
chmod +x realesrgan-ncnn-vulkan
xattr -dr com.apple.quarantine . 2>/dev/null || true
./realesrgan-ncnn-vulkan -h 2>&1 | head -5   # confirm it runs (prints usage)
```
Expected: usage text (confirms the Metal binary runs). If Gatekeeper blocks it, the `xattr` clears quarantine. Note the models dir (`models/`) with `realesrgan-x4plus`, `realesrgan-x4plus-anime`, `realesr-animevideov3`.

- [ ] **Step 2: Create `tools/hd_upscale.sh`**

```bash
#!/bin/bash
# Usage: hd_upscale.sh <src_dir> <out_dir> <model> [realesrgan_dir]
# Upscales every PNG in <src_dir> 4x into <out_dir>/<samebasename>.png
set -euo pipefail
SRC="$1"; OUT="$2"; MODEL="${3:-realesrgan-x4plus-anime}"
RE="${4:-$HOME/tools/realesrgan}"
BIN="$RE/realesrgan-ncnn-vulkan"
mkdir -p "$OUT"
[ -x "$BIN" ] || { echo "realesrgan not found at $BIN"; exit 2; }
n=0
for f in "$SRC"/*.png; do
  [ -e "$f" ] || continue
  base="$(basename "$f")"
  "$BIN" -i "$f" -o "$OUT/$base" -s 4 -n "$MODEL" -m "$RE/models" -f png >/dev/null 2>&1
  n=$((n+1))
done
echo "upscaled $n file(s) with model $MODEL -> $OUT"
```

- [ ] **Step 3: Produce the comparison sample**

Pick ~8 representative portraits and run each candidate model into its own folder:
```bash
mkdir -p ~/PAL/hd_build/sample
# take 8 source PNGs as the sample set
ls ~/PAL/hd_build/src/*.png | head -8 | xargs -I{} cp {} ~/PAL/hd_build/sample/
cd /Users/xiaolin/Projects/sdlpal && chmod +x tools/hd_upscale.sh
for M in realesrgan-x4plus-anime realesr-animevideov3 realesrgan-x4plus; do
  ./tools/hd_upscale.sh ~/PAL/hd_build/sample ~/PAL/hd_build/cmp/$M "$M"
done
echo "=== compare these folders in Preview ==="; ls -d ~/PAL/hd_build/cmp/*
open ~/PAL/hd_build/cmp/realesrgan-x4plus-anime 2>/dev/null || true
```
Expected: three folders of 4× results. **STOP here for a human decision:** open the folders side by side, judge which model best preserves the portrait style with clean edges/alpha (no white halos). Record the chosen model name. If all three smear the art badly, the fallback is xBRZ (note it and escalate — do not batch).

- [ ] **Step 4: Commit the scripts (not the images)**

```bash
git add tools/hd_upscale.sh
git commit -m "feat(hd-pipeline): add realesrgan upscale script + model-selection sample harness"
```

---

## Task 5: Batch upscale + wrap-up

**Files:** none new (uses `tools/hd_upscale.sh`).

- [ ] **Step 1: Batch the chosen model over all portraits**

Using the model chosen in Task 4 (substitute `<MODEL>`):
```bash
cd /Users/xiaolin/Projects/sdlpal
./tools/hd_upscale.sh ~/PAL/hd_build/src ~/PAL/hd_assets_tmp "<MODEL>"
```

- [ ] **Step 2: Rename outputs to the hash contract and place next to game data**

The upscaler keeps the source basename (already `<hash>.png`), so the batch output is already hash-named. Move into the contract location:
```bash
mkdir -p ~/PAL/hd_assets
cp ~/PAL/hd_assets_tmp/*.png ~/PAL/hd_assets/
cp ~/PAL/hd_build/manifest.json ~/PAL/hd_assets/manifest.json
echo "=== assets ==="; ls ~/PAL/hd_assets/*.png | wc -l
echo "=== manifest entries ==="; python3 -c "import json;print(len(json.load(open('$HOME/PAL/hd_assets/manifest.json'))))"
```
Expected: the number of HD PNGs equals (or closely matches) the manifest entry count. Every HD PNG is `hd_assets/<hash>.png` at 4× the source dimensions.

- [ ] **Step 3: Verify one asset end-to-end**

```bash
# Confirm an HD asset is 4x its manifest w/h
python3 - <<'PY'
import json, os, struct
m = json.load(open(os.path.expanduser('~/PAL/hd_assets/manifest.json')))
e = m[0]
p = os.path.expanduser('~/PAL/hd_assets/%s.png' % e['hash'])
with open(p,'rb') as f:
    f.read(16); w,h = struct.unpack('>II', f.read(8))
print('hash',e['hash'],'src',e['w'],'x',e['h'],'hd',w,'x',h,'expect',e['w']*4,e['h']*4)
assert w==e['w']*4 and h==e['h']*4, "dimension mismatch"
print("OK")
PY
```
Expected: `OK` — HD dimensions are exactly 4× the source recorded in the manifest, and the file is named by the same hash the runtime computes.

- [ ] **Step 4: Final doc note + commit**

Append a short "sub-project B complete" note to the spec's status and record where the generated assets live (only in `~/PAL/hd_assets`, never committed).
```bash
git add docs/superpowers/specs/2026-08-10-hd-asset-pipeline-design.md
git commit -m "docs(hd-pipeline): mark sub-project B complete; HD portrait assets generated locally"
```

---

## Self-Review

**Spec coverage:**
- §3 two-stage pipeline → Tasks 1–3 (extractor), 4–5 (upscale).
- §4 hash consistency (reuse palcommon.c) → Task 1 (`HDX_RenderSprite` uses `PAL_HashSprite`; test asserts equality with runtime).
- §5 palette-0 bake → Task 1 `HDX_ParsePalette` + Task 3 reading `pat.mkf` chunk 0.
- §6 transparency → alpha carried through RGBA in Task 1; clean-edge check in Task 4 Step 3.
- §7 output contract → Task 3 manifest + Task 5 `hd_assets/<hash>.png`.
- §8 model selection first → Task 4.
- §9 scope (RGM only, no C runtime, no Xcode) → standalone build (Task 3), no engine/project edits.
- §10 success criteria → Task 1 (hash test), Task 3 (full run + manifest), Task 4/5 (upscaled clean, dimension check).
- §11 tests via harness with synthetic sample → Tasks 1–2 use `bitmap[]`, no game data.
- §12 files → all created as listed; §13.4 stb_image_write vendored (Task 2).

**Placeholder scan:** No TBD/TODO; every code step has complete code; the one human-judgment point (model selection, Task 4 Step 3) is explicit and gated, not a placeholder.

**Type consistency:** `HDX_RenderSprite` / `HDX_ParsePalette` / `HDX_WritePNG` signatures identical across Tasks 1–3. `HDX_RenderSprite` consumes `PAL_HashSprite` + `PAL_HDRenderSprite` (sub-project A, on master). Manifest field names match spec §7. The extractor reads RGM via `PAL_MKFReadChunk` — the same call the runtime uses — so hashed bytes are identical.
