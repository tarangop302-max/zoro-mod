#include "hud_layout_editor.h"

#ifdef ANDROID
#include "../android_glfw_shim.h"
#endif

#include "../user.h"

/* Whatever the player's real settings were right before entering the
 * editor, so "Back" can put everything -- including the HUD-visible
 * hotkey itself -- exactly how it was. */
static bool  s_saved_hud_hotkey = false;
static bool  s_snapshot_valid = false;

static bool  s_snap_lb_pos_custom = false;
static float s_snap_lb_rel_x = 0.0f;
static float s_snap_lb_rel_y = 0.0f;
static float s_snap_lb_scale = 0.72f;

static bool  s_snap_tm_pos_custom = false;
static float s_snap_tm_rel_x = 0.0f;
static float s_snap_tm_rel_y = 0.0f;

static bool  s_snap_mm_pos_custom = false;
static float s_snap_mm_rel_x = 0.0f;
static float s_snap_mm_rel_y = 0.0f;
static int   s_snap_mm_size = 300;

void ui_hud_layout_editor_enter(tenv* env) {
  tuser_data* usr = env->usr;
  user_settings* usrs = &usr->usrs;
  game_data* gdata = &usr->gdata;

  s_saved_hud_hotkey = usrs->hotkeys[HOTKEY_HUD].active;
  usrs->hotkeys[HOTKEY_HUD].active = true;

  s_snap_lb_pos_custom = usrs->leaderboard_pos_custom;
  s_snap_lb_rel_x = usrs->leaderboard_rel_x;
  s_snap_lb_rel_y = usrs->leaderboard_rel_y;
  s_snap_lb_scale = usrs->leaderboard_scale;

  s_snap_tm_pos_custom = usrs->teammates_pos_custom;
  s_snap_tm_rel_x = usrs->teammates_rel_x;
  s_snap_tm_rel_y = usrs->teammates_rel_y;

  s_snap_mm_pos_custom = usrs->minimap_pos_custom;
  s_snap_mm_rel_x = usrs->minimap_rel_x;
  s_snap_mm_rel_y = usrs->minimap_rel_y;
  s_snap_mm_size = usrs->minimap_size;

  s_snapshot_valid = true;

  gdata->curr_screen = HUD_LAYOUT_EDITOR;
}

static void exit_editor(tenv* env, bool discard) {
  tuser_data* usr = env->usr;
  user_settings* usrs = &usr->usrs;
  game_data* gdata = &usr->gdata;

  if (discard && s_snapshot_valid) {
    usrs->leaderboard_pos_custom = s_snap_lb_pos_custom;
    usrs->leaderboard_rel_x = s_snap_lb_rel_x;
    usrs->leaderboard_rel_y = s_snap_lb_rel_y;
    usrs->leaderboard_scale = s_snap_lb_scale;

    usrs->teammates_pos_custom = s_snap_tm_pos_custom;
    usrs->teammates_rel_x = s_snap_tm_rel_x;
    usrs->teammates_rel_y = s_snap_tm_rel_y;

    usrs->minimap_pos_custom = s_snap_mm_pos_custom;
    usrs->minimap_rel_x = s_snap_mm_rel_x;
    usrs->minimap_rel_y = s_snap_mm_rel_y;
    usrs->minimap_size = s_snap_mm_size;
  }

  usrs->hotkeys[HOTKEY_HUD].active = s_saved_hud_hotkey;
  s_snapshot_valid = false;

  save_user_settings(usrs);
  gdata->curr_screen = SETTINGS;
}

void ui_hud_layout_editor(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  user_settings* usrs = &usr->usrs;
  ImGuiStyle* style = igGetStyle();

  igPushFont(usr->imgui_data.regular_font[usrs->ui_font_size],
             usr->imgui_data.regular_font[usrs->ui_font_size]->LegacySize);

  igSetNextWindowPos((ImVec2){ctx->size[0] * 0.5f, style->WindowPadding.y},
                     ImGuiCond_Always, (ImVec2){0.5f, 0.0f});
  igSetNextWindowBgAlpha(0.9f);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                           ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoResize |
                           ImGuiWindowFlags_AlwaysAutoResize;

  if (igBegin("Adjust HUD Layout##hud_layout_editor", NULL, flags)) {
    ImVec2 live_pos;
    ImVec2 live_size;
    igGetWindowPos(&live_pos);
    igGetWindowSize(&live_size);

#ifdef ANDROID
    android_ui_capture_rect(live_pos.x, live_pos.y, live_pos.x + live_size.x,
                            live_pos.y + live_size.y);
#endif

    igTextWrapped(
        "Drag the leaderboard, teammates list, or minimap to move them. "
        "Drag a bottom-right corner to resize.");

    igSeparator();

    float btn_w = 140.0f;

    if (igButton("Back##hud_layout_editor", (ImVec2){btn_w, 0})) {
      exit_editor(env, true);
    }

    igSameLine(0, style->ItemSpacing.x);

    igPushStyleColor_Vec4(ImGuiCol_Button,
                          (ImVec4){0.20f, 0.62f, 0.30f, 1.0f});
    if (igButton("Confirm##hud_layout_editor", (ImVec2){btn_w, 0})) {
      exit_editor(env, false);
    }
    igPopStyleColor(1);
  }

  igEnd();

  igPopFont();
}
