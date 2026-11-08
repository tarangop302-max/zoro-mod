#include "lb_text_fx.h"

#include <math.h>
#include <string.h>

#define LB_TWO_PI 6.28318530718f

bool lb_fx_active(const lb_text_fx* fx) {
  return fx->mode != LB_FX_SOLID || fx->outline != LB_OUTLINE_NONE || fx->shadow;
}

ImVec4 lb_fx_color2(const lb_text_fx* fx, float alpha_scale) {
  return (ImVec4){fx->color2[0], fx->color2[1], fx->color2[2],
                  fx->color2[3] * alpha_scale};
}

static ImVec4 lerp4(ImVec4 a, ImVec4 b, float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return (ImVec4){a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}

/* 0..1 phase of a repeating animation, `rate` cycles per second at speed 1. */
static float phase(double now, float speed, float rate) {
  double p = now * (double)speed * (double)rate;
  return (float)(p - floor(p));
}

static ImVec4 effect_color(const lb_text_fx* fx, ImVec4 c1, ImVec4 c2, float t,
                           double now) {
  switch (fx->mode) {
    case LB_FX_GRADIENT:
      return lerp4(c1, c2, t);
    case LB_FX_RAINBOW:
    case LB_FX_RAINBOW_ANIM: {
      float h = t * 0.85f; /* stop short of a full turn so ends differ */
      if (fx->mode == LB_FX_RAINBOW_ANIM) h += phase(now, fx->speed, 0.2f);
      h -= floorf(h);
      ImVec4 c = {0.0f, 0.0f, 0.0f, c1.w};
      igColorConvertHSVtoRGB(h, 0.75f, 1.0f, &c.x, &c.y, &c.z);
      return c;
    }
    case LB_FX_WAVE: {
      float k = 0.5f + 0.5f * sinf(LB_TWO_PI * (t - phase(now, fx->speed, 0.4f)));
      return lerp4(c1, c2, k);
    }
    case LB_FX_PULSE: {
      float k = 0.5f + 0.5f * sinf(LB_TWO_PI * phase(now, fx->speed, 0.5f));
      return lerp4(c1, c2, k);
    }
    default:
      return c1;
  }
}

static size_t utf8_len(unsigned char c) {
  if (c < 0x80) return 1;
  if ((c >> 5) == 0x6) return 2;
  if ((c >> 4) == 0xE) return 3;
  if ((c >> 3) == 0x1E) return 4;
  return 1; /* stray continuation byte: step over it */
}

void lb_fx_text(const lb_text_fx* fx, ImVec4 c1, ImVec4 c2, float t0, float t1,
                const char* text) {
  ImDrawList* dl = igGetWindowDrawList();
  ImVec2 pos;
  igGetCursorScreenPos(&pos);
  ImVec2 total;
  igCalcTextSize(&total, text, NULL, false, -1);

  ImFont* font = igGetFont();
  const float fsz = igGetFontSize();
  const double now = igGetTime();
  const size_t len = strlen(text);

  size_t n = 0; /* characters, so the effect spreads evenly over them */
  for (size_t b = 0; b < len; b += utf8_len((unsigned char)text[b])) ++n;

  /* Outline / shadow distances scale with the text so they look the same on a
     small and a large leaderboard. */
  float ow = 0.0f;
  if (fx->outline == LB_OUTLINE_THIN) ow = fmaxf(1.0f, fsz * 0.06f);
  if (fx->outline == LB_OUTLINE_THICK) ow = fmaxf(1.5f, fsz * 0.11f);
  const float sw = fmaxf(1.0f, fsz * 0.07f);

  size_t i = 0;
  for (size_t b = 0; b < len; ++i) {
    size_t cl = utf8_len((unsigned char)text[b]);
    if (b + cl > len) cl = len - b;

    ImVec2 px = {0.0f, 0.0f};
    if (b > 0) igCalcTextSize(&px, text, text + b, false, -1);
    const ImVec2 at = {pos.x + px.x, pos.y};

    const float frac = n > 1 ? (float)i / (float)(n - 1) : 0.0f;
    const ImVec4 col = effect_color(fx, c1, c2, t0 + (t1 - t0) * frac, now);

    if (fx->shadow)
      ImDrawList_AddText_FontPtr(
          dl, font, fsz, (ImVec2){at.x + sw, at.y + sw},
          igColorConvertFloat4ToU32((ImVec4){0.0f, 0.0f, 0.0f, 0.6f * col.w}),
          text + b, text + b + cl, 0.0f, NULL);

    if (ow > 0.0f) {
      const ImU32 oc = igColorConvertFloat4ToU32(
          (ImVec4){fx->outline_color[0], fx->outline_color[1],
                   fx->outline_color[2], fx->outline_color[3] * col.w});
      static const float dir[8][2] = {{-1, 0}, {1, 0},  {0, -1},  {0, 1},
                                      {-0.7f, -0.7f}, {0.7f, -0.7f},
                                      {-0.7f, 0.7f},  {0.7f, 0.7f}};
      const int count = fx->outline == LB_OUTLINE_THIN ? 4 : 8;
      for (int d = 0; d < count; ++d)
        ImDrawList_AddText_FontPtr(
            dl, font, fsz, (ImVec2){at.x + dir[d][0] * ow, at.y + dir[d][1] * ow},
            oc, text + b, text + b + cl, 0.0f, NULL);
    }

    ImDrawList_AddText_FontPtr(dl, font, fsz, at, igColorConvertFloat4ToU32(col),
                               text + b, text + b + cl, 0.0f, NULL);
    b += cl;
  }

  igDummy(total);
}
