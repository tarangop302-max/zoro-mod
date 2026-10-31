#include "lobby_bg.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../rendering/texture.h"
#include "../user.h"
#include "crystal_theme.h"

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
    crystal_draw_glow_blob(dl, (ImVec2){p0.x + w * 0.72f, p0.y + h * 0.30f},
                           400.0f, p->ga[0], p->ga[1], p->ga[2], 0.30f);
    crystal_draw_glow_blob(dl, (ImVec2){p0.x + w * 0.20f, p0.y + h * 0.80f},
                           340.0f, p->gb[0], p->gb[1], p->gb[2], 0.24f);
  } else {
    crystal_draw_glow_blob(dl, (ImVec2){p0.x + w * 0.70f, p0.y + h * 0.35f},
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

  float popup_w = fminf(460.0f, ctx->size[0] - 24.0f);
  igSetNextWindowSize((ImVec2){popup_w, 0.0f}, ImGuiCond_Appearing);
  igSetNextWindowSizeConstraints((ImVec2){0.0f, 0.0f},
                                 (ImVec2){100000.0f, ctx->size[1] * 0.92f},
                                 NULL, NULL);
  igPushStyleVar_Float(ImGuiStyleVar_ScrollbarSize, 26.0f);
  if (igBeginPopup("Lobby Background", ImGuiWindowFlags_NoSavedSettings)) {
    igPushFont(usr->imgui_data.regular_font[usrs->ui_font_size],
               usr->imgui_data.regular_font[usrs->ui_font_size]->LegacySize);

    igSeparatorText("Lobby background");
    ensure_custom_loaded(env);

    /* entries: -1 = default, 0..n-1 = presets, LOBBY_BG_CUSTOM = upload */
    int entries[LOBBY_BG_PRESET_COUNT + 2];
    int n = 0;
    entries[n++] = -1;
    for (int i = 0; i < LOBBY_BG_PRESET_COUNT; ++i) entries[n++] = i;
    if (s_tex) entries[n++] = LOBBY_BG_CUSTOM;

    const int cols = 4;
    ImVec2 avail;
    igGetContentRegionAvail(&avail);
    float gap = style->ItemSpacing.x;
    float tw = (avail.x - gap * (cols - 1)) / cols;
    float th = tw * 0.58f;
    ImDrawList* dl = igGetWindowDrawList();

    for (int k = 0; k < n; ++k) {
      int e = entries[k];
      if (k % cols) igSameLine(0, -1);
      igPushID_Int(e + 10);
      bool clicked = igInvisibleButton("##thumb", (ImVec2){tw, th}, 0);
      ImVec2 p0, p1;
      igGetItemRectMin(&p0);
      igGetItemRectMax(&p1);
      if (e == -1)
        draw_preset(dl, &DEFAULT_PRESET, p0, p1, false);
      else if (e == LOBBY_BG_CUSTOM)
        draw_cover(dl, p0, p1);
      else
        draw_preset(dl, &PRESETS[e], p0, p1, false);
      bool sel = usrs->lobby_bg_index == e;
      ImDrawList_AddRect(dl, p0, p1,
                         sel ? col(0.87f, 0.77f, 1.0f, 1.0f)
                             : col(0.65f, 0.55f, 0.95f, igIsItemHovered(0) ? 0.9f : 0.45f),
                         6.0f, 0, sel ? 3.0f : 1.2f);
      if (clicked) {
        usrs->lobby_bg_index = e;
        save_user_settings(usrs);
      }
      igPopID();
    }

    const char* sel_name = "Default";
    if (usrs->lobby_bg_index >= 0 &&
        usrs->lobby_bg_index < LOBBY_BG_PRESET_COUNT)
      sel_name = PRESETS[usrs->lobby_bg_index].name;
    else if (usrs->lobby_bg_index == LOBBY_BG_CUSTOM)
      sel_name = "My picture";
    igSpacing();
    igText("Selected: %s", sel_name);

    if (usrs->lobby_bg_index == LOBBY_BG_CUSTOM && s_tex) {
      igSetNextItemWidth(-1);
      igSliderFloat("##lobby_bg_dim", &usrs->lobby_bg_dim, 0.0f, 0.85f,
                    "Dim %.2f", ImGuiSliderFlags_AlwaysClamp);
      if (igIsItemDeactivatedAfterEdit()) save_user_settings(usrs);
    }

    igSpacing();
#ifdef ANDROID
    if (igButton("Upload from gallery", (ImVec2){-1, 0.0f})) {
      s_status[0] = '\0';
      android_jni_request_pick_lobby_bg();
    }
#else
    igTextDisabled("Gallery upload is available on Android.");
#endif
    if (s_status[0]) igTextWrapped("%s", s_status);
    igSpacing();

    igGetContentRegionAvail(&avail);
    if (igButton("Close##lobby_bg", (ImVec2){avail.x, 0.0f})) {
      save_user_settings(usrs);
      igCloseCurrentPopup();
    }

    igPopFont();
    igEndPopup();
  }
  igPopStyleVar(1);

  crystal_pop_theme();
}

void ui_lobby_bg_destroy(tenv* env) {
  unload_custom(env);
  s_tried = false;
}
