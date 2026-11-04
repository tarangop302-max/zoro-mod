#include "webp_decode.h"

#include <stdlib.h>
#include <string.h>

#include "webp_tables.h"

/* ======================================================================== *
 *  Lossy: VP8 key frame (RFC 6386)
 * ======================================================================== */

typedef struct {
  const uint8_t *p, *end;
  uint32_t value, range;
  int bits;
  int eof;
} bd_t;

static int bd_byte(bd_t *b) {
  if (b->p < b->end) return *b->p++;
  b->eof = 1;
  return 0;
}
static void bd_init(bd_t *b, const uint8_t *p, size_t n) {
  b->p = p;
  b->end = p + n;
  b->eof = 0;
  b->value = 0;
  b->value = (uint32_t)bd_byte(b) << 8;
  b->value |= (uint32_t)bd_byte(b);
  b->range = 255;
  b->bits = 0;
}
static int bd_get(bd_t *b, int prob) {
  uint32_t split = 1 + (((b->range - 1) * (uint32_t)prob) >> 8);
  uint32_t S = split << 8;
  int bit;
  if (b->value >= S) {
    bit = 1;
    b->range -= split;
    b->value -= S;
  } else {
    bit = 0;
    b->range = split;
  }
  while (b->range < 128) {
    b->value <<= 1;
    b->range <<= 1;
    if (++b->bits == 8) {
      b->bits = 0;
      b->value |= (uint32_t)bd_byte(b);
    }
  }
  return bit;
}
static int bd_bits(bd_t *b, int n) {
  int v = 0;
  while (n-- > 0) v |= bd_get(b, 128) << n;
  return v;
}
static int bd_signed(bd_t *b, int n) {
  int v = bd_bits(b, n);
  return bd_get(b, 128) ? -v : v;
}

enum { B_DC = 0, B_TM, B_VE, B_HE, B_RD, B_VR, B_LD, B_VL, B_HD, B_HU };

static const int8_t kYModesIntra4[18] = {-B_DC, 1,    -B_TM, 2,    -B_VE, 3,
                                         4,     6,    -B_HE, 5,    -B_RD, -B_VR,
                                         -B_LD, 7,    -B_VL, 8,    -B_HD, -B_HU};
static const uint8_t kZigzag[16] = {0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15};
static const uint8_t kBands[17] = {0, 1, 2, 3, 6, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 7, 0};
static const uint8_t kCat3[] = {173, 148, 140, 0};
static const uint8_t kCat4[] = {176, 155, 140, 135, 0};
static const uint8_t kCat5[] = {180, 157, 141, 134, 130, 0};
static const uint8_t kCat6[] = {254, 254, 243, 230, 196, 177, 153, 140, 133, 130, 129, 0};
static const uint8_t *const kCat3456[] = {kCat3, kCat4, kCat5, kCat6};

#define BPS 32
#define Y_OFF (BPS * 1 + 8)
#define U_OFF (Y_OFF + BPS * 16 + BPS)
#define V_OFF (U_OFF + 16)
#define WORK_SIZE (BPS * 17 + BPS * 9)

typedef struct {
  uint8_t is_i4, segment, skip, uvmode;
  uint8_t imodes[16];
} mbinfo_t;

typedef struct {
  int y1[2], y2[2], uv[2];
} quant_t;

typedef struct {
  uint8_t limit, ilevel, hev, inner;
} finfo_t;

static int clip_i(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static uint8_t clip8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

static int get_large(bd_t *br, const uint8_t *p) {
  int v;
  if (!bd_get(br, p[3])) {
    if (!bd_get(br, p[4]))
      v = 2;
    else
      v = 3 + bd_get(br, p[5]);
  } else {
    if (!bd_get(br, p[6])) {
      if (!bd_get(br, p[7])) {
        v = 5 + bd_get(br, 159);
      } else {
        v = 7 + 2 * bd_get(br, 165);
        v += bd_get(br, 145);
      }
    } else {
      int bit1 = bd_get(br, p[8]);
      int bit0 = bd_get(br, p[9 + bit1]);
      int cat = 2 * bit1 + bit0;
      const uint8_t *tab;
      v = 0;
      for (tab = kCat3456[cat]; *tab; ++tab) v += v + bd_get(br, *tab);
      v += 3 + (8 << cat);
    }
  }
  return v;
}

static int get_coeffs(bd_t *br, const uint8_t proba[8][3][11], int ctx,
                      const int dq[2], int n, int16_t *out) {
  const uint8_t *p = proba[kBands[n]][ctx];
  for (; n < 16; ++n) {
    if (!bd_get(br, p[0])) return n;
    while (!bd_get(br, p[1])) {
      p = proba[kBands[++n]][0];
      if (n == 16) return 16;
    }
    {
      const uint8_t(*pc)[11] = proba[kBands[n + 1]];
      int v;
      if (!bd_get(br, p[2])) {
        v = 1;
        p = pc[1];
      } else {
        v = get_large(br, p);
        p = pc[2];
      }
      {
        int neg = bd_get(br, 128);
        out[kZigzag[n]] = (int16_t)((neg ? -v : v) * dq[n > 0]);
      }
    }
  }
  return 16;
}

/* ---- transforms ---- */
#define MUL1(a) ((((a)*20091) >> 16) + (a))
#define MUL2(a) (((a)*35468) >> 16)

static void idct4(const int16_t *in, uint8_t *dst) {
  int C[16], *tmp = C, i;
  for (i = 0; i < 4; ++i) {
    int a = in[0] + in[8];
    int b = in[0] - in[8];
    int c = MUL2(in[4]) - MUL1(in[12]);
    int d = MUL1(in[4]) + MUL2(in[12]);
    tmp[0] = a + d;
    tmp[1] = b + c;
    tmp[2] = b - c;
    tmp[3] = a - d;
    tmp += 4;
    in++;
  }
  tmp = C;
  for (i = 0; i < 4; ++i) {
    int dc = tmp[0] + 4;
    int a = dc + tmp[8];
    int b = dc - tmp[8];
    int c = MUL2(tmp[4]) - MUL1(tmp[12]);
    int d = MUL1(tmp[4]) + MUL2(tmp[12]);
    dst[0] = clip8(dst[0] + ((a + d) >> 3));
    dst[1] = clip8(dst[1] + ((b + c) >> 3));
    dst[2] = clip8(dst[2] + ((b - c) >> 3));
    dst[3] = clip8(dst[3] + ((a - d) >> 3));
    tmp++;
    dst += BPS;
  }
}

static void iwht(const int16_t *in, int16_t *out) {
  int tmp[16], i;
  for (i = 0; i < 4; ++i) {
    int a0 = in[0 + i] + in[12 + i];
    int a1 = in[4 + i] + in[8 + i];
    int a2 = in[4 + i] - in[8 + i];
    int a3 = in[0 + i] - in[12 + i];
    tmp[0 + i] = a0 + a1;
    tmp[8 + i] = a0 - a1;
    tmp[4 + i] = a3 + a2;
    tmp[12 + i] = a3 - a2;
  }
  for (i = 0; i < 4; ++i) {
    int dc = tmp[0 + i * 4] + 3;
    int a0 = dc + tmp[3 + i * 4];
    int a1 = tmp[1 + i * 4] + tmp[2 + i * 4];
    int a2 = tmp[1 + i * 4] - tmp[2 + i * 4];
    int a3 = dc - tmp[3 + i * 4];
    out[0] = (int16_t)((a0 + a1) >> 3);
    out[16] = (int16_t)((a3 + a2) >> 3);
    out[32] = (int16_t)((a0 - a1) >> 3);
    out[48] = (int16_t)((a3 - a2) >> 3);
    out += 64;
  }
}

/* ---- intra prediction (operates inside the BPS work buffer) ---- */
#define AVG3(a, b, c) ((uint8_t)(((a) + 2 * (b) + (c) + 2) >> 2))
#define AVG2(a, b) ((uint8_t)(((a) + (b) + 1) >> 1))
#define DST(x, y) dst[(x) + (y)*BPS]

static void fill(uint8_t *dst, int size, int v) {
  for (int j = 0; j < size; ++j) memset(dst + j * BPS, v, (size_t)size);
}
static void pred_tm(uint8_t *dst, int size) {
  const uint8_t *top = dst - BPS;
  int tl = top[-1];
  for (int y = 0; y < size; ++y) {
    int l = dst[-1] - tl;
    for (int x = 0; x < size; ++x) dst[x] = clip8(top[x] + l);
    dst += BPS;
  }
}
static void pred_ve(uint8_t *dst, int size) {
  for (int j = 0; j < size; ++j) memcpy(dst + j * BPS, dst - BPS, (size_t)size);
}
static void pred_he(uint8_t *dst, int size) {
  for (int j = 0; j < size; ++j) memset(dst + j * BPS, dst[j * BPS - 1], (size_t)size);
}
/* variant: 0 = DC, 1 = NoTop, 2 = NoLeft, 3 = NoTopLeft */
static void pred_dc(uint8_t *dst, int size, int variant) {
  int dc = 0, shift = size == 16 ? 4 : 3;
  if (variant == 0) {
    for (int j = 0; j < size; ++j) dc += dst[-1 + j * BPS] + dst[j - BPS];
    dc += size;
    dc >>= shift + 1;
  } else if (variant == 1) {
    for (int j = 0; j < size; ++j) dc += dst[-1 + j * BPS];
    dc += size >> 1;
    dc >>= shift;
  } else if (variant == 2) {
    for (int j = 0; j < size; ++j) dc += dst[j - BPS];
    dc += size >> 1;
    dc >>= shift;
  } else {
    dc = 0x80;
  }
  fill(dst, size, dc);
}

static void pred4(uint8_t *dst, int mode) {
  const uint8_t *top = dst - BPS;
  int X = dst[-1 - BPS], I = dst[-1], J = dst[-1 + BPS], K = dst[-1 + 2 * BPS],
      L = dst[-1 + 3 * BPS];
  int A = top[0], B = top[1], C = top[2], D = top[3], E = top[4], F = top[5],
      G = top[6], H = top[7];
  switch (mode) {
    case B_DC: {
      int dc = 4;
      for (int i = 0; i < 4; ++i) dc += dst[i - BPS] + dst[-1 + i * BPS];
      dc >>= 3;
      fill(dst, 4, dc);
      break;
    }
    case B_TM: pred_tm(dst, 4); break;
    case B_VE: {
      uint8_t v[4] = {AVG3(X, A, B), AVG3(A, B, C), AVG3(B, C, D), AVG3(C, D, E)};
      for (int j = 0; j < 4; ++j) memcpy(dst + j * BPS, v, 4);
      break;
    }
    case B_HE: {
      uint8_t r[4] = {AVG3(X, I, J), AVG3(I, J, K), AVG3(J, K, L), AVG3(K, L, L)};
      for (int j = 0; j < 4; ++j) memset(dst + j * BPS, r[j], 4);
      break;
    }
    case B_RD:
      DST(0, 3) = AVG3(J, K, L);
      DST(1, 3) = DST(0, 2) = AVG3(I, J, K);
      DST(2, 3) = DST(1, 2) = DST(0, 1) = AVG3(X, I, J);
      DST(3, 3) = DST(2, 2) = DST(1, 1) = DST(0, 0) = AVG3(A, X, I);
      DST(3, 2) = DST(2, 1) = DST(1, 0) = AVG3(B, A, X);
      DST(3, 1) = DST(2, 0) = AVG3(C, B, A);
      DST(3, 0) = AVG3(D, C, B);
      break;
    case B_VR:
      DST(0, 0) = DST(1, 2) = AVG2(X, A);
      DST(1, 0) = DST(2, 2) = AVG2(A, B);
      DST(2, 0) = DST(3, 2) = AVG2(B, C);
      DST(3, 0) = AVG2(C, D);
      DST(0, 3) = AVG3(K, J, I);
      DST(0, 2) = AVG3(J, I, X);
      DST(0, 1) = DST(1, 3) = AVG3(I, X, A);
      DST(1, 1) = DST(2, 3) = AVG3(X, A, B);
      DST(2, 1) = DST(3, 3) = AVG3(A, B, C);
      DST(3, 1) = AVG3(B, C, D);
      break;
    case B_LD:
      DST(0, 0) = AVG3(A, B, C);
      DST(1, 0) = DST(0, 1) = AVG3(B, C, D);
      DST(2, 0) = DST(1, 1) = DST(0, 2) = AVG3(C, D, E);
      DST(3, 0) = DST(2, 1) = DST(1, 2) = DST(0, 3) = AVG3(D, E, F);
      DST(3, 1) = DST(2, 2) = DST(1, 3) = AVG3(E, F, G);
      DST(3, 2) = DST(2, 3) = AVG3(F, G, H);
      DST(3, 3) = AVG3(G, H, H);
      break;
    case B_VL:
      DST(0, 0) = AVG2(A, B);
      DST(1, 0) = DST(0, 2) = AVG2(B, C);
      DST(2, 0) = DST(1, 2) = AVG2(C, D);
      DST(3, 0) = DST(2, 2) = AVG2(D, E);
      DST(0, 1) = AVG3(A, B, C);
      DST(1, 1) = DST(0, 3) = AVG3(B, C, D);
      DST(2, 1) = DST(1, 3) = AVG3(C, D, E);
      DST(3, 1) = DST(2, 3) = AVG3(D, E, F);
      DST(3, 2) = AVG3(E, F, G);
      DST(3, 3) = AVG3(F, G, H);
      break;
    case B_HD:
      DST(0, 0) = DST(2, 1) = AVG2(I, X);
      DST(0, 1) = DST(2, 2) = AVG2(J, I);
      DST(0, 2) = DST(2, 3) = AVG2(K, J);
      DST(0, 3) = AVG2(L, K);
      DST(3, 0) = AVG3(A, B, C);
      DST(2, 0) = AVG3(X, A, B);
      DST(1, 0) = DST(3, 1) = AVG3(I, X, A);
      DST(1, 1) = DST(3, 2) = AVG3(J, I, X);
      DST(1, 2) = DST(3, 3) = AVG3(K, J, I);
      DST(1, 3) = AVG3(L, K, J);
      break;
    case B_HU:
      DST(0, 0) = AVG2(I, J);
      DST(2, 0) = DST(0, 1) = AVG2(J, K);
      DST(2, 1) = DST(0, 2) = AVG2(K, L);
      DST(1, 0) = AVG3(I, J, K);
      DST(3, 0) = DST(1, 1) = AVG3(J, K, L);
      DST(3, 1) = DST(1, 2) = AVG3(K, L, L);
      DST(3, 2) = DST(2, 2) = DST(0, 3) = DST(1, 3) = DST(2, 3) = DST(3, 3) = (uint8_t)L;
      break;
  }
}

/* ---- loop filter ---- */
static int sclip1(int v) { return v < -128 ? -128 : v > 127 ? 127 : v; }
static int sclip2(int v) { return v < -16 ? -16 : v > 15 ? 15 : v; }
static int iabs(int v) { return v < 0 ? -v : v; }

static void do_filter2(uint8_t *p, int step) {
  int p1 = p[-2 * step], p0 = p[-step], q0 = p[0], q1 = p[step];
  int a = 3 * (q0 - p0) + sclip1(p1 - q1);
  int a1 = sclip2((a + 4) >> 3);
  int a2 = sclip2((a + 3) >> 3);
  p[-step] = clip8(p0 + a2);
  p[0] = clip8(q0 - a1);
}
static void do_filter4(uint8_t *p, int step) {
  int p1 = p[-2 * step], p0 = p[-step], q0 = p[0], q1 = p[step];
  int a = 3 * (q0 - p0);
  int a1 = sclip2((a + 4) >> 3);
  int a2 = sclip2((a + 3) >> 3);
  int a3 = (a1 + 1) >> 1;
  p[-2 * step] = clip8(p1 + a3);
  p[-step] = clip8(p0 + a2);
  p[0] = clip8(q0 - a1);
  p[step] = clip8(q1 - a3);
}
static void do_filter6(uint8_t *p, int step) {
  int p2 = p[-3 * step], p1 = p[-2 * step], p0 = p[-step];
  int q0 = p[0], q1 = p[step], q2 = p[2 * step];
  int a = sclip1(3 * (q0 - p0) + sclip1(p1 - q1));
  int a1 = (27 * a + 63) >> 7;
  int a2 = (18 * a + 63) >> 7;
  int a3 = (9 * a + 63) >> 7;
  p[-3 * step] = clip8(p2 + a3);
  p[-2 * step] = clip8(p1 + a2);
  p[-step] = clip8(p0 + a1);
  p[0] = clip8(q0 - a1);
  p[step] = clip8(q1 - a2);
  p[2 * step] = clip8(q2 - a3);
}
static int hev(const uint8_t *p, int step, int thresh) {
  int p1 = p[-2 * step], p0 = p[-step], q0 = p[0], q1 = p[step];
  return iabs(p1 - p0) > thresh || iabs(q1 - q0) > thresh;
}
static int needs_filter(const uint8_t *p, int step, int t) {
  int p1 = p[-2 * step], p0 = p[-step], q0 = p[0], q1 = p[step];
  return 4 * iabs(p0 - q0) + iabs(p1 - q1) <= t;
}
static int needs_filter2(const uint8_t *p, int step, int t, int it) {
  int p3 = p[-4 * step], p2 = p[-3 * step], p1 = p[-2 * step];
  int p0 = p[-step], q0 = p[0];
  int q1 = p[step], q2 = p[2 * step], q3 = p[3 * step];
  if (4 * iabs(p0 - q0) + iabs(p1 - q1) > t) return 0;
  return iabs(p3 - p2) <= it && iabs(p2 - p1) <= it && iabs(p1 - p0) <= it &&
         iabs(q3 - q2) <= it && iabs(q2 - q1) <= it && iabs(q1 - q0) <= it;
}
static void simple_loop(uint8_t *p, int hstride, int vstride, int thresh) {
  int thresh2 = 2 * thresh + 1;
  for (int i = 0; i < 16; ++i)
    if (needs_filter(p + i * vstride, hstride, thresh2))
      do_filter2(p + i * vstride, hstride);
}
static void loop26(uint8_t *p, int hstride, int vstride, int size, int thresh,
                   int ithresh, int hev_thresh) {
  int thresh2 = 2 * thresh + 1;
  while (size-- > 0) {
    if (needs_filter2(p, hstride, thresh2, ithresh)) {
      if (hev(p, hstride, hev_thresh))
        do_filter2(p, hstride);
      else
        do_filter6(p, hstride);
    }
    p += vstride;
  }
}
static void loop24(uint8_t *p, int hstride, int vstride, int size, int thresh,
                   int ithresh, int hev_thresh) {
  int thresh2 = 2 * thresh + 1;
  while (size-- > 0) {
    if (needs_filter2(p, hstride, thresh2, ithresh)) {
      if (hev(p, hstride, hev_thresh))
        do_filter2(p, hstride);
      else
        do_filter4(p, hstride);
    }
    p += vstride;
  }
}

typedef struct {
  int ok;
  int w, h, mbw, mbh;
  uint8_t *y, *u, *v;
  int ys, uvs;
} vp8frame_t;

static void vp8_free(vp8frame_t *f) {
  free(f->y);
  free(f->u);
  free(f->v);
  memset(f, 0, sizeof *f);
}

static bool vp8_header_size(const uint8_t *d, size_t n, int *w, int *h) {
  if (n < 10) return false;
  if (d[0] & 1) return false; /* not a key frame */
  if (d[3] != 0x9d || d[4] != 0x01 || d[5] != 0x2a) return false;
  *w = (d[6] | (d[7] << 8)) & 0x3fff;
  *h = (d[8] | (d[9] << 8)) & 0x3fff;
  return *w > 0 && *h > 0;
}

static bool vp8_decode_frame(const uint8_t *d, size_t n, vp8frame_t *F) {
  memset(F, 0, sizeof *F);
  int w, h;
  if (!vp8_header_size(d, n, &w, &h)) return false;
  uint32_t tag = d[0] | (d[1] << 8) | (d[2] << 16);
  size_t part0_size = tag >> 5;
  if (!((tag >> 4) & 1)) { /* show_frame == 0: still decode, harmless */ }
  const uint8_t *buf = d + 10;
  size_t buf_n = n - 10;
  if (part0_size > buf_n) return false;

  int mbw = (w + 15) >> 4, mbh = (h + 15) >> 4;
  F->w = w;
  F->h = h;
  F->mbw = mbw;
  F->mbh = mbh;
  F->ys = mbw * 16;
  F->uvs = mbw * 8;
  F->y = (uint8_t *)calloc((size_t)F->ys * mbh * 16, 1);
  F->u = (uint8_t *)calloc((size_t)F->uvs * mbh * 8, 1);
  F->v = (uint8_t *)calloc((size_t)F->uvs * mbh * 8, 1);
  if (!F->y || !F->u || !F->v) {
    vp8_free(F);
    return false;
  }

  bd_t br;
  bd_init(&br, buf, part0_size);
  bd_get(&br, 128); /* colour space */
  bd_get(&br, 128); /* clamp type */

  /* segment header */
  int use_segment = bd_get(&br, 128), update_map = 0, abs_delta = 0;
  int seg_quant[4] = {0}, seg_filter[4] = {0};
  uint8_t seg_probs[3] = {255, 255, 255};
  if (use_segment) {
    update_map = bd_get(&br, 128);
    if (bd_get(&br, 128)) {
      abs_delta = bd_get(&br, 128);
      for (int s = 0; s < 4; ++s) seg_quant[s] = bd_get(&br, 128) ? bd_signed(&br, 7) : 0;
      for (int s = 0; s < 4; ++s) seg_filter[s] = bd_get(&br, 128) ? bd_signed(&br, 6) : 0;
    }
    if (update_map)
      for (int s = 0; s < 3; ++s) seg_probs[s] = bd_get(&br, 128) ? (uint8_t)bd_bits(&br, 8) : 255;
  }
  /* filter header */
  int simple = bd_get(&br, 128);
  int level = bd_bits(&br, 6);
  int sharp = bd_bits(&br, 3);
  int use_lf_delta = bd_get(&br, 128);
  int ref_lf_delta0 = 0, mode_lf_delta0 = 0;
  if (use_lf_delta) {
    if (bd_get(&br, 128)) {
      for (int i = 0; i < 4; ++i)
        if (bd_get(&br, 128)) {
          int v = bd_signed(&br, 6);
          if (i == 0) ref_lf_delta0 = v;
        }
      for (int i = 0; i < 4; ++i)
        if (bd_get(&br, 128)) {
          int v = bd_signed(&br, 6);
          if (i == 0) mode_lf_delta0 = v;
        }
    }
  }
  int filter_type = (level == 0) ? 0 : simple ? 1 : 2;

  /* partitions */
  int nparts = 1 << bd_bits(&br, 2);
  const uint8_t *sizes = buf + part0_size;
  size_t after0 = buf_n - part0_size;
  if (after0 < (size_t)3 * (nparts - 1)) {
    vp8_free(F);
    return false;
  }
  const uint8_t *pp = sizes + 3 * (nparts - 1);
  size_t left = after0 - 3 * (nparts - 1);
  bd_t parts[8];
  for (int i = 0; i < nparts; ++i) {
    size_t psz;
    if (i < nparts - 1) {
      psz = (size_t)(sizes[3 * i] | (sizes[3 * i + 1] << 8) | (sizes[3 * i + 2] << 16));
      if (psz > left) psz = left;
    } else {
      psz = left;
    }
    bd_init(&parts[i], pp, psz);
    pp += psz;
    left -= psz;
  }

  /* quantizers */
  int base_q0 = bd_bits(&br, 7);
  int dqy1_dc = bd_get(&br, 128) ? bd_signed(&br, 4) : 0;
  int dqy2_dc = bd_get(&br, 128) ? bd_signed(&br, 4) : 0;
  int dqy2_ac = bd_get(&br, 128) ? bd_signed(&br, 4) : 0;
  int dquv_dc = bd_get(&br, 128) ? bd_signed(&br, 4) : 0;
  int dquv_ac = bd_get(&br, 128) ? bd_signed(&br, 4) : 0;
  quant_t q[4];
  for (int i = 0; i < 4; ++i) {
    int qq;
    if (use_segment) {
      qq = seg_quant[i];
      if (!abs_delta) qq += base_q0;
    } else {
      if (i > 0) {
        q[i] = q[0];
        continue;
      }
      qq = base_q0;
    }
    q[i].y1[0] = vp8_dc_table[clip_i(qq + dqy1_dc, 0, 127)];
    q[i].y1[1] = vp8_ac_table[clip_i(qq, 0, 127)];
    q[i].y2[0] = vp8_dc_table[clip_i(qq + dqy2_dc, 0, 127)] * 2;
    q[i].y2[1] = (vp8_ac_table[clip_i(qq + dqy2_ac, 0, 127)] * 101581) >> 16;
    if (q[i].y2[1] < 8) q[i].y2[1] = 8;
    q[i].uv[0] = vp8_dc_table[clip_i(qq + dquv_dc, 0, 117)];
    q[i].uv[1] = vp8_ac_table[clip_i(qq + dquv_ac, 0, 127)];
  }

  bd_get(&br, 128); /* refresh entropy probs (ignored on key frames) */

  /* token probabilities */
  uint8_t proba[4][8][3][11];
  for (int t = 0; t < 4; ++t)
    for (int b = 0; b < 8; ++b)
      for (int c = 0; c < 3; ++c)
        for (int p = 0; p < 11; ++p) {
          int idx = ((t * 8 + b) * 3 + c) * 11 + p;
          proba[t][b][c][p] = bd_get(&br, vp8_coeffs_update[idx])
                                  ? (uint8_t)bd_bits(&br, 8)
                                  : vp8_coeffs_proba0[idx];
        }
  int use_skip = bd_get(&br, 128);
  int skip_p = use_skip ? bd_bits(&br, 8) : 0;

  /* loop-filter strengths per segment / mode */
  finfo_t fstr[4][2];
  memset(fstr, 0, sizeof fstr);
  if (filter_type > 0) {
    for (int s = 0; s < 4; ++s) {
      int base_level;
      if (use_segment) {
        base_level = seg_filter[s];
        if (!abs_delta) base_level += level;
      } else {
        base_level = level;
      }
      for (int i4 = 0; i4 <= 1; ++i4) {
        int lv = base_level;
        if (use_lf_delta) {
          lv += ref_lf_delta0;
          if (i4) lv += mode_lf_delta0;
        }
        lv = clip_i(lv, 0, 63);
        finfo_t *fi = &fstr[s][i4];
        if (lv > 0) {
          int il = lv;
          if (sharp > 0) {
            il >>= (sharp > 4) ? 2 : 1;
            if (il > 9 - sharp) il = 9 - sharp;
          }
          if (il < 1) il = 1;
          fi->ilevel = (uint8_t)il;
          fi->limit = (uint8_t)(2 * lv + il);
          fi->hev = (uint8_t)((lv >= 40) ? 2 : (lv >= 15) ? 1 : 0);
        } else {
          fi->limit = 0;
        }
        fi->inner = (uint8_t)i4;
      }
    }
  }

  /* ---- parse intra modes for the whole frame (partition 0) ---- */
  mbinfo_t *mbs = (mbinfo_t *)calloc((size_t)mbw * mbh, sizeof(mbinfo_t));
  finfo_t *finfo = (finfo_t *)calloc((size_t)mbw * mbh, sizeof(finfo_t));
  uint8_t *intra_t = (uint8_t *)calloc((size_t)mbw * 4, 1);
  uint8_t intra_l[4] = {0};
  if (!mbs || !finfo || !intra_t) {
    free(mbs);
    free(finfo);
    free(intra_t);
    vp8_free(F);
    return false;
  }
  for (int my = 0; my < mbh; ++my) {
    memset(intra_l, B_DC, 4);
    for (int mx = 0; mx < mbw; ++mx) {
      mbinfo_t *mb = &mbs[my * mbw + mx];
      uint8_t *top = intra_t + mx * 4;
      if (update_map)
        mb->segment = (uint8_t)(!bd_get(&br, seg_probs[0]) ? bd_get(&br, seg_probs[1])
                                                           : bd_get(&br, seg_probs[2]) + 2);
      if (use_skip) mb->skip = (uint8_t)bd_get(&br, skip_p);
      mb->is_i4 = (uint8_t)!bd_get(&br, 145);
      if (!mb->is_i4) {
        int ymode = bd_get(&br, 156) ? (bd_get(&br, 128) ? B_TM : B_HE)
                                     : (bd_get(&br, 163) ? B_VE : B_DC);
        mb->imodes[0] = (uint8_t)ymode;
        memset(top, ymode, 4);
        memset(intra_l, ymode, 4);
      } else {
        uint8_t *modes = mb->imodes;
        for (int y = 0; y < 4; ++y) {
          int ymode = intra_l[y];
          for (int x = 0; x < 4; ++x) {
            const uint8_t *prob = &vp8_bmodes_proba[(top[x] * 10 + ymode) * 9];
            int i = kYModesIntra4[bd_get(&br, prob[0])];
            while (i > 0) i = kYModesIntra4[2 * i + bd_get(&br, prob[i])];
            ymode = -i;
            top[x] = (uint8_t)ymode;
          }
          memcpy(modes, top, 4);
          modes += 4;
          intra_l[y] = (uint8_t)ymode;
        }
      }
      mb->uvmode = (uint8_t)(!bd_get(&br, 142)   ? B_DC
                             : !bd_get(&br, 114) ? B_VE
                             : bd_get(&br, 183)  ? B_TM
                                                 : B_HE);
    }
  }
  free(intra_t);

  /* ---- residuals + reconstruction, row by row ---- */
  uint8_t *top_nz_y = (uint8_t *)calloc((size_t)mbw * 4, 1);
  uint8_t *top_nz_u = (uint8_t *)calloc((size_t)mbw * 2, 1);
  uint8_t *top_nz_v = (uint8_t *)calloc((size_t)mbw * 2, 1);
  uint8_t *top_nz_dc = (uint8_t *)calloc((size_t)mbw, 1);
  uint8_t *yuv_t = (uint8_t *)calloc((size_t)mbw * 32, 1); /* y16 u8 v8 */
  if (!top_nz_y || !top_nz_u || !top_nz_v || !top_nz_dc || !yuv_t) {
    free(mbs); free(finfo); free(top_nz_y); free(top_nz_u); free(top_nz_v);
    free(top_nz_dc); free(yuv_t);
    vp8_free(F);
    return false;
  }
  static const int kScan[16] = {
      0 + 0 * BPS, 4 + 0 * BPS, 8 + 0 * BPS, 12 + 0 * BPS, 0 + 4 * BPS, 4 + 4 * BPS,
      8 + 4 * BPS, 12 + 4 * BPS, 0 + 8 * BPS, 4 + 8 * BPS, 8 + 8 * BPS, 12 + 8 * BPS,
      0 + 12 * BPS, 4 + 12 * BPS, 8 + 12 * BPS, 12 + 12 * BPS};

  uint8_t work[WORK_SIZE];
  for (int my = 0; my < mbh; ++my) {
    bd_t *tbr = &parts[my & (nparts - 1)];
    uint8_t left_nz_y[4] = {0}, left_nz_u[2] = {0}, left_nz_v[2] = {0}, left_nz_dc = 0;

    uint8_t *y_dst = work + Y_OFF, *u_dst = work + U_OFF, *v_dst = work + V_OFF;
    memset(work, 0, sizeof work);
    for (int j = 0; j < 16; ++j) y_dst[j * BPS - 1] = 129;
    for (int j = 0; j < 8; ++j) u_dst[j * BPS - 1] = v_dst[j * BPS - 1] = 129;
    if (my > 0) {
      y_dst[-1 - BPS] = u_dst[-1 - BPS] = v_dst[-1 - BPS] = 129;
    } else {
      memset(y_dst - BPS - 1, 127, 16 + 4 + 1);
      memset(u_dst - BPS - 1, 127, 8 + 1);
      memset(v_dst - BPS - 1, 127, 8 + 1);
    }

    for (int mx = 0; mx < mbw; ++mx) {
      mbinfo_t *mb = &mbs[my * mbw + mx];
      const quant_t *Q = &q[mb->segment];
      int16_t co[384];
      memset(co, 0, sizeof co);
      int skip = use_skip ? mb->skip : 0;
      int any_nz = 0;

      if (!skip) {
        int first;
        const uint8_t(*ac_proba)[3][11];
        if (!mb->is_i4) {
          int16_t dc[16] = {0};
          int ctx = top_nz_dc[mx] + left_nz_dc;
          int nz = get_coeffs(tbr, proba[1], ctx, Q->y2, 0, dc);
          top_nz_dc[mx] = left_nz_dc = (uint8_t)(nz > 0);
          if (nz > 1) {
            iwht(dc, co);
          } else {
            int dc0 = (dc[0] + 3) >> 3;
            for (int i = 0; i < 256; i += 16) co[i] = (int16_t)dc0;
          }
          first = 1;
          ac_proba = proba[0];
        } else {
          first = 0;
          ac_proba = proba[3];
        }
        for (int by = 0; by < 4; ++by) {
          for (int bx = 0; bx < 4; ++bx) {
            int16_t *blk = co + (by * 4 + bx) * 16;
            int ctx = left_nz_y[by] + top_nz_y[mx * 4 + bx];
            int nz = get_coeffs(tbr, ac_proba, ctx, Q->y1, first, blk);
            uint8_t l = (uint8_t)(nz > first);
            left_nz_y[by] = top_nz_y[mx * 4 + bx] = l;
            if (nz > 1 || blk[0] != 0) any_nz = 1;
          }
        }
        for (int ch = 0; ch < 2; ++ch) {
          uint8_t *tnz = (ch == 0 ? top_nz_u : top_nz_v) + mx * 2;
          uint8_t *lnz = ch == 0 ? left_nz_u : left_nz_v;
          for (int by = 0; by < 2; ++by) {
            for (int bx = 0; bx < 2; ++bx) {
              int16_t *blk = co + (16 + ch * 4 + by * 2 + bx) * 16;
              int ctx = lnz[by] + tnz[bx];
              int nz = get_coeffs(tbr, proba[2], ctx, Q->uv, 0, blk);
              uint8_t l = (uint8_t)(nz > 0);
              lnz[by] = tnz[bx] = l;
              if (nz > 1 || blk[0] != 0) any_nz = 1;
            }
          }
        }
        skip = !any_nz;
      } else {
        memset(left_nz_y, 0, 4);
        memset(left_nz_u, 0, 2);
        memset(left_nz_v, 0, 2);
        memset(top_nz_y + mx * 4, 0, 4);
        memset(top_nz_u + mx * 2, 0, 2);
        memset(top_nz_v + mx * 2, 0, 2);
        if (!mb->is_i4) left_nz_dc = top_nz_dc[mx] = 0;
      }
      if (filter_type > 0) {
        finfo_t *fi = &finfo[my * mbw + mx];
        *fi = fstr[mb->segment][mb->is_i4];
        fi->inner |= (uint8_t)!skip;
      }

      /* ---- reconstruct this macroblock ---- */
      if (mx > 0) {
        for (int j = -1; j < 16; ++j) y_dst[j * BPS - 1] = y_dst[j * BPS + 15];
        for (int j = -1; j < 8; ++j) {
          u_dst[j * BPS - 1] = u_dst[j * BPS + 7];
          v_dst[j * BPS - 1] = v_dst[j * BPS + 7];
        }
      }
      uint8_t *ty = yuv_t + mx * 32;
      if (my > 0) {
        memcpy(y_dst - BPS, ty, 16);
        memcpy(u_dst - BPS, ty + 16, 8);
        memcpy(v_dst - BPS, ty + 24, 8);
      }
      if (mb->is_i4) {
        uint8_t *tr = y_dst - BPS + 16;
        if (my > 0) {
          if (mx >= mbw - 1)
            memset(tr, ty[15], 4);
          else
            memcpy(tr, yuv_t + (mx + 1) * 32, 4);
        }
        memcpy(tr + 4 * BPS, tr, 4);
        memcpy(tr + 8 * BPS, tr, 4);
        memcpy(tr + 12 * BPS, tr, 4);
        for (int n = 0; n < 16; ++n) {
          uint8_t *dst = y_dst + kScan[n];
          pred4(dst, mb->imodes[n]);
          idct4(co + n * 16, dst);
        }
      } else {
        int mode = mb->imodes[0];
        if (mode == B_DC)
          pred_dc(y_dst, 16, mx == 0 ? (my == 0 ? 3 : 2) : (my == 0 ? 1 : 0));
        else if (mode == B_TM)
          pred_tm(y_dst, 16);
        else if (mode == B_VE)
          pred_ve(y_dst, 16);
        else
          pred_he(y_dst, 16);
        for (int n = 0; n < 16; ++n) idct4(co + n * 16, y_dst + kScan[n]);
      }
      {
        int mode = mb->uvmode;
        for (int ch = 0; ch < 2; ++ch) {
          uint8_t *dst = ch == 0 ? u_dst : v_dst;
          if (mode == B_DC)
            pred_dc(dst, 8, mx == 0 ? (my == 0 ? 3 : 2) : (my == 0 ? 1 : 0));
          else if (mode == B_TM)
            pred_tm(dst, 8);
          else if (mode == B_VE)
            pred_ve(dst, 8);
          else
            pred_he(dst, 8);
          for (int n = 0; n < 4; ++n)
            idct4(co + (16 + ch * 4 + n) * 16, dst + (n & 1) * 4 + (n >> 1) * 4 * BPS);
        }
      }
      if (my < mbh - 1) {
        memcpy(ty, y_dst + 15 * BPS, 16);
        memcpy(ty + 16, u_dst + 7 * BPS, 8);
        memcpy(ty + 24, v_dst + 7 * BPS, 8);
      }
      for (int j = 0; j < 16; ++j)
        memcpy(F->y + (size_t)(my * 16 + j) * F->ys + mx * 16, y_dst + j * BPS, 16);
      for (int j = 0; j < 8; ++j) {
        memcpy(F->u + (size_t)(my * 8 + j) * F->uvs + mx * 8, u_dst + j * BPS, 8);
        memcpy(F->v + (size_t)(my * 8 + j) * F->uvs + mx * 8, v_dst + j * BPS, 8);
      }
    }
  }
  free(top_nz_y); free(top_nz_u); free(top_nz_v); free(top_nz_dc); free(yuv_t);
  free(mbs);

  /* ---- in-loop filter over the finished frame ---- */
  if (filter_type > 0) {
    for (int my = 0; my < mbh; ++my) {
      for (int mx = 0; mx < mbw; ++mx) {
        const finfo_t *fi = &finfo[my * mbw + mx];
        int limit = fi->limit, il = fi->ilevel, ht = fi->hev;
        if (limit == 0) continue;
        uint8_t *yp = F->y + (size_t)my * 16 * F->ys + mx * 16;
        uint8_t *up = F->u + (size_t)my * 8 * F->uvs + mx * 8;
        uint8_t *vp = F->v + (size_t)my * 8 * F->uvs + mx * 8;
        int ys = F->ys, uvs = F->uvs;
        if (filter_type == 1) {
          if (mx > 0) simple_loop(yp, 1, ys, limit + 4);
          if (fi->inner)
            for (int k = 1; k <= 3; ++k) simple_loop(yp + 4 * k, 1, ys, limit);
          if (my > 0) simple_loop(yp, ys, 1, limit + 4);
          if (fi->inner)
            for (int k = 1; k <= 3; ++k) simple_loop(yp + 4 * k * ys, ys, 1, limit);
        } else {
          if (mx > 0) {
            loop26(yp, 1, ys, 16, limit + 4, il, ht);
            loop26(up, 1, uvs, 8, limit + 4, il, ht);
            loop26(vp, 1, uvs, 8, limit + 4, il, ht);
          }
          if (fi->inner) {
            for (int k = 1; k <= 3; ++k) loop24(yp + 4 * k, 1, ys, 16, limit, il, ht);
            loop24(up + 4, 1, uvs, 8, limit, il, ht);
            loop24(vp + 4, 1, uvs, 8, limit, il, ht);
          }
          if (my > 0) {
            loop26(yp, ys, 1, 16, limit + 4, il, ht);
            loop26(up, uvs, 1, 8, limit + 4, il, ht);
            loop26(vp, uvs, 1, 8, limit + 4, il, ht);
          }
          if (fi->inner) {
            for (int k = 1; k <= 3; ++k) loop24(yp + 4 * k * ys, ys, 1, 16, limit, il, ht);
            loop24(up + 4 * uvs, uvs, 1, 8, limit, il, ht);
            loop24(vp + 4 * uvs, uvs, 1, 8, limit, il, ht);
          }
        }
      }
    }
  }
  free(finfo);
  F->ok = 1;
  return true;
}

/* ---- YUV -> RGBA with libwebp's "fancy" chroma upsampling ---- */
static int mult_hi(int v, int c) { return (v * c) >> 8; }
static uint8_t yuv_clip(int v) { return (v & ~((256 << 6) - 1)) == 0 ? (uint8_t)(v >> 6) : (v < 0 ? 0 : 255); }
static void yuv_to_rgb(int y, int u, int v, uint8_t *rgba) {
  rgba[0] = yuv_clip(mult_hi(y, 19077) + mult_hi(v, 26149) - 14234);
  rgba[1] = yuv_clip(mult_hi(y, 19077) - mult_hi(u, 6419) - mult_hi(v, 13320) + 8708);
  rgba[2] = yuv_clip(mult_hi(y, 19077) + mult_hi(u, 33050) - 17685);
  rgba[3] = 255;
}
#define LOAD_UV(u, v) ((uint32_t)(u) | ((uint32_t)(v) << 16))
static void put_px(const uint8_t *y, int x, uint32_t uv, uint8_t *dst) {
  yuv_to_rgb(y[x], (int)(uv & 0xff), (int)(uv >> 16), dst + (size_t)x * 4);
}
static void upsample_pair(const uint8_t *top_y, const uint8_t *bottom_y,
                          const uint8_t *top_u, const uint8_t *top_v,
                          const uint8_t *cur_u, const uint8_t *cur_v,
                          uint8_t *top_dst, uint8_t *bottom_dst, int len) {
  int last_pair = (len - 1) >> 1;
  uint32_t tl_uv = LOAD_UV(top_u[0], top_v[0]);
  uint32_t l_uv = LOAD_UV(cur_u[0], cur_v[0]);
  {
    uint32_t uv0 = (3 * tl_uv + l_uv + 0x00020002u) >> 2;
    put_px(top_y, 0, uv0, top_dst);
  }
  if (bottom_y) {
    uint32_t uv0 = (3 * l_uv + tl_uv + 0x00020002u) >> 2;
    put_px(bottom_y, 0, uv0, bottom_dst);
  }
  for (int x = 1; x <= last_pair; ++x) {
    uint32_t t_uv = LOAD_UV(top_u[x], top_v[x]);
    uint32_t uv = LOAD_UV(cur_u[x], cur_v[x]);
    uint32_t avg = tl_uv + t_uv + l_uv + uv + 0x00080008u;
    uint32_t diag_12 = (avg + 2 * (t_uv + l_uv)) >> 3;
    uint32_t diag_03 = (avg + 2 * (tl_uv + uv)) >> 3;
    {
      uint32_t uv0 = (diag_12 + tl_uv) >> 1;
      uint32_t uv1 = (diag_03 + t_uv) >> 1;
      put_px(top_y, 2 * x - 1, uv0, top_dst);
      put_px(top_y, 2 * x - 0, uv1, top_dst);
    }
    if (bottom_y) {
      uint32_t uv0 = (diag_03 + l_uv) >> 1;
      uint32_t uv1 = (diag_12 + uv) >> 1;
      put_px(bottom_y, 2 * x - 1, uv0, bottom_dst);
      put_px(bottom_y, 2 * x + 0, uv1, bottom_dst);
    }
    tl_uv = t_uv;
    l_uv = uv;
  }
  if (!(len & 1)) {
    {
      uint32_t uv0 = (3 * tl_uv + l_uv + 0x00020002u) >> 2;
      put_px(top_y, len - 1, uv0, top_dst);
    }
    if (bottom_y) {
      uint32_t uv0 = (3 * l_uv + tl_uv + 0x00020002u) >> 2;
      put_px(bottom_y, len - 1, uv0, bottom_dst);
    }
  }
}

static uint8_t *vp8_to_rgba(const vp8frame_t *F) {
  int w = F->w, h = F->h;
  uint8_t *out = (uint8_t *)malloc((size_t)w * h * 4);
  if (!out) return NULL;
  size_t rs = (size_t)w * 4;
  const uint8_t *Y = F->y, *U = F->u, *V = F->v;
  int ys = F->ys, uvs = F->uvs;
  /* first line */
  upsample_pair(Y, NULL, U, V, U, V, out, NULL, w);
  int y = 1;
  for (; y + 1 < h; y += 2) {
    int k = (y + 1) >> 1;
    upsample_pair(Y + (size_t)y * ys, Y + (size_t)(y + 1) * ys, U + (size_t)(k - 1) * uvs,
                  V + (size_t)(k - 1) * uvs, U + (size_t)k * uvs, V + (size_t)k * uvs,
                  out + y * rs, out + (y + 1) * rs, w);
  }
  if (y < h) { /* even height: last row alone */
    int k = (h >> 1) - 1;
    upsample_pair(Y + (size_t)y * ys, NULL, U + (size_t)k * uvs, V + (size_t)k * uvs,
                  U + (size_t)k * uvs, V + (size_t)k * uvs, out + y * rs, NULL, w);
  }
  return out;
}

/* ======================================================================== *
 *  Lossless: VP8L
 * ======================================================================== */

typedef struct {
  const uint8_t *p;
  size_t len, pos;
  uint64_t val;
  int nbits;
  int eos;
} lbr_t;

static void lbr_init(lbr_t *b, const uint8_t *p, size_t n) {
  b->p = p; b->len = n; b->pos = 0; b->val = 0; b->nbits = 0; b->eos = 0;
}
static uint32_t lbr_bits(lbr_t *b, int n) {
  if (n == 0) return 0;
  while (b->nbits < n) {
    if (b->pos < b->len) b->val |= (uint64_t)b->p[b->pos++] << b->nbits;
    else b->eos = 1;
    b->nbits += 8;
  }
  uint32_t v = (uint32_t)(b->val & ((n == 32) ? 0xffffffffu : ((1u << n) - 1)));
  b->val >>= n;
  b->nbits -= n;
  return v;
}

typedef struct {
  uint16_t count[16];
  uint16_t *sym;
  int single, single_sym;
} hcode_t;

static void hcode_free(hcode_t *h) { free(h->sym); h->sym = NULL; }

static bool hcode_build(hcode_t *h, const uint8_t *lens, int n) {
  memset(h, 0, sizeof *h);
  int nz = 0, last = 0;
  for (int i = 0; i < n; ++i)
    if (lens[i]) { nz++; last = i; }
  if (nz <= 1) {
    h->single = 1;
    h->single_sym = nz ? last : 0;
    return true;
  }
  h->sym = (uint16_t *)malloc(sizeof(uint16_t) * (size_t)n);
  if (!h->sym) return false;
  for (int i = 0; i < n; ++i) h->count[lens[i]]++;
  h->count[0] = 0;
  /* over-subscription check */
  int left = 1;
  for (int l = 1; l < 16; ++l) {
    left <<= 1;
    left -= h->count[l];
    if (left < 0) { hcode_free(h); return false; }
  }
  uint16_t offs[16];
  offs[1] = 0;
  for (int l = 1; l < 15; ++l) offs[l + 1] = (uint16_t)(offs[l] + h->count[l]);
  for (int i = 0; i < n; ++i)
    if (lens[i]) h->sym[offs[lens[i]]++] = (uint16_t)i;
  return true;
}

static int hcode_read(const hcode_t *h, lbr_t *br) {
  if (h->single) return h->single_sym;
  int code = 0, first = 0, index = 0;
  for (int len = 1; len < 16; ++len) {
    code |= (int)lbr_bits(br, 1);
    int count = h->count[len];
    if (code - count < first) return h->sym[index + (code - first)];
    index += count;
    first += count;
    first <<= 1;
    code <<= 1;
  }
  br->eos = 1;
  return 0;
}

static const uint8_t kCodeLengthOrder[19] = {17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

static bool read_hcode(lbr_t *br, hcode_t *h, int alpha_size) {
  uint8_t *lens = (uint8_t *)calloc((size_t)alpha_size, 1);
  if (!lens) return false;
  bool ok = false;
  if (lbr_bits(br, 1)) { /* simple */
    int nsym = (int)lbr_bits(br, 1) + 1;
    int first8 = (int)lbr_bits(br, 1);
    int s = (int)lbr_bits(br, first8 ? 8 : 1);
    if (s >= alpha_size) goto done;
    lens[s] = 1;
    if (nsym == 2) {
      s = (int)lbr_bits(br, 8);
      if (s >= alpha_size) goto done;
      lens[s] = 1;
    }
  } else {
    uint8_t cl[19] = {0};
    int ncodes = (int)lbr_bits(br, 4) + 4;
    for (int i = 0; i < ncodes; ++i) cl[kCodeLengthOrder[i]] = (uint8_t)lbr_bits(br, 3);
    hcode_t ch;
    if (!hcode_build(&ch, cl, 19)) goto done;
    int max_symbol;
    if (lbr_bits(br, 1)) {
      int nb = 2 + 2 * (int)lbr_bits(br, 3);
      max_symbol = 2 + (int)lbr_bits(br, nb);
    } else {
      max_symbol = alpha_size;
    }
    int symbol = 0, prev = 8;
    while (symbol < alpha_size) {
      if (max_symbol-- == 0) break;
      int cl_sym = hcode_read(&ch, br);
      if (br->eos) { hcode_free(&ch); goto done; }
      if (cl_sym < 16) {
        lens[symbol++] = (uint8_t)cl_sym;
        if (cl_sym) prev = cl_sym;
      } else {
        int use_prev = cl_sym == 16;
        int slot = cl_sym - 16;
        static const int extra[3] = {2, 3, 7}, offs[3] = {3, 3, 11};
        int rep = (int)lbr_bits(br, extra[slot]) + offs[slot];
        if (symbol + rep > alpha_size) { hcode_free(&ch); goto done; }
        uint8_t v = use_prev ? (uint8_t)prev : 0;
        while (rep-- > 0) lens[symbol++] = v;
      }
    }
    hcode_free(&ch);
  }
  if (br->eos) goto done;
  ok = hcode_build(h, lens, alpha_size);
done:
  free(lens);
  return ok;
}

typedef struct {
  hcode_t c[5]; /* green+len+cache, red, blue, alpha, dist */
} hgroup_t;

static int sub_size(int size, int bits) { return (size + (1 << bits) - 1) >> bits; }

static const int kAlpha[4] = {256 + 24, 256, 256, 256};

static uint32_t cc_hash(uint32_t argb, int bits) { return (0x1e35a7bdu * argb) >> (32 - bits); }

/* Decode an entropy-coded ARGB image of xs*ys. */
static uint32_t *decode_image_stream(lbr_t *br, int xs, int ys, int is_level0,
                                     int *out_xs);

typedef struct {
  int type;
  int bits;
  int xs, ys;
  uint32_t *data;
} xform_t;

static bool decode_pixels(lbr_t *br, uint32_t *dst, int xs, int ys, hgroup_t *groups,
                          int ngroups, const uint32_t *meta, int meta_bits, int meta_xs,
                          int cc_bits) {
  uint32_t *cache = NULL;
  int cc_size = cc_bits ? 1 << cc_bits : 0;
  if (cc_size) {
    cache = (uint32_t *)calloc((size_t)cc_size, 4);
    if (!cache) return false;
  }
  (void)ngroups;
  size_t total = (size_t)xs * ys, src = 0, last_cached = 0;
  int col = 0, row = 0;
  hgroup_t *g = &groups[0];
  int mask = meta_bits ? (1 << meta_bits) - 1 : -1;
  while (src < total) {
    if (br->eos) goto fail;
    if ((col & mask) == 0 && meta) {
      uint32_t idx = (meta[(size_t)(row >> meta_bits) * meta_xs + (col >> meta_bits)] >> 8) & 0xffff;
      g = &groups[idx];
    }
    int code = hcode_read(&g->c[0], br);
    if (code < 256) {
      uint32_t r = (uint32_t)hcode_read(&g->c[1], br);
      uint32_t b = (uint32_t)hcode_read(&g->c[2], br);
      uint32_t a = (uint32_t)hcode_read(&g->c[3], br);
      dst[src++] = (a << 24) | (r << 16) | ((uint32_t)code << 8) | b;
      if (++col >= xs) { col = 0; row++; }
    } else if (code < 256 + 24) {
      int ls = code - 256;
      int length;
      if (ls < 4) length = ls + 1;
      else { int eb = (ls - 2) >> 1; int off = (2 + (ls & 1)) << eb; length = off + (int)lbr_bits(br, eb) + 1; }
      int ds = hcode_read(&g->c[4], br);
      int dcode;
      if (ds < 4) dcode = ds + 1;
      else { int eb = (ds - 2) >> 1; int off = (2 + (ds & 1)) << eb; dcode = off + (int)lbr_bits(br, eb) + 1; }
      int dist;
      if (dcode > 120) dist = dcode - 120;
      else {
        int dc = vp8l_code_to_plane[dcode - 1];
        int yo = dc >> 4, xo = 8 - (dc & 0xf);
        dist = yo * xs + xo;
        if (dist < 1) dist = 1;
      }
      if ((size_t)dist > src || src + (size_t)length > total) goto fail;
      for (int i = 0; i < length; ++i) { dst[src] = dst[src - dist]; src++; }
      col += length;
      while (col >= xs) { col -= xs; row++; }
      if (meta && src < total) {
        uint32_t idx = (meta[(size_t)(row >> meta_bits) * meta_xs + (col >> meta_bits)] >> 8) & 0xffff;
        g = &groups[idx];
      }
    } else {
      int key = code - 280;
      if (!cache || key >= cc_size) goto fail;
      while (last_cached < src) cache[cc_hash(dst[last_cached], cc_bits)] = dst[last_cached], last_cached++;
      dst[src++] = cache[key];
      if (++col >= xs) { col = 0; row++; }
    }
    if (cache)
      while (last_cached < src) { cache[cc_hash(dst[last_cached], cc_bits)] = dst[last_cached]; last_cached++; }
  }
  free(cache);
  return !br->eos;
fail:
  free(cache);
  return false;
}

static uint32_t *decode_entropy_image(lbr_t *br, int xs, int ys, int is_level0, xform_t *xf,
                                      int *nxf, int *out_xs) {
  /* transforms (only at level 0) */
  int cur_xs = xs;
  if (is_level0) {
    int used = 0;
    while (lbr_bits(br, 1)) {
      if (*nxf >= 4) return NULL;
      int type = (int)lbr_bits(br, 2);
      if (used & (1 << type)) return NULL;
      used |= 1 << type;
      xform_t *t = &xf[*nxf];
      memset(t, 0, sizeof *t);
      t->type = type;
      t->xs = cur_xs;
      t->ys = ys;
      if (type == 0 || type == 1) {
        t->bits = (int)lbr_bits(br, 3) + 2;
        int sx, dummy;
        (void)dummy;
        sx = sub_size(cur_xs, t->bits);
        t->data = decode_image_stream(br, sx, sub_size(ys, t->bits), 0, NULL);
        if (!t->data) { (*nxf)++; return NULL; }
      } else if (type == 3) {
        int ncol = (int)lbr_bits(br, 8) + 1;
        int bits = ncol > 16 ? 0 : ncol > 4 ? 1 : ncol > 2 ? 2 : 3;
        uint32_t *pal = decode_image_stream(br, ncol, 1, 0, NULL);
        if (!pal) { (*nxf)++; return NULL; }
        int full = 1 << (8 >> bits);
        if (full < ncol) full = ncol;
        uint32_t *pal2 = (uint32_t *)calloc((size_t)(full > 256 ? full : 256), 4);
        if (!pal2) { free(pal); (*nxf)++; return NULL; }
        pal2[0] = pal[0];
        for (int i = 1; i < ncol; ++i) {
          uint32_t a = pal[i], b = pal2[i - 1];
          pal2[i] = (((a & 0xff00ff00u) + (b & 0xff00ff00u)) & 0xff00ff00u) |
                    (((a & 0x00ff00ffu) + (b & 0x00ff00ffu)) & 0x00ff00ffu);
        }
        free(pal);
        t->data = pal2;
        t->bits = bits;
        t->ys = ncol; /* palette size stored here */
        cur_xs = sub_size(cur_xs, bits);
      }
      (*nxf)++;
    }
  }
  /* colour cache */
  int cc_bits = 0;
  if (lbr_bits(br, 1)) {
    cc_bits = (int)lbr_bits(br, 4);
    if (cc_bits < 1 || cc_bits > 11) return NULL;
  }
  /* meta prefix codes */
  uint32_t *meta = NULL;
  int meta_bits = 0, meta_xs = 0, ngroups = 1;
  if (is_level0 && lbr_bits(br, 1)) {
    meta_bits = (int)lbr_bits(br, 3) + 2;
    meta_xs = sub_size(cur_xs, meta_bits);
    meta = decode_image_stream(br, meta_xs, sub_size(ys, meta_bits), 0, NULL);
    if (!meta) return NULL;
    for (int i = 0; i < meta_xs * sub_size(ys, meta_bits); ++i) {
      int gidx = (int)((meta[i] >> 8) & 0xffff);
      if (gidx + 1 > ngroups) ngroups = gidx + 1;
    }
  }
  hgroup_t *groups = (hgroup_t *)calloc((size_t)ngroups, sizeof(hgroup_t));
  if (!groups) { free(meta); return NULL; }
  bool ok = true;
  for (int gi = 0; gi < ngroups && ok; ++gi) {
    for (int j = 0; j < 5 && ok; ++j) {
      int asz = j == 4 ? 40 : kAlpha[j] + (j == 0 && cc_bits ? 1 << cc_bits : 0);
      ok = read_hcode(br, &groups[gi].c[j], asz);
    }
  }
  uint32_t *pix = NULL;
  if (ok) {
    pix = (uint32_t *)malloc((size_t)cur_xs * ys * 4);
    if (pix) {
      if (!decode_pixels(br, pix, cur_xs, ys, groups, ngroups, meta, meta_bits, meta_xs, cc_bits)) {
        free(pix);
        pix = NULL;
      }
    }
  }
  for (int gi = 0; gi < ngroups; ++gi)
    for (int j = 0; j < 5; ++j) hcode_free(&groups[gi].c[j]);
  free(groups);
  free(meta);
  if (out_xs) *out_xs = cur_xs;
  return pix;
}

static uint32_t *decode_image_stream(lbr_t *br, int xs, int ys, int is_level0, int *out_xs) {
  xform_t dummy[4];
  int n = 0;
  uint32_t *r = decode_entropy_image(br, xs, ys, is_level0, dummy, &n, out_xs);
  for (int i = 0; i < n; ++i) free(dummy[i].data);
  return r;
}

/* ---- inverse transforms ---- */
static uint32_t avg2(uint32_t a, uint32_t b) { return (((a ^ b) & 0xfefefefeu) >> 1) + (a & b); }
static uint32_t add_px(uint32_t a, uint32_t b) {
  return (((a & 0xff00ff00u) + (b & 0xff00ff00u)) & 0xff00ff00u) |
         (((a & 0x00ff00ffu) + (b & 0x00ff00ffu)) & 0x00ff00ffu);
}
static int ch_sub3(int a, int b, int c) { return iabs(b - c) - iabs(a - c); }
static uint32_t select_px(uint32_t a, uint32_t b, uint32_t c) {
  int s = ch_sub3((int)(a >> 24), (int)(b >> 24), (int)(c >> 24)) +
          ch_sub3((int)((a >> 16) & 0xff), (int)((b >> 16) & 0xff), (int)((c >> 16) & 0xff)) +
          ch_sub3((int)((a >> 8) & 0xff), (int)((b >> 8) & 0xff), (int)((c >> 8) & 0xff)) +
          ch_sub3((int)(a & 0xff), (int)(b & 0xff), (int)(c & 0xff));
  return s <= 0 ? a : b;
}
static uint32_t clamp_full(uint32_t a, uint32_t b, uint32_t c) {
  uint32_t r = 0;
  for (int s = 0; s < 32; s += 8)
    r |= (uint32_t)clip8((int)((a >> s) & 0xff) + (int)((b >> s) & 0xff) - (int)((c >> s) & 0xff)) << s;
  return r;
}
static uint32_t clamp_half(uint32_t a, uint32_t b) {
  uint32_t r = 0;
  for (int s = 0; s < 32; s += 8) {
    int av = (int)((a >> s) & 0xff), bv = (int)((b >> s) & 0xff);
    r |= (uint32_t)clip8(av + (av - bv) / 2) << s;
  }
  return r;
}
static uint32_t predict(int mode, uint32_t L, const uint32_t *T) {
  switch (mode) {
    case 0: return 0xff000000u;
    case 1: return L;
    case 2: return T[0];
    case 3: return T[1];
    case 4: return T[-1];
    case 5: return avg2(avg2(L, T[1]), T[0]);
    case 6: return avg2(L, T[-1]);
    case 7: return avg2(L, T[0]);
    case 8: return avg2(T[-1], T[0]);
    case 9: return avg2(T[0], T[1]);
    case 10: return avg2(avg2(L, T[-1]), avg2(T[0], T[1]));
    case 11: return select_px(T[0], L, T[-1]);
    case 12: return clamp_full(L, T[0], T[-1]);
    case 13: return clamp_half(avg2(L, T[0]), T[-1]);
    default: return 0xff000000u;
  }
}

static void apply_inverse(xform_t *t, uint32_t *data) {
  if (t->type == 2) { /* subtract green */
    size_t n = (size_t)t->xs * t->ys;
    for (size_t i = 0; i < n; ++i) {
      uint32_t p = data[i], g = (p >> 8) & 0xff;
      uint32_t rb = (((p & 0x00ff00ffu) + ((g << 16) | g)) & 0x00ff00ffu);
      data[i] = (p & 0xff00ff00u) | rb;
    }
  } else if (t->type == 1) { /* cross colour */
    int bs = t->bits, tw = sub_size(t->xs, bs);
    for (int y = 0; y < t->ys; ++y)
      for (int x = 0; x < t->xs; ++x) {
        uint32_t code = t->data[(size_t)(y >> bs) * tw + (x >> bs)];
        int8_t g2r = (int8_t)(code & 0xff), g2b = (int8_t)((code >> 8) & 0xff),
               r2b = (int8_t)((code >> 16) & 0xff);
        uint32_t p = data[(size_t)y * t->xs + x];
        int green = (int8_t)((p >> 8) & 0xff);
        int red = (int)((p >> 16) & 0xff), blue = (int)(p & 0xff);
        red += (g2r * green) >> 5;
        red &= 0xff;
        blue += (g2b * green) >> 5;
        blue += (r2b * (int8_t)red) >> 5;
        blue &= 0xff;
        data[(size_t)y * t->xs + x] = (p & 0xff00ff00u) | ((uint32_t)red << 16) | (uint32_t)blue;
      }
  } else if (t->type == 0) { /* predictor */
    int bs = t->bits, tw = sub_size(t->xs, bs), w = t->xs;
    for (int y = 0; y < t->ys; ++y) {
      uint32_t *row = data + (size_t)y * w;
      if (y == 0) {
        row[0] = add_px(row[0], 0xff000000u);
        for (int x = 1; x < w; ++x) row[x] = add_px(row[x], row[x - 1]);
        continue;
      }
      row[0] = add_px(row[0], row[-w]);
      for (int x = 1; x < w; ++x) {
        int mode = (int)((t->data[(size_t)(y >> bs) * tw + (x >> bs)] >> 8) & 0xf);
        row[x] = add_px(row[x], predict(mode, row[x - 1], row + x - w));
      }
    }
  }
}

/* Full VP8L image decode. If `headerless` is set (ALPH chunk), the signature /
 * size header is absent and w/h come from the caller. */
static bool vp8l_decode(const uint8_t *d, size_t n, int headerless, int *pw, int *ph,
                        uint32_t **out_argb) {
  lbr_t br;
  lbr_init(&br, d, n);
  int w, h;
  if (!headerless) {
    if (n < 5 || d[0] != 0x2f) return false;
    lbr_bits(&br, 8);
    w = (int)lbr_bits(&br, 14) + 1;
    h = (int)lbr_bits(&br, 14) + 1;
    lbr_bits(&br, 1);
    if (lbr_bits(&br, 3) != 0) return false;
    *pw = w;
    *ph = h;
  } else {
    w = *pw;
    h = *ph;
  }
  xform_t xf[4];
  int nxf = 0, cur_xs = w;
  uint32_t *pix = decode_entropy_image(&br, w, h, 1, xf, &nxf, &cur_xs);
  if (!pix) {
    for (int i = 0; i < nxf; ++i) free(xf[i].data);
    return false;
  }
  /* apply inverse transforms in reverse order */
  for (int i = nxf - 1; i >= 0; --i) {
    xform_t *t = &xf[i];
    if (t->type == 3) {
      int bits = t->bits, ncol = t->ys, ow = t->xs;
      uint32_t *out = (uint32_t *)malloc((size_t)ow * h * 4);
      if (!out) { free(pix); pix = NULL; break; }
      int pw2 = sub_size(ow, bits);
      int bpp = 8 >> bits, cmask = (1 << bits) - 1;
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < ow; ++x) {
          uint32_t packed = (pix[(size_t)y * pw2 + (x >> bits)] >> 8) & 0xff;
          int idx = bits ? (int)((packed >> ((x & cmask) * bpp)) & ((1 << bpp) - 1)) : (int)packed;
          out[(size_t)y * ow + x] = idx < ncol ? t->data[idx] : 0u;
        }
      free(pix);
      pix = out;
    } else {
      t->ys = h;
      apply_inverse(t, pix);
    }
  }
  for (int i = 0; i < nxf; ++i) free(xf[i].data);
  if (!pix) return false;
  *out_argb = pix;
  return true;
}

/* ======================================================================== *
 *  Container
 * ======================================================================== */

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

typedef struct {
  const uint8_t *vp8, *vp8l, *alph;
  size_t vp8_n, vp8l_n, alph_n;
  int canvas_w, canvas_h;
} wp_t;

static bool wp_parse(const uint8_t *d, size_t n, wp_t *o) {
  memset(o, 0, sizeof *o);
  if (n < 20 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WEBP", 4)) return false;
  size_t riff = rd32(d + 4) + 8;
  if (riff > n) riff = n;
  size_t pos = 12;
  while (pos + 8 <= riff) {
    uint32_t sz = rd32(d + pos + 4);
    const uint8_t *body = d + pos + 8;
    if ((size_t)sz > riff - pos - 8) sz = (uint32_t)(riff - pos - 8);
    if (!memcmp(d + pos, "VP8X", 4) && sz >= 10) {
      o->canvas_w = (body[4] | (body[5] << 8) | (body[6] << 16)) + 1;
      o->canvas_h = (body[7] | (body[8] << 8) | (body[9] << 16)) + 1;
    } else if (!memcmp(d + pos, "ALPH", 4)) {
      o->alph = body; o->alph_n = sz;
    } else if (!memcmp(d + pos, "VP8 ", 4)) {
      o->vp8 = body; o->vp8_n = sz;
    } else if (!memcmp(d + pos, "VP8L", 4)) {
      o->vp8l = body; o->vp8l_n = sz;
    }
    pos += 8 + sz + (sz & 1);
  }
  return o->vp8 || o->vp8l;
}

bool webp_get_size(const uint8_t *data, size_t len, int *out_w, int *out_h) {
  wp_t wp;
  if (!wp_parse(data, len, &wp)) return false;
  if (wp.vp8) return vp8_header_size(wp.vp8, wp.vp8_n, out_w, out_h);
  if (wp.vp8l_n < 5 || wp.vp8l[0] != 0x2f) return false;
  uint32_t v = rd32(wp.vp8l + 1);
  *out_w = (int)(v & 0x3fff) + 1;
  *out_h = (int)((v >> 14) & 0x3fff) + 1;
  return true;
}

static bool decode_alpha(const wp_t *wp, int w, int h, uint8_t *alpha) {
  if (wp->alph_n < 2) return false;
  int comp = wp->alph[0] & 3, filt = (wp->alph[0] >> 2) & 3;
  const uint8_t *src = wp->alph + 1;
  size_t src_n = wp->alph_n - 1;
  size_t total = (size_t)w * h;
  if (comp == 0) {
    if (src_n < total) return false;
    memcpy(alpha, src, total);
  } else if (comp == 1) {
    uint32_t *argb = NULL;
    int aw = w, ah = h;
    if (!vp8l_decode(src, src_n, 1, &aw, &ah, &argb)) return false;
    for (size_t i = 0; i < total; ++i) alpha[i] = (uint8_t)((argb[i] >> 8) & 0xff);
    free(argb);
  } else {
    return false;
  }
  if (filt) {
    for (int y = 0; y < h; ++y) {
      uint8_t *row = alpha + (size_t)y * w, *prev = y ? row - w : NULL;
      for (int x = 0; x < w; ++x) {
        int pred;
        if (filt == 1) pred = x ? row[x - 1] : (prev ? prev[0] : 0);
        else if (filt == 2) pred = prev ? prev[x] : (x ? row[x - 1] : 0);
        else {
          if (!prev) pred = x ? row[x - 1] : 0;
          else if (!x) pred = prev[0];
          else pred = clip_i(row[x - 1] + prev[x] - prev[x - 1], 0, 255);
        }
        row[x] = (uint8_t)(row[x] + pred);
      }
    }
  }
  return true;
}

bool webp_decode_rgba(const uint8_t *data, size_t len, uint8_t **out_rgba, int *out_w,
                      int *out_h) {
  wp_t wp;
  if (!data || !out_rgba || !wp_parse(data, len, &wp)) return false;
  if (wp.vp8) {
    vp8frame_t F;
    if (!vp8_decode_frame(wp.vp8, wp.vp8_n, &F)) return false;
    uint8_t *rgba = vp8_to_rgba(&F);
    int w = F.w, h = F.h;
    vp8_free(&F);
    if (!rgba) return false;
    if (wp.alph) {
      uint8_t *a = (uint8_t *)malloc((size_t)w * h);
      if (a && decode_alpha(&wp, w, h, a))
        for (size_t i = 0; i < (size_t)w * h; ++i) rgba[i * 4 + 3] = a[i];
      free(a);
    }
    *out_rgba = rgba; *out_w = w; *out_h = h;
    return true;
  }
  int w = 0, h = 0;
  uint32_t *argb = NULL;
  if (!vp8l_decode(wp.vp8l, wp.vp8l_n, 0, &w, &h, &argb)) return false;
  uint8_t *rgba = (uint8_t *)malloc((size_t)w * h * 4);
  if (!rgba) { free(argb); return false; }
  for (size_t i = 0; i < (size_t)w * h; ++i) {
    uint32_t p = argb[i];
    rgba[i * 4 + 0] = (uint8_t)(p >> 16);
    rgba[i * 4 + 1] = (uint8_t)(p >> 8);
    rgba[i * 4 + 2] = (uint8_t)p;
    rgba[i * 4 + 3] = (uint8_t)(p >> 24);
  }
  free(argb);
  *out_rgba = rgba; *out_w = w; *out_h = h;
  return true;
}
