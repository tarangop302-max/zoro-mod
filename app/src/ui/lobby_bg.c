#include "lobby_bg.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../rendering/texture.h"
#include "../user.h"
#include "crystal_theme.h"
#include <math.h>
#include <external/stb/stb_image.h>
#include "settings.h"

#ifdef ANDROID
#include "../android_jni.h"
#include "../android_path.h"
#endif

/* ---- built-in backgrounds -------------------------------------------- */

typedef struct lobby_preset {
  const char* name;
  float c[4][3]; /* corners: top-left, top-right, bottom-right, bottom-left */
  float ga[3];   /* big glow (upper right) */
  float gb[3];   /* small glow (lower left) */
} lobby_preset;

/* Same colours crystal_draw_background() uses. Only used for the thumbnail of
   the "Default" choice -- the full-screen default still calls
   crystal_draw_background() itself. */
static const lobby_preset DEFAULT_PRESET = {
    "Default",
    {{0.078f, 0.039f, 0.141f},
     {0.176f, 0.106f, 0.306f},
     {0.141f, 0.082f, 0.259f},
     {0.114f, 0.063f, 0.212f}},
    {0.592f, 0.353f, 1.0f},
    {0.353f, 0.235f, 0.784f}};

static const lobby_preset PRESETS[LOBBY_BG_PRESET_COUNT] = {
    {"Midnight",
     {{0.02f, 0.04f, 0.12f},
      {0.06f, 0.14f, 0.32f},
      {0.04f, 0.10f, 0.26f},
      {0.03f, 0.07f, 0.20f}},
     {0.30f, 0.55f, 1.00f},
     {0.20f, 0.35f, 0.80f}},
    {"Emerald",
     {{0.02f, 0.09f, 0.07f},
      {0.06f, 0.25f, 0.18f},
      {0.04f, 0.20f, 0.14f},
      {0.03f, 0.14f, 0.10f}},
     {0.25f, 0.90f, 0.60f},
     {0.15f, 0.60f, 0.45f}},
    {"Crimson",
     {{0.12f, 0.03f, 0.05f},
      {0.32f, 0.07f, 0.12f},
      {0.26f, 0.05f, 0.10f},
      {0.20f, 0.04f, 0.08f}},
     {1.00f, 0.35f, 0.40f},
     {0.80f, 0.20f, 0.30f}},
    {"Sunset",
     {{0.14f, 0.05f, 0.18f},
      {0.38f, 0.12f, 0.22f},
      {0.42f, 0.20f, 0.12f},
      {0.22f, 0.08f, 0.20f}},
     {1.00f, 0.55f, 0.30f},
     {0.85f, 0.30f, 0.55f}},
    {"Mono",
     {{0.04f, 0.04f, 0.05f},
      {0.14f, 0.14f, 0.16f},
      {0.10f, 0.10f, 0.12f},
      {0.07f, 0.07f, 0.08f}},
     {0.75f, 0.75f, 0.80f},
     {0.50f, 0.50f, 0.55f}},
    {"Aurora",
     {{0.02f, 0.07f, 0.12f},
      {0.10f, 0.12f, 0.30f},
      {0.06f, 0.20f, 0.26f},
      {0.03f, 0.10f, 0.18f}},
     {0.30f, 1.00f, 0.80f},
     {0.55f, 0.35f, 0.95f}},
};

static ImU32 col(float r, float g, float b, float a) {
  return igColorConvertFloat4ToU32((ImVec4){r, g, b, a});
}

/* Gradient + glow blobs inside [p0,p1]. `full` = full-screen (two big glows,
   like the original); otherwise a thumbnail (one glow, clipped to the rect). */
static void draw_preset(ImDrawList* dl, const lobby_preset* p, ImVec2 p0,
                        ImVec2 p1, bool full) {
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
                           w * 0.55f, p->ga[0], p->ga[1], p->ga[2], 0.45f);
  }
  ImDrawList_PopClipRect(dl);
}

/* ---- uploaded picture -------------------------------------------------- */

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

/* ---- theme that follows the selected background -------------------------
   Presets use their own main glow colour as the UI accent. For an uploaded
   picture the average colour is measured once and the most similar built-in
   theme (by hue, or Mono for near-grey pictures) is used. */

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
  char path[700];
  custom_path(path, sizeof path);
  int w = 0, h = 0, c = 0;
  stbi_uc* data = stbi_load(path, &w, &h, &c, 4);
  if (!data || w < 1 || h < 1) {
    if (data) stbi_image_free(data);
    memcpy(s_theme_acc, DEFAULT_PRESET.ga, sizeof s_theme_acc);
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

  /* Near-grey picture -> the grey theme (Mono). */
  const lobby_preset* best = &DEFAULT_PRESET;
  if (mean_sat < 0.14f) {
    for (int i = 0; i < LOBBY_BG_PRESET_COUNT; ++i)
      if (PRESETS[i].name[0] == 'M' && PRESETS[i].name[1] == 'o') best = &PRESETS[i];
  } else {
    float best_d = hue_dist(avg_hue, rgb_hue(DEFAULT_PRESET.ga[0],
                                             DEFAULT_PRESET.ga[1],
                                             DEFAULT_PRESET.ga[2], NULL));
    for (int i = 0; i < LOBBY_BG_PRESET_COUNT; ++i) {
      float sat;
      float ph = rgb_hue(PRESETS[i].ga[0], PRESETS[i].ga[1], PRESETS[i].ga[2],
                         &sat);
      if (sat < 0.2f) continue; /* skip the grey theme for coloured pictures */
      float d = hue_dist(avg_hue, ph);
      if (d < best_d) {
        best_d = d;
        best = &PRESETS[i];
      }
    }
  }
  memcpy(s_theme_acc, best->ga, sizeof s_theme_acc);
}

void lobby_bg_sync_theme(tenv* env) {
  user_settings* usrs = &env->usr->usrs;
  int idx = usrs->lobby_bg_index;
  if (idx >= 0 && idx < LOBBY_BG_PRESET_COUNT) {
    crystal_theme_set_accent(PRESETS[idx].ga[0], PRESETS[idx].ga[1],
                             PRESETS[idx].ga[2]);
  } else if (idx == LOBBY_BG_CUSTOM) {
    if (!s_theme_valid) compute_custom_theme(env);
    crystal_theme_set_accent(s_theme_acc[0], s_theme_acc[1], s_theme_acc[2]);
  } else {
    crystal_theme_reset_default();
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

/* ---- public ------------------------------------------------------------- */

void ui_lobby_bg_draw(tenv* env) {
  user_settings* usrs = &env->usr->usrs;
#ifdef ANDROID
  poll_picker(env);
#endif

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
  } else if (usrs->lobby_bg_index >= 0 &&
             usrs->lobby_bg_index < LOBBY_BG_PRESET_COUNT) {
    draw_preset(dl, &PRESETS[usrs->lobby_bg_index], (ImVec2){0, 0},
                (ImVec2){w, h}, true);
    return;
  }
  /* Default, or the uploaded picture is missing: original look. */
  crystal_draw_background(env);
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
                          "Pick a look for the main menu", 480.0f)) {
    ui_touch_scroll(env, 21);
    ensure_custom_loaded(env);

    const char* sel_name = "Default";
    if (usrs->lobby_bg_index >= 0 &&
        usrs->lobby_bg_index < LOBBY_BG_PRESET_COUNT)
      sel_name = PRESETS[usrs->lobby_bg_index].name;
    else if (usrs->lobby_bg_index == LOBBY_BG_CUSTOM)
      sel_name = "My picture";

    crystal_section("Looks");

    /* entries: -1 = default, 0..n-1 = presets, LOBBY_BG_CUSTOM = upload */
    int entries[LOBBY_BG_PRESET_COUNT + 2];
    int n = 0;
    entries[n++] = -1;
    for (int i = 0; i < LOBBY_BG_PRESET_COUNT; ++i) entries[n++] = i;
    if (s_tex) entries[n++] = LOBBY_BG_CUSTOM;

    const int cols = 3;
    const float margin = 10.0f; /* room for the selection glow */
    ImVec2 avail;
    igGetContentRegionAvail(&avail);
    float gap = style->ItemSpacing.x + 2.0f;
    float tw = (avail.x - margin * 2.0f - gap * (cols - 1)) / cols;
    if (tw < 40.0f) tw = 40.0f;
    float th = tw * 0.60f;
    float lh = igGetTextLineHeight();
    float card_h = th + lh + 8.0f;
    ImDrawList* dl = igGetWindowDrawList();

    for (int k = 0; k < n; ++k) {
      int e = entries[k];
      int c = k % cols;
      if (c == 0) {
        igDummy((ImVec2){1.0f, 2.0f});
      }
      ImVec2 p0;
      if (c == 0) {
        ImVec2 cur;
        igGetCursorScreenPos(&cur);
        p0 = (ImVec2){cur.x + margin, cur.y};
      } else {
        igSameLine(0, gap);
        igGetCursorScreenPos(&p0);
      }
      igSetCursorScreenPos(p0);

      bool sel = usrs->lobby_bg_index == e;
      if (sel) crystal_glow_rect(p0, (ImVec2){tw, th}, 0.690f, 0.580f, 0.960f);

      igPushID_Int(e + 10);
      bool clicked = igInvisibleButton("##thumb", (ImVec2){tw, card_h}, 0);
      bool hot = igIsItemHovered(0);
      igPopID();

      ImVec2 p1 = {p0.x + tw, p0.y + th};
      if (e == -1)
        draw_preset(dl, &DEFAULT_PRESET, p0, p1, false);
      else if (e == LOBBY_BG_CUSTOM)
        draw_cover(dl, p0, p1);
      else
        draw_preset(dl, &PRESETS[e], p0, p1, false);

      ImDrawList_AddRect(
          dl, p0, p1,
          sel ? col(0.87f, 0.77f, 1.0f, 1.0f)
              : col(0.65f, 0.55f, 0.95f, hot ? 0.90f : 0.40f),
          8.0f, 0, sel ? 3.0f : 1.2f);

      if (sel) { /* check badge, top-right of the thumbnail */
        ImVec2 bc = {p1.x - 12.0f, p0.y + 12.0f};
        ImDrawList_AddCircleFilled(dl, bc, 9.0f, col(0.80f, 0.70f, 1.0f, 1.0f),
                                   18);
        ImDrawList_AddLine(dl, (ImVec2){bc.x - 4.0f, bc.y + 0.5f},
                           (ImVec2){bc.x - 1.0f, bc.y + 3.5f},
                           col(0.10f, 0.05f, 0.20f, 1.0f), 2.0f);
        ImDrawList_AddLine(dl, (ImVec2){bc.x - 1.0f, bc.y + 3.5f},
                           (ImVec2){bc.x + 4.5f, bc.y - 3.0f},
                           col(0.10f, 0.05f, 0.20f, 1.0f), 2.0f);
      }

      const char* label = "Default";
      if (e == LOBBY_BG_CUSTOM)
        label = "My picture";
      else if (e >= 0)
        label = PRESETS[e].name;
      ImVec2 ts;
      igCalcTextSize(&ts, label, NULL, false, -1.0f);
      ImDrawList_AddText_Vec2(
          dl, (ImVec2){p0.x + (tw - ts.x) * 0.5f, p1.y + 5.0f},
          sel ? col(0.95f, 0.92f, 1.0f, 1.0f) : col(0.70f, 0.64f, 0.84f, 1.0f),
          label, NULL);

      if (clicked) {
        usrs->lobby_bg_index = e;
        save_user_settings(usrs);
      }
    }

    if (usrs->lobby_bg_index == LOBBY_BG_CUSTOM && s_tex) {
      crystal_section("Picture dimming");
      igSetNextItemWidth(-1);
      igSliderFloat("##lobby_bg_dim", &usrs->lobby_bg_dim, 0.0f, 0.85f,
                    "Dim %.2f", ImGuiSliderFlags_AlwaysClamp);
      if (igIsItemDeactivatedAfterEdit()) save_user_settings(usrs);
    }

    crystal_section("My picture");
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

    igSpacing();
    igTextDisabled("Selected: %s", sel_name);
    igDummy((ImVec2){1.0f, 6.0f});

    crystal_panel_end();
  }

  crystal_pop_theme();
}

void ui_lobby_bg_destroy(tenv* env) {
  unload_custom(env);
  s_tried = false;
}
