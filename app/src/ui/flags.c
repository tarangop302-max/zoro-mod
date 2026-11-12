#include "flags.h"

#include <math.h>
#include <string.h>

static ImU32 C(int r, int g, int b) {
  return (ImU32)(0xFFu << 24 | (unsigned)b << 16 | (unsigned)g << 8 | (unsigned)r);
}

/* Rectangle given in 0..1 flag coordinates. */
static void R(ImDrawList* dl, ImVec2 a, ImVec2 s, float x0, float y0, float x1,
              float y1, ImU32 col) {
  ImDrawList_AddRectFilled(
      dl, (ImVec2){a.x + x0 * s.x, a.y + y0 * s.y},
      (ImVec2){a.x + x1 * s.x, a.y + y1 * s.y}, col, 0.0f, 0);
}

static void CIRC(ImDrawList* dl, ImVec2 a, ImVec2 s, float cx, float cy,
                 float rad, ImU32 col) {
  ImDrawList_AddCircleFilled(dl, (ImVec2){a.x + cx * s.x, a.y + cy * s.y},
                             rad * s.y, col, 20);
}

/* White crescent opening to the right, with an optional dot (star). */
static void crescent(ImDrawList* dl, ImVec2 a, ImVec2 s, float cx, float cy,
                     float rad, ImU32 fg, ImU32 bg, bool star) {
  CIRC(dl, a, s, cx, cy, rad, fg);
  CIRC(dl, a, s, cx + rad * 0.40f * (s.y / s.x), cy, rad * 0.80f, bg);
  if (star) CIRC(dl, a, s, cx + rad * 1.55f * (s.y / s.x), cy, rad * 0.32f, fg);
}

static void union_jack(ImDrawList* dl, ImVec2 a, ImVec2 s) {
  ImU32 blue = C(1, 33, 105), white = C(255, 255, 255), red = C(200, 16, 46);
  ImVec2 b = {a.x + s.x, a.y + s.y};
  ImDrawList_PushClipRect(dl, a, b, true);
  ImDrawList_AddRectFilled(dl, a, b, blue, 0.0f, 0);
  float t = s.y * 0.22f;
  ImDrawList_AddLine(dl, a, b, white, t);
  ImDrawList_AddLine(dl, (ImVec2){a.x, b.y}, (ImVec2){b.x, a.y}, white, t);
  float t2 = s.y * 0.09f;
  ImDrawList_AddLine(dl, a, b, red, t2);
  ImDrawList_AddLine(dl, (ImVec2){a.x, b.y}, (ImVec2){b.x, a.y}, red, t2);
  R(dl, a, s, 0.0f, 0.5f - 0.17f, 1.0f, 0.5f + 0.17f, white);
  R(dl, a, s, 0.5f - 0.10f, 0.0f, 0.5f + 0.10f, 1.0f, white);
  R(dl, a, s, 0.0f, 0.5f - 0.10f, 1.0f, 0.5f + 0.10f, red);
  R(dl, a, s, 0.5f - 0.06f, 0.0f, 0.5f + 0.06f, 1.0f, red);
  ImDrawList_PopClipRect(dl);
}

bool ui_flag_draw(ImDrawList* dl, ImVec2 pmin, ImVec2 pmax, const char* cc) {
  ImVec2 a = pmin;
  ImVec2 s = {pmax.x - pmin.x, pmax.y - pmin.y};
  char c0 = 0, c1 = 0;
  if (cc && cc[0] && cc[1]) { c0 = cc[0]; c1 = cc[1]; }
  #define IS(x, y) (c0 == (x) && c1 == (y))

  ImU32 white = C(255, 255, 255), red = C(206, 17, 38);

  if (IS('J', 'P')) {
    R(dl, a, s, 0, 0, 1, 1, white);
    CIRC(dl, a, s, 0.5f, 0.5f, 0.30f, C(188, 0, 45));
  } else if (IS('U', 'S')) {
    for (int i = 0; i < 7; i++)
      R(dl, a, s, 0, i / 7.0f, 1, (i + 1) / 7.0f, i % 2 == 0 ? C(178, 34, 52) : white);
    R(dl, a, s, 0, 0, 0.45f, 4 / 7.0f, C(60, 59, 110));
  } else if (IS('G', 'B')) {
    union_jack(dl, a, s);
  } else if (IS('A', 'U')) {
    R(dl, a, s, 0, 0, 1, 1, C(0, 0, 139));
    union_jack(dl, a, (ImVec2){s.x * 0.5f, s.y * 0.5f});
    CIRC(dl, a, s, 0.25f, 0.78f, 0.10f, white);
    CIRC(dl, a, s, 0.72f, 0.30f, 0.06f, white);
    CIRC(dl, a, s, 0.82f, 0.62f, 0.06f, white);
    CIRC(dl, a, s, 0.64f, 0.75f, 0.06f, white);
  } else if (IS('C', 'A')) {
    R(dl, a, s, 0, 0, 1, 1, white);
    R(dl, a, s, 0, 0, 0.25f, 1, C(216, 6, 33));
    R(dl, a, s, 0.75f, 0, 1, 1, C(216, 6, 33));
    CIRC(dl, a, s, 0.5f, 0.52f, 0.20f, C(216, 6, 33));
  } else if (IS('B', 'R')) {
    R(dl, a, s, 0, 0, 1, 1, C(0, 156, 59));
    ImDrawList_AddQuadFilled(dl, (ImVec2){a.x + 0.5f * s.x, a.y + 0.12f * s.y},
                             (ImVec2){a.x + 0.90f * s.x, a.y + 0.5f * s.y},
                             (ImVec2){a.x + 0.5f * s.x, a.y + 0.88f * s.y},
                             (ImVec2){a.x + 0.10f * s.x, a.y + 0.5f * s.y},
                             C(255, 223, 0));
    CIRC(dl, a, s, 0.5f, 0.5f, 0.22f, C(0, 39, 118));
  } else if (IS('F', 'R')) {
    R(dl, a, s, 0, 0, 1.0f / 3, 1, C(0, 35, 149));
    R(dl, a, s, 1.0f / 3, 0, 2.0f / 3, 1, white);
    R(dl, a, s, 2.0f / 3, 0, 1, 1, C(237, 41, 57));
  } else if (IS('D', 'E')) {
    R(dl, a, s, 0, 0, 1, 1.0f / 3, C(0, 0, 0));
    R(dl, a, s, 0, 1.0f / 3, 1, 2.0f / 3, C(221, 0, 0));
    R(dl, a, s, 0, 2.0f / 3, 1, 1, C(255, 206, 0));
  } else if (IS('E', 'S')) {
    R(dl, a, s, 0, 0, 1, 1, C(170, 21, 27));
    R(dl, a, s, 0, 0.25f, 1, 0.75f, C(241, 191, 0));
  } else if (IS('F', 'I')) {
    R(dl, a, s, 0, 0, 1, 1, white);
    R(dl, a, s, 0, 0.40f, 1, 0.66f, C(0, 53, 128));
    R(dl, a, s, 0.26f, 0, 0.50f, 1, C(0, 53, 128));
  } else if (IS('P', 'L')) {
    R(dl, a, s, 0, 0, 1, 0.5f, white);
    R(dl, a, s, 0, 0.5f, 1, 1, C(220, 20, 60));
  } else if (IS('T', 'R')) {
    R(dl, a, s, 0, 0, 1, 1, C(227, 10, 23));
    crescent(dl, a, s, 0.38f, 0.5f, 0.27f, white, C(227, 10, 23), true);
  } else if (IS('I', 'N')) {
    R(dl, a, s, 0, 0, 1, 1.0f / 3, C(255, 153, 51));
    R(dl, a, s, 0, 1.0f / 3, 1, 2.0f / 3, white);
    R(dl, a, s, 0, 2.0f / 3, 1, 1, C(19, 136, 8));
    CIRC(dl, a, s, 0.5f, 0.5f, 0.14f, C(0, 0, 128));
    CIRC(dl, a, s, 0.5f, 0.5f, 0.09f, white);
  } else if (IS('S', 'G')) {
    R(dl, a, s, 0, 0, 1, 0.5f, C(237, 41, 57));
    R(dl, a, s, 0, 0.5f, 1, 1, white);
    crescent(dl, a, s, 0.22f, 0.26f, 0.19f, white, C(237, 41, 57), false);
    CIRC(dl, a, s, 0.40f, 0.26f, 0.045f, white);
  } else if (IS('B', 'G')) {
    R(dl, a, s, 0, 0, 1, 1.0f / 3, white);
    R(dl, a, s, 0, 1.0f / 3, 1, 2.0f / 3, C(0, 150, 110));
    R(dl, a, s, 0, 2.0f / 3, 1, 1, C(214, 38, 18));
  } else if (IS('N', 'G')) {
    R(dl, a, s, 0, 0, 1.0f / 3, 1, C(0, 135, 81));
    R(dl, a, s, 1.0f / 3, 0, 2.0f / 3, 1, white);
    R(dl, a, s, 2.0f / 3, 0, 1, 1, C(0, 135, 81));
  } else if (IS('A', 'E')) {
    R(dl, a, s, 0, 0, 1, 1.0f / 3, C(0, 115, 47));
    R(dl, a, s, 0, 1.0f / 3, 1, 2.0f / 3, white);
    R(dl, a, s, 0, 2.0f / 3, 1, 1, C(0, 0, 0));
    R(dl, a, s, 0, 0, 0.28f, 1, red);
  } else if (IS('A', 'Z')) {
    R(dl, a, s, 0, 0, 1, 1.0f / 3, C(0, 181, 226));
    R(dl, a, s, 0, 1.0f / 3, 1, 2.0f / 3, C(239, 51, 64));
    R(dl, a, s, 0, 2.0f / 3, 1, 1, C(80, 158, 47));
    crescent(dl, a, s, 0.44f, 0.5f, 0.14f, white, C(239, 51, 64), true);
  } else {
    /* unknown: neutral grey placeholder with a lighter centre line */
    R(dl, a, s, 0, 0, 1, 1, C(90, 86, 110));
    R(dl, a, s, 0, 0.47f, 1, 0.53f, C(140, 135, 165));
    ImDrawList_AddRect(dl, pmin, pmax, C(0, 0, 0) & 0x55FFFFFF, 2.0f, 0, 1.0f);
    return false;
  }
  #undef IS
  /* thin outline so light flags (white/red) stay visible on the dark rows */
  ImDrawList_AddRect(dl, pmin, pmax, 0x66000000u, 2.0f, 0, 1.0f);
  return true;
}
