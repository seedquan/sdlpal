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
