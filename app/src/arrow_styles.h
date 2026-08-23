#ifndef ARROW_STYLES_H
#define ARROW_STYLES_H

#include "constants.h"

/* Ten arrows packed 3-per-row in a 768px-wide atlas (three full rows plus
   a fourth row holding just the newest design). Each 256px cell has an 8px
   transparent gutter to keep neighbouring sprites out of generated mipmaps. */
#define ARROW_ATLAS_WIDTH 768.0f
#define ARROW_ATLAS_HEIGHT 1024.0f

static inline void arrow_style_uv_bounds(int style, float* u0, float* v0,
                                         float* u1, float* v1) {
  if (style < 0 || style >= ARROW_STYLE_COUNT) style = 0;
  int column = style % 3;
  int row = style / 3;
  *u0 = (column * 256.0f + 8.0f) / ARROW_ATLAS_WIDTH;
  *v0 = (row * 256.0f + 8.0f) / ARROW_ATLAS_HEIGHT;
  *u1 = (column * 256.0f + 248.0f) / ARROW_ATLAS_WIDTH;
  *v1 = (row * 256.0f + 248.0f) / ARROW_ATLAS_HEIGHT;
}

/* redarrow.webp was supplied as 612x408, and the newest design (index 9)
   was supplied as 714x497; the remaining source canvases are square. The
   atlas stores every cell square and this restores each one's real shape. */
static inline float arrow_style_aspect(int style) {
  if (style == 0) return 1.5f;
  if (style == 9) return 1.44f;
  return 1.0f;
}

#endif
