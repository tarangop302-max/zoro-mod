#include "ntl_tags.h"
#include "../user.h"
#include "../external/mongoose.h"
#include "../rendering/texture.h"
#include "../ui/crystal_theme.h"
#include "ntl_tags_data.h"
#include "webp_decode.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifdef ANDROID
#include "../android_path.h"
#include "../android_jni.h"
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
#define NT_AUTH_FILE "ntl_tags_auth.cfg"       /* "id md5(password)" lines   */
#define NT_CACHE_FILE "ntl_tags_cache.json"    /* last community tag list    */
#define NT_FCACHE_FILE "ntl_fstags_cache.json" /* last free-tag list         */
#define NT_HOST "https://ntl-slither.com"
#define NT_UPLOAD_URL "https://ntl-slither.com/uploadtag.php?ntlmod=wbst"
#define NT_MOD_VERSION "9.78"                  /* modv the NTL server expects */

/* ---- community ("custom") tags: uploaded through NTL's site -------------
 * Ids 368..666 and >= 756 (NTL main-mt.js). Their pictures are not bundled:
 * they come from tags/cstags (list, picture inline) or newtag.php?id=N. */
#define NT_COMM_MAX 600
#define NT_CATLAS_W 2048
#define NT_CATLAS_H 2048
#define NT_CPAD 2
#define NT_MAX_AUTH 64
#define NT_IMG_RETRY_S 120.0
#define NT_CFG_MAGIC 0x3247544Eu /* "NTG2": assist byte = show tags in Assist */
#define NT_CFG_MAGIC_V1 0x3147544Eu /* "NTG1": old file, assist byte = hide */

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

static bool s_show_normal = true;         /* "Show NTL tags", Normal mode    */
static bool s_show_assist = false;        /* "Show NTL tags", Assist mode (off by default) */
static int s_sel = NTL_TAG_NONE;          /* our own tag id                  */

/* community tag store */
enum { CT_META = 0, CT_NEED_IMG, CT_HAVE_WEBP, CT_READY, CT_FAILED };
typedef struct {
  ntl_tag_def def; /* ax/ay/aw/ah valid once state == CT_READY            */
  uint8_t *webp;   /* downloaded, waiting to be decoded                   */
  size_t webp_n;
  uint8_t state;
  uint8_t meta_default; /* metadata guessed (only the id was known)       */
  uint8_t tries;        /* failed downloads so far (auto retry up to 3)   */
  double retry_at;
} ctag_t;
static ctag_t s_ct[NT_COMM_MAX];
static int s_nct = 0;
static int16_t s_cindex[65536];           /* tag id -> index in s_ct, -1     */
static bool s_cindex_init = false;
static uint8_t *s_catlas = NULL;          /* CPU copy of the atlas (RGBA)    */
static int s_sx = 0, s_sy = 0, s_sh = 0;  /* shelf packer                    */
static bool s_catlas_dirty = false;
static double s_catlas_next_upload = 0.0;
static texture *s_ctex = NULL;
static VkDescriptorSet s_cds = VK_NULL_HANDLE;
static unsigned long long s_ctex_id = 0;
static struct { texture *tex; VkDescriptorSet ds; int frames; } s_retire[6];
static int s_disp[NTL_TAG_COUNT + NT_COMM_MAX]; /* picker order: tag ids   */
static int s_ndisp = 0;
static bool s_disp_dirty = true;

/* network (one request at a time) */
typedef enum { RQ_NONE = 0, RQ_LIST, RQ_IMG, RQ_AUTH, RQ_FLIST } rq_kind;
static struct {
  struct mg_connection *conn;
  rq_kind kind;
  int id;
  double started;
  char path[200];
  char md5[33];
} s_rq;
static bool s_loaded = false;             /* cache read + first sync wanted  */
static bool s_list_want = false;
static double s_list_next = 0.0;
static int s_list_ver = 1;
static bool s_list_busy = false;
static bool s_auth_pending = false;
static int s_auth_tries = 0;
static double s_auth_retry_at = 0.0;
static int s_auth_id = 0;
static char s_auth_md5[33];
static char s_status[112];
static double s_status_until = 0.0;
static double s_last_ui = 0.0;
static int s_user_req = -1; /* tag id the user just asked for (Add ID) */

/* passwords the user unlocked community tags with (md5, like the NTL mod) */
static struct { int id; char md5[33]; } s_auth[NT_MAX_AUTH];
static int s_nauth = 0;

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

static void cfg_save(void);
/* ---- community tag store ------------------------------------------------ */
static void status_set(const char *msg) {
  snprintf(s_status, sizeof s_status, "%s", msg);
  s_status_until = now_s() + 5.0;
}

/* Valid custom-tag id range, exactly as the NTL mod filters them. */
static bool comm_id_ok(int id) {
  return id >= 368 && id != 667 && !(id > 667 && id < 756) && id < 65535;
}

/* Free ("public") tags the NTL server added after the ones bundled here. The
 * NTL mod downloads them from tags/fstags and appends them to its fstags list,
 * so entry g gets id 200+g (g < 168) or 500+g (g >= 168, i.e. 668..755), at
 * most 256 entries. They need no password, unlike uploaded tags (368+). */
#define NT_FST_MAX 256
static bool free_id_ok(int id) {
  bool in_range = (id >= NT_FST_BASE && id < NT_FST_BASE + NT_FST_SPLIT) ||
                  (id >= NT_FST_BASE2 + NT_FST_SPLIT && id < NT_FST_BASE2 + NT_FST_MAX);
  return in_range && !ntl_tags_valid(id);
}
/* Usable without a password: bundled, or a free tag from the server list. */
static bool tag_open(int id) { return ntl_tags_valid(id) || free_id_ok(id); }

static int ct_find(int id) {
  if (!s_cindex_init) return -1;
  if (id < 0 || id > 65534) return -1;
  return s_cindex[id];
}

static bool ctag_ready(int id) {
  int i = ct_find(id);
  return i >= 0 && s_ct[i].state == CT_READY;
}

bool ntl_tags_known(int tag_id) { return ntl_tags_valid(tag_id) || ctag_ready(tag_id); }
static bool ctag_ready_or_bundled(int id) { return ntl_tags_known(id); }

static int ct_add(int id) {
  if (!s_cindex_init) {
    for (int i = 0; i < 65536; ++i) s_cindex[i] = -1;
    s_cindex_init = true;
  }
  int i = ct_find(id);
  if (i >= 0) return i;
  if (s_nct >= NT_COMM_MAX || (!comm_id_ok(id) && !free_id_ok(id))) return -1;
  i = s_nct++;
  memset(&s_ct[i], 0, sizeof s_ct[i]);
  s_ct[i].def.id = (short)id;
  s_ct[i].state = CT_META;
  s_cindex[id] = (int16_t)i;
  s_disp_dirty = true;
  return i;
}

static int ct_cmp(const void *a, const void *b) {
  return ((const ctag_t *)a)->def.id - ((const ctag_t *)b)->def.id;
}
static void ct_sort(void) {
  qsort(s_ct, (size_t)s_nct, sizeof(ctag_t), ct_cmp);
  for (int i = 0; i < 65536; ++i) s_cindex[i] = -1;
  for (int i = 0; i < s_nct; ++i) s_cindex[(unsigned short)s_ct[i].def.id] = (int16_t)i;
  s_disp_dirty = true;
}

static void disp_rebuild(void) {
  s_ndisp = 0;
  for (int i = 0; i < NTL_TAG_COUNT; ++i) s_disp[s_ndisp++] = ntl_tag_defs[i].id;
  for (int i = 0; i < s_nct; ++i)
    if (s_ct[i].state == CT_READY) s_disp[s_ndisp++] = s_ct[i].def.id;
  s_disp_dirty = false;
}

/* Where a tag id's picture lives: the bundled atlas or the downloaded one. */
static bool resolve(int id, const ntl_tag_def **def, unsigned long long *tex,
                    int *atw, int *ath) {
  if (id >= 0 && id < NT_ID_MAX && s_index[id] >= 0) {
    if (!s_tex) return false;
    *def = &ntl_tag_defs[s_index[id]];
    *tex = s_tex;
    *atw = NTL_TAG_ATLAS_W;
    *ath = NTL_TAG_ATLAS_H;
    return true;
  }
  int i = ct_find(id);
  if (i < 0 || s_ct[i].state != CT_READY || !s_ctex_id) return false;
  *def = &s_ct[i].def;
  *tex = s_ctex_id;
  *atw = NT_CATLAS_W;
  *ath = NT_CATLAS_H;
  return true;
}

/* ---- auth (passwords of the user's own uploaded tags) ------------------- */
static FILE *open_named(const char *name, const char *mode) {
#ifdef ANDROID
  char path[512];
  android_build_path(path, sizeof path, name);
  return fopen(path, mode);
#else
  return fopen(name, mode);
#endif
}

static const char *auth_find(int id) {
  for (int i = 0; i < s_nauth; ++i)
    if (s_auth[i].id == id) return s_auth[i].md5;
  return NULL;
}

static void auth_save(void) {
  FILE *f = open_named(NT_AUTH_FILE, "wb");
  if (!f) return;
  for (int i = 0; i < s_nauth; ++i) fprintf(f, "%d %s\n", s_auth[i].id, s_auth[i].md5);
  fclose(f);
}

static void auth_load(void) {
  s_nauth = 0;
  FILE *f = open_named(NT_AUTH_FILE, "rb");
  if (!f) return;
  char line[96];
  while (s_nauth < NT_MAX_AUTH && fgets(line, sizeof line, f)) {
    int id;
    char md5[40];
    if (sscanf(line, "%d %32s", &id, md5) == 2 && strlen(md5) == 32) {
      s_auth[s_nauth].id = id;
      memcpy(s_auth[s_nauth].md5, md5, 33);
      s_nauth++;
    }
  }
  fclose(f);
}

static void auth_add(int id, const char *md5) {
  for (int i = 0; i < s_nauth; ++i)
    if (s_auth[i].id == id) {
      snprintf(s_auth[i].md5, 33, "%s", md5);
      auth_save();
      return;
    }
  if (s_nauth >= NT_MAX_AUTH) return;
  s_auth[s_nauth].id = id;
  snprintf(s_auth[s_nauth].md5, 33, "%s", md5);
  s_nauth++;
  auth_save();
}

static void md5_hex(const char *s, char out[33]) {
  mg_md5_ctx c;
  unsigned char d[16];
  mg_md5_init(&c);
  mg_md5_update(&c, (const unsigned char *)s, strlen(s));
  mg_md5_final(&c, d);
  for (int i = 0; i < 16; ++i) snprintf(out + i * 2, 3, "%02x", d[i]);
}

/* ---- picture atlas for the downloaded tags ----------------------------- */
static bool atlas_place(ctag_t *t, const uint8_t *rgba, int w, int h) {
  if (w < 1 || h < 1 || w > 700 || h > 700) return false;
  if (!s_catlas) {
    s_catlas = (uint8_t *)calloc((size_t)NT_CATLAS_W * NT_CATLAS_H, 4);
    if (!s_catlas) return false;
  }
  int pw = w + 2 * NT_CPAD, ph = h + 2 * NT_CPAD;
  if (s_sx + pw > NT_CATLAS_W) { /* next shelf */
    s_sy += s_sh;
    s_sx = 0;
    s_sh = 0;
  }
  if (s_sy + ph > NT_CATLAS_H) return false;
  int ox = s_sx + NT_CPAD, oy = s_sy + NT_CPAD;
  for (int y = -1; y <= h; ++y) {
    int sy = y < 0 ? 0 : y >= h ? h - 1 : y;
    uint8_t *drow = s_catlas + ((size_t)(oy + y) * NT_CATLAS_W + ox - 1) * 4;
    const uint8_t *srow = rgba + (size_t)sy * w * 4;
    memcpy(drow + 4, srow, (size_t)w * 4);
    memcpy(drow, srow, 4);                            /* left gutter pixel  */
    memcpy(drow + (size_t)(w + 1) * 4, srow + (size_t)(w - 1) * 4, 4); /* right */
  }
  s_sx += pw;
  if (ph > s_sh) s_sh = ph;
  t->def.ax = (short)ox;
  t->def.ay = (short)oy;
  t->def.aw = (short)w;
  t->def.ah = (short)h;
  return true;
}

static void retire_push(tenv *env, texture *tex, VkDescriptorSet ds) {
  (void)env;
  for (int i = 0; i < 6; ++i)
    if (!s_retire[i].tex) {
      s_retire[i].tex = tex;
      s_retire[i].ds = ds;
      s_retire[i].frames = 90;
      return;
    }
  /* queue full (should not happen): free the oldest right away */
  igImplVulkan_RemoveTexture(s_retire[0].ds);
  destroy_texture(env->ctx, s_retire[0].tex);
  s_retire[0].tex = tex;
  s_retire[0].ds = ds;
  s_retire[0].frames = 90;
}

static void retire_tick(tenv *env, bool flush) {
  for (int i = 0; i < 6; ++i) {
    if (!s_retire[i].tex) continue;
    if (flush || --s_retire[i].frames <= 0) {
      igImplVulkan_RemoveTexture(s_retire[i].ds);
      destroy_texture(env->ctx, s_retire[i].tex);
      s_retire[i].tex = NULL;
      s_retire[i].ds = VK_NULL_HANDLE;
    }
  }
}

static void atlas_upload(tenv *env) {
  if (!s_catlas) return;
  texture *tex = create_mipmap_texture_from_pixels(env->ctx, s_catlas, NT_CATLAS_W, NT_CATLAS_H);
  if (!tex) return;
  VkDescriptorSet ds = igImplVulkan_AddTexture(env->usr->r->linear_sampler, tex->view,
                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  if (s_ctex) retire_push(env, s_ctex, s_cds);
  s_ctex = tex;
  s_cds = ds;
  s_ctex_id = (unsigned long long)(uintptr_t)ds;
  s_catlas_dirty = false;
}

/* Decode a couple of queued pictures per frame so a big list never stalls. */
static void decode_some(void) {
  int budget = 2;
  for (int i = 0; i < s_nct && budget > 0; ++i) {
    ctag_t *t = &s_ct[i];
    if (t->state != CT_HAVE_WEBP) continue;
    budget--;
    uint8_t *rgba = NULL;
    int w = 0, h = 0;
    bool ok = webp_decode_rgba(t->webp, t->webp_n, &rgba, &w, &h);
    free(t->webp);
    t->webp = NULL;
    t->webp_n = 0;
    if (ok && atlas_place(t, rgba, w, h)) {
      t->state = CT_READY;
      if (t->meta_default) { /* only the id was known: centre the picture */
        t->def.w = (short)w;
        t->def.h = (short)h;
        t->def.bx = -32;
        t->def.by = (short)(-h / 2 - 20);
      }
      s_catlas_dirty = true;
      s_disp_dirty = true;
    } else {
      t->state = CT_FAILED;
    }
    free(rgba);
  }
}

/* ---- JSON helpers -------------------------------------------------------- */
static double jnum(struct mg_str v, const char *path, double dflt) {
  double x;
  if (mg_json_get_num(v, path, &x)) return x;
  char *s = mg_json_get_str(v, path);
  if (s) {
    dflt = atof(s);
    free(s);
  }
  return dflt;
}
static unsigned css_color(const char *s, unsigned dflt) {
  if (!strcasecmp(s, "red")) return 0xFF0000u;
  if (!strcasecmp(s, "yellow")) return 0xFFFF00u;
  if (!strcasecmp(s, "white")) return 0xFFFFFFu;
  if (!strcasecmp(s, "black")) return 0x000000u;
  const char *p = s[0] == '#' ? s + 1 : s;
  if (strlen(p) == 3) { /* #rgb -> #rrggbb */
    unsigned r = (unsigned)strtoul((char[]){p[0], 0}, NULL, 16);
    unsigned g = (unsigned)strtoul((char[]){p[1], 0}, NULL, 16);
    unsigned b = (unsigned)strtoul((char[]){p[2], 0}, NULL, 16);
    return (r * 17u << 16) | (g * 17u << 8) | (b * 17u);
  }
  if (!*p) return dflt;
  return (unsigned)strtoul(p, NULL, 16) & 0xFFFFFFu;
}
static unsigned jcolor(struct mg_str v, const char *path, unsigned dflt) {
  char *s = mg_json_get_str(v, path);
  if (s) {
    dflt = css_color(s, dflt);
    free(s);
    return dflt;
  }
  double x;
  if (mg_json_get_num(v, path, &x)) return (unsigned)x & 0xFFFFFFu;
  return dflt;
}

/* base64 text (optionally a data: URI) -> bytes */
static uint8_t *b64_to_bytes(const char *s, size_t n, size_t *out_n) {
  const char *comma = memchr(s, ',', n > 64 ? 64 : n);
  if (comma && n >= 5 && !strncmp(s, "data:", 5)) {
    n -= (size_t)(comma + 1 - s);
    s = comma + 1;
  }
  while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) n--;
  if (n < 8) return NULL;
  uint8_t *buf = (uint8_t *)malloc(n / 4 * 3 + 8);
  if (!buf) return NULL;
  size_t m = mg_base64_decode(s, n, (char *)buf, n / 4 * 3 + 8);
  if (m == 0) {
    free(buf);
    return NULL;
  }
  *out_n = m;
  return buf;
}

/* JSON string value -> malloc'ed C string. mongoose's own mg_json_get_str()
 * fails on "\/" (which PHP's json_encode writes for every "/" -- and base64
 * pictures are full of them), so every tag picture in the NTL list was lost. */
static char *json_str_value(struct mg_str json, const char *path) {
  int len = 0, off = mg_json_get(json, path, &len);
  if (off < 0 || len < 2 || json.buf[off] != '"') return NULL;
  char *r = (char *)malloc((size_t)len);
  if (!r) return NULL;
  const char *p = json.buf + off + 1;
  size_t n = (size_t)len - 2, j = 0;
  for (size_t i = 0; i < n; ++i) {
    char ch = p[i];
    if (ch == '\\' && i + 1 < n) {
      char e = p[++i];
      if (e == 'u' && i + 4 < n) {
        unsigned v = 0;
        for (int k = 1; k <= 4; ++k) {
          char h = p[i + k];
          v = v * 16 + (unsigned)(h >= '0' && h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
        }
        i += 4;
        ch = v < 256 ? (char)v : '?';
      } else if (e == 'n') ch = '\n';
      else if (e == 't') ch = '\t';
      else if (e == 'r') ch = '\r';
      else ch = e; /* \/ \" \\ */
    }
    r[j++] = ch;
  }
  r[j] = 0;
  return r;
}

/* Parse {"v":N,"d":[{id,w,h,c1,c2,bx,by,src},...]}. Returns number of tags. */
static int parse_list(const char *body, size_t n, bool from_cache) {
  struct mg_str json = mg_str_n(body, n);
  struct mg_str arr = mg_json_get_tok(json, "$.d");
  if (arr.len < 2 || arr.buf[0] != '[') return -1;
  long ver = mg_json_get_long(json, "$.v", 0);
  if (ver > 0 && !from_cache) s_list_ver = (int)ver;
  else if (ver > 0) s_list_ver = (int)ver;

  int count = 0, index = 0;
  size_t ofs = 0;
  struct mg_str key, val;
  while ((ofs = mg_json_next(arr, ofs, &key, &val)) > 0) {
    int O = index++;
    long raw_id = (long)jnum(val, "$.id", 0);
    int id = raw_id > 0 ? (int)raw_id : 580 - O;
    if (!(raw_id > 0) && id < 368) id = 1123 - id;
    if (!comm_id_ok(id)) continue;
    int ti = ct_add(id);
    if (ti < 0) continue;
    ctag_t *t = &s_ct[ti];
    t->def.w = (short)jnum(val, "$.w", t->def.w ? t->def.w : 140);
    t->def.h = (short)jnum(val, "$.h", t->def.h ? t->def.h : 140);
    t->def.bx = (short)jnum(val, "$.bx", -32);
    t->def.by = (short)jnum(val, "$.by", -70);
    t->def.c1 = jcolor(val, "$.c1", 0xFFFFFFu);
    t->def.c2 = jcolor(val, "$.c2", 0x92EFFEu);
    t->meta_default = 0;
    count++;
    if (t->state == CT_READY || t->state == CT_HAVE_WEBP) continue;
    char *src = json_str_value(val, "$.src");
    if (src) {
      size_t bn = 0;
      uint8_t *bytes = b64_to_bytes(src, strlen(src), &bn);
      free(src);
      if (bytes) {
        free(t->webp);
        t->webp = bytes;
        t->webp_n = bn;
        t->state = CT_HAVE_WEBP;
        continue;
      }
    }
    if (t->state != CT_FAILED || now_s() >= t->retry_at) t->state = CT_NEED_IMG; /* newtag.php */
  }
  ct_sort();
  return count;
}

/* Parse the free-tag list {"v":N,"d":[{fst_blbw,fst_blbh,fst_atc1,fst_atc2,
 * fst_blbx,fst_blby,fst_src},...]}. Entry k of "d" is entry (bundled + k) of
 * the NTL mod's fstags array (the mod concatenates the two). */
static bool s_flist_want = false, s_flist_busy = false;
static double s_flist_next = 0.0;
static int s_flist_ver = 0;

static int parse_fst_list(const char *body, size_t n) {
  struct mg_str json = mg_str_n(body, n);
  struct mg_str arr = mg_json_get_tok(json, "$.d");
  if (arr.len < 2 || arr.buf[0] != '[') return -1;
  long ver = mg_json_get_long(json, "$.v", 0);
  if (ver > 0) s_flist_ver = (int)ver;
  int count = 0, index = 0;
  size_t ofs = 0;
  struct mg_str key, val;
  while ((ofs = mg_json_next(arr, ofs, &key, &val)) > 0) {
    int g = (NTL_TAG_COUNT - NT_BUILTIN_COUNT) + index++;
    if (g >= NT_FST_MAX) break;
    int id = g < NT_FST_SPLIT ? NT_FST_BASE + g : NT_FST_BASE2 + g;
    if (!free_id_ok(id)) continue;
    int ti = ct_add(id);
    if (ti < 0) continue;
    ctag_t *t = &s_ct[ti];
    t->def.w = (short)jnum(val, "$.fst_blbw", 140);
    t->def.h = (short)jnum(val, "$.fst_blbh", 140);
    t->def.bx = (short)jnum(val, "$.fst_blbx", -32);
    t->def.by = (short)jnum(val, "$.fst_blby", -70);
    t->def.c1 = jcolor(val, "$.fst_atc1", 0xFFFFFFu);
    t->def.c2 = jcolor(val, "$.fst_atc2", 0x92EFFEu);
    t->meta_default = 0;
    count++;
    if (t->state == CT_READY || t->state == CT_HAVE_WEBP) continue;
    char *src = json_str_value(val, "$.fst_src");
    if (src) {
      size_t bn = 0;
      uint8_t *bytes = b64_to_bytes(src, strlen(src), &bn);
      free(src);
      if (bytes) {
        free(t->webp);
        t->webp = bytes;
        t->webp_n = bn;
        t->state = CT_HAVE_WEBP;
        continue;
      }
    }
    if (t->state != CT_FAILED || now_s() >= t->retry_at) t->state = CT_NEED_IMG; /* newtag.php */
  }
  ct_sort();
  return count;
}

static void fcache_save(const char *body, size_t n) {
  FILE *f = open_named(NT_FCACHE_FILE, "wb");
  if (!f) return;
  fwrite(body, 1, n, f);
  fclose(f);
}

static void fcache_load(void) {
  FILE *f = open_named(NT_FCACHE_FILE, "rb");
  if (!f) return;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n > 16 && n < 12 * 1024 * 1024) {
    char *buf = (char *)malloc((size_t)n + 1);
    if (buf) {
      size_t got = fread(buf, 1, (size_t)n, f);
      buf[got] = 0;
      parse_fst_list(buf, got);
      free(buf);
    }
  }
  fclose(f);
}

static void cache_save(const char *body, size_t n) {
  FILE *f = open_named(NT_CACHE_FILE, "wb");
  if (!f) return;
  fwrite(body, 1, n, f);
  fclose(f);
}

static void cache_load(void) {
  FILE *f = open_named(NT_CACHE_FILE, "rb");
  if (!f) return;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n > 16 && n < 12 * 1024 * 1024) {
    char *buf = (char *)malloc((size_t)n + 1);
    if (buf) {
      size_t got = fread(buf, 1, (size_t)n, f);
      buf[got] = 0;
      parse_list(buf, got, true);
      free(buf);
    }
  }
  fclose(f);
}

/* ---- HTTPS client (one request at a time, on the shared mongoose mgr) ---- */
/* What to do with a finished request (status + body) or a failed one. */
static void rq_on_response(int status, const char *body, size_t bn) {
    double now = now_s();
    if (s_rq.kind == RQ_LIST) {
      s_list_busy = false;
      s_list_next = now + 600.0;
      int cnt = status == 200 ? parse_list(body, bn, false) : -1;
      if (cnt >= 0) {
        cache_save(body, bn);
        char m[64];
        snprintf(m, sizeof m, "%d community tags synced", cnt);
        status_set(m);
      } else if (status == 200 && bn < 64) {
        status_set("Tag list is up to date");
      } else {
        status_set("Could not read the NTL tag list");
        s_list_next = now + 30.0;
      }
    } else if (s_rq.kind == RQ_FLIST) {
      s_flist_busy = false;
      s_flist_next = now + 600.0;
      int cnt = status == 200 ? parse_fst_list(body, bn) : -1;
      if (cnt > 0) fcache_save(body, bn);
      if (cnt < 0 && !(status == 200 && bn < 64)) s_flist_next = now + 30.0;
      /* always say what came back, so a missing free tag can be diagnosed */
      char m[112];
      snprintf(m, sizeof m, "Free tags: HTTP %d, %u bytes, %d tags (v%d)", status, (unsigned)bn,
               cnt < 0 ? 0 : cnt, s_flist_ver);
      status_set(m);
    } else if (s_rq.kind == RQ_IMG) {
      int i = ct_find(s_rq.id);
      if (i >= 0) {
        ctag_t *t = &s_ct[i];
        size_t wn = 0;
        uint8_t *bytes = (status == 200 && bn > 16 && body[0] != '{' && body[0] != '<')
                             ? b64_to_bytes(body, bn, &wn)
                             : NULL;
        if (bytes) {
          free(t->webp);
          t->webp = bytes;
          t->webp_n = wn;
          t->state = CT_HAVE_WEBP;
        } else {
          t->state = CT_FAILED;
          t->tries = 3; /* the server answered: it really has no such tag */
          t->retry_at = now + NT_IMG_RETRY_S;
          if (t->def.id == s_user_req) {
            char m[64];
            snprintf(m, sizeof m, "Tag #%d was not found on the server", t->def.id);
            status_set(m);
          }
        }
      }
    } else if (s_rq.kind == RQ_AUTH) {
      bool ok = status == 200 && bn >= 2 && !strncmp(body, "ok", 2);
      if (ok) {
        auth_add(s_rq.id, s_rq.md5);
        s_sel = s_rq.id;
        cfg_save();
        status_set("Tag unlocked - announcing it to NTL players");
      } else {
        char m[100];
        size_t k = bn < 60 ? bn : 60;
        snprintf(m, sizeof m, "Wrong tag ID or password%s%.*s", k ? ": " : "", (int)k, k ? body : "");
        status_set(m);
      }
    }
  s_rq.kind = RQ_NONE;
  s_rq.conn = NULL;
}

static void rq_on_failure(const char *why) {
    rq_kind k = s_rq.kind;
    int id = s_rq.id;
    s_rq.kind = RQ_NONE;
    s_rq.conn = NULL;
    if (k == RQ_LIST) {
      s_list_busy = false;
      s_list_next = now_s() + 60.0;
    } else if (k == RQ_FLIST) {
      s_flist_busy = false;
      s_flist_next = now_s() + 60.0;
    } else if (k == RQ_IMG) {
      int i = ct_find(id);
      if (i >= 0) {
        s_ct[i].state = CT_FAILED;
        s_ct[i].tries++;
        s_ct[i].retry_at = now_s() + 15.0; /* retried automatically */
      }
    } else if (k == RQ_AUTH) {
      if (++s_auth_tries < 3) { /* transient network trouble: try again */
        s_auth_pending = true;
        s_auth_id = id;
        s_auth_retry_at = now_s() + 2.0;
      }
    }
    if (k != RQ_NONE) {
      char m[112];
      if (why[0])
        snprintf(m, sizeof m, "NTL server: %s", why);
      else
        snprintf(m, sizeof m, "NTL server did not answer");
      status_set(m);
    }
  s_rq.kind = RQ_NONE;
  s_rq.conn = NULL;
}

static void http_cb(struct mg_connection *c, int ev, void *ev_data) {
  if (c != s_rq.conn) return;
  if (ev == MG_EV_CONNECT) {
    /* .name = the server name sent in the TLS handshake (SNI). Without it
     ntl-slither.com closes the connection before answering. Certificate
     checks stay skipped. */
    struct mg_tls_opts tls = {.name = mg_str("ntl-slither.com"), .skip_verification = 1};
    mg_tls_init(c, &tls);
    mg_printf(c,
              "GET %s HTTP/1.1\r\nHost: ntl-slither.com\r\n"
              "User-Agent: Mozilla/5.0 (Linux; Android 13) AppleWebKit/537.36 "
              "(KHTML, like Gecko) Chrome/120.0 Mobile Safari/537.36 Vlither/4.2\r\n"
              "Origin: https://slither.io\r\nReferer: https://slither.io/\r\n"
              "Accept: */*\r\nAccept-Language: en-US,en;q=0.9\r\n"
              "Accept-Encoding: identity\r\nConnection: close\r\n\r\n",
              s_rq.path);
  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;
    int status = mg_http_status(hm);
    const char *body = hm->body.buf;
    size_t bn = hm->body.len;
    rq_on_response(status, body, bn);
    s_rq.kind = RQ_NONE;
    s_rq.conn = NULL;
    c->is_draining = 1;
  } else if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE) {
    /* The connection died before a full HTTP answer arrived. Say why (mongoose
       gives the reason with MG_EV_ERROR) so a failure can be diagnosed. */
    char why[96] = "";
    if (ev == MG_EV_ERROR && ev_data) snprintf(why, sizeof why, "%.90s", (const char *)ev_data);
    /* The server may answer without a Content-Length and just close the
       connection (the browser's fetch() accepts that). Mongoose then never
       reports a message, so read what arrived ourselves. */
    if (ev == MG_EV_CLOSE && s_rq.kind != RQ_NONE && c->recv.len > 12) {
      struct mg_http_message hm;
      int hl = mg_http_parse((const char *)c->recv.buf, c->recv.len, &hm);
      if (hl > 0) {
        hm.body = mg_str_n((const char *)c->recv.buf + hl, c->recv.len - (size_t)hl);
        http_cb(c, MG_EV_HTTP_MSG, &hm);
        return;
      }
    }
    if (ev == MG_EV_CLOSE && !why[0])
      snprintf(why, sizeof why, "closed after %u bytes", (unsigned)c->recv.len);
    rq_on_failure(why);
    if (ev == MG_EV_ERROR) c->is_closing = 1;
  }
}

#ifdef ANDROID
/* On Android the request goes through the phone's own HTTPS stack
 * (java.net.HttpURLConnection) on a worker thread: mongoose's built-in TLS
 * client cannot complete a handshake with ntl-slither.com. */
#include <pthread.h>
static struct {
  volatile int state; /* 0 idle, 1 running, 2 finished (waiting to be read) */
  char url[300];
  int status;
  char *body;
  size_t n;
  char err[96];
} s_job;

static void *job_main(void *arg) {
  (void)arg;
  char *b = NULL;
  size_t n = 0;
  char err[96] = "";
  int st = android_jni_http_get(s_job.url, &b, &n, err, sizeof err);
  s_job.status = st;
  s_job.body = b;
  s_job.n = n;
  snprintf(s_job.err, sizeof s_job.err, "%s", err);
  __atomic_store_n(&s_job.state, 2, __ATOMIC_RELEASE);
  return NULL;
}

static void rq_start(rq_kind k, int id, const char *path) {
  if (s_job.state != 0) return;
  s_rq.kind = k;
  s_rq.id = id;
  s_rq.started = now_s();
  s_rq.conn = NULL;
  snprintf(s_rq.path, sizeof s_rq.path, "%s", path);
  snprintf(s_job.url, sizeof s_job.url, "%s%s", NT_HOST, path);
  s_job.state = 1;
  pthread_t th;
  if (pthread_create(&th, NULL, job_main, NULL) != 0) {
    s_job.state = 0;
    rq_on_failure("cannot start network thread");
    return;
  }
  pthread_detach(th);
}

/* Collect a finished worker-thread request. Returns true while the worker is
 * busy or just delivered (the pump must not start another request). */
static bool job_poll(void) {
  int js = __atomic_load_n(&s_job.state, __ATOMIC_ACQUIRE);
  if (js == 1) return true;
  if (js == 2) {
    if (s_rq.kind != RQ_NONE) {
      if (s_job.status > 0 && s_job.body)
        rq_on_response(s_job.status, s_job.body, s_job.n);
      else
        rq_on_failure(s_job.err[0] ? s_job.err : "no answer");
    }
    free(s_job.body);
    s_job.body = NULL;
    s_job.state = 0;
    return true;
  }
  return false;
}
#else
static void rq_start(rq_kind k, int id, const char *path) {
  s_rq.kind = k;
  s_rq.id = id;
  s_rq.started = now_s();
  snprintf(s_rq.path, sizeof s_rq.path, "%s", path);
  s_rq.conn = mg_http_connect(&s_mgr, NT_HOST, http_cb, NULL);
  if (!s_rq.conn) {
    if (k == RQ_LIST) s_list_busy = false;
    s_rq.kind = RQ_NONE;
  }
}

#endif

static void net_pump(void) {
#ifdef ANDROID
  if (job_poll()) return;
#endif
  if (s_rq.kind != RQ_NONE) {
    if (now_s() - s_rq.started > 25.0) { /* stuck request */
      struct mg_connection *c = s_rq.conn;
      if (s_rq.kind == RQ_LIST) s_list_busy = false;
      if (s_rq.kind == RQ_FLIST) s_flist_busy = false;
      if (s_rq.kind == RQ_IMG) {
        int i = ct_find(s_rq.id);
        if (i >= 0) {
          s_ct[i].state = CT_FAILED;
          s_ct[i].retry_at = now_s() + 30.0;
        }
      }
      s_rq.kind = RQ_NONE;
      s_rq.conn = NULL;
      if (c) c->is_closing = true;
      status_set("NTL server timed out");
    }
    return;
  }
  double now = now_s();
  if (s_auth_pending && now >= s_auth_retry_at) {
    s_auth_pending = false;
    snprintf(s_rq.md5, sizeof s_rq.md5, "%s", s_auth_md5);
    char path[160];
    snprintf(path, sizeof path, "/tags/tag?id=%d&pass=%s", s_auth_id, s_auth_md5);
    rq_start(RQ_AUTH, s_auth_id, path);
    return;
  }
  for (int i = 0; i < s_nct; ++i) {
    ctag_t *t = &s_ct[i];
    if (t->state == CT_FAILED && t->tries < 3 && t->retry_at > 0.0 && now >= t->retry_at) {
      t->retry_at = 0.0; /* retry a picture whose download failed */
      t->state = CT_NEED_IMG;
    }
    if (t->state == CT_NEED_IMG) {
      char path[80];
      snprintf(path, sizeof path, "/newtag.php?id=%d", t->def.id);
      rq_start(RQ_IMG, t->def.id, path);
      return;
    }
  }
  if (s_list_want && now >= s_list_next) {
    s_list_want = false;
    s_list_busy = true;
    char path[120];
    snprintf(path, sizeof path, "/tags/cstags?v=%d&modv=%s", s_nct > 0 ? s_list_ver : 1,
             NT_MOD_VERSION);
    rq_start(RQ_LIST, 0, path);
    return;
  }
  if (s_flist_want && now >= s_flist_next) {
    s_flist_want = false;
    s_flist_busy = true;
    char path[120];
    snprintf(path, sizeof path, "/tags/fstags?v=%d&modv=%s", s_flist_ver, NT_MOD_VERSION);
    rq_start(RQ_FLIST, 0, path);
    return;
  }
}

/* Make sure the cached list is loaded and a sync is scheduled. */
static void comm_ensure_loaded(void) {
  if (s_loaded) return;
  s_loaded = true;
  auth_load();
  cache_load();
  fcache_load();
  s_list_want = true;
  s_list_next = 0.0;
  s_flist_want = true;
  s_flist_next = 0.0;
}

static void comm_request_refresh(void) {
  if (s_list_busy || s_flist_busy) return;
  s_list_want = true;
  s_list_next = 0.0;
  s_flist_want = true;
  s_flist_next = 0.0;
  status_set("Syncing NTL tags...");
}

/* A feed packet named a tag we have no picture for: ask for it. */
static void comm_note_unknown(int id) {
  if (!comm_id_ok(id) && !free_id_ok(id)) return;
  comm_ensure_loaded();
  int i = ct_find(id);
  if (i >= 0) return; /* known entry: already queued / failed / loading */
  /* Not in the list we have: re-sync the list (it may be newer), and also ask
     for the picture by id so it shows even if the list lags behind. */
  if (!s_list_busy && now_s() >= s_list_next) s_list_want = true;
  if (!s_flist_busy && now_s() >= s_flist_next) s_flist_want = true;
  i = ct_add(id);
  if (i >= 0) {
    s_ct[i].meta_default = 1;
    s_ct[i].def.w = 140;
    s_ct[i].def.h = 140;
    s_ct[i].def.bx = -32;
    s_ct[i].def.by = -70;
    s_ct[i].def.c1 = 0xFFFFFF;
    s_ct[i].def.c2 = 0x92EFFE;
    s_ct[i].state = CT_NEED_IMG;
  }
}

/* "!loadtag id w h c1 c2 bx by" (what NTL's upload page gives you) or a plain
 * id. Returns the id or -1. */
static void ct_default_meta(ctag_t *t) {
  t->meta_default = 1;
  t->def.w = t->def.h = 140;
  t->def.bx = -32;
  t->def.by = -70;
  t->def.c1 = 0xFFFFFF;
  t->def.c2 = 0x92EFFE;
}

/* What the "Add ID" box accepts (all of NTL's own commands work):
 *   !tag 476 mypassword        use tag 476 (checks the password with the server)
 *   476 mypassword             same
 *   !loadtag id w h c1 c2 bx by   the line NTL's upload page shows
 *   476                        just download and show tag 476
 * Returns the tag id, or -1 when the text is not understood. */
static int comm_add_by_text(const char *text) {
  /* NTL's chat prefixes commands with "# " in some places: accept that, extra
     spaces, and any letter case ("# !Tag 535 pass"). */
  while (*text == ' ' || *text == '#' || *text == '\t') text++;
  bool is_tag_cmd = false;
  if (*text == '!' || ((text[0] | 32) == 't' && (text[1] | 32) == 'a' && (text[2] | 32) == 'g' &&
                       text[3] == ' ')) {
    const char *w = *text == '!' ? text + 1 : text;
    is_tag_cmd = (w[0] | 32) == 't' && (w[1] | 32) == 'a' && (w[2] | 32) == 'g' &&
                 (w[3] == ' ' || w[3] == 0);
    while (*text && *text != ' ') text++;
  }
  long id = 0;
  char tok2[64] = "";
  int got_tokens = sscanf(text, " %ld %63s", &id, tok2);
  /* a bundled tag (built-in / public fstag) needs no download and no password */
  if (got_tokens >= 1 && ntl_tags_valid((int)id)) {
    s_sel = (int)id;
    cfg_save();
    char m[64];
    snprintf(m, sizeof m, "Tag #%ld selected", id);
    status_set(m);
    return (int)id;
  }
  if (got_tokens >= 1 && !comm_id_ok((int)id) && !free_id_ok((int)id)) {
    /* NTL itself answers "tagid missing" for such numbers: 60..199 and the
       ids between the bundled and uploaded ranges are not tags. */
    char m[100];
    snprintf(m, sizeof m, "Tag #%ld does not exist", id);
    status_set(m);
    return -1;
  }
  if (got_tokens < 1) {
    char m[100];
    snprintf(m, sizeof m, "No tag ID in \"%.20s\"", text);
    status_set(m);
    return -1;
  }
  comm_ensure_loaded();
  s_user_req = (int)id;
  int i = ct_add((int)id);
  if (i < 0) return -1;
  ctag_t *t = &s_ct[i];

  /* a free tag from the server list: no password, just select it */
  if (free_id_ok((int)id)) {
    s_sel = (int)id;
    cfg_save();
    if (t->state == CT_META || t->state == CT_FAILED) {
      if (!t->def.w) ct_default_meta(t);
      t->tries = 0;
      t->state = CT_NEED_IMG;
    }
    char m[64];
    snprintf(m, sizeof m, "Tag #%ld selected", id);
    status_set(m);
    return (int)id;
  }

  /* a password was given: unlock the tag for use */
  bool has_pass = tok2[0] && !isdigit((unsigned char)tok2[0]) && tok2[0] != '#' && tok2[0] != '-';
  if (is_tag_cmd || has_pass) {
    if (!tok2[0]) {
      status_set("Use: !tag id password");
      return -1;
    }
    md5_hex(tok2, s_auth_md5);
    s_auth_id = (int)id;
    s_auth_pending = true;
    s_auth_tries = 0;
    s_auth_retry_at = 0.0;
    if (t->state == CT_META || t->state == CT_FAILED) {
      if (!t->def.w) ct_default_meta(t);
      t->tries = 0;
      t->state = CT_NEED_IMG;
    }
    char m[64];
    snprintf(m, sizeof m, "Checking tag #%ld...", id);
    status_set(m);
    return (int)id;
  }

  /* "!loadtag id w h c1 c2 bx by": the tag's picture settings */
  int w = 0, h = 0, bx = -32, by = -70;
  char c1[24] = "", c2[24] = "";
  int got = sscanf(text, " %ld %d %d %23s %23s %d %d", &id, &w, &h, c1, c2, &bx, &by);
  if (got >= 7) {
    t->def.w = (short)w;
    t->def.h = (short)h;
    t->def.c1 = (unsigned)strtoul(c1[0] == '#' ? c1 + 1 : c1, NULL, 16) & 0xFFFFFFu;
    t->def.c2 = (unsigned)strtoul(c2[0] == '#' ? c2 + 1 : c2, NULL, 16) & 0xFFFFFFu;
    t->def.bx = (short)bx;
    t->def.by = (short)by;
    t->meta_default = 0;
  } else if ((t->state == CT_META || t->state == CT_FAILED) && !t->def.w) {
    ct_default_meta(t);
  }
  if (t->state == CT_META || t->state == CT_FAILED) {
    t->tries = 0;
    t->state = CT_NEED_IMG;
  }
  char m[64];
  snprintf(m, sizeof m, "Downloading tag #%ld...", id);
  status_set(m);
  return (int)id;
}

void ntl_tags_open_upload_page(void) {
#ifdef ANDROID
  android_jni_open_url(NT_UPLOAD_URL);
#elif defined(_WIN32)
  system("start \"\" \"" NT_UPLOAD_URL "\"");
#else
  system("xdg-open \"" NT_UPLOAD_URL "\" >/dev/null 2>&1 &");
#endif
  status_set("Upload page opened - then tap Add ID");
}

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
  if (got != sizeof c || (c.magic != NT_CFG_MAGIC && c.magic != NT_CFG_MAGIC_V1)) return;
  s_show_normal = c.show != 0;
  /* New file: the byte is "show in Assist". An old (NTG1) file never had that
     choice, so Assist starts OFF for it as well. */
  s_show_assist = c.magic == NT_CFG_MAGIC ? c.assist_hide != 0 : false;
  s_sel = (tag_open(c.selected) || (comm_id_ok(c.selected) && auth_find(c.selected)))
              ? c.selected
              : NTL_TAG_NONE;
}

static void cfg_save(void) {
  FILE *f = open_cfg("wb");
  if (!f) return;
  cfg_file c = {NT_CFG_MAGIC, s_sel, (uint8_t)(s_show_normal ? 1 : 0),
                (uint8_t)(s_show_assist ? 1 : 0), {0, 0}};
  fwrite(&c, 1, sizeof c, f);
  fclose(f);
}

bool ntl_tags_show_enabled(void) { return s_show_normal; }
bool ntl_tags_show_get(int mode) { return mode ? s_show_assist : s_show_normal; }
void ntl_tags_show_set(int mode, bool on) {
  if (mode)
    s_show_assist = on;
  else
    s_show_normal = on;
  cfg_save();
}
int ntl_tags_selected(void) { return s_sel; }

/* ---- spawn-packet skin header (NTL "e" array) --------------------------- */
bool ntl_tags_encode_header(uint8_t hdr[8]) {
  if (!tag_open(s_sel)) return false;
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
    if (tag_open(id)) {
      tag = id;
      if (free_id_ok(id)) comm_note_unknown(id); /* picture not downloaded yet */
    }
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
  } else if (ev == MG_EV_ERROR) {
    if (s_sel >= 0 && !tag_open(s_sel)) { /* only matters for uploaded tags */
      char m[112];
      snprintf(m, sizeof m, "NTL feed: %.80s", ev_data ? (const char *)ev_data : "connect failed");
      status_set(m);
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

static char s_reg_tc[33];
static int s_reg_tid = -1;

static void ws_send_position(user_settings *us, snake *me) {
  char buf[512];
  size_t n = 0;
  /* The NTL feed learns our tag only from the registration message, and the
     NTL mod sends that once per connection. If the tag changed since (typed
     "!tag" mid-game), reconnect so the registration is sent again. */
  if (s_registered) {
    const char *a = (s_sel >= 0 && !tag_open(s_sel)) ? auth_find(s_sel) : NULL;
    int tid = a ? s_sel : -1;
    if (tid != s_reg_tid || strcmp(a ? a : "", s_reg_tc)) {
      ws_close();
      s_next_connect = 0.0;
      return;
    }
  }
  if (!s_registered) {
    /* [nick, "ip:port", tagToken, tagId, skin, x, y, ntlid] -- same as the
       NTL mod. */
    buf[n++] = '[';
    n = json_str(buf, sizeof buf, n, us->nickname);
    buf[n++] = ',';
    n = json_str(buf, sizeof buf, n, us->ipv4);
    /* Free tags travel in the spawn packet. A community tag is announced here
       with the md5 of its password, like the NTL mod does after "!tag". */
    const char *tc = "";
    int tid = -1;
    if (s_sel >= 0 && !tag_open(s_sel)) {
      const char *a = auth_find(s_sel);
      if (a) {
        tc = a;
        tid = s_sel;
      }
    }
    s_reg_tid = tid;
    snprintf(s_reg_tc, sizeof s_reg_tc, "%s", tc);
    if (tid >= 0) {
      char m[64];
      snprintf(m, sizeof m, "Tag #%d announced to NTL players", tid);
      status_set(m);
    }
    n += (size_t)snprintf(buf + n, sizeof buf - n, ",\"%s\",%d,%d,%d,%d,%d]", tc, tid,
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
  auth_load();
  cfg_load();
  memset(s_ants, 0, sizeof s_ants);
  memset(s_map_gen, 0, sizeof s_map_gen);
  s_cur_gen = 0;
  mg_mgr_init(&s_mgr);
  s_mgr_ready = true;
}

void ntl_tags_destroy(tenv *env) {
  ws_close();
  if (s_rq.conn) {
    s_rq.conn->is_closing = true;
    s_rq.conn = NULL;
    s_rq.kind = RQ_NONE;
  }
  retire_tick(env, true);
  if (s_ctex) {
    igImplVulkan_RemoveTexture(s_cds);
    destroy_texture(env->ctx, s_ctex);
    s_ctex = NULL;
    s_cds = VK_NULL_HANDLE;
    s_ctex_id = 0;
  }
  free(s_catlas);
  s_catlas = NULL;
  for (int i = 0; i < s_nct; ++i) {
    free(s_ct[i].webp);
    s_ct[i].webp = NULL;
  }
  s_nct = 0;
  s_loaded = false;
  s_cindex_init = false;
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

  /* community tags: downloads, decoding, atlas upload (also in the lobby) */
  if (s_loaded) {
    net_pump();
    decode_some();
    retire_tick(env, false);
    if (s_catlas_dirty && now >= s_catlas_next_upload) {
      s_catlas_next_upload = now + 0.8;
      atlas_upload(env);
    }
    if (s_disp_dirty) disp_rebuild();
  }

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
      o->ntl_tag1 = (int16_t)(ntl_tags_known(s_sel) ? s_sel + 1 : 0);
      continue;
    }
    unsigned nid = (unsigned)o->ntlid & 0xFFFFu;
    if (s_cur_gen && s_map_gen[nid] == s_cur_gen) {
      int v = s_map_val[nid];
      if (v >= 0 && ntl_tags_known(v)) o->ntl_tag1 = (int16_t)(v + 1);
      else if (v >= 0) comm_note_unknown(v); /* uploaded tag we lack: fetch it */
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

/* Antenna + tag for one head. World coordinates map to the screen as
 *   screen = (screen_cx, screen_cy) + (world - (view_x, view_y)) * gsc
 * `owner` picks the physics slot (a snake id; the Skin Editor preview uses its
 * own fixed id), `sc` is the snake scale (body diameter = 29 * sc). */
static void draw_tag_chain(int tid, int owner, float ang, float sc, float gsc, float view_x,
                           float view_y, float hx, float hy, float screen_cx, float screen_cy,
                           float alive) {
  const ntl_tag_def *def;
  unsigned long long atlas_tex;
  int atw, ath;
  if (!resolve(tid, &def, &atlas_tex, &atw, &ath)) return;
  float A = sc * gsc;
  double now = now_s();

  ant_t *a = ant_get(owner, now);
  float cs = cosf(ang), sn = sinf(ang);
  /* where the antenna is anchored now (behind the head) */
  const float cur_x = hx - 8.0f * cs * sc;
  const float cur_y = hy - 8.0f * sn * sc;

  if (!a->shown) { /* lay the antenna out straight behind the head */
    a->x[0] = cur_x;
    a->y[0] = cur_y;
    for (int i = 1; i < NT_PTS; ++i) {
      a->x[i] = a->x[0] - cs * i * NT_SEG * sc;
      a->y[i] = a->y[0] - sn * i * NT_SEG * sc;
      a->vx[i] = a->vy[i] = 0;
    }
    a->t = now;
    a->atba = ang + (float)M_PI;
    a->shown = true;
  }

  /* physics: NTL's fixed 60 Hz spring chain, at most 4 ticks per frame */
  double dt = now - a->t;
  if (dt > 0.25) dt = 0.25;
  int ticks = (int)(dt / NT_TICK);
  if (ticks > 4) ticks = 4;
  a->t += ticks * NT_TICK;
  float maxlen = NT_SEG * sc;
  /* The head moves every frame but the chain steps at 60 Hz, so a frame can
     get 0, 1 or 2 steps. Boosting makes the head move far per frame, and the
     uneven steps showed up as the tag vibrating. Spread the anchor's movement
     evenly over the steps taken (a->x[0] still holds the previous anchor). */
  const float from_x = a->x[0], from_y = a->y[0];
  for (int k = 0; k < ticks; ++k) {
    float f = (float)(k + 1) / (float)ticks;
    a->x[0] = from_x + (cur_x - from_x) * f;
    a->y[0] = from_y + (cur_y - from_y) * f;
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
  /* A frame with no physics step: carry the whole chain along with the head
     instead of leaving it behind (that gap is what flickered). */
  const float off_x = cur_x - a->x[0], off_y = cur_y - a->y[0];
  ImVec2 sp[NT_PTS];
  for (int i = 0; i < NT_PTS; ++i)
    sp[i] = (ImVec2){screen_cx + (a->x[i] + off_x - view_x) * gsc,
                     screen_cy + (a->y[i] + off_y - view_y) * gsc};

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
  float u0 = (float)def->ax / atw, v0 = (float)def->ay / ath;
  float u1 = (float)(def->ax + def->aw) / atw;
  float v1 = (float)(def->ay + def->ah) / ath;
  ImTextureRef tex = {NULL, (ImTextureID)atlas_tex};
  ImDrawList_AddImageQuad(dl, tex, q[0], q[1], q[2], q[3], (ImVec2){u0, v0},
                          (ImVec2){u1, v0}, (ImVec2){u1, v1}, (ImVec2){u0, v1},
                          pack(0xFFFFFF, alpha));
}

void ntl_tags_draw_snake(tenv *env, snake *o, float hx, float hy,
                         float screen_cx, float screen_cy, float alive) {
  if (!o || o->ntl_tag1 <= 0) return;
  if (!ntl_tags_show_get(env->usr->usrs.hotkeys[HOTKEY_ASSIST].active ? 1 : 0)) return;
  game_data *g = &env->usr->gdata;
  draw_tag_chain(o->ntl_tag1 - 1, o->id, o->ang, o->sc, g->data.gsc, g->data.view_xx,
                 g->data.view_yy, hx, hy, screen_cx, screen_cy, alive);
}

/* The selected tag on the Skin Editor's preview snake. (hx, hy) is the centre of
 * the head in window coordinates, `body_d` the diameter of one body segment;
 * the head faces right. Drawn with the in-game antenna physics and a slight
 * sway so the tag hangs and swings like it does in a match. */
void ntl_tags_draw_preview(float hx, float hy, float body_d) {
  if (s_sel < 0 || !ctag_ready_or_bundled(s_sel)) return;
  ImVec2 wp;
  igGetWindowPos(&wp);
  double t = now_s();
  float sway_x = sinf((float)t * 1.3f) * body_d * 0.12f;
  float sway_y = sinf((float)t * 2.1f) * body_d * 0.07f;
  draw_tag_chain(s_sel, -4242, 0.0f, body_d / 29.0f, 1.0f, 0.0f, 0.0f, wp.x + hx + sway_x,
                 wp.y + hy + sway_y, 0.0f, 0.0f, 1.0f);
}

/* ---- settings UI -------------------------------------------------------- */
/* ---- Skin Editor tag block ----------------------------------------------
 * Drawn by ui/skin_editor.c between the "Saved skins" and "Custom" rows:
 *
 *   NTL tag  #12  (or the latest status message)
 *   [ Select tag ............................ ] [ box: chosen tag ]
 *   [ Upload ] [ Add ID ] [ Remove ]
 *
 * "Select tag" (or the box) opens the big tag screen. Colours go through
 * crystal_tint(), so the block follows the lobby background theme like the
 * other windows. */
static ImU32 tcol(float r, float g, float b, float a) {
  return igColorConvertFloat4ToU32(crystal_tint((ImVec4){r, g, b, a}));
}

/* Row holding the "Select tag" button and the box with the chosen tag. */
static float sel_row_h(void) { return igGetFrameHeight() * 1.7f; }

float ntl_tags_block_height(void) {
  ImGuiStyle *st = igGetStyle();
  float fh = igGetFrameHeight(), sp = st->ItemSpacing.y;
  return (fh + sp) + (sel_row_h() + sp) + (fh + sp);
}

static void thumb(ImDrawList *dl, const ntl_tag_def *d, unsigned long long tex_id,
                  int atw, int ath, ImVec2 a, ImVec2 b) {
  float cw = b.x - a.x, ch = b.y - a.y;
  float sc = fminf(cw / d->aw, ch / d->ah);
  float w = d->aw * sc, h = d->ah * sc;
  ImVec2 p0 = {a.x + (cw - w) * 0.5f, a.y + (ch - h) * 0.5f};
  ImVec2 p1 = {p0.x + w, p0.y + h};
  ImTextureRef tex = {NULL, (ImTextureID)tex_id};
  ImDrawList_AddImage(dl, tex, p0, p1,
                      (ImVec2){(float)d->ax / atw, (float)d->ay / ath},
                      (ImVec2){(float)(d->ax + d->aw) / atw, (float)(d->ay + d->ah) / ath},
                      0xFFFFFFFFu);
}

static void draw_cell(ImDrawList *dl, ImVec2 a, ImVec2 b, int id, bool sel, bool hot,
                      bool show_id) {
  const ntl_tag_def *def;
  unsigned long long tex;
  int atw, ath;
  float r = (b.x - a.x) * 0.12f;
  ImDrawList_AddRectFilled(dl, a, b,
                           sel   ? tcol(0.45f, 0.30f, 0.80f, 0.60f)
                           : hot ? tcol(0.30f, 0.20f, 0.50f, 0.75f)
                                 : tcol(0.10f, 0.07f, 0.18f, 0.60f),
                           r, 0);
  float pad = (b.x - a.x) * 0.06f + 2.0f;
  if (resolve(id, &def, &tex, &atw, &ath))
    thumb(dl, def, tex, atw, ath, (ImVec2){a.x + pad, a.y + pad},
          (ImVec2){b.x - pad, b.y - pad - (show_id ? igGetFontSize() * 0.9f : 0.0f)});
  if (sel) ImDrawList_AddRect(dl, a, b, tcol(0.85f, 0.75f, 1.0f, 0.95f), r, 0, 2.0f);
  if (show_id) {
    char t[16];
    snprintf(t, sizeof t, "#%d", id);
    ImVec2 ts;
    igCalcTextSize(&ts, t, NULL, false, -1.0f);
    ImDrawList_AddText_Vec2(dl, (ImVec2){a.x + ((b.x - a.x) - ts.x) * 0.5f, b.y - ts.y - 4.0f},
                            tcol(0.78f, 0.70f, 0.95f, 0.9f), t, NULL);
  }
  if (!tag_open(id)) { /* community tag: amber = needs password, green = unlocked */
    bool unlocked = auth_find(id) != NULL;
    ImDrawList_AddCircleFilled(dl, (ImVec2){b.x - 7, a.y + 7}, 4.0f,
                               unlocked ? 0xFF60D060u : 0xFF3CBEFFu, 12);
  }
}

static bool s_open_add = false, s_open_pw = false, s_open_big = false;
static char s_id_buf[160];
static char s_pw_buf[64];
static int s_pw_id = 0;
static float s_goal_big = -1.0f;
static bool s_scroll_big_sel = false;

static void select_tag(int id) {
  if (tag_open(id) || auth_find(id)) {
    s_sel = id;
    cfg_save();
    status_set("Tag selected - shows from your next life");
  } else {
    s_pw_id = id;
    s_pw_buf[0] = 0;
    s_open_pw = true;
  }
}

static void tag_modals(tenv *env) {
  float W = env->ctx->size[0], H = env->ctx->size[1];
  float pw = fminf(W * 0.60f, fmaxf(W * 0.40f, igGetFontSize() * 24.0f));
  ImVec2 center = {W * 0.5f, H * 0.38f};
  ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;

  /* ---- big tag screen: every tag as a large tile, drag to scroll, tap to pick ---- */
  if (s_open_big) {
    igOpenPopup_Str("NTL tags##big", 0);
    s_open_big = false;
    s_goal_big = -1.0f;
    s_scroll_big_sel = true;
  }
  igSetNextWindowPos((ImVec2){W * 0.5f, H * 0.5f}, ImGuiCond_Always, (ImVec2){0.5f, 0.5f});
  igSetNextWindowSize((ImVec2){fminf(W * 0.62f, W - 40.0f), H * 0.88f}, ImGuiCond_Always);
  if (igBeginPopupModal("NTL tags##big", NULL,
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar |
                            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
    ImGuiStyle *bst = igGetStyle();
    ImGuiIO *io = igGetIO_Nil();
    double nowb = now_s();
    float fh = igGetFrameHeight();
    /* header */
    {
      igAlignTextToFramePadding();
      igTextColored((ImVec4){0.80f, 0.72f, 1.0f, 1.0f}, "NTL TAGS");
      igSameLine(0, bst->ItemSpacing.x * 2);
      bool is_status = nowb < s_status_until && s_status[0];
      if (is_status)
        igTextDisabled("%s", s_status);
      else if (s_sel >= 0)
        igTextDisabled("Your tag: #%d", s_sel);
      else
        igTextDisabled("Tap a tag to use it");
    }
    ImVec2 av;
    igGetContentRegionAvail(&av);
    float footer_h = fh + bst->ItemSpacing.y;
    igPushStyleVar_Float(ImGuiStyleVar_ChildRounding, 10.0f);
    igPushStyleColor_Vec4(ImGuiCol_ChildBg, (ImVec4){0.176f, 0.106f, 0.306f, 0.35f});
    bool vis = igBeginChild_Str("##ntl_big_grid", (ImVec2){av.x, av.y - footer_h},
                                ImGuiChildFlags_Borders,
                                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (vis) {
      ImVec2 org, cav;
      igGetCursorScreenPos(&org); /* top-left of the first row, already scrolled */
      igGetContentRegionAvail(&cav);
      float gap = bst->ItemSpacing.x;
      float cell = igGetFontSize() * 4.4f;
      int cols = (int)((cav.x + gap) / (cell + gap));
      if (cols < 1) cols = 1;
      cell = (cav.x - gap * (cols - 1)) / cols; /* stretch tiles to fill the row */
      float pitch_y = cell + gap;
      int n = s_ndisp, rows = (n + cols - 1) / cols;
      igDummy((ImVec2){cav.x, rows * pitch_y - gap});
      ImVec2 wp, wsz;
      igGetWindowPos(&wp);
      igGetWindowSize(&wsz);
      ImDrawList *dl = igGetWindowDrawList();

      if (s_scroll_big_sel) {
        s_scroll_big_sel = false;
        for (int i = 0; i < n; ++i)
          if (s_disp[i] == s_sel) {
            s_goal_big = (i / cols) * pitch_y - (wsz.y - cell) * 0.5f;
            if (s_goal_big < 0) s_goal_big = 0;
          }
      }
      static bool drag_on = false, drag_moved = false;
      static float drag_y0 = 0, vel = 0;
      bool hov = igIsWindowHovered(ImGuiHoveredFlags_None);
      int tap = -1;
      if (hov && igIsMouseClicked_Bool(0, false)) {
        drag_on = true;
        drag_moved = false;
        drag_y0 = io->MousePos.y;
        vel = 0;
        s_goal_big = -1.0f;
      }
      if (hov && io->MouseWheel != 0.0f) {
        igSetScrollY_Float(igGetScrollY() - io->MouseWheel * pitch_y);
        s_goal_big = -1.0f;
      }
      if (drag_on) {
        if (igIsMouseDown_Nil(0)) {
          if (!drag_moved && fabsf(io->MousePos.y - drag_y0) > 10.0f) drag_moved = true;
          if (drag_moved) {
            igSetScrollY_Float(igGetScrollY() - io->MouseDelta.y);
            vel = io->MouseDelta.y;
          }
        } else {
          if (!drag_moved) {
            float mx = io->MousePos.x - org.x, my = io->MousePos.y - org.y;
            int c = (int)floorf(mx / (cell + gap)), r = (int)floorf(my / pitch_y);
            if (mx >= 0 && my >= 0 && c >= 0 && c < cols && r >= 0 &&
                mx - c * (cell + gap) <= cell && my - r * pitch_y <= cell && r * cols + c < n)
              tap = r * cols + c;
          }
          drag_on = false;
        }
      } else if (fabsf(vel) > 0.5f) {
        igSetScrollY_Float(igGetScrollY() - vel);
        vel *= 0.92f;
      } else {
        vel = 0;
      }
      if (s_goal_big >= 0.0f && !drag_on) {
        float cur = igGetScrollY(), nd = cur + (s_goal_big - cur) * 0.3f;
        if (fabsf(s_goal_big - cur) < 1.0f) {
          nd = s_goal_big;
          s_goal_big = -1.0f;
        }
        igSetScrollY_Float(nd);
      }
      for (int i = 0; i < n; ++i) {
        int r = i / cols, c = i % cols;
        ImVec2 a = {org.x + c * (cell + gap), org.y + r * pitch_y};
        ImVec2 b = {a.x + cell, a.y + cell};
        if (b.y < wp.y || a.y > wp.y + wsz.y) continue;
        bool hot = hov && !drag_moved && io->MousePos.x >= a.x && io->MousePos.x <= b.x &&
                   io->MousePos.y >= a.y && io->MousePos.y <= b.y;
        draw_cell(dl, a, b, s_disp[i], s_disp[i] == s_sel, hot, true);
      }
      if (tap >= 0) {
        int id = s_disp[tap];
        if (tag_open(id) || auth_find(id)) {
          select_tag(id);
          igCloseCurrentPopup();
        } else {
          select_tag(id); /* asks for the password */
          igCloseCurrentPopup();
        }
      }
    }
    igEndChild();
    igPopStyleColor(1);
    igPopStyleVar(1);
    /* footer */
    float bw3 = (av.x - 2 * bst->ItemSpacing.x) / 3.0f;
    igBeginDisabled(s_list_busy);
    if (igButton(s_list_busy ? "Syncing..." : "Sync", (ImVec2){bw3, 0})) comm_request_refresh();
    crystal_sheen();
    igEndDisabled();
    igSameLine(0, -1);
    igBeginDisabled(s_sel < 0);
    if (igButton("Remove tag", (ImVec2){bw3, 0})) {
      s_sel = NTL_TAG_NONE;
      cfg_save();
      status_set("Tag removed");
    }
    crystal_sheen();
    igEndDisabled();
    igSameLine(0, -1);
    if (igButton("Done", (ImVec2){bw3, 0})) igCloseCurrentPopup();
    crystal_sheen();
    igEndPopup();
  }

  if (s_open_add) {
    igOpenPopup_Str("Add NTL tag", 0);
    s_open_add = false;
  }
  igSetNextWindowPos(center, ImGuiCond_Always, (ImVec2){0.5f, 0.5f});
  igSetNextWindowSize((ImVec2){pw, 0}, ImGuiCond_Always);
  if (igBeginPopupModal("Add NTL tag", NULL, wf)) {
    igTextWrapped("Type your tag's ID and password to use it, like in NTL: \"!tag 123 password\". "
                  "A plain ID (or the \"!loadtag ...\" line from the upload page) just downloads "
                  "the tag so you can see it.");
    igSetNextItemWidth(-1);
    igInputTextWithHint("##ntl_add_id", "!tag id password", s_id_buf,
                        sizeof s_id_buf, 0, NULL, NULL);
    float bw = (pw - igGetStyle()->WindowPadding.x * 2 - igGetStyle()->ItemSpacing.x) * 0.5f;
    if (igButton("Add", (ImVec2){bw, 0})) {
      if (comm_add_by_text(s_id_buf) >= 0) s_id_buf[0] = 0; /* sets its own status */
      igCloseCurrentPopup();
    }
    crystal_sheen();
    igSameLine(0, -1);
    if (igButton("Cancel", (ImVec2){bw, 0})) igCloseCurrentPopup();
    crystal_sheen();
    igEndPopup();
  }

  if (s_open_pw) {
    igOpenPopup_Str("Tag password", 0);
    s_open_pw = false;
  }
  igSetNextWindowPos(center, ImGuiCond_Always, (ImVec2){0.5f, 0.5f});
  igSetNextWindowSize((ImVec2){pw, 0}, ImGuiCond_Always);
  if (igBeginPopupModal("Tag password", NULL, wf)) {
    igTextWrapped("Tag #%d belongs to its uploader. Enter the password you chose when you "
                  "uploaded it to use it as your tag.",
                  s_pw_id);
    igSetNextItemWidth(-1);
    igInputTextWithHint("##ntl_pw", "Password", s_pw_buf, sizeof s_pw_buf,
                        ImGuiInputTextFlags_Password, NULL, NULL);
    float bw = (pw - igGetStyle()->WindowPadding.x * 2 - igGetStyle()->ItemSpacing.x) * 0.5f;
    igBeginDisabled(s_pw_buf[0] == 0);
    if (igButton("Unlock", (ImVec2){bw, 0})) {
      md5_hex(s_pw_buf, s_auth_md5);
      s_auth_id = s_pw_id;
      s_auth_pending = true;
      s_pw_buf[0] = 0;
      status_set("Checking password...");
      igCloseCurrentPopup();
    }
    crystal_sheen();
    igEndDisabled();
    igSameLine(0, -1);
    if (igButton("Cancel", (ImVec2){bw, 0})) {
      s_pw_buf[0] = 0;
      igCloseCurrentPopup();
    }
    crystal_sheen();
    igEndPopup();
  }
}

void ntl_tags_skin_editor_ui(tenv *env, float x, float y, float w) {
  ImGuiStyle *st = igGetStyle();
  double now = now_s();
  float fh = igGetFrameHeight(), sp = st->ItemSpacing.y, spx = st->ItemSpacing.x;

  comm_ensure_loaded();
  if (s_disp_dirty) disp_rebuild();
  ImDrawList *dl = igGetWindowDrawList();

  /* ---- header: "NTL tag" + your tag number, or the latest status message --- */
  {
    igSetCursorPos((ImVec2){x, y});
    ImVec2 p0;
    igGetCursorScreenPos(&p0);
    float ty = p0.y + st->FramePadding.y;
    char val[130];
    bool is_status = now < s_status_until && s_status[0];
    if (is_status)
      snprintf(val, sizeof val, "%s", s_status);
    else if (s_sel >= 0)
      snprintf(val, sizeof val, "#%d", s_sel);
    else
      snprintf(val, sizeof val, "none");
    ImVec2 lw;
    igCalcTextSize(&lw, "NTL tag", NULL, false, -1.0f);
    ImDrawList_PushClipRect(dl, p0, (ImVec2){p0.x + w, p0.y + fh}, true);
    ImDrawList_AddText_Vec2(dl, (ImVec2){p0.x, ty}, tcol(0.62f, 0.55f, 0.80f, 1.0f), "NTL tag", NULL);
    ImDrawList_AddText_Vec2(dl, (ImVec2){p0.x + lw.x + spx, ty},
                            is_status ? tcol(0.95f, 0.85f, 1.0f, 1.0f)
                                      : (s_sel >= 0 ? tcol(0.85f, 0.78f, 1.0f, 1.0f)
                                                    : tcol(0.62f, 0.55f, 0.80f, 0.8f)),
                            val, NULL);
    ImDrawList_PopClipRect(dl);
  }

  /* ---- [ Select tag ] [ box with the chosen tag ] ---- */
  float rowh = sel_row_h();
  float y1 = y + fh + sp;
  igSetCursorPos((ImVec2){x, y1});
  if (igButton("Select tag", (ImVec2){w - rowh - spx, rowh})) s_open_big = true;
  crystal_sheen();
  igSameLine(0, spx);
  {
    ImVec2 a;
    igGetCursorScreenPos(&a);
    ImVec2 b = {a.x + rowh, a.y + rowh};
    if (igInvisibleButton("##ntl_sel_box", (ImVec2){rowh, rowh}, 0)) s_open_big = true;
    bool hot = igIsItemHovered(ImGuiHoveredFlags_None);
    ImDrawList_AddRectFilled(dl, a, b,
                             hot ? tcol(0.30f, 0.20f, 0.50f, 0.75f) : tcol(0.176f, 0.106f, 0.306f, 0.60f),
                             10.0f, 0);
    ImDrawList_AddRect(dl, a, b, tcol(0.690f, 0.580f, 0.960f, 0.50f), 10.0f, 0, 1.4f);
    const ntl_tag_def *def;
    unsigned long long tex;
    int atw, ath;
    float pad = rowh * 0.10f;
    if (s_sel >= 0 && resolve(s_sel, &def, &tex, &atw, &ath)) {
      thumb(dl, def, tex, atw, ath, (ImVec2){a.x + pad, a.y + pad}, (ImVec2){b.x - pad, b.y - pad});
    } else {
      const char *t = s_sel >= 0 ? "..." : "-";
      ImVec2 ts;
      igCalcTextSize(&ts, t, NULL, false, -1.0f);
      ImDrawList_AddText_Vec2(dl, (ImVec2){a.x + (rowh - ts.x) * 0.5f, a.y + (rowh - ts.y) * 0.5f},
                              tcol(0.62f, 0.55f, 0.80f, 0.8f), t, NULL);
    }
  }

  /* ---- buttons: Upload / Add ID / Remove ---- */
  float y3 = y1 + rowh + sp;
  igSetCursorPos((ImVec2){x, y3});
  float bw = (w - 2 * spx) / 3.0f;
  if (igButton("Upload", (ImVec2){bw, 0})) ntl_tags_open_upload_page();
  crystal_sheen();
  igSameLine(0, -1);
  if (igButton("Add ID", (ImVec2){bw, 0})) s_open_add = true;
  crystal_sheen();
  igSameLine(0, -1);
  igBeginDisabled(s_sel < 0);
  if (igButton("Remove", (ImVec2){bw, 0})) {
    s_sel = NTL_TAG_NONE;
    cfg_save();
    status_set("Tag removed");
  }
  crystal_sheen();
  igEndDisabled();

  tag_modals(env);
}
