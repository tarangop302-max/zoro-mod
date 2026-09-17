#include "crystal_theme.h"

#include <math.h>

static ImU32 crystal_col(float r, float g, float b, float a) {
  return igColorConvertFloat4ToU32((ImVec4){r, g, b, a});
}

/* A soft circular glow with a smooth falloff, built from many thin,
   NON-overlapping stroked rings (each ring's alpha directly IS the visible
   alpha there -- no compounding blend math to get wrong) rather than a
   handful of overlapping filled discs. A small number of overlapping filled
   circles very visibly banded into hard concentric rings on-device (their
   alphas compound where they overlap, and that compounded total jumps in
   big, increasingly large steps toward the outer edge -- exactly the
   "target/bullseye" artifact seen in testing), since ImGui has no real
   gradient fill to fall back on. Many thin rings with a smoothly-decreasing
   alpha curve is the standard workaround and reads as an actual soft glow
   instead of stacked circles. */
static void crystal_draw_glow_blob(ImDrawList* dl, ImVec2 center,
                                    float radius, float r, float g, float b,
                                    float peak_alpha) {
  const int RINGS = 40;
  float step = radius / RINGS;
  for (int i = RINGS; i >= 1; i--) {
    float t = (float)i / RINGS;             /* 1.0 at edge, ~0 at center */
    float falloff = (1.0f - t) * (1.0f - t); /* smooth, 0 at edge */
    float alpha = peak_alpha * falloff;
    if (alpha < 0.002f) continue;
    ImDrawList_AddCircle(dl, center, step * i, crystal_col(r, g, b, alpha),
                        64, step * 1.6f);
  }
}

void crystal_draw_background_alpha(tenv* env, float alpha) {
  tcontext* ctx = env->ctx;
  ImDrawList* dl = igGetWindowDrawList();
  float w = ctx->size[0];
  float h = ctx->size[1];

  ImDrawList_AddRectFilledMultiColor(
      dl, (ImVec2){0, 0}, (ImVec2){w, h},
      crystal_col(0.078f, 0.039f, 0.141f, alpha) /* top-left */,
      crystal_col(0.176f, 0.106f, 0.306f, alpha) /* top-right */,
      crystal_col(0.141f, 0.082f, 0.259f, alpha) /* bottom-right */,
      crystal_col(0.114f, 0.063f, 0.212f, alpha) /* bottom-left */);

  /* Two soft purple glow blobs, roughly matching the approved mockup's
     glow positions. Scaled by the same alpha so a translucent background
     (e.g. the skin editor, which has a live preview sitting underneath)
     gets a proportionally subtler glow rather than two full-strength
     blobs sitting on top of a faint wash. */
  ImVec2 glow_a = {w * 0.72f, h * 0.30f};
  ImVec2 glow_b = {w * 0.20f, h * 0.80f};
  crystal_draw_glow_blob(dl, glow_a, 400.0f, 0.592f, 0.353f, 1.0f,
                        0.30f * alpha);
  crystal_draw_glow_blob(dl, glow_b, 340.0f, 0.353f, 0.235f, 0.784f,
                        0.24f * alpha);
}

void crystal_draw_background(tenv* env) {
  crystal_draw_background_alpha(env, 1.0f);
}

/* Bilinear-interpolate the 4 base corner colors at normalized position
   (u,v) in [0,1]x[0,1] of the full background rect. Used so a sub-rect
   (a "band" around a hole) gets exactly the color it would have had as
   part of the single full-screen gradient -- no visible seam between
   bands. */
static ImVec4 crystal_lerp_corner(ImVec4 tl, ImVec4 tr, ImVec4 br, ImVec4 bl,
                                  float u, float v) {
  ImVec4 top = {tl.x + (tr.x - tl.x) * u, tl.y + (tr.y - tl.y) * u,
               tl.z + (tr.z - tl.z) * u, tl.w + (tr.w - tl.w) * u};
  ImVec4 bot = {bl.x + (br.x - bl.x) * u, bl.y + (br.y - bl.y) * u,
               bl.z + (br.z - bl.z) * u, bl.w + (br.w - bl.w) * u};
  return (ImVec4){top.x + (bot.x - top.x) * v, top.y + (bot.y - top.y) * v,
                  top.z + (bot.z - top.z) * v, top.w + (bot.w - top.w) * v};
}

static void crystal_fill_band(ImDrawList* dl, float w, float h, ImVec4 tl,
                              ImVec4 tr, ImVec4 br, ImVec4 bl, float x0,
                              float y0, float x1, float y1) {
  if (x1 <= x0 || y1 <= y0) return;
  float u0 = x0 / w, u1 = x1 / w, v0 = y0 / h, v1 = y1 / h;
  ImU32 c_tl = igColorConvertFloat4ToU32(crystal_lerp_corner(tl, tr, br, bl, u0, v0));
  ImU32 c_tr = igColorConvertFloat4ToU32(crystal_lerp_corner(tl, tr, br, bl, u1, v0));
  ImU32 c_br = igColorConvertFloat4ToU32(crystal_lerp_corner(tl, tr, br, bl, u1, v1));
  ImU32 c_bl = igColorConvertFloat4ToU32(crystal_lerp_corner(tl, tr, br, bl, u0, v1));
  ImDrawList_AddRectFilledMultiColor(dl, (ImVec2){x0, y0}, (ImVec2){x1, y1},
                                    c_tl, c_tr, c_br, c_bl);
}

/* Splits the screen into up to 7 non-overlapping "bands" that tile
   everything EXCEPT the (up to) two given rects, assuming excl1 sits
   above excl2 (or only excl1 is present). Each band is {x0,y0,x1,y1}. */
static int crystal_compute_bands(float w, float h, ImVec2 e1_min,
                                 ImVec2 e1_max, ImVec2 e2_min, ImVec2 e2_max,
                                 float out[][4]) {
  bool has1 = e1_max.x > e1_min.x && e1_max.y > e1_min.y;
  bool has2 = e2_max.x > e2_min.x && e2_max.y > e2_min.y;
  int n = 0;
  float cursor_y = 0;

  if (has1) {
    float ex0 = fmaxf(0.0f, fminf(e1_min.x, w));
    float ex1 = fmaxf(0.0f, fminf(e1_max.x, w));
    float ey0 = fmaxf(0.0f, fminf(e1_min.y, h));
    float ey1 = fmaxf(0.0f, fminf(e1_max.y, h));
    out[n][0] = 0; out[n][1] = cursor_y; out[n][2] = w; out[n][3] = ey0; n++;
    out[n][0] = 0; out[n][1] = ey0; out[n][2] = ex0; out[n][3] = ey1; n++;
    out[n][0] = ex1; out[n][1] = ey0; out[n][2] = w; out[n][3] = ey1; n++;
    cursor_y = ey1;
  }
  if (has2) {
    float ex0 = fmaxf(0.0f, fminf(e2_min.x, w));
    float ex1 = fmaxf(0.0f, fminf(e2_max.x, w));
    float ey0 = fmaxf(0.0f, fminf(e2_min.y, h));
    float ey1 = fmaxf(0.0f, fminf(e2_max.y, h));
    out[n][0] = 0; out[n][1] = cursor_y; out[n][2] = w; out[n][3] = ey0; n++;
    out[n][0] = 0; out[n][1] = ey0; out[n][2] = ex0; out[n][3] = ey1; n++;
    out[n][0] = ex1; out[n][1] = ey0; out[n][2] = w; out[n][3] = ey1; n++;
    cursor_y = ey1;
  }
  out[n][0] = 0; out[n][1] = cursor_y; out[n][2] = w; out[n][3] = h; n++;

  return n;
}

void crystal_draw_background_alpha_excl2(tenv* env, float alpha,
                                         ImVec2 excl1_min, ImVec2 excl1_max,
                                         ImVec2 excl2_min, ImVec2 excl2_max) {
  tcontext* ctx = env->ctx;
  ImDrawList* dl = igGetWindowDrawList();
  float w = ctx->size[0];
  float h = ctx->size[1];

  ImVec4 tl = {0.078f, 0.039f, 0.141f, alpha};
  ImVec4 tr = {0.176f, 0.106f, 0.306f, alpha};
  ImVec4 br = {0.141f, 0.082f, 0.259f, alpha};
  ImVec4 bl = {0.114f, 0.063f, 0.212f, alpha};

  float bands[7][4];
  int n = crystal_compute_bands(w, h, excl1_min, excl1_max, excl2_min,
                                excl2_max, bands);

  for (int i = 0; i < n; i++) {
    crystal_fill_band(dl, w, h, tl, tr, br, bl, bands[i][0], bands[i][1],
                      bands[i][2], bands[i][3]);
  }

  /* Glow blobs, redrawn once per band and clipped to that band, so they
     never wash over the excluded content -- same visual result as the
     unclipped version everywhere else on screen. */
  ImVec2 glow_a = {w * 0.72f, h * 0.30f};
  ImVec2 glow_b = {w * 0.20f, h * 0.80f};

  for (int i = 0; i < n; i++) {
    if (bands[i][2] <= bands[i][0] || bands[i][3] <= bands[i][1]) continue;
    igPushClipRect((ImVec2){bands[i][0], bands[i][1]},
                   (ImVec2){bands[i][2], bands[i][3]}, true);
    crystal_draw_glow_blob(dl, glow_a, 400.0f, 0.592f, 0.353f, 1.0f, 0.30f * alpha);
    crystal_draw_glow_blob(dl, glow_b, 340.0f, 0.353f, 0.235f, 0.784f, 0.24f * alpha);
    igPopClipRect();
  }
}

/* Thin light line along a widget's top edge -- call right after drawing it
   (uses the last item's rect). */
void crystal_sheen(void) {
  ImVec2 mn, mx;
  igGetItemRectMin(&mn);
  igGetItemRectMax(&mx);
  if (mx.x - mn.x < 20) return;
  ImDrawList* dl = igGetWindowDrawList();
  ImDrawList_AddLine(dl, (ImVec2){mn.x + 10, mn.y + 1.5f},
                      (ImVec2){mx.x - 10, mn.y + 1.5f},
                      crystal_col(1.0f, 1.0f, 1.0f, 0.12f), 1.5f);
}

/* Soft rounded glow behind an upcoming widget -- call BEFORE drawing it,
   with its known screen-space position/size, so the glow ends up behind.
   Kept deliberately tight: packed-together button rows need a small expand
   distance or the glow bleeds into neighboring rows instead of reading as a
   glow around just one button. */
void crystal_glow_rect(ImVec2 pos, ImVec2 size, float r, float g, float b) {
  ImDrawList* dl = igGetWindowDrawList();
  for (int i = 4; i >= 1; i--) {
    float expand = i * 2.5f;
    ImDrawList_AddRectFilled(
        dl, (ImVec2){pos.x - expand, pos.y - expand},
        (ImVec2){pos.x + size.x + expand, pos.y + size.y + expand},
        crystal_col(r, g, b, 0.05f * i), 14.0f + expand * 0.4f,
        ImDrawFlags_None);
  }
}

/* Pushes the ambient glass colors used by most widgets on a crystal-themed
   screen. Must be matched with crystal_pop_theme(). */
void crystal_push_theme(void) {
  igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, 12.0f);

  igPushStyleColor_Vec4(ImGuiCol_Border,
                        (ImVec4){0.690f, 0.580f, 0.960f, 0.35f});
  igPushStyleColor_Vec4(ImGuiCol_FrameBg,
                        (ImVec4){0.373f, 0.290f, 0.607f, 0.28f});
  igPushStyleColor_Vec4(ImGuiCol_FrameBgHovered,
                        (ImVec4){0.430f, 0.330f, 0.680f, 0.34f});
  igPushStyleColor_Vec4(ImGuiCol_FrameBgActive,
                        (ImVec4){0.470f, 0.360f, 0.720f, 0.42f});
  igPushStyleColor_Vec4(ImGuiCol_Button,
                        (ImVec4){0.373f, 0.290f, 0.607f, 0.28f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                        (ImVec4){0.430f, 0.330f, 0.680f, 0.34f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
                        (ImVec4){0.470f, 0.360f, 0.720f, 0.42f});
  igPushStyleColor_Vec4(ImGuiCol_Header,
                        (ImVec4){0.430f, 0.330f, 0.680f, 0.34f});
  igPushStyleColor_Vec4(ImGuiCol_HeaderHovered,
                        (ImVec4){0.470f, 0.360f, 0.720f, 0.42f});
  igPushStyleColor_Vec4(ImGuiCol_Text,
                        (ImVec4){0.945f, 0.925f, 1.0f, 1.0f});
}

#define CRYSTAL_COLOR_COUNT 10
#define CRYSTAL_STYLEVAR_COUNT 1

void crystal_pop_theme(void) {
  igPopStyleColor(CRYSTAL_COLOR_COUNT);
  igPopStyleVar(CRYSTAL_STYLEVAR_COUNT);
}
