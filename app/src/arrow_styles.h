#ifndef ARROW_STYLES_H
#define ARROW_STYLES_H

#include "constants.h"

/* Nineteen arrows packed 3-per-row in a 768px-wide atlas (six full rows plus
   a seventh row holding the last one). Each 256px cell has an 8px
   transparent gutter to keep neighbouring sprites out of generated mipmaps. */
#define ARROW_ATLAS_WIDTH 768.0f
#define ARROW_ATLAS_HEIGHT 1792.0f

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

/* Real width/height of each design. The atlas stores every cell square and
   this restores each one's real shape. Styles 0 (redarrow.webp, 612x408) and
   9 (714x497) came as non-square images; styles 10-18 are the newer designs,
   each cropped to its own bounding box and stretched to fill its cell. */
static inline float arrow_style_aspect(int style) {
  static const float k_aspect[ARROW_STYLE_COUNT] = {
      1.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.44f,
      1.571f, 1.472f, 1.616f, 1.646f, 1.393f, 0.996f, 1.681f, 1.603f, 1.000f};
  if (style < 0 || style >= ARROW_STYLE_COUNT) return 1.0f;
  return k_aspect[style];
}

#endif
