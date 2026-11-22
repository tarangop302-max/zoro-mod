#include "skin_editor.h"

#include <string.h>
#include <math.h>
#include <stdio.h>

#include "../user.h"
#include "crystal_theme.h"
#include "lobby_bg.h"
#include "../game/ntl_tags.h"
#include "../game/skin_io.h"
#include "../game/skin_gen.h"


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

static int s_se_tab = 0; /* Custom view: 0 = skin colours, 1 = accessories */

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

static void se_make_track(se_track* t, float bx0, float by0, float bw,
                          float bh, float d) {
  float m = d * 1.06f + 4.0f; /* edge margin incl. wiggle amplitude (0.35 d) */
  t->r = (bh - 2.0f * m) * 0.5f;
  t->y1 = by0 + m;
  t->y2 = by0 + bh - m;
  t->cy = (t->y1 + t->y2) * 0.5f;
  t->xl = bx0 + m + t->r;
  t->xr = bx0 + bw - m - t->r;
  t->s = t->xr - t->xl;
  t->p = 2.0f * t->s + 2.0f * (float)PI * t->r;
}

/* Track position plus a sideways wiggle that is a fixed function of the
   distance along the path. The head swings up and down as it advances and
   every body segment passes through exactly the same points the head did, so
   the body follows the head's path precisely. The head tilts up/down with
   the wiggle. */
static void se_snake_at(const se_track* t, float u, float d, float* x,
                        float* y, float* ang) {
  se_track_at(t, u, x, y, ang);
  float amp = 0.35f * d;
  float k = 6.2831853f / (6.0f * d);
  float off = amp * sinf(k * u);
  *x += -sinf(*ang) * off;
  *y += cosf(*ang) * off;
  *ang += atanf(amp * k * cosf(k * u));
}


/* ---- Random skin generator (RANDOM tab) ---------------------------------
   The engine lives in game/skin_gen.c. This part only holds the UI state:
   the recipe (options), the last batch of candidates, the list of skins that
   were already shown (never offered twice) and a back/forward history of the
   skins that were applied to the preview. */

#define SE_HIST_MAX 64
#define SE_SEEN_MAX 512

typedef struct {
  char code[MAX_SKIN_CODE_LEN + 1];
  uint8_t acc;
} se_hist_t;

static skin_gen_recipe s_rcp;
static bool s_rcp_ready = false;
static skin_gen_skin s_cand[SKIN_GEN_MAX_BATCH];
static int s_ncand = 0, s_sel = -1;
static int s_batch = 6;
static bool s_remix = false;
static uint64_t s_seen[SE_SEEN_MAX];
static int s_nseen = 0;
static se_hist_t s_hist[SE_HIST_MAX];
static int s_hn = 0, s_hpos = -1;
static char s_gmsg[96] = "";
static uint32_t s_ui_rng = 0;

static uint32_t se_rnd(void) {
  if (!s_ui_rng) s_ui_rng = skin_gen_new_seed() | 1u;
  s_ui_rng ^= s_ui_rng << 13;
  s_ui_rng ^= s_ui_rng >> 17;
  s_ui_rng ^= s_ui_rng << 5;
  return s_ui_rng;
}

static void se_rcp_init(void) {
  if (s_rcp_ready) return;
  skin_gen_default_recipe(&s_rcp);
  s_rcp_ready = true;
}

static void se_hist_push(const char* code, uint8_t acc) {
  if (s_hpos < s_hn - 1) s_hn = s_hpos + 1; /* drop the "forward" part */
  if (s_hn >= SE_HIST_MAX) {
    memmove(&s_hist[0], &s_hist[1], sizeof(se_hist_t) * (SE_HIST_MAX - 1));
    s_hn = SE_HIST_MAX - 1;
  }
  strncpy(s_hist[s_hn].code, code, MAX_SKIN_CODE_LEN);
  s_hist[s_hn].code[MAX_SKIN_CODE_LEN] = 0;
  s_hist[s_hn].acc = acc;
  s_hn++;
  s_hpos = s_hn - 1;
}

/* Put a code (and accessory, unless "keep") on the live preview. */
static void se_apply(user_settings* usrs, const char* code, uint8_t acc) {
  strncpy(usrs->skin_code, code, MAX_SKIN_CODE_LEN);
  usrs->skin_code[MAX_SKIN_CODE_LEN] = 0;
  if (acc != SKIN_GEN_KEEP_ACC) usrs->accessory = acc;
  usrs->custom_skin = true;
  usrs->active_saved_skin = -1;
}

/* Apply a skin and remember it in the history. The skin that was on screen
   before the very first apply is stored too, so Back can return to it. */
static void se_use(user_settings* usrs, const skin_gen_skin* sk) {
  if (s_hn == 0 && usrs->skin_code[0]) se_hist_push(usrs->skin_code, usrs->accessory);
  se_apply(usrs, sk->code, sk->accessory);
  se_hist_push(sk->code, usrs->accessory);
}

static void se_seen_add(uint64_t key) {
  for (int i = 0; i < s_nseen; i++)
    if (s_seen[i] == key) return;
  if (s_nseen >= SE_SEEN_MAX) {
    memmove(&s_seen[0], &s_seen[1], sizeof(uint64_t) * (SE_SEEN_MAX - 1));
    s_nseen = SE_SEEN_MAX - 1;
  }
  s_seen[s_nseen++] = key;
}

/* Build a new batch from the current options. Optionally apply the best one. */
static void se_generate(user_settings* usrs, bool apply_best) {
  se_rcp_init();
  s_rcp.seed = skin_gen_new_seed();
  skin_gen_recipe rc = s_rcp;
  if (s_remix) {
    skin_gen_own_from_code(&rc, usrs->skin_code);
    if (rc.own_n < 2) {
      snprintf(s_gmsg, sizeof s_gmsg, "Remix needs a skin with 2+ different colours.");
      return;
    }
  }
  int n = skin_gen_batch(&rc, s_batch, NUM_ACCESSORIES, s_seen, s_nseen, s_cand);
  s_ncand = n;
  s_sel = -1;
  if (n <= 0) {
    snprintf(s_gmsg, sizeof s_gmsg, "Nothing fits these options. Loosen them and try again.");
    return;
  }
  for (int i = 0; i < n; i++) se_seen_add(s_cand[i].key);
  snprintf(s_gmsg, sizeof s_gmsg, "%d new skins. Tap one to use it.", n);
  if (apply_best) {
    se_use(usrs, &s_cand[0]);
    s_sel = 0;
  }
}

/* Every option gets a random value, then a skin is made and applied. */
static void se_surprise(user_settings* usrs) {
  se_rcp_init();
  s_rcp.mood = (int)(se_rnd() % SKIN_MOOD_COUNT);
  s_rcp.theme = (se_rnd() % 100 < 35) ? (int)(se_rnd() % SKIN_THEME_COUNT) : -1;
  s_rcp.style = -1;
  s_rcp.tone = SKIN_TONE_AUTO;
  s_rcp.size = (int)(se_rnd() % SKIN_SIZE_COUNT);
  s_rcp.symmetry = SKIN_SYM_AUTO;
  s_rcp.wildness = 0.05f + (float)(se_rnd() % 60) / 100.0f;
  s_rcp.random_accessory = true;
  s_remix = false;
  se_generate(usrs, true);
}

static void se_opt_label(const char* text, float lw) {
  igTextDisabled("%s", text);
  igSameLine(igGetStyle()->WindowPadding.x + lw, 0);
}

/* The body of the RANDOM tab (called inside the scrolling child panel). */
static void se_random_panel(tenv* env, float ts, float clip_x1, float clip_y1,
                            float clip_x2, float clip_y2) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;
  ImGuiStyle* style = igGetStyle();
  se_rcp_init();

  ImVec2 av;
  igGetContentRegionAvail(&av);
  float w = av.x;
  float half = (w - style->ItemSpacing.x) * 0.5f;

  igTextDisabled("Random skin generator");

  /* Generate / Surprise me */
  igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.510f, 0.294f, 0.910f, 1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.569f, 0.353f, 0.960f, 1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0.450f, 0.243f, 0.850f, 1.0f});
  if (igButton("Generate##gen_go", (ImVec2){half, 0})) se_generate(usrs, true);
  igPopStyleColor(3);
  crystal_sheen();
  igSetItemTooltip("%s", "Make new skins from the options below and use the best one.");
  igSameLine(0, -1);
  if (igButton("Surprise me##gen_sur", (ImVec2){half, 0})) se_surprise(usrs);
  crystal_sheen();
  igSetItemTooltip("%s", "Random options, random skin, random accessory.");

  /* History */
  igBeginDisabled(s_hpos <= 0);
  if (igButton("\uea38 Back##gen_back", (ImVec2){half, 0})) {
    s_hpos--;
    se_apply(usrs, s_hist[s_hpos].code, s_hist[s_hpos].acc);
    s_sel = -1;
  }
  igEndDisabled();
  crystal_sheen();
  igSameLine(0, -1);
  igBeginDisabled(s_hpos < 0 || s_hpos >= s_hn - 1);
  if (igButton("Forward \uea34##gen_fwd", (ImVec2){half, 0})) {
    s_hpos++;
    se_apply(usrs, s_hist[s_hpos].code, s_hist[s_hpos].acc);
    s_sel = -1;
  }
  igEndDisabled();
  crystal_sheen();

  if (s_hn > 0) igTextDisabled("History %d/%d    %s", s_hpos + 1, s_hn, s_gmsg);
  else if (s_gmsg[0]) igTextDisabled("%s", s_gmsg);

  /* Results: one strip per skin, tap to use. */
  if (s_ncand > 0) {
    igSpacing();
    float sh = ts * 0.9f;
    ImDrawList* dl = igGetWindowDrawList();
    for (int i = 0; i < s_ncand; i++) {
      const skin_gen_skin* c = &s_cand[i];
      igPushID_Int(i);
      ImVec2 p;
      igGetCursorScreenPos(&p);
      bool clicked = igInvisibleButton("##cand", (ImVec2){w, sh}, 0);
      bool hov = igIsItemHovered(ImGuiHoveredFlags_None);
      bool sel = (i == s_sel);
      /* Outline only: a fill here would dull the beads drawn under ImGui. */
      ImDrawList_AddRect(dl, p, (ImVec2){p.x + w, p.y + sh},
                         se_col(0.800f, 0.700f, 1.0f, sel ? 0.95f : hov ? 0.65f : 0.25f), 8.0f, 0,
                         sel ? 2.0f : 1.0f);

      float bs = sh * 0.80f, pad = 8.0f;
      float avail = w - pad * 2.0f - bs;
      float step = c->len > 1 ? avail / (float)(c->len - 1) : bs;
      if (step > bs / 3.2f) step = bs / 3.2f;
      if (step < bs / 10.0f) step = bs / 10.0f;
      int nfit = (int)(avail / step) + 1;
      if (nfit > c->len) nfit = c->len;
      bool inside = p.x >= clip_x1 && p.x + w <= clip_x2 && p.y >= clip_y1 && p.y + sh <= clip_y2;
      if (inside) {
        for (int k = 0; k < nfit; k++) {
          int id = get_cg_id(gdata, c->code[k]);
          if (id < 0) continue;
          bp_renderer_push(usr->r->bpr,
                           &(bp_instance){{p.x + pad + k * step, p.y + (sh - bs) * 0.5f, bs},
                                          gdata->cg_uvs[id], {1, 1, 1, 1}});
        }
      }
      igTextDisabled("%s, %d beads, %d%% match%s", skin_gen_style_name(c->style), c->len,
                     (int)(c->score * 100.0f + 0.5f),
                     c->accessory == SKIN_GEN_KEEP_ACC ? "" : ", + accessory");
      igPopID();
      if (clicked) {
        se_use(usrs, c);
        s_sel = i;
      }
    }
  }

  /* Options */
  igSpacing();
  if (igCollapsingHeader_TreeNodeFlags("Options", ImGuiTreeNodeFlags_DefaultOpen)) {
    float lw = w * 0.32f;
    float cw = w - lw;

    se_opt_label("Mood", lw);
    igSetNextItemWidth(cw);
    igCombo_Str("##g_mood", &s_rcp.mood, skin_gen_mood_list(), -1);

    igBeginDisabled(s_remix);
    se_opt_label("Theme", lw);
    igSetNextItemWidth(cw);
    int ti = s_rcp.theme + 1;
    if (igCombo_Str("##g_theme", &ti, skin_gen_theme_list(), -1)) s_rcp.theme = ti - 1;
    igEndDisabled();

    se_opt_label("Pattern", lw);
    igSetNextItemWidth(cw);
    int si = s_rcp.style + 1;
    if (igCombo_Str("##g_style", &si, skin_gen_style_list(), -1)) s_rcp.style = si - 1;

    igBeginDisabled(s_remix || s_rcp.theme >= 0);
    se_opt_label("Brightness", lw);
    igSetNextItemWidth(cw);
    igCombo_Str("##g_tone", &s_rcp.tone, skin_gen_tone_list(), -1);
    igEndDisabled();

    se_opt_label("Band width", lw);
    igSetNextItemWidth(cw);
    igCombo_Str("##g_size", &s_rcp.size, skin_gen_size_list(), -1);

    se_opt_label("Mirror", lw);
    igSetNextItemWidth(cw);
    igCombo_Str("##g_sym", &s_rcp.symmetry, skin_gen_sym_list(), -1);

    se_opt_label("Wildness", lw);
    igSetNextItemWidth(cw);
    igSliderFloat("##g_wild", &s_rcp.wildness, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

    se_opt_label("Max length", lw);
    igSetNextItemWidth(cw);
    igSliderInt("##g_len", &s_rcp.max_len, 8, SKIN_GEN_MAX_CODE, "%d beads",
                ImGuiSliderFlags_AlwaysClamp);

    se_opt_label("Results", lw);
    igSetNextItemWidth(cw);
    igSliderInt("##g_batch", &s_batch, 1, 12, "%d skins", ImGuiSliderFlags_AlwaysClamp);

    igCheckbox("Random accessory##g_acc", &s_rcp.random_accessory);
    igCheckbox("Remix my current colours##g_remix", &s_remix);
    igSetItemTooltip("%s", "Builds new patterns using only the colours of the skin in the preview.");
  }

  igDummy((ImVec2){1, style->ItemSpacing.y * 2.0f});
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
  /* Both views (main + Custom) share ONE preview box: its position and size
     are always computed from the main SKIN EDITOR layout. */
  float row_h = style->ItemSpacing.y + frame_height;
  float tag_h_main = ntl_tags_block_height();
  float stack_h_main = row_h * 6 + tag_h_main;
  float reserved_main = stack_h_main + (scale + style->ItemSpacing.y) * 2.0f;
  float min_grid_main = pop_y + header_h + style->WindowPadding.y + reserved_main;
  float grid_main = (pop_y + header_h + pop_bottom) * 0.5f + reserved_main * 0.5f;
  if (grid_main < min_grid_main) grid_main = min_grid_main;
  float shift_y = scale * 0.8f;
  float box_up = scale * 0.3f;
  float box_dn = shift_y - frame_height * 0.75f;
  grid_main += shift_y;

  /* The NTL tag block only shows in the main view. */
  float tag_h = gdata->skin_editing ? 0.0f : tag_h_main;
  float stack_h = gdata->skin_editing ? row_h * 4 : stack_h_main;
  float grid_y = grid_main;

  float sk_w = scale + (8 * (scale / 48)) * ((MAX_SKIN_CODE_LEN / 2.0f) - 1);

  /* Bounding box around both preview rows (the row further up on screen
     comes from the "+ 2 * (scale + spacing)" offset, the lower one from
     "+ scale + spacing" -- see the two bp_renderer loops below). Padded
     generously for the shadow sprites (which extend 10*(scale/48) past
     each segment) and the eyes/accessory (small offsets past the last
     segment). */
  float preview_row_a_y = grid_main - shift_y - (stack_h_main + scale + style->ItemSpacing.y);
  float preview_row_b_y = grid_main - shift_y - (stack_h_main + 2 * (scale + style->ItemSpacing.y));
  float preview_pad = scale * 0.5f;
  /* The preview box: same vertical band as before, but a fixed, compact
     width instead of the old 256-segment-wide strip. */
  float box_w = pop_w * 0.90f;
  float cur_top = fminf(preview_row_a_y, preview_row_b_y) - preview_pad - box_up;
  float cur_bot = fmaxf(preview_row_a_y, preview_row_b_y) + scale + preview_pad + box_dn;
  float box_extra = 0.20f * (cur_bot - cur_top);
  ImVec2 preview_excl_min = {ctx->size[0] * 0.5f - box_w * 0.5f,
                             fminf(preview_row_a_y, preview_row_b_y) - preview_pad - box_up - box_extra};
  ImVec2 preview_excl_max = {ctx->size[0] * 0.5f + box_w * 0.5f,
                             fmaxf(preview_row_a_y, preview_row_b_y) + scale + preview_pad + box_dn};

  /* Custom view: the buttons start just below the box, then the two tab
     buttons (SKIN COLOURS / ACCESSORIES) sit under OK and the wide panel
     under them. Geometry is computed once here and reused when drawing. */
  float panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
  int ncols = 7;
  float ts = scale; /* colour / accessory tile size in the Custom panel */
  ImVec2 panel_excl_min = {0, 0};
  ImVec2 panel_excl_max = {0, 0};
  if (gdata->skin_editing) {
    grid_y = preview_excl_max.y + row_h * 0.45f + row_h * 4.0f;
    panel_w = pop_w * 0.94f;
    panel_x = ctx->size[0] * 0.5f - panel_w * 0.5f;
    panel_y = grid_y + row_h;
    panel_h = pop_bottom - panel_y - style->WindowPadding.y * 2.0f;
    if (panel_h < scale * 2.25f) panel_h = scale * 2.25f;
    /* Big tiles: 14 per row, as large as the panel width allows. */
    float inner_w = panel_w - style->ScrollbarSize - style->WindowPadding.x * 2.0f;
    ncols = 14;
    ts = (inner_w - (ncols - 1) * style->ItemSpacing.x) / ncols;
    if (ts > scale * 1.6f) ts = scale * 1.6f;
    if (ts < scale) {
      ts = scale;
      ncols = (int)(inner_w / (ts + style->ItemSpacing.x));
      if (ncols < 4) ncols = 4;
    }
    if (usrs->custom_skin) {
      panel_excl_min = (ImVec2){panel_x, panel_y};
      panel_excl_max = (ImVec2){panel_x + panel_w, panel_y + panel_h};
    }
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
  float d = scale * 0.85f;
  se_track tr;
  for (int it = 0; it < 8; it++) {
    se_make_track(&tr, bx0, by0, bw, bh, d);
    float gap = fmaxf(3.0f * d, 0.07f * tr.p);
    float dmax = 6.0f * (tr.p - gap) / (float)n_eff;
    float dlane = (bh - 8.0f) / 3.97f; /* lanes must clear each other */
    if (d > dmax) d = dmax;
    if (d > dlane) d = dlane;
    if (d < scale * 0.18f) d = scale * 0.18f;
  }
  se_make_track(&tr, bx0, by0, bw, bh, d);
  float sp = d / 6.0f;

  /* time: restart (snake re-enters) whenever the screen is opened again */
  static double s_last_t = 0.0;
  static float s_head = 0.0f;
  double now = igGetTime();
  float dt = (float)(now - s_last_t);
  if (dt > 0.25f || dt < 0.0f) { s_head = 0.0f; dt = 0.0f; }
  if (dt > 0.05f) dt = 0.05f;
  s_last_t = now;
  s_head += dt * 154.0f * resolution_scale;
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
    se_snake_at(&tr, u, d, &x, &y, &a);
    bp_renderer_push(usr->r->bpr,
                     &(bp_instance){{x - d * 0.5f - 10 * ks, y - d * 0.5f - 10 * ks, 68 * ks},
                                    gdata->SHADOW_UV, {0, 0, 0, 0.2f}});
  }

  float head_x = 0, head_y = 0, head_a = 0;
  for (int k = 0; k < MAX_SKIN_CODE_LEN; k++) {
    float u = s_head - (MAX_SKIN_CODE_LEN - 1 - k) * sp;
    if (u < -lead) continue;
    float x, y, a;
    se_snake_at(&tr, u, d, &x, &y, &a);
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

  float picker_x = ctx->size[0] * 0.5 - tot_size[0] * 0.5f;
  float half_w = tot_size[0] / 2 - style->ItemSpacing.x / 2;

  if (gdata->skin_editing) {
    /* Rows 4-3: skin code input, then Save (image 2). */
    igSetCursorPosX(picker_x);
    igSetCursorPosY((grid_y) - row_h * 4);
    igSetNextItemWidth(tot_size[0] - 3 * (frame_height + style->ItemSpacing.x));
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
    igSameLine(0, -1);
    if (igButton("R##skin_rnd", (ImVec2){frame_height, frame_height})) {
      se_generate(usrs, true);
    }
    crystal_sheen();
    igSetItemTooltip("%s", "Random skin (see the RANDOM tab for options)");
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

  if (!gdata->skin_editing) s_se_tab = 0;

  if (gdata->skin_editing) {
    /* SKIN COLOURS / ACCESSORIES tabs, directly under OK. */
    const char* tab_names[3] = {"SKIN COLOURS##tab_col", "ACCESSORIES##tab_acc",
                                "RANDOM##tab_rnd"};
    float tab_w = (tot_size[0] - 2.0f * style->ItemSpacing.x) / 3.0f;
    for (int t = 0; t < 3; t++) {
      igSetCursorPosY(grid_y);
      igSetCursorPosX(picker_x + t * (tab_w + style->ItemSpacing.x));
      bool on = s_se_tab == t;
      if (on)
        igPushStyleColor_Vec4(ImGuiCol_Button,
                              (ImVec4){0.510f, 0.294f, 0.910f, 0.60f});
      if (igButton(tab_names[t], (ImVec2){tab_w, 0})) s_se_tab = t;
      crystal_sheen();
      if (on) igPopStyleColor(1);
    }
  }

  if (usrs->custom_skin && gdata->skin_editing) {
    igSetCursorPosX(panel_x);
    igSetCursorPosY(panel_y);
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
      static int last_tab = -1;
      if (last_tab != s_se_tab) {
        igSetScrollY_Float(0.0f);
        last_tab = s_se_tab;
      }
      float clip_x1 = panel_x;
      float clip_y1 = panel_y;
      float clip_x2 = panel_x + panel_w - style->ScrollbarSize;
      float clip_y2 = panel_y + panel_h;
      int skin_code_len = strlen(usrs->skin_code);
      float pitch_x = style->ItemSpacing.x + ts;
      float pitch_y = style->ItemSpacing.y + ts;
      float grid_w = ncols * ts + (ncols - 1) * style->ItemSpacing.x;
      float gx0 = (panel_w - style->ScrollbarSize - grid_w) * 0.5f;
      if (gx0 < 0) gx0 = 0;

      if (s_se_tab == 0) {
        igTextDisabled("Snake colours");
        float colors_y = igGetCursorPosY() + style->ItemSpacing.y;
        int rows = (42 + ncols - 1) / ncols;

        for (int cg_id = 0; cg_id < 42; cg_id++) {
          int i = cg_id / ncols, j = cg_id % ncols;
          bool is_disabled =
              (cg_id == 36 || cg_id == 38 || cg_id == 40 || cg_id == 41 ||
               !(skin_code_len < MAX_SKIN_CODE_LEN));

          igSetCursorPosY(colors_y + pitch_y * i);
          igSetCursorPosX(gx0 + pitch_x * j);
          ImVec2 item_pos;
          igGetCursorScreenPos(&item_pos);
          bool ct = gdata->cg_colors_ct[cg_id];

          float a = 0.8f * (1.0f - 0.5f * is_disabled);
          igBeginDisabled(is_disabled);
          char label[2] = {gdata->ntl_cg_map[cg_id], 0};
          igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, ts / 2);
          igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){ct, ct, ct, 1});
          igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0, 0, 0, 0});
          igPushStyleColor_Vec4(ImGuiCol_Border, (ImVec4){0, 0, 0, 0});
          igPushStyleColor_Vec4(ImGuiCol_BorderShadow, (ImVec4){0, 0, 0, 0});
          igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0, 0, 0, 0});
          igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0, 0, 0, 0});
          igPushID_Int(cg_id);
          bool pressed = igButton(label, (ImVec2){ts, ts});
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

          bool sprite_visible = item_pos.x + ts >= clip_x1 && item_pos.x <= clip_x2 &&
                                item_pos.y + ts >= clip_y1 && item_pos.y <= clip_y2;
          if (sprite_visible) {
            bp_renderer_push(usr->r->bpr,
                             &(bp_instance){{item_pos.x - ts * 0.25f,
                                             item_pos.y - ts * 0.25f,
                                             ts * 1.5f},
                                            gdata->SHADOW_UV,
                                            {0, 0, 0, 0.2f}});
            bp_renderer_push(usr->r->bpr,
                             &(bp_instance){{item_pos.x, item_pos.y, ts},
                                            gdata->cg_uvs[cg_id],
                                            {1, 1, 1, a}});
          }
        }

        igSetCursorPosX(0);
        igSetCursorPosY(colors_y + pitch_y * rows);
        igDummy((ImVec2){1, style->ItemSpacing.y});
      } else if (s_se_tab == 1) {
        igTextDisabled("Accessories");
        float accessories_y = igGetCursorPosY() + style->ItemSpacing.y;
        int total_acc = NUM_ACCESSORIES + 1; /* last tile = no accessory */
        int rows = (total_acc + ncols - 1) / ncols;

        for (int aid = 0; aid < total_acc; aid++) {
          int i = aid / ncols, j = aid % ncols;
          bool is_no_accessory = aid == NUM_ACCESSORIES;
          igSetCursorPosY(accessories_y + pitch_y * i);
          igSetCursorPosX(gx0 + pitch_x * j);
          ImVec2 item_pos;
          igGetCursorScreenPos(&item_pos);
          float a = 0.8f;

          char label[12] = {0};
          sprintf(label, "a%d", aid);
          igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){1, 0.5f, 0.5f, 1});
          if (!is_no_accessory) {
            igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, ts / 2);
            igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0, 0, 0, 0});
            igPushStyleColor_Vec4(ImGuiCol_Border, (ImVec4){0, 0, 0, 0});
            igPushStyleColor_Vec4(ImGuiCol_BorderShadow, (ImVec4){0, 0, 0, 0});
            igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0, 0, 0, 0});
            igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0, 0, 0, 0});
          }
          igPushID_Str(label);
          bool pressed = igButton(is_no_accessory ? "\uea0f" : "",
                                  (ImVec2){ts, ts});
          igPopID();
          if (!is_no_accessory) igPopStyleVar(1);
          igPopStyleColor(is_no_accessory ? 1 : 6);
          if (pressed) usrs->accessory = is_no_accessory ? NO_ACCESSORY : aid;

          bool active = igIsItemActive();
          bool hovered = igIsItemHovered(ImGuiHoveredFlags_None);
          if (hovered) a = 1.0f;
          if (active) a = 0.0f;

          bool sprite_visible = item_pos.x + ts >= clip_x1 && item_pos.x <= clip_x2 &&
                                item_pos.y + ts >= clip_y1 && item_pos.y <= clip_y2;
          if (!is_no_accessory && sprite_visible)
            bp_renderer_push(usr->r->bpr,
                             &(bp_instance){{item_pos.x, item_pos.y, ts},
                                            gdata->accessories[aid].uv,
                                            {1, 1, 1, a}});
        }

        igSetCursorPosX(0);
        igSetCursorPosY(accessories_y + pitch_y * rows);
        igDummy((ImVec2){1, style->ItemSpacing.y});
      } else {
        se_random_panel(env, ts, clip_x1, clip_y1, clip_x2, clip_y2);
      }
    }
    igEndChild();
    igPopStyleColor(5);
  }

  crystal_pop_theme();

  igPopFont();
}

void ui_skin_editor_destroy(tenv* env) {}
