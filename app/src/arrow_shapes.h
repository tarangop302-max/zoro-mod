#ifndef ARROW_SHAPES_H
#define ARROW_SHAPES_H

/* Recolourable steering-arrow shapes (GENERATED data + drawing code).

   Unlike the fixed-design arrows in arrow_atlas.png these are plain
   polygons, drawn with whatever colour the player picks in
   Settings > Controls > Arrow shapes (the same igColorEdit4 picker as the
   Laser colour). A shape is selected by arrow_style >= ARROW_STYLE_COUNT:
   shape index = arrow_style - ARROW_STYLE_COUNT.

   Coordinates are unit-less: x runs -0.5 .. +0.5 with the tip at +x, y uses
   the same scale, so one `size` value scales the whole shape. Must be
   included after cimgui.h (ImDrawList etc.). Colours are packed here
   directly on purpose: IM_COL32 may be tinted by the crystal theme on
   Android, which would change the colour the player chose. */

#include <stddef.h>
#include "constants.h"

#ifndef ARROW_COL32
#define ARROW_COL32(r, g, b, a) \
  ((ImU32)(((ImU32)(a) << 24) | ((ImU32)(b) << 16) | ((ImU32)(g) << 8) | ((ImU32)(r))))
#endif

typedef struct arrow_tri { float x[3], y[3]; } arrow_tri;
typedef struct arrow_ovl { arrow_tri t; unsigned char r, g, b, a; } arrow_ovl;
typedef struct arrow_poly { int n; const float* p; } arrow_poly;
typedef struct arrow_shape_def {
  int ntri; const arrow_tri* tri;
  int npoly; const arrow_poly* poly;
  int novl; const arrow_ovl* ovl;
  int has_ridge; float rx0, ry0, rx1, ry1;
} arrow_shape_def;

static const char* const ARROW_SHAPE_NAMES[ARROW_SHAPE_COUNT] = {
  "Dart", "Long Nose", "Wide Arrow", "Slim Arrow", "Chevron", "Double Chevron", "Triangle", "Kite", "Stealth", "Spear", "Fletched", "Pentagon"
};

static const arrow_tri SH0_TRI[] = {
  {{0.5000f,-0.5000f,-0.1436f},{0.0000f,0.3369f,0.0000f}},
  {{-0.1436f,-0.5000f,0.5000f},{0.0000f,-0.3369f,0.0000f}},
};
static const float SH0_P0[] = {0.5000f,0.0000f,-0.5000f,-0.3369f,-0.1436f,0.0000f,-0.5000f,0.3369f};
static const arrow_poly SH0_POLY[] = {{4,SH0_P0}};
static const arrow_ovl SH0_OVL[] = {
  {{{0.5000f,-0.2516f,-0.1436f},{0.0000f,-0.1965f,0.0000f}},255,255,255,60},
  {{{0.5000f,-0.1436f,-0.2516f},{0.0000f,0.0000f,0.1965f}},0,0,0,70},
};
static const arrow_tri SH1_TRI[] = {
  {{-0.5000f,-0.5000f,-0.1596f},{0.1021f,-0.1021f,-0.1021f}},
  {{-0.1596f,-0.1596f,0.5000f},{-0.1021f,-0.2298f,0.0000f}},
  {{-0.5000f,-0.1596f,0.5000f},{0.1021f,-0.1021f,0.0000f}},
  {{0.5000f,-0.1596f,-0.1596f},{0.0000f,0.2298f,0.1021f}},
  {{0.5000f,-0.1596f,-0.5000f},{0.0000f,0.1021f,0.1021f}},
};
static const float SH1_P0[] = {-0.5000f,-0.1021f,-0.1596f,-0.1021f,-0.1596f,-0.2298f,0.5000f,0.0000f,-0.1596f,0.2298f,-0.1596f,0.1021f,-0.5000f,0.1021f};
static const arrow_poly SH1_POLY[] = {{7,SH1_P0}};
static const arrow_tri SH2_TRI[] = {
  {{-0.5000f,-0.5000f,-0.0556f},{0.2617f,-0.2617f,-0.2617f}},
  {{-0.0556f,-0.0556f,0.5000f},{-0.2617f,-0.4346f,0.0000f}},
  {{-0.5000f,-0.0556f,0.5000f},{0.2617f,-0.2617f,0.0000f}},
  {{0.5000f,-0.0556f,-0.0556f},{0.0000f,0.4346f,0.2617f}},
  {{0.5000f,-0.0556f,-0.5000f},{0.0000f,0.2617f,0.2617f}},
};
static const float SH2_P0[] = {-0.5000f,-0.2617f,-0.0556f,-0.2617f,-0.0556f,-0.4346f,0.5000f,0.0000f,-0.0556f,0.4346f,-0.0556f,0.2617f,-0.5000f,0.2617f};
static const arrow_poly SH2_POLY[] = {{7,SH2_P0}};
static const arrow_tri SH3_TRI[] = {
  {{-0.5000f,-0.5000f,0.1596f},{0.0596f,-0.0596f,-0.0596f}},
  {{0.1596f,0.1596f,0.5000f},{-0.0596f,-0.2255f,0.0000f}},
  {{-0.5000f,0.1596f,0.5000f},{0.0596f,-0.0596f,0.0000f}},
  {{0.5000f,0.1596f,0.1596f},{0.0000f,0.2255f,0.0596f}},
  {{0.5000f,0.1596f,-0.5000f},{0.0000f,0.0596f,0.0596f}},
};
static const float SH3_P0[] = {-0.5000f,-0.0596f,0.1596f,-0.0596f,0.1596f,-0.2255f,0.5000f,0.0000f,0.1596f,0.2255f,0.1596f,0.0596f,-0.5000f,0.0596f};
static const arrow_poly SH3_POLY[] = {{7,SH3_P0}};
static const arrow_tri SH4_TRI[] = {
  {{0.1479f,-0.5000f,-0.1479f},{0.0000f,-0.4113f,-0.4113f}},
  {{0.1479f,-0.1479f,0.5000f},{0.0000f,-0.4113f,0.0000f}},
  {{0.1479f,0.5000f,-0.1479f},{0.0000f,0.0000f,0.4113f}},
  {{-0.1479f,-0.5000f,0.1479f},{0.4113f,0.4113f,0.0000f}},
};
static const float SH4_P0[] = {-0.5000f,-0.4113f,-0.1479f,-0.4113f,0.5000f,0.0000f,-0.1479f,0.4113f,-0.5000f,0.4113f,0.1479f,0.0000f};
static const arrow_poly SH4_POLY[] = {{6,SH4_P0}};
static const arrow_tri SH5_TRI[] = {
  {{-0.2015f,-0.5000f,-0.3209f},{0.0000f,-0.3164f,-0.3164f}},
  {{-0.2015f,-0.3209f,-0.0224f},{0.0000f,-0.3164f,0.0000f}},
  {{-0.2015f,-0.0224f,-0.3209f},{0.0000f,0.0000f,0.3164f}},
  {{-0.3209f,-0.5000f,-0.2015f},{0.3164f,0.3164f,0.0000f}},
  {{0.3209f,0.0224f,0.2015f},{0.0000f,-0.3164f,-0.3164f}},
  {{0.3209f,0.2015f,0.5000f},{0.0000f,-0.3164f,0.0000f}},
  {{0.3209f,0.5000f,0.2015f},{0.0000f,0.0000f,0.3164f}},
  {{0.2015f,0.0224f,0.3209f},{0.3164f,0.3164f,0.0000f}},
};
static const float SH5_P0[] = {-0.5000f,-0.3164f,-0.3209f,-0.3164f,-0.0224f,0.0000f,-0.3209f,0.3164f,-0.5000f,0.3164f,-0.2015f,0.0000f};
static const float SH5_P1[] = {0.0224f,-0.3164f,0.2015f,-0.3164f,0.5000f,0.0000f,0.2015f,0.3164f,0.0224f,0.3164f,0.3209f,0.0000f};
static const arrow_poly SH5_POLY[] = {{6,SH5_P0},{6,SH5_P1}};
static const arrow_tri SH6_TRI[] = {
  {{-0.5000f,-0.5000f,0.5000f},{0.4000f,-0.4000f,0.0000f}},
};
static const float SH6_P0[] = {0.5000f,0.0000f,-0.5000f,-0.4000f,-0.5000f,0.4000f};
static const arrow_poly SH6_POLY[] = {{3,SH6_P0}};
static const arrow_tri SH7_TRI[] = {
  {{0.5000f,-0.1581f,-0.5000f},{0.0000f,0.2479f,0.0000f}},
  {{-0.5000f,-0.1581f,0.5000f},{0.0000f,-0.2479f,0.0000f}},
};
static const float SH7_P0[] = {0.5000f,0.0000f,-0.1581f,-0.2479f,-0.5000f,0.0000f,-0.1581f,0.2479f};
static const arrow_poly SH7_POLY[] = {{4,SH7_P0}};
static const arrow_tri SH8_TRI[] = {
  {{0.5000f,-0.5000f,-0.1703f},{0.0000f,0.4527f,0.1780f}},
  {{0.5000f,-0.1703f,-0.2802f},{0.0000f,0.1780f,0.0000f}},
  {{0.5000f,-0.2802f,-0.1703f},{0.0000f,0.0000f,-0.1780f}},
  {{-0.1703f,-0.5000f,0.5000f},{-0.1780f,-0.4527f,0.0000f}},
};
static const float SH8_P0[] = {0.5000f,0.0000f,-0.5000f,-0.4527f,-0.1703f,-0.1780f,-0.2802f,0.0000f,-0.1703f,0.1780f,-0.5000f,0.4527f};
static const arrow_poly SH8_POLY[] = {{6,SH8_P0}};
static const arrow_tri SH9_TRI[] = {
  {{-0.5000f,-0.5000f,0.0876f},{0.0268f,-0.0268f,-0.0268f}},
  {{0.0876f,0.1804f,0.5000f},{-0.0268f,-0.1464f,0.0000f}},
  {{-0.5000f,0.0876f,0.5000f},{0.0268f,-0.0268f,0.0000f}},
  {{0.5000f,0.1804f,0.0876f},{0.0000f,0.1464f,0.0268f}},
  {{0.5000f,0.0876f,-0.5000f},{0.0000f,0.0268f,0.0268f}},
};
static const float SH9_P0[] = {-0.5000f,-0.0268f,0.0876f,-0.0268f,0.1804f,-0.1464f,0.5000f,0.0000f,0.1804f,0.1464f,0.0876f,0.0268f,-0.5000f,0.0268f};
static const arrow_poly SH9_POLY[] = {{7,SH9_P0}};
static const arrow_tri SH10_TRI[] = {
  {{-0.4125f,-0.4125f,0.2854f},{0.0271f,-0.0271f,-0.0271f}},
  {{0.2854f,0.2854f,0.5000f},{-0.0271f,-0.1375f,0.0000f}},
  {{-0.4125f,0.2854f,0.5000f},{0.0271f,-0.0271f,0.0000f}},
  {{0.5000f,0.2854f,0.2854f},{0.0000f,0.1375f,0.0271f}},
  {{0.5000f,0.2854f,-0.4125f},{0.0000f,0.0271f,0.0271f}},
  {{-0.5000f,-0.3750f,-0.2604f},{-0.0271f,-0.1979f,-0.1979f}},
  {{-0.2604f,-0.3854f,-0.5000f},{-0.1979f,-0.0271f,-0.0271f}},
  {{-0.3750f,-0.5000f,-0.3854f},{0.1979f,0.0271f,0.0271f}},
  {{-0.3854f,-0.2604f,-0.3750f},{0.0271f,0.1979f,0.1979f}},
  {{-0.2458f,-0.1208f,-0.0063f},{-0.0271f,-0.1979f,-0.1979f}},
  {{-0.0063f,-0.1313f,-0.2458f},{-0.1979f,-0.0271f,-0.0271f}},
  {{-0.1208f,-0.2458f,-0.1313f},{0.1979f,0.0271f,0.0271f}},
  {{-0.1313f,-0.0063f,-0.1208f},{0.0271f,0.1979f,0.1979f}},
};
static const float SH10_P0[] = {-0.4125f,-0.0271f,0.2854f,-0.0271f,0.2854f,-0.1375f,0.5000f,0.0000f,0.2854f,0.1375f,0.2854f,0.0271f,-0.4125f,0.0271f};
static const float SH10_P1[] = {-0.5000f,-0.0271f,-0.3854f,-0.0271f,-0.2604f,-0.1979f,-0.3750f,-0.1979f};
static const float SH10_P2[] = {-0.5000f,0.0271f,-0.3854f,0.0271f,-0.2604f,0.1979f,-0.3750f,0.1979f};
static const float SH10_P3[] = {-0.2458f,-0.0271f,-0.1313f,-0.0271f,-0.0063f,-0.1979f,-0.1208f,-0.1979f};
static const float SH10_P4[] = {-0.2458f,0.0271f,-0.1313f,0.0271f,-0.0063f,0.1979f,-0.1208f,0.1979f};
static const arrow_poly SH10_POLY[] = {{7,SH10_P0},{4,SH10_P1},{4,SH10_P2},{4,SH10_P3},{4,SH10_P4}};
static const arrow_tri SH11_TRI[] = {
  {{0.5000f,0.2609f,-0.5000f},{0.0000f,0.3391f,0.3391f}},
  {{0.5000f,-0.5000f,-0.5000f},{0.0000f,0.3391f,-0.3391f}},
  {{-0.5000f,0.2609f,0.5000f},{-0.3391f,-0.3391f,0.0000f}},
};
static const float SH11_P0[] = {0.5000f,0.0000f,0.2609f,-0.3391f,-0.5000f,-0.3391f,-0.5000f,0.3391f,0.2609f,0.3391f};
static const arrow_poly SH11_POLY[] = {{5,SH11_P0}};

static const arrow_shape_def ARROW_SHAPES[ARROW_SHAPE_COUNT] = {
  {2,SH0_TRI,1,SH0_POLY,2,SH0_OVL,1,-0.1436f,0.0000f,0.5000f,0.0000f},
  {5,SH1_TRI,1,SH1_POLY,0,NULL,0,0,0,0,0},
  {5,SH2_TRI,1,SH2_POLY,0,NULL,0,0,0,0,0},
  {5,SH3_TRI,1,SH3_POLY,0,NULL,0,0,0,0,0},
  {4,SH4_TRI,1,SH4_POLY,0,NULL,0,0,0,0,0},
  {8,SH5_TRI,2,SH5_POLY,0,NULL,0,0,0,0,0},
  {1,SH6_TRI,1,SH6_POLY,0,NULL,0,0,0,0,0},
  {2,SH7_TRI,1,SH7_POLY,0,NULL,0,0,0,0,0},
  {4,SH8_TRI,1,SH8_POLY,0,NULL,0,0,0,0,0},
  {5,SH9_TRI,1,SH9_POLY,0,NULL,0,0,0,0,0},
  {13,SH10_TRI,5,SH10_POLY,0,NULL,0,0,0,0,0},
  {3,SH11_TRI,1,SH11_POLY,0,NULL,0,0,0,0,0},
};

static inline float arrow_shape_clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/* f > 0 lightens towards white, f < 0 darkens towards black. */
static inline ImU32 arrow_shape_shade(const float c[4], float f, float a) {
  float r = c[0], g = c[1], b = c[2];
  if (f >= 0.0f) { r += (1.0f - r) * f; g += (1.0f - g) * f; b += (1.0f - b) * f; }
  else { r *= 1.0f + f; g *= 1.0f + f; b *= 1.0f + f; }
  return ARROW_COL32((int)(arrow_shape_clamp01(r) * 255.0f + 0.5f),
                     (int)(arrow_shape_clamp01(g) * 255.0f + 0.5f),
                     (int)(arrow_shape_clamp01(b) * 255.0f + 0.5f),
                     (int)(arrow_shape_clamp01(a) * 255.0f + 0.5f));
}

/* Draws shape `shape` centred on (cx, cy), rotated by (cs, sn) = (cos, sin) of
   the steering angle, `size` pixels wide, in colour rgba[4] (0..1, as picked
   by igColorEdit4) with the colour's alpha multiplied by `alpha`. The fill is
   lit from the top of the screen (light at the top, darker at the bottom) so
   it keeps the same look whichever way the arrow points. */
static inline void arrow_shape_draw(ImDrawList* dl, int shape, float cx, float cy,
                                    float cs, float sn, float size,
                                    const float rgba[4], float alpha) {
  if (shape < 0 || shape >= ARROW_SHAPE_COUNT) shape = 0;
  const arrow_shape_def* sh = &ARROW_SHAPES[shape];
  float a = arrow_shape_clamp01(rgba[3] * alpha);
  ImVec2 white;
  igGetFontTexUvWhitePixel(&white);

  for (int i = 0; i < sh->ntri; ++i) {
    const arrow_tri* t = &sh->tri[i];
    ImVec2 p[3];
    ImU32 col[3];
    for (int k = 0; k < 3; ++k) {
      float lx = t->x[k] * size, ly = t->y[k] * size;
      p[k].x = cx + lx * cs - ly * sn;
      p[k].y = cy + lx * sn + ly * cs;
      float tt = arrow_shape_clamp01(0.5f + (p[k].y - cy) / size);
      float f = tt < 0.5f ? 0.40f * (1.0f - 2.0f * tt) : -0.28f * (2.0f * tt - 1.0f);
      col[k] = arrow_shape_shade(rgba, f, a);
    }
    ImDrawList_PrimReserve(dl, 3, 3);
    ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
    ImDrawList_PrimWriteIdx(dl, base);
    ImDrawList_PrimWriteIdx(dl, (ImDrawIdx)(base + 1));
    ImDrawList_PrimWriteIdx(dl, (ImDrawIdx)(base + 2));
    for (int k = 0; k < 3; ++k) ImDrawList_PrimWriteVtx(dl, p[k], white, col[k]);
  }

  for (int i = 0; i < sh->novl; ++i) {
    const arrow_ovl* o = &sh->ovl[i];
    ImVec2 p[3];
    for (int k = 0; k < 3; ++k) {
      float lx = o->t.x[k] * size, ly = o->t.y[k] * size;
      p[k].x = cx + lx * cs - ly * sn;
      p[k].y = cy + lx * sn + ly * cs;
    }
    ImDrawList_AddTriangleFilled(dl, p[0], p[1], p[2],
        ARROW_COL32(o->r, o->g, o->b, (int)(o->a * a + 0.5f)));
  }

  float th = size * 0.018f;
  if (th < 1.4f) th = 1.4f;
  ImU32 edge = arrow_shape_shade(rgba, -0.55f, a);
  for (int i = 0; i < sh->npoly; ++i) {
    ImVec2 pts[16];
    int n = sh->poly[i].n > 16 ? 16 : sh->poly[i].n;
    for (int k = 0; k < n; ++k) {
      float lx = sh->poly[i].p[k * 2] * size, ly = sh->poly[i].p[k * 2 + 1] * size;
      pts[k].x = cx + lx * cs - ly * sn;
      pts[k].y = cy + lx * sn + ly * cs;
    }
    ImDrawList_AddPolyline(dl, pts, n, edge, ImDrawFlags_Closed, th);
  }
  if (sh->has_ridge) {
    ImVec2 r0 = {cx + sh->rx0 * size * cs - sh->ry0 * size * sn, cy + sh->rx0 * size * sn + sh->ry0 * size * cs};
    ImVec2 r1 = {cx + sh->rx1 * size * cs - sh->ry1 * size * sn, cy + sh->rx1 * size * sn + sh->ry1 * size * cs};
    ImDrawList_AddLine(dl, r0, r1, arrow_shape_shade(rgba, 0.75f, a), th);
  }
}

#endif
