#include "clips_gallery.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../user.h"
#include "crystal_theme.h"

#ifdef ANDROID
#include <dirent.h>
#include "../android_path.h"
#include "../android_jni.h"
#include "../game/recorder.h"
#endif

#ifndef IM_COL32
#define IM_COL32(R, G, B, A) \
  (((ImU32)(A) << 24) | ((ImU32)(B) << 16) | ((ImU32)(G) << 8) | ((ImU32)(R)))
#endif

/* In-app list for the clips recorder.c/GameActivity.kt save under the
   app-private "Movies/clips" directory. List-based rather than a
   thumbnail grid like Kill Shots: native code has no way to decode a
   video frame for a preview without a lot of extra machinery, so each
   row is just a timestamp with Play/Save/Delete. Play hands off to the
   phone's own video player (see android_jni_play_clip());  this screen
   never plays video itself. */

#define MAX_CLIP_ITEMS 200

typedef struct {
  char filename[128];
  bool saved_to_gallery;
} clip_item;

static clip_item s_clips[MAX_CLIP_ITEMS];
static int s_clip_count = 0;
static bool s_scanned = false;
static int s_pending_delete_index = -1;

void ui_clips_gallery_init(tenv *env) {
  (void)env;
  s_clip_count = 0;
  s_scanned = false;
  s_pending_delete_index = -1;
}

#ifdef ANDROID

static void scan_clips_dir(void) {
  s_clip_count = 0;
  char dir_path[600];
  android_build_clips_dir(dir_path, sizeof(dir_path));
  DIR *d = opendir(dir_path);
  if (!d) return;

  struct dirent *ent;
  while ((ent = readdir(d)) != NULL && s_clip_count < MAX_CLIP_ITEMS) {
    size_t len = strlen(ent->d_name);
    if (len < 5) continue;
    if (strcmp(ent->d_name + len - 4, ".mp4") != 0) continue;
    strncpy(s_clips[s_clip_count].filename, ent->d_name,
           sizeof(s_clips[0].filename) - 1);
    s_clips[s_clip_count].filename[sizeof(s_clips[0].filename) - 1] = 0;
    s_clips[s_clip_count].saved_to_gallery = false;
    s_clip_count++;
  }
  closedir(d);

  /* Filenames are "clip_<10-digit-unix-ts>.mp4" -- lexicographic order
     matches chronological order until the year 2286. */
  for (int a = 0; a < s_clip_count; a++)
    for (int b = a + 1; b < s_clip_count; b++)
      if (strcmp(s_clips[a].filename, s_clips[b].filename) < 0) {
        clip_item tmp = s_clips[a];
        s_clips[a] = s_clips[b];
        s_clips[b] = tmp;
      }
}

static void delete_clip(int index) {
  if (index < 0 || index >= s_clip_count) return;
  char dir_path[600];
  android_build_clips_dir(dir_path, sizeof(dir_path));
  char path[700];
  snprintf(path, sizeof(path), "%s/%s", dir_path, s_clips[index].filename);
  remove(path);
  for (int i = index; i < s_clip_count - 1; i++) s_clips[i] = s_clips[i + 1];
  s_clip_count--;
}

static void format_clip_label(const char *filename, char *out, int out_size) {
  long ts = 0;
  sscanf(filename, "clip_%ld.mp4", &ts);
  if (ts > 0) {
    time_t t = (time_t)ts;
    struct tm tm_buf;
    localtime_r(&t, &tm_buf);
    char buf[64];
    if (strftime(buf, sizeof(buf), "%b %d, %I:%M %p", &tm_buf) > 0) {
      snprintf(out, (size_t)out_size, "%s", buf);
      return;
    }
  }
  snprintf(out, (size_t)out_size, "%s", filename);
}

static void draw_list(tenv *env, float sw, float sh) {
  (void)env;
  ImGuiStyle *style = igGetStyle();

  float list_w = sw * 0.8f;
  if (list_w > 760.0f) list_w = 760.0f;
  float start_x = sw * 0.5f - list_w * 0.5f;
  float start_y = 76.0f;

  igSetCursorPos((ImVec2){start_x, start_y});
  igBeginChild_Str("##clips_list", (ImVec2){list_w, sh - start_y - 96.0f},
                   ImGuiChildFlags_None, ImGuiWindowFlags_None);

  if (s_clip_count == 0) {
    igTextWrapped(
        "No clips yet. Tap the record button in-game, or use Start "
        "Recording in Controls.");
  }

  float row_h = igGetFrameHeight() * 1.6f;
  float btn_w = 90.0f;
  float gap = 10.0f;
  float buttons_w = btn_w * 3.0f + gap * 2.0f;

  int deleted_this_frame = -1;
  for (int i = 0; i < s_clip_count; i++) {
    igPushID_Int(i);

    char label[64];
    format_clip_label(s_clips[i].filename, label, sizeof(label));

    igAlignTextToFramePadding();
    igText("%s", label);
    igSameLine(list_w - buttons_w, -1.0f);

    if (igButton("Play", (ImVec2){btn_w, row_h})) {
      android_jni_play_clip(s_clips[i].filename);
    }
    igSameLine(0, gap);

    bool saved = s_clips[i].saved_to_gallery;
    igBeginDisabled(saved);
    igPushStyleColor_Vec4(ImGuiCol_Button,
                          saved ? (ImVec4){0.30f, 0.30f, 0.32f, 1.0f}
                                : (ImVec4){0.16f, 0.55f, 0.30f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                          (ImVec4){0.20f, 0.65f, 0.36f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
                          (ImVec4){0.13f, 0.45f, 0.25f, 1.0f});
    if (igButton(saved ? "Saved" : "Save", (ImVec2){btn_w, row_h})) {
      android_jni_save_clip_to_gallery(s_clips[i].filename);
      s_clips[i].saved_to_gallery = true;
    }
    igPopStyleColor(3);
    igEndDisabled();
    igSameLine(0, gap);

    bool confirming = (s_pending_delete_index == i);
    igPushStyleColor_Vec4(ImGuiCol_Button,
                          (ImVec4){0.647f, 0.176f, 0.176f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                          (ImVec4){0.75f, 0.22f, 0.22f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
                          (ImVec4){0.55f, 0.14f, 0.14f, 1.0f});
    if (igButton(confirming ? "Sure?" : "Delete", (ImVec2){btn_w, row_h})) {
      if (confirming) {
        deleted_this_frame = i;
        s_pending_delete_index = -1;
      } else {
        s_pending_delete_index = i;
      }
    }
    igPopStyleColor(3);

    igSpacing();
    igSeparator();
    igSpacing();
    igPopID();

    if (deleted_this_frame >= 0) break;
  }

  if (deleted_this_frame >= 0) delete_clip(deleted_this_frame);

  igEndChild();
  (void)style;
}

#endif /* ANDROID */

void ui_clips_gallery(tenv *env) {
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

  const char *title = "Clips";
  ImVec2 title_size;
  igCalcTextSize(&title_size, title, NULL, false, -1.0f);
  ImDrawList_AddText_Vec2(bg, (ImVec2){sw * 0.5f - title_size.x * 0.5f, 18.0f},
                          IM_COL32(255, 255, 255, 235), title, NULL);

#ifdef ANDROID
  {
    bool recording_now = recorder_is_recording();
    float tb_w = 220.0f;
    float tb_h = igGetFrameHeight() * 1.25f;
    igSetCursorPos(
        (ImVec2){sw - tb_w - style->WindowPadding.x - 12.0f, 12.0f});
    if (recording_now) {
      igPushStyleColor_Vec4(ImGuiCol_Button,
                            (ImVec4){0.647f, 0.176f, 0.176f, 1.0f});
      igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                            (ImVec4){0.75f, 0.22f, 0.22f, 1.0f});
    } else {
      igPushStyleColor_Vec4(ImGuiCol_Button,
                            (ImVec4){0.16f, 0.55f, 0.30f, 1.0f});
      igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                            (ImVec4){0.20f, 0.65f, 0.36f, 1.0f});
    }
    if (igButton(recording_now ? "Stop Recording###clips_rec_toggle"
                               : "Start Recording###clips_rec_toggle",
                (ImVec2){tb_w, tb_h})) {
      recorder_toggle();
    }
    igPopStyleColor(2);
    crystal_sheen();
  }

  if (!s_scanned) {
    scan_clips_dir();
    s_scanned = true;
  }
  draw_list(env, sw, sh);
#else
  igSetCursorPos((ImVec2){sw * 0.5f - 160.0f, sh * 0.45f});
  igTextWrapped(
      "Screen recording is an Android-only feature and isn't available "
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

void ui_clips_gallery_destroy(tenv *env) {
  (void)env;
  s_clip_count = 0;
  s_scanned = false;
  s_pending_delete_index = -1;
}
