# HD Asset Runtime (Sub-project C) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the HD portrait assets from sub-project B actually render in-game — a hash→RGBA cache plus one lookup in the HD overlay path that blits the HD asset when present, else the existing nearest-neighbor placeholder.

**Architecture:** A new `hdassets.c/.h` module provides a lazy hash→RGBA cache (`stbi_load` on first hit, negative-cached misses) and a palette-equality gate. `video.c`'s `VIDEO_HD_Present` computes the gate once per frame and, for each plain overlay command, blits the HD asset (packing RGBA8→ARGB) when the gate passes and the asset exists, else falls back to `PAL_HDRenderSprite`. `hdassets.c` is folded into `video.c` via a unity `#include` (no Xcode project change); the standalone harness compiles it separately for unit tests.

**Tech Stack:** C (matching the codebase), stb_image (already vendored/used at runtime), GoogleTest via the existing `tests/run-standalone.sh` harness.

## Global Constraints

- Reuse `gConfig.fHDRemaster` as the master switch. v1 HARDCODES the asset dir `"hd_assets"` (relative to the game's working dir) — NO new config option, NO `palcfg.c/.h` change.
- `hdassets.c` must NOT depend on `pat.mkf`/`PAL_GetPalette`: the reference palette is passed INTO `HDAssets_Init(dir, refPalette)` by the caller. `hdassets.c` depends only on `stbi_load`/`stbi_image_free` (declared extern), `SDL_Color`, and stdlib/string.
- Palette gate compares R,G,B only (ignore alpha), 256 entries.
- HD assets are 4× RGBA8 (`[r,g,b,a]` byte order from `stbi_load(...,4)`); blit packs to `0xAARRGGBB` and draws only pixels with `a != 0`, at `cmd.x*HD_SCALE / cmd.y*HD_SCALE`.
- Zero Xcode project changes: `video.c` gets `#include "hdassets.c"` at the bottom (unity build); `hdassets.c` is NOT added to the Pal target. The harness compiles `hdassets.c` as its own TU.
- When `fHDRemaster` is OFF, output must be pixel-identical to before (zero regression). Missing asset dir / missing PNG → cache miss → placeholder (graceful degradation).
- Unit tests use synthetic PNGs written by sub-project B's `HDX_WritePNG` — NO game data. Integration/visual verification uses the user's own `~/PAL` data.
- Match existing C style (Linux/K&R braces, `LP*`/`UINT`/`BYTE`/`INT`/`BOOL` typedefs).
- App build: `xcodebuild -workspace macos/SDLPal.xcworkspace -scheme SDLPal -configuration Release -derivedDataPath build/macos_dd build`
- Unit tests: `./tests/run-standalone.sh`
- Manual run: copy the fresh `Pal.app` into `~/PAL/` and launch there (game data + `~/PAL/hd_assets` present).

---

## File Structure

| File | Responsibility | Change |
|------|----------------|--------|
| `hdassets.h` | Declarations: Init/Free/Get/PaletteMatchesReference | Create |
| `hdassets.c` | Lazy hash→RGBA cache + palette gate | Create |
| `video.c` | Unity-include hdassets.c; per-frame gate; overlay lookup; Init/Free | Modify |
| `tests/run-standalone.sh` | Compile `hdassets.c` into the harness | Modify |
| `tests/test_rleblit.cpp` | Append hdassets unit tests | Modify |

---

## Task 1: `hdassets` module — cache, lookup, palette gate

**Files:**
- Create: `hdassets.h`, `hdassets.c`
- Modify: `tests/run-standalone.sh` (compile `hdassets.c`, link its object)
- Test: `tests/test_rleblit.cpp` (append two tests)

**Interfaces:**
- Produces:
  - `void HDAssets_Init(const char *dir, const SDL_Color *refPalette);` — sets the asset dir (copy string; default `"hd_assets"` if `dir` NULL) and copies 256 reference colors; clears any existing cache.
  - `void HDAssets_Free(void);` — frees all cached RGBA buffers (via `stbi_image_free`), resets count.
  - `INT HDAssets_Get(uint64_t hash, const uint8_t **outRGBA, INT *outW, INT *outH);` — returns 0 and sets outputs to the cached 4× RGBA8 + dims on hit; returns -1 on miss. Lazily `stbi_load`s `<dir>/<016llx>.png` (forced 4 channels) on first request for a hash; caches result including misses (negative cache) so a hash touches disk at most once.
  - `BOOL HDAssets_PaletteMatchesReference(const SDL_Color *live);` — TRUE iff all 256 R/G/B match the reference; FALSE on NULL.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_rleblit.cpp` (harness already provides `HDX_WritePNG` from sub-project B and `stbi_load`; `<sys/stat.h>`/`<cstring>` may need adding at top):

```cpp
extern "C" {
    #include "hdassets.h"
}
#include <sys/stat.h>

TEST(sdlpal, HDAssets_GetAndCache) {
    uint64_t hash = 0xABCDEF1234567890ull;
    uint8_t px[3 * 2 * 4];
    for (int i = 0; i < 3 * 2 * 4; i++) px[i] = (uint8_t)i;
    const char *dir = "/tmp/hda_test";
    mkdir(dir, 0755);
    char p[256];
    snprintf(p, sizeof(p), "%s/%016llx.png", dir, (unsigned long long)hash);
    ASSERT_EQ(0, HDX_WritePNG(p, px, 3, 2));

    SDL_Color ref[256];
    for (int i = 0; i < 256; i++) { ref[i].r = (uint8_t)i; ref[i].g = 0; ref[i].b = 0; ref[i].a = 255; }
    HDAssets_Init(dir, ref);

    const uint8_t *rgba = NULL; INT w = 0, h = 0;
    ASSERT_EQ(0, HDAssets_Get(hash, &rgba, &w, &h));
    EXPECT_EQ(3, w);
    EXPECT_EQ(2, h);
    EXPECT_EQ(0, rgba[0]);   // pixel0 R
    EXPECT_EQ(1, rgba[1]);   // pixel0 G

    // Miss (negative cache) returns -1.
    const uint8_t *rgbaM = NULL; INT wm = 0, hm = 0;
    EXPECT_EQ(-1, HDAssets_Get(0x1111ull, &rgbaM, &wm, &hm));

    // Second get is served from cache — same pointer, no reload.
    const uint8_t *rgba2 = NULL;
    ASSERT_EQ(0, HDAssets_Get(hash, &rgba2, &w, &h));
    EXPECT_EQ(rgba, rgba2);

    HDAssets_Free();
}

TEST(sdlpal, HDAssets_PaletteGate) {
    SDL_Color ref[256];
    for (int i = 0; i < 256; i++) { ref[i].r = (uint8_t)i; ref[i].g = 0; ref[i].b = 0; ref[i].a = 255; }
    HDAssets_Init("/tmp/hda_none", ref);

    SDL_Color live[256];
    memcpy(live, ref, sizeof(ref));
    EXPECT_TRUE(HDAssets_PaletteMatchesReference(live));

    live[100].r = (uint8_t)(live[100].r ^ 0xFF);
    EXPECT_FALSE(HDAssets_PaletteMatchesReference(live));

    // Alpha differences are ignored.
    memcpy(live, ref, sizeof(ref));
    live[50].a = 0;
    EXPECT_TRUE(HDAssets_PaletteMatchesReference(live));

    HDAssets_Free();
}
```

- [ ] **Step 2: Extend the harness, run, verify failure**

In `tests/run-standalone.sh`, add a compile step and link the object (mirror the existing `hd_extract_core.c` step):
```bash
echo "[2d/6] compile hdassets.c"
clang -c "$ROOT/hdassets.c" -o "$OUT/hdassets.o" -DUSE_SDL3=1 -DHAVE_CONFIG_H "${INC[@]}"
```
and add `"$OUT/hdassets.o"` to the final link object list. (`$ROOT` is already an include dir, so `hdassets.h` at repo root is found.)

Run: `cd /Users/xiaolin/Projects/sdlpal && ./tests/run-standalone.sh`
Expected: FAIL — `hdassets.h` not found / undefined `HDAssets_*`.

- [ ] **Step 3: Create `hdassets.h`**

```c
#ifndef HDASSETS_H
#define HDASSETS_H

#include <stdint.h>
#include "palcommon.h"   /* SDL_Color, INT, BOOL */

#ifdef __cplusplus
extern "C" {
#endif

void HDAssets_Init(const char *dir, const SDL_Color *refPalette);
void HDAssets_Free(void);
INT  HDAssets_Get(uint64_t hash, const uint8_t **outRGBA, INT *outW, INT *outH);
BOOL HDAssets_PaletteMatchesReference(const SDL_Color *live);

#ifdef __cplusplus
}
#endif

#endif
```

- [ ] **Step 4: Create `hdassets.c`**

```c
#include "hdassets.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Provided by stb_image: at runtime via the app's existing stb implementation;
   in the unit-test harness via the STB_IMAGE_IMPLEMENTATION TU. */
extern unsigned char *stbi_load(char const *filename, int *x, int *y, int *comp, int req_comp);
extern void           stbi_image_free(void *retval_from_stbi_load);

#define HDA_MAX 512

typedef struct tagHDAENTRY {
   uint64_t   hash;
   uint8_t   *rgba;    /* NULL => negative cache (no asset for this hash) */
   INT        w, h;
} HDAENTRY;

static HDAENTRY   g_hda[HDA_MAX];
static INT        g_hdaCount = 0;
static char       g_hdaDir[1024] = "hd_assets";
static SDL_Color  g_hdaRef[256];

void
HDAssets_Init(
   const char       *dir,
   const SDL_Color  *refPalette
)
{
   INT i;
   HDAssets_Free();
   if (dir != NULL)
   {
      strncpy(g_hdaDir, dir, sizeof(g_hdaDir) - 1);
      g_hdaDir[sizeof(g_hdaDir) - 1] = '\0';
   }
   if (refPalette != NULL)
   {
      for (i = 0; i < 256; i++) g_hdaRef[i] = refPalette[i];
   }
}

void
HDAssets_Free(
   void
)
{
   INT i;
   for (i = 0; i < g_hdaCount; i++)
   {
      if (g_hda[i].rgba != NULL)
      {
         stbi_image_free(g_hda[i].rgba);
         g_hda[i].rgba = NULL;
      }
   }
   g_hdaCount = 0;
}

static HDAENTRY *
HDA_Find(
   uint64_t hash
)
{
   INT i;
   for (i = 0; i < g_hdaCount; i++)
   {
      if (g_hda[i].hash == hash) return &g_hda[i];
   }
   return NULL;
}

INT
HDAssets_Get(
   uint64_t         hash,
   const uint8_t  **outRGBA,
   INT             *outW,
   INT             *outH
)
{
   HDAENTRY *e = HDA_Find(hash);
   if (e == NULL)
   {
      char           path[1152];
      int            w = 0, h = 0, comp = 0;
      unsigned char *img;
      if (g_hdaCount >= HDA_MAX) return -1;
      snprintf(path, sizeof(path), "%s/%016llx.png", g_hdaDir, (unsigned long long)hash);
      img = stbi_load(path, &w, &h, &comp, 4);
      e = &g_hda[g_hdaCount++];
      e->hash = hash;
      e->rgba = img;              /* img == NULL => negative cache */
      e->w = w;
      e->h = h;
   }
   if (e->rgba == NULL) return -1;
   if (outRGBA) *outRGBA = e->rgba;
   if (outW) *outW = e->w;
   if (outH) *outH = e->h;
   return 0;
}

BOOL
HDAssets_PaletteMatchesReference(
   const SDL_Color *live
)
{
   INT i;
   if (live == NULL) return FALSE;
   for (i = 0; i < 256; i++)
   {
      if (live[i].r != g_hdaRef[i].r ||
          live[i].g != g_hdaRef[i].g ||
          live[i].b != g_hdaRef[i].b)
      {
         return FALSE;
      }
   }
   return TRUE;
}
```

- [ ] **Step 5: Run tests to verify pass**

Run: `cd /Users/xiaolin/Projects/sdlpal && ./tests/run-standalone.sh 2>&1 | tail -6`
Expected: PASS — all prior tests + `HDAssets_GetAndCache` + `HDAssets_PaletteGate`.

- [ ] **Step 6: Commit**

```bash
git add hdassets.h hdassets.c tests/run-standalone.sh tests/test_rleblit.cpp
git commit -m "feat(hd-runtime): add HD asset cache (lazy load + negative cache + palette gate)"
```

---

## Task 2: Wire HD assets into `VIDEO_HD_Present`

**Files:**
- Modify: `video.c` (include hdassets.h + unity-include hdassets.c; lazy Init; per-frame gate; overlay lookup; Free in VIDEO_Shutdown)

**Interfaces:**
- Consumes: `HDAssets_Init`, `HDAssets_Free`, `HDAssets_Get`, `HDAssets_PaletteMatchesReference` (Task 1); existing `gpPalette`, `PAL_GetPalette`, the `HDDRAWCMD.hash` field, `HD_SCALE`/`HD_W`/`HD_H`, `gpHDPixels`.

- [ ] **Step 1: Include the module in `video.c`**

Near the top of `video.c` with the other `#include`s, add:
```c
#include "hdassets.h"
```
If `PAL_GetPalette` is not already declared in a header video.c includes, also add `#include "palette.h"`.
At the very BOTTOM of `video.c` (after all existing code), add the unity include so `hdassets.c` compiles as part of the Pal target without a project-file change:
```c
#include "hdassets.c"
```

- [ ] **Step 2: Lazy-init + per-frame palette gate in `VIDEO_HD_Present`**

In `VIDEO_HD_Present`, after the `gpHDTexture == NULL` guard and before the overlay loop (`n = PAL_HDGetFrameCommands(&cmds);`), add:
```c
   //
   // HD assets: lazy one-time init (game data is ready by first present),
   // and compute the per-frame palette gate once.
   //
   {
      static BOOL s_hdaInit = FALSE;
      if (!s_hdaInit)
      {
         HDAssets_Init("hd_assets", PAL_GetPalette(0, FALSE));
         s_hdaInit = TRUE;
      }
   }
   {
      BOOL g_paletteRef = HDAssets_PaletteMatchesReference(gpPalette->colors);
      /* declared here so the overlay loop below can read it */
```
Close the block after the overlay loop (see Step 3) — i.e. wrap the overlay `for` loop inside this `{ ... }` scope so `g_paletteRef` is visible. (Alternatively declare `BOOL g_paletteRef` among the function's locals at the top and assign it here — pick whichever keeps the diff clean; the value must be computed once per frame, not per command.)

- [ ] **Step 3: Add the HD-asset lookup at the top of the overlay loop body**

Inside the `for (c = 0; c < n; c++)` loop, immediately after the `if (!cmds[c].plain || cmds[c].sprite == NULL) continue;` line, insert the HD-asset path. When it draws, `continue` to skip the placeholder:
```c
      /* HD asset path: only when the live palette equals the baked reference. */
      if (g_paletteRef)
      {
         const uint8_t *hd = NULL; INT hw = 0, hh = 0;
         if (HDAssets_Get(cmds[c].hash, &hd, &hw, &hh) == 0 && hw <= HD_W && hh <= HD_H)
         {
            INT ax, ay;
            for (ay = 0; ay < hh; ay++)
            {
               INT dyv = cmds[c].y * HD_SCALE + ay;
               if (dyv < 0 || dyv >= HD_H) continue;
               for (ax = 0; ax < hw; ax++)
               {
                  INT dxv = cmds[c].x * HD_SCALE + ax;
                  const uint8_t *pp = hd + (ay * hw + ax) * 4;
                  if (dxv < 0 || dxv >= HD_W) continue;
                  if (pp[3])   /* opaque only */
                     gpHDPixels[dyv * HD_W + dxv] =
                        ((uint32_t)pp[3] << 24) | ((uint32_t)pp[0] << 16) |
                        ((uint32_t)pp[1] << 8)  | (uint32_t)pp[2];
               }
            }
            continue;   /* HD asset drawn — skip the placeholder */
         }
      }
      /* else: fall through to the existing PAL_HDRenderSprite placeholder below */
```
Leave the existing placeholder code (the `PAL_HDRenderSprite(...)` block) unchanged after this insert.

- [ ] **Step 4: Optional debug counter (folds into the existing `[HD]` log)**

To confirm assets are actually used at runtime, extend the `#if HD_DEBUG` log to count HD-asset hits this frame. Add a `static UINT s_hdaHits;` reset at the top of the overlay section, `s_hdaHits++;` right before the HD `continue;`, and include `hdaHits=%u` in the existing `UTIL_LogOutput` format. (Keep this within the existing `#if HD_DEBUG` guard.)

- [ ] **Step 5: Free in `VIDEO_Shutdown`**

In `VIDEO_Shutdown`, next to the existing HD frees (`gpHDTexture`/`gpHDPixels`), add:
```c
   HDAssets_Free();
```

- [ ] **Step 6: Build**

Run: `cd /Users/xiaolin/Projects/sdlpal && xcodebuild -workspace macos/SDLPal.xcworkspace -scheme SDLPal -configuration Release -derivedDataPath build/macos_dd build 2>&1 | tail -3`
Expected: `** BUILD SUCCEEDED **`. (If the linker complains about duplicate `stbi_load`/`HDAssets_*`, confirm `hdassets.c` is included ONLY via the unity `#include` in video.c and is NOT separately in the target.)

- [ ] **Step 7: Unit tests still pass**

Run: `./tests/run-standalone.sh 2>&1 | tail -3`
Expected: all tests pass (Task 1's included).

- [ ] **Step 8: Runtime — assets present (no crash + hits logged)**

```bash
rm -rf ~/PAL/Pal.app && cp -R build/macos_dd/Build/Products/Release/Pal.app ~/PAL/Pal.app
# ensure hd_assets is in place (sub-project B put them here)
ls ~/PAL/hd_assets/*.png | wc -l    # expect 88
printf 'HDRemaster=1\nLogLevel=0\nLogFileName=sdlpal.log\n' > ~/PAL/sdlpal.cfg
( cd ~/PAL && ./Pal.app/Contents/MacOS/Pal >/dev/null 2>&1 & echo $! >/tmp/hdc.pid ); sleep 10
if kill -0 $(cat /tmp/hdc.pid) 2>/dev/null; then echo ALIVE; else echo DIED; fi
kill $(cat /tmp/hdc.pid) 2>/dev/null; pkill -x Pal 2>/dev/null
grep "\[HD\]" ~/PAL/sdlpal.log | tail -5   # look for hdaHits when a portrait is shown
```
Expected: ALIVE, no crash. `hdaHits` is only nonzero once a portrait is on screen (title/menu has none) — so a zero count during the intro is fine; the definitive check is Step 10.

- [ ] **Step 9: Runtime — assets absent (graceful fallback) + HD off (regression)**

```bash
mv ~/PAL/hd_assets ~/PAL/hd_assets.off
( cd ~/PAL && ./Pal.app/Contents/MacOS/Pal >/dev/null 2>&1 & echo $! >/tmp/hdc2.pid ); sleep 6
kill 0 2>/dev/null; if kill -0 $(cat /tmp/hdc2.pid) 2>/dev/null; then echo "no-assets ALIVE"; fi
kill $(cat /tmp/hdc2.pid) 2>/dev/null; pkill -x Pal 2>/dev/null
mv ~/PAL/hd_assets.off ~/PAL/hd_assets
printf 'HDRemaster=0\n' > ~/PAL/sdlpal.cfg
( cd ~/PAL && ./Pal.app/Contents/MacOS/Pal >/dev/null 2>&1 & echo $! >/tmp/hdc3.pid ); sleep 6
if kill -0 $(cat /tmp/hdc3.pid) 2>/dev/null; then echo "HD-off ALIVE"; fi
kill $(cat /tmp/hdc3.pid) 2>/dev/null; pkill -x Pal 2>/dev/null
```
Expected: both runs ALIVE (no crash). With assets removed, the game still runs (placeholder). With HD off, classic path.

- [ ] **Step 10: Visual confirmation (controller/user)**

Restore `printf 'HDRemaster=1\n...' > ~/PAL/sdlpal.cfg`, launch the app, start/continue a game, and open the status screen (or trigger a dialogue) so a character portrait appears. Capture with `screencapture` and confirm the portrait is visibly HD (sharp, anti-aliased) vs. the blocky placeholder. Leave the screenshot path in the report for the controller to inspect. (Reaching a portrait needs in-game navigation; if automation can't get there, note it and hand the final eyeball to the user.)

- [ ] **Step 11: Commit**

```bash
git add video.c
git commit -m "feat(hd-runtime): blit HD assets in VIDEO_HD_Present when present (palette-gated), else placeholder"
```

---

## Self-Review

**Spec coverage:**
- §3.1 cache module → Task 1 (`hdassets.c/.h`, all four functions).
- §3.2 overlay lookup → Task 2 Step 3.
- §3.3 lifecycle (Init on first present, Free in Shutdown) → Task 2 Steps 2, 5.
- §4 palette gate (per-frame, R/G/B compare, fall back on mismatch) → Task 1 (`HDAssets_PaletteMatchesReference`) + Task 2 Step 2/3.
- §5 hardcoded `hd_assets`, no config change → Task 2 Step 2 (`HDAssets_Init("hd_assets", ...)`), no palcfg edits.
- §6 lazy load + negative cache → Task 1 `HDAssets_Get`.
- §7 tests (synthetic PNG, no game data) → Task 1 tests; integration Task 2 Steps 8–10.
- §8 unity include (zero pbxproj) → Task 2 Step 1.
- §10 success (visible HD, fallback, zero regression) → Task 2 Steps 8–10.

**Placeholder scan:** No TBD/TODO; every code step has complete code. The one human-judgment point (Step 10 visual) is explicit and gated, not a placeholder.

**Type consistency:** `HDAssets_Init(const char*, const SDL_Color*)`, `HDAssets_Get(uint64_t, const uint8_t**, INT*, INT*)`, `HDAssets_PaletteMatchesReference(const SDL_Color*)`, `HDAssets_Free(void)` — identical across Task 1 (definition) and Task 2 (call sites). The overlay blit reads `HDAssets_Get`'s RGBA8 (`[r,g,b,a]`) and packs `0xAARRGGBB`, consistent with `gpHDPixels`' ARGB format used by the base layer. `cmds[c].hash` is the `HDDRAWCMD` field added in sub-project A.
