/*
 * Minimal symbol stubs for the standalone unit-test harness
 * (tests/run-standalone.sh). These satisfy references pulled in by
 * palcommon.c without linking the whole game. The unit tests never
 * exercise the code paths that use them.
 */
#include "palcfg.h"

CONFIGURATION gConfig;          /* real type, zero-initialized */
INT (*Decompress)(LPCVOID, LPVOID, INT) = 0;   /* global.c function pointer; unused by tests */
void *gpRenderer = 0;           /* video.c SDL_Renderer*; unused by tests */
void *gAudioDevice[8] = {0};    /* audio.c device state; unused by tests */

/* video.c / video_glsl.c scale-mode helpers referenced by sdl_compat.c */
int VIDEO_GetScaleMode(void) { return 0; }
int VIDEO_GLSL_GetScaleMode(void) { return 0; }
