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
