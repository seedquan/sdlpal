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
