#include "bg_picker.h"

#include <math.h>
#include <stdio.h>

#include "../game/backgrounds.h"
#include "../user.h"
#include "crystal_theme.h"
#include "settings.h"

#define BG_PICKER_POPUP "Backgrounds"
#define BG_PICKER_SCROLL_ID 26 /* unique ui_touch_scroll() owner id */

/* ---- tap detection that survives touch scrolling --------------------------
   A finger that starts on a tile and then drags to scroll must not select it:
   a tap only counts if the pointer barely moved and the list did not scroll. */
static int s_press_id = -1;
static ImVec2 s_press_pos;
static float s_press_scroll = 0.0f;
static bool s_press_moved = false;

static bool tile_tap(int id, ImVec2 p0, ImVec2 p1, bool* hot) {
  ImGuiIO* io = igGetIO_Nil();
  bool inside = igIsWindowHovered(ImGuiHoveredFlags_None) &&
                igIsMouseHoveringRect(p0, p1, true);
  if (hot) *hot = inside;
  bool tap = false;
  if (inside && igIsMouseClicked_Bool(0, false)) {
    s_press_id = id;
    s_press_pos = io->MousePos;
    s_press_scroll = igGetScrollY();
    s_press_moved = false;
  }
  if (s_press_id == id) {
    if (igIsMouseDown_Nil(0)) {
      float dx = io->MousePos.x - s_press_pos.x;
      float dy = io->MousePos.y - s_press_pos.y;
      if (dx * dx + dy * dy > 9.0f * 9.0f ||
          fabsf(igGetScrollY() - s_press_scroll) > 2.0f)
        s_press_moved = true;
    } else if (igIsMouseReleased_Nil(0)) {
      tap = !s_press_moved;
      s_press_id = -1;
    } else {
      s_press_id = -1; /* lost the release (popup closed, etc.) */
    }
  }
  return tap;
}

static ImU32 col(float r, float g, float b, float a) {
  return igColorConvertFloat4ToU32((ImVec4){r, g, b, a});
}

static void draw_check(ImDrawList* dl, ImVec2 p1, ImVec2 p0) {
  ImVec2 bc = {p1.x - 14.0f, p0.y + 14.0f};
  ImDrawList_AddCircleFilled(dl, bc, 10.0f, col(0.80f, 0.70f, 1.0f, 1.0f), 18);
  ImDrawList_AddLine(dl, (ImVec2){bc.x - 4.5f, bc.y + 0.5f},
                     (ImVec2){bc.x - 1.0f, bc.y + 4.0f},
                     col(0.10f, 0.05f, 0.20f, 1.0f), 2.2f);
  ImDrawList_AddLine(dl, (ImVec2){bc.x - 1.0f, bc.y + 4.0f},
                     (ImVec2){bc.x + 5.0f, bc.y - 3.5f},
                     col(0.10f, 0.05f, 0.20f, 1.0f), 2.2f);
}

static void draw_frame(ImDrawList* dl, ImVec2 p0, ImVec2 p1, bool sel,
                       bool hot) {
  ImDrawList_AddRect(dl, p0, p1,
                     sel ? col(0.87f, 0.77f, 1.0f, 1.0f)
                         : col(0.65f, 0.55f, 0.95f, hot ? 0.90f : 0.40f),
                     8.0f, 0, sel ? 3.0f : 1.2f);
}

static void draw_label(ImDrawList* dl, ImVec2 p0, ImVec2 p1, const char* label,
                       bool sel) {
  float lh = igGetTextLineHeight();
  ImDrawList_AddRectFilled(dl, (ImVec2){p0.x, p1.y - lh - 8.0f}, p1,
                           col(0.0f, 0.0f, 0.0f, 0.55f), 8.0f,
                           ImDrawFlags_RoundCornersBottom);
  ImDrawList_AddText_Vec2(
      dl, (ImVec2){p0.x + 9.0f, p1.y - lh - 4.0f},
      sel ? col(1.0f, 0.97f, 1.0f, 1.0f) : col(0.86f, 0.82f, 0.95f, 1.0f),
      label, NULL);
}

/* One thumbnail: the cell for `index` from the preview sheet, cropped to the
   tile's shape (the sheet's cells are square). Falls back to a flat tile if the
   sheet did not load. */
static void draw_thumb(tenv* env, ImDrawList* dl, int index, ImVec2 p0,
                       ImVec2 p1, float aspect_h_over_w) {
  renderer* r = env->usr->r;
  if (!r || !r->bg_thumbs_ds) {
    ImDrawList_AddRectFilled(dl, p0, p1, col(0.16f, 0.14f, 0.24f, 1.0f), 8.0f,
                             0);
    return;
  }
  const float cols = (float)BACKGROUND_THUMB_COLS;
  const float rows = (float)((NUM_BACKGROUNDS + BACKGROUND_THUMB_COLS - 1) /
                             BACKGROUND_THUMB_COLS);
  const float texel_u = 1.0f / (cols * BACKGROUND_THUMB_CELL);
  const float texel_v = 1.0f / (rows * BACKGROUND_THUMB_CELL);
  float cu = 1.0f / cols, cv = 1.0f / rows;
  float u0 = (index % BACKGROUND_THUMB_COLS) * cu + texel_u;
  float v0 = (index / BACKGROUND_THUMB_COLS) * cv + texel_v;
  float u1 = u0 + cu - 2.0f * texel_u;
  float v1 = v0 + cv - 2.0f * texel_v;
  /* Tile is wider than tall: keep the centre band of the square cell. */
  float keep = aspect_h_over_w > 1.0f ? 1.0f : aspect_h_over_w;
  float trim = (v1 - v0) * (1.0f - keep) * 0.5f;
  v0 += trim;
  v1 -= trim;
  ImTextureRef tex = {NULL, (ImTextureID)r->bg_thumbs_ds};
  ImDrawList_AddImageRounded(dl, tex, p0, p1, (ImVec2){u0, v0}, (ImVec2){u1, v1},
                             col(1, 1, 1, 1), 8.0f, 0);
}

void ui_bg_picker_button(tenv* env) {
  user_settings* usrs = &env->usr->usrs;
  const int current = background_clamp(usrs->game_bg_style);

  char btn[96];
  snprintf(btn, sizeof btn, "%s  >##bg_picker_open", BACKGROUNDS[current].label);
  if (igButton(btn, (ImVec2){-1, 0})) igOpenPopup_Str(BG_PICKER_POPUP, 0);

  crystal_panel_set_layout(0.62f, 0.90f, 0.0f);
  if (!crystal_panel_begin(env, BG_PICKER_POPUP, "BACKGROUNDS",
                           "Tap one to use it", 900.0f))
    return;
  ui_touch_scroll(env, BG_PICKER_SCROLL_ID);

  igTextDisabled("Now: %s", BACKGROUNDS[current].label);
  igSpacing();

  ImVec2 avail;
  igGetContentRegionAvail(&avail);
  const float m = 8.0f;    /* room for the selection glow */
  const float gap = 12.0f; /* between tiles */
  float target = igGetTextLineHeight() * 9.0f;
  int cols = (int)floorf((avail.x - m * 2.0f + gap) / (target + gap));
  if (cols < 2) cols = 2;
  if (cols > 5) cols = 5;
  float tw = floorf((avail.x - m * 2.0f - gap * (cols - 1)) / cols);
  if (tw < 70.0f) tw = 70.0f;
  const float aspect = 0.72f;
  float th = floorf(tw * aspect);
  float rows_h = th + gap;

  ImVec2 org;
  igGetCursorScreenPos(&org);
  ImDrawList* dl = igGetWindowDrawList();
  int picked = -1;

  for (int i = 0; i < NUM_BACKGROUNDS; ++i) {
    int c = i % cols, rrow = i / cols;
    ImVec2 p0 = {org.x + m + c * (tw + gap), org.y + m + rrow * rows_h};
    ImVec2 p1 = {p0.x + tw, p0.y + th};
    bool sel = (i == current);
    bool hot = false;
    bool tap = tile_tap(5000 + i, p0, p1, &hot);
    if (sel) crystal_glow_rect(p0, (ImVec2){tw, th}, 0.690f, 0.580f, 0.960f);
    draw_thumb(env, dl, i, p0, p1, aspect);
    draw_label(dl, p0, p1, BACKGROUNDS[i].label, sel);
    draw_frame(dl, p0, p1, sel, hot);
    if (sel) draw_check(dl, p1, p0);
    if (tap) picked = i;
  }

  int rows = (NUM_BACKGROUNDS + cols - 1) / cols;
  igDummy((ImVec2){1.0f, m * 2.0f + rows * rows_h});

  if (picked >= 0) {
    usrs->game_bg_style = picked;
    save_user_settings(usrs);
    igCloseCurrentPopup();
  }

  crystal_panel_end();
}
