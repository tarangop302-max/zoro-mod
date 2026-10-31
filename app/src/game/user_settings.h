#ifndef USER_SETTINGS_H
#define USER_SETTINGS_H

#include <cglm/cglm.h>
#include <stdbool.h>
#include <stdint.h>

#include "../constants.h"

#define MAX_NTL_TEAM_PROFILES 12
#define MAX_NTL_TEAM_NAME 47

#define MAX_SAVED_SKINS 30

/* gameplay_mode.render_mode values (Settings > Render mode). The first three
   are the original modes; the rest are the NTL-style skin textures. Values
   are stored in the settings file, so only ever append new ones. */
#define RENDER_MODE_TEXTURE 0
#define RENDER_MODE_SOLID 1
#define RENDER_MODE_FLAT 2
#define RENDER_MODE_ROUNDED_PLAIN 3
#define RENDER_MODE_SQUARED_PLAIN 4
#define RENDER_MODE_VLITHER_TEXTURED 5
#define RENDER_MODE_STRIPED_PLAIN 6
#define NUM_RENDER_MODES 7
#define MAX_SAVED_SKIN_NAME 23

#define MAX_SAVED_NAMES 20

/* Head dot diameter range (screen px), see user_settings.head_dot_size. */
#define HEAD_DOT_SIZE_MIN 2.0f
#define HEAD_DOT_SIZE_MAX 30.0f
#define HEAD_DOT_SIZE_DEFAULT 8.0f

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

typedef struct saved_skin {
  char name[MAX_SAVED_SKIN_NAME + 1];
  char skin_code[MAX_SKIN_CODE_LEN + 1];
  uint8_t accessory;
} saved_skin;

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
  /* When true, the trackpad arrow points directly from the touch-down
     point to the current finger position every frame (instant, absolute
     aim -- same idea as Vlither Enhanced's arrow steering). When false,
     it keeps the original relative/incremental drag behavior. */
  bool  ctrl_trackpad_direct;
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

  bool  hk_show_btn[NUM_STORED_HOTKEYS];

  gameplay_mode modes[2];

  hotkey hotkeys[NUM_STORED_HOTKEYS];

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

  /* v2.10 extension field, appended after head_dot_color so pre-v2.10
     files remain a compatible prefix (see v210_prefix in
     read_user_settings).

     white_skin_enemies: per-mode ([0]=Normal, [1]=Assist), mirrors
     transparent_skin_opacity's per-mode array layout above. When true,
     every OTHER snake's body renders as a flat white silhouette instead
     of its real skin/pattern (the local player's own snake is never
     touched). Only exposed in Controls for Assist mode -- index [1] is
     the one actually wired up in redraw.c; index [0] exists purely for
     layout symmetry and isn't read anywhere. */
  bool  white_skin_enemies[2];

  /* v2.11 extension fields. Player-saved custom skins (Skin editor > Custom
     > Save), browsable from the skin picker's "Saved skins" row alongside
     the built-in "Default skins" row. Kept at the end so pre-v2.11 files
     remain a compatible prefix (see v211_prefix in read_user_settings). */
  int saved_skin_count;
  int active_saved_skin;
  saved_skin saved_skins[MAX_SAVED_SKINS];

  /* v2.12 extension field. Tracks the highest score ever reached (see
     best_length in read_user_settings/callback.c) -- unlike `score`
     above, which is overwritten with the last game's result on every
     death, this one only ever goes up. Shown as a 4th line in the
     lobby stats (title_screen.c). Kept at the end so pre-v2.12 files
     remain a compatible prefix (see v212_prefix in read_user_settings). */
  int best_length;

  /* v2.13 extension field, ported from Vlither-android. Index into
     PROFILE_EMOJIS (profile_emoji.h); 0 = none. Chosen from Public Chat
     and broadcast alongside our marker/score/ping (see
     jsr_network_send_location), so it shows up next to our name for
     teammates on the roster and near our minimap dot. Purely cosmetic --
     not sent anywhere the SOS signal isn't already going. Kept at the end
     so pre-v2.13 files remain a compatible prefix (see v213_prefix in
     read_user_settings). */
  int profile_emoji_id;

  /* v2.14 extension fields. Ping/FPS history graph (net_graph.c): whether it
     is drawn, and where -- position relative to the screen, set by dragging it
     on the HUD layout editor screen (see hud_layout_editor.c). Appended after
     profile_emoji_id so every earlier file stays a compatible prefix (see
     v214_prefix in read_user_settings).

     net_graph_reserved exists only to keep sizeof(user_settings) strictly
     larger than the previous release. profile_emoji_id was followed by enough
     tail padding for the fields above to fit without growing the struct; an
     old file would then look like a complete new one and these fields would be
     filled from its padding bytes instead of their defaults. */
  bool    show_net_graph;
  bool    net_graph_pos_custom;
  float   net_graph_rel_x;
  float   net_graph_rel_y;
  uint8_t net_graph_reserved[8];

  /* v2.15 extension: native Snakey Rain integration (snakey_rain.c). Can be
     toggled live from the title screen. Appended after net_graph_reserved so
     every earlier file stays a compatible prefix (see v215_prefix in
     read_user_settings). The 16-byte reserved block keeps sizeof(user_settings)
     strictly larger than the previous release for the same reason as
     net_graph_reserved above. */
  uint8_t snakey_rain_settings_reserved[16];
  bool    snakey_rain_enabled;
  char    snakey_rain_username[64];
  char    snakey_rain_password[64];
  int     snakey_rain_max_bots;
  char    snakey_rain_bot_name[25];
  char    snakey_rain_bot_skin[128];

  /* v2.16 extension: per-mode Food glow toggle ([0]=Normal, [1]=Assist).
     Off by default; when off, no food type draws any glow halo. Appended
     after snakey_rain_bot_skin so every earlier file stays a compatible
     prefix (see v216_prefix in read_user_settings). The reserved block keeps
     sizeof(user_settings) strictly larger than the previous release. */
  uint8_t food_glow_reserved[8];
  bool    food_glow[2];

  /* v2.17 extension: "Open settings" hotkey (opens the Settings screen as
     a popup during a match). Appended after food_glow so every earlier file
     stays a compatible prefix (see v217_prefix in read_user_settings). The
     reserved block keeps sizeof(user_settings) strictly larger than the
     previous release. Access through usr_hotkey(usrs, HOTKEY_OPEN_SETTINGS). */
  uint8_t food_glow_reserved2[8];
  hotkey  hotkey_open_settings;

  /* v2.18 extension: "Eyes back" hotkey (ported from NTL VANCED). Appended
     after hotkey_open_settings so every earlier file stays a compatible
     prefix (see v218_prefix in read_user_settings). The reserved block keeps
     sizeof(user_settings) strictly larger than the previous release. Access
     through usr_hotkey(usrs, HOTKEY_EYES_BACK). */
  uint8_t eyes_back_reserved[8];
  hotkey  hotkey_eyes_back;

  /* v2.19 extension: per-mode head dot ([0]=Normal, [1]=Assist), drawn on
     top of the local player's snake head (redraw.c). Edited in the in-game
     Settings window, under each mode's section.

     head_dot_enabled: draw the dot at all (off by default).
     head_dot_size:    dot diameter in screen pixels (fixed on-screen size,
                       it does not scale with game zoom).
     head_dot_colors:  per-mode dot tint. The older single head_dot_color
                       field above is kept only for file-layout
                       compatibility; it seeds these colors when a file
                       saved before v2.19 is loaded.

     Appended after hotkey_eyes_back so every earlier file stays a compatible
     prefix (see v219_prefix in read_user_settings). The reserved block keeps
     sizeof(user_settings) strictly larger than the previous release. */
  uint8_t head_dot_reserved[8];
  bool    head_dot_enabled[2];
  float   head_dot_size[2];
  vec3    head_dot_colors[2];

  /* Team minimap dot pulse (v2.20). Local display option: makes the dots on
     this player's own minimap breathe so they are easier to spot.
     ntl_pulse_interval is the length of one pulse in seconds.

     Appended after head_dot_colors so every earlier file stays a compatible
     prefix (see v220_prefix in read_user_settings). */
  uint8_t ntl_pulse_reserved[8];
  bool    ntl_pulse_enabled;
  float   ntl_pulse_interval;

  /* v2.21 extension: previous nickname list (like NTL's nickname scroll).
     Names are added automatically when a nickname is entered / played with
     (user_settings_add_saved_name) and picked or removed from the list button
     next to the nickname box on the title screen. Oldest first; when full the
     oldest entry is dropped. Appended after ntl_pulse_interval so every
     earlier file stays a compatible prefix (see v221_prefix in
     read_user_settings). The reserved block keeps sizeof(user_settings)
     strictly larger than the previous release. */
  uint8_t saved_names_reserved[8];
  int     saved_name_count;
  char    saved_names[MAX_SAVED_NAMES][MAX_NICKNAME_LEN + 1];

  /* v2.22 extension: Snakey Rain panel state -- the last bot mode and Boost
     choice (restored on the next launch) and whether the in-game control bar
     is collapsed. Appended after saved_names so every earlier file stays a
     compatible prefix (see v222_prefix in read_user_settings). The 16-byte
     reserved block keeps sizeof(user_settings) strictly larger than the
     previous release (it always crosses the 16-byte alignment boundary). */
  uint8_t snakey_rain_ui_reserved[16];
  int     snakey_rain_last_mode;
  bool    snakey_rain_last_boost;
  bool    snakey_rain_bar_collapsed;

  /* v2.23 extension: NTL-style centre ring and assist turn path.

     center_ring_*: a ring drawn at the centre of the map (the server centre)
     so you always know where the middle is. Radius is in game units (NTL's
     default is 64), colour has an alpha channel.

     turn_path_*: while Assist is held, draws the path your snake would follow
     if it turned as hard as it can (both directions). The segment turns the
     "danger" colour when it would hit another snake. It is only drawn while
     your length is at least turn_path_min_score.

     Appended after snakey_rain_bar_collapsed so every earlier file stays a
     compatible prefix (see v223_prefix in read_user_settings). The 16-byte
     reserved block keeps sizeof(user_settings) strictly larger than the
     previous release. */
  uint8_t center_ring_reserved[16];
  bool    center_ring_enabled;
  float   center_ring_radius;
  vec4    center_ring_color;
  bool    turn_path_enabled;
  int     turn_path_min_score;
  vec4    turn_path_color;
  vec4    turn_path_danger_color;

  /* v2.24 extension: turn path line width in pixels. Appended after the
     v2.23 block; the 16-byte reserved block keeps sizeof(user_settings)
     strictly larger than the previous release (see v224_prefix). */
  uint8_t turn_path_reserved[16];
  float   turn_path_width;

  /* v2.25 extension: per-slot width multiplier for custom keyboard buttons
     (key_btn_wmul, 1.0 = the old width; height still comes from
     custom_key_btn.rel_size) and a thickness multiplier for the zoom slider
     (zslider_thick, 1.0 = the old thickness). Edited in the keyboard / HUD
     editor. Appended after turn_path_width so every earlier file stays a
     compatible prefix (see v225_prefix in read_user_settings). The reserved
     block keeps sizeof(user_settings) strictly larger than the previous
     release. */
  uint8_t key_geom_reserved[16];
  float   key_btn_wmul[MAX_KEY_BTNS];
  float   zslider_thick;

  /* v2.26 extension: "Keyboard layout" hotkey. Opens the Adjust keyboard
     buttons editor in the middle of a match (like the Open settings hotkey
     opens Settings). Appended after zslider_thick so every earlier file stays
     a compatible prefix (see v226_prefix in read_user_settings). Access through
     usr_hotkey(usrs, HOTKEY_KEY_EDITOR). */
  uint8_t key_editor_reserved[8];
  hotkey  hotkey_key_editor;
} user_settings;

static inline hotkey* usr_hotkey(user_settings* u, int i) {
  if (i < NUM_STORED_HOTKEYS) return &u->hotkeys[i];
  if (i == HOTKEY_OPEN_SETTINGS) return &u->hotkey_open_settings;
  if (i == HOTKEY_KEY_EDITOR) return &u->hotkey_key_editor;
  return &u->hotkey_eyes_back;
}

void user_settings_default(user_settings* usr_settings);
void read_user_settings(user_settings* usr_settings);
/* Latency compensation (server-synced steering + extra head lead). Stored in
   the first spare byte of net_graph_reserved so the file layout is unchanged;
   the byte holds 1 when the option is OFF, so existing files default to ON. */
static inline bool user_settings_latency_comp(const user_settings* s) {
  return s->net_graph_reserved[0] == 0;
}
static inline void user_settings_set_latency_comp(user_settings* s, bool on) {
  s->net_graph_reserved[0] = on ? 0 : 1;
}

/* How much of the one-way delay our own turns are held back on screen while
   Latency compensation is ON: 0 = show the turn instantly (old behaviour),
   100 = show it exactly when the server receives it. Stored in the second spare
   byte of net_graph_reserved as percent+1, so 0 (old files) means the default,
   100 (what you see is what the server has). */
static inline int user_settings_turn_delay_pct(const user_settings* s) {
  int v = s->net_graph_reserved[1];
  if (v == 0) return 100;
  v -= 1;
  return v > 100 ? 100 : v;
}
static inline void user_settings_set_turn_delay_pct(user_settings* s, int pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  s->net_graph_reserved[1] = (uint8_t)(pct + 1);
}

/* Dynamic minimap (minimap shrinks slightly as the arena shrinks). Stored in
   the third spare byte of net_graph_reserved so the file layout is unchanged;
   the byte holds 1 when the option is OFF, so existing files default to ON. */
static inline bool user_settings_dynamic_minimap(const user_settings* s) {
  return s->net_graph_reserved[2] == 0;
}
static inline void user_settings_set_dynamic_minimap(user_settings* s, bool on) {
  s->net_graph_reserved[2] = on ? 0 : 1;
}

void save_user_settings(user_settings* usr_settings);
/* Remember a nickname in the previous-names list. Ignores empty names and
   names already in the list. Returns true if the list changed. */
bool user_settings_add_saved_name(user_settings* usr_settings, const char* name);
/* Remove entry `index` from the previous-names list. */
void user_settings_remove_saved_name(user_settings* usr_settings, int index);

#endif
