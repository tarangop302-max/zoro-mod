#include "lobby_bg.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../rendering/texture.h"
#include "../user.h"
#include "crystal_theme.h"
#include <external/stb/stb_image.h>
#include "settings.h"
#include "lobby_scenes.h"

#ifdef ANDROID
#include "../android_jni.h"
#include "../android_path.h"
#endif

/* ============================================================================
 * Lobby background = THEME (the animated design) x COLOUR (the palette).
 *
 * Any theme can be combined with any colour. Saved in the settings file
 * without changing its layout:
 *   lobby_bg_index       -1 = theme + colour,  LOBBY_BG_CUSTOM = own picture
 *                        (0..LOBBY_BG_PRESET_COUNT-1 = values written by the
 *                         older single-list version; converted on first use)
 *   lobby_bg_reserved[0] 1 = animations frozen
 *   lobby_bg_reserved[1] theme  index  (0 = Default)
 *   lobby_bg_reserved[2] colour index  (0 = Purple, the original look)
 * ========================================================================== */

#define LOBBY_THEME_COUNT 10
#define LOBBY_COLOUR_COUNT 30
#define LOBBY_LEGACY_CLASSIC 6 /* old list: 0..5 were the plain gradients */

typedef struct lobby_theme {
  const char* name;
  int style; /* lobby_style */
} lobby_theme;

/* Order matters only for the saved index: add new themes at the END. */
static const lobby_theme THEMES[LOBBY_THEME_COUNT] = {
    {"Default", LOBBY_STYLE_GLOW},    {"Embers", LOBBY_STYLE_EMBERS},
    {"Sunburst", LOBBY_STYLE_SUNBURST}, {"Grid", LOBBY_STYLE_GRID},
    {"Stars", LOBBY_STYLE_STARS},     {"Petals", LOBBY_STYLE_PETALS},
    {"Code", LOBBY_STYLE_CODE},       {"Waves", LOBBY_STYLE_WAVES},
    {"Slash", LOBBY_STYLE_BLADE},     {"Rain", LOBBY_STYLE_RAIN},
};

typedef struct lobby_colour {
  const char* name;
  float c[4][3]; /* corners: top-left, top-right, bottom-right, bottom-left */
  float ga[3];   /* main accent / big glow (upper right) */
  float gb[3];   /* second accent / small glow (lower left) */
} lobby_colour;

static lobby_colour COLOURS[LOBBY_COLOUR_COUNT];
static bool s_palette_ready = false;

static void hsv_to_rgb(float h, float s, float v, float out[3]) {
  h = fmodf(h, 360.0f);
  if (h < 0.0f) h += 360.0f;
  if (s < 0.0f) s = 0.0f;
  if (s > 1.0f) s = 1.0f;
  if (v < 0.0f) v = 0.0f;
  if (v > 1.0f) v = 1.0f;
  float c = v * s;
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  float m = v - c;
  float r = 0, g = 0, b = 0;
  if (h < 60.0f) { r = c; g = x; }
  else if (h < 120.0f) { r = x; g = c; }
  else if (h < 180.0f) { g = c; b = x; }
  else if (h < 240.0f) { g = x; b = c; }
  else if (h < 300.0f) { r = x; b = c; }
  else { r = c; b = x; }
  out[0] = r + m;
  out[1] = g + m;
  out[2] = b + m;
}

/* The 7 original colours keep their exact hand-tuned values (same as the old
   background list). */
static const lobby_colour ORIGINAL_COLOURS[7] = {
    {"Purple",
     {{0.078f, 0.039f, 0.141f}, {0.176f, 0.106f, 0.306f},
      {0.141f, 0.082f, 0.259f}, {0.114f, 0.063f, 0.212f}},
     {0.592f, 0.353f, 1.0f}, {0.353f, 0.235f, 0.784f}},
    {"Midnight",
     {{0.02f, 0.04f, 0.12f}, {0.06f, 0.14f, 0.32f}, {0.04f, 0.10f, 0.26f},
      {0.03f, 0.07f, 0.20f}},
     {0.30f, 0.55f, 1.00f}, {0.20f, 0.35f, 0.80f}},
    {"Emerald",
     {{0.02f, 0.09f, 0.07f}, {0.06f, 0.25f, 0.18f}, {0.04f, 0.20f, 0.14f},
      {0.03f, 0.14f, 0.10f}},
     {0.25f, 0.90f, 0.60f}, {0.15f, 0.60f, 0.45f}},
    {"Crimson",
     {{0.12f, 0.03f, 0.05f}, {0.32f, 0.07f, 0.12f}, {0.26f, 0.05f, 0.10f},
      {0.20f, 0.04f, 0.08f}},
     {1.00f, 0.35f, 0.40f}, {0.80f, 0.20f, 0.30f}},
    {"Sunset",
     {{0.14f, 0.05f, 0.18f}, {0.38f, 0.12f, 0.22f}, {0.42f, 0.20f, 0.12f},
      {0.22f, 0.08f, 0.20f}},
     {1.00f, 0.55f, 0.30f}, {0.85f, 0.30f, 0.55f}},
    {"Mono",
     {{0.04f, 0.04f, 0.05f}, {0.14f, 0.14f, 0.16f}, {0.10f, 0.10f, 0.12f},
      {0.07f, 0.07f, 0.08f}},
     {0.75f, 0.75f, 0.80f}, {0.50f, 0.50f, 0.55f}},
    {"Aurora",
     {{0.02f, 0.07f, 0.12f}, {0.10f, 0.12f, 0.30f}, {0.06f, 0.20f, 0.26f},
      {0.03f, 0.10f, 0.18f}},
     {0.30f, 1.00f, 0.80f}, {0.55f, 0.35f, 0.95f}},
};

/* The other 23 are generated from a hue (same recipe the originals follow:
   dark gradient corners, bright main accent, shifted second accent). */
typedef struct colour_gen {
  const char* name;
  float hue, sat, val, shift;
} colour_gen;

static const colour_gen GENERATED[LOBBY_COLOUR_COUNT - 7] = {
    {"Ruby", 350, 0.80f, 1.00f, 25},     {"Rose", 335, 0.60f, 1.05f, 20},
    {"Magenta", 305, 0.78f, 1.00f, -30}, {"Violet", 275, 0.72f, 1.00f, 30},
    {"Indigo", 245, 0.75f, 1.00f, -25},  {"Cobalt", 222, 0.80f, 1.00f, 30},
    {"Sky", 200, 0.62f, 1.10f, -20},     {"Cyan", 185, 0.78f, 1.00f, 35},
    {"Teal", 170, 0.72f, 1.00f, -30},    {"Jade", 150, 0.72f, 1.00f, 30},
    {"Lime", 105, 0.72f, 1.00f, -35},    {"Olive", 72, 0.62f, 1.00f, 25},
    {"Gold", 46, 0.85f, 1.00f, -15},     {"Amber", 35, 0.88f, 1.00f, 15},
    {"Orange", 24, 0.86f, 1.00f, -20},   {"Coral", 10, 0.68f, 1.05f, 25},
    {"Peach", 18, 0.42f, 1.15f, 15},     {"Lavender", 262, 0.38f, 1.10f, 20},
    {"Mint", 160, 0.38f, 1.10f, -20},    {"Ice", 195, 0.32f, 1.15f, 15},
    {"Bronze", 30, 0.62f, 0.85f, -12},   {"Wine", 340, 0.72f, 0.80f, 20},
    {"Steel", 215, 0.26f, 1.00f, 15},
};

static void ensure_palette(void) {
  if (s_palette_ready) return;
  s_palette_ready = true;
  for (int i = 0; i < 7; ++i) COLOURS[i] = ORIGINAL_COLOURS[i];
  for (int i = 0; i < LOBBY_COLOUR_COUNT - 7; ++i) {
    const colour_gen* g = &GENERATED[i];
    lobby_colour* o = &COLOURS[7 + i];
    o->name = g->name;
    hsv_to_rgb(g->hue, g->sat, 0.14f * g->val, o->c[0]);
    hsv_to_rgb(g->hue, g->sat * 0.90f, 0.31f * g->val, o->c[1]);
    hsv_to_rgb(g->hue, g->sat * 0.95f, 0.26f * g->val, o->c[2]);
    hsv_to_rgb(g->hue, g->sat, 0.20f * g->val, o->c[3]);
    hsv_to_rgb(g->hue, g->sat * 0.90f, 1.00f, o->ga);
    hsv_to_rgb(g->hue + g->shift, g->sat * 0.90f, 0.80f, o->gb);
  }
}

static int colour_by_name(const char* name) {
  for (int i = 0; i < LOBBY_COLOUR_COUNT; ++i)
    if (strcmp(COLOURS[i].name, name) == 0) return i;
  return 0;
}

static ImU32 col(float r, float g, float b, float a) {
  return igColorConvertFloat4ToU32((ImVec4){r, g, b, a});
}

/* ---- saved selection ------------------------------------------------------ */

static bool anim_on(const user_settings* u) { return u->lobby_bg_reserved[0] == 0; }

static float scene_time(const user_settings* u) {
  return anim_on(u) ? (float)fmod(igGetTime(), 7200.0) : 12.0f;
}

static int cur_theme(const user_settings* u) {
  int v = u->lobby_bg_reserved[1];
  return (v >= 0 && v < LOBBY_THEME_COUNT) ? v : 0;
}

static int cur_colour(const user_settings* u) {
  int v = u->lobby_bg_reserved[2];
  return (v >= 0 && v < LOBBY_COLOUR_COUNT) ? v : 0;
}

/* Settings saved by the previous single-list version (index 0..13) become a
   theme + colour pair, once. */
static void migrate_legacy(user_settings* u) {
  int idx = u->lobby_bg_index;
  if (idx < 0 || idx >= LOBBY_BG_PRESET_COUNT) return;
  if (idx < LOBBY_LEGACY_CLASSIC) {
    u->lobby_bg_reserved[1] = 0;
    u->lobby_bg_reserved[2] = (uint8_t)(idx + 1); /* Midnight..Aurora */
  } else {
    static const char* const NATIVE[8] = {"Orange", "Gold",    "Magenta", "Indigo",
                                          "Rose",   "Emerald", "Sky",     "Jade"};
    int t = idx - LOBBY_LEGACY_CLASSIC + 1;
    if (t >= LOBBY_THEME_COUNT) t = 0;
    u->lobby_bg_reserved[1] = (uint8_t)t;
    u->lobby_bg_reserved[2] = (uint8_t)colour_by_name(NATIVE[(t - 1) & 7]);
  }
  u->lobby_bg_index = -1;
  save_user_settings(u);
}

/* ---- drawing ---------------------------------------------------------------- */

/* Gradient + glow blobs + the theme's animated scene, inside [p0,p1].
   `full` = full-screen; otherwise a thumbnail (one glow, fewer particles). */
static void draw_look(ImDrawList* dl, int theme, int colour, ImVec2 p0,
                      ImVec2 p1, bool full, float t) {
  const lobby_colour* p = &COLOURS[colour];
  float w = p1.x - p0.x;
  float h = p1.y - p0.y;
  ImDrawList_PushClipRect(dl, p0, p1, true);
  ImDrawList_AddRectFilledMultiColor(
      dl, p0, p1, col(p->c[0][0], p->c[0][1], p->c[0][2], 1.0f),
      col(p->c[1][0], p->c[1][1], p->c[1][2], 1.0f),
      col(p->c[2][0], p->c[2][1], p->c[2][2], 1.0f),
      col(p->c[3][0], p->c[3][1], p->c[3][2], 1.0f));
  if (full) {
    crystal_draw_glow_blob_raw(dl, (ImVec2){p0.x + w * 0.72f, p0.y + h * 0.30f},
                               400.0f, p->ga[0], p->ga[1], p->ga[2], 0.30f);
    crystal_draw_glow_blob_raw(dl, (ImVec2){p0.x + w * 0.20f, p0.y + h * 0.80f},
                               340.0f, p->gb[0], p->gb[1], p->gb[2], 0.24f);
  } else {
    crystal_draw_glow_blob_raw(dl, (ImVec2){p0.x + w * 0.70f, p0.y + h * 0.35f},
                               w * 0.45f, p->ga[0], p->ga[1], p->ga[2], 0.45f);
  }
  if (THEMES[theme].style != LOBBY_STYLE_GLOW)
    lobby_scene_draw(dl, THEMES[theme].style, p0, p1, t, full, p->ga, p->gb);
  ImDrawList_PopClipRect(dl);
}

/* ---- uploaded picture -------------------------------------------------------- */

static texture* s_tex = NULL;
static VkDescriptorSet s_ds = VK_NULL_HANDLE;
static bool s_tried = false;  /* already attempted to load the file */
static char s_status[96] = "";

static void custom_path(char* out, int out_size) {
#ifdef ANDROID
  android_build_path(out, out_size, "lobby_bg.jpg");
#else
  snprintf(out, (size_t)out_size, "%s", "lobby_bg.jpg");
#endif
}

static void unload_custom(tenv* env) {
  if (!s_tex) return;
  tcontext_wait_idle(env->ctx);
  igImplVulkan_RemoveTexture(s_ds);
  destroy_texture(env->ctx, s_tex);
  s_tex = NULL;
  s_ds = VK_NULL_HANDLE;
}

/* Loads the uploaded picture once (again after a new upload). */
static void ensure_custom_loaded(tenv* env) {
  if (s_tex || s_tried) return;
  s_tried = true;
  char path[700];
  custom_path(path, sizeof path);
  s_tex = create_mipmap_texture_from_filepath(env->ctx, path);
  if (s_tex) {
    s_ds = igImplVulkan_AddTexture(env->usr->r->linear_sampler, s_tex->view,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }
}

/* ---- UI theme that follows the selected background -----------------------------
   A colour uses its own main accent for the UI. For an uploaded picture the
   average colour is measured once and the most similar colour (by hue, or Mono
   for near-grey pictures) is used. */

static bool s_theme_valid = false;
static float s_theme_acc[3] = {0.592f, 0.353f, 1.0f};

static float rgb_hue(float r, float g, float b, float* sat_out) {
  float mx = fmaxf(r, fmaxf(g, b));
  float mn = fminf(r, fminf(g, b));
  float d = mx - mn;
  if (sat_out) *sat_out = mx > 0.0f ? d / mx : 0.0f;
  if (d < 1e-6f) return 0.0f;
  float h;
  if (mx == r)
    h = 60.0f * fmodf((g - b) / d, 6.0f);
  else if (mx == g)
    h = 60.0f * ((b - r) / d + 2.0f);
  else
    h = 60.0f * ((r - g) / d + 4.0f);
  if (h < 0.0f) h += 360.0f;
  return h;
}

static float hue_dist(float a, float b) {
  float d = fabsf(a - b);
  return d > 180.0f ? 360.0f - d : d;
}

static void compute_custom_theme(tenv* env) {
  (void)env;
  s_theme_valid = true;
  ensure_palette();
  char path[700];
  custom_path(path, sizeof path);
  int w = 0, h = 0, c = 0;
  stbi_uc* data = stbi_load(path, &w, &h, &c, 4);
  if (!data || w < 1 || h < 1) {
    if (data) stbi_image_free(data);
    memcpy(s_theme_acc, COLOURS[0].ga, sizeof s_theme_acc);
    return;
  }
  long total = (long)w * (long)h;
  long step = total / 40000;
  if (step < 1) step = 1;

  double sx = 0.0, sy = 0.0, wsum = 0.0, ssum = 0.0;
  long n = 0;
  for (long i = 0; i < total; i += step) {
    const stbi_uc* px = data + i * 4;
    float r = px[0] / 255.0f, g = px[1] / 255.0f, b = px[2] / 255.0f;
    float sat;
    float hue = rgb_hue(r, g, b, &sat);
    float v = fmaxf(r, fmaxf(g, b));
    float wt = sat * v + 0.001f;
    float rad = hue * 3.14159265f / 180.0f;
    sx += cosf(rad) * wt;
    sy += sinf(rad) * wt;
    wsum += wt;
    ssum += sat;
    n++;
  }
  stbi_image_free(data);

  float mean_sat = n > 0 ? (float)(ssum / (double)n) : 0.0f;
  float avg_hue = 262.0f;
  if (wsum > 0.0)
    avg_hue = atan2f((float)sy, (float)sx) * 180.0f / 3.14159265f;
  if (avg_hue < 0.0f) avg_hue += 360.0f;

  int best = 0;
  if (mean_sat < 0.14f) {
    best = colour_by_name("Mono"); /* near-grey picture -> grey theme */
  } else {
    float best_d = 1e9f;
    for (int i = 0; i < LOBBY_COLOUR_COUNT; ++i) {
      float sat;
      float ph = rgb_hue(COLOURS[i].ga[0], COLOURS[i].ga[1], COLOURS[i].ga[2],
                         &sat);
      if (sat < 0.5f) continue; /* skip grey / pastel colours */
      float d = hue_dist(avg_hue, ph);
      if (d < best_d) {
        best_d = d;
        best = i;
      }
    }
  }
  memcpy(s_theme_acc, COLOURS[best].ga, sizeof s_theme_acc);
}

void lobby_bg_sync_theme(tenv* env) {
  user_settings* usrs = &env->usr->usrs;
  ensure_palette();
  migrate_legacy(usrs);
  if (usrs->lobby_bg_index == LOBBY_BG_CUSTOM) {
    if (!s_theme_valid) compute_custom_theme(env);
    crystal_theme_set_accent(s_theme_acc[0], s_theme_acc[1], s_theme_acc[2]);
  } else {
    int c = cur_colour(usrs);
    if (c == 0)
      crystal_theme_reset_default();
    else
      crystal_theme_set_accent(COLOURS[c].ga[0], COLOURS[c].ga[1],
                               COLOURS[c].ga[2]);
  }
}

/* Draws the picture so it fills [p0,p1] (cropped, aspect ratio kept). */
static void draw_cover(ImDrawList* dl, ImVec2 p0, ImVec2 p1) {
  float w = p1.x - p0.x;
  float h = p1.y - p0.y;
  float iw = (float)s_tex->size[0];
  float ih = (float)s_tex->size[1];
  if (iw < 1.0f || ih < 1.0f || w < 1.0f || h < 1.0f) return;
  float scale = w / iw > h / ih ? w / iw : h / ih;
  float u = w / (iw * scale);
  float v = h / (ih * scale);
  ImTextureRef tref = {NULL, (ImTextureID)s_ds};
  ImDrawList_PushClipRect(dl, p0, p1, true);
  ImDrawList_AddImage(dl, tref, p0, p1,
                      (ImVec2){0.5f - u * 0.5f, 0.5f - v * 0.5f},
                      (ImVec2){0.5f + u * 0.5f, 0.5f + v * 0.5f},
                      col(1.0f, 1.0f, 1.0f, 1.0f));
  ImDrawList_PopClipRect(dl);
}

#ifdef ANDROID
/* The gallery picker is asynchronous: Kotlin copies the chosen picture into
   the app folder and queues an event that is picked up here. */
static void poll_picker(tenv* env) {
  int ev = android_jni_poll_lobby_bg_event();
  if (ev == 0) return;
  user_settings* usrs = &env->usr->usrs;
  if (ev == 1) {
    unload_custom(env);
    s_tried = false;
    s_theme_valid = false; /* re-measure the new picture */
    s_status[0] = '\0';
    usrs->lobby_bg_index = LOBBY_BG_CUSTOM;
    save_user_settings(usrs);
  } else {
    snprintf(s_status, sizeof s_status, "Couldn't load that picture.");
  }
}
#endif

/* ---- public ------------------------------------------------------------------- */

void ui_lobby_bg_draw(tenv* env) {
  user_settings* usrs = &env->usr->usrs;
#ifdef ANDROID
  poll_picker(env);
#endif
  ensure_palette();
  migrate_legacy(usrs);

  float w = (float)env->ctx->size[0];
  float h = (float)env->ctx->size[1];
  ImDrawList* dl = igGetWindowDrawList();

  if (usrs->lobby_bg_index == LOBBY_BG_CUSTOM) {
    ensure_custom_loaded(env);
    if (s_tex) {
      draw_cover(dl, (ImVec2){0, 0}, (ImVec2){w, h});
      float dim = usrs->lobby_bg_dim;
      if (dim > 0.0f)
        ImDrawList_AddRectFilled(dl, (ImVec2){0, 0}, (ImVec2){w, h},
                                 col(0.0f, 0.0f, 0.0f, dim), 0, 0);
      return;
    }
  } else {
    int theme = cur_theme(usrs);
    int colour = cur_colour(usrs);
    if (theme == 0 && colour == 0) {
      crystal_draw_background(env); /* the original look, untouched */
      return;
    }
    draw_look(dl, theme, colour, (ImVec2){0, 0}, (ImVec2){w, h}, true,
              scene_time(usrs));
    return;
  }
  /* uploaded picture missing: original look */
  crystal_draw_background(env);
}

/* ---- picker -------------------------------------------------------------------- */

/* Touch-friendly tap on a rectangle drawn with the draw list. Unlike an
   InvisibleButton it never becomes the "active item", so a finger that lands
   on a tile can still scroll the panel, and a scroll can never select a tile:
   it only counts when the finger lifts without having moved or scrolled. */
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
      s_press_id = -1; /* lost the release (panel closed, etc.) */
    }
  }
  return tap;
}

static void draw_check(ImDrawList* dl, ImVec2 p1, ImVec2 p0) {
  ImVec2 bc = {p1.x - 12.0f, p0.y + 12.0f};
  ImDrawList_AddCircleFilled(dl, bc, 9.0f, col(0.80f, 0.70f, 1.0f, 1.0f), 18);
  ImDrawList_AddLine(dl, (ImVec2){bc.x - 4.0f, bc.y + 0.5f},
                     (ImVec2){bc.x - 1.0f, bc.y + 3.5f},
                     col(0.10f, 0.05f, 0.20f, 1.0f), 2.0f);
  ImDrawList_AddLine(dl, (ImVec2){bc.x - 1.0f, bc.y + 3.5f},
                     (ImVec2){bc.x + 4.5f, bc.y - 3.0f},
                     col(0.10f, 0.05f, 0.20f, 1.0f), 2.0f);
}

static void draw_frame(ImDrawList* dl, ImVec2 p0, ImVec2 p1, bool sel,
                       bool hot) {
  ImDrawList_AddRect(dl, p0, p1,
                     sel ? col(0.87f, 0.77f, 1.0f, 1.0f)
                         : col(0.65f, 0.55f, 0.95f, hot ? 0.90f : 0.40f),
                     8.0f, 0, sel ? 3.0f : 1.2f);
}

/* Name strip along the bottom of a tile. */
static void draw_tile_label(ImDrawList* dl, ImVec2 p0, ImVec2 p1,
                            const char* label, bool sel) {
  float lh = igGetTextLineHeight();
  ImDrawList_AddRectFilled(dl, (ImVec2){p0.x, p1.y - lh - 8.0f}, p1,
                           col(0.0f, 0.0f, 0.0f, 0.50f), 0, 0);
  ImDrawList_AddText_Vec2(
      dl, (ImVec2){p0.x + 9.0f, p1.y - lh - 4.0f},
      sel ? col(1.0f, 0.97f, 1.0f, 1.0f) : col(0.82f, 0.78f, 0.92f, 1.0f),
      label, NULL);
}

static void set_theme(user_settings* u, int theme) {
  u->lobby_bg_reserved[1] = (uint8_t)theme;
  u->lobby_bg_index = -1;
  save_user_settings(u);
}

static void set_colour(user_settings* u, int colour) {
  u->lobby_bg_reserved[2] = (uint8_t)colour;
  u->lobby_bg_index = -1;
  save_user_settings(u);
}

void ui_lobby_bg_button(tenv* env, float frame_height) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  user_settings* usrs = &usr->usrs;
  ImGuiStyle* style = igGetStyle();

  /* Same x / width as the Snakey Rain button, one row further down. */
  const float btn_w = 190.0f;
  float btn_x = ctx->size[0] - btn_w - style->WindowPadding.x * 4;
  float row = frame_height + style->ItemSpacing.y * 2;
  float btn_y = style->WindowPadding.y * 4 + row * 2;

  crystal_push_theme();

  igSetCursorPosX(btn_x);
  igSetCursorPosY(btn_y);
  if (igButton("Background##lobby_bg_btn", (ImVec2){btn_w, frame_height}))
    igOpenPopup_Str("Lobby Background", 0);
  crystal_sheen();

  /* Fixed-size themed panel (see crystal_panel_begin): it can't shrink. */
  if (crystal_panel_begin(env, "Lobby Background", "LOBBY BACKGROUND",
                          "Pick a theme and a colour", 540.0f)) {
    ui_touch_scroll(env, 21);
    ensure_custom_loaded(env);
    ensure_palette();
    migrate_legacy(usrs);

    const bool custom = usrs->lobby_bg_index == LOBBY_BG_CUSTOM && s_tex;
    const int theme = cur_theme(usrs);
    const int colour = cur_colour(usrs);
    ImDrawList* dl = igGetWindowDrawList();

    if (custom)
      igTextDisabled("Now: My picture");
    else
      igTextDisabled("Now: %s  +  %s", THEMES[theme].name, COLOURS[colour].name);

    bool anim = anim_on(usrs);
    if (igCheckbox("Animated backgrounds", &anim)) {
      usrs->lobby_bg_reserved[0] = anim ? 0 : 1;
      save_user_settings(usrs);
    }
    igSpacing();

    /* ---- layout: themes down the left, 3 columns of colours on the right.
       Every theme row lines up with one row of three colours. ---- */
    ImVec2 avail;
    igGetContentRegionAvail(&avail);
    const float m = 8.0f;   /* room for the selection glow */
    const float gap = 14.0f;
    float left_w = floorf(avail.x * 0.47f);
    float tile_w = left_w - m * 2.0f;
    if (tile_w < 80.0f) tile_w = 80.0f;
    float th = tile_w * 0.34f;
    float rh = th + 8.0f;
    float lh = igGetTextLineHeight();
    float cap_h = lh + 8.0f;
    float rx = left_w + gap; /* x offset of the colour block */
    float cgap = 8.0f;
    float cw = (avail.x - rx - m - cgap * 2.0f) / 3.0f;
    if (cw < 24.0f) cw = 24.0f;

    ImVec2 org;
    igGetCursorScreenPos(&org);
    ImU32 cap_col = col(0.70f, 0.64f, 0.84f, 1.0f);
    ImDrawList_AddText_Vec2(dl, (ImVec2){org.x + m, org.y}, cap_col, "THEME",
                            NULL);
    ImDrawList_AddText_Vec2(dl, (ImVec2){org.x + rx, org.y}, cap_col, "COLOUR",
                            NULL);
    float y0 = org.y + cap_h;
    float t_now = scene_time(usrs);

    /* themes (drawn in the currently selected colour) */
    for (int i = 0; i < LOBBY_THEME_COUNT; ++i) {
      ImVec2 p0 = {org.x + m, y0 + i * rh};
      ImVec2 p1 = {p0.x + tile_w, p0.y + th};
      bool sel = !custom && theme == i;
      bool hot = false;
      bool tap = tile_tap(1000 + i, p0, p1, &hot);
      if (sel) crystal_glow_rect(p0, (ImVec2){tile_w, th}, 0.690f, 0.580f, 0.960f);
      draw_look(dl, i, colour, p0, p1, false,
                (anim && (hot || sel)) ? t_now : 12.0f);
      draw_tile_label(dl, p0, p1, THEMES[i].name, sel);
      draw_frame(dl, p0, p1, sel, hot);
      if (sel) draw_check(dl, p1, p0);
      if (tap) set_theme(usrs, i);
    }

    /* colours (3 per row) */
    for (int k = 0; k < LOBBY_COLOUR_COUNT; ++k) {
      int r = k / 3, c = k % 3;
      ImVec2 p0 = {org.x + rx + c * (cw + cgap), y0 + r * rh};
      ImVec2 p1 = {p0.x + cw, p0.y + th};
      bool sel = !custom && colour == k;
      bool hot = false;
      bool tap = tile_tap(2000 + k, p0, p1, &hot);
      if (sel) crystal_glow_rect(p0, (ImVec2){cw, th}, 0.690f, 0.580f, 0.960f);
      draw_look(dl, 0, k, p0, p1, false, 12.0f);
      draw_frame(dl, p0, p1, sel, hot);
      if (sel) draw_check(dl, p1, p0);
      if (tap) set_colour(usrs, k);
    }

    igDummy((ImVec2){1.0f, cap_h + rh * LOBBY_THEME_COUNT + 4.0f});

    /* ---- own picture ---- */
    if (s_tex) {
      crystal_section("My picture");
      ImVec2 cur;
      igGetCursorScreenPos(&cur);
      ImVec2 p0 = {cur.x + m, cur.y + 2.0f};
      ImVec2 p1 = {p0.x + tile_w, p0.y + th};
      bool hot = false;
      bool tap = tile_tap(3000, p0, p1, &hot);
      if (custom) crystal_glow_rect(p0, (ImVec2){tile_w, th}, 0.690f, 0.580f, 0.960f);
      draw_cover(dl, p0, p1);
      draw_tile_label(dl, p0, p1, "My picture", custom);
      draw_frame(dl, p0, p1, custom, hot);
      if (custom) draw_check(dl, p1, p0);
      if (tap) {
        usrs->lobby_bg_index = LOBBY_BG_CUSTOM;
        save_user_settings(usrs);
      }
      igDummy((ImVec2){1.0f, th + 8.0f});
    } else {
      crystal_section("My picture");
    }

    if (custom) {
      igTextDisabled("Picture dimming");
      igSetNextItemWidth(-1);
      igSliderFloat("##lobby_bg_dim", &usrs->lobby_bg_dim, 0.0f, 0.85f,
                    "Dim %.2f", ImGuiSliderFlags_AlwaysClamp);
      if (igIsItemDeactivatedAfterEdit()) save_user_settings(usrs);
    }

#ifdef ANDROID
    if (igButton("Upload from gallery", (ImVec2){-1, 0.0f})) {
      s_status[0] = '\0';
      android_jni_request_pick_lobby_bg();
    }
    crystal_sheen();
#else
    igTextDisabled("Gallery upload is available on Android.");
#endif
    if (s_status[0]) igTextWrapped("%s", s_status);

    igDummy((ImVec2){1.0f, 6.0f});

    crystal_panel_end();
  }

  crystal_pop_theme();
}

void ui_lobby_bg_destroy(tenv* env) {
  unload_custom(env);
  s_tried = false;
}
