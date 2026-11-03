#include "lobby_scenes.h"

#include <math.h>

#define PI_F 3.14159265f

static ImU32 rgba(const float c[3], float a) {
  if (a < 0.0f) a = 0.0f;
  if (a > 1.0f) a = 1.0f;
  return igColorConvertFloat4ToU32((ImVec4){c[0], c[1], c[2], a});
}

static ImU32 rgba3(float r, float g, float b, float a) {
  float c[3] = {r, g, b};
  return rgba(c, a);
}

static float fr(float x) { return x - floorf(x); }

/* Deterministic pseudo-random 0..1 from (n, k). */
static float hf(int n, int k) {
  unsigned x = (unsigned)n * 374761393u + (unsigned)k * 668265263u;
  x = (x ^ (x >> 13)) * 1274126177u;
  x ^= x >> 16;
  return (float)(x & 0xFFFFFFu) / 16777216.0f;
}

static float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

/* ---- Embers: sparks drifting upward ------------------------------------ */
static void scene_embers(ImDrawList* dl, ImVec2 p0, float w, float h, float t,
                         bool full, const float a[3], const float b[3]) {
  float u = h / 1000.0f;
  int n = full ? 80 : 16;
  for (int i = 0; i < n; ++i) {
    float speed = 0.025f + hf(i, 1) * 0.07f;
    float y = 1.0f - fr(hf(i, 2) + t * speed);
    float x = hf(i, 3) + sinf(t * (0.4f + hf(i, 4)) + i) * 0.025f;
    float fade = sinf(clampf(y, 0.0f, 1.0f) * PI_F);
    float flick = 0.7f + 0.3f * sinf(t * 6.0f + i * 1.7f);
    float r = (2.0f + hf(i, 5) * 4.0f) * u;
    if (r < 0.8f) r = 0.8f;
    ImVec2 c = {p0.x + x * w, p0.y + y * h};
    const float* col = hf(i, 6) > 0.5f ? a : b;
    ImDrawList_AddCircleFilled(dl, c, r * 3.0f, rgba(col, 0.06f * fade * flick),
                               12);
    ImDrawList_AddCircleFilled(dl, c, r, rgba(col, 0.85f * fade * flick), 8);
  }
}

/* ---- Sunburst: rotating rays + gold dust ------------------------------- */
static void scene_sunburst(ImDrawList* dl, ImVec2 p0, float w, float h,
                           float t, bool full, const float a[3],
                           const float b[3]) {
  ImVec2 c = {p0.x + w * 0.5f, p0.y + h * 0.42f};
  float len = sqrtf(w * w + h * h);
  int rays = full ? 28 : 16;
  float rot = t * 0.04f;
  for (int i = 0; i < rays; ++i) {
    float a0 = rot + (float)i / rays * 2.0f * PI_F;
    float a1 = a0 + (PI_F / rays) * 0.9f;
    float pulse = 0.5f + 0.5f * sinf(t * 0.6f + i * 0.9f);
    ImVec2 q0 = {c.x + cosf(a0) * len, c.y + sinf(a0) * len};
    ImVec2 q1 = {c.x + cosf(a1) * len, c.y + sinf(a1) * len};
    ImDrawList_AddTriangleFilled(dl, c, q0, q1,
                                 rgba(a, 0.035f + 0.05f * pulse));
  }
  /* soft halo so the rays fade out of the centre */
  for (int k = 4; k >= 1; --k) {
    ImDrawList_AddCircleFilled(dl, c, h * 0.12f * (float)k,
                               rgba(b, 0.035f), 40);
  }
  float u = h / 1000.0f;
  int n = full ? 40 : 8;
  for (int i = 0; i < n; ++i) {
    float x = hf(i, 1);
    float y = fr(hf(i, 2) - t * (0.01f + hf(i, 3) * 0.02f));
    float tw = 0.5f + 0.5f * sinf(t * (1.0f + hf(i, 4) * 2.0f) + i);
    float r = (1.5f + hf(i, 5) * 2.5f) * u;
    if (r < 0.7f) r = 0.7f;
    ImDrawList_AddCircleFilled(dl, (ImVec2){p0.x + x * w, p0.y + y * h}, r,
                               rgba(a, 0.15f + 0.6f * tw), 8);
  }
}

/* ---- Grid: synthwave sun + perspective floor --------------------------- */
static void scene_grid(ImDrawList* dl, ImVec2 p0, float w, float h, float t,
                       bool full, const float a[3], const float b[3]) {
  float hor = p0.y + h * 0.58f;
  float cx = p0.x + w * 0.5f;
  float u = h / 1000.0f;
  float lw = full ? 2.0f * u + 0.5f : 1.0f;

  /* sun: stacked discs fake a vertical gradient, bars cut into its lower half */
  float sr = h * 0.22f;
  ImVec2 sc = {cx, hor - sr * 0.15f};
  ImDrawList_PushClipRect(dl, p0, (ImVec2){p0.x + w, hor}, true);
  ImDrawList_AddCircleFilled(dl, sc, sr * 1.5f, rgba(a, 0.06f), 48);
  ImDrawList_AddCircleFilled(dl, sc, sr * 1.2f, rgba(a, 0.08f), 48);
  ImDrawList_AddCircleFilled(dl, sc, sr, rgba(a, 0.85f), 48);
  ImDrawList_AddCircleFilled(dl, (ImVec2){sc.x, sc.y - sr * 0.25f}, sr * 0.75f,
                             rgba(b, 0.30f), 48);
  int bars = full ? 6 : 3;
  for (int i = 0; i < bars; ++i) {
    float f = (float)(i + 1) / (bars + 1);
    float y = sc.y + sr * (0.05f + 0.9f * f);
    float th = sr * 0.05f * (0.4f + f * 1.2f);
    ImDrawList_AddRectFilled(dl, (ImVec2){sc.x - sr, y},
                             (ImVec2){sc.x + sr, y + th},
                             rgba3(0.04f, 0.02f, 0.10f, 1.0f), 0, 0);
  }
  ImDrawList_PopClipRect(dl);

  /* floor */
  ImDrawList_AddRectFilled(dl, (ImVec2){p0.x, hor}, (ImVec2){p0.x + w, p0.y + h},
                           rgba3(0.03f, 0.01f, 0.08f, 0.75f), 0, 0);
  ImDrawList_PushClipRect(dl, (ImVec2){p0.x, hor}, (ImVec2){p0.x + w, p0.y + h},
                          true);
  int vlines = full ? 14 : 8;
  for (int k = -vlines; k <= vlines; ++k) {
    float xb = cx + (float)k * w * (full ? 0.11f : 0.14f);
    ImDrawList_AddLine(dl, (ImVec2){cx, hor}, (ImVec2){xb, p0.y + h},
                       rgba(b, 0.55f), lw);
  }
  int hlines = full ? 12 : 6;
  for (int i = 0; i < hlines; ++i) {
    float f = fr((float)i / hlines + t * 0.12f);
    float y = hor + (p0.y + h - hor) * f * f;
    ImDrawList_AddLine(dl, (ImVec2){p0.x, y}, (ImVec2){p0.x + w, y},
                       rgba(b, 0.15f + 0.6f * f), lw);
  }
  ImDrawList_PopClipRect(dl);
  ImDrawList_AddLine(dl, (ImVec2){p0.x, hor}, (ImVec2){p0.x + w, hor},
                     rgba(a, 0.9f), lw * 1.5f);
}

/* ---- Stars: twinkle + shooting star ------------------------------------ */
static void scene_stars(ImDrawList* dl, ImVec2 p0, float w, float h, float t,
                        bool full, const float a[3], const float b[3]) {
  float u = h / 1000.0f;
  int n = full ? 140 : 26;
  for (int i = 0; i < n; ++i) {
    float x = fr(hf(i, 1) + t * 0.004f * (0.3f + hf(i, 2)));
    float y = hf(i, 3);
    float tw = 0.35f + 0.65f * (0.5f + 0.5f * sinf(t * (0.8f + hf(i, 4) * 2.5f) +
                                                 hf(i, 5) * 6.28f));
    float r = (0.8f + hf(i, 6) * 2.0f) * u * 1.4f;
    if (r < 0.6f) r = 0.6f;
    ImVec2 c = {p0.x + x * w, p0.y + y * h};
    ImDrawList_AddCircleFilled(dl, c, r, rgba3(0.92f, 0.95f, 1.0f, tw), 8);
    if (hf(i, 7) > 0.9f) { /* a few get a cross sparkle */
      float s = r * 4.0f * tw;
      ImDrawList_AddLine(dl, (ImVec2){c.x - s, c.y}, (ImVec2){c.x + s, c.y},
                         rgba(a, 0.5f * tw), 1.0f);
      ImDrawList_AddLine(dl, (ImVec2){c.x, c.y - s}, (ImVec2){c.x, c.y + s},
                         rgba(a, 0.5f * tw), 1.0f);
    }
  }
  /* shooting star every ~7 seconds */
  float cyc = t / 7.0f;
  float ph = fr(cyc);
  if (ph < 0.12f) {
    int id = (int)floorf(cyc);
    float k = ph / 0.12f;
    float sx = p0.x + w * (0.35f + 0.6f * hf(id, 11));
    float sy = p0.y + h * (0.05f + 0.3f * hf(id, 12));
    float dx = -w * 0.28f, dy = h * 0.20f;
    ImVec2 head = {sx + dx * k, sy + dy * k};
    ImVec2 tail = {sx + dx * (k - 0.35f), sy + dy * (k - 0.35f)};
    float fade = 1.0f - k;
    ImDrawList_AddLine(dl, tail, head, rgba(b, 0.5f * fade), 2.5f * u + 1.0f);
    ImDrawList_AddLine(dl, tail, head, rgba3(1, 1, 1, 0.9f * fade), 1.0f);
  }
}

/* ---- Petals: falling, swaying ------------------------------------------ */
static void scene_petals(ImDrawList* dl, ImVec2 p0, float w, float h, float t,
                         bool full, const float a[3], const float b[3]) {
  float u = h / 1000.0f;
  int n = full ? 36 : 9;
  for (int i = 0; i < n; ++i) {
    float speed = 0.03f + hf(i, 1) * 0.04f;
    float y = fr(hf(i, 2) + t * speed);
    float x = hf(i, 3) + sinf(t * (0.5f + hf(i, 4)) + i * 2.0f) * 0.04f +
              y * 0.06f;
    x = fr(x);
    float s = (7.0f + hf(i, 5) * 9.0f) * u;
    if (s < 1.5f) s = 1.5f;
    float ang = t * (0.5f + hf(i, 6)) + i;
    float squash = 0.35f + 0.65f * fabsf(cosf(t * (0.7f + hf(i, 7)) + i));
    float ca = cosf(ang), sa = sinf(ang);
    ImVec2 c = {p0.x + x * w, p0.y + y * h};
    /* diamond, squashed on one axis and rotated = tumbling petal */
    float px[4] = {s, 0.0f, -s, 0.0f};
    float py[4] = {0.0f, s * 0.55f * squash, 0.0f, -s * 0.55f * squash};
    ImVec2 q[4];
    for (int k = 0; k < 4; ++k)
      q[k] = (ImVec2){c.x + px[k] * ca - py[k] * sa,
                      c.y + px[k] * sa + py[k] * ca};
    const float* col = hf(i, 8) > 0.5f ? a : b;
    float edge = sinf(clampf(y, 0.0f, 1.0f) * PI_F);
    ImDrawList_AddQuadFilled(dl, q[0], q[1], q[2], q[3],
                             rgba(col, 0.55f * edge + 0.1f));
  }
}

/* ---- Code: falling columns --------------------------------------------- */
static void scene_code(ImDrawList* dl, ImVec2 p0, float w, float h, float t,
                       bool full, const float a[3], const float b[3]) {
  float cell = full ? 22.0f * (h / 1000.0f + 0.3f) : 7.0f;
  int cols = (int)(w / cell);
  int rows = (int)(h / cell) + 1;
  int trail = full ? 14 : 6;
  ImDrawList_PushClipRect(dl, p0, (ImVec2){p0.x + w, p0.y + h}, true);
  for (int c = 0; c < cols; ++c) {
    if (hf(c, 1) < 0.35f) continue; /* leave gaps between columns */
    float speed = 3.0f + hf(c, 2) * 7.0f;
    float head = fmodf(t * speed + hf(c, 3) * (rows + trail), (float)(rows + trail));
    for (int k = 0; k < trail; ++k) {
      int row = (int)head - k;
      if (row < 0 || row >= rows) continue;
      float f = 1.0f - (float)k / trail;
      int flip = (int)(t * 6.0f) + row * 7 + c;
      if (k > 0 && hf(flip, c) < 0.25f) continue; /* flickering glyph gaps */
      float gw = cell * (0.30f + 0.25f * hf(row, c + 5));
      float gh = cell * (0.45f + 0.25f * hf(row + 3, c));
      ImVec2 q = {p0.x + c * cell + (cell - gw) * 0.5f,
                  p0.y + row * cell + (cell - gh) * 0.5f};
      ImU32 colr = k == 0 ? rgba3(0.85f, 1.0f, 0.9f, 0.95f)
                          : rgba(a, 0.65f * f * f);
      ImDrawList_AddRectFilled(dl, q, (ImVec2){q.x + gw, q.y + gh}, colr, 0, 0);
    }
  }
  ImDrawList_PopClipRect(dl);
  (void)b;
}

/* ---- Waves: layered sine bands ----------------------------------------- */
static void scene_waves(ImDrawList* dl, ImVec2 p0, float w, float h, float t,
                        bool full, const float a[3], const float b[3]) {
  int layers = 4;
  int steps = full ? 110 : 28;
  float dx = w / steps;
  /* a soft moon glow */
  ImVec2 moon = {p0.x + w * 0.75f, p0.y + h * 0.22f};
  ImDrawList_AddCircleFilled(dl, moon, h * 0.16f, rgba(a, 0.07f), 40);
  ImDrawList_AddCircleFilled(dl, moon, h * 0.09f, rgba(a, 0.20f), 40);
  ImDrawList_AddCircleFilled(dl, moon, h * 0.06f, rgba3(0.95f, 1, 1, 0.75f), 40);
  for (int l = 0; l < layers; ++l) {
    float base = 0.52f + l * 0.12f;
    float amp = (0.025f + l * 0.008f) * h;
    float freq = 2.0f + l * 0.7f;
    float spd = (0.25f + l * 0.12f) * (l % 2 ? -1.0f : 1.0f);
    float alpha = 0.18f + l * 0.12f;
    const float* col = (l % 2) ? b : a;
    float shade = 1.0f - l * 0.18f;
    ImU32 fill = rgba3(col[0] * shade, col[1] * shade, col[2] * shade, alpha);
    float prev_y = 0.0f;
    for (int i = 0; i <= steps; ++i) {
      float x = (float)i / steps;
      float y = p0.y + base * h + sinf(x * freq * 2.0f * PI_F + t * spd * 2.0f) * amp +
                sinf(x * freq * 3.1f + t * 0.7f + l) * amp * 0.4f;
      if (i > 0) {
        float px0 = p0.x + (i - 1) * dx;
        float px1 = p0.x + i * dx;
        ImDrawList_AddQuadFilled(dl, (ImVec2){px0, prev_y}, (ImVec2){px1, y},
                                 (ImVec2){px1, p0.y + h},
                                 (ImVec2){px0, p0.y + h}, fill);
        ImDrawList_AddLine(dl, (ImVec2){px0, prev_y}, (ImVec2){px1, y},
                           rgba3(1, 1, 1, 0.10f + 0.04f * l), 1.2f);
      }
      prev_y = y;
    }
  }
}

/* ---- Blade: three sword slashes ---------------------------------------- */
static void scene_blade(ImDrawList* dl, ImVec2 p0, float w, float h, float t,
                        bool full, const float a[3], const float b[3]) {
  float u = h / 1000.0f;
  /* faint diagonal hatching */
  int hatch = full ? 26 : 8;
  for (int i = -hatch; i < hatch * 2; ++i) {
    float x = p0.x + (float)i / hatch * w;
    ImDrawList_AddLine(dl, (ImVec2){x, p0.y}, (ImVec2){x - h * 0.45f, p0.y + h},
                       rgba(a, 0.025f), 1.0f);
  }
  /* Every 5 s three parallel slashes cut across in quick succession. */
  float cyc = t / 5.0f;
  float ph = fr(cyc);
  for (int s = 0; s < 3; ++s) {
    float start = 0.05f + s * 0.07f;
    float k = (ph - start) / 0.10f; /* 0..1 = the cut itself */
    if (k < 0.0f) continue;
    float life = (ph - start) / 0.55f; /* fade-out afterwards */
    if (life > 1.0f) continue;
    float cutk = clampf(k, 0.0f, 1.0f);
    float e = 1.0f - (1.0f - cutk) * (1.0f - cutk);
    float off = (s - 1) * h * 0.14f;
    ImVec2 from = {p0.x + w * 0.08f, p0.y + h * 0.18f + off};
    ImVec2 to = {p0.x + w * 0.92f, p0.y + h * 0.86f + off};
    ImVec2 tip = {from.x + (to.x - from.x) * e, from.y + (to.y - from.y) * e};
    float fade = 1.0f - life;
    ImDrawList_AddLine(dl, from, tip, rgba(a, 0.18f * fade), 14.0f * u + 2.0f);
    ImDrawList_AddLine(dl, from, tip, rgba(b, 0.55f * fade), 5.0f * u + 1.0f);
    ImDrawList_AddLine(dl, from, tip, rgba3(0.92f, 1.0f, 0.95f, 0.95f * fade),
                       1.8f * u + 0.8f);
  }
  /* drifting green mist */
  for (int i = 0; i < (full ? 4 : 2); ++i) {
    float x = fr(0.15f + i * 0.28f + t * 0.01f);
    float y = 0.75f + 0.1f * sinf(t * 0.3f + i);
    ImDrawList_AddCircleFilled(dl, (ImVec2){p0.x + x * w, p0.y + y * h},
                               h * 0.18f, rgba(b, 0.04f), 32);
  }
}

void lobby_scene_draw(ImDrawList* dl, int style, ImVec2 p0, ImVec2 p1,
                      float t, bool full, const float a[3], const float b[3]) {
  float w = p1.x - p0.x;
  float h = p1.y - p0.y;
  if (w < 1.0f || h < 1.0f) return;
  ImDrawList_PushClipRect(dl, p0, p1, true);
  switch (style) {
    case LOBBY_STYLE_EMBERS:   scene_embers(dl, p0, w, h, t, full, a, b); break;
    case LOBBY_STYLE_SUNBURST: scene_sunburst(dl, p0, w, h, t, full, a, b); break;
    case LOBBY_STYLE_GRID:     scene_grid(dl, p0, w, h, t, full, a, b); break;
    case LOBBY_STYLE_STARS:    scene_stars(dl, p0, w, h, t, full, a, b); break;
    case LOBBY_STYLE_PETALS:   scene_petals(dl, p0, w, h, t, full, a, b); break;
    case LOBBY_STYLE_CODE:     scene_code(dl, p0, w, h, t, full, a, b); break;
    case LOBBY_STYLE_WAVES:    scene_waves(dl, p0, w, h, t, full, a, b); break;
    case LOBBY_STYLE_BLADE:    scene_blade(dl, p0, w, h, t, full, a, b); break;
    default: break;
  }
  ImDrawList_PopClipRect(dl);
}
