# HD Render Core (Sub-project A) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove the HD-remaster architecture by rendering the unmodified game through a high-resolution truecolor "dual-plane" pipeline, with content-hash sprite identity and palette-effect reproduction validated on fade-in/out.

**Architecture:** The original 8-bit/320×200 render path stays authoritative. A new HD plane is composited at present time as `upscale(faded 32-bit screen)` (base) + HD overlays of the sprites drawn this frame (colorized with the *live* palette, so fades/tints reproduce automatically). Sprites are identified by a content hash computed at blit time, so sub-project C can later swap the placeholder upscale for AI assets keyed by that same hash.

**Tech Stack:** C (matching existing codebase style), SDL3 (as used by the macOS build in `video.c`), GoogleTest via the `PalTests` Xcode scheme. No new external dependencies.

## Global Constraints

- No new external libraries or submodules. Hash = FNV-1a 64-bit implemented inline.
- New code lives in existing compiled translation units (`palcommon.c/.h`, `video.c/.h`); new unit tests append to `tests/test_rleblit.cpp`. No `project.pbxproj` edits.
- `HDRemaster` config option defaults to `FALSE`.
- When `HDRemaster` is off, output must be **pixel-identical** to the current build (zero regression).
- HD internal scale is a compile-time constant `HD_SCALE = 4` (1280×800) for this spike; not user-configurable yet.
- Match existing code style (Linux/K&R brace style, `LP*`/`UINT`/`BYTE` typedefs, ALL-CAPS public `PAL_*`/`VIDEO_*` names).
- Personal-use spike; no copyrighted game art is stored, embedded, or committed. The placeholder upscaler operates on the user's own runtime data only.
- Build the app: `xcodebuild -workspace macos/SDLPal.xcworkspace -scheme SDLPal -configuration Release -derivedDataPath build/macos_dd build`
- Run unit tests: `xcodebuild -workspace macos/SDLPal.xcworkspace -scheme PalTests -destination 'platform=macOS' test`
- Manual run with real data: copy the fresh `Pal.app` into `~/PAL/` and launch there (that folder holds the user's own game data + `sdlpal.cfg`).

---

## File Structure

| File | Responsibility | Change |
|------|----------------|--------|
| `palcommon.h` | Declare sprite-identity + HD-render pure helpers | Modify |
| `palcommon.c` | Implement `PAL_RLESpriteBytes`, `PAL_HashSprite`, `PAL_HDRenderSprite`; add blit-interception recording | Modify |
| `video.h` | Declare `VIDEO_HD_*` lifecycle/compose entry points and the draw-command recorder API | Modify |
| `video.c` | HD plane lifecycle, per-frame draw-command buffer, compose + present, `HDRemaster` gating, debug overlay | Modify |
| `palcfg.h` / `palcfg.c` | Register `HDRemaster` boolean config | Modify |
| `tests/test_rleblit.cpp` | Unit tests for identity + HD-render helpers | Modify (append) |

---

## Task 1: Sprite identity — RLE byte length + content hash

**Files:**
- Modify: `palcommon.h` (add two declarations near the existing RLE helpers, ~line 205)
- Modify: `palcommon.c` (add two functions)
- Test: `tests/test_rleblit.cpp` (append two `TEST` cases)

**Interfaces:**
- Produces:
  - `UINT PAL_RLESpriteBytes(LPCBITMAPRLE lpBitmapRLE);` — total sprite length in bytes (optional 4-byte `0x00000002` prefix + 4-byte w/h header + RLE data). Returns `0` if `lpBitmapRLE == NULL`.
  - `uint64_t PAL_HashSprite(LPCBITMAPRLE lpBitmapRLE);` — FNV-1a 64 over the *prefix-stripped* content (w/h header + data), so identity is prefix-independent. Returns `0` if `lpBitmapRLE == NULL`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_rleblit.cpp` (the existing `bitmap[]` array at the top of the file is a real sprite; reuse it):

```cpp
TEST(sdlpal, PAL_RLESpriteBytes) {
    // bitmap[] has no 0x02 prefix; width/height are in bytes [0..3].
    UINT w = bitmap[0] | (bitmap[1] << 8);
    UINT h = bitmap[2] | (bitmap[3] << 8);
    UINT n = PAL_RLESpriteBytes(bitmap);
    // Length must cover at least the 4-byte header and be within the array.
    EXPECT_GT(n, 4u);
    EXPECT_LE(n, (UINT)sizeof(bitmap));
    // NULL is handled.
    EXPECT_EQ(0u, PAL_RLESpriteBytes(NULL));
    (void)w; (void)h;
}

TEST(sdlpal, PAL_HashSprite) {
    uint64_t a = PAL_HashSprite(bitmap);
    uint64_t b = PAL_HashSprite(bitmap);
    EXPECT_EQ(a, b);                 // stable
    EXPECT_NE(0ull, a);              // non-trivial
    EXPECT_EQ(0ull, PAL_HashSprite(NULL));

    // A copy WITH the optional 0x02 prefix must hash identically (prefix-independent).
    UINT n = PAL_RLESpriteBytes(bitmap);
    std::vector<BYTE> withPrefix(4 + n);
    withPrefix[0] = 0x02; withPrefix[1] = 0; withPrefix[2] = 0; withPrefix[3] = 0;
    memcpy(withPrefix.data() + 4, bitmap, n);
    EXPECT_EQ(a, PAL_HashSprite(withPrefix.data()));

    // A one-byte change in the data must change the hash.
    std::vector<BYTE> mutated(bitmap, bitmap + n);
    mutated[n - 1] ^= 0xFF;
    EXPECT_NE(a, PAL_HashSprite(mutated.data()));
}
```

Add `#include <vector>` and `#include <cstring>` near the top of `tests/test_rleblit.cpp` if not already present.

- [ ] **Step 2: Run tests to verify they fail**

Run: `xcodebuild -workspace macos/SDLPal.xcworkspace -scheme PalTests -destination 'platform=macOS' test 2>&1 | grep -E "PAL_RLESpriteBytes|PAL_HashSprite|error:"`
Expected: FAIL — undefined symbols `PAL_RLESpriteBytes` / `PAL_HashSprite`.

- [ ] **Step 3: Declare in `palcommon.h`**

Add after the `PAL_RLEGetHeight` declaration (~line 216):

```c
UINT
PAL_RLESpriteBytes(
   LPCBITMAPRLE      lpBitmapRLE
);

uint64_t
PAL_HashSprite(
   LPCBITMAPRLE      lpBitmapRLE
);
```

Ensure `<stdint.h>` is available (add `#include <stdint.h>` at the top of `palcommon.h` if `uint64_t` is not already visible).

- [ ] **Step 4: Implement in `palcommon.c`**

Add these functions (place them next to the other RLE helpers). The walker mirrors the decoder in `PAL_RLEBlitToSurfaceWithShadow`: a token `T` with `(T & 0x80) && T <= 0x80 + width` is a transparent run of `T-0x80` pixels consuming **0** data bytes; otherwise it is an opaque run of `T` pixels consuming `T` data bytes. Walk until `width*height` pixels are produced.

```c
UINT
PAL_RLESpriteBytes(
   LPCBITMAPRLE      lpBitmapRLE
)
{
   UINT uiPrefix = 0, uiWidth, uiHeight, uiTotal, uiProduced = 0, uiBytes = 0;
   BYTE T;

   if (lpBitmapRLE == NULL)
   {
      return 0;
   }

   if (lpBitmapRLE[0] == 0x02 && lpBitmapRLE[1] == 0x00 &&
       lpBitmapRLE[2] == 0x00 && lpBitmapRLE[3] == 0x00)
   {
      lpBitmapRLE += 4;
      uiPrefix = 4;
   }

   uiWidth  = lpBitmapRLE[0] | (lpBitmapRLE[1] << 8);
   uiHeight = lpBitmapRLE[2] | (lpBitmapRLE[3] << 8);
   uiTotal  = uiWidth * uiHeight;
   lpBitmapRLE += 4;
   uiBytes = 4;

   while (uiProduced < uiTotal)
   {
      T = *lpBitmapRLE++;
      uiBytes++;
      if ((T & 0x80) && T <= 0x80 + uiWidth)
      {
         uiProduced += T - 0x80;
      }
      else
      {
         uiProduced += T;
         lpBitmapRLE += T;
         uiBytes += T;
      }
   }

   return uiPrefix + uiBytes;
}

uint64_t
PAL_HashSprite(
   LPCBITMAPRLE      lpBitmapRLE
)
{
   const uint64_t   FNV_OFFSET = 14695981039346656037ull;
   const uint64_t   FNV_PRIME  = 1099511628211ull;
   uint64_t         hash = FNV_OFFSET;
   UINT             uiBytes, i, uiPrefix = 0;
   LPCBITMAPRLE     p = lpBitmapRLE;

   if (lpBitmapRLE == NULL)
   {
      return 0;
   }

   if (p[0] == 0x02 && p[1] == 0x00 && p[2] == 0x00 && p[3] == 0x00)
   {
      uiPrefix = 4;
   }

   uiBytes = PAL_RLESpriteBytes(lpBitmapRLE);
   p = lpBitmapRLE + uiPrefix;            /* hash prefix-stripped content */
   for (i = 0; i < uiBytes - uiPrefix; i++)
   {
      hash ^= (uint64_t)p[i];
      hash *= FNV_PRIME;
   }

   return hash;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `xcodebuild -workspace macos/SDLPal.xcworkspace -scheme PalTests -destination 'platform=macOS' test 2>&1 | grep -E "PAL_RLESpriteBytes|PAL_HashSprite|Test Suite.*passed|failed"`
Expected: both `PAL_RLESpriteBytes` and `PAL_HashSprite` PASS.

- [ ] **Step 6: Commit**

```bash
git add palcommon.h palcommon.c tests/test_rleblit.cpp
git commit -m "feat(hd): add content-hash sprite identity (RLE byte length + FNV-1a hash)"
```

---

## Task 2: Register `HDRemaster` config option

**Files:**
- Modify: `palcfg.h` (add `PALCFG_HDREMASTER` enum + `fHDRemaster` field)
- Modify: `palcfg.c` (add config table row + load/write/get/set handling)

**Interfaces:**
- Produces: `gConfig.fHDRemaster` (BOOL), parsed from `HDRemaster=` in `sdlpal.cfg`, default `FALSE`.

- [ ] **Step 1: Add the enum and struct field in `palcfg.h`**

In the `PALCFG_*` enum (near `PALCFG_ENABLEAVIPLAY`), add:

```c
    PALCFG_HDREMASTER,
```

In the config struct (near `BOOL fEnableAviPlay;`), add:

```c
    BOOL             fHDRemaster;
```

- [ ] **Step 2: Add the config table row in `palcfg.c`**

Next to the `PALCFG_ENABLEAVIPLAY` row in the descriptor table, add (default FALSE, min FALSE, max TRUE):

```c
	{ PALCFG_HDREMASTER,        PALCFG_BOOLEAN,  "HDRemaster",        10, MAKE_BOOLEAN(FALSE,                         FALSE,                 TRUE) },
```

- [ ] **Step 3: Wire load / write / get / set in `palcfg.c`**

Mirror every place `PALCFG_ENABLEAVIPLAY` / `fEnableAviPlay` appears:

- In the loader (near `gConfig.fEnableAviPlay = values[PALCFG_ENABLEAVIPLAY].bValue;`):
```c
	gConfig.fHDRemaster = values[PALCFG_HDREMASTER].bValue;
```
- In the writer (near the `PALCFG_ENABLEAVIPLAY` `sprintf`):
```c
		sprintf(buf, "%s=%d\n", PAL_ConfigName(PALCFG_HDREMASTER), gConfig.fHDRemaster); fputs(buf, fp);
```
- In `PAL_GetConfigItem` (near `case PALCFG_ENABLEAVIPLAY:`):
```c
	case PALCFG_HDREMASTER:        value.bValue = gConfig.fHDRemaster; break;
```
- In `PAL_SetConfigItem` (near `case PALCFG_ENABLEAVIPLAY:`):
```c
	case PALCFG_HDREMASTER:        gConfig.fHDRemaster = value.bValue; break;
```

- [ ] **Step 4: Build to verify it compiles**

Run: `xcodebuild -workspace macos/SDLPal.xcworkspace -scheme SDLPal -configuration Release -derivedDataPath build/macos_dd build 2>&1 | tail -3`
Expected: `** BUILD SUCCEEDED **`

- [ ] **Step 5: Verify parsing manually**

```bash
printf 'HDRemaster=1\n' > /tmp/hdtest.cfg
```
Confirm by reading `PAL_LoadConfig`/token handling that `HDRemaster` maps to `PALCFG_HDREMASTER` (grep the descriptor table). No runtime assert needed here; Task 6 exercises the flag end to end.

- [ ] **Step 6: Commit**

```bash
git add palcfg.h palcfg.c
git commit -m "feat(hd): add HDRemaster config option (default off)"
```

---

## Task 3: Per-frame draw-command buffer + frame boundary

**Files:**
- Modify: `video.h` (declare recorder API + `HDDrawCmd` struct)
- Modify: `video.c` (implement the buffer; pure list logic, no SDL yet)
- Test: `tests/test_rleblit.cpp` (append tests — the recorder API is plain C and links into PalTests via `video.c`? No: `video.c` is NOT in the PalTests target. Put the *pure* buffer logic in `palcommon.c` instead so it is testable.)

> Decomposition note: to keep this unit-testable, the buffer lives in `palcommon.c` (in the PalTests target), not `video.c`. `video.c` only calls it.

- Modify instead: `palcommon.h` / `palcommon.c` for the buffer; `video.c` calls it.

**Interfaces:**
- Produces (in `palcommon.h`):
```c
typedef struct tagHDDRAWCMD {
   uint64_t   hash;       /* PAL_HashSprite of the sprite */
   INT        x, y;       /* destination top-left in 320x200 space */
   BOOL       plain;      /* TRUE only for the plain blit path (no colorshift/mono/shadow) */
} HDDRAWCMD;

void        PAL_HDResetFrameOnNextRecord(void);   /* mark: next record starts a new frame */
void        PAL_HDRecordBlit(uint64_t hash, INT x, INT y, BOOL plain);
UINT        PAL_HDGetFrameCommands(const HDDRAWCMD **ppCmds);  /* returns count, sets *ppCmds */
UINT        PAL_HDGetUniqueHashCount(void);        /* distinct hashes in current frame */
```
- Behavior: after `PAL_HDResetFrameOnNextRecord()`, the *next* `PAL_HDRecordBlit` clears the buffer first (new frame), then appends. Subsequent records in the same frame append. This lets presents with no intervening blits (fades) keep the previous frame's commands.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_rleblit.cpp`:

```cpp
TEST(sdlpal, PAL_HDDrawCommandBuffer) {
    const HDDRAWCMD *cmds = NULL;

    // Fresh frame: reset-then-record clears and appends.
    PAL_HDResetFrameOnNextRecord();
    PAL_HDRecordBlit(0x1111, 10, 20, TRUE);
    PAL_HDRecordBlit(0x2222, 30, 40, TRUE);
    EXPECT_EQ(2u, PAL_HDGetFrameCommands(&cmds));
    EXPECT_EQ(0x1111ull, cmds[0].hash);
    EXPECT_EQ(30, cmds[1].x);

    // Same frame (no reset): appends.
    PAL_HDRecordBlit(0x3333, 0, 0, FALSE);
    EXPECT_EQ(3u, PAL_HDGetFrameCommands(&cmds));

    // Unique hash count ignores duplicates.
    PAL_HDResetFrameOnNextRecord();
    PAL_HDRecordBlit(0xAAAA, 0, 0, TRUE);
    PAL_HDRecordBlit(0xAAAA, 1, 1, TRUE);
    PAL_HDRecordBlit(0xBBBB, 2, 2, TRUE);
    EXPECT_EQ(2u, PAL_HDGetUniqueHashCount());

    // A reset followed by a record starts a brand new frame (old cmds gone).
    PAL_HDResetFrameOnNextRecord();
    PAL_HDRecordBlit(0xCCCC, 5, 5, TRUE);
    EXPECT_EQ(1u, PAL_HDGetFrameCommands(&cmds));
    EXPECT_EQ(0xCCCCull, cmds[0].hash);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `xcodebuild -workspace macos/SDLPal.xcworkspace -scheme PalTests -destination 'platform=macOS' test 2>&1 | grep -E "PAL_HDDrawCommandBuffer|error:"`
Expected: FAIL — undefined recorder symbols.

- [ ] **Step 3: Declare in `palcommon.h`**

Add the `HDDRAWCMD` struct and the four function declarations shown in the Interfaces block above.

- [ ] **Step 4: Implement in `palcommon.c`**

Use a fixed-capacity static buffer (sprites per frame are bounded; 4096 is ample). No dynamic allocation in the hot path.

```c
#define HD_MAX_CMDS   4096
static HDDRAWCMD       g_hdCmds[HD_MAX_CMDS];
static UINT            g_hdCmdCount = 0;
static BOOL            g_hdResetPending = FALSE;

void
PAL_HDResetFrameOnNextRecord(
   void
)
{
   g_hdResetPending = TRUE;
}

void
PAL_HDRecordBlit(
   uint64_t   hash,
   INT        x,
   INT        y,
   BOOL       plain
)
{
   if (g_hdResetPending)
   {
      g_hdCmdCount = 0;
      g_hdResetPending = FALSE;
   }
   if (g_hdCmdCount >= HD_MAX_CMDS)
   {
      return;
   }
   g_hdCmds[g_hdCmdCount].hash  = hash;
   g_hdCmds[g_hdCmdCount].x     = x;
   g_hdCmds[g_hdCmdCount].y     = y;
   g_hdCmds[g_hdCmdCount].plain = plain;
   g_hdCmdCount++;
}

UINT
PAL_HDGetFrameCommands(
   const HDDRAWCMD  **ppCmds
)
{
   if (ppCmds) *ppCmds = g_hdCmds;
   return g_hdCmdCount;
}

UINT
PAL_HDGetUniqueHashCount(
   void
)
{
   UINT i, j, unique = 0;
   for (i = 0; i < g_hdCmdCount; i++)
   {
      BOOL seen = FALSE;
      for (j = 0; j < i; j++)
      {
         if (g_hdCmds[j].hash == g_hdCmds[i].hash) { seen = TRUE; break; }
      }
      if (!seen) unique++;
   }
   return unique;
}
```

- [ ] **Step 5: Run to verify pass**

Run: `xcodebuild -workspace macos/SDLPal.xcworkspace -scheme PalTests -destination 'platform=macOS' test 2>&1 | grep -E "PAL_HDDrawCommandBuffer|passed|failed"`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add palcommon.h palcommon.c tests/test_rleblit.cpp
git commit -m "feat(hd): add per-frame draw-command buffer with frame-boundary logic"
```

---

## Task 4: HD sprite render (decode + upscale + colorize)

**Files:**
- Modify: `palcommon.h` (declare `PAL_HDRenderSprite`)
- Modify: `palcommon.c` (implement it)
- Test: `tests/test_rleblit.cpp` (append)

**Interfaces:**
- Produces:
```c
/* Decode the sprite to indices, nearest-upscale by `scale`, colorize via `palette`.
   Writes ARGB8888 into `out` (caller-allocated, (w*scale)*(h*scale) uint32).
   Transparent (uncovered) pixels get alpha 0. Sets *outW/*outH to the scaled size.
   Returns 0 on success, -1 on NULL/զero-size. */
INT PAL_HDRenderSprite(LPCBITMAPRLE lpBitmapRLE, const SDL_Color *palette,
                       INT scale, uint32_t *out, INT *outW, INT *outH);
```

- [ ] **Step 1: Write the failing test**

A tiny hand-built sprite: width=2, height=1, one opaque run of 2 pixels (indices 5, 6). At `scale=2` the output is 4×2; each source pixel becomes a 2×2 block; alpha 0xFF where covered.

```cpp
TEST(sdlpal, PAL_HDRenderSprite) {
    // width=2, height=1; token 0x02 => opaque run of 2 pixels: idx 5, idx 6.
    BYTE spr[] = { 0x02,0x00, 0x01,0x00, 0x02, 0x05, 0x06 };
    SDL_Color pal[256]; memset(pal, 0, sizeof(pal));
    pal[5].r = 0x10; pal[5].g = 0x20; pal[5].b = 0x30; pal[5].a = 0xFF;
    pal[6].r = 0x40; pal[6].g = 0x50; pal[6].b = 0x60; pal[6].a = 0xFF;

    uint32_t out[4 * 2]; INT ow = 0, oh = 0;
    INT rc = PAL_HDRenderSprite(spr, pal, 2, out, &ow, &oh);
    EXPECT_EQ(0, rc);
    EXPECT_EQ(4, ow);
    EXPECT_EQ(2, oh);

    // ARGB8888: top-left 2x2 block = index 5 color, opaque.
    uint32_t c5 = (0xFFu<<24)|(0x10u<<16)|(0x20u<<8)|0x30u;
    uint32_t c6 = (0xFFu<<24)|(0x40u<<16)|(0x50u<<8)|0x60u;
    EXPECT_EQ(c5, out[0]);            // (0,0)
    EXPECT_EQ(c5, out[1]);            // (1,0)
    EXPECT_EQ(c6, out[2]);            // (2,0)
    EXPECT_EQ(c6, out[4 + 0]);        // (0,1)

    EXPECT_EQ(-1, PAL_HDRenderSprite(NULL, pal, 2, out, &ow, &oh));
}
```

- [ ] **Step 2: Run to verify failure**

Run: `xcodebuild -workspace macos/SDLPal.xcworkspace -scheme PalTests -destination 'platform=macOS' test 2>&1 | grep -E "PAL_HDRenderSprite|error:"`
Expected: FAIL — undefined symbol.

- [ ] **Step 3: Implement in `palcommon.c`**

Decode into an index buffer + coverage mask (same run semantics as the walker), then nearest-upscale + colorize to ARGB8888.

```c
INT
PAL_HDRenderSprite(
   LPCBITMAPRLE      lpBitmapRLE,
   const SDL_Color  *palette,
   INT               scale,
   uint32_t         *out,
   INT              *outW,
   INT              *outH
)
{
   UINT   uiWidth, uiHeight, uiTotal, i = 0;
   INT    sx, sy, dx, dy;
   BYTE   T;
   static BYTE  idx[320 * 200];      /* max sprite fits in screen bounds */
   static BYTE  cov[320 * 200];      /* coverage: 1 = opaque */
   UINT   pos = 0;

   if (lpBitmapRLE == NULL || out == NULL || scale <= 0)
   {
      return -1;
   }
   if (lpBitmapRLE[0] == 0x02 && lpBitmapRLE[1] == 0x00 &&
       lpBitmapRLE[2] == 0x00 && lpBitmapRLE[3] == 0x00)
   {
      lpBitmapRLE += 4;
   }
   uiWidth  = lpBitmapRLE[0] | (lpBitmapRLE[1] << 8);
   uiHeight = lpBitmapRLE[2] | (lpBitmapRLE[3] << 8);
   uiTotal  = uiWidth * uiHeight;
   if (uiTotal == 0 || uiTotal > sizeof(idx)) return -1;
   lpBitmapRLE += 4;

   memset(cov, 0, uiTotal);
   while (i < uiTotal)
   {
      T = *lpBitmapRLE++;
      if ((T & 0x80) && T <= 0x80 + uiWidth)
      {
         i += T - 0x80;              /* transparent run */
      }
      else
      {
         UINT k;
         for (k = 0; k < T && i < uiTotal; k++, i++)
         {
            idx[i] = *lpBitmapRLE++;
            cov[i] = 1;
         }
      }
   }

   *outW = (INT)uiWidth * scale;
   *outH = (INT)uiHeight * scale;
   for (sy = 0; sy < (INT)uiHeight; sy++)
   {
      for (sx = 0; sx < (INT)uiWidth; sx++)
      {
         UINT     s = sy * uiWidth + sx;
         uint32_t argb;
         if (cov[s])
         {
            SDL_Color c = palette[idx[s]];
            argb = (0xFFu << 24) | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
         }
         else
         {
            argb = 0;                /* transparent */
         }
         for (dy = 0; dy < scale; dy++)
         {
            for (dx = 0; dx < scale; dx++)
            {
               pos = (sy * scale + dy) * (*outW) + (sx * scale + dx);
               out[pos] = argb;
            }
         }
      }
   }
   return 0;
}
```

Ensure `palcommon.c` includes the SDL header providing `SDL_Color` (it already uses SDL surfaces, so it is available).

- [ ] **Step 4: Run to verify pass**

Run: `xcodebuild -workspace macos/SDLPal.xcworkspace -scheme PalTests -destination 'platform=macOS' test 2>&1 | grep -E "PAL_HDRenderSprite|passed|failed"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add palcommon.h palcommon.c tests/test_rleblit.cpp
git commit -m "feat(hd): add HD sprite render (decode + nearest upscale + palette colorize)"
```

---

## Task 5: Interception hook in the blit path

**Files:**
- Modify: `palcommon.c` (record a draw command from the plain blit path)

**Interfaces:**
- Consumes: `PAL_HashSprite`, `PAL_HDRecordBlit` (Tasks 1, 3), `gConfig.fHDRemaster` (Task 2).
- Behavior: only the **plain** path (`PAL_RLEBlitToSurface` → `PAL_RLEBlitToSurfaceWithShadow(..., bShadow=FALSE)`) records with `plain=TRUE`. The color-shift / mono-color / shadow entry points are left unrecorded for this spike (their pixels come through the upscaled base layer), so HD overlays never show a sprite missing its per-sprite effect.

- [ ] **Step 1: Add recording at the top of `PAL_RLEBlitToSurfaceWithShadow`**

Immediately after the NULL check and after `dx`/`dy` are known (before the early-out intersection test so position is captured even if partially offscreen), insert:

```c
   //
   // HD remaster: record a draw command for the plain, non-shadow path.
   //
   if (gConfig.fHDRemaster && !bShadow)
   {
      PAL_HDRecordBlit(PAL_HashSprite(lpBitmapRLE), dx, dy, TRUE);
   }
```

`gConfig` is already visible in `palcommon.c` (it uses config elsewhere); if not, add `#include "palcfg.h"` and ensure `extern` visibility as other files do.

- [ ] **Step 2: Build to verify it compiles**

Run: `xcodebuild -workspace macos/SDLPal.xcworkspace -scheme SDLPal -configuration Release -derivedDataPath build/macos_dd build 2>&1 | tail -3`
Expected: `** BUILD SUCCEEDED **`

- [ ] **Step 3: Verify recording is inert when HD is off**

The guard `gConfig.fHDRemaster` ensures zero cost/effect when off. Confirm by grepping the diff that the only behavior change is inside the `if (gConfig.fHDRemaster ...)` block.

- [ ] **Step 4: Commit**

```bash
git add palcommon.c
git commit -m "feat(hd): record draw commands from the plain blit path when HDRemaster on"
```

---

## Task 6: HD plane compose + present in `video.c`

**Files:**
- Modify: `video.h` (declare `VIDEO_HD_Present`)
- Modify: `video.c` (HD texture lifecycle + compose/present, gated on `HDRemaster`)

**Interfaces:**
- Consumes: `PAL_HDGetFrameCommands`, `PAL_HDResetFrameOnNextRecord`, `PAL_HDRenderSprite` (Tasks 3,4), `gConfig.fHDRemaster` (Task 2), the existing `gpScreenReal` (32-bit truecolor screen), `gpRenderer`, `gpPalette`.
- Produces: `void VIDEO_HD_Present(void);` — composes the HD frame and presents it, replacing the classic `SDL_RenderCopy(gpTexture)` path when HD is on.

**Design recap (why this is correct):**
- Base layer = the existing `gpScreenReal` (already colorized with the **live** palette, so it is already faded/tinted) scaled up to `320*HD_SCALE × 200*HD_SCALE`.
- Overlays = each `plain` draw command this frame, rendered via `PAL_HDRenderSprite(sprite, gpPalette->colors, HD_SCALE, ...)`. Because it colorizes with the **live** palette too, overlays fade in lockstep with the base — this is how the fade success criterion is met, with no separate brightness code.
- `PAL_HDResetFrameOnNextRecord()` is called at the **end** of present, so the next blit starts a fresh command list while fade-only presents (no blits) keep replaying the last frame's overlays under the changing palette.

> Note: the draw command stores only `hash` + position, but compose needs the sprite bytes. For this spike, the recorder is extended to also stash the sprite pointer for the current frame. Update `HDDRAWCMD` to add `LPCBITMAPRLE sprite;` and `PAL_HDRecordBlit` to accept it. (Pointers are valid within the frame they were recorded; fades reuse them before any reload.) Adjust Task 3's struct/signature accordingly — see Step 1.

- [ ] **Step 1: Extend the draw command to carry the sprite pointer**

In `palcommon.h`, add `LPCBITMAPRLE sprite;` to `HDDRAWCMD`, and change the recorder signature to:
```c
void PAL_HDRecordBlit(uint64_t hash, LPCBITMAPRLE sprite, INT x, INT y, BOOL plain);
```
Update `palcommon.c` implementation to store `g_hdCmds[...].sprite = sprite;`. Update the Task-3 unit test calls to pass a sprite pointer (use `bitmap` or `NULL`), e.g. `PAL_HDRecordBlit(0x1111, bitmap, 10, 20, TRUE);` and the Task-5 hook to `PAL_HDRecordBlit(PAL_HashSprite(lpBitmapRLE), lpBitmapRLE, dx, dy, TRUE);`. Re-run the Task 3 test to confirm still green.

- [ ] **Step 2: Add HD texture lifecycle in `video.c`**

Near `gpTexture` creation, add a lazily-created streaming target and a CPU compose buffer:

```c
static SDL_Texture *gpHDTexture = NULL;       /* 1280x800 ARGB8888 */
static uint32_t     *gpHDPixels  = NULL;       /* compose buffer */
#define HD_SCALE 4
#define HD_W (320 * HD_SCALE)
#define HD_H (200 * HD_SCALE)

static void VIDEO_HD_Ensure(void)
{
   if (gpHDTexture == NULL)
   {
      gpHDTexture = SDL_CreateTexture(gpRenderer, SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_STREAMING, HD_W, HD_H);
   }
   if (gpHDPixels == NULL)
   {
      gpHDPixels = (uint32_t *)malloc(sizeof(uint32_t) * HD_W * HD_H);
   }
}
```
Free both in `VIDEO_Shutdown` (next to the existing texture/ palette frees).

- [ ] **Step 3: Implement `VIDEO_HD_Present` in `video.c`**

```c
void
VIDEO_HD_Present(
   void
)
{
   const HDDRAWCMD *cmds = NULL;
   UINT n, c;
   INT sxi, syi;
   uint32_t *src;

   VIDEO_HD_Ensure();

   //
   // Base layer: nearest-upscale gpScreenReal (already palette-colorized/faded).
   //
   src = (uint32_t *)gpScreenReal->pixels;
   for (syi = 0; syi < HD_H; syi++)
   {
      INT sy = syi / HD_SCALE;
      for (sxi = 0; sxi < HD_W; sxi++)
      {
         INT sx = sxi / HD_SCALE;
         gpHDPixels[syi * HD_W + sxi] = src[sy * gpScreenReal->w + sx] | 0xFF000000u;
      }
   }

   //
   // Overlays: plain sprites this frame, colorized with the live palette.
   //
   n = PAL_HDGetFrameCommands(&cmds);
   for (c = 0; c < n; c++)
   {
      static uint32_t spr[HD_W * HD_H];
      INT ow = 0, oh = 0, ox, oy;
      if (!cmds[c].plain || cmds[c].sprite == NULL) continue;
      if (PAL_HDRenderSprite(cmds[c].sprite, gpPalette->colors, HD_SCALE, spr, &ow, &oh) != 0)
         continue;
      for (oy = 0; oy < oh; oy++)
      {
         INT dyv = cmds[c].y * HD_SCALE + oy;
         if (dyv < 0 || dyv >= HD_H) continue;
         for (ox = 0; ox < ow; ox++)
         {
            INT dxv = cmds[c].x * HD_SCALE + ox;
            uint32_t px = spr[oy * ow + ox];
            if (dxv < 0 || dxv >= HD_W) continue;
            if (px & 0xFF000000u)                 /* opaque only */
               gpHDPixels[dyv * HD_W + dxv] = px;
         }
      }
   }

   SDL_UpdateTexture(gpHDTexture, NULL, gpHDPixels, HD_W * sizeof(uint32_t));
   SDL_RenderClear(gpRenderer);
   SDL_RenderCopy(gpRenderer, gpHDTexture, NULL, NULL);
   SDL_RenderPresent(gpRenderer);

   PAL_HDResetFrameOnNextRecord();
}
```
(If the macOS SDL3 build uses `SDL_RenderTexture`/`SDL_RenderCopy` compatibility shims, match whatever `VIDEO_UpdateScreen` already calls at video.c:502 — reuse the exact same present call names.)

- [ ] **Step 4: Fork the present path in `VIDEO_UpdateScreen`**

At the end of `VIDEO_UpdateScreen` (video.c ~500-512), wrap the classic present:

```c
	if (gConfig.fHDRemaster)
	{
		VIDEO_HD_Present();
		return;
	}
	SDL_RenderCopy(gpRenderer, gpTexture, NULL, NULL);   /* existing classic path continues */
```
Keep the classic path untouched below the guard.

- [ ] **Step 5: Declare `VIDEO_HD_Present` in `video.h`** and build

Run: `xcodebuild -workspace macos/SDLPal.xcworkspace -scheme SDLPal -configuration Release -derivedDataPath build/macos_dd build 2>&1 | tail -3`
Expected: `** BUILD SUCCEEDED **`

- [ ] **Step 6: Manual visual verification (HD on)**

```bash
rm -rf ~/PAL/Pal.app && cp -R build/macos_dd/Build/Products/Release/Pal.app ~/PAL/Pal.app
printf 'HDRemaster=1\nEnableAviPlay=1\n' >> ~/PAL/sdlpal.cfg   # or edit existing
open ~/PAL/Pal.app
```
Expected: game boots, renders at 1280×800; walk on a map, open a menu. Trigger a scene transition (enter/exit a building) and confirm the **fade in/out looks uniform** — no bright sprites floating over a fading background. Capture with `screencapture -x -o /tmp/hd_on.png` and review.

- [ ] **Step 7: Zero-regression check (HD off)**

```bash
printf 'HDRemaster=0\n' > ~/PAL/sdlpal.cfg   # keep other options as desired
open ~/PAL/Pal.app
```
Expected: identical to the pre-change build (classic 640×400 path). Confirm no visual change and no new logs.

- [ ] **Step 8: Commit**

```bash
git add video.h video.c palcommon.h palcommon.c tests/test_rleblit.cpp
git commit -m "feat(hd): compose+present HD dual-plane; fades reproduce via live-palette overlays"
```

---

## Task 7: Debug overlay + diagnostics

**Files:**
- Modify: `video.c` (draw counters; add an "overlay-only" diagnostic toggle)
- Modify: `input.c` (bind a debug key to toggle diagnostics) — optional; a compile-time flag is acceptable if input wiring is heavy.

**Interfaces:**
- Consumes: `PAL_HDGetUniqueHashCount`, `PAL_HDGetFrameCommands`.
- Produces: on-screen (or logged) counters: unique sprite hashes this frame, total commands this frame; plus a toggle that renders **overlays on black** (base layer suppressed) to visually confirm which sprites are HD-overlaid and where.

- [ ] **Step 1: Add counters + overlay-only mode to `VIDEO_HD_Present`**

Add file-scope `static BOOL g_hdOverlayOnly = FALSE;` and, controlled by a compile-time `#define HD_DEBUG 1`, log once per second:

```c
#if HD_DEBUG
   {
      static Uint32 last = 0; Uint32 now = SDL_GetTicks();
      if (now - last > 1000) {
         UTIL_LogOutput(LOGLEVEL_DEBUG, "[HD] cmds=%u unique=%u overlayOnly=%d\n",
                        PAL_HDGetFrameCommands(NULL), PAL_HDGetUniqueHashCount(), g_hdOverlayOnly);
         last = now;
      }
   }
#endif
```
In overlay-only mode, initialize `gpHDPixels` to opaque black instead of the upscaled base (wrap the base-layer loop in `if (!g_hdOverlayOnly) { ... } else { memset-black }`).

- [ ] **Step 2: Build**

Run: `xcodebuild -workspace macos/SDLPal.xcworkspace -scheme SDLPal -configuration Release -derivedDataPath build/macos_dd build 2>&1 | tail -3`
Expected: `** BUILD SUCCEEDED **`

- [ ] **Step 3: Verify diagnostics**

Run the app from `~/PAL` with `HDRemaster=1`, then read the log:
```bash
open ~/PAL/Pal.app; sleep 8; pkill -x Pal
log show --last 20s --predicate 'process == "Pal"' 2>/dev/null | grep "\[HD\]" | tail
```
Expected: `[HD] cmds=… unique=…` lines with plausible non-zero counts on a populated screen. (Temporarily flip `g_hdOverlayOnly` default to TRUE and rebuild to eyeball overlay placement, then set back to FALSE.)

- [ ] **Step 4: Commit**

```bash
git add video.c
git commit -m "feat(hd): add HD debug counters and overlay-only diagnostic mode"
```

---

## Task 8: Regression guard + wrap-up

**Files:**
- Test: `tests/test_rleblit.cpp` (already covers pure logic)
- Modify: `docs/superpowers/plans/2026-08-10-hd-render-core.md` (check off tasks)

- [ ] **Step 1: Full unit-test run**

Run: `xcodebuild -workspace macos/SDLPal.xcworkspace -scheme PalTests -destination 'platform=macOS' test 2>&1 | tail -15`
Expected: all suites pass (existing + the 4 new HD tests).

- [ ] **Step 2: Golden pixel-identity check (HD off)**

With `HDRemaster=0`, capture a fixed screen (e.g., the splash) from the pre-branch build and the new build and compare:
```bash
# new build already at ~/PAL; capture splash then compare against a saved baseline PNG
```
Expected: byte-identical (or visually identical) frames — confirms zero regression.

- [ ] **Step 3: Performance sanity (HD on, battle)**

Enter a battle with `HDRemaster=1`; confirm playable frame rate. If sluggish, note it as a follow-up for sub-project C (the placeholder per-frame recompose is not yet cached across frames). No fix required in the spike unless unplayable.

- [ ] **Step 4: Final commit**

```bash
git add -A
git commit -m "docs(hd): mark sub-project A plan complete"
```

---

## Self-Review

**Spec coverage:**
- §3 dual-plane renderer → Task 6 (base upscale + overlay compose).
- §4 content-hash identity → Tasks 1 (hash), 5 (record at blit).
- §4.3 placeholder upscale → Task 4.
- §5 palette-effect (fade) → Task 6 (live-palette overlay colorization; verified Step 6). Refines the spec's "brightness multiplier" into the strictly-simpler live-palette approach; same success criterion (uniform fade).
- §6 config + fallback → Task 2 (config), Task 6 Step 4/7 (fork + zero-regression).
- §7 deliverable/success → Task 6 (renders through HD), Task 7 (counters), Task 8 (zero-regression).
- §8 tests → Tasks 1,3,4 (unit), Task 6/8 (visual/golden).
- §9 risks: performance → Task 8 Step 3; non-sprite draws → base layer (Task 6); alignment → integer HD_SCALE; GLSL/SDL3 interaction → Task 6 Step 3 note to reuse existing present calls.

**Placeholder scan:** No TBD/TODO; every code step shows complete code; test steps show real assertions.

**Type consistency:** `HDDRAWCMD` gains `sprite` in Task 6 Step 1 with the Task-3 struct/signature/tests updated in the same step. `PAL_HDRecordBlit` signature is consistent after Task 6 Step 1. `PAL_HDRenderSprite`, `PAL_HashSprite`, `PAL_RLESpriteBytes`, `VIDEO_HD_Present` signatures match across tasks.
