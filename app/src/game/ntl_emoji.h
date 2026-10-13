#ifndef NTL_EMOJI_H
#define NTL_EMOJI_H

/* Colour emoji for team minimap dots.
 *
 * The emoji pictures live in app/res/textures/emoji_sheet.png (a grid of
 * 16 columns x 10 rows, 64px cells, cut from Noto Color Emoji).
 * The picked emoji travels in the existing one-byte "shape" field of the
 * location packet: 0..5 are the classic vector shapes, and
 * NTL_SHAPE_EMOJI_BASE + n is sprite n. Builds that don't know about emoji
 * simply draw a circle for those values. */

#define NTL_SHAPE_EMOJI_BASE 16
#define NTL_EMOJI_COUNT 159
#define NTL_EMOJI_COLS 16
#define NTL_EMOJI_ROWS 10

#define NTL_SHAPE_VALID(s) \
  ((((s) >= 0) && ((s) <= 5)) || \
   (((s) >= NTL_SHAPE_EMOJI_BASE) && \
    ((s) < NTL_SHAPE_EMOJI_BASE + NTL_EMOJI_COUNT)))

/* Called by the renderer once the sprite sheet is uploaded
 * (a Vulkan descriptor set cast to an integer; 0 = not available). */
void ntl_emoji_set_texture(unsigned long long texture_id);

#endif
