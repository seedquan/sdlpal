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
