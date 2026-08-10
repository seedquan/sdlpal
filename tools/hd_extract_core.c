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
