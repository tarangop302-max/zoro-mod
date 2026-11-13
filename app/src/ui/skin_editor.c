#include "skin_editor.h"

#include <string.h>
#include <math.h>
#include <stdio.h>

#include "../user.h"
#include "crystal_theme.h"
#include "lobby_bg.h"
#include "../game/ntl_tags.h"
#include "../game/skin_io.h"


int skin_code_filter(ImGuiInputTextCallbackData* data) {
  return !((data->EventChar >= 'a' && data->EventChar <= 'z') ||
           (data->EventChar == '-') || (data->EventChar == ',') ||
           (data->EventChar >= '0' && data->EventChar <= '9'));
}


/* ---- popup frame -------------------------------------------------------
   The Skin Editor is now a centered popup with the same footprint as the lobby
   Settings / Controls popups (52% x 88% of the screen). Behind it only the
   lobby background is shown (no game / bot preview is started for this
   screen). The skin preview and the colour grid are drawn by a separate
   renderer UNDER ImGui, so those two rectangles ("holes") must stay clear of
   any ImGui fill: the popup fill is drawn clipped to the bands around them
   and the lobby background is drawn only OUTSIDE the popup. */

static ImU32 se_col(float r, float g, float b, float a) {
  return igColorConvertFloat4ToU32(crystal_tint((ImVec4){r, g, b, a}));
}

static int se_bands(float W, float H, ImVec2 a0, ImVec2 a1, ImVec2 b0,
                    ImVec2 b1, float out[8][4]) {
  int n = 0;
#define SE_ADD(x0, y0, x1, y1)              \
  do {                                      \
    if ((x1) > (x0) && (y1) > (y0)) {       \
      out[n][0] = (x0);                     \
      out[n][1] = (y0);                     \
      out[n][2] = (x1);                     \
      out[n][3] = (y1);                     \
      n++;                                  \
    }                                       \
  } while (0)
  bool has_b = b1.x > b0.x && b1.y > b0.y;
  SE_ADD(0, 0, W, a0.y);
  SE_ADD(0, a0.y, a0.x, a1.y);
  SE_ADD(a1.x, a0.y, W, a1.y);
  float y = a1.y;
  if (has_b) {
    SE_ADD(0, y, W, b0.y);
    SE_ADD(0, b0.y, b0.x, b1.y);
    SE_ADD(b1.x, b0.y, W, b1.y);
    y = b1.y;
  }
  SE_ADD(0, y, W, H);
#undef SE_ADD
  return n;
}

static void se_draw_popup_bg(tenv* env, float pop_x, float pop_y, float pop_w,
                             float pop_h, ImVec2 a0, ImVec2 a1, ImVec2 b0,
                             ImVec2 b1) {
  ImDrawList* dl = igGetWindowDrawList();
  float W = env->ctx->size[0];
  float H = env->ctx->size[1];
  ImVec2 pmin = {pop_x, pop_y};
  ImVec2 pmax = {pop_x + pop_w, pop_y + pop_h};

  /* 1. Lobby background + dim, only outside the popup (4 strips). */
  float strips[4][4] = {{0, 0, W, pop_y},
                        {0, pop_y + pop_h, W, H},
                        {0, pop_y, pop_x, pop_y + pop_h},
                        {pop_x + pop_w, pop_y, W, pop_y + pop_h}};
  for (int i = 0; i < 4; i++) {
    if (strips[i][2] <= strips[i][0] || strips[i][3] <= strips[i][1]) continue;
    ImDrawList_PushClipRect(dl, (ImVec2){strips[i][0], strips[i][1]},
                            (ImVec2){strips[i][2], strips[i][3]}, true);
    ui_lobby_bg_draw(env);
    ImDrawList_AddRectFilled(dl, (ImVec2){0, 0}, (ImVec2){W, H},
                             igColorConvertFloat4ToU32((ImVec4){0, 0, 0, 0.42f}),
                             0, 0);
    ImDrawList_PopClipRect(dl);
  }

  /* 2. Popup panel fill, with the preview strip / colour grid cut out. */
  float bands[8][4];
  int n = se_bands(W, H, a0, a1, b0, b1, bands);
  ImU32 fill = se_col(0.070f, 0.050f, 0.120f, 0.95f);
  for (int i = 0; i < n; i++) {
    ImDrawList_PushClipRect(dl, (ImVec2){bands[i][0], bands[i][1]},
                            (ImVec2){bands[i][2], bands[i][3]}, true);
    ImDrawList_AddRectFilled(dl, pmin, pmax, fill, 14.0f, 0);
    ImDrawList_PopClipRect(dl);
  }

  /* 3. Border (never crosses a hole -- they sit inside the popup). */
  ImDrawList_AddRect(dl, pmin, pmax, se_col(0.690f, 0.580f, 0.960f, 0.55f),
                     14.0f, 0, 1.5f);
}


/* ---- themed preview box + looping snake ---------------------------------
   The preview lives in a box that follows the selected background theme
   (all colours go through crystal_tint via se_col). The box interior is a
   flat rect drawn by the preview renderer (so the snake sits on top of it);
   the corner brackets / hairline are drawn by ImGui on top. The snake enters
   the box from the left wall, runs along the top lane, U-turns at the right
   side, comes back along the bottom lane, U-turns again and then keeps
   chasing its own tail around a rounded racetrack, always fully inside. */

static void se_draw_preview_frame(ImVec2 mn, ImVec2 mx, float rs) {
  ImDrawList* dl = igGetWindowDrawList();
  ImU32 line = se_col(0.690f, 0.580f, 0.960f, 0.28f);
  ImU32 acc = se_col(0.800f, 0.700f, 1.0f, 0.95f);
  float len = 18.0f * rs, th = 2.0f;
  ImDrawList_AddRect(dl, mn, mx, line, 0.0f, 0, 1.0f);
  /* four corner brackets */
  ImDrawList_AddLine(dl, (ImVec2){mn.x, mn.y}, (ImVec2){mn.x + len, mn.y}, acc, th);
  ImDrawList_AddLine(dl, (ImVec2){mn.x, mn.y}, (ImVec2){mn.x, mn.y + len}, acc, th);
  ImDrawList_AddLine(dl, (ImVec2){mx.x, mn.y}, (ImVec2){mx.x - len, mn.y}, acc, th);
  ImDrawList_AddLine(dl, (ImVec2){mx.x, mn.y}, (ImVec2){mx.x, mn.y + len}, acc, th);
  ImDrawList_AddLine(dl, (ImVec2){mn.x, mx.y}, (ImVec2){mn.x + len, mx.y}, acc, th);
  ImDrawList_AddLine(dl, (ImVec2){mn.x, mx.y}, (ImVec2){mn.x, mx.y - len}, acc, th);
  ImDrawList_AddLine(dl, (ImVec2){mx.x, mx.y}, (ImVec2){mx.x - len, mx.y}, acc, th);
  ImDrawList_AddLine(dl, (ImVec2){mx.x, mx.y}, (ImVec2){mx.x, mx.y - len}, acc, th);
}

typedef struct {
  float xl, xr, y1, y2, cy, r, s, p; /* racetrack geometry, p = perimeter */
} se_track;

/* Position + heading at distance u along the track. u < 0 is the straight
   lead-in on the top lane (the snake entering the box). */
static void se_track_at(const se_track* t, float u, float* x, float* y,
                        float* ang) {
  if (u < 0) { *x = t->xl + u; *y = t->y1; *ang = 0; return; }
  u = fmodf(u, t->p);
  float arc = (float)PI * t->r;
  if (u < t->s) { *x = t->xl + u; *y = t->y1; *ang = 0; return; }
  u -= t->s;
  if (u < arc) {
    float th = u / t->r;
    *x = t->xr + t->r * sinf(th); *y = t->cy - t->r * cosf(th); *ang = th;
    return;
  }
  u -= arc;
  if (u < t->s) { *x = t->xr - u; *y = t->y2; *ang = (float)PI; return; }
  u -= t->s;
  float th = u / t->r;
  *x = t->xl - t->r * sinf(th); *y = t->cy + t->r * cosf(th);
  *ang = th + (float)PI;
}

void ui_skin_editor_init(tenv* env) {}

void ui_skin_editor(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  ImGuiStyle* style = igGetStyle();
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;

  igPushFont(usr->imgui_data.regular_font[usrs->ui_font_size],
             usr->imgui_data.regular_font[usrs->ui_font_size]->LegacySize);

  usr->r->global.bg_opacity = 0;
  usr->r->global.bd_opacity = 0;
  usr->r->global.minimap_opacity = 0;

  /* This screen has a live skin preview + color/accessory grid drawn by a
     separate renderer (bp_renderer_render() runs in the base scene pass,
     before ImGui composites on top), so any background painted here as a
     single full-screen rect sits OVER that content, not behind it --
     even a low alpha visibly dulls it. Rather than trade theme strength
     for color accuracy, carve the preview strip and (if visible) the
     color grid panel out of the background entirely: full-strength
     purple everywhere else, zero tint on the actual snake/colors. The
     two exclusion rects are computed below, before any widgets are laid
     out, and the background is drawn once that's known -- see
     crystal_draw_background_alpha_excl2(). */
  float frame_height = igGetFrameHeight();

  /* Popup rect: same footprint as the lobby Settings / Controls popups. */
  const float pop_w = ctx->size[0] * 0.52f;
  const float pop_h = ctx->size[1] * 0.88f;
  const float pop_x = (ctx->size[0] - pop_w) * 0.5f;
  const float pop_y = (ctx->size[1] - pop_h) * 0.5f;
  const float pop_bottom = pop_y + pop_h;
  const float header_h = frame_height * 1.3f + style->ItemSpacing.y;

  float resolution_scale = fminf(ctx->size[0] / 1280.0f, ctx->size[1] / 720.0f);
  if (resolution_scale < 0.70f) resolution_scale = 0.70f;
  if (resolution_scale > 1.25f) resolution_scale = 1.25f;
  float scale = 48.0f * resolution_scale;

  /* The 256-segment preview is very wide. Shrink it only when necessary so
     the editor remains fully visible on lower-resolution devices. */
  float preview_units = 1.0f + (MAX_SKIN_CODE_LEN / 2.0f - 1.0f) * (8.0f / 48.0f);
  float max_scale_for_width = (pop_w * 0.94f) / preview_units;
  if (scale > max_scale_for_width) scale = max_scale_for_width;

  vec2 tot_size = {style->ItemSpacing.x * 6 + 7 * scale,
                   style->ItemSpacing.y * 5 + 6 * scale};

  /* Row-units reserved above grid_y for the buttons/labels below the
     preview. The full color/accessory grid (child panel starting at
     grid_y) only ever shows while gdata->skin_editing is true, so the two
     modes reserve a different number of rows:
       editing:     [skin code input] [Save] [Default] [OK]        -> 4
       not editing: [Default skins] [<>] [Saved skins] [<>]
                    [Custom] [OK]                                   -> 6 */
  int picker_rows = gdata->skin_editing ? 4 : 6;
  /* The NTL tag block (label + sliding picker + buttons) sits at the bottom,
     between the Custom button and OK; editing a custom skin needs the room
     for its colour grid instead, so it only shows when not editing. */
  float tag_h = gdata->skin_editing ? 0.0f : ntl_tags_block_height();
  float stack_h = (style->ItemSpacing.y + frame_height) * picker_rows + tag_h;
  float reserved_h = stack_h + (scale + style->ItemSpacing.y) * 2.0f;
  float min_grid_y = pop_y + header_h + style->WindowPadding.y + reserved_h;
  float grid_y;
  if (gdata->skin_editing) {
    /* The color/accessory grid (child panel) starts at grid_y and fills
       everything down to the bottom of the screen, so grid_y only needs to
       leave enough room above it for the preview + input rows -- it isn't
       meant to be centered as a fixed-height block. */
    grid_y = (pop_y + header_h + pop_bottom) * 0.5f - tot_size[1] * 0.5f;
    if (grid_y < min_grid_y) grid_y = min_grid_y;
  } else {
    /* No panel below grid_y here -- the preview + picker rows are the
       whole visible block, so center that block in the screen instead of
       anchoring it to the top via min_grid_y. */
    grid_y = (pop_y + header_h + pop_bottom) * 0.5f + reserved_h * 0.5f;
    if (grid_y < min_grid_y) grid_y = min_grid_y;
  }

  float sk_w = scale + (8 * (scale / 48)) * ((MAX_SKIN_CODE_LEN / 2.0f) - 1);

  /* Bounding box around both preview rows (the row further up on screen
     comes from the "+ 2 * (scale + spacing)" offset, the lower one from
     "+ scale + spacing" -- see the two bp_renderer loops below). Padded
     generously for the shadow sprites (which extend 10*(scale/48) past
     each segment) and the eyes/accessory (small offsets past the last
     segment). */
  float preview_row_a_y = grid_y - (stack_h + scale + style->ItemSpacing.y);
  float preview_row_b_y = grid_y - (stack_h + 2 * (scale + style->ItemSpacing.y));
  float preview_pad = scale * 0.5f;
  /* The preview box: same vertical band as before, but a fixed, compact
     width instead of the old 256-segment-wide strip. */
  float box_w = pop_w * 0.90f;
  ImVec2 preview_excl_min = {ctx->size[0] * 0.5f - box_w * 0.5f,
                             fminf(preview_row_a_y, preview_row_b_y) - preview_pad};
  ImVec2 preview_excl_max = {ctx->size[0] * 0.5f + box_w * 0.5f,
                             fmaxf(preview_row_a_y, preview_row_b_y) + scale + preview_pad};

  /* Color/accessory grid panel geometry -- identical to the child-window
     rect computed further down when the panel actually gets drawn. kept
     in sync deliberately (same formulas) rather than shared via a
     variable, since the panel's own setup runs much later after several
     other widgets. Only present while editing a custom skin. */
  ImVec2 panel_excl_min = {0, 0};
  ImVec2 panel_excl_max = {0, 0};
  if (usrs->custom_skin && gdata->skin_editing) {
    float panel_w = tot_size[0] + style->ScrollbarSize + style->WindowPadding.x * 2.0f;
    if (panel_w > pop_w - style->WindowPadding.x * 2.0f)
      panel_w = pop_w - style->WindowPadding.x * 2.0f;
    float panel_x = ctx->size[0] * 0.5f - panel_w * 0.5f;
    float panel_h = pop_bottom - grid_y - style->WindowPadding.y * 2.0f;
    if (panel_h < scale * 2.25f) panel_h = scale * 2.25f;
    panel_excl_min = (ImVec2){panel_x, grid_y};
    panel_excl_max = (ImVec2){panel_x + panel_w, grid_y + panel_h};
  }

  /* Restored to the original 0.72 (in fact this could go all the way to
     1.0 now) -- alpha no longer trades off against the preview/swatch
     colors since this doesn't paint over them at all. */
  se_draw_popup_bg(env, pop_x, pop_y, pop_w, pop_h, preview_excl_min,
                   preview_excl_max, panel_excl_min, panel_excl_max);
  se_draw_preview_frame(preview_excl_min, preview_excl_max, resolution_scale);
  crystal_push_theme();

  /* ---- header: title + round close (same as OK) ---- */
  {
    ImDrawList* hdl = igGetWindowDrawList();
    float hpad = style->WindowPadding.x + 6.0f;
    float d = frame_height * 1.15f;
    ImVec2 tp = {pop_x + hpad + 14.0f, pop_y + (header_h - igGetTextLineHeight()) * 0.5f};
    ImDrawList_AddCircleFilled(hdl, (ImVec2){pop_x + hpad + 3.0f, tp.y + igGetTextLineHeight() * 0.5f},
                               4.5f, se_col(0.800f, 0.700f, 1.0f, 1.0f), 16);
    ImDrawList_AddText_Vec2(hdl, tp, se_col(0.850f, 0.780f, 1.0f, 1.0f),
                            "SKIN EDITOR", NULL);
    /* Import / Export, immediately left of the close button. */
    {
      float bh = d * 0.85f;
      ImVec2 t1, t2;
      igCalcTextSize(&t1, "Import", NULL, false, -1.0f);
      igCalcTextSize(&t2, "Export", NULL, false, -1.0f);
      float padx = style->FramePadding.x;
      float w_imp = t1.x + padx * 2.0f, w_exp = t2.x + padx * 2.0f;
      float gap = 6.0f;
      float y = pop_y + (header_h - bh) * 0.5f;
      float x_exp = pop_x + pop_w - hpad - d - gap - w_exp;
      float x_imp = x_exp - gap - w_imp;
      igSetCursorScreenPos((ImVec2){x_imp, y});
      if (igButton("Import##skin_imp", (ImVec2){w_imp, bh})) skin_io_request_import(env);
      crystal_sheen();
      igSetCursorScreenPos((ImVec2){x_exp, y});
      if (igButton("Export##skin_exp", (ImVec2){w_exp, bh})) skin_io_request_export(env);
      crystal_sheen();

      /* Result message, centred just under the header line. */
      const char* st = skin_io_status();
      if (st[0]) {
        ImVec2 ts;
        igCalcTextSize(&ts, st, NULL, false, -1.0f);
        ImDrawList_AddText_Vec2(hdl,
                                (ImVec2){pop_x + (pop_w - ts.x) * 0.5f, pop_y + header_h + 6.0f},
                                se_col(0.760f, 0.700f, 0.920f, 0.95f), st, NULL);
      }
    }
    if (crystal_close_button("##skin_close",
                             (ImVec2){pop_x + pop_w - hpad - d,
                                      pop_y + (header_h - d) * 0.5f},
                             d)) {
      if (usrs->skin_code[0] == 0) usrs->custom_skin = false;
      gdata->skin_editing = false;
      gdata->curr_screen = TITLE_SCREEN;
      save_user_settings(usrs);
    }
    ImVec2 l0 = {pop_x + hpad, pop_y + header_h};
    ImDrawList_AddRectFilledMultiColor(
        hdl, l0, (ImVec2){pop_x + pop_w - hpad, pop_y + header_h + 2.0f},
        se_col(0.800f, 0.700f, 1.0f, 0.90f), se_col(0.592f, 0.353f, 1.0f, 0.0f),
        se_col(0.592f, 0.353f, 1.0f, 0.0f), se_col(0.800f, 0.700f, 1.0f, 0.90f));
  }


  for (int i = strlen(usrs->skin_code); i < MAX_SKIN_CODE_LEN + 1; i++)
    usrs->skin_code[i] = 0;

  /* ---- snake preview: racetrack loop inside the themed box ---- */
  float bx0 = preview_excl_min.x, by0 = preview_excl_min.y;
  float bw = preview_excl_max.x - bx0, bh = preview_excl_max.y - by0;

  /* which segments actually exist (custom skins can be short) */
  int cg[MAX_SKIN_CODE_LEN];
  int n_eff = 0;
  for (int k = 0; k < MAX_SKIN_CODE_LEN; k++) {
    int id;
    if (usrs->custom_skin) {
      id = get_cg_id(&usr->gdata, usrs->skin_code[MAX_SKIN_CODE_LEN - 1 - k]);
    } else {
      int sk_len = gdata->default_skins[usrs->default_skin][0];
      id = gdata->default_skins[usrs->default_skin]
                               [1 + ((MAX_SKIN_CODE_LEN - 1 - k) % sk_len)];
    }
    cg[k] = id;
    if (id != -1) n_eff++;
  }
  if (n_eff < 1) n_eff = 1;

  /* Segment size: small enough that the whole body fits on the loop with a
     clear gap between head and tail (spacing between segments = d / 6). */
  float d = scale * 0.55f;
  se_track tr;
  for (int it = 0; it < 6; it++) {
    float m = d * 0.71f + 4.0f;
    float lane = bh - 2.0f * m;
    tr.r = lane * 0.5f;
    tr.y1 = by0 + m;
    tr.y2 = by0 + bh - m;
    tr.cy = (tr.y1 + tr.y2) * 0.5f;
    tr.xl = bx0 + m + tr.r;
    tr.xr = bx0 + bw - m - tr.r;
    tr.s = tr.xr - tr.xl;
    tr.p = 2.0f * tr.s + 2.0f * (float)PI * tr.r;
    float gap = fmaxf(4.0f * d, 0.12f * tr.p);
    float dmax = 6.0f * (tr.p - gap) / (float)n_eff;
    if (d > dmax) d = dmax;
    if (d < scale * 0.18f) d = scale * 0.18f;
  }
  float sp = d / 6.0f;

  /* time: restart (snake re-enters) whenever the screen is opened again */
  static double s_last_t = 0.0;
  static float s_head = 0.0f;
  double now = igGetTime();
  float dt = (float)(now - s_last_t);
  if (dt > 0.25f || dt < 0.0f) { s_head = 0.0f; dt = 0.0f; }
  if (dt > 0.05f) dt = 0.05f;
  s_last_t = now;
  s_head += dt * 220.0f * resolution_scale;
  if (s_head > 1e7f) s_head = fmodf(s_head, tr.p);

  /* box interior (theme-tinted, flat) under the snake */
  {
    ImVec4 f = crystal_tint((ImVec4){0.045f, 0.028f, 0.085f, 1.0f});
    bp_renderer_push(usr->r->bpr,
                     &(bp_instance){{bx0, by0, bw, 0}, gdata->cg_uvs[BLANK_UV],
                                    {f.x, f.y, f.z, 1.0f}, {bh, 2.0f}});
  }

  float lead = tr.r; /* hidden until it has crossed the left wall */
  float ks = d / 48.0f;
  for (int k = 0; k < MAX_SKIN_CODE_LEN; k++) {
    if (cg[k] == -1) continue;
    float u = s_head - (MAX_SKIN_CODE_LEN - 1 - k) * sp;
    if (u < -lead) continue;
    float x, y, a;
    se_track_at(&tr, u, &x, &y, &a);
    bp_renderer_push(usr->r->bpr,
                     &(bp_instance){{x - d * 0.5f - 10 * ks, y - d * 0.5f - 10 * ks, 68 * ks},
                                    gdata->SHADOW_UV, {0, 0, 0, 0.2f}});
  }

  float head_x = 0, head_y = 0, head_a = 0;
  for (int k = 0; k < MAX_SKIN_CODE_LEN; k++) {
    float u = s_head - (MAX_SKIN_CODE_LEN - 1 - k) * sp;
    if (u < -lead) continue;
    float x, y, a;
    se_track_at(&tr, u, &x, &y, &a);
    if (k == MAX_SKIN_CODE_LEN - 1) { head_x = x; head_y = y; head_a = a; }
    if (cg[k] == -1) continue;
    float we = gdata->worm_effect[(MAX_SKIN_CODE_LEN - 1 - k) % WORM_EFFECT_LEN] *
                   (!usrs->custom_skin) + usrs->custom_skin;
    bp_renderer_push(usr->r->bpr,
                     &(bp_instance){{x - d * 0.5f, y - d * 0.5f, d, a + (float)PI},
                                    gdata->cg_uvs[cg[k]], {we, we, we, 1}});
  }

  /* eyes / accessory / NTL tag follow the head (only once it is inside) */
  if (s_head >= -lead) {
    float ssc = d / 29;
    float ed = 6 * ssc;
    float esp = 6 * ssc;
    float er = 6;
    default_skin_data* dfs =
        gdata->dfs + ((1 - usrs->custom_skin) * (1 + usrs->default_skin));
    float pr = dfs->pr;
    float iris_r = er * ssc;
    float pupil_r = pr * ssc;
    float ca = cosf(head_a), sa = sinf(head_a);
    float ox, oy, px, py;
    ox = ed; oy = -esp - .5f;
    px = head_x + ox * ca - oy * sa; py = head_y + ox * sa + oy * ca;
    bp_renderer_push(usr->r->bpr, &(bp_instance){{px - iris_r, py - iris_r, iris_r * 2},
                     gdata->cg_uvs[BLANK_UV], {dfs->ec.r, dfs->ec.g, dfs->ec.b, 1}});
    ox = ed; oy = esp + .5f;
    px = head_x + ox * ca - oy * sa; py = head_y + ox * sa + oy * ca;
    bp_renderer_push(usr->r->bpr, &(bp_instance){{px - iris_r, py - iris_r, iris_r * 2},
                     gdata->cg_uvs[BLANK_UV], {dfs->ec.r, dfs->ec.g, dfs->ec.b, 1}});
    ox = ed + .5f + 2 * ssc; oy = -esp;
    px = head_x + ox * ca - oy * sa; py = head_y + ox * sa + oy * ca;
    bp_renderer_push(usr->r->bpr, &(bp_instance){{px - pupil_r, py - pupil_r, pupil_r * 2},
                     gdata->cg_uvs[BLANK_UV], {dfs->ppc.r, dfs->ppc.g, dfs->ppc.b, 1}});
    ox = ed + .5f + 2 * ssc; oy = esp;
    px = head_x + ox * ca - oy * sa; py = head_y + ox * sa + oy * ca;
    bp_renderer_push(usr->r->bpr, &(bp_instance){{px - pupil_r, py - pupil_r, pupil_r * 2},
                     gdata->cg_uvs[BLANK_UV], {dfs->ppc.r, dfs->ppc.g, dfs->ppc.b, 1}});
    if (usrs->accessory < NUM_ACCESSORIES) {
      accessory_data* acc = gdata->accessories + usrs->accessory;
      float aox = acc->of * ed;
      float m = d * 0.5f * acc->sc;
      float acx = head_x + aox * ca;
      float acy = head_y + aox * sa;
      bp_renderer_push(usr->r->bpr,
                       &(bp_instance){{acx - m, acy - m, m * 2, head_a}, acc->uv, {1, 1, 1, 1}});
    }
    ntl_tags_draw_preview(head_x, head_y, d);
  }

  igSetCursorPosX(ctx->size[0] * 0.5 - sk_w * 0.5f);
  igSetCursorPosY((grid_y) - (stack_h + 2 * (scale + style->ItemSpacing.y)));

  float row_h = style->ItemSpacing.y + frame_height;
  float picker_x = ctx->size[0] * 0.5 - tot_size[0] * 0.5f;
  float half_w = tot_size[0] / 2 - style->ItemSpacing.x / 2;

  if (gdata->skin_editing) {
    /* Rows 4-3: skin code input, then Save (image 2). */
    igSetCursorPosX(picker_x);
    igSetCursorPosY((grid_y) - row_h * 4);
    igSetNextItemWidth(tot_size[0] - 2 * (frame_height + style->ItemSpacing.x));
    igInputTextWithHint("##ss", "Skin code", usrs->skin_code,
                        MAX_SKIN_CODE_LEN + 1,
                        ImGuiInputTextFlags_CallbackCharFilter |
                            ImGuiInputTextFlags_EnterReturnsTrue,
                        skin_code_filter, NULL);
    crystal_sheen();
    igSameLine(0, -1);
    igPushStyleVarX(ImGuiStyleVar_FramePadding, 0);
    int skin_code_len = strlen(usrs->skin_code);
    if (igButton("C", (ImVec2){frame_height, frame_height}) &&
        skin_code_len > 0) {
      usrs->skin_code[strlen(usrs->skin_code) - 1] = 0;
    }
    crystal_sheen();
    igSameLine(0, -1);
    if (igButton("\ue9ac", (ImVec2){frame_height, frame_height})) {
      usrs->skin_code[0] = 0;
    }
    crystal_sheen();
    igPopStyleVar(1);

    igSetCursorPosX(picker_x);
    igSetCursorPosY((grid_y) - row_h * 3);
    bool can_save = usrs->skin_code[0] != 0 &&
                    skin_saved_total(usrs) < MAX_SAVED_SKINS + SKIN_LIB_MAX;
    igBeginDisabled(!can_save);
    if (igButton("Save", (ImVec2){tot_size[0]})) {
      if (skin_saved_add(usrs, usrs->skin_code, usrs->accessory))
        usrs->active_saved_skin = skin_saved_total(usrs) - 1;
    }
    crystal_sheen();
    igEndDisabled();
  } else {
    /* Rows (bottom to top): OK, NTL tags, Custom, Saved skins, Default skins.
       Everything above OK is shifted up by the tag block's height. */
    /* "Default skins" label + arrows. */
    igSetCursorPosX(picker_x);
    igSetCursorPosY((grid_y) - tag_h - row_h * 6);
    igTextDisabled("Default skins");

    igSetCursorPosX(picker_x);
    igSetCursorPosY((grid_y) - tag_h - row_h * 5);
    if (igButton("\uea38##default_prev", (ImVec2){half_w, 0})) {
      usrs->default_skin =
          (usrs->default_skin + (NUM_DEFAULT_SKINS - 1)) % NUM_DEFAULT_SKINS;
      usrs->custom_skin = false;
    }
    crystal_sheen();
    igSameLine(0, -1);
    if (igButton("\uea34##default_next", (ImVec2){half_w, 0})) {
      usrs->default_skin = (usrs->default_skin + 1) % NUM_DEFAULT_SKINS;
      usrs->custom_skin = false;
    }
    crystal_sheen();

    /* "Saved skins" label + arrows. Cycles the whole saved list (the stored
       slots plus every imported skin) and loads that entry's code/accessory
       as the active skin. */
    int total = skin_saved_total(usrs);
    if (usrs->active_saved_skin >= total) usrs->active_saved_skin = -1;
    igSetCursorPosX(picker_x);
    igSetCursorPosY((grid_y) - tag_h - row_h * 4);
    if (total > 0 && usrs->active_saved_skin >= 0)
      igTextDisabled("Saved skins  %d/%d", usrs->active_saved_skin + 1, total);
    else
      igTextDisabled("Saved skins  (%d)", total);

    igSetCursorPosX(picker_x);
    igSetCursorPosY((grid_y) - tag_h - row_h * 3);
    int cur = usrs->active_saved_skin;
    int pick = -1;
    igBeginDisabled(total == 0);
    if (igButton("\uea38##saved_prev", (ImVec2){half_w, 0}) && total > 0)
      pick = cur <= 0 ? total - 1 : cur - 1;
    crystal_sheen();
    igSameLine(0, -1);
    if (igButton("\uea34##saved_next", (ImVec2){half_w, 0}) && total > 0)
      pick = (cur + 1) % total;
    crystal_sheen();
    igEndDisabled();
    if (pick >= 0) {
      char code[MAX_SKIN_CODE_LEN + 1];
      uint8_t acc = 0;
      if (skin_saved_get(usrs, pick, code, &acc)) {
        strncpy(usrs->skin_code, code, MAX_SKIN_CODE_LEN + 1);
        usrs->skin_code[MAX_SKIN_CODE_LEN] = 0;
        usrs->accessory = acc;
        usrs->active_saved_skin = pick;
        usrs->custom_skin = true;
      }
    }

    /* Custom button, directly below Saved skins. */
    igSetCursorPosX(picker_x);
    igSetCursorPosY((grid_y) - tag_h - row_h * 2);
    if (igButton("\ue905 Custom", (ImVec2){tot_size[0]})) {
      gdata->skin_editing = true;
      usrs->custom_skin = true;
    }
    crystal_sheen();

    /* NTL tags at the bottom: label + show toggle, sliding picker,
       Upload/Add ID/Sync/Remove. */
    ntl_tags_skin_editor_ui(env, picker_x, grid_y - row_h * 1 - tag_h, tot_size[0]);
  }

  if (gdata->skin_editing) {
    igSetCursorPosX(picker_x);
    igSetCursorPosY((grid_y) - row_h * 2);
    if (igButton("\uea40 Default", (ImVec2){tot_size[0]})) {
      gdata->skin_editing = false;
      usrs->custom_skin = false;
    }
    crystal_sheen();
  }

  igSetCursorPosX(picker_x);
  igSetCursorPosY((grid_y) - row_h * 1);
  {
    ImVec2 ok_pos;
    igGetCursorScreenPos(&ok_pos);
    crystal_glow_rect(ok_pos, (ImVec2){tot_size[0], frame_height}, 0.647f,
                      0.420f, 1.0f);
  }
  igPushStyleColor_Vec4(ImGuiCol_Button,
                        (ImVec4){0.510f, 0.294f, 0.910f, 1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                        (ImVec4){0.569f, 0.353f, 0.960f, 1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
                        (ImVec4){0.450f, 0.243f, 0.850f, 1.0f});
  if (igButton("OK", (ImVec2){tot_size[0]})) {
    if (usrs->skin_code[0] == 0) usrs->custom_skin = false;
    gdata->skin_editing = false;
    gdata->curr_screen = TITLE_SCREEN;
    save_user_settings(usrs);
  }
  crystal_sheen();
  igPopStyleColor(3);

  if (usrs->custom_skin && gdata->skin_editing) {
    float panel_w = tot_size[0] + style->ScrollbarSize + style->WindowPadding.x * 2.0f;
    if (panel_w > pop_w - style->WindowPadding.x * 2.0f)
      panel_w = pop_w - style->WindowPadding.x * 2.0f;
    float panel_x = ctx->size[0] * 0.5f - panel_w * 0.5f;
    float panel_h = pop_bottom - grid_y - style->WindowPadding.y * 2.0f;
    if (panel_h < scale * 2.25f) panel_h = scale * 2.25f;

    igSetCursorPosX(panel_x);
    igSetCursorPosY(grid_y);
    igPushStyleColor_Vec4(ImGuiCol_ChildBg,
                          (ImVec4){0.176f, 0.106f, 0.306f, 0.30f});
    igPushStyleColor_Vec4(ImGuiCol_ScrollbarBg,
                          (ImVec4){0.176f, 0.106f, 0.306f, 0.20f});
    igPushStyleColor_Vec4(ImGuiCol_ScrollbarGrab,
                          (ImVec4){0.470f, 0.360f, 0.720f, 0.55f});
    igPushStyleColor_Vec4(ImGuiCol_ScrollbarGrabHovered,
                          (ImVec4){0.569f, 0.353f, 0.960f, 0.70f});
    igPushStyleColor_Vec4(ImGuiCol_ScrollbarGrabActive,
                          (ImVec4){0.510f, 0.294f, 0.910f, 0.85f});
    bool child_visible = igBeginChild_Str(
        "custom_skin_scroll_panel", (ImVec2){panel_w, panel_h},
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoMove);

    if (child_visible) {
      float clip_x1 = panel_x;
      float clip_y1 = grid_y;
      float clip_x2 = panel_x + panel_w - style->ScrollbarSize;
      float clip_y2 = grid_y + panel_h;
      int skin_code_len = strlen(usrs->skin_code);

      igTextDisabled("Snake colours");
      float colors_y = igGetCursorPosY() + style->ItemSpacing.y;

      int cg_id = 0;
      for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 7; j++) {
          bool is_disabled =
              (cg_id == 36 || cg_id == 38 || cg_id == 40 || cg_id == 41 ||
               !(skin_code_len < MAX_SKIN_CODE_LEN));

          igSetCursorPosY(colors_y + (style->ItemSpacing.y + scale) * i);
          igSetCursorPosX((style->ItemSpacing.x + scale) * j);
          ImVec2 item_pos;
          igGetCursorScreenPos(&item_pos);
          bool ct = gdata->cg_colors_ct[cg_id];

          float a = 0.8f * (1.0f - 0.5f * is_disabled);
          igBeginDisabled(is_disabled);
          char label[2] = {gdata->ntl_cg_map[cg_id], 0};
          igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, scale / 2);
          igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){ct, ct, ct, 1});
          igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0, 0, 0, 0});
          igPushStyleColor_Vec4(ImGuiCol_Border, (ImVec4){0, 0, 0, 0});
          igPushStyleColor_Vec4(ImGuiCol_BorderShadow, (ImVec4){0, 0, 0, 0});
          igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0, 0, 0, 0});
          igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0, 0, 0, 0});
          igPushID_Int(cg_id);
          bool pressed = igButton(label, (ImVec2){scale, scale});
          igPopID();
          igPopStyleVar(1);
          igPopStyleColor(6);
          igEndDisabled();
          if (pressed && strlen(usrs->skin_code) < MAX_SKIN_CODE_LEN)
            usrs->skin_code[strlen(usrs->skin_code)] = gdata->ntl_cg_map[cg_id];

          bool active = igIsItemActive();
          bool hovered = igIsItemHovered(ImGuiHoveredFlags_None);
          if (hovered) a = 1.0f;
          if (active) a = 0.6f;

          bool sprite_visible = item_pos.x + scale >= clip_x1 && item_pos.x <= clip_x2 &&
                                item_pos.y + scale >= clip_y1 && item_pos.y <= clip_y2;
          if (sprite_visible) {
            bp_renderer_push(usr->r->bpr,
                             &(bp_instance){{item_pos.x - scale * 0.25f,
                                             item_pos.y - scale * 0.25f,
                                             scale * 1.5f},
                                            gdata->SHADOW_UV,
                                            {0, 0, 0, 0.2f}});
            bp_renderer_push(usr->r->bpr,
                             &(bp_instance){{item_pos.x, item_pos.y, scale},
                                            gdata->cg_uvs[cg_id],
                                            {1, 1, 1, a}});
          }
          cg_id++;
        }
      }

      float accessories_label_y = colors_y +
          (style->ItemSpacing.y + scale) * 6.0f + style->ItemSpacing.y * 2.0f;
      igSetCursorPosX(0);
      igSetCursorPosY(accessories_label_y);
      igTextDisabled("Accessories");
      float accessories_y = igGetCursorPosY() + style->ItemSpacing.y;

      int aid = 0;
      for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 7; j++) {
          if (aid > NUM_ACCESSORIES) break;
          bool is_no_accessory = aid == NUM_ACCESSORIES;
          igSetCursorPosY(accessories_y + (style->ItemSpacing.y + scale) * i);
          igSetCursorPosX((style->ItemSpacing.x + scale) * j);
          ImVec2 item_pos;
          igGetCursorScreenPos(&item_pos);
          float a = 0.8f;

          char label[12] = {0};
          sprintf(label, "a%d", aid);
          igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){1, 0.5f, 0.5f, 1});
          if (!is_no_accessory) {
            igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, scale / 2);
            igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0, 0, 0, 0});
            igPushStyleColor_Vec4(ImGuiCol_Border, (ImVec4){0, 0, 0, 0});
            igPushStyleColor_Vec4(ImGuiCol_BorderShadow, (ImVec4){0, 0, 0, 0});
            igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0, 0, 0, 0});
            igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0, 0, 0, 0});
          }
          igPushID_Str(label);
          bool pressed = igButton(is_no_accessory ? "\uea0f" : "",
                                  (ImVec2){scale, scale});
          igPopID();
          if (!is_no_accessory) igPopStyleVar(1);
          igPopStyleColor(is_no_accessory ? 1 : 6);
          if (pressed) usrs->accessory = is_no_accessory ? NO_ACCESSORY : aid;

          bool active = igIsItemActive();
          bool hovered = igIsItemHovered(ImGuiHoveredFlags_None);
          if (hovered) a = 1.0f;
          if (active) a = 0.0f;

          bool sprite_visible = item_pos.x + scale >= clip_x1 && item_pos.x <= clip_x2 &&
                                item_pos.y + scale >= clip_y1 && item_pos.y <= clip_y2;
          if (!is_no_accessory && sprite_visible)
            bp_renderer_push(usr->r->bpr,
                             &(bp_instance){{item_pos.x, item_pos.y, scale},
                                            gdata->accessories[aid].uv,
                                            {1, 1, 1, a}});
          aid++;
        }
      }

      igSetCursorPosX(0);
      igSetCursorPosY(accessories_y + (style->ItemSpacing.y + scale) * 5.0f);
      igDummy((ImVec2){tot_size[0], style->ItemSpacing.y});
    }
    igEndChild();
    igPopStyleColor(5);
  }

  crystal_pop_theme();

  igPopFont();
}

void ui_skin_editor_destroy(tenv* env) {}
