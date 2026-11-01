#include "kills_gallery.h"

#include <stdio.h>
#include <string.h>

#include "../user.h"
#include "crystal_theme.h"
#include "settings.h"
#include "../game/screenshot.h"
#include "../rendering/texture.h"

#ifdef ANDROID
#include <dirent.h>
#include "../android_path.h"
#include "../android_jni.h"
#endif

#ifndef IM_COL32
#define IM_COL32(R, G, B, A) \
  crystal_tint_u32((((ImU32)(A) << 24) | ((ImU32)(B) << 16) | ((ImU32)(G) << 8) | ((ImU32)(R))))
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
  bool saved_to_gallery;
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
    s_items[s_item_count].saved_to_gallery = false;
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

/* Deletions are deferred to the start of the next frame: the viewer has just
   drawn this item's texture, and destroying it mid-frame would leave this
   frame's draw list pointing at a dead descriptor. */
static int s_pending_delete = -1;

/* The picture viewer: shown INSIDE the same popup, in place of the grid. */
static void draw_viewer_body(tenv *env) {
  gallery_item *it = &s_items[s_viewing_index];
  load_item(env, it);

  ImVec2 av;
  igGetContentRegionAvail(&av);
  float note_h = igGetTextLineHeight() * 3.2f;

  if (it->tex) {
    float aspect = (float)it->tex->size[0] / (float)it->tex->size[1];
    float max_w = av.x - 8.0f;
    float max_h = av.y - note_h;
    if (max_h < 80.0f) max_h = 80.0f;
    float dw = max_w, dh = dw / aspect;
    if (dh > max_h) {
      dh = max_h;
      dw = dh * aspect;
    }
    ImVec2 org;
    igGetCursorScreenPos(&org);
    ImVec2 p0 = {org.x + (av.x - dw) * 0.5f, org.y + 4.0f};
    ImVec2 p1 = {p0.x + dw, p0.y + dh};
    ImDrawList *dl = igGetWindowDrawList();
    ImTextureRef tref = {NULL, (ImTextureID)it->ds};
    ImDrawList_AddImageRounded(dl, tref, p0, p1, (ImVec2){0, 0},
                               (ImVec2){1, 1}, IM_COL32(255, 255, 255, 255),
                               10.0f, 0);
    ImDrawList_AddRect(dl, p0, p1, IM_COL32(176, 148, 245, 140), 10.0f, 0, 1.5f);
    igDummy((ImVec2){av.x, dh + 12.0f});
  } else {
    igTextWrapped("Couldn't load this image.");
  }

  igTextDisabled(
      "Kills are only saved in the app. Tap Save to also add this one to "
      "your phone's Gallery.");
}

/* The grid: 4 across, as many rows as needed, scrolls vertically. */
static void draw_grid_body(tenv *env) {
  if (s_item_count == 0) {
    igTextWrapped("No kill screenshots yet. Turn SCREENSHOT on (the button at "
                  "the bottom), get kills in a match, then choose which ones "
                  "to save from the Kill Shots button on the death popup.");
    return;
  }

  ImGuiStyle *style = igGetStyle();
  ImVec2 av;
  igGetContentRegionAvail(&av);
  const float gap = 14.0f;
  const float margin = 10.0f;
  int cols = 4;
  while (cols > 1 && (av.x - margin * 2.0f - gap * (cols - 1)) / cols < 110.0f)
    cols--;
  float cw = (av.x - margin * 2.0f - gap * (cols - 1)) / cols;
  if (cw < 60.0f) cw = 60.0f;

  /* Real aspect ratio of the pictures (they were squashed into squares). */
  float asp = 0.5625f;
  for (int i = 0; i < s_item_count; i++)
    if (s_items[i].tex && s_items[i].tex->size[0] > 0 &&
        s_items[i].tex->size[1] > 0) {
      asp = (float)s_items[i].tex->size[1] / (float)s_items[i].tex->size[0];
      break;
    }
  if (asp < 0.40f) asp = 0.40f;
  if (asp > 1.30f) asp = 1.30f;
  float ch = cw * asp;
  float row_h = ch + gap;
  int rows = (s_item_count + cols - 1) / cols;

  ImVec2 org;
  igGetCursorScreenPos(&org);
  org.y += margin * 0.6f;
  ImDrawList *dl = igGetWindowDrawList();
  int hover = -1;

  /* Keep memory bounded: when close to the thumbnail limit, drop a couple of
     pictures that are scrolled out of view. */
  int loaded_count = 0;
  for (int j = 0; j < s_item_count; j++)
    if (s_items[j].tex) loaded_count++;
  int to_unload = loaded_count >= MAX_LOADED_THUMBS - 4 ? 2 : 0;

  for (int i = 0; i < s_item_count; i++) {
    int r = i / cols, c = i % cols;
    ImVec2 p0 = {org.x + margin + c * (cw + gap), org.y + r * row_h};
    ImVec2 p1 = {p0.x + cw, p0.y + ch};
    gallery_item *it = &s_items[i];
    bool visible = igIsRectVisible_Vec2(p0, p1);
    if (visible) {
      load_item(env, it);
      if (igIsMouseHoveringRect(p0, p1, true)) hover = i;
    } else if (to_unload > 0 && it->tex) {
      unload_item(env, it);
      to_unload--;
    }
    if (!visible) continue;

    if (it->tex) {
      ImTextureRef tref = {NULL, (ImTextureID)it->ds};
      ImDrawList_AddImageRounded(dl, tref, p0, p1, (ImVec2){0, 0},
                                 (ImVec2){1, 1}, IM_COL32(255, 255, 255, 255),
                                 10.0f, 0);
    } else {
      ImDrawList_AddRectFilled(dl, p0, p1, IM_COL32(56, 40, 90, 140), 10.0f, 0);
    }
    bool hot = hover == i;
    ImDrawList_AddRect(dl, p0, p1,
                       hot ? IM_COL32(220, 196, 255, 255)
                           : IM_COL32(176, 148, 245, 120),
                       10.0f, 0, hot ? 2.5f : 1.3f);
  }
  igDummy((ImVec2){1.0f, margin + rows * row_h});

  /* Tap = press + release without dragging, so a scroll-drag never opens a
     picture and dragging on a picture scrolls the list. */
  static int press_idx = -1;
  static bool press_dragged = false;
  if (igIsWindowHovered(0) && igIsMouseClicked_Bool(0, false)) {
    press_idx = hover;
    press_dragged = false;
  }
  if (press_idx >= 0 && igIsMouseDragging(0, 12.0f)) press_dragged = true;
  if (igIsMouseReleased_Nil(0)) {
    if (press_idx >= 0 && !press_dragged) {
      s_viewing_index = press_idx;
      s_confirm_delete = false;
    }
    press_idx = -1;
  }
  (void)style;
}

#endif /* ANDROID */

void ui_kills_gallery(tenv *env) {
  tuser_data *usr = env->usr;
  game_data *gdata = &usr->gdata;

#ifdef ANDROID
  if (s_pending_delete >= 0) {
    int idx = s_pending_delete;
    s_pending_delete = -1;
    delete_item(env, idx);
  }
#endif

  usr->r->global.bg_opacity = 0;
  usr->r->global.bd_opacity = 0;
  usr->r->global.minimap_opacity = 0;

  igPushFont(usr->imgui_data.regular_font[usr->usrs.ui_font_size],
             usr->imgui_data.regular_font[usr->usrs.ui_font_size]
                 ->LegacySize);

  crystal_draw_background(env);
  crystal_push_theme();

  ImGuiStyle *style = igGetStyle();

  /* Centred, fixed-size popup. The X, tapping outside and Back all return to
     the title screen. */
  static bool popup_open = false;
  if (!popup_open) igOpenPopup_Str("Kill Shots", 0);
  float footer_h = igGetFrameHeight() * 1.6f + style->ItemSpacing.y * 2.0f;
  crystal_panel_set_layout(0.74f, 0.88f, footer_h);

#ifdef ANDROID
  if (!s_scanned) {
    scan_kills_dir();
    s_scanned = true;
  }
  bool viewing = s_viewing_index >= 0 && s_viewing_index < s_item_count;
  const char *subtitle = viewing ? "Viewing a kill shot"
                                 : "Tap a picture to open it";
#else
  bool viewing = false;
  const char *subtitle = "Android only";
#endif

  if (crystal_panel_begin(env, "Kill Shots", "KILL SHOTS", subtitle, 760.0f)) {
    popup_open = true;
    float fb_h = igGetFrameHeight() * 1.6f;

#ifdef ANDROID
    if (viewing) {
      draw_viewer_body(env);
    } else {
      ui_touch_scroll(env, 24);
      draw_grid_body(env);
    }
    crystal_panel_end_body();

    ImVec2 fav;
    igGetContentRegionAvail(&fav);
    float sp = style->ItemSpacing.x;

    if (viewing) {
      gallery_item *it = &s_items[s_viewing_index];
      float btn_w = (fav.x - sp * 2.0f) / 3.0f;
      if (igButton("Close", (ImVec2){btn_w, fb_h})) {
        s_viewing_index = -1;
        s_confirm_delete = false;
      }
      crystal_sheen();
      igSameLine(0, sp);

      bool already_saved = it->saved_to_gallery;
      igBeginDisabled(already_saved);
      igPushStyleColor_Vec4(ImGuiCol_Button,
                            (ImVec4){0.16f, 0.55f, 0.30f, 1.0f});
      igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                            (ImVec4){0.20f, 0.65f, 0.36f, 1.0f});
      igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
                            (ImVec4){0.13f, 0.45f, 0.25f, 1.0f});
      if (igButton(already_saved ? "Saved" : "\uea1a Save",
                   (ImVec2){btn_w, fb_h})) {
        android_jni_save_image_to_gallery(it->filename);
        it->saved_to_gallery = true;
      }
      igPopStyleColor(3);
      igEndDisabled();
      crystal_sheen();
      igSameLine(0, sp);

      igPushStyleColor_Vec4(ImGuiCol_Button,
                            (ImVec4){0.647f, 0.176f, 0.176f, 1.0f});
      igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                            (ImVec4){0.75f, 0.22f, 0.22f, 1.0f});
      igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
                            (ImVec4){0.55f, 0.14f, 0.14f, 1.0f});
      if (igButton(s_confirm_delete ? "Confirm delete?" : "Delete",
                   (ImVec2){btn_w, fb_h})) {
        if (s_confirm_delete) {
          s_pending_delete = s_viewing_index;
          s_viewing_index = -1;
          s_confirm_delete = false;
        } else {
          s_confirm_delete = true;
        }
      }
      crystal_sheen();
      igPopStyleColor(3);
    } else {
      /* SCREENSHOT ON/OFF -- whether kills get captured during a run at all.
         OFF by default (see screenshot.h): capturing costs a brief hitch, so
         it is opt-in. Constant ### id so the button keeps its identity when
         the label flips between ON and OFF. */
      float btn_w = (fav.x - sp) * 0.5f;
      bool ss_on = screenshot_capture_enabled();
      if (ss_on) {
        igPushStyleColor_Vec4(ImGuiCol_Button,
                              (ImVec4){0.16f, 0.55f, 0.30f, 1.0f});
        igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                              (ImVec4){0.20f, 0.65f, 0.36f, 1.0f});
        igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
                              (ImVec4){0.13f, 0.45f, 0.25f, 1.0f});
      }
      if (igButton(ss_on ? "SCREENSHOT: ON###kill_ss_toggle"
                         : "SCREENSHOT: OFF###kill_ss_toggle",
                   (ImVec2){btn_w, fb_h})) {
        screenshot_set_capture_enabled(!ss_on);
      }
      if (ss_on) igPopStyleColor(3);
      crystal_sheen();
      igSameLine(0, sp);
      if (igButton("Back", (ImVec2){btn_w, fb_h})) {
        gdata->curr_screen = TITLE_SCREEN;
        popup_open = false;
        igCloseCurrentPopup();
      }
      crystal_sheen();
    }
#else
    igTextWrapped(
        "Kill screenshots are an Android-only feature and aren't available "
        "on this build.");
    crystal_panel_end_body();
    ImVec2 fav;
    igGetContentRegionAvail(&fav);
    if (igButton("Back", (ImVec2){fav.x, fb_h})) {
      gdata->curr_screen = TITLE_SCREEN;
      popup_open = false;
      igCloseCurrentPopup();
    }
    crystal_sheen();
    (void)viewing;
#endif

    crystal_panel_end();
  } else if (popup_open) {
    /* closed with the X / by tapping outside = Back */
    popup_open = false;
    gdata->curr_screen = TITLE_SCREEN;
  }

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
