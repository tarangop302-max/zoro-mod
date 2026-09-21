#ifndef CONSTANTS_H
#define CONSTANTS_H

#define CLIENT_VERSION 291

#define MAX_NICKNAME_LEN 24
#define MAX_IPV4_LEN 22
#define MAX_SERVER_LIST 300
#define MAX_SERVER_IP_LEN 22
#define MAX_SKIN_CODE_LEN 256
#define NUM_COLOR_GROUPS 42
#define NUM_DEFAULT_SKINS 66
#define NUM_ACCESSORIES 32
#define NO_ACCESSORY 255
#define BLANK_UV 40
#define WORM_EFFECT_LEN 13
#define NUM_FOOD_SIZES 17
#define NUM_PREY_SIZES 22
#define MAX_MINIMAP_SIZE 512
#define ARROW_STYLE_COUNT 10
#define TIMEOUT 5
#define PING_SAMPLE_COUNT 8
#define GOOD_PING 30
#define BAD_PING 60
#define NUM_LEADERBOARD_ENTRIES 10
#define MAX_ZOOM_IN 10
#define MAX_ZOOM_OUT 0.025f
#define LAST_POSITION_DURATION 60

/* ---------------------------------------------------------------------------
 * Movement / steering / network-spike tuning.
 * Everything that decides how fast the head reacts to the arrow and how the
 * game rides out a lag spike lives here so it can be tuned in one place.
 * ------------------------------------------------------------------------- */

/* Minimum real-time gap between steering (angle) packets. Was 50 ms measured on
   the per-frame clock (which really meant 50-67 ms at 60 Hz). 33 ms is the rate
   the NTL client uses, i.e. ~30 steering updates per second. */
#define STEER_ANGLE_MIN_MS 33.0
/* Minimum real-time gap between boost on/off packets (was 150 ms). */
#define STEER_BOOST_MIN_MS 50.0

/* 1 = the moment a steering packet is sent, the head starts turning locally
   (same turn rate the server uses) instead of waiting a full round trip for
   the server to echo the turn back. Server echoes are reconciled afterwards.
   0 = old behaviour. */
#define STEER_PREDICT 1
/* While a locally-predicted turn is still "in flight", server echoes about our
   own heading are not allowed to yank it backwards. The window is
   2 * one-way-delay + 50 ms, clamped to this range. */
#define STEER_PRED_MIN_WINDOW_MS 80.0f
#define STEER_PRED_MAX_WINDOW_MS 400.0f

/* Latency lead: server positions arrive one network trip late, so every head
   is drawn where it was ~one-way-delay ago -- i.e. short of where the server
   actually has it, which is a big part of "died before touching the body".
   Extrapolating that much (along the snake's own heading) draws heads where
   the server has them now. 0 = off, 1 = full estimate. */
#define NET_LEAD_FACTOR 1.0f
#define NET_LEAD_MAX_MS 100.0f

/* Lag-spike handling. A spike shorter than LAG_START_MS is simply ridden out by
   dead-reckoning at full speed (no slow-down). Past that the world eases down
   to LAG_MULT_MIN (was 0.2 after 750 ms, which looked like a freeze). */
#define LAG_START_MS 1000.0f
#define LAG_MULT_MIN 0.5f

/* Longest single frame the simulation will catch up on, in 8 ms units.
   Was 5 (=40 ms): any frame slower than 25 fps ran the world in slow motion. */
#define MAX_VFR 10.0f

#define PI2 6.2831853f
#define PI 3.1415926f

/* True when at least min_ms of real time has passed since last_ms. Also true if
   the clock base was reset underneath us (now < last), so a gate can never get
   stuck shut after glfwSetTime(0). */
static inline int rt_gate(double now_ms, double last_ms, double min_ms) {
  return now_ms < last_ms || now_ms - last_ms >= min_ms;
}

#define USER_SETTINGS_FILE "user.dat"

#define PROTOCOL_VERSION 19
#define GD_FLXC 38
#define GD_EEZ 53
#define GD_AFC 26
#define GD_VFC 62
#define GD_SMUC 100
#define GD_SMUC_M3 (GD_SMUC - 3)
#define GD_A64K (65536.0f / PI2)
#define GD_K64A (PI2 / 65536.0f)
#define GD_NSEP 4.5f

#define MAX_BOOST_INSTANCES 131072
#define MAX_FOOD_INSTANCES 131072
#define MAX_SPRITE_INSTANCES 131072
#define MAX_PREYS 2048

#define MAX_HOTKEY_DESC_LENGTH 64
#define HOTKEY_HUD 0
#define HOTKEY_SHOW_NAMES 1
#define HOTKEY_BIG_FOOD 2
#define HOTKEY_ASSIST 3
#define HOTKEY_BOT 4
#define HOTKEY_MENU 5
#define HOTKEY_RESTART 6
#define HOTKEY_QUIT 7
#define NUM_HOTKEYS 8
#define MAX_KEY_BTNS 16

typedef enum conn_status {
  DISCONNECTED = 0,
  CONNECTING = 1,
  CONNECTED = 2
} conn_status;

typedef enum screen {
  TITLE_SCREEN = 0,
  SKIN_EDITOR = 1,
  PLAYING = 2,
  SETTINGS = 3,
  CONTROLS = 4,
  NTL_PANEL = 5,
  HUD_LAYOUT_EDITOR = 6,
  KEYBOARD_EDITOR = 7,
  KILLS_GALLERY = 8,
  KILL_SHOTS_REVIEW = 9,
  CLIPS_GALLERY = 10
} screen;

typedef enum font_size {
  FONT_SIZE_SMALL,
  FONT_SIZE_REGULAR,
  FONT_SIZE_LARGE,
  NUM_FONT_SIZES
} font_size;

#endif
