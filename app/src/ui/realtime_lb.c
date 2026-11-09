#include "realtime_lb.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../constants.h"
#include "../external/mongoose.h"
#include "../network/server.h"
#include "../user.h"
#include "crystal_theme.h"

#ifdef ANDROID
#include "../android_path.h"
#endif

#ifndef IM_COL32
#define IM_COL32(R, G, B, A) \
  crystal_tint_u32((((ImU32)(A) << 24) | ((ImU32)(B) << 16) | ((ImU32)(G) << 8) | ((ImU32)(R))))
#endif

/* From network/callback.c */
void decode_secret(const uint8_t *packet, size_t packet_len, uint8_t *result);

#define RT_MAX_SERVERS 200
#define RT_TOP 10
#define RT_NAME_LEN 28
#define RT_WORKERS 10          /* servers visited at the same time        */
#define RT_VISIT_TIMEOUT 9.0   /* seconds before a visit is given up      */
#define RT_ROUND_MIN 45.0      /* minimum seconds between full rounds     */
#define RT_STALE 240.0         /* hide a server not refreshed for this long */
#define RT_MAX_MSCPS 2040
#define RT_BOT_NICK "lb"

typedef struct {
  char ip[32];
  char cc[4];
  int players;
  int n;
  double updated; /* igGetTime() of the last visit that worked */
  char name[RT_TOP][RT_NAME_LEN];
  long score[RT_TOP];
} rt_server;

typedef struct {
  struct mg_connection *c;
  bool active;
  char ip[32];
  double start;
  bool have_tables;
  int mscps;
  float fm[RT_MAX_MSCPS + 2];
  float fp[RT_MAX_MSCPS + 2];
} rt_worker;

/* ------------------------------------------------------------------ state */
static struct mg_mgr s_mgr;
static bool s_running = false;
static rt_worker s_w[RT_WORKERS];

static char s_targets[RT_MAX_SERVERS][MAX_SERVER_IP_LEN + 1];
static int s_target_count = 0;
static int s_next_target = 0;
static double s_round_start = 0.0;
static int s_round_ok = 0;
static int s_round_done = 0;

static rt_server s_srv[RT_MAX_SERVERS];
static int s_srv_count = 0;

static char s_filter[48] = "";
static int s_return_screen = TITLE_SCREEN;

/* country lookup (ip-api.com, free, plain http) */
static char s_geo_ip[RT_MAX_SERVERS][24];
static char s_geo_cc[RT_MAX_SERVERS][4];
static int s_geo_count = 0;
static struct mg_connection *s_geo_conn = NULL;
static double s_geo_next = 0.0;

/* --------------------------------------------------------------------- geo */
#define GEO_FILE "rtlb_geo.txt"

static void geo_save(void) {
#ifdef ANDROID
  char path[600];
  android_build_path(path, sizeof(path), GEO_FILE);
  FILE *f = fopen(path, "w");
  if (!f) return;
  for (int i = 0; i < s_geo_count; i++)
    fprintf(f, "%s %s\n", s_geo_ip[i], s_geo_cc[i]);
  fclose(f);
#endif
}

static void geo_load(void) {
#ifdef ANDROID
  if (s_geo_count) return;
  char path[600];
  android_build_path(path, sizeof(path), GEO_FILE);
  FILE *f = fopen(path, "r");
  if (!f) return;
  char ip[64], cc[16];
  while (s_geo_count < RT_MAX_SERVERS && fscanf(f, "%63s %15s", ip, cc) == 2) {
    snprintf(s_geo_ip[s_geo_count], sizeof(s_geo_ip[0]), "%s", ip);
    snprintf(s_geo_cc[s_geo_count], sizeof(s_geo_cc[0]), "%s", cc);
    s_geo_count++;
  }
  fclose(f);
#endif
}

static const char *geo_lookup(const char *host_ip) {
  for (int i = 0; i < s_geo_count; i++)
    if (strcmp(s_geo_ip[i], host_ip) == 0) return s_geo_cc[i];
  return NULL;
}

static void host_of(const char *ip_port, char *out, size_t cap) {
  size_t i = 0;
  while (ip_port[i] && ip_port[i] != ':' && i + 1 < cap) {
    out[i] = ip_port[i];
    i++;
  }
  out[i] = 0;
}

static char s_geo_body[RT_MAX_SERVERS * 20];

static void geo_cb(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_CONNECT) {
    size_t len = strlen(s_geo_body);
    mg_printf(c,
              "POST /batch?fields=query,countryCode HTTP/1.1\r\n"
              "Host: ip-api.com\r\nContent-Type: application/json\r\n"
              "Content-Length: %d\r\nConnection: close\r\n\r\n%s",
              (int)len, s_geo_body);
  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;
    if (mg_http_status(hm) == 200) {
      for (int i = 0; i < 100; i++) {
        char p[32];
        snprintf(p, sizeof(p), "$[%d]", i);
        struct mg_str row = mg_json_get_tok(hm->body, p);
        if (row.buf == NULL) break;
        char *q = mg_json_get_str(row, "$.query");
        char *cc = mg_json_get_str(row, "$.countryCode");
        if (q && cc && s_geo_count < RT_MAX_SERVERS) {
          snprintf(s_geo_ip[s_geo_count], sizeof(s_geo_ip[0]), "%s", q);
          snprintf(s_geo_cc[s_geo_count], sizeof(s_geo_cc[0]), "%s", cc);
          s_geo_count++;
        }
        free(q);
        free(cc);
      }
      geo_save();
    }
    c->is_draining = 1;
  } else if (ev == MG_EV_CLOSE) {
    s_geo_conn = NULL;
  }
}

/* one request per call, 100 addresses at most; skips ones already known */
static void geo_tick(double now) {
  if (s_geo_conn || now < s_geo_next || s_target_count == 0) return;
  int n = 0;
  size_t pos = 0;
  s_geo_body[pos++] = '[';
  for (int i = 0; i < s_target_count && n < 100; i++) {
    char h[32];
    host_of(s_targets[i], h, sizeof(h));
    if (geo_lookup(h)) continue;
    int w = snprintf(s_geo_body + pos, sizeof(s_geo_body) - pos - 2, "%s\"%s\"",
                     n ? "," : "", h);
    if (w < 0) break;
    pos += (size_t)w;
    n++;
  }
  s_geo_body[pos++] = ']';
  s_geo_body[pos] = 0;
  s_geo_next = now + 6.0; /* free tier allows ~15 requests/minute */
  if (n == 0) return;
  s_geo_conn = mg_http_connect(&s_mgr, "http://ip-api.com", geo_cb, NULL);
}

/* ------------------------------------------------------------ result store */
static void store_result(rt_worker *w, int players, int n,
                         char names[][RT_NAME_LEN], const long *scores) {
  rt_server *s = NULL;
  for (int i = 0; i < s_srv_count; i++)
    if (strcmp(s_srv[i].ip, w->ip) == 0) {
      s = &s_srv[i];
      break;
    }
  if (!s) {
    if (s_srv_count >= RT_MAX_SERVERS) return;
    s = &s_srv[s_srv_count++];
    memset(s, 0, sizeof(*s));
    snprintf(s->ip, sizeof(s->ip), "%s", w->ip);
  }
  s->players = players;
  s->n = n;
  for (int i = 0; i < n; i++) {
    snprintf(s->name[i], RT_NAME_LEN, "%s", names[i]);
    s->score[i] = scores[i];
  }
  s->updated = igGetTime();
}

/* ----------------------------------------------------------- packet logic */
static void worker_finish(rt_worker *w) {
  if (w->c) w->c->is_closing = 1;
}

static void on_packet(rt_worker *w, const uint8_t *a, int len) {
  if (len < 1) return;
  struct mg_connection *c = w->c;
  char cmd = (char)a[0];

  if (cmd == '6') {
    uint8_t secret[27] = {0};
    decode_secret(a, (size_t)len, secret);
    mg_ws_send(c, secret, 27, WEBSOCKET_OP_BINARY);

    static const uint8_t cwa[20] = {54, 206, 204, 169, 97, 178, 74,  136, 124, 117,
                                    14, 210, 106, 236, 8,  208, 136, 213, 140, 111};
    const char *nick = RT_BOT_NICK;
    int nl = (int)strlen(nick);
    uint8_t ba[64];
    ba[0] = 115;
    ba[1] = 30;
    ba[2] = CLIENT_VERSION >> 8 & 255;
    ba[3] = CLIENT_VERSION & 255;
    memcpy(ba + 4, cwa, 20);
    ba[24] = (uint8_t)(rand() % 9);
    ba[25] = (uint8_t)nl;
    memcpy(ba + 26, nick, (size_t)nl);
    ba[26 + nl] = 0;
    ba[27 + nl] = NO_ACCESSORY;
    mg_ws_send(c, ba, (size_t)(28 + nl), WEBSOCKET_OP_BINARY);
  } else if (cmd == 'a') {
    if (len < 6) return;
    int ms = a[4] << 8 | a[5];
    if (ms <= 0 || ms > RT_MAX_MSCPS) return;
    w->mscps = ms;
    for (int i = 0; i <= ms; i++) {
      w->fm[i] = i >= ms ? w->fm[i - 1] : powf(1.0f - i / (float)ms, 2.25f);
      w->fp[i] = i == 0 ? 0.0f : w->fp[i - 1] + 1.0f / w->fm[i - 1];
    }
    w->have_tables = true;
  } else if (cmd == 'l' && w->have_tables) {
    if (len < 6) return;
    int m = 1;
    int lb_pos = a[m];
    m += 1;
    m += 2; /* our rank */
    int players = a[m] << 8 | a[m + 1];
    m += 2;

    char names[RT_TOP][RT_NAME_LEN];
    long scores[RT_TOP];
    int n = 0, pos = 0;
    while (m + 7 <= len) {
      pos++;
      int sct = a[m] << 8 | a[m + 1];
      m += 2;
      float fam = (a[m] << 16 | a[m + 1] << 8 | a[m + 2]) / 16777215.0f;
      m += 3;
      m += 1; /* skin */
      int nl = a[m];
      m += 1;
      if (m + nl > len) break;
      if (pos != lb_pos && n < RT_TOP) { /* skip our own bot */
        if (sct > w->mscps) sct = w->mscps;
        scores[n] = (long)floorf((w->fp[sct] + fam / w->fm[sct] - 1.0f) * 15.0f - 5.0f);
        int cp = nl < RT_NAME_LEN - 1 ? nl : RT_NAME_LEN - 1;
        memcpy(names[n], a + m, (size_t)cp);
        names[n][cp] = 0;
        n++;
      }
      m += nl;
    }
    if (n > 0) {
      store_result(w, players, n, names, scores);
      s_round_ok++;
      worker_finish(w);
    }
  }
}

static void ws_cb(struct mg_connection *c, int ev, void *ev_data) {
  rt_worker *w = (rt_worker *)c->fn_data;
  if (!w) return;
  if (ev == MG_EV_WS_OPEN) {
    mg_ws_send(c, (uint8_t[]){1}, 1, WEBSOCKET_OP_BINARY);
    mg_ws_send(c, (uint8_t[]){'c', 0}, 2, WEBSOCKET_OP_BINARY);
  } else if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message *msg = (struct mg_ws_message *)ev_data;
    const uint8_t *a = (const uint8_t *)msg->data.buf;
    int total = (int)msg->data.len;
    if (total < 1) return;
    if (a[0] < 32) { /* several packets in one message */
      int m = 0;
      while (m + 1 < total) {
        int len;
        if (a[m] < 32) {
          len = a[m] << 8 | a[m + 1];
          m += 2;
        } else {
          len = a[m] - 32;
          m += 1;
        }
        if (len <= 0 || m + len > total) break;
        on_packet(w, a + m, len);
        m += len;
        if (!w->c || w->c->is_closing) break;
      }
    } else {
      on_packet(w, a, total);
    }
  } else if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE) {
    if (ev == MG_EV_CLOSE) {
      w->c = NULL;
      w->active = false;
      s_round_done++;
    } else {
      c->is_closing = 1;
    }
  }
}

static void start_visit(rt_worker *w, const char *ip) {
  memset(w, 0, offsetof(rt_worker, mscps) + sizeof(int));
  snprintf(w->ip, sizeof(w->ip), "%s", ip);
  char url[96];
  snprintf(url, sizeof(url), "ws://%s/slither", ip);
  w->c = mg_ws_connect(&s_mgr, url, ws_cb, w, "Origin: https://slither.com\r\n");
  if (!w->c) {
    s_round_done++;
    return;
  }
  w->active = true;
  w->start = igGetTime();
}

/* --------------------------------------------------------------- scheduler */
static void collect_targets(tenv *env) {
  game_data *g = &env->usr->gdata;
  s_target_count = 0;
  for (int i = 0; i < g->server_list.count && s_target_count < RT_MAX_SERVERS; i++) {
    if (!g->server_list.ips[i][0]) continue;
    snprintf(s_targets[s_target_count++], sizeof(s_targets[0]), "%s",
             g->server_list.ips[i]);
  }
}

static void tick(tenv *env) {
  game_data *g = &env->usr->gdata;
  double now = igGetTime();

  mg_mgr_poll(&s_mgr, 0);
  server_list_poll(env); /* the list download is polled by the title screen */

  /* give up on visits that take too long */
  for (int i = 0; i < RT_WORKERS; i++)
    if (s_w[i].active && s_w[i].c && now - s_w[i].start > RT_VISIT_TIMEOUT)
      s_w[i].c->is_closing = 1;

  int active = 0;
  for (int i = 0; i < RT_WORKERS; i++)
    if (s_w[i].active) active++;

  if (s_target_count == 0) { /* need the list first */
    if (!g->server_list.fetching && !g->server_list.fetched) server_list_fetch(env);
    if (g->server_list.fetched) {
      collect_targets(env);
      s_next_target = 0;
      s_round_start = now;
    }
    return;
  }

  geo_tick(now);

  if (s_next_target >= s_target_count) {
    if (active == 0 && now - s_round_start >= RT_ROUND_MIN) {
      s_next_target = 0;
      s_round_start = now;
      s_round_ok = 0;
      s_round_done = 0;
    }
    return;
  }

  for (int i = 0; i < RT_WORKERS && s_next_target < s_target_count; i++) {
    if (s_w[i].active) continue;
    start_visit(&s_w[i], s_targets[s_next_target++]);
  }
}

/* ---------------------------------------------------------------- helpers */
static bool ci_contains(const char *hay, const char *needle) {
  size_t n = strlen(needle);
  if (!n) return true;
  for (; *hay; ++hay) {
    size_t k = 0;
    while (k < n && hay[k] &&
           tolower((unsigned char)hay[k]) == tolower((unsigned char)needle[k]))
      ++k;
    if (k == n) return true;
  }
  return false;
}

static const char *cc_of(const rt_server *s) {
  char h[32];
  host_of(s->ip, h, sizeof(h));
  const char *cc = geo_lookup(h);
  return cc ? cc : "";
}

static bool server_matches(const rt_server *s) {
  if (!s_filter[0]) return true;
  if (ci_contains(s->ip, s_filter) || ci_contains(cc_of(s), s_filter)) return true;
  for (int i = 0; i < s->n; i++)
    if (ci_contains(s->name[i], s_filter)) return true;
  return false;
}

static int cmp_srv(const void *x, const void *y) {
  const rt_server *a = &s_srv[*(const int *)x];
  const rt_server *b = &s_srv[*(const int *)y];
  long sa = a->n ? a->score[0] : 0, sb = b->n ? b->score[0] : 0;
  return sa < sb ? 1 : sa > sb ? -1 : 0;
}

static ImU32 rank_col(int i) {
  if (i == 0) return IM_COL32(255, 214, 90, 255);
  if (i == 1) return IM_COL32(205, 212, 225, 255);
  if (i == 2) return IM_COL32(222, 150, 96, 255);
  return IM_COL32(170, 165, 195, 255);
}

static void draw_card(ImDrawList *dl, const rt_server *s, ImVec2 p0, float w,
                      float lh, float pad) {
  float row = lh + 4.0f;
  float head = lh * 1.6f;
  ImVec2 p1 = {p0.x + w, p0.y + head + row * (float)s->n + pad};

  ImDrawList_AddRectFilled(dl, p0, p1, IM_COL32(26, 22, 44, 215), 10.0f, 0);
  ImDrawList_AddRect(dl, p0, p1, IM_COL32(165, 140, 240, 110), 10.0f, 0, 1.2f);

  float x = p0.x + pad;
  float y = p0.y + pad * 0.6f;
  const char *cc = cc_of(s);
  if (!cc[0]) cc = "..";
  ImVec2 cs;
  igCalcTextSize(&cs, cc, NULL, false, -1.0f);
  ImDrawList_AddRectFilled(dl, (ImVec2){x, y},
                           (ImVec2){x + cs.x + 14.0f, y + lh + 2.0f},
                           IM_COL32(88, 70, 170, 255), 6.0f, 0);
  ImDrawList_AddText_Vec2(dl, (ImVec2){x + 7.0f, y + 1.0f},
                          IM_COL32(255, 255, 255, 255), cc, NULL);
  ImDrawList_AddText_Vec2(dl, (ImVec2){x + cs.x + 24.0f, y + 1.0f},
                          IM_COL32(225, 220, 245, 255), s->ip, NULL);

  char pc[32];
  snprintf(pc, sizeof(pc), "%d players", s->players);
  ImVec2 ps;
  igCalcTextSize(&ps, pc, NULL, false, -1.0f);
  ImDrawList_AddText_Vec2(dl, (ImVec2){p1.x - pad - ps.x, y + 1.0f},
                          IM_COL32(150, 215, 170, 255), pc, NULL);

  ImDrawList_AddLine(dl, (ImVec2){p0.x + pad, p0.y + head - 4.0f},
                     (ImVec2){p1.x - pad, p0.y + head - 4.0f},
                     IM_COL32(165, 140, 240, 60), 1.0f);

  for (int i = 0; i < s->n; i++) {
    float ry = p0.y + head + row * (float)i;
    char rk[8];
    snprintf(rk, sizeof(rk), "%d", i + 1);
    ImDrawList_AddText_Vec2(dl, (ImVec2){p0.x + pad, ry}, rank_col(i), rk, NULL);

    char sc[24];
    snprintf(sc, sizeof(sc), "%ld", s->score[i]);
    ImVec2 ss;
    igCalcTextSize(&ss, sc, NULL, false, -1.0f);
    float score_x = p1.x - pad - ss.x;
    ImDrawList_AddText_Vec2(dl, (ImVec2){score_x, ry},
                            IM_COL32(255, 255, 255, 235), sc, NULL);

    float name_x = p0.x + pad + lh * 1.6f;
    ImDrawList_PushClipRect(dl, (ImVec2){name_x, ry},
                            (ImVec2){score_x - 8.0f, ry + row}, true);
    ImDrawList_AddText_Vec2(dl, (ImVec2){name_x, ry},
                            IM_COL32(215, 210, 238, 255),
                            s->name[i][0] ? s->name[i] : "-", NULL);
    ImDrawList_PopClipRect(dl);
  }
}

/* ------------------------------------------------------------------ screen */
void ui_realtime_lb_close(tenv *env) {
  (void)env;
  if (!s_running) return;
  /* frees every socket (WebSockets, geo lookup) in one go */
  mg_mgr_free(&s_mgr);
  s_running = false;
  s_geo_conn = NULL;
  memset(s_w, 0, sizeof(s_w));
  s_target_count = 0;
  s_next_target = 0;
}

void ui_realtime_lb_open(tenv *env) {
  game_data *gdata = &env->usr->gdata;
  if (gdata->curr_screen != REALTIME_LB) s_return_screen = gdata->curr_screen;
  if (!s_running) {
    mg_mgr_init(&s_mgr);
    s_running = true;
    memset(s_w, 0, sizeof(s_w));
    s_target_count = 0;
    s_next_target = 0;
    s_round_ok = s_round_done = 0;
    s_srv_count = 0; /* start clean, so nothing shown is old */
    geo_load();
  }
  gdata->curr_screen = REALTIME_LB;
}

void ui_realtime_lb(tenv *env) {
  tuser_data *usr = env->usr;
  game_data *gdata = &usr->gdata;
  tcontext *ctx = env->ctx;

  usr->r->global.bg_opacity = 0;
  usr->r->global.bd_opacity = 0;
  usr->r->global.minimap_opacity = 0;

  if (!s_running) ui_realtime_lb_open(env);
  tick(env);
  double now = igGetTime();

  igPushFont(usr->imgui_data.regular_font[usr->usrs.ui_font_size],
             usr->imgui_data.regular_font[usr->usrs.ui_font_size]->LegacySize);

  crystal_draw_background(env);
  crystal_push_theme();

  float sw = ctx->size[0];
  float sh = ctx->size[1];
  ImGuiStyle *style = igGetStyle();
  ImDrawList *bg = igGetWindowDrawList();

  const char *title = "Realtime Leaderboards";
  ImVec2 ts;
  igCalcTextSize(&ts, title, NULL, false, -1.0f);
  ImDrawList_AddText_Vec2(bg, (ImVec2){sw * 0.5f - ts.x * 0.5f, 18.0f},
                          IM_COL32(255, 255, 255, 235), title, NULL);

  {
    char st[160];
    if (s_target_count == 0)
      snprintf(st, sizeof(st), "Loading server list...");
    else
      snprintf(st, sizeof(st), "Running on this device  -  %d/%d servers this round",
               s_round_done > s_target_count ? s_target_count : s_round_done,
               s_target_count);
    ImVec2 ss;
    igCalcTextSize(&ss, st, NULL, false, -1.0f);
    ImDrawList_AddText_Vec2(bg, (ImVec2){sw * 0.5f - ss.x * 0.5f, 18.0f + ts.y + 4.0f},
                            IM_COL32(170, 165, 200, 220), st, NULL);
  }

  igSetCursorPos((ImVec2){sw - 300.0f - style->WindowPadding.x - 12.0f, 12.0f});
  igPushItemWidth(300.0f);
  igInputTextWithHint("##rtlb_filter", "Filter: country / ip / name", s_filter,
                      sizeof(s_filter), 0, NULL, NULL);
  igPopItemWidth();
  crystal_sheen();

  float list_top = 18.0f + ts.y * 2.0f + 22.0f;
  if (list_top < 84.0f) list_top = 84.0f;

  float btn_h = igGetFrameHeight() * 1.4f;
  float list_w = sw - 40.0f;
  float list_h = sh - list_top - btn_h - style->WindowPadding.y - 20.0f;
  igSetCursorPos((ImVec2){20.0f, list_top});
  igBeginChild_Str("##rtlb_list", (ImVec2){list_w, list_h}, ImGuiChildFlags_None,
                   ImGuiWindowFlags_None);
#ifdef ANDROID
  ui_touch_scroll(env, 26);
#endif

  ImDrawList *dl = igGetWindowDrawList();
  float lh = igGetTextLineHeight();
  float pad = 12.0f;
  float gap = 12.0f;
  int cols = list_w >= 1100.0f ? 2 : 1;
  float card_w = (list_w - gap * (float)(cols - 1) - 8.0f) / (float)cols;

  int order[RT_MAX_SERVERS];
  int visible = 0;
  for (int i = 0; i < s_srv_count; i++) {
    if (now - s_srv[i].updated > RT_STALE) continue;
    if (!server_matches(&s_srv[i])) continue;
    order[visible++] = i;
  }
  qsort(order, (size_t)visible, sizeof(int), cmp_srv);

  float col_y[2] = {0.0f, 0.0f};
  ImVec2 origin;
  igGetCursorScreenPos(&origin);
  for (int k = 0; k < visible; k++) {
    const rt_server *s = &s_srv[order[k]];
    int col = (cols == 2 && col_y[1] < col_y[0]) ? 1 : 0;
    ImVec2 p0 = {origin.x + (card_w + gap) * (float)col, origin.y + col_y[col]};
    draw_card(dl, s, p0, card_w, lh, pad);
    col_y[col] += lh * 1.6f + (lh + 4.0f) * (float)s->n + pad + gap;
  }

  if (visible == 0) {
    igTextWrapped(s_srv_count == 0
                      ? "Collecting... servers appear here as they answer. This "
                        "uses your internet (Wi-Fi recommended)."
                      : "No server matches the filter.");
  } else {
    float total_h = col_y[0] > col_y[1] ? col_y[0] : col_y[1];
    igSetCursorPosY(total_h + 4.0f);
    igDummy((ImVec2){1.0f, 1.0f});
  }
  igEndChild();

  float btn_w = 220.0f;
  igSetCursorPos(
      (ImVec2){sw * 0.5f - btn_w * 0.5f, sh - style->WindowPadding.y - btn_h});
  if (igButton("Back##rtlb_back", (ImVec2){btn_w, btn_h})) {
    ui_realtime_lb_close(env);
    gdata->curr_screen = s_return_screen == REALTIME_LB ? TITLE_SCREEN : s_return_screen;
  }
  crystal_sheen();

  crystal_pop_theme();
  igPopFont();
}
