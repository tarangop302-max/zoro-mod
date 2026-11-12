#include "server.h"
#ifdef ANDROID
#include "../android_glfw_shim.h"
#endif

#include "../user.h"
#include "callback.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#ifdef ANDROID
#include "../android_path.h"
#endif

/* ------------------------------------------------------------------ *
 * Favorite servers (shown first / starred in the server picker).
 * Kept in their own tiny text file (one "ip:port" per line) so the
 * versioned binary settings file does not have to change.
 * ------------------------------------------------------------------ */
#define SERVER_FAV_FILE "server_favs.txt"
#define SERVER_FAV_MAX  32

static char s_favs[SERVER_FAV_MAX][MAX_SERVER_IP_LEN + 1];
static int  s_fav_count  = 0;
static bool s_fav_loaded = false;

static FILE* fav_open(const char* mode) {
#ifdef ANDROID
  char path[512];
  android_build_path(path, sizeof(path), SERVER_FAV_FILE);
  return fopen(path, mode);
#else
  return fopen(SERVER_FAV_FILE, mode);
#endif
}

static void fav_save(void) {
  FILE* f = fav_open("w");
  if (!f) return;
  for (int i = 0; i < s_fav_count; i++) fprintf(f, "%s\n", s_favs[i]);
  fclose(f);
}

static void fav_load(void) {
  if (s_fav_loaded) return;
  s_fav_loaded = true;
  s_fav_count  = 0;
  FILE* f = fav_open("r");
  if (!f) return;
  char line[64];
  while (s_fav_count < SERVER_FAV_MAX && fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = '\0';
    if (!line[0] || strlen(line) > MAX_SERVER_IP_LEN) continue;
    strcpy(s_favs[s_fav_count++], line);
  }
  fclose(f);
}

bool server_fav_is(const char* ip) {
  fav_load();
  for (int i = 0; i < s_fav_count; i++)
    if (strcmp(s_favs[i], ip) == 0) return true;
  return false;
}

void server_fav_toggle(const char* ip) {
  fav_load();
  for (int i = 0; i < s_fav_count; i++) {
    if (strcmp(s_favs[i], ip) == 0) {
      for (int j = i + 1; j < s_fav_count; j++)
        strcpy(s_favs[j - 1], s_favs[j]);
      s_fav_count--;
      fav_save();
      return;
    }
  }
  if (s_fav_count >= SERVER_FAV_MAX || strlen(ip) > MAX_SERVER_IP_LEN) return;
  strcpy(s_favs[s_fav_count++], ip);
  fav_save();
}

void server_init(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;
  mg_log_set(MG_LL_NONE);
  mg_mgr_init(&gdata->network_manager);
}

void server_connect(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;

  char url[256] = {};

  bool is_local = (strncmp(usrs->ipv4, "127.", 4) == 0 ||
                   strncmp(usrs->ipv4, "localhost", 9) == 0 ||
                   strncmp(usrs->ipv4, "192.168.", 8) == 0 ||
                   strncmp(usrs->ipv4, "10.", 3) == 0);
  (void)is_local;
  /* Plain ws:// for every server, same as the NTL browser mod. */
  sprintf(url, "ws://%s/slither", usrs->ipv4);
  gdata->connection =
    mg_ws_connect(&gdata->network_manager, url, server_callback, env,
                  "Origin: https://slither.com\r\n");
#ifdef ANDROID

  mg_mgr_poll(&gdata->network_manager, 5);
  mg_mgr_poll(&gdata->network_manager, 5);
#endif
}

void server_poll(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;
#ifdef ANDROID
  /* While actually playing, never sleep in here: the game loop already blocks
   * on vsync once per frame, so a further 1 ms sleep just eats frame budget
   * (and Android's timer wake-up often overshoots it), while the socket is
   * drained again at the start of the very next frame anyway. In
   * menus/disconnected there's no rush, so keep the longer wait there to
   * avoid needlessly waking the CPU (battery). */
  int poll_ms = (gdata->conn == CONNECTED) ? 0 : 5;
  mg_mgr_poll(&gdata->network_manager, poll_ms);
#else
  mg_mgr_poll(&gdata->network_manager, 0);
#endif
  /* Ping accuracy: the socket is only read here, once per frame, so a pong is
     seen up to a frame after it really arrived. Remember when this read ended;
     the next pong was NOT there at this moment (see the 'p' handler). */
  gdata->data.rt_poll_end_ms = glfwGetTime() * 1000.0;
}

void server_destroy(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;

  mg_mgr_free(&gdata->network_manager);
}

#define SL_URL_HTTP  "http://slither.io/i80124.txt"
#define SL_URL_HTTPS "https://slither.io/i80124.txt"

/* ---- Built-in server table (mirrors NTL's list) -------------------------
   Custom servers come first (custom_count of them); kind 2 = Battledome-type
   arena where Eyes back is disabled, kind 1 = other custom server, kind 0 =
   regular official server (also fetched from slither.io; duplicates merge). */
typedef struct {
  const char* ip;
  const char* name;
  int         kind;
} ntl_server;

#define NTL_SERVER_COUNT 72
#define NTL_CUSTOM_COUNT 24

static const ntl_server NTL_SERVERS[NTL_SERVER_COUNT] = {
  {"206.206.76.190:444", "Singapore Battledome", 2},
  {"139.84.170.60:444", "India Battledome", 2},
  {"51.91.19.175:444", "EU Battledome", 2},
  {"206.221.176.241:444", "USA Battledome", 2},
  {"139.84.170.60:442", "India Drawing", 2},
  {"139.84.170.60:443", "India Battledome (443)", 2},
  {"45.76.102.107:443", "JP Voting Games", 2},
  {"45.76.102.107:444", "JP Battledome", 2},
  {"51.91.19.175:460", "EU Battle Royale", 2},
  {"172.93.101.248:446", "Slither Canvas", 2},
  {"206.221.176.211:444", "Paper Slither", 2},
  {"206.221.176.241:443", "US Battle Royale", 2},
  {"45.77.5.207:442", "West Coast Fakedome", 2},
  {"45.77.5.207:443", "US One vs One", 2},
  {"45.77.5.207:444", "West Coast Battledome", 2},
  {"216.128.169.33:443", "BR One vs One", 2},
  {"216.128.169.33:444", "BR Battledome", 2},
  {"51.91.19.175:443", "EU Custom (443)", 1},
  {"51.91.19.175:446", "EU StarBlitz (446)", 1},
  {"172.93.101.248:443", "US StarBlitz (443)", 1},
  {"172.93.101.248:444", "US StarBlitz (444)", 1},
  {"206.221.176.180:443", "US StarBlitz 2 (443)", 1},
  {"206.221.176.180:444", "US StarBlitz 2 (444)", 1},
  {"206.221.176.241:480", "US Custom (480)", 1},
  {"94.72.180.82:444", NULL, 0},
  {"217.138.162.194:444", NULL, 0},
  {"94.20.222.150:444", NULL, 0},
  {"148.113.20.151:443", NULL, 0},
  {"148.113.20.151:444", NULL, 0},
  {"144.168.36.194:444", NULL, 0},
  {"144.168.36.90:444", NULL, 0},
  {"181.41.140.146:443", NULL, 0},
  {"181.41.140.146:444", NULL, 0},
  {"181.41.140.170:443", NULL, 0},
  {"181.41.140.170:444", NULL, 0},
  {"181.41.140.178:444", NULL, 0},
  {"15.235.216.115:444", NULL, 0},
  {"15.235.218.24:443", NULL, 0},
  {"15.235.218.24:444", NULL, 0},
  {"185.199.38.101:443", NULL, 0},
  {"185.199.38.101:444", NULL, 0},
  {"162.19.235.91:444", NULL, 0},
  {"57.129.37.42:443", NULL, 0},
  {"57.129.37.42:444", NULL, 0},
  {"107.155.103.54:444", NULL, 0},
  {"95.216.38.155:444", NULL, 0},
  {"95.216.39.140:444", NULL, 0},
  {"135.125.74.228:444", NULL, 0},
  {"135.125.74.229:444", NULL, 0},
  {"92.222.100.202:443", NULL, 0},
  {"92.222.100.202:444", NULL, 0},
  {"92.222.100.203:444", NULL, 0},
  {"198.244.228.230:443", NULL, 0},
  {"198.244.231.35:444", NULL, 0},
  {"57.128.202.109:444", NULL, 0},
  {"57.128.202.110:444", NULL, 0},
  {"213.159.5.171:443", NULL, 0},
  {"213.159.5.171:444", NULL, 0},
  {"213.159.5.171:475", NULL, 0},
  {"107.155.76.138:444", NULL, 0},
  {"107.155.98.194:444", NULL, 0},
  {"107.155.83.170:444", NULL, 0},
  {"15.204.212.200:444", NULL, 0},
  {"192.211.52.146:444", NULL, 0},
  {"23.29.125.178:443", NULL, 0},
  {"66.165.238.34:444", NULL, 0},
  {"66.165.238.34:475", NULL, 0},
  {"51.161.209.120:443", NULL, 0},
  {"51.161.209.120:444", NULL, 0},
  {"45.158.39.114:443", NULL, 0},
  {"45.158.39.122:444", NULL, 0},
  {"45.158.39.98:444", NULL, 0},
};

typedef struct {
  const char* host;
  const char* cc;
} ntl_host_cc;

static const ntl_host_cc NTL_HOST_CC[] = {
  {"94.72.180.82", "NG"},
  {"217.138.162.194", "AE"},
  {"94.20.222.150", "AZ"},
  {"139.84.170.60", "IN"},
  {"148.113.20.151", "IN"},
  {"144.168.36.194", "JP"},
  {"144.168.36.90", "JP"},
  {"181.41.140.146", "JP"},
  {"181.41.140.170", "JP"},
  {"181.41.140.178", "JP"},
  {"45.76.102.107", "JP"},
  {"15.235.216.115", "SG"},
  {"15.235.218.24", "SG"},
  {"206.206.76.190", "SG"},
  {"185.199.38.101", "BG"},
  {"162.19.235.91", "DE"},
  {"57.129.37.42", "DE"},
  {"107.155.103.54", "ES"},
  {"95.216.38.155", "FI"},
  {"95.216.39.140", "FI"},
  {"135.125.74.228", "FR"},
  {"135.125.74.229", "FR"},
  {"51.91.19.175", "FR"},
  {"92.222.100.202", "FR"},
  {"92.222.100.203", "FR"},
  {"198.244.228.230", "GB"},
  {"198.244.231.35", "GB"},
  {"57.128.202.109", "PL"},
  {"57.128.202.110", "PL"},
  {"213.159.5.171", "TR"},
  {"107.155.76.138", "CA"},
  {"107.155.98.194", "CA"},
  {"107.155.83.170", "US"},
  {"15.204.212.200", "US"},
  {"172.93.101.248", "US"},
  {"192.211.52.146", "US"},
  {"206.221.176.180", "US"},
  {"206.221.176.211", "US"},
  {"206.221.176.241", "US"},
  {"23.29.125.178", "US"},
  {"45.77.5.207", "US"},
  {"66.165.238.34", "US"},
  {"51.161.209.120", "AU"},
  {"216.128.169.33", "BR"},
  {"45.158.39.114", "BR"},
  {"45.158.39.122", "BR"},
  {"45.158.39.98", "BR"},
};
#define NTL_HOST_CC_COUNT ((int)(sizeof(NTL_HOST_CC) / sizeof(NTL_HOST_CC[0])))

const char* server_custom_name(int idx) {
  if (idx < 0 || idx >= NTL_CUSTOM_COUNT) return "";
  return NTL_SERVERS[idx].name;
}

const char* server_country(const char* ip_port) {
  if (!ip_port || !*ip_port) return NULL;
  char host[32] = {0};
  if (sscanf(ip_port, "%31[^:]", host) != 1) return NULL;
  for (int i = 0; i < NTL_HOST_CC_COUNT; i++)
    if (strcmp(host, NTL_HOST_CC[i].host) == 0) return NTL_HOST_CC[i].cc;
  return NULL;
}

bool server_is_battledome(const char* ip_port) {
  if (!ip_port || !*ip_port) return false;
  for (int i = 0; i < NTL_CUSTOM_COUNT; i++)
    if (NTL_SERVERS[i].kind == 2 && strcmp(ip_port, NTL_SERVERS[i].ip) == 0)
      return true;
  return false;
}

static void server_list_seed_custom(game_data* gdata) {
  gdata->server_list.count = 0;
  for (int i = 0; i < MAX_SERVER_LIST; i++) {
    gdata->server_list.slots[i] = -1;
    gdata->server_list.sids[i]  = -1;
  }
  for (int i = 0; i < NTL_SERVER_COUNT; i++) {
    strncpy(gdata->server_list.ips[i], NTL_SERVERS[i].ip, MAX_SERVER_IP_LEN);
    gdata->server_list.ips[i][MAX_SERVER_IP_LEN] = '\0';
    gdata->server_list.count++;
  }
  gdata->server_list.custom_count = NTL_CUSTOM_COUNT;
}

/* Add a fetched server, or merge its slots/id into the built-in entry that
   already has the same ip:port. */
static void sl_add(char ips[][MAX_SERVER_IP_LEN + 1], int* slots, int* sids,
                   int* count, const char* ip, int slot, int sid) {
  for (int k = 0; k < *count; k++) {
    if (strcmp(ips[k], ip) == 0) {
      slots[k] = slot;
      sids[k]  = sid;
      return;
    }
  }
  if (*count >= MAX_SERVER_LIST) return;
  snprintf(ips[*count], MAX_SERVER_IP_LEN + 1, "%s", ip);
  slots[*count] = slot;
  sids[*count]  = sid;
  (*count)++;
}

static void server_list_parse(const char* data, int len,
                               char ips[][MAX_SERVER_IP_LEN + 1],
                               int* slots, int* sids, int* count) {

  enum { MAX_HB = MAX_SERVER_LIST * 28 + 28 };
  static int hb[MAX_SERVER_LIST * 28 + 28];
  int hb_len = 0;
  int cb = 0, eb = 0, gb = 0;

  for (int i = 1; i < len && hb_len < MAX_HB; i++) {
    int ch = (unsigned char)data[i];
    int bb = (((ch - 97 - eb) % 26) + 26) % 26;
    cb = cb * 16 + bb;
    eb = (eb + 7) % 26;
    if (gb == 1) {
      hb[hb_len++] = cb;
      gb = 0;
      cb = 0;
    } else {
      gb++;
    }
  }

  /* NOTE: *count is NOT reset here — the caller may have pre-seeded
     custom server entries at the start of the array, and this function
     appends the fetched official servers right after them. */
  if (hb_len > 0 && hb_len % 28 == 0) {

    for (int i = 0; i + 27 < hb_len && *count < MAX_SERVER_LIST; i += 28) {
      if (hb[i] <= 120) {
        int port = (hb[i + 21] << 8) | hb[i + 22];
        char ipbuf[MAX_SERVER_IP_LEN + 1];
        snprintf(ipbuf, sizeof(ipbuf), "%d.%d.%d.%d:%d",
                 hb[i+1], hb[i+2], hb[i+3], hb[i+4], port);
        /* Same layout NTL decodes: [23..24] = players, [26..27] = server id. */
        sl_add(ips, slots, sids, count, ipbuf,
               (hb[i + 23] << 8) | hb[i + 24], (hb[i + 26] << 8) | hb[i + 27]);
      }
    }
  } else if (hb_len > 0 && hb_len % 11 == 0) {

    for (int i = 0; i + 10 < hb_len && *count < MAX_SERVER_LIST; i += 11) {
      int port = (hb[i + 5] << 8) | hb[i + 6];
      char ipbuf[MAX_SERVER_IP_LEN + 1];
      snprintf(ipbuf, sizeof(ipbuf), "%d.%d.%d.%d:%d",
               hb[i+0], hb[i+1], hb[i+2], hb[i+3], port);
      sl_add(ips, slots, sids, count, ipbuf,
             (hb[i + 7] << 16) | (hb[i + 8] << 8) | hb[i + 9], hb[i + 10]);
    }
  }
}

static void sl_do_https(game_data* gdata);

static void sl_http_cb(struct mg_connection* c, int ev, void* ev_data) {
  game_data* gdata = (game_data*)c->fn_data;

  if (ev == MG_EV_CONNECT) {
    if (gdata->server_list.retry_https) {
      struct mg_tls_opts tls = {.skip_verification = 1};
      mg_tls_init(c, &tls);
    }
    mg_printf(c,
              "GET /i80124.txt HTTP/1.0\r\n"
              "Host: slither.io\r\n"
              "User-Agent: Mozilla/5.0\r\n"
              "Connection: close\r\n\r\n");
  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message* hm = (struct mg_http_message*)ev_data;
    int status = mg_http_status(hm);
    if (status == 200 && hm->body.len > 0) {
      server_list_parse(hm->body.buf, (int)hm->body.len,
                        gdata->server_list.ips, gdata->server_list.slots,
                        gdata->server_list.sids, &gdata->server_list.count);
      gdata->server_list.fetching   = false;
      gdata->server_list.fetched    = true;
      gdata->server_list.fetch_error = false;
    } else if ((status >= 301 && status <= 308) &&
               !gdata->server_list.retry_https) {

      gdata->server_list.retry_https = true;
      sl_do_https(gdata);
    } else if (!gdata->server_list.retry_https) {

      gdata->server_list.retry_https = true;
      sl_do_https(gdata);
    } else {
      gdata->server_list.fetching    = false;
      gdata->server_list.fetch_error = true;
    }
    c->is_draining = 1;
  } else if (ev == MG_EV_ERROR) {
    if (!gdata->server_list.retry_https) {

      gdata->server_list.retry_https = true;
      sl_do_https(gdata);
    } else {
      gdata->server_list.fetching    = false;
      gdata->server_list.fetch_error = true;
    }
  } else if (ev == MG_EV_CLOSE) {
    if (gdata->server_list.fetching && !gdata->server_list.retry_https) {
      gdata->server_list.fetching    = false;
      gdata->server_list.fetch_error = true;
    }
  }
}

static void sl_do_https(game_data* gdata) {
  mg_http_connect(&gdata->server_list.mgr, SL_URL_HTTPS, sl_http_cb, gdata);
}

void server_list_init(tenv* env) {
  game_data* gdata = &((tuser_data*)env->usr)->gdata;
  mg_mgr_init(&gdata->server_list.mgr);
  gdata->server_list.fetching    = false;
  gdata->server_list.fetched     = false;
  gdata->server_list.fetch_error = false;
  gdata->server_list.retry_https = false;
  gdata->server_list.pinging     = 0;
  gdata->server_list.ping_stop   = 0;
  gdata->server_list.pings_done  = 0;
  for (int i = 0; i < MAX_SERVER_LIST; i++) {
    gdata->server_list.pings[i]        = -1;
    gdata->server_list.sorted_order[i] = i;
  }
  server_list_seed_custom(gdata);
}

void server_list_fetch(tenv* env) {
  game_data* gdata = &((tuser_data*)env->usr)->gdata;
  if (gdata->server_list.fetching) return;
  gdata->server_list.ping_stop   = 1;
  gdata->server_list.pings_done  = 0;
  gdata->server_list.fetching    = true;
  gdata->server_list.fetched     = false;
  gdata->server_list.fetch_error = false;
  gdata->server_list.retry_https = false;
  for (int i = 0; i < MAX_SERVER_LIST; i++) {
    gdata->server_list.pings[i]        = -1;
    gdata->server_list.sorted_order[i] = i;
  }
  server_list_seed_custom(gdata);
  mg_http_connect(&gdata->server_list.mgr, SL_URL_HTTP, sl_http_cb, gdata);
}

void server_list_poll(tenv* env) {
  game_data* gdata = &((tuser_data*)env->usr)->gdata;
  mg_mgr_poll(&gdata->server_list.mgr, 0);
}

void server_list_destroy(tenv* env) {
  game_data* gdata = &((tuser_data*)env->usr)->gdata;
  gdata->server_list.ping_stop = 1;
  mg_mgr_free(&gdata->server_list.mgr);
}

/* ------------------------------------------------------------------ *
 * Server list ping probe.
 *
 * This mirrors the ping calculation used by the official web client:
 * rather than just timing a raw TCP connect() to the server's game
 * port (which only measures handshake latency and can be skewed by
 * SYN-cookie/accept-queue behavior), it opens a real WebSocket to the
 * server's dedicated ping-probe endpoint on port 80 at "/ptc", sends a
 * single application-level ping byte (0x70), waits for the same byte
 * to be echoed back, and repeats that round trip 4 times. The final
 * reported ping is the *minimum* of those 4 samples, which is what the
 * web client does to filter out one-off jitter/queueing delay and
 * report the best achievable latency to that server.
 * ------------------------------------------------------------------ */

#define PING_BATCH    64
#define PING_TIMEOUT  7000   /* ms, matches the web client's ipv4 probe timeout */
#define PING_SAMPLES  4      /* number of ping/pong round trips sampled */
#define PING_BYTE     0x70   /* 'p' - single-byte ping payload used by /ptc */

typedef struct {
  uint64_t start_ms;
  int      sample_count;
  int      samples[PING_SAMPLES];
  bool     done;
  int      result;
} ping_probe;

static void sl_ping_ws_cb(struct mg_connection* c, int ev, void* ev_data) {
  ping_probe* p = (ping_probe*)c->fn_data;
  if (p->done) return;

  if (ev == MG_EV_WS_OPEN) {
    p->start_ms = mg_millis();
    uint8_t b = PING_BYTE;
    mg_ws_send(c, &b, 1, WEBSOCKET_OP_BINARY);

  } else if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message* wm = (struct mg_ws_message*)ev_data;
    if (wm->data.len == 1 && (uint8_t)wm->data.buf[0] == PING_BYTE) {
      int elapsed = (int)(mg_millis() - p->start_ms);
      if (p->sample_count < PING_SAMPLES) p->samples[p->sample_count++] = elapsed;

      if (p->sample_count < PING_SAMPLES) {
        p->start_ms = mg_millis();
        uint8_t b = PING_BYTE;
        mg_ws_send(c, &b, 1, WEBSOCKET_OP_BINARY);
      } else {
        int best = 9999;
        for (int i = 0; i < p->sample_count; i++)
          if (p->samples[i] < best) best = p->samples[i];
        p->result = best;
        p->done   = true;
        c->is_closing = 1;
      }
    }

  } else if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE) {
    if (!p->done) {
      /* Connection dropped before all 4 samples came back. If we got at
         least one good round trip, report the best of what we have
         instead of throwing the whole probe away. */
      int best = 9999;
      for (int i = 0; i < p->sample_count; i++)
        if (p->samples[i] < best) best = p->samples[i];
      p->result = best;
      p->done   = true;
    }
  }
}

static void* sl_ping_thread(void* arg) {
  game_data* gdata = (game_data*)arg;
  int n = gdata->server_list.count;

  for (int bs = 0; bs < n; bs += PING_BATCH) {
    if (gdata->server_list.ping_stop) break;

    int be  = (bs + PING_BATCH < n) ? bs + PING_BATCH : n;
    int bsz = be - bs;

    struct mg_mgr ping_mgr;
    mg_mgr_init(&ping_mgr);

    ping_probe probes[PING_BATCH];
    memset(probes, 0, sizeof(probes));

    for (int j = 0; j < bsz; j++) {
      int idx = bs + j;
      char ip_buf[32] = {0};
      int  port = 0;
      if (sscanf(gdata->server_list.ips[idx], "%31[^:]:%d", ip_buf, &port) != 2) {
        probes[j].done   = true;
        probes[j].result = 9999;
        continue;
      }

      char url[64];
      snprintf(url, sizeof(url), "ws://%s:80/ptc", ip_buf);

      struct mg_connection* c = mg_ws_connect(
        &ping_mgr, url, sl_ping_ws_cb, &probes[j],
        "%s:%s\r\n%s:%s\r\n",
        "Origin", "https://slither.com",
        "Host", "slither.com");

      if (!c) {
        probes[j].done   = true;
        probes[j].result = 9999;
      }
    }

    uint64_t t0 = mg_millis();
    while (!gdata->server_list.ping_stop) {
      bool all_done = true;
      for (int j = 0; j < bsz; j++) {
        if (!probes[j].done) { all_done = false; break; }
      }
      if (all_done) break;
      if (mg_millis() - t0 >= PING_TIMEOUT) break;
      mg_mgr_poll(&ping_mgr, 50);
    }

    for (int j = 0; j < bsz; j++) {
      int idx = bs + j;
      if (!probes[j].done) {
        int best = 9999;
        for (int i = 0; i < probes[j].sample_count; i++)
          if (probes[j].samples[i] < best) best = probes[j].samples[i];
        probes[j].result = best;
      }
      gdata->server_list.pings[idx] = probes[j].result;
      gdata->server_list.pings_done++;
    }

    mg_mgr_free(&ping_mgr);
  }

  if (!gdata->server_list.ping_stop) {
    int n2 = gdata->server_list.count;
    for (int i = 0; i < n2; i++) gdata->server_list.sorted_order[i] = i;
    for (int i = 1; i < n2; i++) {
      int ki = gdata->server_list.sorted_order[i];
      int kp = gdata->server_list.pings[ki];
      int j  = i - 1;
      while (j >= 0 && gdata->server_list.pings[gdata->server_list.sorted_order[j]] > kp) {
        gdata->server_list.sorted_order[j+1] = gdata->server_list.sorted_order[j];
        j--;
      }
      gdata->server_list.sorted_order[j+1] = ki;
    }
  }

  gdata->server_list.pinging = 0;
  return NULL;
}

void server_list_start_ping(tenv* env) {
  game_data* gdata = &((tuser_data*)env->usr)->gdata;
  if (gdata->server_list.pinging || gdata->server_list.count == 0) return;
  gdata->server_list.ping_stop  = 0;
  gdata->server_list.pings_done = 0;
  gdata->server_list.pinging    = 1;
  for (int i = 0; i < gdata->server_list.count; i++) {
    gdata->server_list.pings[i]        = -1;
    gdata->server_list.sorted_order[i] = i;
  }
  pthread_t tid;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_create(&tid, &attr, sl_ping_thread, gdata);
  pthread_attr_destroy(&attr);
}
