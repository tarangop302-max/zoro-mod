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

/* 1 = my own head is steered by the server-true model (game/lag_comp.c): every
   steering byte is logged, every heading the server reports re-anchors the
   model, and the turns still in flight are replayed on top. 0 = old behaviour
   (echoes simply overwrite the heading). */
#define STEER_PREDICT 1
/* Latency compensation is a runtime setting (Settings > General): when ON,
   our own turns are shown when the server will receive them (sent time +
   one-way delay x Turn delay) instead of the instant they are sent, so the head
   on screen is where the real head is. */

/* Latency lead: server positions arrive one network trip late, so every head
   is drawn where it was ~one-way-delay ago -- i.e. short of where the server
   actually has it, which is the main reason for "died before touching the
   body". Extrapolating that much draws heads where the server has them now.
   0 = off, 1 = full estimate.
   The limits below used to be 100 ms and 0.9 of a segment, which stopped the
   lead at ~200 ms ping: the real head kept going while the drawn one waited,
   so at high ping you died up to ~70 units before touching (boosting). They
   are now only sanity limits; game/head_ext.c keeps the body joined to a head
   that is several segments ahead of the newest server point. */
#define NET_LEAD_FACTOR 1.0f
#define NET_LEAD_MAX_MS 400.0f
/* Extra extrapolation (ms) on top of the network delay. A frame is read up to
   one frame after its packet arrived and reaches the screen ~1-2 frames after
   it is drawn, none of which the ping covers -- at 90 Hz that is roughly 10-20
   ms. If enemy heads look too far ahead of their bodies lower this; if you
   still die before touching, raise it (try 0 / 10 / 20). */
#define NET_LEAD_EXTRA_MS 10.0f

/* Lag-spike handling. A spike shorter than LAG_START_MS is simply ridden out by
   dead-reckoning at full speed (no slow-down). Past that the world eases down
   to LAG_MULT_MIN (was 0.2 after 750 ms, which looked like a freeze). */
#define LAG_START_MS 750.0f  /* NTL match */
#define LAG_MULT_MIN 0.2f  /* NTL match */

/* Longest single frame the simulation will catch up on, in 8 ms units.
   Was 5 (=40 ms): any frame slower than 25 fps ran the world in slow motion. */
#define MAX_VFR 5.0f  /* NTL match: frame step capped at 5 x 8 ms */

/* NTL-matched steering values. */
#define NTL_KEY_BATCH_MS 50          /* keyboard turn packets (was 150) */
#define NTL_MOUSE_DEAD_D2 1.0f       /* squared px; mouse steers from 1 px out (was 256 = 16 px) */
#define NTL_TOUCH_DEAD_R 20.0f       /* px dead zone for touch/arrow (was 6) */

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
#define HOTKEY_OPEN_SETTINGS 8
#define HOTKEY_EYES_BACK 9
#define HOTKEY_KEY_EDITOR 10
#define HOTKEY_TEAM_CHAT 11
/* Hotkeys 0..7 live in the middle of user_settings (layout must not change,
   or old user.dat files would be misread). HOTKEY_OPEN_SETTINGS and
   HOTKEY_EYES_BACK are stored at the end of the struct -- use usr_hotkey() to
   access any index. */
#define NUM_STORED_HOTKEYS 8
#define NUM_HOTKEYS 12
#define MAX_KEY_BTNS 16
/* Lobby (title screen) background: -1 = theme + colour chosen in the picker
   (stored in lobby_bg_reserved[1]/[2], see ui/lobby_bg.c), LOBBY_BG_CUSTOM =
   the picture uploaded from the phone gallery. 0..LOBBY_BG_PRESET_COUNT-1 are
   values written by the older single-list picker; they are converted to
   theme + colour on first use, so keep this at 14. */
#define LOBBY_BG_PRESET_COUNT 14
#define LOBBY_BG_CUSTOM 100

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
  CLIPS_GALLERY = 10,
  REALTIME_LB = 11
} screen;

typedef enum font_size {
  FONT_SIZE_SMALL,
  FONT_SIZE_REGULAR,
  FONT_SIZE_LARGE,
  NUM_FONT_SIZES
} font_size;

#endif
