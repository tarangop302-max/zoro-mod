#ifndef WEBP_DECODE_H
#define WEBP_DECODE_H

/* Tiny self-contained WebP decoder (no libwebp dependency).
 *
 * Supports what NTL tag images use, and then some:
 *   - lossy   (VP8 key frame: all intra modes, segments, loop filter)
 *   - lossless (VP8L: all four transforms, colour cache, LZ77)
 *   - VP8X extended files with an ALPH alpha plane (raw or VP8L-compressed,
 *     with the horizontal / vertical / gradient un-filtering)
 * Animated WebP is not supported (the first frame is not extracted).
 *
 * Output is straight (non-premultiplied) RGBA8, w*h*4 bytes, malloc'ed.
 * Free it with free(). Returns false on any malformed / unsupported input and
 * never reads outside [data, data+len). */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

bool webp_decode_rgba(const uint8_t *data, size_t len, uint8_t **out_rgba,
                      int *out_w, int *out_h);

/* Only reads the header: image size without decoding. */
bool webp_get_size(const uint8_t *data, size_t len, int *out_w, int *out_h);

#endif
