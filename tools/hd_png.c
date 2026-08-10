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
