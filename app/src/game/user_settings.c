#include "user_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <thermite.h>

#ifdef ANDROID
#include "../android_path.h"
#define OPEN_SETTINGS_FILE(mode) \
    (android_build_path(_settings_path, sizeof(_settings_path), USER_SETTINGS_FILE), \
     fopen(_settings_path, (mode)))
#else
#define OPEN_SETTINGS_FILE(mode) fopen(USER_SETTINGS_FILE, (mode))
#endif

bool g_ctrl_swap_sides = false;

void user_settings_default(user_settings* usr_settings) {
  usr_settings->ui_font_size = FONT_SIZE_SMALL;
  usr_settings->lb_font_size = FONT_SIZE_REGULAR;
  usr_settings->snake_names_font_size = FONT_SIZE_REGULAR;
  usr_settings->stats_font_size = FONT_SIZE_REGULAR;

  strcpy(usr_settings->version, SETTINGS_VERSION);

  usr_settings->bd_color[0] = 1;
  usr_settings->bd_color[1] = 0.25f;
  usr_settings->bd_color[2] = 0.25f;
  usr_settings->laser_color[0] = 0.5f;
  usr_settings->laser_color[1] = 1;
  usr_settings->laser_color[2] = 0.5f;
  usr_settings->laser_color[3] = 1;
  usr_settings->laser_thickness = 2;
  usr_settings->cursor_size = 48;
  usr_settings->minimap_size = 300;
  usr_settings->minimap_pos_custom = false;
  usr_settings->minimap_rel_x = 0.84f;
  usr_settings->minimap_rel_y = 0.78f;
  usr_settings->zoom_step = 0.1f;
  usr_settings->snake_scores = true;
  usr_settings->restart_rc = false;
  usr_settings->quit_mc = false;
  usr_settings->smooth_zoom = false;
  usr_settings->vsync = true;
  usr_settings->instant_restart = false;
  usr_settings->bot_radius_mult = 20;
  usr_settings->bot_follow_circle_score = 2000;
  usr_settings->ctrl_mode_trackpad = true;

  usr_settings->boost_pos_custom = false;
  usr_settings->boost_rel_x      = 0.875f;
  usr_settings->boost_rel_y      = 0.875f;
  usr_settings->boost_rel_size   = 0.125f;
  usr_settings->boost_opacity    = 1.0f;

  usr_settings->joy_pos_custom   = false;
  usr_settings->joy_rel_x        = 0.125f;
  usr_settings->joy_rel_y        = 0.825f;
  usr_settings->joy_rel_size     = 0.175f;
  usr_settings->joy_opacity      = 1.0f;

  usr_settings->zoom_sensitivity    = 1.0f;
  usr_settings->arrow_size          = 1.0f;
  usr_settings->arrow_sensitivity   = 1.0f;
  usr_settings->boost_arrow_anim    = false;
  usr_settings->arrow_invisible     = false;
  usr_settings->bot_vis             = true;
  usr_settings->zslider_rel_x       = 0.968f;
  usr_settings->zslider_rel_y       = 0.500f;
  usr_settings->zslider_rel_h       = 0.280f;
  usr_settings->zslider_opacity     = 1.0f;
  usr_settings->zslider_horizontal  = false;
  usr_settings->zslider_hidden      = false;

  for (int i = 0; i < NUM_HOTKEYS; i++)
    usr_settings->hk_show_btn[i] = false;
  usr_settings->ctrl_swap_sides    = false;

  usr_settings->ntl_enabled = true;
  usr_settings->ntl_team_id[0] = '\0';
  usr_settings->ntl_auth_key[0] = '\0';

  usr_settings->modes[0].food_flicker = true;
  usr_settings->modes[0].food_float = true;
  usr_settings->modes[0].uniform_food_color = false;
  usr_settings->modes[0].food_type = 0;
  usr_settings->modes[0].food_scale = 1;
  usr_settings->modes[0].food_color[0] = 1;
  usr_settings->modes[0].food_color[1] = 1;
  usr_settings->modes[0].food_color[2] = 1;
  usr_settings->modes[0].boost_type = 0;
  usr_settings->modes[0].qsm = 1;
  usr_settings->modes[0].bg_scale = 599 / 4096.0f;
  usr_settings->modes[0].boost_strength = 1;
  usr_settings->modes[0].show_crosshair = false;
  usr_settings->modes[0].show_boost = true;
  usr_settings->modes[0].show_shadows = true;
  usr_settings->modes[0].show_background = true;
  usr_settings->modes[0].show_accessories = true;
  usr_settings->modes[0].death_effect = true;
  usr_settings->modes[0].player_names_outline = false;
  usr_settings->modes[0].render_mode = 0;
  usr_settings->modes[0].transparent_skin = false;
  usr_settings->modes[0].center_line = false;

  usr_settings->modes[1].food_flicker = false;
  usr_settings->modes[1].food_float = false;
  usr_settings->modes[1].uniform_food_color = true;
  usr_settings->modes[1].food_type = 1;
  usr_settings->modes[1].food_scale = 1;
  usr_settings->modes[1].food_color[0] = 0.7f;
  usr_settings->modes[1].food_color[1] = 0.7f;
  usr_settings->modes[1].food_color[2] = 0.7f;
  usr_settings->modes[1].boost_type = 1;
  usr_settings->modes[1].qsm = 1;
  usr_settings->modes[1].bg_scale = 599 / 4096.0f;
  usr_settings->modes[1].boost_strength = 1;
  usr_settings->modes[1].show_crosshair = true;
  usr_settings->modes[1].show_boost = false;
  usr_settings->modes[1].show_shadows = true;
  usr_settings->modes[1].show_background = false;
  usr_settings->modes[1].show_accessories = false;
  usr_settings->modes[1].death_effect = false;
  usr_settings->modes[1].player_names_outline = true;
  usr_settings->modes[1].render_mode = 1;
  usr_settings->modes[1].transparent_skin = false;
  usr_settings->modes[1].center_line = false;

  usr_settings->hotkeys[HOTKEY_HUD] = (hotkey){GLFW_KEY_H, true, 0, "HUD"};
  usr_settings->hotkeys[HOTKEY_SHOW_NAMES] =
      (hotkey){GLFW_KEY_P, true, 0, "Show names"};
  usr_settings->hotkeys[HOTKEY_BIG_FOOD] =
      (hotkey){GLFW_KEY_F, false, 0, "Big food"};
  usr_settings->hotkeys[HOTKEY_ASSIST] =
      (hotkey){GLFW_KEY_K, false, 0, "Assist"};
  usr_settings->hotkeys[HOTKEY_BOT] =
      (hotkey){GLFW_KEY_T, false, 0, "Bot"};
  usr_settings->hotkeys[HOTKEY_MENU] =
      (hotkey){GLFW_KEY_Z, true, 0, "Hotkey menu"};
  usr_settings->hotkeys[HOTKEY_RESTART] =
      (hotkey){GLFW_KEY_R, false, 1, "Restart"};
  usr_settings->hotkeys[HOTKEY_QUIT] = (hotkey){GLFW_KEY_Q, false, 1, "Quit"};

  for (int i = 0; i < MAX_KEY_BTNS; i++) {
    usr_settings->key_btns[i].active   = false;
    usr_settings->key_btns[i].glfw_key = 0;
    usr_settings->key_btns[i].label[0] = '\0';
    usr_settings->key_btns[i].rel_x    = 0.5f;
    usr_settings->key_btns[i].rel_y    = 0.5f;
    usr_settings->key_btns[i].rel_size = 0.08f;
    usr_settings->key_btns[i].opacity  = 0.85f;
  }

  usr_settings->transparent_skin_opacity[0] = 0.35f;
  usr_settings->transparent_skin_opacity[1] = 0.35f;
  usr_settings->minimap_drag_enabled = false;
  usr_settings->fps_limit = 0;
  usr_settings->performance_mode = false;

  usr_settings->ntl_chat_rel_x = 0.012f;
  usr_settings->ntl_chat_rel_y = 0.020f;
  usr_settings->ntl_chat_rel_w = 0.36f;
  usr_settings->ntl_chat_rel_h = 0.44f;
  usr_settings->ntl_players_rel_x = 0.72f;
  usr_settings->ntl_players_rel_y = 0.020f;
  usr_settings->ntl_players_rel_w = 0.265f;
  usr_settings->ntl_players_rel_h = 0.44f;

  usr_settings->ntl_chat_minimized = false;
  usr_settings->ntl_show_teammates = true;
  usr_settings->ntl_marker_labels = true;
  usr_settings->ntl_marker_shape = 0;
  usr_settings->ntl_marker_size = 5.0f;
  usr_settings->ntl_marker_color[0] = 0.05f;
  usr_settings->ntl_marker_color[1] = 1.0f;
  usr_settings->ntl_marker_color[2] = 0.55f;
  usr_settings->ntl_marker_color[3] = 1.0f;
  usr_settings->own_marker_shape = 0;
  usr_settings->own_marker_size = 5.5f;
  usr_settings->own_marker_color[0] = 1.0f;
  usr_settings->own_marker_color[1] = 0.35f;
  usr_settings->own_marker_color[2] = 0.35f;
  usr_settings->own_marker_color[3] = 1.0f;
  usr_settings->ntl_team_profile_count = 0;
  usr_settings->ntl_active_team_profile = -1;
  memset(usr_settings->ntl_team_profiles, 0,
         sizeof usr_settings->ntl_team_profiles);
  usr_settings->ntl_client_id[0] = '\0';
  usr_settings->public_chat_key[0] = '\0';

  usr_settings->public_chat_pos_custom = false;
  usr_settings->public_chat_rel_x = 0.02f;
  usr_settings->public_chat_rel_y = 0.03f;
  usr_settings->public_chat_rel_w = 0.36f;
  usr_settings->public_chat_rel_h = 0.44f;

  usr_settings->hud_layout_edit_mode = false;
  usr_settings->leaderboard_pos_custom = false;
  usr_settings->leaderboard_rel_x = 0.80f;
  usr_settings->leaderboard_rel_y = 0.02f;
  usr_settings->leaderboard_scale = 0.72f;
  usr_settings->teammates_pos_custom = false;
  usr_settings->teammates_rel_x = 0.80f;
  usr_settings->teammates_rel_y = 0.30f;

  memset(usr_settings->key_btn_shape, 0, sizeof(usr_settings->key_btn_shape));
  usr_settings->arrow_style = 0;
  usr_settings->arrow_sync_with_zoom = true;
  usr_settings->head_dot_color[0] = 1.0f;
  usr_settings->head_dot_color[1] = 1.0f;
  usr_settings->head_dot_color[2] = 1.0f;

  usr_settings->white_skin_enemies[0] = true;
  usr_settings->white_skin_enemies[1] = true;
}

void write_default_settings(user_settings* usr_settings) {
  user_settings_default(usr_settings);

#ifdef ANDROID
  char _settings_path[512];
#endif
  FILE* f = OPEN_SETTINGS_FILE("wb");
  if (f == NULL) {

#ifdef ANDROID
    return;
#else
    printf("Error creating settings file.");
    exit(-1);
#endif
  }

  fwrite(usr_settings, sizeof(user_settings), 1, f);
  fclose(f);
}

void read_user_settings(user_settings* usr_settings) {
#ifdef ANDROID
  char _settings_path[512];
#endif
  FILE* f = OPEN_SETTINGS_FILE("rb");

  if (f == NULL) {
    write_default_settings(usr_settings);
    return;
  }

  /* Seed a temporary value with current defaults, then overwrite only the
     bytes that exist in the file. v2.3 files are a strict prefix of v2.4, so
     newly appended options receive safe defaults while all old preferences
     survive the upgrade. */
  user_settings loaded;
  user_settings_default(&loaded);

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    write_default_settings(usr_settings);
    return;
  }
  long file_size = ftell(f);
  rewind(f);
  if (file_size <= 0) {
    fclose(f);
    write_default_settings(usr_settings);
    return;
  }

  /* Each appended settings generation reuses space that may have been tail
     padding in the previous layout. Distinguish the known logical prefixes so
     legacy padding never overwrites newer defaults or the persistent NTL ID. */
  size_t v23_prefix = offsetof(user_settings, transparent_skin_opacity);
  size_t v24_prefix = offsetof(user_settings, ntl_chat_minimized);
  size_t v25_prefix = offsetof(user_settings, ntl_client_id);
  size_t v26_prefix = offsetof(user_settings, public_chat_key);
  size_t v27_prefix = offsetof(user_settings, public_chat_pos_custom);
  size_t v28_prefix = offsetof(user_settings, hud_layout_edit_mode);
  size_t v29_prefix = offsetof(user_settings, key_btn_shape);
  size_t v210_prefix = offsetof(user_settings, white_skin_enemies);
  size_t bytes_to_read;
  if ((size_t)file_size >= sizeof loaded)
    bytes_to_read = sizeof loaded;
  else if ((size_t)file_size >= v210_prefix)
    /* A file saved before the assist-mode white-skin-for-enemies toggle
       existed ends at v210_prefix, possibly followed only by compiler
       tail padding. Do not copy that padding into the new field. */
    bytes_to_read = v210_prefix;
  else if ((size_t)file_size >= v29_prefix)
    /* A file saved before the Vlither-ported arrow/head-dot/key-shape
       fields existed ends at v29_prefix, possibly followed only by
       compiler tail padding. Do not copy that padding into the new
       fields. */
    bytes_to_read = v29_prefix;
  else if ((size_t)file_size >= v28_prefix)
    /* A file saved before the HUD layout fields existed ends at
       v28_prefix, possibly followed only by compiler tail padding. Do not
       copy that padding into the new fields. */
    bytes_to_read = v28_prefix;
  else if ((size_t)file_size >= v27_prefix)
    /* A file saved before the chat position/size fields existed ends at
       v27_prefix, possibly followed only by compiler tail padding. Do not
       copy that padding into the new fields. */
    bytes_to_read = v27_prefix;
  else if ((size_t)file_size >= v26_prefix)
    /* A file saved before the public chat key field existed ends at
       v26_prefix, possibly followed only by compiler tail padding. Do not
       copy that padding into the new key field. */
    bytes_to_read = v26_prefix;
  else if ((size_t)file_size >= v25_prefix)
    /* v2.5 ended at v25_prefix, followed only by compiler tail padding. Do not
       copy that padding into the new persistent NTL client ID. */
    bytes_to_read = v25_prefix;
  else if ((size_t)file_size >= v24_prefix)
    /* A complete v2.4 file is larger than v24_prefix only because the struct
       was rounded up to 16-byte alignment. Ignore those final padding bytes. */
    bytes_to_read = v24_prefix;
  else
    bytes_to_read = (size_t)file_size < v23_prefix
                        ? (size_t)file_size
                        : v23_prefix;
  size_t bytes_read = fread(&loaded, 1, bytes_to_read, f);
  fclose(f);

  if (bytes_read < sizeof loaded.version ||
      strncmp(loaded.version, SETTINGS_VERSION, strlen(SETTINGS_VERSION)) != 0) {
    printf("Settings file outdated, recreating with default settings.\n");
    write_default_settings(usr_settings);
    return;
  }

  /* Validate appended settings in case a truncated or hand-edited file was
     loaded. */
  for (int i = 0; i < 2; ++i) {
    if (loaded.transparent_skin_opacity[i] < 0.15f ||
        loaded.transparent_skin_opacity[i] > 0.85f)
      loaded.transparent_skin_opacity[i] = 0.35f;
  }
  if (loaded.fps_limit != 0 && loaded.fps_limit != 60 &&
      loaded.fps_limit != 90 && loaded.fps_limit != 120 &&
      loaded.fps_limit != 144)
    loaded.fps_limit = 0;

  const float ntl_defaults[8] = {
      0.012f, 0.020f, 0.36f, 0.44f,
      0.72f, 0.020f, 0.265f, 0.44f};
  float* ntl_values = &loaded.ntl_chat_rel_x;
  for (int i = 0; i < 8; ++i) {
    bool is_size = (i == 2 || i == 3 || i == 6 || i == 7);
    float min_value = is_size ? 0.10f : -0.25f;
    float max_value = is_size ? 1.00f : 1.25f;
    if (!isfinite(ntl_values[i]) || ntl_values[i] < min_value ||
        ntl_values[i] > max_value)
      ntl_values[i] = ntl_defaults[i];
  }
  if (loaded.ntl_marker_shape < 0 || loaded.ntl_marker_shape > 2)
    loaded.ntl_marker_shape = 0;
  if (loaded.own_marker_shape < 0 || loaded.own_marker_shape > 2)
    loaded.own_marker_shape = 0;
  if (!isfinite(loaded.ntl_marker_size) || loaded.ntl_marker_size < 2.0f ||
      loaded.ntl_marker_size > 14.0f)
    loaded.ntl_marker_size = 5.0f;
  if (!isfinite(loaded.own_marker_size) || loaded.own_marker_size < 2.0f ||
      loaded.own_marker_size > 14.0f)
    loaded.own_marker_size = 5.5f;
  for (int c = 0; c < 4; ++c) {
    if (!isfinite(loaded.ntl_marker_color[c]) ||
        loaded.ntl_marker_color[c] < 0.0f || loaded.ntl_marker_color[c] > 1.0f)
      loaded.ntl_marker_color[c] =
          (const float[]){0.05f, 1.0f, 0.55f, 1.0f}[c];
    if (!isfinite(loaded.own_marker_color[c]) ||
        loaded.own_marker_color[c] < 0.0f || loaded.own_marker_color[c] > 1.0f)
      loaded.own_marker_color[c] =
          (const float[]){1.0f, 0.35f, 0.35f, 1.0f}[c];
  }
  if (loaded.ntl_team_profile_count < 0 ||
      loaded.ntl_team_profile_count > MAX_NTL_TEAM_PROFILES)
    loaded.ntl_team_profile_count = 0;
  if (loaded.ntl_active_team_profile < -1 ||
      loaded.ntl_active_team_profile >= loaded.ntl_team_profile_count)
    loaded.ntl_active_team_profile = -1;
  for (int i = 0; i < loaded.ntl_team_profile_count; ++i) {
    loaded.ntl_team_profiles[i].name[MAX_NTL_TEAM_NAME] = 0;
    loaded.ntl_team_profiles[i].team_id[95] = 0;
    loaded.ntl_team_profiles[i].auth_key[95] = 0;
    if (!loaded.ntl_team_profiles[i].name[0]) {
      if (loaded.ntl_active_team_profile == i)
        loaded.ntl_active_team_profile = -1;
      else if (loaded.ntl_active_team_profile > i)
        --loaded.ntl_active_team_profile;
      for (int j = i + 1; j < loaded.ntl_team_profile_count; ++j)
        loaded.ntl_team_profiles[j - 1] = loaded.ntl_team_profiles[j];
      --loaded.ntl_team_profile_count;
      --i;
    }
  }
  loaded.ntl_client_id[8] = 0;
  bool valid_ntl_client_id = strlen(loaded.ntl_client_id) == 8;
  for (int i = 0; valid_ntl_client_id && i < 8; ++i) {
    char c = loaded.ntl_client_id[i];
    valid_ntl_client_id =
        (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  }
  if (!valid_ntl_client_id) loaded.ntl_client_id[0] = 0;

  if (!isfinite(loaded.public_chat_rel_x) || loaded.public_chat_rel_x < -0.25f ||
      loaded.public_chat_rel_x > 1.25f)
    loaded.public_chat_rel_x = 0.02f;
  if (!isfinite(loaded.public_chat_rel_y) || loaded.public_chat_rel_y < -0.25f ||
      loaded.public_chat_rel_y > 1.25f)
    loaded.public_chat_rel_y = 0.03f;
  if (!isfinite(loaded.public_chat_rel_w) || loaded.public_chat_rel_w < 0.10f ||
      loaded.public_chat_rel_w > 1.00f)
    loaded.public_chat_rel_w = 0.36f;
  if (!isfinite(loaded.public_chat_rel_h) || loaded.public_chat_rel_h < 0.10f ||
      loaded.public_chat_rel_h > 1.00f)
    loaded.public_chat_rel_h = 0.44f;

  if (!isfinite(loaded.leaderboard_rel_x) || loaded.leaderboard_rel_x < -0.25f ||
      loaded.leaderboard_rel_x > 1.25f)
    loaded.leaderboard_rel_x = 0.80f;
  if (!isfinite(loaded.leaderboard_rel_y) || loaded.leaderboard_rel_y < -0.25f ||
      loaded.leaderboard_rel_y > 1.25f)
    loaded.leaderboard_rel_y = 0.02f;
  if (!isfinite(loaded.leaderboard_scale) || loaded.leaderboard_scale < 0.40f ||
      loaded.leaderboard_scale > 1.50f)
    loaded.leaderboard_scale = 0.72f;
  if (!isfinite(loaded.teammates_rel_x) || loaded.teammates_rel_x < -0.25f ||
      loaded.teammates_rel_x > 1.25f)
    loaded.teammates_rel_x = 0.80f;
  if (!isfinite(loaded.teammates_rel_y) || loaded.teammates_rel_y < -0.25f ||
      loaded.teammates_rel_y > 1.25f)
    loaded.teammates_rel_y = 0.30f;

  /* Never reopen mid-adjustment -- this is a live editing session
     (auto-play + auto-restart get force-enabled while it's on), not
     a preference, so a stale saved "true" (e.g. from a crash while
     editing) must not silently re-trigger it on next launch. */
  loaded.hud_layout_edit_mode = false;

  /* NTL's own chat feature is retired -- its floating HUD
     widget is a no-op now regardless, but force this off too
     so no saved-true value can ever re-trigger anything else
     that might still check it. */
  loaded.ntl_enabled = false;

  if (loaded.arrow_style < 0 || loaded.arrow_style >= ARROW_STYLE_COUNT)
    loaded.arrow_style = 0;
  for (int c = 0; c < 3; ++c) {
    if (!isfinite(loaded.head_dot_color[c]) || loaded.head_dot_color[c] < 0.0f ||
        loaded.head_dot_color[c] > 1.0f)
      loaded.head_dot_color[c] = 1.0f;
  }
  for (int i = 0; i < MAX_KEY_BTNS; ++i) {
    if (loaded.key_btn_shape[i] > 1) loaded.key_btn_shape[i] = 0;
  }

  *usr_settings = loaded;
}

void save_user_settings(user_settings* usr_settings) {
#ifdef ANDROID
  char _settings_path[512];
#endif
  FILE* f = OPEN_SETTINGS_FILE("wb");
  if (f == NULL) {

#ifdef ANDROID
    return;
#else
    printf("Error saving settings.");
    exit(-1);
#endif
  }

  fwrite(usr_settings, sizeof(user_settings), 1, f);
  fclose(f);
}
