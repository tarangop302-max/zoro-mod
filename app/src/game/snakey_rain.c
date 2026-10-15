#include "snakey_rain.h"

#include "../external/mongoose.h"
#include "../user.h"
#ifdef ANDROID
#include "../android_glfw_shim.h"
#endif

#include <math.h>
#include <stdio.h>
#include <string.h>

#define SR_HOST "snakeyrain.com"
#define SR_PORT 8421
#define SR_VERSION_MAJOR 6
#define SR_VERSION_MINOR 36
#define SR_SPAWN_MAX 512

typedef struct sr_writer {
  uint8_t data[768];
  size_t len;
} sr_writer;

typedef struct sr_state {
  bool initialized;
  bool enabled_at_start; /* runtime active flag (no restart needed) */
  bool mgr_ready;
  bool shutting_down;
  bool ws_open;
  bool logged_in;
  bool start_requested;
  bool bots_running;
  bool boost;
  int mode;
  int bot_count;
  int account_max_bots;
  int desired_max_bots;
  double last_position_send;
  struct mg_mgr mgr;
  struct mg_connection *connection;
  char username[64];
  char password[64];
  char bot_name[25];
  char bot_skin[128];
  char status[96];
  char direction[48];
  char message[160];
  char toast[96];   /* transient on-screen notice (local only) */
  double toast_until;
  /* Connection / panel state */
  bool login_failed;        /* server rejected the credentials */
  bool conn_error;          /* gave up reconnecting; tap Retry */
  bool test_login;          /* connection opened only to verify the login */
  bool test_result_shown;   /* status currently shows a login-test result */
  int reconnect_attempts;
  double reconnect_at;      /* >0: time of the next automatic reconnect */
  double connect_started;   /* time the current connection attempt began */
  double message_until;     /* show the server message until this time */
  double stop_confirm_until;/* armed "tap again to stop" window */
  double picker_close_at;   /* auto-close time of the mode picker */
  bool mode_picker_open;
  bool settings_open;
  bool show_password;
  char last_error[80];      /* why the last connection attempt failed */
  uint8_t spawn_packet[SR_SPAWN_MAX];
  size_t spawn_packet_len;
} sr_state;

static sr_state S;

#define SR_MAX_RECONNECT 8
#define SR_DEFAULT_SKIN                                                    \
  "uuuuuuuauuuuuuaauuuuuaaauuuuaaaauuuaaaaauuaaaaaauaaaaaaa"               \
  "uuaaaaaauuuaaaaauuuuaaaauuuuuaaauuuuuuaa"

/* Modes the control bar can select (12 is blocked by the server protocol;
   6 and 7 are not selectable -- the server may still report them). */
static const int SR_MODES[] = {1, 2, 3, 4, 5, 8, 9, 10, 11, 13, 14};
#define SR_MODE_COUNT ((int)(sizeof SR_MODES / sizeof SR_MODES[0]))

static bool sr_mode_valid(int mode) {
  for (int i = 0; i < SR_MODE_COUNT; ++i)
    if (SR_MODES[i] == mode) return true;
  return false;
}

static const char *sr_mode_name(int mode) {
  switch (mode) {
    case 1: return "Follow";
    case 2: return "Unfollow";
    case 3: return "Random";
    case 4: return "Straight";
    case 5: return "Copy";
    case 8: return "Rain";
    case 9: return "Tornado";
    case 10: return "Circle CW";
    case 11: return "Circle CCW";
    case 13: return "Heart";
    case 14: return "Wigwag";
    default: {
      /* A mode the server set that the bar cannot select (e.g. 6): show its
         number instead of mislabelling it "Follow". */
      static char buf[16];
      snprintf(buf, sizeof buf, "Mode %d", mode);
      return buf;
    }
  }
}

static void sr_local_chat(const char *text) {
  if (!text || !text[0]) return;
  /* Local-only notification, shown briefly under the Snakey Rain bar. It is
     never sent to the game server or to other players. */
  snprintf(S.toast, sizeof S.toast, "%s", text);
  S.toast_until = igGetTime() + 4.0;
}

static void sr_status(const char *text) {
  snprintf(S.status, sizeof S.status, "%s", text ? text : "");
  /* Only push bot spawn / stop style events into chat for the local user. */
  if (!text || !text[0]) return;
  if (strstr(text, "Bots running") || strstr(text, "Bots stopped") ||
      strstr(text, "Starting bots") || strstr(text, "Stopping bots") ||
      strstr(text, "Waiting for your snake to spawn")) {
    sr_local_chat(text);
  }
}

static bool sr_put_u8(sr_writer *w, unsigned value) {
  if (!w || w->len + 1 > sizeof w->data) return false;
  w->data[w->len++] = (uint8_t)value;
  return true;
}

static bool sr_put_u16(sr_writer *w, unsigned value) {
  if (!w || w->len + 2 > sizeof w->data) return false;
  w->data[w->len++] = (uint8_t)(value & 255u);
  w->data[w->len++] = (uint8_t)((value >> 8) & 255u);
  return true;
}

static bool sr_put_string8(sr_writer *w, const char *text) {
  size_t len = text ? strlen(text) : 0;
  if (len > 255) len = 255;
  if (!sr_put_u8(w, (unsigned)len) || w->len + len > sizeof w->data)
    return false;
  if (len) memcpy(w->data + w->len, text, len);
  w->len += len;
  return true;
}

static bool sr_put_string16(sr_writer *w, const char *text) {
  size_t len = text ? strlen(text) : 0;
  if (len > 65535) len = 65535;
  if (!sr_put_u16(w, (unsigned)len) || w->len + len > sizeof w->data)
    return false;
  if (len) memcpy(w->data + w->len, text, len);
  w->len += len;
  return true;
}

static unsigned sr_read_u16(const uint8_t *data, size_t len, size_t offset) {
  if (!data || offset + 2 > len) return 0;
  return (unsigned)data[offset] | ((unsigned)data[offset + 1] << 8);
}

static void sr_send_raw(const void *data, size_t len, bool before_login) {
  if (!S.connection || !S.ws_open || !data || !len) return;
  if (!before_login && !S.logged_in) return;
  mg_ws_send(S.connection, data, len, WEBSOCKET_OP_BINARY);
}

static void sr_send_writer(const sr_writer *w, bool before_login) {
  if (w && w->len) sr_send_raw(w->data, w->len, before_login);
}

static void sr_send_login(void) {
  sr_writer w = {0};
  sr_put_u8(&w, 'a');
  sr_put_string16(&w, S.username);
  sr_put_string16(&w, S.password);
  sr_put_u8(&w, SR_VERSION_MAJOR);
  sr_put_u8(&w, SR_VERSION_MINOR);
  sr_send_writer(&w, true);
}

static void sr_send_character(void) {
  const char *name = S.bot_name[0] ? S.bot_name : "SnakeyRain";
  const char *skin = S.bot_skin[0] ? S.bot_skin :
      "uuuuuuuauuuuuuaauuuuuaaauuuuaaaauuuaaaaauuaaaaaauaaaaaaa"
      "uuaaaaaauuuaaaaauuuuaaaauuuuuaaauuuuuuaa";
  sr_writer w = {0};
  sr_put_u8(&w, 'c');
  sr_put_u8(&w, SR_VERSION_MAJOR);
  sr_put_u8(&w, SR_VERSION_MINOR);
  sr_put_u8(&w, 1);
  sr_put_string8(&w, name);
  sr_put_string8(&w, skin);
  sr_put_string8(&w, "12");
  sr_put_u8(&w, 11);
  sr_put_string8(&w, "");
  sr_send_writer(&w, false);
}

static void sr_send_tuning(void) {
  sr_writer w = {0};
  sr_put_u8(&w, 'T');
  sr_put_u16(&w, 2000);
  sr_put_u16(&w, 800);
  sr_put_u8(&w, 90);
  sr_put_u16(&w, 50);
  sr_put_u8(&w, 1);
  sr_put_u16(&w, 2000);
  sr_put_u8(&w, 45);
  sr_put_u16(&w, 1000);
  sr_send_writer(&w, false);

  sr_writer evolve = {0};
  sr_put_u8(&evolve, 'E');
  sr_put_u8(&evolve, 0);
  sr_put_u16(&evolve, 30);
  sr_put_u8(&evolve, 1);
  sr_send_writer(&evolve, false);
}

static void sr_send_max_bots(void) {
  int limit = S.desired_max_bots;
  if (S.account_max_bots > 0 && limit > S.account_max_bots)
    limit = S.account_max_bots;
  if (limit < 1) limit = 1;
  sr_writer w = {0};
  sr_put_u8(&w, 'B');
  sr_put_u16(&w, (unsigned)limit);
  sr_send_writer(&w, false);
}

static void sr_send_spawn(void) {
  if (!S.spawn_packet_len) return;
  sr_send_raw(S.spawn_packet, S.spawn_packet_len, false);
}

static bool sr_current_position(tenv *env, uint16_t *x, uint16_t *y) {
  if (!env || !env->usr || !x || !y) return false;
  game_data *g = &env->usr->gdata;
  snake *me = get_snake(g, g->data.snake_id);
  if (!me || !isfinite(me->xx) || !isfinite(me->yy)) return false;
  float fx = fmaxf(0.0f, fminf(me->xx + me->fx, 65535.0f));
  float fy = fmaxf(0.0f, fminf(me->yy + me->fy, 65535.0f));
  *x = (uint16_t)lroundf(fx);
  *y = (uint16_t)lroundf(fy);
  return true;
}

static void sr_send_origin(tenv *env) {
  uint16_t x, y;
  if (!sr_current_position(env, &x, &y)) return;
  sr_writer w = {0};
  sr_put_u8(&w, 'o');
  sr_put_u16(&w, x);
  sr_put_u16(&w, y);
  sr_send_writer(&w, false);
}

static void sr_send_mode(tenv *env, int mode) {
  if (mode < 1 || mode > 14 || mode == 12) return;
  S.mode = mode;
  if (mode == 8 || mode == 9 || mode == 14) sr_send_origin(env);
  uint8_t command[2] = {'d', (uint8_t)mode};
  sr_send_raw(command, sizeof command, false);
}

static void sr_send_server(tenv *env) {
  if (!env || !env->usr || !S.logged_in || !S.spawn_packet_len) return;
  const char *address = env->usr->usrs.ipv4;
  const char *colon = strrchr(address, ':');
  if (!colon || colon == address || !colon[1] ||
      strchr(address, ':') != colon) {
    sr_status("Snakey Rain requires an IPv4 game server");
    return;
  }
  char host[128] = {0};
  char port[16] = {0};
  size_t host_len = (size_t)(colon - address);
  if (host_len >= sizeof host) host_len = sizeof host - 1;
  memcpy(host, address, host_len);
  snprintf(port, sizeof port, "%s", colon + 1);

  sr_writer w = {0};
  sr_put_u8(&w, 'r');
  sr_put_string8(&w, host);
  sr_put_string8(&w, port);
  sr_send_writer(&w, false);
  sr_status("Starting bots...");
}

static void sr_try_start(tenv *env) {
  if (!S.start_requested) return;
  if (!S.logged_in) {
    sr_status("Connecting to Snakey Rain...");
    return;
  }
  if (!S.spawn_packet_len) {
    sr_status("Waiting for your snake to spawn...");
    return;
  }
  sr_send_server(env);
}

static void sr_after_login(tenv *env) {
  sr_send_character();
  sr_send_tuning();
  sr_send_max_bots();
  sr_send_spawn();
  sr_send_mode(env, S.mode);
  if (S.boost) {
    uint8_t boost[2] = {'b', 1};
    sr_send_raw(boost, sizeof boost, false);
  }
  sr_try_start(env);
}

static void sr_read_message(const uint8_t *data, size_t len) {
  if (!data || len < 3) return;
  size_t text_len = sr_read_u16(data, len, 1);
  if (text_len > len - 3) text_len = len - 3;
  if (text_len >= sizeof S.message) text_len = sizeof S.message - 1;
  memcpy(S.message, data + 3, text_len);
  S.message[text_len] = 0;
  S.message_until = S.message[0] ? igGetTime() + 8.0 : 0.0;
}

/* The connection dropped while the player still wants bots: retry with a
   growing delay instead of leaving the bar in a dead state. */
static void sr_schedule_reconnect(void) {
  S.reconnect_attempts++;
  if (S.reconnect_attempts > SR_MAX_RECONNECT) {
    S.start_requested = false;
    S.conn_error = true;
    S.reconnect_at = 0.0;
    {
      char msg[128];
      snprintf(msg, sizeof msg, "Can't reach Snakey Rain (%s) - tap Retry",
               S.last_error);
      sr_status(msg);
    }
    sr_local_chat("Snakey Rain: can't connect - tap Retry");
    return;
  }
  int shift = S.reconnect_attempts > 4 ? 3 : S.reconnect_attempts - 1;
  double delay = 2.0 * (double)(1 << shift);
  if (delay > 15.0) delay = 15.0;
  S.reconnect_at = igGetTime() + delay;
  {
    char msg[128];
    snprintf(msg, sizeof msg, "Connection lost (%s) - reconnecting...",
             S.last_error);
    sr_status(msg);
  }
}

static void sr_callback(struct mg_connection *c, int ev, void *ev_data) {
  tenv *env = (tenv *)c->fn_data;
  if (ev == MG_EV_CONNECT) {
    if (c->is_tls) {
      struct mg_tls_opts opts = {.skip_verification = 1};
      mg_tls_init(c, &opts);
    }
  } else if (ev == MG_EV_WS_OPEN) {
    S.ws_open = true;
    sr_status("Logging in...");
    sr_send_login();
  } else if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message *msg = (struct mg_ws_message *)ev_data;
    const uint8_t *data = (const uint8_t *)msg->data.buf;
    size_t len = msg->data.len;
    if (!data || len < 1) return;
    switch (data[0]) {
      case 'l': {
        const bool was_test = S.test_login;
        S.test_login = false;
        if (len < 2 || !data[1]) {
          S.login_failed = true;
          S.start_requested = false;
          S.reconnect_at = 0.0;
          S.test_result_shown = was_test;
          sr_status("Login failed - check username/password");
          sr_local_chat("Snakey Rain: login failed - check username/password");
          c->is_closing = true;
          return;
        }
        S.logged_in = true;
        S.login_failed = false;
        S.conn_error = false;
        S.reconnect_attempts = 0;
        S.account_max_bots = len >= 4 ? (int)sr_read_u16(data, len, 2) : 0;
        if (was_test && !S.start_requested) {
          /* Credentials check only: report, then drop the connection. */
          char ok[96];
          if (S.account_max_bots > 0)
            snprintf(ok, sizeof ok, "Login OK - account limit %d bots",
                     S.account_max_bots);
          else
            snprintf(ok, sizeof ok, "Login OK");
          S.test_result_shown = true;
          sr_status(ok);
          c->is_closing = true;
          return;
        }
        sr_status("Logged in");
        sr_after_login(env);
        break;
      }
      case 'g':
        if (len >= 2) S.mode = data[1];
        break;
      case 'b':
        if (len >= 2) S.boost = !!data[1];
        break;
      case 'i':
        if (len >= 3) S.bot_count = (int)sr_read_u16(data, len, 1);
        break;
      case 'w':
        if (len >= 2) {
          S.bots_running = !!data[1];
          sr_status(S.bots_running ? "Bots running" : "Bots stopped");
        }
        break;
      case 'm':
        sr_read_message(data, len);
        break;
      case 'd':
        if (len >= 2) {
          size_t n = data[1];
          if (n > len - 2) n = len - 2;
          if (n >= sizeof S.direction) n = sizeof S.direction - 1;
          memcpy(S.direction, data + 2, n);
          S.direction[n] = 0;
        }
        break;
      case 'P': {
        uint8_t pong[2] = {'P', 1};
        sr_send_raw(pong, sizeof pong, false);
        break;
      }
    }
  } else if (ev == MG_EV_ERROR) {
    /* mongoose passes the failure reason (e.g. "dns resolve failed",
       "connect failed", a TLS error) as a C string. */
    if (ev_data) snprintf(S.last_error, sizeof S.last_error, "%s", (char *)ev_data);
    sr_status("Snakey Rain connection error");
    c->is_closing = true;
  } else if (ev == MG_EV_CLOSE) {
    if (!S.last_error[0])
      snprintf(S.last_error, sizeof S.last_error, "%s",
               S.logged_in ? "server closed the connection"
               : S.ws_open ? "closed during login"
                           : "no response from server");
    S.connection = NULL;
    S.ws_open = false;
    S.logged_in = false;
    S.bots_running = false;
    S.bot_count = 0;
    if (S.shutting_down) return;
    if (S.login_failed) {
      S.test_login = false;
      return; /* keep the "login failed" status */
    }
    if (S.test_login) {
      S.test_login = false;
      S.test_result_shown = true;
      sr_status("Could not reach Snakey Rain");
      return;
    }
    if (S.start_requested)
      sr_schedule_reconnect();
    else if (!S.test_result_shown)
      sr_status("Logged out");
  }
}

static void sr_connect(tenv *env) {
  if (!S.enabled_at_start || S.connection) return;
  char url[96];
  snprintf(url, sizeof url, "wss://%s:%d/", SR_HOST, SR_PORT);
  S.reconnect_at = 0.0;
  S.last_error[0] = 0;
  S.connect_started = igGetTime();
  S.connection = mg_ws_connect(&S.mgr, url, sr_callback, env,
                               "Origin: https://slither.io\r\n");
  if (S.connection)
    sr_status("Connecting to Snakey Rain...");
  else
    sr_status("Unable to create Snakey Rain connection");
}

static void sr_copy_settings(user_settings *us) {
  if (!us) return;
  snprintf(S.username, sizeof S.username, "%s", us->snakey_rain_username);
  snprintf(S.password, sizeof S.password, "%s", us->snakey_rain_password);
  snprintf(S.bot_name, sizeof S.bot_name, "%s", us->snakey_rain_bot_name);
  snprintf(S.bot_skin, sizeof S.bot_skin, "%s", us->snakey_rain_bot_skin);
  S.desired_max_bots = us->snakey_rain_max_bots;
  if (!S.bot_name[0]) snprintf(S.bot_name, sizeof S.bot_name, "SnakeyRain");
  if (!S.bot_skin[0])
    snprintf(S.bot_skin, sizeof S.bot_skin,
             "uuuuuuuauuuuuuaauuuuuaaauuuuaaaauuuaaaaauuaaaaaauaaaaaaa"
             "uuaaaaaauuuaaaaauuuuaaaauuuuuaaauuuuuuaa");
  if (S.desired_max_bots < 1) S.desired_max_bots = 1;
}

static void sr_enable(tenv *env) {
  if (!env || !env->usr) return;
  sr_copy_settings(&env->usr->usrs);
  if (!S.mgr_ready) {
    mg_mgr_init(&S.mgr);
    S.mgr_ready = true;
  }
  S.enabled_at_start = true;
  sr_status("Ready");
}

static void sr_disable(void) {
  S.start_requested = false;
  S.test_login = false;
  S.login_failed = false;
  S.conn_error = false;
  S.test_result_shown = false;
  S.reconnect_at = 0.0;
  S.reconnect_attempts = 0;
  S.mode_picker_open = false;
  S.settings_open = false;
  if (S.connection) {
    /* Let the manager run once so the socket is really closed and freed
       (the update loop no longer polls once Snakey Rain is disabled). */
    S.connection->is_closing = 1;
    if (S.mgr_ready) mg_mgr_poll(&S.mgr, 0);
    S.connection = NULL;
  }
  S.bots_running = false;
  S.logged_in = false;
  S.ws_open = false;
  S.bot_count = 0;
  S.spawn_packet_len = 0;
  S.enabled_at_start = false;
  sr_status("Disabled");
}

void snakey_rain_init(tenv *env) {
  memset(&S, 0, sizeof S);
  S.initialized = true;
  S.mode = 1;
  if (env && env->usr) {
    if (sr_mode_valid(env->usr->usrs.snakey_rain_last_mode))
      S.mode = env->usr->usrs.snakey_rain_last_mode;
    S.boost = env->usr->usrs.snakey_rain_last_boost;
  }
  snprintf(S.direction, sizeof S.direction, "Follow");
  if (env && env->usr && env->usr->usrs.snakey_rain_enabled)
    sr_enable(env);
  else
    sr_status("Disabled");
}

void snakey_rain_destroy(tenv *env) {
  (void)env;
  if (!S.initialized) return;
  S.shutting_down = true;
  if (S.connection) S.connection->is_closing = 1;
  if (S.mgr_ready) mg_mgr_free(&S.mgr);
  memset(&S, 0, sizeof S);
}

bool snakey_rain_enabled_at_start(void) { return S.enabled_at_start; }

bool snakey_rain_requires_restart(tenv *env) {
  (void)env;
  return false; /* settings apply live — no app restart */
}

void snakey_rain_apply_settings(tenv *env) {
  if (!S.initialized || !env || !env->usr) return;
  user_settings *us = &env->usr->usrs;
  if (!us->snakey_rain_enabled) {
    if (S.enabled_at_start) sr_disable();
    return;
  }
  if (!S.enabled_at_start) {
    sr_enable(env);
    return;
  }
  sr_copy_settings(us);
  /* Editing the credentials (or any setting) clears a previous failure. */
  S.login_failed = false;
  S.conn_error = false;
  if (S.logged_in) {
    sr_send_character();
    sr_send_max_bots();
    sr_status("Bot name/skin updated");
    sr_local_chat("Snakey Rain: bot name/skin applied");
  }
}

void snakey_rain_on_spawn(tenv *env, const uint8_t *packet,
                          size_t packet_len) {
  if (!S.enabled_at_start || !env || !env->usr || !packet || !packet_len ||
      env->usr->gdata.preview_active ||
      env->usr->gdata.curr_screen != PLAYING)
    return;
  if (packet_len + 1 > sizeof S.spawn_packet) return;
  S.spawn_packet[0] = 'S';
  memcpy(S.spawn_packet + 1, packet, packet_len);
  S.spawn_packet_len = packet_len + 1;
  if (S.logged_in) {
    sr_send_spawn();
    sr_try_start(env);
  }
}

void snakey_rain_update(tenv *env) {
  if (!S.initialized || !env || !env->usr) return;
  user_settings *us = &env->usr->usrs;
  /* Live toggle — no app restart required. */
  if (us->snakey_rain_enabled && !S.enabled_at_start)
    sr_enable(env);
  else if (!us->snakey_rain_enabled && S.enabled_at_start)
    sr_disable();
  if (!S.enabled_at_start) return;
  /* Keep credentials/name/skin in sync while idle. */
  if (!S.logged_in) sr_copy_settings(us);
  else if (S.desired_max_bots != us->snakey_rain_max_bots) {
    S.desired_max_bots = us->snakey_rain_max_bots;
    if (S.desired_max_bots < 1) S.desired_max_bots = 1;
    sr_send_max_bots();
  }
  const double now_t = igGetTime();
  /* A pending (re)connect is only meaningful during a match: once the player
     is back in the lobby, drop it instead of restarting bots from a stale
     spawn packet. */
  if (S.start_requested && !S.logged_in &&
      env->usr->gdata.curr_screen != PLAYING) {
    S.start_requested = false;
    S.reconnect_at = 0.0;
    S.reconnect_attempts = 0;
    sr_status("Stopped");
  }
  if (!S.connection && S.start_requested && S.reconnect_at > 0.0 &&
      now_t >= S.reconnect_at)
    sr_connect(env);
  if (S.connection && !S.logged_in && S.connect_started > 0.0 &&
      now_t - S.connect_started > 15.0) {
    S.connect_started = 0.0;
    S.connection->is_closing = 1;
    sr_status("Snakey Rain connection timed out");
  }
  mg_mgr_poll(&S.mgr, 0);
  if (!S.logged_in || !S.bots_running || !env || !env->usr ||
      env->usr->gdata.curr_screen != PLAYING ||
      env->usr->gdata.conn != CONNECTED)
    return;
  if (S.mode != 1 && S.mode != 6 && S.mode != 9) return;
  double now = igGetTime();
  if (now - S.last_position_send < 0.03) return;
  uint16_t x, y;
  if (!sr_current_position(env, &x, &y)) return;
  sr_writer w = {0};
  sr_put_u8(&w, 'p');
  sr_put_u16(&w, x);
  sr_put_u16(&w, y);
  sr_send_writer(&w, false);
  S.last_position_send = now;
}

/* ============================================================================
 * Start / stop / mode / boost actions
 * ========================================================================== */

typedef enum {
  SRK_OFF = 0,
  SRK_IDLE,
  SRK_CONNECTING,
  SRK_WAITING, /* logged in, waiting for our snake to spawn */
  SRK_STARTING,
  SRK_RUNNING,
  SRK_ERROR
} sr_kind_t;

static int sr_kind(void) {
  if (!S.enabled_at_start) return SRK_OFF;
  if (S.bots_running) return SRK_RUNNING;
  if (S.login_failed || S.conn_error) return SRK_ERROR;
  if (S.start_requested) {
    if (!S.logged_in) return SRK_CONNECTING;
    if (!S.spawn_packet_len) return SRK_WAITING;
    return SRK_STARTING;
  }
  return SRK_IDLE;
}

static void sr_stop(void) {
  uint8_t stop[2] = {'x', 0};
  sr_send_raw(stop, sizeof stop, false);
  S.start_requested = false;
  S.reconnect_at = 0.0;
  S.reconnect_attempts = 0;
  sr_status(S.logged_in ? "Stopping bots..." : "Stopped");
}

static void sr_toggle_start(tenv *env) {
  if (S.bots_running || S.start_requested) {
    sr_stop();
    return;
  }
  /* A fresh start (also the "Retry" after an error) clears old failures. */
  S.login_failed = false;
  S.conn_error = false;
  S.test_result_shown = false;
  S.reconnect_attempts = 0;
  S.reconnect_at = 0.0;
  S.start_requested = true;
  if (!S.connection) sr_connect(env);
  sr_try_start(env);
}

static void sr_test_login(tenv *env) {
  if (!S.initialized || !S.enabled_at_start || !env || !env->usr) return;
  if (S.connection || S.start_requested || S.bots_running || S.test_login)
    return;
  sr_copy_settings(&env->usr->usrs);
  S.login_failed = false;
  S.conn_error = false;
  S.test_result_shown = false;
  S.reconnect_attempts = 0;
  S.test_login = true;
  sr_connect(env);
  if (S.connection)
    sr_status("Testing login...");
  else
    S.test_login = false;
}

/* Remember the last mode / Boost so they survive a restart. */
static void sr_persist_panel(tenv *env) {
  if (!env || !env->usr) return;
  user_settings *us = &env->usr->usrs;
  if (us->snakey_rain_last_mode == S.mode &&
      us->snakey_rain_last_boost == S.boost)
    return;
  us->snakey_rain_last_mode = S.mode;
  us->snakey_rain_last_boost = S.boost;
  save_user_settings(us);
}

static void sr_user_set_mode(tenv *env, int mode) {
  if (!sr_mode_valid(mode)) return;
  sr_send_mode(env, mode);
  sr_persist_panel(env);
}

static void sr_user_toggle_boost(tenv *env) {
  S.boost = !S.boost;
  uint8_t boost[2] = {'b', S.boost ? 1 : 0};
  sr_send_raw(boost, sizeof boost, false);
  sr_persist_panel(env);
}

static void sr_commit(tenv *env) {
  if (!env || !env->usr) return;
  save_user_settings(&env->usr->usrs);
  snakey_rain_apply_settings(env);
}

/* ============================================================================
 * UI helpers
 * ========================================================================== */

typedef struct sr_metrics {
  float s;     /* UI scale (same rule as the in-game Settings screen) */
  float line;  /* text line height */
  float h;     /* pill height -- also the minimum touch target */
  float padx;  /* horizontal padding inside a pill */
  float gap;   /* gap between pills */
} sr_metrics;

static sr_metrics sr_get_metrics(tenv *env) {
  sr_metrics m;
  float sw = (float)env->ctx->size[0];
  float sh = (float)env->ctx->size[1];
  float se = sw < sh ? sw : sh;
  m.s = se / 720.0f;
  if (m.s < 0.75f) m.s = 0.75f;
  if (m.s > 1.30f) m.s = 1.30f;
  m.line = igGetTextLineHeight();
  m.h = fmaxf(m.line + 20.0f * m.s, 42.0f * m.s);
  m.padx = 14.0f * m.s;
  m.gap = 6.0f * m.s;
  return m;
}

static ImU32 sr_u32(ImVec4 c) { return igColorConvertFloat4ToU32(c); }

static ImVec4 sr_alpha(ImVec4 c, float a) {
  c.w = a;
  return c;
}

static ImVec4 sr_mix(ImVec4 a, ImVec4 b, float t) {
  return (ImVec4){a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}

static float sr_text_w(const char *text) {
  ImVec2 ts;
  igCalcTextSize(&ts, text, NULL, false, -1.0f);
  return ts.x;
}

static ImVec4 sr_kind_color(int kind, double t) {
  switch (kind) {
    case SRK_RUNNING:
      return (ImVec4){0.35f, 0.95f, 0.55f, 1.0f};
    case SRK_CONNECTING:
    case SRK_WAITING:
    case SRK_STARTING: {
      float p = 0.80f + 0.20f * sinf((float)t * 5.0f);
      return (ImVec4){1.0f * p, 0.82f * p, 0.30f * p, 1.0f};
    }
    case SRK_ERROR:
      return (ImVec4){1.0f, 0.40f, 0.42f, 1.0f};
    default:
      return (ImVec4){1.0f, 0.55f, 0.75f, 1.0f};
  }
}

static const ImVec4 SR_PURPLE = {0.690f, 0.580f, 0.960f, 1.0f};
static const ImVec4 SR_TEXT = {0.945f, 0.925f, 1.0f, 1.0f};
static const ImVec4 SR_DIM = {0.62f, 0.58f, 0.74f, 1.0f};

/* Shorten text with "..." until it fits maxw pixels. */
static void sr_fit_text(char *buf, float maxw) {
  size_t len = strlen(buf);
  if (sr_text_w(buf) <= maxw) return;
  while (len > 4 && sr_text_w(buf) > maxw) buf[--len] = 0;
  if (len >= 3) memcpy(buf + len - 3, "...", 3);
}

/* Glassy pill: soft glow when "lit", dark purple body, top sheen, accent
   border. a/b = screen-space corners. */
static void sr_pill_bg(ImDrawList *dl, ImVec2 a, ImVec2 b, ImVec4 accent,
                       bool pressed, bool lit, float s) {
  float h = b.y - a.y;
  float round = h * 0.5f;
  if (lit) {
    for (int i = 3; i >= 1; --i) {
      float e = (float)i * 2.0f * s;
      ImDrawList_AddRectFilled(
          dl, (ImVec2){a.x - e, a.y - e}, (ImVec2){b.x + e, b.y + e},
          sr_u32(sr_alpha(accent, 0.05f * (float)i)), round + e * 0.4f,
          ImDrawFlags_None);
    }
  }
  ImVec4 base = {0.075f, 0.050f, 0.135f, 0.92f};
  if (lit) base = sr_mix(base, accent, 0.20f);
  if (pressed) base = sr_mix(base, accent, 0.40f);
  base.w = 0.92f;
  ImDrawList_AddRectFilled(dl, a, b, sr_u32(base), round, ImDrawFlags_None);
  ImDrawList_AddLine(dl, (ImVec2){a.x + round * 0.7f, a.y + 1.5f * s},
                     (ImVec2){b.x - round * 0.7f, a.y + 1.5f * s},
                     sr_u32((ImVec4){1.0f, 1.0f, 1.0f, 0.13f}), 1.5f * s);
  ImDrawList_AddRect(dl, a, b, sr_u32(sr_alpha(accent, lit ? 0.90f : 0.55f)),
                     round, ImDrawFlags_None, 1.5f * s);
}

static void sr_pill_text(ImDrawList *dl, ImVec2 a, ImVec2 b, float left,
                         float right, const char *text, ImVec4 col) {
  ImVec2 ts;
  igCalcTextSize(&ts, text, NULL, false, -1.0f);
  float zone_l = a.x + left;
  float zone_r = b.x - right;
  float tx = zone_l + ((zone_r - zone_l) - ts.x) * 0.5f;
  float ty = a.y + ((b.y - a.y) - ts.y) * 0.5f;
  ImDrawList_AddText_Vec2(dl, (ImVec2){tx, ty}, sr_u32(col), text, NULL);
}

static void sr_draw_gear(ImDrawList *dl, ImVec2 c, float r, ImU32 col,
                         float s) {
  ImDrawList_AddCircle(dl, c, r * 0.60f, col, 20, 2.2f * s);
  for (int i = 0; i < 8; ++i) {
    float ang = (float)i * 0.785398f;
    float ca = cosf(ang), sa = sinf(ang);
    ImDrawList_AddLine(dl, (ImVec2){c.x + ca * r * 0.60f, c.y + sa * r * 0.60f},
                       (ImVec2){c.x + ca * r, c.y + sa * r}, col, 3.0f * s);
  }
  ImDrawList_AddCircleFilled(dl, c, r * 0.20f, col, 12);
}

static void sr_draw_chevron(ImDrawList *dl, ImVec2 c, float r, bool up,
                            ImU32 col) {
  float k = r * 0.55f;
  if (up)
    ImDrawList_AddTriangleFilled(dl, (ImVec2){c.x - r, c.y + k},
                                 (ImVec2){c.x + r, c.y + k},
                                 (ImVec2){c.x, c.y - k}, col);
  else
    ImDrawList_AddTriangleFilled(dl, (ImVec2){c.x - r, c.y - k},
                                 (ImVec2){c.x + r, c.y - k},
                                 (ImVec2){c.x, c.y + k}, col);
}

static void sr_bots_label(int kind, bool confirming, char *buf, size_t n) {
  if (confirming) {
    snprintf(buf, n, "Stop? Tap again");
    return;
  }
  switch (kind) {
    case SRK_CONNECTING: snprintf(buf, n, "Connecting"); break;
    case SRK_WAITING: snprintf(buf, n, "Waiting spawn"); break;
    case SRK_STARTING: snprintf(buf, n, "Starting"); break;
    case SRK_RUNNING: snprintf(buf, n, "Bots=%d", S.bot_count); break;
    case SRK_ERROR: snprintf(buf, n, "Retry"); break;
    default: snprintf(buf, n, "Start Bots"); break;
  }
}

/* ============================================================================
 * Touch scrolling (same behaviour as the in-game Settings popup: dragging on
 * a label / empty space scrolls, dragging a slider or button operates it).
 * ========================================================================== */

static void sr_touch_scroll(tenv *env, int idx) {
  static int owner = -1;
  static bool dragging = false;
  static bool blocked = false;
  static float last_y = 0.0f, start_y = 0.0f, vel = 0.0f;
  static int vel_owner = -1;
  if (!env || !env->wnd) return;
  touch_state *t = &env->wnd->ui_touch;

  const ImGuiID move_id = igGetID_Str("#MOVE");
  const ImGuiID active = igGetActiveID();
  const bool item_active = (active != 0 && active != move_id);

  if (!t->down) {
    blocked = false;
    if (owner == idx) {
      if (dragging) vel_owner = idx;
      owner = -1;
      dragging = false;
    }
    if (vel_owner == idx) {
      if (vel > 0.5f || vel < -0.5f) {
        igSetScrollY_Float(igGetScrollY() - vel);
        vel *= 0.90f;
      } else {
        vel = 0.0f;
        vel_owner = -1;
      }
    }
    return;
  }

  if (owner == -1 && !blocked) {
    if (igIsWindowHovered(ImGuiHoveredFlags_None) && !item_active) {
      owner = idx;
      dragging = false;
      last_y = start_y = t->y;
      vel = 0.0f;
      vel_owner = -1;
    }
    return;
  }
  if (owner != idx) return;

  if (item_active) {
    owner = -1;
    blocked = true;
    return;
  }
  float dy = t->y - last_y;
  if (!dragging && (t->y - start_y > 10.0f || start_y - t->y > 10.0f))
    dragging = true;
  if (dragging) {
    igSetScrollY_Float(igGetScrollY() - dy);
    vel = dy;
  }
  last_y = t->y;
}

/* ============================================================================
 * Shared settings panel (title-screen popup + in-game window)
 * ========================================================================== */

static void sr_settings_contents(tenv *env, int scroll_idx) {
  tuser_data *usr = env->usr;
  user_settings *usrs = &usr->usrs;
  ImGuiStyle *style = igGetStyle();
  const double now = igGetTime();

  sr_touch_scroll(env, scroll_idx);

  /* ---- live status ---- */
  {
    int kind = sr_kind();
    ImVec4 col = sr_kind_color(kind, now);
    char txt[160];
    if (!usrs->snakey_rain_enabled) {
      col = SR_DIM;
      snprintf(txt, sizeof txt, "Off - enable it below to use bots");
    } else if (kind == SRK_OFF) {
      snprintf(txt, sizeof txt, "Starting up...");
    } else if (kind == SRK_RUNNING) {
      snprintf(txt, sizeof txt, "Bots running: %d", S.bot_count);
    } else if (kind == SRK_IDLE) {
      if (S.test_result_shown) {
        snprintf(txt, sizeof txt, "%s", S.status);
        if (strncmp(S.status, "Login OK", 8) == 0)
          col = (ImVec4){0.35f, 0.95f, 0.55f, 1.0f};
        else
          col = (ImVec4){1.0f, 0.40f, 0.42f, 1.0f};
      } else {
        snprintf(txt, sizeof txt,
                 "Ready - bots connect when you start playing");
      }
    } else {
      snprintf(txt, sizeof txt, "%s", S.status);
    }
    if (S.test_login) col = sr_kind_color(SRK_CONNECTING, now);

    ImVec2 p;
    igGetCursorScreenPos(&p);
    float lh = igGetTextLineHeight();
    float r = lh * 0.28f;
    ImDrawList *dl = igGetWindowDrawList();
    ImDrawList_AddCircleFilled(
        dl, (ImVec2){p.x + r + 2.0f, p.y + lh * 0.5f + 1.0f}, r, sr_u32(col),
        16);
    igSetCursorScreenPos((ImVec2){p.x + r * 2.0f + 10.0f, p.y});
    igPushStyleColor_Vec4(ImGuiCol_Text, col);
    igTextWrapped("%s", txt);
    igPopStyleColor(1);
    if (usrs->snakey_rain_enabled && S.account_max_bots > 0)
      igTextDisabled("Account limit: %d bots", S.account_max_bots);
  }
  igSpacing();

  bool enabled = usrs->snakey_rain_enabled;
  if (igCheckbox("Enable Snakey Rain", &enabled)) {
    usrs->snakey_rain_enabled = enabled;
    sr_commit(env);
  }
  igSpacing();

  igBeginDisabled(!usrs->snakey_rain_enabled);

  igSeparatorText("Account");
  igSetNextItemWidth(-1.0f);
  igInputTextWithHint("##snakey_username", "Snakey Rain username",
                      usrs->snakey_rain_username,
                      sizeof usrs->snakey_rain_username,
                      ImGuiInputTextFlags_None, NULL, NULL);
  if (igIsItemDeactivatedAfterEdit()) sr_commit(env);

  {
    float cbw = igGetFrameHeight() + style->ItemInnerSpacing.x +
                sr_text_w("Show");
    ImVec2 av;
    igGetContentRegionAvail(&av);
    igSetNextItemWidth(av.x - cbw - style->ItemSpacing.x);
    igInputTextWithHint("##snakey_password", "Snakey Rain password",
                        usrs->snakey_rain_password,
                        sizeof usrs->snakey_rain_password,
                        S.show_password ? ImGuiInputTextFlags_None
                                        : ImGuiInputTextFlags_Password,
                        NULL, NULL);
    if (igIsItemDeactivatedAfterEdit()) sr_commit(env);
    igSameLine(0.0f, style->ItemSpacing.x);
    igCheckbox("Show", &S.show_password);
  }

  {
    bool busy = !S.enabled_at_start || S.connection != NULL ||
                S.start_requested || S.bots_running || S.test_login;
    ImVec2 av;
    igGetContentRegionAvail(&av);
    igBeginDisabled(busy);
    if (igButton(S.test_login ? "Testing..." : "Test login",
                 (ImVec2){av.x, 0.0f}))
      sr_test_login(env);
    igEndDisabled();
  }

  igSeparatorText("Bots");
  {
    int limit = S.account_max_bots > 0 ? S.account_max_bots : 1000;
    if (limit > 1000) limit = 1000;
    int shown = usrs->snakey_rain_max_bots;
    if (shown > limit) shown = limit;
    if (shown < 1) shown = 1;

    igText("Maximum bots: %d", shown);

    float fh = igGetFrameHeight();
    float step_w = fh * 1.6f;
    float sp = style->ItemSpacing.x;
    ImVec2 av;
    igGetContentRegionAvail(&av);
    bool commit = false;

    if (igButton("-10##sr_dec", (ImVec2){step_w, 0.0f})) {
      shown = shown > 10 ? shown - 10 : 1;
      usrs->snakey_rain_max_bots = shown;
      commit = true;
    }
    igSameLine(0.0f, sp);
    igSetNextItemWidth(av.x - step_w * 2.0f - sp * 2.0f);
    if (igSliderInt("##snakey_max_bots", &shown, 1, limit, "%d",
                    ImGuiSliderFlags_AlwaysClamp))
      usrs->snakey_rain_max_bots = shown;
    if (igIsItemDeactivatedAfterEdit()) commit = true;
    igSameLine(0.0f, sp);
    if (igButton("+10##sr_inc", (ImVec2){step_w, 0.0f})) {
      shown = shown + 10 > limit ? limit : shown + 10;
      usrs->snakey_rain_max_bots = shown;
      commit = true;
    }

    /* Quick presets */
    static const int presets[] = {50, 100, 250, 500, 1000};
    const int np = (int)(sizeof presets / sizeof presets[0]);
    float chip_w = (av.x - sp * (float)(np - 1)) / (float)np;
    for (int i = 0; i < np; ++i) {
      char lbl[24];
      snprintf(lbl, sizeof lbl, "%d##sr_chip%d", presets[i], i);
      bool cur = (shown == presets[i]);
      if (cur)
        igPushStyleColor_Vec4(ImGuiCol_Button,
                              (ImVec4){0.50f, 0.38f, 0.82f, 0.70f});
      igBeginDisabled(presets[i] > limit);
      if (igButton(lbl, (ImVec2){chip_w, 0.0f})) {
        usrs->snakey_rain_max_bots = presets[i];
        commit = true;
      }
      igEndDisabled();
      if (cur) igPopStyleColor(1);
      if (i + 1 < np) igSameLine(0.0f, sp);
    }
    if (commit) sr_commit(env);
  }

  igSeparatorText("Bot look");
  igText("Bot in-game name (max 24)");
  igSetNextItemWidth(-1.0f);
  igInputTextWithHint("##snakey_bot_name", "Name shown on bots",
                      usrs->snakey_rain_bot_name,
                      sizeof usrs->snakey_rain_bot_name,
                      ImGuiInputTextFlags_None, NULL, NULL);
  if (igIsItemDeactivatedAfterEdit()) sr_commit(env);

  igText("Bot skin code");
  igSetNextItemWidth(-1.0f);
  igInputTextWithHint("##snakey_bot_skin", "Skin string (same as extension)",
                      usrs->snakey_rain_bot_skin,
                      sizeof usrs->snakey_rain_bot_skin,
                      ImGuiInputTextFlags_None, NULL, NULL);
  if (igIsItemDeactivatedAfterEdit()) sr_commit(env);
  {
    ImVec2 av;
    igGetContentRegionAvail(&av);
    float half = (av.x - style->ItemSpacing.x) * 0.5f;
    if (igButton("Paste skin", (ImVec2){half, 0.0f})) {
      const char *clip = igGetClipboardText();
      if (clip) {
        while (*clip == ' ' || *clip == '\t' || *clip == '\r' ||
               *clip == '\n')
          ++clip;
        char tmp[sizeof usrs->snakey_rain_bot_skin];
        snprintf(tmp, sizeof tmp, "%s", clip);
        size_t n = strlen(tmp);
        while (n > 0 && (tmp[n - 1] == ' ' || tmp[n - 1] == '\t' ||
                         tmp[n - 1] == '\r' || tmp[n - 1] == '\n'))
          tmp[--n] = 0;
        if (tmp[0]) {
          snprintf(usrs->snakey_rain_bot_skin,
                   sizeof usrs->snakey_rain_bot_skin, "%s", tmp);
          sr_commit(env);
        }
      }
    }
    igSameLine(0.0f, style->ItemSpacing.x);
    if (igButton("Reset skin", (ImVec2){half, 0.0f})) {
      snprintf(usrs->snakey_rain_bot_skin, sizeof usrs->snakey_rain_bot_skin,
               "%s", SR_DEFAULT_SKIN);
      sr_commit(env);
    }
  }

  igEndDisabled();

  igSpacing();
  igPushStyleColor_Vec4(ImGuiCol_Text, SR_DIM);
  igTextWrapped("Credentials, bot name/skin, and the selected game server "
                "are sent to snakeyrain.com when bots start.");
  igPopStyleColor(1);
}

void snakey_rain_draw_settings_panel(tenv *env) {
  if (!env || !env->usr) return;
  sr_settings_contents(env, 4);
}

/* ============================================================================
 * In-game control bar, status chip, mode picker, settings window
 * ========================================================================== */

#define SR_PANEL_WINDOW_FLAGS                                               \
  (ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |                \
   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |             \
   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)

static void sr_draw_chip(tenv *env, sr_metrics m, float sw, float y_top,
                         int kind, double now) {
  char buf[160];
  buf[0] = 0;
  ImVec4 col = sr_kind_color(kind, now);
  if (S.toast[0] && now < S.toast_until) {
    snprintf(buf, sizeof buf, "%s", S.toast);
  } else if (kind == SRK_CONNECTING && S.reconnect_at > now) {
    snprintf(buf, sizeof buf, "%s - retry in %ds (%d/%d)", S.last_error,
             (int)ceil(S.reconnect_at - now), S.reconnect_attempts,
             SR_MAX_RECONNECT);
  } else if (kind == SRK_CONNECTING || kind == SRK_WAITING ||
             kind == SRK_STARTING || kind == SRK_ERROR) {
    snprintf(buf, sizeof buf, "%s", S.status);
  } else if (S.message[0] && now < S.message_until) {
    snprintf(buf, sizeof buf, "%s", S.message);
    col = SR_PURPLE;
  }
  if (!buf[0]) return;
  (void)env;

  float px = 12.0f * m.s;
  sr_fit_text(buf, sw * 0.92f - px * 2.0f);
  ImVec2 ts;
  igCalcTextSize(&ts, buf, NULL, false, -1.0f);
  float w = ts.x + px * 2.0f;
  float h = ts.y + 12.0f * m.s;

  igSetNextWindowPos((ImVec2){sw * 0.5f, y_top}, ImGuiCond_Always,
                     (ImVec2){0.5f, 0.0f});
  igSetNextWindowSize((ImVec2){w, h}, ImGuiCond_Always);
  igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 0.0f});
  igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, 0.0f);
  igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 0.0f);
  if (igBegin("##snakey_rain_chip", NULL,
              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs |
                  ImGuiWindowFlags_NoSavedSettings |
                  ImGuiWindowFlags_NoFocusOnAppearing |
                  ImGuiWindowFlags_NoScrollbar |
                  ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav)) {
    ImVec2 p;
    igGetWindowPos(&p);
    ImDrawList *dl = igGetWindowDrawList();
    ImVec2 b = {p.x + w, p.y + h};
    ImDrawList_AddRectFilled(dl, p, b, sr_u32((ImVec4){0.06f, 0.04f, 0.11f, 0.82f}),
                             h * 0.5f, ImDrawFlags_None);
    ImDrawList_AddRect(dl, p, b, sr_u32(sr_alpha(col, 0.60f)), h * 0.5f,
                       ImDrawFlags_None, 1.2f * m.s);
    ImDrawList_AddText_Vec2(dl, (ImVec2){p.x + px, p.y + (h - ts.y) * 0.5f},
                            sr_u32(SR_TEXT), buf, NULL);
  }
  igEnd();
  igPopStyleVar(3);
}

static void sr_draw_mode_picker(tenv *env, sr_metrics m, float sw, float sh,
                                float anchor_x, float y_top, double now) {
  if (!S.mode_picker_open) return;
  if (now > S.picker_close_at) {
    S.mode_picker_open = false;
    return;
  }
  float name_w = sr_text_w("Circle CCW");
  if (sr_text_w("Mode 14") > name_w) name_w = sr_text_w("Mode 14");
  float cell_w = name_w + m.padx * 2.0f;
  float cell_h = m.h * 0.92f;
  float wpad = 10.0f * m.s;
  int cols = 3;
  float w = 0.0f;
  for (; cols >= 1; --cols) {
    w = (float)cols * cell_w + (float)(cols - 1) * m.gap + wpad * 2.0f;
    if (w <= sw - 16.0f) break;
  }
  if (cols < 1) cols = 1;
  int rows = (SR_MODE_COUNT + cols - 1) / cols;
  float h = (float)rows * cell_h + (float)(rows - 1) * m.gap + wpad * 2.0f;
  float x = anchor_x;
  if (x + w > sw - 8.0f) x = sw - 8.0f - w;
  if (x < 8.0f) x = 8.0f;
  float y = y_top;
  if (y + h > sh - 8.0f) y = fmaxf(8.0f, sh - 8.0f - h);

#ifdef ANDROID
  android_ui_capture_rect(x, y, x + w, y + h);
#endif

  igSetNextWindowPos((ImVec2){x, y}, ImGuiCond_Always, (ImVec2){0.0f, 0.0f});
  igSetNextWindowSize((ImVec2){w, h}, ImGuiCond_Always);
  igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){wpad, wpad});
  igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 16.0f * m.s);
  igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, 1.5f);
  igPushStyleColor_Vec4(ImGuiCol_WindowBg,
                        (ImVec4){0.055f, 0.035f, 0.105f, 0.95f});
  igPushStyleColor_Vec4(ImGuiCol_Border, sr_alpha(SR_PURPLE, 0.55f));
  if (igBegin("##snakey_rain_modes", NULL, SR_PANEL_WINDOW_FLAGS)) {
    ImVec2 wp;
    igGetWindowPos(&wp);
    ImDrawList *dl = igGetWindowDrawList();
    for (int i = 0; i < SR_MODE_COUNT; ++i) {
      int r = i / cols, c = i % cols;
      ImVec2 a = {wp.x + wpad + (float)c * (cell_w + m.gap),
                  wp.y + wpad + (float)r * (cell_h + m.gap)};
      ImVec2 b = {a.x + cell_w, a.y + cell_h};
      char id[24];
      snprintf(id, sizeof id, "##sr_mode_%d", SR_MODES[i]);
      igSetCursorScreenPos(a);
      bool pressed = igInvisibleButton(id, (ImVec2){cell_w, cell_h}, 0);
      bool held = igIsItemActive();
      if (held) S.picker_close_at = now + 10.0;
      bool selected = (SR_MODES[i] == S.mode);
      sr_pill_bg(dl, a, b, SR_PURPLE, held, selected, m.s);
      sr_pill_text(dl, a, b, 0.0f, 0.0f, sr_mode_name(SR_MODES[i]),
                   selected ? SR_TEXT : sr_mix(SR_TEXT, SR_DIM, 0.35f));
      if (pressed) {
        sr_user_set_mode(env, SR_MODES[i]);
        S.mode_picker_open = false;
      }
    }
  }
  igEnd();
  igPopStyleColor(2);
  igPopStyleVar(3);
}

static void sr_draw_settings_window(tenv *env, sr_metrics m, float sw,
                                    float sh) {
  if (!S.settings_open) return;
  float w = fminf(sw * 0.92f, 600.0f * m.s);
  float h = fminf(sh * 0.86f, 820.0f * m.s);
  float x = (sw - w) * 0.5f;
  float y = (sh - h) * 0.5f;
  float wpad = 12.0f * m.s;

#ifdef ANDROID
  android_ui_capture_rect(x, y, x + w, y + h);
#endif

  igSetNextWindowPos((ImVec2){x, y}, ImGuiCond_Always, (ImVec2){0.0f, 0.0f});
  igSetNextWindowSize((ImVec2){w, h}, ImGuiCond_Always);

  /* Panel theme: 3 style vars + 12 colours, popped together below. */
  igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){wpad, wpad});
  igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 16.0f * m.s);
  igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, 12.0f * m.s);
  igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding,
                      (ImVec2){10.0f * m.s, 9.0f * m.s});
  igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing,
                      (ImVec2){8.0f * m.s, 10.0f * m.s});
  igPushStyleVar_Float(ImGuiStyleVar_ScrollbarSize, 24.0f * m.s);
  igPushStyleVar_Float(ImGuiStyleVar_GrabMinSize, 26.0f * m.s);
  igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, 1.5f);
  igPushStyleColor_Vec4(ImGuiCol_WindowBg,
                        (ImVec4){0.060f, 0.040f, 0.110f, 0.96f});
  igPushStyleColor_Vec4(ImGuiCol_Border, sr_alpha(SR_PURPLE, 0.55f));
  igPushStyleColor_Vec4(ImGuiCol_FrameBg,
                        (ImVec4){0.373f, 0.290f, 0.607f, 0.28f});
  igPushStyleColor_Vec4(ImGuiCol_FrameBgHovered,
                        (ImVec4){0.430f, 0.330f, 0.680f, 0.34f});
  igPushStyleColor_Vec4(ImGuiCol_FrameBgActive,
                        (ImVec4){0.470f, 0.360f, 0.720f, 0.42f});
  igPushStyleColor_Vec4(ImGuiCol_Button,
                        (ImVec4){0.373f, 0.290f, 0.607f, 0.40f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                        (ImVec4){0.430f, 0.330f, 0.680f, 0.50f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
                        (ImVec4){0.470f, 0.360f, 0.720f, 0.60f});
  igPushStyleColor_Vec4(ImGuiCol_SliderGrab,
                        (ImVec4){0.690f, 0.580f, 0.960f, 0.90f});
  igPushStyleColor_Vec4(ImGuiCol_SliderGrabActive,
                        (ImVec4){0.800f, 0.700f, 1.000f, 1.00f});
  igPushStyleColor_Vec4(ImGuiCol_CheckMark,
                        (ImVec4){0.800f, 0.700f, 1.000f, 1.00f});
  igPushStyleColor_Vec4(ImGuiCol_Text, SR_TEXT);

  if (igBegin("##snakey_rain_settings", NULL, SR_PANEL_WINDOW_FLAGS)) {
    /* Header: title + Close */
    float close_w = sr_text_w("Close") + m.padx * 2.0f;
    igPushStyleColor_Vec4(ImGuiCol_Text, SR_PURPLE);
    igText("Snakey Rain");
    igPopStyleColor(1);
    igSameLine(0.0f, 0.0f);
    igSetCursorPosX(w - wpad - close_w);
    if (igButton("Close##sr_close", (ImVec2){close_w, 0.0f}))
      S.settings_open = false;
    igSeparator();

    ImVec2 av;
    igGetContentRegionAvail(&av);
    if (igBeginChild_Str("##sr_settings_body", av, ImGuiChildFlags_None,
                         ImGuiWindowFlags_None)) {
      sr_settings_contents(env, 3);
    }
    igEndChild();
  }
  igEnd();
  igPopStyleColor(12);
  igPopStyleVar(8);
}

void snakey_rain_draw(tenv *env) {
  if (!S.enabled_at_start || !env || !env->usr || !env->ctx ||
      env->usr->gdata.curr_screen != PLAYING) {
    S.mode_picker_open = false;
    S.settings_open = false;
    return;
  }
  float sw = (float)env->ctx->size[0];
  float sh = (float)env->ctx->size[1];
  if (sw < 1.0f || sh < 1.0f) return;

  tuser_data *usr = env->usr;
  user_settings *us = &usr->usrs;
  ImFont *font = usr->imgui_data.regular_font[FONT_SIZE_SMALL];
  if (!font) return;
  igPushFont(font, font->LegacySize);

  sr_metrics m = sr_get_metrics(env);
  const double now = igGetTime();
  const int kind = sr_kind();
  const bool collapsed = us->snakey_rain_bar_collapsed;
  const bool confirming = S.bots_running && now < S.stop_confirm_until;
  if (!S.bots_running) S.stop_confirm_until = 0.0;
  if (collapsed) S.mode_picker_open = false;

  /* ---- labels and (stable) widths ---- */
  char bots_label[32];
  sr_bots_label(kind, confirming, bots_label, sizeof bots_label);

  float bots_lbl_w = sr_text_w("Bots=1000");
  {
    static const char *cands[] = {"Start Bots", "Connecting", "Waiting spawn",
                                  "Starting", "Retry", "Stop? Tap again"};
    for (size_t i = 0; i < sizeof cands / sizeof cands[0]; ++i)
      if (sr_text_w(cands[i]) > bots_lbl_w) bots_lbl_w = sr_text_w(cands[i]);
  }
  float mode_lbl_w = sr_text_w("Circle CCW");
  if (sr_text_w(sr_mode_name(S.mode)) > mode_lbl_w)
    mode_lbl_w = sr_text_w(sr_mode_name(S.mode));
  float boost_full_w = fmaxf(sr_text_w("Boost: ON"), sr_text_w("Boost: OFF"));
  float boost_short_w = sr_text_w("Boost");

  float dotw = m.h * 0.42f;
  float margin = 8.0f * m.s;
  float avail = sw - margin * 2.0f;
  float gap = m.gap;
  float icon_w = m.h;
  float bots_w = 0, mode_w = 0, boost_w = 0, width = 0;
  int pass;
  for (pass = 0; pass < 2; ++pass) {
    float px = pass ? m.padx * 0.6f : m.padx;
    bots_w = bots_lbl_w + dotw + px * 2.0f;
    mode_w = mode_lbl_w + m.line + px * 2.0f;
    boost_w = (pass ? boost_short_w : boost_full_w) + px * 2.0f;
    width = collapsed ? bots_w + gap + icon_w
                      : bots_w + mode_w + boost_w + icon_w * 2.0f + gap * 4.0f;
    if (width <= avail) break;
  }
  const bool compact = pass >= 1;
  if (width > avail) {
    float fit = avail / width;
    bots_w *= fit;
    mode_w *= fit;
    boost_w *= fit;
    icon_w *= fit;
    gap *= fit;
    width = avail;
  }

  float pady = 4.0f * m.s;
  float height = m.h + pady * 2.0f;
  float x = (sw - width) * 0.5f;
  float y = fmaxf(6.0f, 8.0f * m.s);

#ifdef ANDROID
  android_ui_capture_rect(x, y, x + width, y + height);
#endif

  float mode_x = x, bar_bottom = y + height;

  igSetNextWindowPos((ImVec2){x, y}, ImGuiCond_Always, (ImVec2){0.0f, 0.0f});
  igSetNextWindowSize((ImVec2){width, height}, ImGuiCond_Always);
  igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 0.0f});
  igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, 0.0f);
  igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 0.0f);
  bool open = igBegin("##snakey_rain_bar", NULL,
                      SR_PANEL_WINDOW_FLAGS | ImGuiWindowFlags_NoBackground |
                          ImGuiWindowFlags_NoFocusOnAppearing);
  if (open) {
    ImDrawList *dl = igGetWindowDrawList();
    float cx = x;
    float cy = y + pady;
    const ImU32 white = sr_u32(SR_TEXT);

    /* --- Bots pill: status dot + label; tap starts / stops --- */
    {
      ImVec2 a = {cx, cy}, b = {cx + bots_w, cy + m.h};
      igSetCursorScreenPos(a);
      bool pressed = igInvisibleButton("##sr_bots", (ImVec2){bots_w, m.h}, 0);
      bool held = igIsItemActive();
      ImVec4 accent = confirming ? sr_kind_color(SRK_ERROR, now)
                                 : sr_kind_color(kind, now);
      bool lit = confirming || kind != SRK_IDLE;
      sr_pill_bg(dl, a, b, accent, held, lit, m.s);

      ImVec2 dc = {a.x + m.padx * 0.6f + dotw * 0.5f, a.y + m.h * 0.5f};
      float dr = dotw * 0.28f;
      if (kind == SRK_RUNNING || kind == SRK_CONNECTING ||
          kind == SRK_WAITING || kind == SRK_STARTING) {
        float halo = dr + 2.0f * m.s + 1.5f * m.s * sinf((float)now * 4.0f);
        ImDrawList_AddCircle(dl, dc, halo, sr_u32(sr_alpha(accent, 0.40f)), 20,
                             1.5f * m.s);
      }
      ImDrawList_AddCircleFilled(
          dl, dc, dr,
          sr_u32(kind == SRK_IDLE ? (ImVec4){0.62f, 0.58f, 0.74f, 1.0f}
                                  : accent),
          20);
      sr_pill_text(dl, a, b, m.padx * 0.6f + dotw, m.padx * 0.6f, bots_label,
                   accent);
      if (pressed) {
        if (kind == SRK_RUNNING) {
          if (now < S.stop_confirm_until) {
            S.stop_confirm_until = 0.0;
            sr_toggle_start(env);
          } else {
            S.stop_confirm_until = now + 2.5;
          }
        } else {
          S.stop_confirm_until = 0.0;
          sr_toggle_start(env);
        }
      }
      cx += bots_w + gap;
    }

    if (!collapsed) {
      /* --- Mode pill: tap opens the mode picker --- */
      {
        ImVec2 a = {cx, cy}, b = {cx + mode_w, cy + m.h};
        mode_x = a.x;
        igSetCursorScreenPos(a);
        bool pressed = igInvisibleButton("##sr_mode", (ImVec2){mode_w, m.h}, 0);
        bool held = igIsItemActive();
        sr_pill_bg(dl, a, b, SR_PURPLE, held, S.mode_picker_open, m.s);
        sr_pill_text(dl, a, b, m.padx * 0.6f, m.padx * 0.6f + m.line * 0.9f,
                     sr_mode_name(S.mode), SR_TEXT);
        sr_draw_chevron(dl, (ImVec2){b.x - m.padx * 0.6f - m.line * 0.4f, a.y + m.h * 0.5f},
                        m.line * 0.28f, S.mode_picker_open, white);
        if (pressed) {
          S.mode_picker_open = !S.mode_picker_open;
          S.picker_close_at = now + 10.0;
          S.settings_open = false;
        }
        cx += mode_w + gap;
      }

      /* --- Boost pill --- */
      {
        ImVec2 a = {cx, cy}, b = {cx + boost_w, cy + m.h};
        igSetCursorScreenPos(a);
        bool pressed =
            igInvisibleButton("##sr_boost", (ImVec2){boost_w, m.h}, 0);
        bool held = igIsItemActive();
        ImVec4 amber = {1.0f, 0.82f, 0.28f, 1.0f};
        sr_pill_bg(dl, a, b, S.boost ? amber : SR_PURPLE, held, S.boost, m.s);
        sr_pill_text(dl, a, b, 0.0f, 0.0f,
                     compact ? "Boost" : (S.boost ? "Boost: ON" : "Boost: OFF"),
                     S.boost ? amber : sr_mix(SR_TEXT, SR_DIM, 0.45f));
        if (pressed) sr_user_toggle_boost(env);
        cx += boost_w + gap;
      }

      /* --- Settings (gear) --- */
      {
        ImVec2 a = {cx, cy}, b = {cx + icon_w, cy + m.h};
        igSetCursorScreenPos(a);
        bool pressed = igInvisibleButton("##sr_gear", (ImVec2){icon_w, m.h}, 0);
        bool held = igIsItemActive();
        sr_pill_bg(dl, a, b, SR_PURPLE, held, S.settings_open, m.s);
        sr_draw_gear(dl, (ImVec2){a.x + icon_w * 0.5f, a.y + m.h * 0.5f},
                     m.h * 0.27f, white, m.s);
        if (pressed) {
          S.settings_open = !S.settings_open;
          S.mode_picker_open = false;
        }
        cx += icon_w + gap;
      }
    }

    /* --- Collapse / expand chevron --- */
    {
      ImVec2 a = {cx, cy}, b = {cx + icon_w, cy + m.h};
      igSetCursorScreenPos(a);
      bool pressed = igInvisibleButton("##sr_collapse", (ImVec2){icon_w, m.h}, 0);
      bool held = igIsItemActive();
      sr_pill_bg(dl, a, b, SR_PURPLE, held, false, m.s);
      /* Points left while expanded (fold the bar away), right when collapsed. */
      float r = m.h * 0.16f, k = r * 0.55f;
      ImVec2 c = {a.x + icon_w * 0.5f, a.y + m.h * 0.5f};
      if (collapsed)
        ImDrawList_AddTriangleFilled(dl, (ImVec2){c.x - k, c.y - r},
                                     (ImVec2){c.x - k, c.y + r},
                                     (ImVec2){c.x + r, c.y}, white);
      else
        ImDrawList_AddTriangleFilled(dl, (ImVec2){c.x + k, c.y - r},
                                     (ImVec2){c.x + k, c.y + r},
                                     (ImVec2){c.x - r, c.y}, white);
      if (pressed) {
        us->snakey_rain_bar_collapsed = !collapsed;
        S.mode_picker_open = false;
        save_user_settings(us);
      }
    }
  }
  igEnd();
  igPopStyleVar(3);

  sr_draw_chip(env, m, sw, bar_bottom + 2.0f * m.s, kind, now);
  sr_draw_mode_picker(env, m, sw, sh, mode_x, bar_bottom + 4.0f * m.s, now);
  sr_draw_settings_window(env, m, sw, sh);

  igPopFont();
}
