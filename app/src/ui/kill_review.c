#include "kill_review.h"

#include <stdio.h>

#include "../user.h"
#include "crystal_theme.h"
#include "settings.h"
#include "../rendering/texture.h"
#include "../game/screenshot.h"

#ifndef IM_COL32
#define IM_COL32(R, G, B, A) \
  crystal_tint_u32((((ImU32)(A) << 24) | ((ImU32)(B) << 16) | ((ImU32)(G) << 8) | ((ImU32)(R))))
#endif

/* Shown right after a match ends with at least one captured kill (see
   death_screen.c's "Kill Shots" button and loop.c's kill_review_pending
   handling). Everything here reads from screenshot.c's in-memory,
   run-scoped captures -- nothing is on disk yet. Leaving via either
   button always clears those captures (screenshot_run_reset()), since by
   definition the run is over by the time this screen is showing. */

typedef struct {
  texture *tex;
  VkDescriptorSet ds;
  int w, h;        /* source size, for undistorted display */
  int kill_number; /* label on the card */
} preview_item;

static preview_item s_previews[SCREENSHOT_MAX_RUN_CAPTURES];
static bool s_selected[SCREENSHOT_MAX_RUN_CAPTURES];
static bool s_built = false;

void ui_kill_review_init(tenv *env) {
  (void)env;
  for (int i = 0; i < SCREENSHOT_MAX_RUN_CAPTURES; i++) {
    s_previews[i].tex = NULL;
    s_previews[i].ds = VK_NULL_HANDLE;
    s_previews[i].w = s_previews[i].h = 0;
    s_previews[i].kill_number = i + 1;
    s_selected[i] = true; /* default: keep everything, let the player
                             deselect the ones they don't want */
  }
  s_built = false;
}

static void unload_previews(tenv *env) {
  bool any = false;
  for (int i = 0; i < SCREENSHOT_MAX_RUN_CAPTURES; i++)
    if (s_previews[i].tex) {
      any = true;
      break;
    }
  if (any && env && env->ctx) {
    tcontext_wait_idle(env->ctx);
    for (int i = 0; i < SCREENSHOT_MAX_RUN_CAPTURES; i++) {
      if (s_previews[i].tex) {
        igImplVulkan_RemoveTexture(s_previews[i].ds);
        destroy_texture(env->ctx, s_previews[i].tex);
        s_previews[i].tex = NULL;
        s_previews[i].ds = VK_NULL_HANDLE;
      }
    }
  }
  s_built = false;
}

static void build_previews(tenv *env) {
  int count = screenshot_run_count();
  for (int i = 0; i < count && i < SCREENSHOT_MAX_RUN_CAPTURES; i++) {
    const unsigned char *rgba;
    int w, h, kill_number;
    if (!screenshot_run_get(i, &rgba, &w, &h, &kill_number)) continue;
    /* Non-owning -- doesn't free/modify screenshot.c's buffer, so it's
       still there for screenshot_run_save() afterward. */
    texture *tex = create_mipmap_texture_from_pixels(env->ctx, rgba, w, h);
    if (!tex) continue;
    s_previews[i].tex = tex;
    s_previews[i].w = w;
    s_previews[i].h = h;
    s_previews[i].kill_number = kill_number;
    s_previews[i].ds = igImplVulkan_AddTexture(
        env->usr->r->linear_sampler, tex->view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }
  s_built = true;
}

static void finish(tenv *env, bool save) {
  if (save) {
    int indices[SCREENSHOT_MAX_RUN_CAPTURES];
    int count = 0;
    int total = screenshot_run_count();
    for (int i = 0; i < total && i < SCREENSHOT_MAX_RUN_CAPTURES; i++)
      if (s_selected[i]) indices[count++] = i;
    if (count > 0) screenshot_run_save(env, indices, count);
  }
  unload_previews(env);
  screenshot_run_reset();
  env->usr->gdata.curr_screen = TITLE_SCREEN;
}

/* Leaving is deferred to the start of the NEXT frame: finish() destroys the
   preview textures, and this frame's draw list still references them. */
static int s_pending = 0; /* 0 none, 1 save, 2 discard */

void ui_kill_review(tenv *env) {
  tuser_data *usr = env->usr;
  game_data *gdata = &usr->gdata;
  (void)gdata;

  if (s_pending) {
    bool save = s_pending == 1;
    s_pending = 0;
    finish(env, save);
    return;
  }

  usr->r->global.bg_opacity = 0;
  usr->r->global.bd_opacity = 0;
  usr->r->global.minimap_opacity = 0;

  igPushFont(usr->imgui_data.regular_font[usr->usrs.ui_font_size],
             usr->imgui_data.regular_font[usr->usrs.ui_font_size]
                 ->LegacySize);

  crystal_draw_background(env);
  crystal_push_theme();

  ImGuiStyle *style = igGetStyle();
  int total = screenshot_run_count();
  if (total > SCREENSHOT_MAX_RUN_CAPTURES) total = SCREENSHOT_MAX_RUN_CAPTURES;
  if (!s_built) build_previews(env);

  int selected_count = 0;
  for (int i = 0; i < total; i++)
    if (s_selected[i]) selected_count++;

  /* Fixed-size centred popup. It cannot be dismissed by the X / an outside
     tap (that would silently throw the captures away) -- only by the two
     buttons at the bottom. */
  static bool popup_open = false;
  if (!popup_open) igOpenPopup_Str("Kill Review", 0);
  float footer_h = igGetFrameHeight() * 1.6f + style->ItemSpacing.y * 2.0f;
  crystal_panel_set_layout(0.74f, 0.88f, footer_h);
  crystal_panel_set_closable(false);

  if (crystal_panel_begin(env, "Kill Review", "NICE KILLS",
                          "Tap a kill to toggle it. Everything starts selected.",
                          760.0f)) {
    popup_open = true;
    ui_touch_scroll(env, 23);

    /* ---- grid: 4 across, as many rows as needed, scrolls vertically ---- */
    ImVec2 av;
    igGetContentRegionAvail(&av);
    const float gap = 14.0f;
    const float margin = 10.0f; /* room for the selection glow */
    int cols = 4;
    while (cols > 1 &&
           (av.x - margin * 2.0f - gap * (cols - 1)) / cols < 110.0f)
      cols--;
    float cw = (av.x - margin * 2.0f - gap * (cols - 1)) / cols;
    if (cw < 60.0f) cw = 60.0f;

    /* Keep the screenshots' real aspect ratio (they were being squashed
       into squares before). */
    float asp = 0.5625f;
    for (int i = 0; i < total; i++)
      if (s_previews[i].w > 0 && s_previews[i].h > 0) {
        asp = (float)s_previews[i].h / (float)s_previews[i].w;
        break;
      }
    if (asp < 0.40f) asp = 0.40f;
    if (asp > 1.30f) asp = 1.30f;
    float ch = cw * asp;
    float row_h = ch + gap;
    int rows = (total + cols - 1) / cols;
    if (rows < 1) rows = 1;

    ImVec2 org;
    igGetCursorScreenPos(&org);
    org.y += margin * 0.6f;
    ImDrawList *dl = igGetWindowDrawList();
    int hover = -1;

    for (int i = 0; i < total; i++) {
      int r = i / cols, c = i % cols;
      ImVec2 p0 = {org.x + margin + c * (cw + gap), org.y + r * row_h};
      ImVec2 p1 = {p0.x + cw, p0.y + ch};
      bool sel = s_selected[i];
      if (igIsMouseHoveringRect(p0, p1, true)) hover = i;

      if (sel) crystal_glow_rect(p0, (ImVec2){cw, ch}, 0.690f, 0.580f, 0.960f);

      if (s_previews[i].tex) {
        ImTextureRef tref = {NULL, (ImTextureID)s_previews[i].ds};
        ImDrawList_AddImageRounded(dl, tref, p0, p1, (ImVec2){0, 0},
                                   (ImVec2){1, 1}, IM_COL32(255, 255, 255, 255),
                                   10.0f, 0);
      } else {
        ImDrawList_AddRectFilled(dl, p0, p1, IM_COL32(56, 40, 90, 140), 10.0f,
                                 0);
      }
      if (!sel) /* dim the ones that won't be saved */
        ImDrawList_AddRectFilled(dl, p0, p1, igColorConvertFloat4ToU32((ImVec4){0, 0, 0, 0.55f}),
                                 10.0f, 0);
      ImDrawList_AddRect(
          dl, p0, p1,
          sel ? IM_COL32(220, 196, 255, 255) : IM_COL32(176, 148, 245, 90),
          10.0f, 0, sel ? 3.0f : 1.2f);

      /* "Kill N" chip, bottom-left */
      char lab[24];
      snprintf(lab, sizeof lab, "Kill %d", s_previews[i].kill_number);
      ImVec2 ts;
      igCalcTextSize(&ts, lab, NULL, false, -1.0f);
      ImVec2 c0 = {p0.x + 8.0f, p1.y - ts.y - 14.0f};
      ImVec2 c1 = {c0.x + ts.x + 14.0f, c0.y + ts.y + 8.0f};
      ImDrawList_AddRectFilled(dl, c0, c1,
                               igColorConvertFloat4ToU32((ImVec4){0, 0, 0, 0.62f}),
                               c1.y - c0.y, 0);
      ImDrawList_AddText_Vec2(dl, (ImVec2){c0.x + 7.0f, c0.y + 4.0f},
                              IM_COL32(255, 255, 255, 235), lab, NULL);

      /* check badge, top-right: filled + tick when selected, empty ring
         when not, so it is obvious the card is tappable */
      ImVec2 bc = {p1.x - 20.0f, p0.y + 20.0f};
      if (sel) {
        ImDrawList_AddCircleFilled(dl, bc, 13.0f, IM_COL32(150, 100, 245, 255),
                                   20);
        ImDrawList_AddCircle(dl, bc, 13.0f, IM_COL32(255, 255, 255, 235), 20,
                             2.0f);
        ImDrawList_AddLine(dl, (ImVec2){bc.x - 6.0f, bc.y + 0.5f},
                           (ImVec2){bc.x - 1.5f, bc.y + 5.0f},
                           IM_COL32(255, 255, 255, 255), 2.6f);
        ImDrawList_AddLine(dl, (ImVec2){bc.x - 1.5f, bc.y + 5.0f},
                           (ImVec2){bc.x + 6.5f, bc.y - 5.0f},
                           IM_COL32(255, 255, 255, 255), 2.6f);
      } else {
        ImDrawList_AddCircleFilled(dl, bc, 13.0f,
                                   igColorConvertFloat4ToU32((ImVec4){0, 0, 0, 0.45f}),
                                   20);
        ImDrawList_AddCircle(dl, bc, 13.0f, IM_COL32(255, 255, 255, 190), 20,
                             2.0f);
      }
    }

    /* Reserve the grid's height so the child scrolls over all of it. */
    igDummy((ImVec2){1.0f, margin + rows * row_h});

    /* Tap = press and release without dragging. Done by hand (instead of one
       button per picture) so that dragging on a picture scrolls the list
       rather than being swallowed by a button, and a scroll-drag never
       toggles a picture by accident. */
    static int press_idx = -1;
    static bool press_dragged = false;
    if (igIsWindowHovered(0) && igIsMouseClicked_Bool(0, false)) {
      press_idx = hover;
      press_dragged = false;
    }
    if (press_idx >= 0 && igIsMouseDragging(0, 12.0f)) press_dragged = true;
    if (igIsMouseReleased_Nil(0)) {
      if (press_idx >= 0 && !press_dragged)
        s_selected[press_idx] = !s_selected[press_idx];
      press_idx = -1;
    }

    crystal_panel_end_body();

    /* ---- pinned footer ---- */
    {
      float fb_h = igGetFrameHeight() * 1.6f;
      ImVec2 fav;
      igGetContentRegionAvail(&fav);
      float btn_w = (fav.x - style->ItemSpacing.x) * 0.5f;

      igBeginDisabled(selected_count == 0);
      igPushStyleColor_Vec4(ImGuiCol_Button,
                            (ImVec4){0.510f, 0.294f, 0.910f, 1.0f});
      igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                            (ImVec4){0.569f, 0.353f, 0.960f, 1.0f});
      igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
                            (ImVec4){0.450f, 0.243f, 0.850f, 1.0f});
      char save_label[40];
      snprintf(save_label, sizeof(save_label), "Save Selected (%d)",
               selected_count);
      if (igButton(save_label, (ImVec2){btn_w, fb_h})) s_pending = 1;
      igPopStyleColor(3);
      igEndDisabled();
      crystal_sheen();

      igSameLine(0, style->ItemSpacing.x);
      if (igButton("Discard All", (ImVec2){btn_w, fb_h})) s_pending = 2;
      crystal_sheen();
    }

    crystal_panel_end();
  } else {
    popup_open = false; /* re-open next frame: this screen has no "close" */
  }

  crystal_pop_theme();
  igPopFont();
}

void ui_kill_review_destroy(tenv *env) {
  unload_previews(env);
}
