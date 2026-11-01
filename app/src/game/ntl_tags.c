#include "ntl_tags.h"
#include "../user.h"
#include "../external/mongoose.h"
#include "ntl_tags_data.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ANDROID
#include "../android_path.h"
#endif

/* ---- constants ported from the NTL mod (main-mt.js) -------------------- */
#define NT_BUILTIN_COUNT 60   /* ids 0..59 are the built-in t_N tags      */
#define NT_FST_BASE 200       /* public fstag n  -> id 200+n   (n < 168)  */
#define NT_FST_BASE2 500      /* public fstag n  -> id 500+n   (n >= 168) */
#define NT_FST_SPLIT 168
#define NT_ID_MAX 756         /* every valid id is < this                 */
#define NT_PTS 10             /* points of the antenna                    */
#define NT_SEG 4.0f           /* max segment length (x snake scale)       */
#define NT_TICK 0.016667      /* the antenna is simulated at 60 Hz        */
#define NT_PULL 3.3332f       /* NTL: target distance per tick            */
#define NT_SPRING 0.08333f
#define NT_DAMP 0.838f
#define NT_TAG_SCALE 0.285f   /* NTL: BI[].y6                             */
#define NT_SLOTS 96

#define NT_WS_URL "ws://ws.ntl-slither.com:9000"
#define NT_CFG_FILE "ntl_tags.cfg"
#define NT_CFG_MAGIC 0x3147544Eu /* "NTG1" */

/* ---- state -------------------------------------------------------------- */
typedef struct {
  int id;
  bool shown;
  double t, last_use;
  float x[NT_PTS], y[NT_PTS], vx[NT_PTS], vy[NT_PTS];
  float atba; /* rotation of the tag picture */
} ant_t;

static ant_t s_ants[NT_SLOTS];
static int16_t s_index[NT_ID_MAX];        /* tag id -> index in ntl_tag_defs */
static unsigned long long s_tex = 0;      /* atlas descriptor set            */

static bool s_show = true;                /* "Show NTL tags"                 */
static int s_sel = NTL_TAG_NONE;          /* our own tag id                  */
static bool s_picker_open = false;
static bool s_assist_hide = false;        /* "ASSIST HIDE TAGS"            */

/* NTL tag feed */
static struct mg_mgr s_mgr;
static bool s_mgr_ready = false;
static struct mg_connection *s_conn = NULL;
static bool s_ws_open = false, s_registered = false;
static double s_next_connect = 0.0, s_next_send = 0.0;
static int16_t s_map_val[65536];
static uint16_t s_map_gen[65536];
static uint16_t s_cur_gen = 0;

static double now_s(void) { return mg_millis() / 1000.0; }

/* ---- tag table ---------------------------------------------------------- */
static void build_index(void) {
  for (int i = 0; i < NT_ID_MAX; ++i) s_index[i] = -1;
  for (int i = 0; i < NTL_TAG_COUNT; ++i) {
    int id = ntl_tag_defs[i].id;
    if (id >= 0 && id < NT_ID_MAX) s_index[id] = (int16_t)i;
  }
}

bool ntl_tags_valid(int tag_id) {
  return tag_id >= 0 && tag_id < NT_ID_MAX && s_index[tag_id] >= 0;
}

void ntl_tags_set_texture(unsigned long long texture_id) { s_tex = texture_id; }

/* ---- settings file ------------------------------------------------------ */
typedef struct {
  uint32_t magic;
  int32_t selected;
  uint8_t show;
  uint8_t assist_hide; /* was padding: old files read as 0 = off */
  uint8_t pad[2];
} cfg_file;

static FILE *open_cfg(const char *mode) {
#ifdef ANDROID
  char path[512];
  android_build_path(path, sizeof path, NT_CFG_FILE);
  return fopen(path, mode);
#else
  return fopen(NT_CFG_FILE, mode);
#endif
}

static void cfg_load(void) {
  FILE *f = open_cfg("rb");
  if (!f) return;
  cfg_file c;
  size_t got = fread(&c, 1, sizeof c, f);
  fclose(f);
  if (got != sizeof c || c.magic != NT_CFG_MAGIC) return;
  s_show = c.show != 0;
  s_assist_hide = c.assist_hide != 0;
  s_sel = ntl_tags_valid(c.selected) ? c.selected : NTL_TAG_NONE;
}

static void cfg_save(void) {
  FILE *f = open_cfg("wb");
  if (!f) return;
  cfg_file c = {NT_CFG_MAGIC, s_sel, (uint8_t)(s_show ? 1 : 0),
                (uint8_t)(s_assist_hide ? 1 : 0), {0, 0}};
  fwrite(&c, 1, sizeof c, f);
  fclose(f);
}

bool ntl_tags_show_enabled(void) { return s_show; }
int ntl_tags_selected(void) { return s_sel; }

/* ---- spawn-packet skin header (NTL "e" array) --------------------------- */
bool ntl_tags_encode_header(uint8_t hdr[8]) {
  if (!ntl_tags_valid(s_sel)) return false;
  if (s_sel < NT_BUILTIN_COUNT) {
    hdr[0] = 254; /* built-in tag marker */
    hdr[1] = 18;
    hdr[6] = (uint8_t)s_sel;
  } else {
    hdr[0] = 255;
    hdr[1] = 38; /* public fstag */
    hdr[6] = (uint8_t)(s_sel < NT_FST_BASE2 ? s_sel - NT_FST_BASE
                                            : s_sel - NT_FST_BASE2);
  }
  return true;
}

int ntl_tags_decode_header(const uint8_t *h, int len, int *plain_skin) {
  if (plain_skin) *plain_skin = -1;
  if (!h || len < 8) return NTL_TAG_NONE;
  int tag = NTL_TAG_NONE;
  if (h[1] == 18) {
    if (h[0] == 254 && h[6] < NT_BUILTIN_COUNT) tag = h[6];
  } else if (h[1] == 38) {
    int idx = h[6];
    int id = idx < NT_FST_SPLIT ? idx + NT_FST_BASE : idx + NT_FST_BASE2;
    if (ntl_tags_valid(id)) tag = id;
  } else {
    return NTL_TAG_NONE;
  }
  if (plain_skin && h[2] != 255) *plain_skin = h[2];
  return tag;
}

/* ---- NTL tag feed (ws://ws.ntl-slither.com:9000) ------------------------ */
static void ws_callback(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_WS_OPEN) {
    s_ws_open = true;
    s_registered = false;
    s_next_send = 0.0;
  } else if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message *m = (struct mg_ws_message *)ev_data;
    const uint8_t *d = (const uint8_t *)m->data.buf;
    size_t n = m->data.len;
    /* [ntlid hi, ntlid lo, tag hi, tag lo] x N ; tag 0xFFFF = none */
    if (!d || n < 4) return;
    if (++s_cur_gen == 0) {
      memset(s_map_gen, 0, sizeof s_map_gen);
      s_cur_gen = 1;
    }
    for (size_t i = 0; i + 4 <= n; i += 4) {
      unsigned id = (unsigned)(d[i] << 8 | d[i + 1]);
      unsigned tg = (unsigned)(d[i + 2] << 8 | d[i + 3]);
      s_map_val[id] = tg == 65535u ? (int16_t)-1 : (int16_t)tg;
      s_map_gen[id] = s_cur_gen;
    }
  } else if (ev == MG_EV_CLOSE) {
    if (c == s_conn) s_conn = NULL;
    s_ws_open = false;
    s_registered = false;
  }
}

static size_t json_str(char *out, size_t cap, size_t n, const char *s) {
  out[n++] = '"';
  for (; s && *s && n + 8 < cap; ++s) {
    unsigned char ch = (unsigned char)*s;
    if (ch == '"' || ch == '\\') {
      out[n++] = '\\';
      out[n++] = (char)ch;
    } else if (ch >= 0x20) {
      out[n++] = (char)ch;
    }
  }
  out[n++] = '"';
  return n;
}

static snake *local_snake(game_data *g) {
  int n = tdarray_length(g->data.snakes);
  for (int i = 0; i < n; ++i)
    if (g->data.snakes[i].id == g->data.snake_id) return g->data.snakes + i;
  return NULL;
}

static void ws_close(void) {
  if (s_conn) {
    s_conn->is_closing = true;
    s_conn = NULL;
  }
  s_ws_open = false;
  s_registered = false;
}

static void ws_send_position(user_settings *us, snake *me) {
  char buf[512];
  size_t n = 0;
  if (!s_registered) {
    /* [nick, "ip:port", tagToken, tagId, skin, x, y, ntlid] -- same as the
       NTL mod. Free tags travel in the spawn packet, so no token / id. */
    buf[n++] = '[';
    n = json_str(buf, sizeof buf, n, us->nickname);
    buf[n++] = ',';
    n = json_str(buf, sizeof buf, n, us->ipv4);
    n += (size_t)snprintf(buf + n, sizeof buf - n, ",\"\",-1,%d,%d,%d,%d]",
                          us->custom_skin ? 0 : (int)us->default_skin,
                          (int)lroundf(me->xx), (int)lroundf(me->yy),
                          me->ntlid);
    s_registered = true;
  } else {
    n = (size_t)snprintf(buf, sizeof buf, "[%d,%d]", (int)lroundf(me->xx),
                         (int)lroundf(me->yy));
  }
  if (n > 0 && n < sizeof buf)
    mg_ws_send(s_conn, buf, n, WEBSOCKET_OP_TEXT);
}

/* ---- lifecycle ---------------------------------------------------------- */
void ntl_tags_init(tenv *env) {
  (void)env;
  build_index();
  cfg_load();
  memset(s_ants, 0, sizeof s_ants);
  memset(s_map_gen, 0, sizeof s_map_gen);
  s_cur_gen = 0;
  mg_mgr_init(&s_mgr);
  s_mgr_ready = true;
}

void ntl_tags_destroy(tenv *env) {
  (void)env;
  ws_close();
  if (s_mgr_ready) {
    mg_mgr_poll(&s_mgr, 0);
    mg_mgr_free(&s_mgr);
    s_mgr_ready = false;
  }
}

void ntl_tags_update(tenv *env) {
  if (!s_mgr_ready || !env || !env->usr) return;
  game_data *g = &env->usr->gdata;
  user_settings *us = &env->usr->usrs;
  double now = now_s();

  mg_mgr_poll(&s_mgr, 0);

  bool playing = g->conn == CONNECTED && g->curr_screen == PLAYING &&
                 !g->data.dead && g->data.snake_id >= 0;
  snake *me = playing ? local_snake(g) : NULL;
  if (!me) {
    if (s_conn || s_ws_open) ws_close();
    return;
  }

  /* connect / report position once a second, like the NTL mod */
  if (!s_conn && now >= s_next_connect) {
    s_next_connect = now + 5.0;
    s_conn = mg_ws_connect(&s_mgr, NT_WS_URL, ws_callback, NULL,
                           "Origin: https://slither.io\r\n");
  }
  if (s_conn && s_ws_open && now >= s_next_send) {
    s_next_send = now + 1.0;
    ws_send_position(us, me);
  }

  /* hand tags to snakes: ours from the picker, the others from the feed */
  int n = tdarray_length(g->data.snakes);
  for (int i = 0; i < n; ++i) {
    snake *o = g->data.snakes + i;
    if (o->id < 0) continue;
    if (o == me) {
      o->ntl_tag1 = (int16_t)(ntl_tags_valid(s_sel) ? s_sel + 1 : 0);
      continue;
    }
    unsigned nid = (unsigned)o->ntlid & 0xFFFFu;
    if (s_cur_gen && s_map_gen[nid] == s_cur_gen) {
      int v = s_map_val[nid];
      if (v >= 0 && ntl_tags_valid(v)) o->ntl_tag1 = (int16_t)(v + 1);
    }
  }
}

/* ---- drawing ------------------------------------------------------------ */
static ant_t *ant_get(int id, double now) {
  ant_t *free_slot = NULL, *oldest = &s_ants[0];
  for (int i = 0; i < NT_SLOTS; ++i) {
    ant_t *a = &s_ants[i];
    if (a->id == id && a->last_use > 0.0) {
      if (now - a->last_use > 0.5) a->shown = false; /* was off screen */
      return a;
    }
    if (a->last_use == 0.0 || now - a->last_use > 2.0) {
      if (!free_slot) free_slot = a;
    }
    if (a->last_use < oldest->last_use) oldest = a;
  }
  ant_t *a = free_slot ? free_slot : oldest;
  memset(a, 0, sizeof *a);
  a->id = id;
  return a;
}

static ImU32 pack(unsigned rgb, float alpha) {
  if (alpha < 0) alpha = 0;
  if (alpha > 1) alpha = 1;
  return ((ImU32)(alpha * 255.0f) << 24) | ((rgb & 0xFF) << 16) |
         (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
}

/* Quadratic bezier a->b (control c) appended to pts, 4 steps. */
static int quad(ImVec2 *pts, int n, ImVec2 a, ImVec2 c, ImVec2 b) {
  for (int s = 1; s <= 4; ++s) {
    float t = s / 4.0f, u = 1 - t;
    pts[n++] = (ImVec2){u * u * a.x + 2 * u * t * c.x + t * t * b.x,
                        u * u * a.y + 2 * u * t * c.y + t * t * b.y};
  }
  return n;
}

void ntl_tags_draw_snake(tenv *env, snake *o, float hx, float hy,
                         float screen_cx, float screen_cy, float alive) {
  if (!s_show || !s_tex || !o || o->ntl_tag1 <= 0) return;
  if (s_assist_hide && env->usr->usrs.hotkeys[HOTKEY_ASSIST].active) return;
  int tid = o->ntl_tag1 - 1;
  if (!ntl_tags_valid(tid)) return;
  const ntl_tag_def *def = &ntl_tag_defs[s_index[tid]];
  game_data *g = &env->usr->gdata;
  float gsc = g->data.gsc, sc = o->sc;
  float A = sc * gsc;
  double now = now_s();

  ant_t *a = ant_get(o->id, now);
  float cs = cosf(o->ang), sn = sinf(o->ang);
  a->x[0] = hx - 8.0f * cs * sc;
  a->y[0] = hy - 8.0f * sn * sc;

  if (!a->shown) { /* lay the antenna out straight behind the head */
    for (int i = 1; i < NT_PTS; ++i) {
      a->x[i] = a->x[0] - cs * i * NT_SEG * sc;
      a->y[i] = a->y[0] - sn * i * NT_SEG * sc;
      a->vx[i] = a->vy[i] = 0;
    }
    a->t = now;
    a->atba = o->ang + (float)M_PI;
    a->shown = true;
  }

  /* physics: NTL's fixed 60 Hz spring chain, at most 4 ticks per frame */
  double dt = now - a->t;
  if (dt > 0.25) dt = 0.25;
  int ticks = (int)(dt / NT_TICK);
  if (ticks > 4) ticks = 4;
  a->t += ticks * NT_TICK;
  float maxlen = NT_SEG * sc;
  for (int k = 0; k < ticks; ++k) {
    for (int i = 1; i < NT_PTS; ++i) {
      float dx = a->x[i] - a->x[i - 1], dy = a->y[i] - a->y[i - 1];
      float z = atan2f(dy, dx);
      float tx = a->x[i - 1] + NT_PULL * cosf(z) * sc;
      float ty = a->y[i - 1] + NT_PULL * sinf(z) * sc;
      a->vx[i] += NT_SPRING * (tx - a->x[i]);
      a->vy[i] += NT_SPRING * (ty - a->y[i]);
      a->x[i] += a->vx[i];
      a->y[i] += a->vy[i];
      a->vx[i] *= NT_DAMP;
      a->vy[i] *= NT_DAMP;
      dx = a->x[i] - a->x[i - 1];
      dy = a->y[i] - a->y[i - 1];
      if (sqrtf(dx * dx + dy * dy) > maxlen) {
        z = atan2f(dy, dx);
        a->x[i] = a->x[i - 1] + maxlen * cosf(z);
        a->y[i] = a->y[i - 1] + maxlen * sinf(z);
      }
    }
  }

  /* the tag turns towards the direction of the last segment */
  float frame_dt = (float)(now - a->last_use);
  if (a->last_use == 0.0 || frame_dt > 0.1f) frame_dt = 0.0167f;
  float want = atan2f(a->y[NT_PTS - 1] - a->y[NT_PTS - 2],
                      a->x[NT_PTS - 1] - a->x[NT_PTS - 2]);
  float df = want - a->atba;
  while (df > (float)M_PI) df -= 2 * (float)M_PI;
  while (df < -(float)M_PI) df += 2 * (float)M_PI;
  a->atba += (1.0f - powf(0.85f, frame_dt / 0.016667f)) * df;
  a->last_use = now;

  float alpha = alive * alive;
  if (alpha <= 0.01f) return;

  ImDrawList *dl = igGetWindowDrawList();
  ImVec2 sp[NT_PTS];
  for (int i = 0; i < NT_PTS; ++i)
    sp[i] = (ImVec2){screen_cx + (a->x[i] - g->data.view_xx) * gsc,
                     screen_cy + (a->y[i] - g->data.view_yy) * gsc};

  /* antenna line: tip -> head, smoothed through the points like NTL */
  ImVec2 path[4 * NT_PTS + 4];
  int pn = 0;
  path[pn++] = sp[NT_PTS - 1];
  ImVec2 cur = sp[NT_PTS - 1];
  for (int i = NT_PTS - 2; i >= 1; --i) {
    ImVec2 mid = {(sp[i].x + sp[i - 1].x) * 0.5f, (sp[i].y + sp[i - 1].y) * 0.5f};
    pn = quad(path, pn, cur, sp[i], mid);
    cur = mid;
  }
  pn = quad(path, pn, cur, sp[1], sp[0]);
  ImDrawList_AddPolyline(dl, path, pn, pack(def->c1, alpha), 0, 5.0f * A);
  ImU32 inner = pack(def->c2, 0.5f * alpha);
  ImDrawList_AddPolyline(dl, path, pn, inner, 0, 4.0f * A);
  ImDrawList_AddPolyline(dl, path, pn, inner, 0, 3.0f * A); /* NTL "atwg" glow */
  ImDrawList_AddPolyline(dl, path, pn, inner, 0, 2.0f * A);

  /* tag picture at the tip, rotated with the antenna */
  float k = NT_TAG_SCALE * A;
  float V = def->h * k, cap = 40.6f * A;
  float Z = V > cap ? cap / V : 1.0f;
  float rx = def->bx * k * Z, ry = def->by * k * Z;
  float rw = def->w * k * Z, rh = def->h * k * Z;
  float c = cosf(a->atba), s = sinf(a->atba);
  ImVec2 tip = sp[NT_PTS - 1];
  float lx[4] = {rx, rx + rw, rx + rw, rx};
  float ly[4] = {ry, ry, ry + rh, ry + rh};
  ImVec2 q[4];
  for (int i = 0; i < 4; ++i)
    q[i] = (ImVec2){tip.x + lx[i] * c - ly[i] * s, tip.y + lx[i] * s + ly[i] * c};
  float u0 = (float)def->ax / NTL_TAG_ATLAS_W, v0 = (float)def->ay / NTL_TAG_ATLAS_H;
  float u1 = (float)(def->ax + def->aw) / NTL_TAG_ATLAS_W;
  float v1 = (float)(def->ay + def->ah) / NTL_TAG_ATLAS_H;
  ImTextureRef tex = {NULL, (ImTextureID)s_tex};
  ImDrawList_AddImageQuad(dl, tex, q[0], q[1], q[2], q[3], (ImVec2){u0, v0},
                          (ImVec2){u1, v0}, (ImVec2){u1, v1}, (ImVec2){u0, v1},
                          pack(0xFFFFFF, alpha));
}

/* ---- settings UI -------------------------------------------------------- */
static void thumb(ImDrawList *dl, const ntl_tag_def *d, ImVec2 a, ImVec2 b) {
  if (!s_tex) return;
  float cw = b.x - a.x, ch = b.y - a.y;
  float sc = fminf(cw / d->aw, ch / d->ah);
  float w = d->aw * sc, h = d->ah * sc;
  ImVec2 p0 = {a.x + (cw - w) * 0.5f, a.y + (ch - h) * 0.5f};
  ImVec2 p1 = {p0.x + w, p0.y + h};
  ImTextureRef tex = {NULL, (ImTextureID)s_tex};
  ImDrawList_AddImage(dl, tex, p0, p1,
                      (ImVec2){(float)d->ax / NTL_TAG_ATLAS_W, (float)d->ay / NTL_TAG_ATLAS_H},
                      (ImVec2){(float)(d->ax + d->aw) / NTL_TAG_ATLAS_W,
                               (float)(d->ay + d->ah) / NTL_TAG_ATLAS_H},
                      0xFFFFFFFFu);
}

void ntl_tags_assist_settings_ui(void) {
  bool v = s_assist_hide;
  if (igCheckbox("ASSIST HIDE TAGS", &v)) {
    s_assist_hide = v;
    cfg_save();
  }
  igTextDisabled("Hides all NTL tags while Assist is on.");
}

void ntl_tags_settings_ui(tenv *env) {
  (void)env;
  igSeparatorText("NTL tags");
  bool show = s_show;
  if (igCheckbox("Show NTL tags on snakes", &show)) {
    s_show = show;
    cfg_save();
  }
  igTextWrapped("Your tag is sent when you spawn, so a new choice shows from "
                "your next life. Tags of other NTL players appear on their "
                "snakes automatically.");

  if (ntl_tags_valid(s_sel))
    igText("Your tag: #%d", s_sel);
  else
    igText("Your tag: none");
  igSameLine(0, -1);
  if (igButton(s_picker_open ? "Hide tag list" : "Choose tag", (ImVec2){0, 0}))
    s_picker_open = !s_picker_open;
  if (ntl_tags_valid(s_sel)) {
    igSameLine(0, -1);
    if (igButton("Remove my tag", (ImVec2){0, 0})) {
      s_sel = NTL_TAG_NONE;
      cfg_save();
    }
  }
  if (!s_picker_open) return;

  float cell = igGetFontSize() * 3.6f;
  ImGuiStyle *style = igGetStyle();
  float gap = style->ItemSpacing.x;
  ImVec2 av;
  igGetContentRegionAvail(&av);
  int cols = (int)((av.x + gap) / (cell + gap));
  if (cols < 1) cols = 1;
  for (int i = 0; i < NTL_TAG_COUNT; ++i) {
    const ntl_tag_def *d = &ntl_tag_defs[i];
    if (i % cols) igSameLine(0, gap);
    igPushID_Int(d->id);
    bool pressed = igInvisibleButton("##tag", (ImVec2){cell, cell}, 0);
    ImVec2 a, b;
    igGetItemRectMin(&a);
    igGetItemRectMax(&b);
    ImDrawList *dl = igGetWindowDrawList();
    bool sel = d->id == s_sel;
    ImDrawList_AddRectFilled(dl, a, b, sel ? 0x60FFB020u : 0x40000000u, 6.0f, 0);
    if (igIsItemHovered(ImGuiHoveredFlags_None))
      ImDrawList_AddRect(dl, a, b, 0xC0FFFFFFu, 6.0f, 0, 1.5f);
    thumb(dl, d, (ImVec2){a.x + 3, a.y + 3}, (ImVec2){b.x - 3, b.y - 3});
    if (pressed) {
      s_sel = d->id;
      cfg_save();
    }
    igPopID();
  }
}
