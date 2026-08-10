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
