#include "kill_review.h"

#include <stdio.h>

#include "../user.h"
#include "crystal_theme.h"
#include "../rendering/texture.h"
#include "../game/screenshot.h"

#ifndef IM_COL32
#define IM_COL32(R, G, B, A) \
  (((ImU32)(A) << 24) | ((ImU32)(B) << 16) | ((ImU32)(G) << 8) | ((ImU32)(R)))
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
} preview_item;

static preview_item s_previews[SCREENSHOT_MAX_RUN_CAPTURES];
static bool s_selected[SCREENSHOT_MAX_RUN_CAPTURES];
static bool s_built = false;

void ui_kill_review_init(tenv *env) {
  (void)env;
  for (int i = 0; i < SCREENSHOT_MAX_RUN_CAPTURES; i++) {
    s_previews[i].tex = NULL;
    s_previews[i].ds = VK_NULL_HANDLE;
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

void ui_kill_review(tenv *env) {
  tuser_data *usr = env->usr;
  game_data *gdata = &usr->gdata;
  tcontext *ctx = env->ctx;

  usr->r->global.bg_opacity = 0;
  usr->r->global.bd_opacity = 0;
  usr->r->global.minimap_opacity = 0;

  igPushFont(usr->imgui_data.regular_font[usr->usrs.ui_font_size],
             usr->imgui_data.regular_font[usr->usrs.ui_font_size]
                 ->LegacySize);

  crystal_draw_background(env);
  crystal_push_theme();

  float sw = ctx->size[0];
  float sh = ctx->size[1];
  ImGuiStyle *style = igGetStyle();
  ImDrawList *bg = igGetWindowDrawList();

  int total = screenshot_run_count();
  if (!s_built) build_previews(env);

  const char *title = "Nice kills! Pick which to save";
  ImVec2 title_size;
  igCalcTextSize(&title_size, title, NULL, false, -1.0f);
  ImDrawList_AddText_Vec2(bg, (ImVec2){sw * 0.5f - title_size.x * 0.5f, 18.0f},
                          IM_COL32(255, 255, 255, 235), title, NULL);
  const char *hint = "Tap a kill to toggle it. Everything starts selected.";
  ImVec2 hint_size;
  igCalcTextSize(&hint_size, hint, NULL, false, -1.0f);
  ImDrawList_AddText_Vec2(
      bg, (ImVec2){sw * 0.5f - hint_size.x * 0.5f, 46.0f},
      IM_COL32(170, 170, 175, 220), hint, NULL);

  int cols = total < 4 ? (total < 1 ? 1 : total) : 4;
  float pad = 14.0f;
  float cell = (sw - style->WindowPadding.x * 2.0f - pad * (cols - 1)) / cols;
  if (cell > 260.0f) cell = 260.0f;
  float grid_w = cell * cols + pad * (cols - 1);
  float start_x = sw * 0.5f - grid_w * 0.5f;
  float start_y = 84.0f;

  igSetCursorPos((ImVec2){start_x, start_y});
  igBeginChild_Str("##kill_review_grid",
                   (ImVec2){grid_w, sh - start_y - 120.0f},
                   ImGuiChildFlags_None, ImGuiWindowFlags_None);

  int selected_count = 0;
  for (int i = 0; i < total && i < SCREENSHOT_MAX_RUN_CAPTURES; i++) {
    if (s_selected[i]) selected_count++;

    int col = i % cols;
    if (col != 0) igSameLine(0, pad);

    char btn_id[32];
    snprintf(btn_id, sizeof(btn_id), "##kill_review_%d", i);

    ImVec2 cell_pos;
    igGetCursorScreenPos(&cell_pos);

    bool sel = s_selected[i];
    if (sel) {
      ImDrawList *cdl = igGetWindowDrawList();
      ImDrawList_AddRectFilled(
          cdl, (ImVec2){cell_pos.x - 4, cell_pos.y - 4},
          (ImVec2){cell_pos.x + cell + 4, cell_pos.y + cell + 4},
          IM_COL32(140, 100, 245, 200), 10.0f, ImDrawFlags_None);
    }

    if (s_previews[i].tex) {
      ImTextureRef tref = {NULL, (ImTextureID)s_previews[i].ds};
      if (igImageButton(btn_id, tref, (ImVec2){cell, cell}, (ImVec2){0, 0},
                        (ImVec2){1, 1}, (ImVec4){0, 0, 0, 0},
                        (ImVec4){1, 1, 1, 1})) {
        s_selected[i] = !s_selected[i];
      }
    } else {
      igPushStyleColor_Vec4(ImGuiCol_Button,
                            (ImVec4){0.220f, 0.157f, 0.353f, 0.55f});
      if (igButton(btn_id, (ImVec2){cell, cell})) s_selected[i] = !s_selected[i];
      igPopStyleColor(1);
    }

    if (sel) {
      ImVec2 chk_pos = {cell_pos.x + cell - 26.0f, cell_pos.y + 6.0f};
      ImDrawList *cdl2 = igGetWindowDrawList();
      ImDrawList_AddCircleFilled(cdl2,
                                 (ImVec2){chk_pos.x + 10.0f, chk_pos.y + 10.0f},
                                 12.0f, IM_COL32(120, 80, 230, 255), 20);
      ImDrawList_AddCircle(cdl2,
                           (ImVec2){chk_pos.x + 10.0f, chk_pos.y + 10.0f},
                           12.0f, IM_COL32(255, 255, 255, 230), 20, 2.0f);
    }
  }

  igEndChild();

  float btn_w = 220.0f;
  float btn_h = igGetFrameHeight() * 1.5f;
  float row_w = btn_w * 2.0f + 24.0f;
  igSetCursorPos(
      (ImVec2){sw * 0.5f - row_w * 0.5f, sh - style->WindowPadding.y - btn_h});

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
  if (igButton(save_label, (ImVec2){btn_w, btn_h})) {
    finish(env, true);
  }
  igPopStyleColor(3);
  igEndDisabled();
  crystal_sheen();

  igSameLine(0, 24.0f);
  if (igButton("Discard All", (ImVec2){btn_w, btn_h})) {
    finish(env, false);
  }
  crystal_sheen();

  crystal_pop_theme();
  igPopFont();
  (void)gdata;
}

void ui_kill_review_destroy(tenv *env) {
  unload_previews(env);
}
