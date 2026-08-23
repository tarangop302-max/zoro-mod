#ifndef USER_SETTINGS_H
#define USER_SETTINGS_H

#include <cglm/cglm.h>
#include <stdbool.h>
#include <stdint.h>

#include "../constants.h"

#define MAX_NTL_TEAM_PROFILES 12
#define MAX_NTL_TEAM_NAME 47

typedef struct hotkey {
  int key;
  bool active;
  int mode;
  char description[MAX_HOTKEY_DESC_LENGTH + 1];
} hotkey;

typedef struct gameplay_mode {
  bool food_flicker;
  bool food_float;
  bool uniform_food_color;
  bool show_crosshair;
  bool show_boost;
  bool show_shadows;
  bool show_background;
  bool show_accessories;
  bool death_effect;
  bool player_names_outline;
  int food_type;
  int boost_type;
  int render_mode;
  bool transparent_skin;
  bool center_line;
  float food_scale;
  float qsm;
  float bg_scale;
  float boost_strength;
  vec3 food_color;
} gameplay_mode;

typedef struct ntl_team_profile {
  char name[MAX_NTL_TEAM_NAME + 1];
  char team_id[96];
  char auth_key[96];
} ntl_team_profile;

typedef struct custom_key_btn {
  bool  active;
  int   glfw_key;
  char  label[8];
  float rel_x;
  float rel_y;
  float rel_size;
  float opacity;
} custom_key_btn;

typedef struct user_settings {
  char version[4];
  char nickname[MAX_NICKNAME_LEN + 1];
  char ipv4[MAX_IPV4_LEN + 1];
  char skin_code[MAX_SKIN_CODE_LEN + 1];
  uint8_t accessory;
  bool custom_skin;
  uint8_t default_skin;
  int score;
  double play_time;
  int kills;
  font_size ui_font_size;
  font_size lb_font_size;
  font_size snake_names_font_size;
  font_size stats_font_size;

  vec3 bd_color;
  vec4 laser_color;
  int laser_thickness;
  int cursor_size;
  int minimap_size;
  bool minimap_pos_custom;
  float minimap_rel_x;
  float minimap_rel_y;
  bool restart_rc;
  bool quit_mc;
  bool vsync;
  bool smooth_zoom;
  bool snake_scores;
  bool instant_restart;
  float zoom_step;
  int bot_radius_mult;
  int bot_follow_circle_score;

  bool ctrl_mode_trackpad;

  bool ctrl_swap_sides;

  bool  boost_pos_custom;
  float boost_rel_x;
  float boost_rel_y;
  float boost_rel_size;
  float boost_opacity;

  bool  joy_pos_custom;
  float joy_rel_x;
  float joy_rel_y;
  float joy_rel_size;
  float joy_opacity;

  float arrow_size;
  float arrow_sensitivity;
  /* Originally drove a separate glow-pulse layer behind the arrow while
     boosting; that layer was removed. Now controls whether the arrow
     itself grows while boosting (see boost_sz in ui_overlay.c) -- off
     by default, so the arrow stays a constant size unless the player
     opts in via Controls > Touch Arrow Cursor. */
  bool  boost_arrow_anim;
  bool  arrow_invisible;
  bool  bot_vis;

  float zoom_sensitivity;
  float zslider_rel_x;
  float zslider_rel_y;
  float zslider_rel_h;
  float zslider_opacity;
  bool  zslider_horizontal;
  bool  zslider_hidden;

  bool  ntl_enabled;
  char  ntl_team_id[96];
  char  ntl_auth_key[96];

  bool  hk_show_btn[NUM_HOTKEYS];

  gameplay_mode modes[2];

  hotkey hotkeys[NUM_HOTKEYS];

  custom_key_btn key_btns[MAX_KEY_BTNS];

  /* v2.4 extension fields. These stay at the end so v2.3 binary settings can
     be loaded as a compatible prefix without resetting player preferences. */
  float transparent_skin_opacity[2];
  bool minimap_drag_enabled;
  int fps_limit;
  bool performance_mode;

  float ntl_chat_rel_x;
  float ntl_chat_rel_y;
  float ntl_chat_rel_w;
  float ntl_chat_rel_h;
  float ntl_players_rel_x;
  float ntl_players_rel_y;
  float ntl_players_rel_w;
  float ntl_players_rel_h;

  /* v2.5 extension fields. */
  bool ntl_chat_minimized;
  bool ntl_show_teammates;
  bool ntl_marker_labels;
  int ntl_marker_shape;
  float ntl_marker_size;
  vec4 ntl_marker_color;
  int own_marker_shape;
  float own_marker_size;
  vec4 own_marker_color;

  int ntl_team_profile_count;
  int ntl_active_team_profile;
  ntl_team_profile ntl_team_profiles[MAX_NTL_TEAM_PROFILES];

  /* v2.5.4 extension field. NTL identifies one client by the first eight
     hexadecimal characters of its transmitted nickname. Keep that prefix
     stable so nickname changes update the existing player instead of creating
     a nameless/new entry. */
  char ntl_client_id[9];

  /* ZORO Public Chat access key, assigned by the clan owner.
     Entered once, saved here so it doesn't need retyping. */
  char public_chat_key[96];

  /* v2.7 extension fields. Saved position/size for the Public/Team
     Chat window (global_chat.c), set via "Adjust position" /
     "Adjust size" on the TEAM CHAT panel. Relative to the viewport
     work area so they scale sensibly across screen sizes. Ignored
     (default top-left placement used instead) until the player
     customizes it, at which point public_chat_pos_custom is set. */
  bool  public_chat_pos_custom;
  float public_chat_rel_x;
  float public_chat_rel_y;
  float public_chat_rel_w;
  float public_chat_rel_h;

  /* v2.8 extension fields. Saved position (and, for the
     leaderboard, scale) for the in-game leaderboard, teammates
     list, and minimap (ui_overlay.c), set from the dedicated HUD
     layout editor screen reached via "Adjust HUD Layout" in
     Settings (see hud_layout_editor.c). Relative to the viewport
     work area. Ignored (default placement used instead) until
     customized, at which point the matching *_pos_custom flag is
     set.

     hud_layout_edit_mode is unused -- an earlier version of this
     editor toggled live gameplay settings (bot mode, instant
     restart) directly rather than using the isolated preview
     session hud_layout_editor.c uses now, and this flag gated
     that. Kept as an inert placeholder rather than removed, since
     removing a field from the middle of this struct would shift
     every field after it and misread already-saved settings
     files. Always false; see read_user_settings(). */
  bool  hud_layout_edit_mode;
  bool  leaderboard_pos_custom;
  float leaderboard_rel_x;
  float leaderboard_rel_y;
  float leaderboard_scale;
  bool  teammates_pos_custom;
  float teammates_rel_x;
  float teammates_rel_y;

  /* v2.9 extension fields, ported from Vlither-android. These stay at the
     end so pre-v2.9 files remain a compatible prefix.

     key_btn_shape: per-slot shape for custom keyboard buttons (0 = rounded
     rectangle, 1 = circle), set from the keyboard button editor.

     arrow_style: selected index into the touch-arrow design atlas.
     arrow_sync_with_zoom: when true, the arrow/head-dot scale with game
     zoom; when false they keep a fixed on-screen size.
     head_dot_color: local head-dot tint, chosen via the Controls color
     picker. */
  uint8_t key_btn_shape[MAX_KEY_BTNS];
  int   arrow_style;
  bool  arrow_sync_with_zoom;
  vec3  head_dot_color;
} user_settings;

void user_settings_default(user_settings* usr_settings);
void read_user_settings(user_settings* usr_settings);
void save_user_settings(user_settings* usr_settings);

#endif
