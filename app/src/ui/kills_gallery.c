#include "kills_gallery.h"

#include <stdio.h>
#include <string.h>

#include "../user.h"
#include "crystal_theme.h"
#include "../rendering/texture.h"

#ifdef ANDROID
#include <dirent.h>
#include "../android_path.h"
#endif

#ifndef IM_COL32
#define IM_COL32(R, G, B, A) \
  (((ImU32)(A) << 24) | ((ImU32)(B) << 16) | ((ImU32)(G) << 8) | ((ImU32)(R)))
#endif

/* Simple in-app viewer for the screenshots screenshot.c saves on every
   kill. Deliberately Android-only (like the capture itself, and like
   android_path.h that it depends on for the directory) -- on other
   platforms this just shows a short explanation instead. */

#define MAX_GALLERY_ITEMS 300
#define MAX_LOADED_THUMBS 40

typedef struct {
  char filename[128];
  texture *tex;
  VkDescriptorSet ds;
} gallery_item;

static gallery_item s_items[MAX_GALLERY_ITEMS];
static int s_item_count = 0;
static bool s_scanned = false;
static int s_viewing_index = -1;
static bool s_confirm_delete = false;

void ui_kills_gallery_init(tenv *env) {
  (void)env;
  s_item_count = 0;
  s_scanned = false;
  s_viewing_index = -1;
  s_confirm_delete = false;
}

#ifdef ANDROID

static void scan_kills_dir(void) {
  s_item_count = 0;
  char dir_path[600];
  android_build_kills_dir(dir_path, sizeof(dir_path));
  DIR *d = opendir(dir_path);
  if (!d) return;

  struct dirent *ent;
  while ((ent = readdir(d)) != NULL && s_item_count < MAX_GALLERY_ITEMS) {
    size_t len = strlen(ent->d_name);
    if (len < 5) continue;
    if (strcmp(ent->d_name + len - 4, ".png") != 0) continue;
    strncpy(s_items[s_item_count].filename, ent->d_name,
           sizeof(s_items[0].filename) - 1);
    s_items[s_item_count].filename[sizeof(s_items[0].filename) - 1] = 0;
    s_items[s_item_count].tex = NULL;
    s_items[s_item_count].ds = VK_NULL_HANDLE;
    s_item_count++;
  }
  closedir(d);

  /* Filenames are "kill_<10-digit-unix-ts>_<n>.png" -- lexicographic
     order matches chronological order until the year 2286, so a plain
     string sort (newest first) is all that's needed. */
  for (int a = 0; a < s_item_count; a++)
    for (int b = a + 1; b < s_item_count; b++)
      if (strcmp(s_items[a].filename, s_items[b].filename) < 0) {
        gallery_item tmp = s_items[a];
        s_items[a] = s_items[b];
        s_items[b] = tmp;
      }
}

static void item_filepath(const gallery_item *it, char *out, int out_size) {
  char dir_path[600];
  android_build_kills_dir(dir_path, sizeof(dir_path));
  snprintf(out, (size_t)out_size, "%s/%s", dir_path, it->filename);
}

static void load_item(tenv *env, gallery_item *it) {
  if (it->tex) return;
  int loaded_count = 0;
  for (int j = 0; j < s_item_count; j++)
    if (s_items[j].tex) loaded_count++;
  if (loaded_count >= MAX_LOADED_THUMBS) return;

  char path[700];
  item_filepath(it, path, sizeof(path));
  it->tex = create_mipmap_texture_from_filepath(env->ctx, path);
  if (it->tex) {
    it->ds = igImplVulkan_AddTexture(env->usr->r->linear_sampler,
                                     it->tex->view,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }
}

static void unload_item(tenv *env, gallery_item *it) {
  if (!it->tex) return;
  tcontext_wait_idle(env->ctx);
  igImplVulkan_RemoveTexture(it->ds);
  destroy_texture(env->ctx, it->tex);
  it->tex = NULL;
  it->ds = VK_NULL_HANDLE;
}

static void delete_item(tenv *env, int index) {
  if (index < 0 || index >= s_item_count) return;
  gallery_item *it = &s_items[index];
  char path[700];
  item_filepath(it, path, sizeof(path));
  unload_item(env, it);
  remove(path);
  for (int i = index; i < s_item_count - 1; i++) s_items[i] = s_items[i + 1];
  s_item_count--;
}

static void draw_viewer(tenv *env, float sw, float sh) {
  ImGuiStyle *style = igGetStyle();
  gallery_item *it = &s_items[s_viewing_index];
  load_item(env, it);

  if (it->tex) {
    float avail_w = sw * 0.82f;
    float avail_h = sh * 0.6f;
    float aspect = (float)it->tex->size[0] / (float)it->tex->size[1];
    float dw = avail_w, dh = dw / aspect;
    if (dh > avail_h) {
      dh = avail_h;
      dw = dh * aspect;
    }
    igSetCursorPos((ImVec2){sw * 0.5f - dw * 0.5f, sh * 0.16f});
    ImTextureRef tref = {NULL, (ImTextureID)it->ds};
    igImage(tref, (ImVec2){dw, dh}, (ImVec2){0, 0}, (ImVec2){1, 1});
    igSetCursorPosY(sh * 0.16f + dh + 24.0f);
  } else {
    igSetCursorPos((ImVec2){sw * 0.5f - 90.0f, sh * 0.4f});
    igText("Couldn't load this image.");
    igSetCursorPosY(sh * 0.4f + 40.0f);
  }

  float btn_w = 180.0f;
  float btn_h = igGetFrameHeight() * 1.5f;
  float row_w = btn_w * 2.0f + 20.0f;
  igSetCursorPosX(sw * 0.5f - row_w * 0.5f);

  if (igButton("Close", (ImVec2){btn_w, btn_h})) {
    s_viewing_index = -1;
    s_confirm_delete = false;
  }
  crystal_sheen();
  igSameLine(0, 20.0f);

  igPushStyleColor_Vec4(ImGuiCol_Button,
                        (ImVec4){0.647f, 0.176f, 0.176f, 1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                        (ImVec4){0.75f, 0.22f, 0.22f, 1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
                        (ImVec4){0.55f, 0.14f, 0.14f, 1.0f});
  const char *del_label = s_confirm_delete ? "Confirm delete?" : "Delete";
  if (igButton(del_label, (ImVec2){btn_w, btn_h})) {
    if (s_confirm_delete) {
      int idx = s_viewing_index;
      s_viewing_index = -1;
      s_confirm_delete = false;
      delete_item(env, idx);
    } else {
      s_confirm_delete = true;
    }
  }
  crystal_sheen();
  igPopStyleColor(3);

  igSetCursorPosX(sw * 0.5f - row_w * 0.5f);
  igTextWrapped(
      "Only removes it from this gallery -- if it was also saved to your "
      "phone's Gallery app, delete it there too.");
  (void)style;
}

static void draw_grid(tenv *env, float sw, float sh) {
  ImGuiStyle *style = igGetStyle();

  int cols = 4;
  float pad = 12.0f;
  float cell = (sw - style->WindowPadding.x * 2.0f - pad * (cols - 1)) / cols;
  if (cell > 220.0f) cell = 220.0f;
  float grid_w = cell * cols + pad * (cols - 1);
  float start_x = sw * 0.5f - grid_w * 0.5f;
  float start_y = 76.0f;

  igSetCursorPos((ImVec2){start_x, start_y});
  igBeginChild_Str("##kills_grid", (ImVec2){grid_w, sh - start_y - 96.0f},
                   ImGuiChildFlags_None, ImGuiWindowFlags_None);

  if (s_item_count == 0) {
    igTextWrapped("No kill screenshots yet -- get a kill in a match and "
                  "one shows up here automatically.");
  }

  for (int i = 0; i < s_item_count; i++) {
    int col = i % cols;
    if (col != 0) igSameLine(0, pad);

    ImVec2 cell_pos;
    igGetCursorScreenPos(&cell_pos);
    bool visible = igIsRectVisible_Vec2(
        cell_pos, (ImVec2){cell_pos.x + cell, cell_pos.y + cell});

    gallery_item *it = &s_items[i];
    if (visible) load_item(env, it);

    char btn_id[32];
    snprintf(btn_id, sizeof(btn_id), "##kill_item_%d", i);
    if (it->tex) {
      ImTextureRef tref = {NULL, (ImTextureID)it->ds};
      if (igImageButton(btn_id, tref, (ImVec2){cell, cell}, (ImVec2){0, 0},
                        (ImVec2){1, 1}, (ImVec4){0, 0, 0, 0},
                        (ImVec4){1, 1, 1, 1})) {
        s_viewing_index = i;
        s_confirm_delete = false;
      }
    } else {
      igPushStyleColor_Vec4(ImGuiCol_Button,
                            (ImVec4){0.220f, 0.157f, 0.353f, 0.55f});
      igButton(btn_id, (ImVec2){cell, cell});
      igPopStyleColor(1);
    }
  }

  igEndChild();
}

#endif /* ANDROID */

void ui_kills_gallery(tenv *env) {
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

  const char *title = "Kill Shots";
  ImVec2 title_size;
  igCalcTextSize(&title_size, title, NULL, false, -1.0f);
  ImDrawList_AddText_Vec2(bg, (ImVec2){sw * 0.5f - title_size.x * 0.5f, 18.0f},
                          IM_COL32(255, 255, 255, 235), title, NULL);

#ifdef ANDROID
  if (!s_scanned) {
    scan_kills_dir();
    s_scanned = true;
  }
  if (s_viewing_index >= 0 && s_viewing_index < s_item_count) {
    draw_viewer(env, sw, sh);
  } else {
    draw_grid(env, sw, sh);
  }
#else
  igSetCursorPos((ImVec2){sw * 0.5f - 160.0f, sh * 0.45f});
  igTextWrapped(
      "Kill screenshots are an Android-only feature and aren't available "
      "on this build.");
#endif

  float btn_w = 220.0f;
  float btn_h = igGetFrameHeight() * 1.4f;
  igSetCursorPos(
      (ImVec2){sw * 0.5f - btn_w * 0.5f, sh - style->WindowPadding.y - btn_h});
  if (igButton("Back", (ImVec2){btn_w, btn_h})) {
    gdata->curr_screen = TITLE_SCREEN;
  }
  crystal_sheen();

  crystal_pop_theme();
  igPopFont();
}

void ui_kills_gallery_destroy(tenv *env) {
#ifdef ANDROID
  if (env && env->ctx) {
    bool any_loaded = false;
    for (int i = 0; i < s_item_count; i++)
      if (s_items[i].tex) {
        any_loaded = true;
        break;
      }
    if (any_loaded) {
      tcontext_wait_idle(env->ctx);
      for (int i = 0; i < s_item_count; i++) {
        if (s_items[i].tex) {
          igImplVulkan_RemoveTexture(s_items[i].ds);
          destroy_texture(env->ctx, s_items[i].tex);
        }
      }
    }
  }
#else
  (void)env;
#endif
  s_item_count = 0;
  s_scanned = false;
  s_viewing_index = -1;
}
