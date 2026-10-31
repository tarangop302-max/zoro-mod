#include "crystal_theme.h"

#include <math.h>

#include "settings.h"

/* ---- theme palette (see crystal_theme.h) ------------------------------- */
#define CRYSTAL_BASE_HUE 262.0f

static float s_theme_hue = CRYSTAL_BASE_HUE;
static float s_theme_sat = 1.0f;

static void crystal_rgb_to_hsv(float r, float g, float b, float* h, float* s,
                               float* v) {
  float mx = fmaxf(r, fmaxf(g, b));
  float mn = fminf(r, fminf(g, b));
  float d = mx - mn;
  *v = mx;
  *s = mx > 0.0f ? d / mx : 0.0f;
  float hh = 0.0f;
  if (d > 1e-6f) {
    if (mx == r)
      hh = 60.0f * fmodf((g - b) / d, 6.0f);
    else if (mx == g)
      hh = 60.0f * ((b - r) / d + 2.0f);
    else
      hh = 60.0f * ((r - g) / d + 4.0f);
    if (hh < 0.0f) hh += 360.0f;
  }
  *h = hh;
}

static void crystal_hsv_to_rgb(float h, float s, float v, float* r, float* g,
                               float* b) {
  h = fmodf(h, 360.0f);
  if (h < 0.0f) h += 360.0f;
  float c = v * s;
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  float m = v - c;
  float rr = 0, gg = 0, bb = 0;
  if (h < 60.0f) { rr = c; gg = x; }
  else if (h < 120.0f) { rr = x; gg = c; }
  else if (h < 180.0f) { gg = c; bb = x; }
  else if (h < 240.0f) { gg = x; bb = c; }
  else if (h < 300.0f) { rr = x; bb = c; }
  else { rr = c; bb = x; }
  *r = rr + m;
  *g = gg + m;
  *b = bb + m;
}

void crystal_theme_set_accent(float r, float g, float b) {
  float h, s, v;
  crystal_rgb_to_hsv(r, g, b, &h, &s, &v);
  s_theme_hue = h;
  /* Default accent (0.592, 0.353, 1.0) has saturation ~0.647. */
  float k = s / 0.647f;
  if (k < 0.06f) k = 0.06f;
  if (k > 1.30f) k = 1.30f;
  s_theme_sat = k;
}

void crystal_theme_reset_default(void) {
  s_theme_hue = CRYSTAL_BASE_HUE;
  s_theme_sat = 1.0f;
}

ImVec4 crystal_tint(ImVec4 c) {
  if (fabsf(s_theme_hue - CRYSTAL_BASE_HUE) < 0.5f &&
      fabsf(s_theme_sat - 1.0f) < 0.01f)
    return c;
  float h, s, v;
  crystal_rgb_to_hsv(c.x, c.y, c.z, &h, &s, &v);
  /* Only the purple family is re-mapped. */
  if (s < 0.10f || h < 225.0f || h > 310.0f) return c;
  float ns = s * s_theme_sat;
  if (ns > 1.0f) ns = 1.0f;
  float r, g, b;
  crystal_hsv_to_rgb(h + (s_theme_hue - CRYSTAL_BASE_HUE), ns, v, &r, &g, &b);
  return (ImVec4){r, g, b, c.w};
}

ImU32 crystal_tint_u32(ImU32 c) {
  if (fabsf(s_theme_hue - CRYSTAL_BASE_HUE) < 0.5f &&
      fabsf(s_theme_sat - 1.0f) < 0.01f)
    return c;
  ImVec4 f;
  igColorConvertU32ToFloat4(&f, c);
  return igColorConvertFloat4ToU32(crystal_tint(f));
}

static ImU32 crystal_col(float r, float g, float b, float a) {
  return igColorConvertFloat4ToU32(crystal_tint((ImVec4){r, g, b, a}));
}

static ImU32 crystal_col_raw(float r, float g, float b, float a) {
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
static void crystal_glow_impl(ImDrawList* dl, ImVec2 center, float radius,
                              float r, float g, float b, float peak_alpha,
                              bool tinted) {
  const int RINGS = 40;
  float step = radius / RINGS;
  for (int i = RINGS; i >= 1; i--) {
    float t = (float)i / RINGS;             /* 1.0 at edge, ~0 at center */
    float falloff = (1.0f - t) * (1.0f - t); /* smooth, 0 at edge */
    float alpha = peak_alpha * falloff;
    if (alpha < 0.002f) continue;
    ImDrawList_AddCircle(dl, center, step * i,
                         tinted ? crystal_col(r, g, b, alpha)
                                : crystal_col_raw(r, g, b, alpha),
                         64, step * 1.6f);
  }
}

void crystal_draw_glow_blob(ImDrawList* dl, ImVec2 center, float radius,
                            float r, float g, float b, float peak_alpha) {
  crystal_glow_impl(dl, center, radius, r, g, b, peak_alpha, true);
}

void crystal_draw_glow_blob_raw(ImDrawList* dl, ImVec2 center, float radius,
                                float r, float g, float b, float peak_alpha) {
  crystal_glow_impl(dl, center, radius, r, g, b, peak_alpha, false);
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

  ImVec4 tl = crystal_tint((ImVec4){0.078f, 0.039f, 0.141f, alpha});
  ImVec4 tr = crystal_tint((ImVec4){0.176f, 0.106f, 0.306f, alpha});
  ImVec4 br = crystal_tint((ImVec4){0.141f, 0.082f, 0.259f, alpha});
  ImVec4 bl = crystal_tint((ImVec4){0.114f, 0.063f, 0.212f, alpha});

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

/* ============================================================================
 * Fixed-size modal panel
 * ========================================================================== */

#define CRYSTAL_PANEL_COLORS 20
#define CRYSTAL_PANEL_VARS 10

static bool s_panel_font_pushed = false;
static bool s_panel_body_open = false;
static float s_panel_wf = 0.0f, s_panel_hf = 0.0f, s_panel_footer = 0.0f;

void crystal_panel_set_layout(float w_frac, float h_frac, float footer_h) {
  s_panel_wf = w_frac;
  s_panel_hf = h_frac;
  s_panel_footer = footer_h;
}

static float crystal_ui_scale(tenv* env) {
  float sw = (float)env->ctx->size[0];
  float sh = (float)env->ctx->size[1];
  float se = sw < sh ? sw : sh;
  float s = se / 720.0f;
  if (s < 0.75f) s = 0.75f;
  if (s > 1.30f) s = 1.30f;
  return s;
}

void crystal_card_bg(ImVec2 pos, ImVec2 size, float r, float g, float b) {
  ImDrawList* dl = igGetWindowDrawList();
  ImVec2 p1 = {pos.x + size.x, pos.y + size.y};
  ImDrawList_AddRectFilled(dl, pos, p1, crystal_col(r, g, b, 0.14f), 12.0f,
                           ImDrawFlags_None);
  ImDrawList_AddRect(dl, pos, p1, crystal_col(r, g, b, 0.45f), 12.0f,
                     ImDrawFlags_None, 1.2f);
}

void crystal_section(const char* label) {
  ImVec2 p;
  igGetCursorScreenPos(&p);
  float lh = igGetTextLineHeight();
  ImDrawList* dl = igGetWindowDrawList();
  igSpacing();
  igGetCursorScreenPos(&p);
  ImDrawList_AddRectFilled(dl, (ImVec2){p.x, p.y + 2.0f},
                           (ImVec2){p.x + 4.0f, p.y + lh - 2.0f},
                           crystal_col(0.690f, 0.580f, 0.960f, 1.0f), 2.0f,
                           ImDrawFlags_None);
  igSetCursorScreenPos((ImVec2){p.x + 12.0f, p.y});
  igTextColored((ImVec4){0.800f, 0.720f, 1.0f, 1.0f}, "%s", label);
  igSpacing();
}

bool crystal_panel_begin(tenv* env, const char* id, const char* title,
                         const char* subtitle, float max_w) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  const float s = crystal_ui_scale(env);
  const float sw = (float)ctx->size[0];
  const float sh = (float)ctx->size[1];
  float w = fminf(max_w * s, sw - 24.0f);
  float h = fminf(sh * 0.90f, 780.0f * s);
  if (s_panel_wf > 0.0f) w = sw * s_panel_wf;
  if (s_panel_hf > 0.0f) h = sh * s_panel_hf;
  const float footer_h = s_panel_footer;
  s_panel_wf = s_panel_hf = s_panel_footer = 0.0f; /* one-shot */
  if (w < 200.0f) w = 200.0f;
  if (h < 160.0f) h = 160.0f;
  const float pad = 14.0f * s;

  igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){pad, pad});
  igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 20.0f * s);
  igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, 1.5f);
  igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, 12.0f * s);
  igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding,
                      (ImVec2){10.0f * s, 9.0f * s});
  igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing,
                      (ImVec2){8.0f * s, 10.0f * s});
  igPushStyleVar_Float(ImGuiStyleVar_ScrollbarSize, 20.0f * s);
  igPushStyleVar_Float(ImGuiStyleVar_ScrollbarRounding, 10.0f * s);
  igPushStyleVar_Float(ImGuiStyleVar_GrabMinSize, 30.0f * s);
  igPushStyleVar_Float(ImGuiStyleVar_GrabRounding, 12.0f * s);

  igPushStyleColor_Vec4(ImGuiCol_WindowBg,
                        (ImVec4){0.060f, 0.040f, 0.110f, 0.97f});
  igPushStyleColor_Vec4(ImGuiCol_Border,
                        (ImVec4){0.690f, 0.580f, 0.960f, 0.55f});
  igPushStyleColor_Vec4(ImGuiCol_ModalWindowDimBg,
                        (ImVec4){0.020f, 0.010f, 0.050f, 0.74f});
  igPushStyleColor_Vec4(ImGuiCol_FrameBg,
                        (ImVec4){0.373f, 0.290f, 0.607f, 0.28f});
  igPushStyleColor_Vec4(ImGuiCol_FrameBgHovered,
                        (ImVec4){0.430f, 0.330f, 0.680f, 0.34f});
  igPushStyleColor_Vec4(ImGuiCol_FrameBgActive,
                        (ImVec4){0.470f, 0.360f, 0.720f, 0.42f});
  igPushStyleColor_Vec4(ImGuiCol_Button,
                        (ImVec4){0.373f, 0.290f, 0.607f, 0.40f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                        (ImVec4){0.430f, 0.330f, 0.680f, 0.50f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
                        (ImVec4){0.470f, 0.360f, 0.720f, 0.60f});
  igPushStyleColor_Vec4(ImGuiCol_SliderGrab,
                        (ImVec4){0.690f, 0.580f, 0.960f, 0.95f});
  igPushStyleColor_Vec4(ImGuiCol_SliderGrabActive,
                        (ImVec4){0.850f, 0.760f, 1.000f, 1.00f});
  igPushStyleColor_Vec4(ImGuiCol_CheckMark,
                        (ImVec4){0.800f, 0.700f, 1.000f, 1.00f});
  igPushStyleColor_Vec4(ImGuiCol_Text,
                        (ImVec4){0.945f, 0.925f, 1.000f, 1.00f});
  igPushStyleColor_Vec4(ImGuiCol_Separator,
                        (ImVec4){0.690f, 0.580f, 0.960f, 0.30f});
  igPushStyleColor_Vec4(ImGuiCol_ScrollbarBg,
                        (ImVec4){0.373f, 0.290f, 0.607f, 0.10f});
  igPushStyleColor_Vec4(ImGuiCol_ScrollbarGrab,
                        (ImVec4){0.600f, 0.500f, 0.880f, 0.65f});
  igPushStyleColor_Vec4(ImGuiCol_ScrollbarGrabHovered,
                        (ImVec4){0.690f, 0.580f, 0.960f, 0.80f});
  igPushStyleColor_Vec4(ImGuiCol_ScrollbarGrabActive,
                        (ImVec4){0.800f, 0.700f, 1.000f, 0.95f});
  igPushStyleColor_Vec4(ImGuiCol_Header,
                        (ImVec4){0.430f, 0.330f, 0.680f, 0.34f});
  igPushStyleColor_Vec4(ImGuiCol_HeaderHovered,
                        (ImVec4){0.470f, 0.360f, 0.720f, 0.42f});

  /* Explicit size applied EVERY frame (Cond_Always) and no auto-resize flag:
     the panel can never drift smaller. */
  igSetNextWindowPos((ImVec2){sw * 0.5f, sh * 0.5f}, ImGuiCond_Always,
                     (ImVec2){0.5f, 0.5f});
  igSetNextWindowSize((ImVec2){w, h}, ImGuiCond_Always);

  bool open = igBeginPopupModal(
      id, NULL,
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
          ImGuiWindowFlags_NoScrollWithMouse |
          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
  if (!open) {
    igPopStyleColor(CRYSTAL_PANEL_COLORS);
    igPopStyleVar(CRYSTAL_PANEL_VARS);
    return false;
  }

  ImFont* font = usr->imgui_data.regular_font[usr->usrs.ui_font_size];
  s_panel_font_pushed = (font != NULL);
  if (font) igPushFont(font, font->LegacySize);

  ImVec2 wp, wsz;
  igGetWindowPos(&wp);
  igGetWindowSize(&wsz);
  ImDrawList* dl = igGetWindowDrawList();
  const bool appearing = igIsWindowAppearing();
  static double s_open_time = 0.0;
  if (appearing) s_open_time = igGetTime();

  /* Soft glow inside the top of the panel. */
  ImDrawList_PushClipRect(dl, wp, (ImVec2){wp.x + wsz.x, wp.y + wsz.y}, true);
  crystal_draw_glow_blob(dl, (ImVec2){wp.x + wsz.x * 0.80f, wp.y + 4.0f},
                         wsz.x * 0.55f, 0.592f, 0.353f, 1.0f, 0.22f);
  ImDrawList_PopClipRect(dl);

  /* ---- header: dot + title + subtitle, round close button on the right --- */
  ImVec2 cp;
  igGetCursorScreenPos(&cp);
  float lh = igGetTextLineHeight();
  float close_d = lh * 1.9f;

  ImDrawList_AddCircleFilled(dl, (ImVec2){cp.x + 6.0f * s, cp.y + lh * 0.5f},
                             5.0f * s, crystal_col(0.800f, 0.700f, 1.0f, 1.0f),
                             16);
  ImDrawList_AddCircle(dl, (ImVec2){cp.x + 6.0f * s, cp.y + lh * 0.5f},
                       9.0f * s, crystal_col(0.690f, 0.580f, 0.960f, 0.45f),
                       20, 1.5f);
  igSetCursorScreenPos((ImVec2){cp.x + 24.0f * s, cp.y});
  igTextColored((ImVec4){0.850f, 0.780f, 1.0f, 1.0f}, "%s", title);
  if (subtitle && subtitle[0]) {
    igSetCursorScreenPos((ImVec2){cp.x + 24.0f * s, cp.y + lh + 2.0f});
    igPushStyleColor_Vec4(ImGuiCol_Text,
                          (ImVec4){0.640f, 0.590f, 0.780f, 1.0f});
    igText("%s", subtitle);
    igPopStyleColor(1);
  }

  ImVec2 close_pos = {wp.x + wsz.x - pad - close_d, cp.y - 2.0f};
  igSetCursorScreenPos(close_pos);
  bool close_pressed =
      igInvisibleButton("##panel_close", (ImVec2){close_d, close_d}, 0);
  bool close_hot = igIsItemHovered(0);
  bool close_held = igIsItemActive();
  {
    ImVec2 c = {close_pos.x + close_d * 0.5f, close_pos.y + close_d * 0.5f};
    ImDrawList_AddCircleFilled(
        dl, c, close_d * 0.5f,
        close_held ? crystal_col(0.470f, 0.360f, 0.720f, 0.80f)
                   : crystal_col(0.373f, 0.290f, 0.607f, close_hot ? 0.60f : 0.40f),
        24);
    ImDrawList_AddCircle(dl, c, close_d * 0.5f,
                         crystal_col(0.690f, 0.580f, 0.960f, 0.60f), 24, 1.2f);
    float k = close_d * 0.22f;
    ImU32 xc = crystal_col(0.945f, 0.925f, 1.0f, 1.0f);
    ImDrawList_AddLine(dl, (ImVec2){c.x - k, c.y - k}, (ImVec2){c.x + k, c.y + k},
                       xc, 2.2f);
    ImDrawList_AddLine(dl, (ImVec2){c.x - k, c.y + k}, (ImVec2){c.x + k, c.y - k},
                       xc, 2.2f);
  }
  float header_bottom = cp.y + (subtitle && subtitle[0] ? lh * 2.0f + 4.0f : lh);
  if (close_pos.y + close_d > header_bottom)
    header_bottom = close_pos.y + close_d;
  igSetCursorScreenPos((ImVec2){cp.x, header_bottom + 6.0f * s});

  /* gradient accent line under the header */
  {
    ImVec2 l;
    igGetCursorScreenPos(&l);
    float lw = wsz.x - pad * 2.0f;
    ImDrawList_AddRectFilledMultiColor(
        dl, l, (ImVec2){l.x + lw, l.y + 2.0f},
        crystal_col(0.800f, 0.700f, 1.0f, 0.90f),
        crystal_col(0.592f, 0.353f, 1.0f, 0.00f),
        crystal_col(0.592f, 0.353f, 1.0f, 0.00f),
        crystal_col(0.800f, 0.700f, 1.0f, 0.90f));
    igDummy((ImVec2){1.0f, 8.0f * s});
  }

  /* Tap on the dimmed area outside the panel closes it (ignored for the
     first moments so the tap that opened it can't also close it). */
  if (!close_pressed && igGetTime() - s_open_time > 0.30 &&
      igIsMouseClicked_Bool(0, false) &&
      !igIsMouseHoveringRect(wp, (ImVec2){wp.x + wsz.x, wp.y + wsz.y}, false))
    close_pressed = true;
  if (close_pressed) igCloseCurrentPopup();

  /* ---- scrolling body. Always-on scrollbar keeps the content width the same
     whether or not it overflows, so layout can never oscillate. ----------- */
  ImVec2 av;
  igGetContentRegionAvail(&av);
  av.y -= footer_h;
  if (av.y < 40.0f) av.y = 40.0f;
  igBeginChild_Str("##panel_body", av, ImGuiChildFlags_None,
                   ImGuiWindowFlags_AlwaysVerticalScrollbar);
  s_panel_body_open = true;
  return true;
}

void crystal_panel_end_body(void) {
  if (s_panel_body_open) {
    igEndChild();
    s_panel_body_open = false;
  }
}

void crystal_panel_end(void) {
  crystal_panel_end_body();
  if (s_panel_font_pushed) {
    igPopFont();
    s_panel_font_pushed = false;
  }
  igEndPopup();
  igPopStyleColor(CRYSTAL_PANEL_COLORS);
  igPopStyleVar(CRYSTAL_PANEL_VARS);
}

bool crystal_close_button(const char* id, ImVec2 pos, float d) {
  ImDrawList* dl = igGetWindowDrawList();
  igSetCursorScreenPos(pos);
  bool pressed = igInvisibleButton(id, (ImVec2){d, d}, 0);
  bool hot = igIsItemHovered(0);
  bool held = igIsItemActive();
  ImVec2 c = {pos.x + d * 0.5f, pos.y + d * 0.5f};
  ImDrawList_AddCircleFilled(
      dl, c, d * 0.5f,
      held ? crystal_col(0.470f, 0.360f, 0.720f, 0.80f)
           : crystal_col(0.373f, 0.290f, 0.607f, hot ? 0.60f : 0.40f),
      24);
  ImDrawList_AddCircle(dl, c, d * 0.5f,
                       crystal_col(0.690f, 0.580f, 0.960f, 0.60f), 24, 1.2f);
  float k = d * 0.22f;
  ImU32 xc = crystal_col(0.945f, 0.925f, 1.0f, 1.0f);
  ImDrawList_AddLine(dl, (ImVec2){c.x - k, c.y - k}, (ImVec2){c.x + k, c.y + k},
                     xc, 2.2f);
  ImDrawList_AddLine(dl, (ImVec2){c.x - k, c.y + k}, (ImVec2){c.x + k, c.y - k},
                     xc, 2.2f);
  return pressed;
}
