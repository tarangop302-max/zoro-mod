#include "skin_gen.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef NO_ACCESSORY
#define NO_ACCESSORY 255
#endif

/* ======================================================================
   Beads: the 38 official skin characters, measured as they really look.
   ====================================================================== */

#define NB 38
#define SAME_LOOK 12.0 /* below this two beads read as the same colour */
#define ACCENT 2       /* thinnest band that still reads as a line */
#define MAX_PAL 16
#define MAX_POOL 40
#define RMAX 1024
#define CODE_BUF 1100

enum { FAM_GREY, FAM_RED, FAM_ORANGE, FAM_YELLOW, FAM_GREEN, FAM_AQUA, FAM_BLUE, FAM_PURPLE, FAM_PINK };

typedef struct {
  char ch;
  int id;
  uint32_t rgb;
  float L;
  int hue;
  float sat;
  bool stars;
  int fam;
  double lab[3];
} bead;

static bead g_beads[NB] = {
    {'z', 0, 0xB377E8, 0.605f, 272, 0.71f, false}, {'x', 1, 0x868FE8, 0.621f, 235, 0.68f, false},
    {'c', 2, 0x77C2C2, 0.739f, 180, 0.38f, false}, {'v', 3, 0x77E877, 0.835f, 120, 0.71f, false},
    {'b', 4, 0xDCDC68, 0.859f, 60, 0.63f, false},  {'n', 5, 0xE89559, 0.691f, 25, 0.76f, false},
    {'m', 6, 0xE88686, 0.665f, 0, 0.68f, false},   {',', 7, 0xE83B3B, 0.526f, 0, 0.79f, false},
    {'a', 8, 0xD12CD1, 0.518f, 300, 0.65f, false}, {'s', 9, 0xE8E8E8, 0.920f, 0, 0.0f, false},
    {'d', 10, 0x8A92E9, 0.632f, 235, 0.68f, true}, {'f', 11, 0x4A4A4A, 0.316f, 0, 0.0f, false},
    {'g', 12, 0xE8B34A, 0.761f, 40, 0.78f, false}, {'h', 13, 0x257F59, 0.473f, 155, 0.55f, false},
    {'j', 14, 0x5D6DE8, 0.508f, 233, 0.75f, false}, {'k', 15, 0x707DE8, 0.560f, 233, 0.72f, false},
    {'l', 16, 0x434EE8, 0.420f, 236, 0.78f, false}, {'q', 17, 0x954AE8, 0.485f, 268, 0.78f, false},
    {'w', 18, 0xE8D13B, 0.835f, 52, 0.79f, false}, {'e', 19, 0x3540E8, 0.384f, 236, 0.80f, true},
    {'r', 20, 0x3541E8, 0.385f, 236, 0.80f, true}, {'t', 21, 0x4920B3, 0.287f, 256, 0.69f, false},
    {'y', 22, 0xE85008, 0.550f, 19, 0.93f, false}, {'u', 23, 0x5EBBD8, 0.714f, 194, 0.61f, false},
    {'i', 24, 0x828182, 0.541f, 0, 0.0f, false},   {'o', 25, 0x38B343, 0.646f, 125, 0.53f, false},
    {'p', 26, 0x1CF55D, 0.852f, 138, 0.92f, false}, {'1', 27, 0xD64444, 0.504f, 0, 0.64f, false},
    {'2', 28, 0xD63535, 0.484f, 0, 0.66f, false},  {'3', 29, 0x505050, 0.338f, 0, 0.0f, false},
    {'4', 30, 0x16169C, 0.207f, 240, 0.75f, false}, {'5', 31, 0x9C1616, 0.335f, 0, 0.75f, false},
    {'6', 32, 0x848425, 0.536f, 60, 0.56f, false}, {'7', 33, 0x845322, 0.398f, 30, 0.59f, false},
    {'8', 34, 0x842584, 0.338f, 300, 0.56f, false}, {'9', 35, 0x258425, 0.483f, 120, 0.56f, false},
    {'0', 37, 0x6177E8, 0.536f, 230, 0.75f, false}, {'-', 39, 0x61259F, 0.303f, 270, 0.62f, false},
};
static int g_idx[256];
static double g_look[NB][NB];
static bool g_ready = false;

static const char EARTHY[] = "679i3h"; /* muddy beads: fine in themes / dark skins */
static const char GLOW[] = "pywsag,vubn"; /* glow against a dark base */

static double lin(int v) {
  double c = v / 255.0;
  return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
static double labf(double v) { return v > 0.008856 ? cbrt(v) : 7.787 * v + 16.0 / 116.0; }

static void init_beads(void) {
  if (g_ready) return;
  for (int i = 0; i < 256; i++) g_idx[i] = -1;
  for (int i = 0; i < NB; i++) {
    bead* b = &g_beads[i];
    g_idx[(uint8_t)b->ch] = i;
    if (b->sat < 0.15f) b->fam = FAM_GREY;
    else if (b->hue < 12 || b->hue >= 345) b->fam = FAM_RED;
    else if (b->hue < 35) b->fam = FAM_ORANGE;
    else if (b->hue < 70) b->fam = FAM_YELLOW;
    else if (b->hue < 170) b->fam = FAM_GREEN;
    else if (b->hue < 205) b->fam = FAM_AQUA;
    else if (b->hue < 250) b->fam = FAM_BLUE;
    else if (b->hue < 285) b->fam = FAM_PURPLE;
    else b->fam = FAM_PINK;
    double r = lin((b->rgb >> 16) & 255), g = lin((b->rgb >> 8) & 255), bl = lin(b->rgb & 255);
    double x = (0.4124 * r + 0.3576 * g + 0.1805 * bl) / 0.95047;
    double y = 0.2126 * r + 0.7152 * g + 0.0722 * bl;
    double z = (0.0193 * r + 0.1192 * g + 0.9505 * bl) / 1.08883;
    b->lab[0] = 116 * labf(y) - 16;
    b->lab[1] = 500 * (labf(x) - labf(y));
    b->lab[2] = 200 * (labf(y) - labf(z));
  }
  for (int i = 0; i < NB; i++)
    for (int j = 0; j < NB; j++) {
      double a = g_beads[i].lab[0] - g_beads[j].lab[0];
      double b = g_beads[i].lab[1] - g_beads[j].lab[1];
      double c = g_beads[i].lab[2] - g_beads[j].lab[2];
      g_look[i][j] = sqrt(a * a + b * b + c * c);
    }
  g_ready = true;
}

static inline int I(char c) { return g_idx[(uint8_t)c]; }
static inline const bead* B(char c) { return &g_beads[g_idx[(uint8_t)c]]; }
static inline double look(char a, char b) { return g_look[I(a)][I(b)]; }
static inline bool official(char c) { return c && g_idx[(uint8_t)c] >= 0; }
static bool in_str(const char* s, char c) { return c && strchr(s, c) != NULL; }

/* ======================================================================
   Dice: mulberry32, deterministic for a given seed.
   ====================================================================== */

typedef struct { uint32_t a; } dice;

static double d_next(dice* d) {
  d->a += 0x6d2b79f5u;
  uint32_t t = d->a;
  t = (t ^ (t >> 15)) * (t | 1u);
  t ^= t + (t ^ (t >> 7)) * (t | 61u);
  return (double)(t ^ (t >> 14)) / 4294967296.0;
}
static int d_int(dice* d, int lo, int hi) { return lo + (int)floor(d_next(d) * (hi - lo + 1)); }
static bool d_chance(dice* d, double p) { return d_next(d) < p; }
static char d_pick(dice* d, const char* list, int n) { return list[(int)floor(d_next(d) * n)]; }
static void d_shuffle(dice* d, char* a, int n) {
  for (int i = n - 1; i > 0; i--) {
    int j = (int)floor(d_next(d) * (i + 1));
    char x = a[i]; a[i] = a[j]; a[j] = x;
  }
}
static int d_weighted(dice* d, const int* w, int n) {
  int sum = 0;
  for (int i = 0; i < n; i++) sum += w[i];
  double r = d_next(d) * sum;
  for (int i = 0; i < n; i++) { r -= w[i]; if (r < 0) return i; }
  return n - 1;
}

static uint32_t mix_seed(uint32_t seed, int n) {
  uint32_t h = seed ^ ((uint32_t)(n + 1) * 0x9e3779b9u);
  h = (h ^ (h >> 16)) * 0x85ebca6bu;
  h = (h ^ (h >> 13)) * 0xc2b2ae35u;
  return h ^ (h >> 16);
}

static int jround(double x) { return (int)floor(x + 0.5); }

/* ======================================================================
   Palettes
   ====================================================================== */

typedef struct { char c[MAX_PAL]; int n; } pal;
typedef struct { char c[MAX_POOL]; int n; } pool_t;

static int hue_gap(int a, int b) {
  int d = abs(a - b) % 360;
  return d > 180 ? 360 - d : d;
}

static bool add_distinct(pal* p, char ch, double min_look) {
  for (int i = 0; i < p->n; i++)
    if (look(p->c[i], ch) < min_look) return false;
  if (p->n >= MAX_PAL) return false;
  p->c[p->n++] = ch;
  return true;
}
static void pool_add(pool_t* p, char c) { if (p->n < MAX_POOL) p->c[p->n++] = c; }
static bool pool_has(const pool_t* p, char c) {
  for (int i = 0; i < p->n; i++) if (p->c[i] == c) return true;
  return false;
}

enum { KEY_L, KEY_HUE, KEY_HUEGAP, KEY_COMPL };
static double key_of(char c, int kind, int ref) {
  const bead* b = B(c);
  switch (kind) {
    case KEY_L: return b->L;
    case KEY_HUE: return b->hue;
    case KEY_HUEGAP: return hue_gap(b->hue, ref);
    default: return abs(hue_gap(b->hue, ref) - 180);
  }
}
/* stable insertion sort by (key, then secondary tie-break) */
static void sort_chars(char* a, int n, int kind, int ref) {
  for (int i = 1; i < n; i++) {
    char x = a[i];
    double kx = key_of(x, kind, ref);
    int j = i - 1;
    while (j >= 0) {
      double kj = key_of(a[j], kind, ref);
      bool greater = kj > kx || (kj == kx && (kind == KEY_HUE ? B(a[j])->L > B(x)->L
                                                               : B(a[j])->id > B(x)->id));
      if (!greater) break;
      a[j + 1] = a[j];
      j--;
    }
    a[j + 1] = x;
  }
}
static pool_t filter_pool(const pool_t* p, bool (*fn)(const bead*, void*), void* ctx) {
  pool_t o = {{0}, 0};
  for (int i = 0; i < p->n; i++) if (fn(B(p->c[i]), ctx)) pool_add(&o, p->c[i]);
  return o;
}
static bool f_sat03(const bead* b, void* x) { (void)x; return b->sat >= 0.3f; }
static bool f_sat04(const bead* b, void* x) { (void)x; return b->sat >= 0.4f; }
static bool f_sat045(const bead* b, void* x) { (void)x; return b->sat >= 0.45f; }
static bool f_light(const bead* b, void* x) { (void)x; return b->L >= 0.62f; }
static bool f_dark(const bead* b, void* x) { (void)x; return b->L < 0.45f; }
static bool f_soft(const bead* b, void* x) { (void)x; return b->L >= 0.6f && b->sat >= 0.3f; }
static bool f_darkpool(const bead* b, void* x) { (void)x; return b->L < 0.45f; }

static char pick_or_any(dice* d, const pool_t* coloured, const pool_t* pool) {
  const pool_t* s = coloured->n ? coloured : pool;
  return d_pick(d, s->c, s->n);
}

static pal rule_analogous(const pool_t* pool, int k, dice* d) {
  pool_t col = filter_pool(pool, f_sat03, NULL);
  char first = pick_or_any(d, &col, pool);
  pal out = {{first}, 1};
  char s[MAX_POOL]; memcpy(s, pool->c, pool->n);
  sort_chars(s, pool->n, KEY_HUEGAP, B(first)->hue);
  for (int i = 0; i < pool->n; i++) {
    if (out.n >= k) break;
    if (hue_gap(B(s[i])->hue, B(first)->hue) <= 70 && B(s[i])->sat >= 0.3f) add_distinct(&out, s[i], 18.0);
  }
  return out;
}
static pal rule_complement(const pool_t* pool, int k, dice* d) {
  pool_t col = filter_pool(pool, f_sat04, NULL);
  char first = pick_or_any(d, &col, pool);
  pal out = {{first}, 1};
  char s[MAX_POOL]; memcpy(s, pool->c, pool->n);
  sort_chars(s, pool->n, KEY_COMPL, B(first)->hue);
  for (int i = 0; i < pool->n; i++)
    if (B(s[i])->sat >= 0.3f && add_distinct(&out, s[i], 30.0)) break;
  char nb[5] = {'s', 'f', '4', 't', 'c'};
  d_shuffle(d, nb, 5);
  for (int i = 0; i < 5; i++) {
    if (!pool_has(pool, nb[i])) continue;
    if (out.n >= k) break;
    add_distinct(&out, nb[i], 25.0);
  }
  return out;
}
static pal rule_triad(const pool_t* pool, int k, dice* d) {
  pool_t col = filter_pool(pool, f_sat04, NULL);
  char first = pick_or_any(d, &col, pool);
  pal out = {{first}, 1};
  const int shifts[2] = {120, 240};
  for (int s = 0; s < 2; s++) {
    int target = (B(first)->hue + shifts[s]) % 360;
    char arr[MAX_POOL]; memcpy(arr, col.c, col.n);
    sort_chars(arr, col.n, KEY_HUEGAP, target);
    for (int i = 0; i < col.n; i++) if (add_distinct(&out, arr[i], 25.0)) break;
  }
  int keep = k > 2 ? k : 2;
  if (out.n > keep) out.n = keep;
  return out;
}
static pal rule_mono(const pool_t* pool, int k, dice* d) {
  pool_t col = filter_pool(pool, f_sat03, NULL);
  char first = pick_or_any(d, &col, pool);
  char fam[MAX_POOL]; int fn = 0;
  for (int i = 0; i < pool->n; i++) {
    char c = pool->c[i];
    if (B(c)->fam == B(first)->fam || hue_gap(B(c)->hue, B(first)->hue) <= 25) fam[fn++] = c;
  }
  sort_chars(fam, fn, KEY_L, 0);
  pal out = {{0}, 0};
  for (int i = 0; i < fn; i++) add_distinct(&out, fam[i], 14.0);
  if (out.n < 2) {
    if (pool_has(pool, 's')) add_distinct(&out, 's', 18.0);
    if (pool_has(pool, 'f')) add_distinct(&out, 'f', 18.0);
  }
  if (out.n > k) {
    int step = (out.n + k - 1) / k;
    pal o2 = {{0}, 0};
    for (int i = 0; i < out.n && o2.n < k; i++) if (i % step == 0) o2.c[o2.n++] = out.c[i];
    return o2;
  }
  return out;
}
static pal rule_contrast(const pool_t* pool, int k, dice* d) {
  pool_t lights = filter_pool(pool, f_light, NULL);
  pool_t darks = filter_pool(pool, f_dark, NULL);
  pal out = {{0}, 0};
  if (lights.n) out.c[out.n++] = d_pick(d, lights.c, lights.n);
  if (darks.n) add_distinct(&out, d_pick(d, darks.c, darks.n), 35.0);
  char s[MAX_POOL]; memcpy(s, pool->c, pool->n);
  d_shuffle(d, s, pool->n);
  for (int i = 0; i < pool->n; i++) {
    if (out.n >= k) break;
    add_distinct(&out, s[i], 25.0);
  }
  return out;
}
static pal rule_candy(const pool_t* pool, int k, dice* d) {
  pool_t soft = filter_pool(pool, f_soft, NULL);
  pal out = {{0}, 0};
  if (!soft.n) return out;
  char first = d_pick(d, soft.c, soft.n);
  out.c[out.n++] = first;
  if (pool_has(pool, 's') && d_chance(d, 0.6)) out.c[out.n++] = 's';
  char near_[MAX_POOL]; int nn = 0;
  for (int i = 0; i < soft.n; i++)
    if (hue_gap(B(soft.c[i])->hue, B(first)->hue) <= 60) near_[nn++] = soft.c[i];
  sort_chars(near_, nn, KEY_HUEGAP, B(first)->hue);
  for (int i = 0; i < nn; i++) {
    if (out.n >= k) break;
    add_distinct(&out, near_[i], 18.0);
  }
  return out;
}
static pal rule_rainbow(const pool_t* pool, int k, dice* d) {
  pool_t col = filter_pool(pool, f_sat045, NULL);
  pal out = {{0}, 0};
  if (!col.n) return out;
  sort_chars(col.c, col.n, KEY_HUE, 0);
  int n = k > 3 ? k : 3;
  int off = d_int(d, 0, col.n - 1);
  for (int i = 0; i < col.n && out.n < n; i++)
    add_distinct(&out, col.c[(off + jround((double)i * col.n / n)) % col.n], 20.0);
  return out;
}

enum { R_ANALOGOUS, R_COMPLEMENT, R_TRIAD, R_MONO, R_CONTRAST, R_CANDY, R_RAINBOW };
static pal run_rule(int r, const pool_t* pool, int k, dice* d) {
  switch (r) {
    case R_ANALOGOUS: return rule_analogous(pool, k, d);
    case R_COMPLEMENT: return rule_complement(pool, k, d);
    case R_TRIAD: return rule_triad(pool, k, d);
    case R_MONO: return rule_mono(pool, k, d);
    case R_CONTRAST: return rule_contrast(pool, k, d);
    case R_CANDY: return rule_candy(pool, k, d);
    default: return rule_rainbow(pool, k, d);
  }
}

static const int MOOD_LO[SKIN_MOOD_COUNT] = {2, 2, 2, 2, 3};
static const int MOOD_HI[SKIN_MOOD_COUNT] = {4, 3, 5, 3, 4};

static const char* THEME_CHARS[SKIN_THEME_COUNT] = {
    "y7,5ngw", "scuxk", "fph9v", "4t-qzea", "4eljucs", "mszvbu",
    "-a,yngw", "t-qwgs", "fw3b", "f5,3", "4t-8f", "h9ov67"};
static const char* THEME_NAMES[SKIN_THEME_COUNT] = {
    "Fire", "Ice", "Toxic", "Galaxy", "Ocean", "Candy",
    "Sunset", "Royal", "Warning", "Blood", "Void", "Forest"};

static pool_t pool_for(const skin_gen_recipe* r, int mood) {
  int tone = r->tone;
  if (tone == SKIN_TONE_AUTO) {
    tone = mood == SKIN_MOOD_CUTE ? SKIN_TONE_LIGHT : mood == SKIN_MOOD_DANGER ? SKIN_TONE_DARK : SKIN_TONE_MIX;
  }
  pool_t p = {{0}, 0};
  for (int i = 0; i < NB; i++) {
    const bead* b = &g_beads[i];
    if (b->stars) continue;
    if (mood != SKIN_MOOD_DANGER && tone != SKIN_TONE_DARK && in_str(EARTHY, b->ch)) continue;
    if (mood == SKIN_MOOD_CUTE && (b->ch == 'p' || b->ch == 'y')) continue;
    if (tone == SKIN_TONE_LIGHT && b->L < 0.6f) continue;
    if (tone == SKIN_TONE_DARK && b->L >= 0.55f) continue;
    pool_add(&p, b->ch);
  }
  return p;
}

/* Returns the palette; *rule_out is just informational. */
static pal pick_palette(const skin_gen_recipe* r, int mood, dice* d, const pool_t* pool_ret_unused,
                        pool_t* pool_out) {
  (void)pool_ret_unused;
  int lo = MOOD_LO[mood], hi = MOOD_HI[mood];

  if (r->own_n > 0) {
    char own[40]; int on = 0;
    for (int i = 0; i < r->own_n && i < 40; i++) if (official(r->own[i])) own[on++] = r->own[i];
    d_shuffle(d, own, on);
    pal p = {{0}, 0};
    for (int i = 0; i < on; i++) add_distinct(&p, own[i], SAME_LOOK);
    int k = d_int(d, lo, hi);
    if (k < 2) k = 2;
    if (k > p.n) k = p.n;
    if (p.n > k) p.n = k;
    *pool_out = (pool_t){{0}, 0};
    for (int i = 0; i < p.n; i++) pool_add(pool_out, p.c[i]);
    return p;
  }

  if (r->theme >= 0 && r->theme < SKIN_THEME_COUNT) {
    char tc[16]; int tn = (int)strlen(THEME_CHARS[r->theme]);
    memcpy(tc, THEME_CHARS[r->theme], tn);
    d_shuffle(d, tc, tn);
    int k = d_int(d, lo > 2 ? lo : 2, hi > 3 ? hi : 3);
    pal p = {{0}, 0};
    for (int i = 0; i < tn; i++) { if (p.n >= k) break; add_distinct(&p, tc[i], 16.0); }
    *pool_out = (pool_t){{0}, 0};
    for (int i = 0; i < tn; i++) pool_add(pool_out, tc[i]);
    return p;
  }

  pool_t pool = pool_for(r, mood);
  *pool_out = pool;
  int k = d_int(d, lo, hi);

  if (mood == SKIN_MOOD_DANGER) {
    pool_t darks = filter_pool(&pool, f_darkpool, NULL);
    pool_t* src = darks.n ? &darks : &pool;
    char s[MAX_POOL]; memcpy(s, src->c, src->n);
    d_shuffle(d, s, src->n);
    pal base = {{0}, 0};
    for (int i = 0; i < src->n; i++) { if (base.n >= k - 1) break; add_distinct(&base, s[i], 16.0); }
    char glows[16]; int gn = 0;
    for (const char* g = GLOW; *g; g++) {
      bool ok = true;
      for (int i = 0; i < base.n; i++) if (look(base.c[i], *g) < 45.0) { ok = false; break; }
      if (ok) glows[gn++] = *g;
    }
    char g = gn ? d_pick(d, glows, gn) : 's';
    if (base.n < MAX_PAL) base.c[base.n++] = g;
    return base;
  }

  static const int cute_rules[4] = {R_CANDY, R_ANALOGOUS, R_MONO, R_RAINBOW}, cute_w[4] = {4, 3, 2, 1};
  static const int rnd_rules[6] = {R_ANALOGOUS, R_COMPLEMENT, R_TRIAD, R_MONO, R_CONTRAST, R_RAINBOW},
                   rnd_w[6] = {2, 2, 2, 1, 1, 1};
  static const int pat_rules[3] = {R_CONTRAST, R_COMPLEMENT, R_TRIAD}, pat_w[3] = {3, 3, 1};
  static const int uni_rules[4] = {R_TRIAD, R_COMPLEMENT, R_RAINBOW, R_CONTRAST}, uni_w[4] = {2, 2, 1, 1};
  int rule;
  switch (mood) {
    case SKIN_MOOD_CUTE: rule = cute_rules[d_weighted(d, cute_w, 4)]; break;
    case SKIN_MOOD_RANDOM: rule = rnd_rules[d_weighted(d, rnd_w, 6)]; break;
    case SKIN_MOOD_PATTERN: rule = pat_rules[d_weighted(d, pat_w, 3)]; break;
    default: rule = uni_rules[d_weighted(d, uni_w, 4)]; break;
  }
  pal colours = run_rule(rule, &pool, k, d);
  if (mood == SKIN_MOOD_CUTE && rule != R_CANDY && colours.n < 4 && d_chance(d, 0.3))
    add_distinct(&colours, 's', 20.0);
  if (mood == SKIN_MOOD_UNIQUE && d_chance(d, 0.5)) {
    char star = d_pick(d, "de", 2);
    bool ok = true;
    for (int i = 0; i < colours.n; i++) if (look(colours.c[i], star) < SAME_LOOK) { ok = false; break; }
    if (ok && colours.n < MAX_PAL) colours.c[colours.n++] = star;
  }
  if (colours.n < 2) colours = rule_contrast(&pool, 2, d);
  return colours;
}

/* ======================================================================
   Patterns (lists of runs: colour + width in beads)
   ====================================================================== */

typedef struct { char ch; int n; } run_t;
typedef struct { run_t r[RMAX]; int n; } runs_t;

static void r_add(runs_t* R, char ch, int n) {
  if (R->n >= RMAX) return;
  R->r[R->n].ch = ch;
  R->r[R->n].n = n < 1 ? 1 : n;
  R->n++;
}
static int imax(int a, int b) { return a > b ? a : b; }

static int to_code(const runs_t* R, char* out, int cap) {
  int n = 0;
  for (int i = 0; i < R->n; i++)
    for (int k = 0; k < R->r[i].n && n < cap - 1; k++) out[n++] = R->r[i].ch;
  out[n] = 0;
  return n;
}
static void to_runs(const char* code, int len, runs_t* R) {
  R->n = 0;
  for (int i = 0; i < len; i++) {
    if (R->n && R->r[R->n - 1].ch == code[i]) R->r[R->n - 1].n++;
    else r_add(R, code[i], 1);
  }
}
static void rebuild(runs_t* R) {
  char buf[CODE_BUF];
  int n = to_code(R, buf, CODE_BUF);
  to_runs(buf, n, R);
}
static int arena_runs(const char* code, int len) {
  runs_t R; to_runs(code, len, &R);
  int s = 0;
  for (int i = 0; i < R.n; i++) s += (R.r[i].n + 254) / 255;
  return s;
}

static void by_l(const char* in, int n, char* out, bool rev) {
  memcpy(out, in, n);
  sort_chars(out, n, KEY_L, 0);
  if (rev) for (int i = 0; i < n / 2; i++) { char t = out[i]; out[i] = out[n - 1 - i]; out[n - 1 - i] = t; }
}
static void by_hue(const char* in, int n, char* out) {
  memcpy(out, in, n);
  sort_chars(out, n, KEY_HUE, 0);
}

static void build(int style, const char* P, int np, int u, dice* d, runs_t* R) {
  R->n = 0;
  char s[MAX_PAL];
  switch (style) {
    case SKIN_STYLE_STRIPES:
      for (int i = 0; i < np; i++) r_add(R, P[i], u);
      break;
    case SKIN_STYLE_THICKTHIN: {
      by_l(P, np, s, !d_chance(d, 0.6));
      for (int i = 1; i < np; i++) {
        r_add(R, s[0], u * d_int(d, 2, 3));
        r_add(R, s[i], imax(ACCENT, jround(u / 2.0)));
      }
      break;
    }
    case SKIN_STYLE_RAMP: {
      if (d_chance(d, 0.5)) by_l(P, np, s, false); else by_hue(P, np, s);
      int w = imax(3, jround(u * 0.8));
      for (int i = 0; i < np; i++) r_add(R, s[i], w);
      if (np > 2) for (int i = np - 2; i >= 1; i--) r_add(R, s[i], w);
      break;
    }
    case SKIN_STYLE_RAINBOW:
      by_hue(P, np, s);
      for (int i = 0; i < np; i++) r_add(R, s[i], u);
      break;
    case SKIN_STYLE_PULSE: {
      by_l(P, np, s, !d_chance(d, 0.5));
      for (int i = 1; i < np; i++) {
        r_add(R, s[0], u * d_int(d, 2, 4));
        r_add(R, s[i], ACCENT);
      }
      break;
    }
    case SKIN_STYLE_BAND: {
      by_l(P, np, s, false);
      char a = d_chance(d, 0.5) ? s[0] : s[np - 1];
      char rest[MAX_PAL]; int rn = 0;
      for (int i = 0; i < np; i++) if (s[i] != a) rest[rn++] = s[i];
      char edge = rn ? rest[rn - 1] : a;
      char mid = rn > 1 ? rest[0] : edge;
      r_add(R, a, u * d_int(d, 3, 5));
      r_add(R, edge, ACCENT);
      r_add(R, mid, u);
      r_add(R, edge, ACCENT);
      break;
    }
    case SKIN_STYLE_DUO: {
      int w = imax(3, u - 1);
      memcpy(s, P, np);
      d_shuffle(d, s, np);
      r_add(R, s[0], w);
      r_add(R, s[1 % np], w);
      break;
    }
    case SKIN_STYLE_FIB: {
      static const int seq[5] = {2, 3, 5, 8, 13};
      if (d_chance(d, 0.5)) by_l(P, np, s, false); else by_hue(P, np, s);
      for (int i = 0; i < 5; i++) r_add(R, s[i % np], seq[i]);
      break;
    }
    case SKIN_STYLE_WEAVE: {
      memcpy(s, P, np);
      d_shuffle(d, s, np);
      char a = s[0], b = s[1 % np], c = s[2 % np];
      r_add(R, a, u); r_add(R, b, ACCENT); r_add(R, c, u); r_add(R, b, ACCENT);
      break;
    }
    case SKIN_STYLE_CHAOS: {
      char prev = 0;
      int n = d_int(d, 5, 9);
      for (int i = 0; i < n; i++) {
        char c = d_pick(d, P, np);
        int guard = 0;
        while (prev && look(c, prev) < SAME_LOOK && guard++ < 10) c = d_pick(d, P, np);
        r_add(R, c, d_int(d, ACCENT, u + 2));
        prev = c;
      }
      break;
    }
    case SKIN_STYLE_COMET: {
      by_l(P, np, s, d_chance(d, 0.5));
      for (int i = 0; i < np; i++)
        r_add(R, s[i], imax(ACCENT, jround(u * 0.5 * pow(1.55, i))));
      break;
    }
    case SKIN_STYLE_CHECKER: {
      int w = imax(3, u - 1);
      memcpy(s, P, np);
      d_shuffle(d, s, np);
      char a = s[0], b = s[1 % np], c = s[2 % np];
      int reps = d_int(d, 2, 4);
      bool three = np >= 3 && d_chance(d, 0.5);
      for (int i = 0; i < reps; i++) {
        r_add(R, a, w);
        r_add(R, (three && (i & 1)) ? c : b, w);
      }
      break;
    }
    default: { /* starry */
      char plain[MAX_PAL]; int pn = 0; char star = 0;
      for (int i = 0; i < np; i++) {
        if (B(P[i])->stars) { if (!star) star = P[i]; }
        else plain[pn++] = P[i];
      }
      if (!pn) plain[pn++] = '4';
      if (!star) star = d_pick(d, "de", 2);
      for (int i = 0; i < pn; i++) { r_add(R, plain[i], u * 2); r_add(R, star, ACCENT + 1); }
      break;
    }
  }
}

static void fix_neighbours(runs_t* R, const char* P, int np, dice* d) {
  for (int pass = 0; pass < 4; pass++) {
    bool changed = false;
    for (int i = 0; i < R->n && R->n > 1; i++) {
      char prev = R->r[(i - 1 + R->n) % R->n].ch;
      char next = R->r[(i + 1) % R->n].ch;
      char c = R->r[i].ch;
      bool clash = (prev != c && look(prev, c) < SAME_LOOK) || (next != c && look(next, c) < SAME_LOOK);
      if (clash) {
        char opts[MAX_PAL]; int on = 0;
        for (int k = 0; k < np; k++)
          if (look(P[k], prev) >= SAME_LOOK && look(P[k], next) >= SAME_LOOK) opts[on++] = P[k];
        R->r[i].ch = on ? d_pick(d, opts, on) : prev;
        changed = true;
      }
    }
    rebuild(R);
    if (!changed) break;
  }
}

static void mutate(runs_t* R, const char* P, int np, double wild, dice* d) {
  if (R->n < 1) return;
  int n = jround(wild * R->n * 0.6);
  for (int k = 0; k < n; k++) {
    int i = d_int(d, 0, R->n - 1);
    switch (d_int(d, 0, 2)) {
      case 0: R->r[i].ch = d_pick(d, P, np); break;
      case 1: R->r[i].n = imax(ACCENT, R->r[i].n + (d_chance(d, 0.5) ? 1 : -1) * d_int(d, 1, 2)); break;
      default: {
        int j = d_int(d, 0, R->n - 1);
        run_t x = R->r[i]; R->r[i] = R->r[j]; R->r[j] = x;
      }
    }
  }
}

/* Keep the code inside the arena: <= max_beads beads, <= 146 runs. */
static void fit_arena(runs_t* R, int max_beads) {
  int total = 0;
  for (int i = 0; i < R->n; i++) total += R->r[i].n;
  if (total > max_beads) {
    double k = (double)max_beads / total;
    for (int i = 0; i < R->n; i++) R->r[i].n = imax(1, (int)floor(R->r[i].n * k));
  }
  while (R->n > SKIN_GEN_MAX_RUNS) {
    int i = 0;
    for (int j = 1; j < R->n; j++) if (R->r[j].n < R->r[i].n) i = j;
    R->r[i > 0 ? i - 1 : 1].n += R->r[i].n;
    for (int j = i; j < R->n - 1; j++) R->r[j] = R->r[j + 1];
    R->n--;
    rebuild(R);
  }
}

/* ======================================================================
   Judging
   ====================================================================== */

static bool is_repeat(const char* code, int len, int p) {
  if (len % p) return false;
  for (int i = p; i < len; i++) if (code[i] != code[i - p]) return false;
  return true;
}

/* The code as one canonical loop: smallest rotation of its smallest block. */
static int canonical(const char* code, int len, char* out) {
  for (int p = 1; p <= len / 2; p++)
    if (is_repeat(code, len, p)) { len = p; break; }
  char best[CODE_BUF], cur[CODE_BUF];
  memcpy(best, code, len);
  for (int i = 1; i < len; i++) {
    memcpy(cur, code + i, len - i);
    memcpy(cur + len - i, code, i);
    if (memcmp(cur, best, len) < 0) memcpy(best, cur, len);
  }
  memcpy(out, best, len);
  out[len] = 0;
  return len;
}

static uint64_t fnv64(const char* s, int n) {
  uint64_t h = 1469598103934665603ull;
  for (int i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 1099511628211ull; }
  return h;
}

static int distinct_looks(const char* chars, int n) {
  pal p = {{0}, 0};
  for (int i = 0; i < n; i++) add_distinct(&p, chars[i], SAME_LOOK);
  return p.n;
}
static int unique_chars(const char* code, int len, char* out) {
  int n = 0;
  for (int i = 0; i < len; i++) {
    bool seen = false;
    for (int k = 0; k < n; k++) if (out[k] == code[i]) { seen = true; break; }
    if (!seen && n < 64) out[n++] = code[i];
  }
  return n;
}

static double score_skin(const char* code, int len, int mood, int style, bool allow_earthy) {
  runs_t R; to_runs(code, len, &R);
  char chars[64]; int nc = unique_chars(code, len, chars);
  double edge = 0; int edges = 0;
  for (int i = 0; i < R.n; i++) {
    char a = R.r[i].ch, b = R.r[(i + 1) % R.n].ch;
    if (a == b) continue;
    double dE = look(a, b);
    edge += style == SKIN_STYLE_RAMP ? fmax(0.0, 1 - fabs(dE - 24) / 30) : fmin(1.0, dE / 45);
    edges++;
  }
  edge = edges ? edge / edges : 0;
  int thin = 0;
  for (int i = 0; i < R.n; i++) if (R.r[i].n < ACCENT) thin++;
  double widths = 1 - (double)thin / R.n;
  int fams[16] = {0}, nf = 0;
  for (int i = 0; i < nc; i++) if (!fams[B(chars[i])->fam]++) nf++;
  double calm = (style == SKIN_STYLE_RAINBOW || style == SKIN_STYLE_CHAOS) ? 1.0 : nf <= 3 ? 1.0 : 0.7;
  int earthy = 0;
  for (int i = 0; i < nc; i++) if (in_str(EARTHY, chars[i])) earthy++;
  double clean = (allow_earthy || !earthy) ? 1.0 : 0.6;
  double lmin = 9, lmax = -9;
  for (int i = 0; i < nc; i++) { double l = B(chars[i])->L; if (l < lmin) lmin = l; if (l > lmax) lmax = l; }
  double depth = fmin(1.0, (lmax - lmin) / 0.3);
  int maxc = 0;
  for (int i = 0; i < nc; i++) {
    int c = 0;
    for (int k = 0; k < len; k++) if (code[k] == chars[i]) c++;
    if (c > maxc) maxc = c;
  }
  double share = (double)maxc / len;
  bool free_balance = style == SKIN_STYLE_PULSE || style == SKIN_STYLE_THICKTHIN || style == SKIN_STYLE_BAND ||
                      style == SKIN_STYLE_STARRY || style == SKIN_STYLE_COMET;
  double balance = free_balance ? 1.0 : share <= 0.66 ? 1.0 : 1 - (share - 0.66) * 2.5;
  double meanL = 0; int dark = 0;
  for (int i = 0; i < len; i++) { meanL += B(code[i])->L; if (B(code[i])->L < 0.45f) dark++; }
  meanL /= len;
  double mood_fit = 1.0;
  if (mood == SKIN_MOOD_CUTE) mood_fit = fmin(1.0, fmax(0.0, (meanL - 0.5) / 0.2));
  if (mood == SKIN_MOOD_DANGER) mood_fit = fmin(1.0, (double)dark / len / 0.55);
  double visible = len <= 128 ? 1.0 : 0.7;
  return 0.3 * edge + 0.12 * widths + 0.1 * calm + 0.12 * clean + 0.1 * depth + 0.1 * balance +
         0.11 * mood_fit + 0.05 * visible;
}

static double similar(const skin_gen_skin* a, const skin_gen_skin* b) {
  if (a->key == b->key) return 1.0;
  char ca[64], cb[64];
  int na = unique_chars(a->code, a->len, ca), nb = unique_chars(b->code, b->len, cb);
  int inter = 0;
  for (int i = 0; i < na; i++) for (int k = 0; k < nb; k++) if (ca[i] == cb[k]) { inter++; break; }
  double jac = (double)inter / (na + nb - inter);
  return 0.6 * jac + 0.3 * (a->style == b->style) + 0.1 * (abs(a->len - b->len) <= 3);
}

/* ======================================================================
   One skin
   ====================================================================== */

static const int MS_STYLES[SKIN_MOOD_COUNT][SKIN_STYLE_COUNT] = {
    /*           stri thth ramp rain puls band duo  fib  weav chao star come chec */
    /* cute   */ {3, 0, 3, 3, 2, 0, 2, 0, 0, 0, 0, 0, 1},
    /* danger */ {1, 3, 0, 0, 3, 3, 0, 0, 0, 0, 0, 2, 0},
    /* random */ {2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1},
    /* pattern*/ {2, 2, 0, 0, 0, 2, 3, 1, 3, 0, 0, 0, 3},
    /* unique */ {0, 0, 1, 0, 0, 1, 0, 2, 2, 2, 2, 2, 0},
};
static const int SIZE_LO[SKIN_SIZE_COUNT] = {3, 4, 6};
static const int SIZE_HI[SKIN_SIZE_COUNT] = {4, 6, 9};

void skin_gen_default_recipe(skin_gen_recipe* r) {
  memset(r, 0, sizeof *r);
  r->mood = SKIN_MOOD_CUTE;
  r->tone = SKIN_TONE_AUTO;
  r->theme = -1;
  r->style = -1;
  r->size = SKIN_SIZE_MEDIUM;
  r->symmetry = SKIN_SYM_AUTO;
  r->wildness = 0.25f;
  r->max_len = SKIN_GEN_MAX_CODE;
  r->random_accessory = false;
  r->own_n = 0;
  r->seed = 1;
}

void skin_gen_own_from_code(skin_gen_recipe* r, const char* code) {
  init_beads();
  r->own_n = 0;
  for (const char* p = code; *p && r->own_n < (int)sizeof r->own; p++) {
    if (!official(*p)) continue;
    bool seen = false;
    for (int i = 0; i < r->own_n; i++) if (r->own[i] == *p) { seen = true; break; }
    if (!seen) r->own[r->own_n++] = *p;
  }
}

bool skin_gen_make(const skin_gen_recipe* r, uint32_t seed, int num_accessories, skin_gen_skin* out) {
  init_beads();
  dice d = {seed};
  int mood = r->mood < 0 || r->mood >= SKIN_MOOD_COUNT ? SKIN_MOOD_RANDOM : r->mood;
  int max_beads = r->max_len <= 0 ? SKIN_GEN_MAX_CODE : r->max_len;
  if (max_beads < 8) max_beads = 8;
  if (max_beads > SKIN_GEN_MAX_CODE) max_beads = SKIN_GEN_MAX_CODE;

  pool_t pool;
  pal pl = pick_palette(r, mood, &d, NULL, &pool);
  if (pl.n < 2 || distinct_looks(pl.c, pl.n) < 2) return false;

  int style = (r->style >= 0 && r->style < SKIN_STYLE_COUNT) ? r->style
                                                              : d_weighted(&d, MS_STYLES[mood], SKIN_STYLE_COUNT);
  if (style == SKIN_STYLE_RAINBOW && pl.n < 3) style = SKIN_STYLE_STRIPES;
  int size = r->size < 0 || r->size >= SKIN_SIZE_COUNT ? SKIN_SIZE_MEDIUM : r->size;
  int u = d_int(&d, SIZE_LO[size], SIZE_HI[size]);

  static runs_t R; /* big; the generator is single-threaded */
  build(style, pl.c, pl.n, u, &d, &R);

  double sym_chance = mood == SKIN_MOOD_CUTE ? 0.5 : mood == SKIN_MOOD_PATTERN ? 0.35 : 0.15;
  bool sym_on = r->symmetry == SKIN_SYM_ON || (r->symmetry == SKIN_SYM_AUTO && d_chance(&d, sym_chance));
  if (sym_on && R.n > 2 && style != SKIN_STYLE_RAMP && style != SKIN_STYLE_DUO) {
    int n0 = R.n;
    for (int i = n0 - 2; i >= 1; i--) r_add(&R, R.r[i].ch, R.r[i].n);
  }
  mutate(&R, pl.c, pl.n, r->wildness, &d);
  rebuild(&R);
  fix_neighbours(&R, pl.c, pl.n, &d);
  fit_arena(&R, max_beads);

  char code[CODE_BUF];
  int len = to_code(&R, code, CODE_BUF);
  if (len < 8 || len > max_beads || arena_runs(code, len) > SKIN_GEN_MAX_RUNS) return false;
  for (int i = 0; i < len; i++) if (!official(code[i])) return false;
  char uc[64]; int nu = unique_chars(code, len, uc);
  if (distinct_looks(uc, nu) < 2) return false;
  for (int i = 0; i < len; i++) {
    char a = code[i], b = code[(i + 1) % len];
    if (a != b && look(a, b) < SAME_LOOK) return false;
  }

  bool allow_earthy = mood == SKIN_MOOD_DANGER || r->theme >= 0 || r->own_n > 0 || r->tone == SKIN_TONE_DARK;
  char canon[CODE_BUF];
  int cl = canonical(code, len, canon);

  memset(out, 0, sizeof *out);
  memcpy(out->code, code, len);
  out->code[len] = 0;
  out->len = len;
  out->runs = arena_runs(code, len);
  out->key = fnv64(canon, cl);
  out->style = style;
  out->mood = mood;
  out->seed = seed;
  out->score = (float)score_skin(code, len, mood, style, allow_earthy);
  out->accessory = SKIN_GEN_KEEP_ACC;
  if (r->random_accessory && num_accessories > 0)
    out->accessory = d_chance(&d, 0.22) ? NO_ACCESSORY : (uint8_t)d_int(&d, 0, num_accessories - 1);
  return true;
}

/* ======================================================================
   The batch
   ====================================================================== */

#define POOL_MAX (SKIN_GEN_MAX_BATCH * 4)

static bool in_history(uint64_t key, const uint64_t* h, int n) {
  for (int i = 0; i < n; i++) if (h[i] == key) return true;
  return false;
}

int skin_gen_batch(const skin_gen_recipe* r, int want, int num_accessories,
                   const uint64_t* history, int n_history, skin_gen_skin* out) {
  static skin_gen_skin pool[POOL_MAX];
  static skin_gen_skin repeats[SKIN_GEN_MAX_BATCH];
  static uint64_t seen[512];
  static int order[POOL_MAX];
  if (want < 1) want = 1;
  if (want > SKIN_GEN_MAX_BATCH) want = SKIN_GEN_MAX_BATCH;
  int np = 0, nr = 0, ns = 0;
  skin_gen_skin tmp;

  for (int n = 0; n < want * 80 && np < want * 4 && ns < 512; n++) {
    if (!skin_gen_make(r, mix_seed(r->seed, n), num_accessories, &tmp)) continue;
    if (in_history(tmp.key, seen, ns)) continue;
    seen[ns++] = tmp.key;
    if (in_history(tmp.key, history, n_history)) {
      if (nr < want) repeats[nr++] = tmp;
      continue;
    }
    pool[np++] = tmp;
  }
  for (int i = 0; i < nr && np < want; i++) pool[np++] = repeats[i];

  for (int i = 0; i < np; i++) order[i] = i;
  for (int i = 1; i < np; i++) { /* best score first, then lower seed */
    int x = order[i], j = i - 1;
    while (j >= 0 && (pool[order[j]].score < pool[x].score ||
                      (pool[order[j]].score == pool[x].score && pool[order[j]].seed > pool[x].seed))) {
      order[j + 1] = order[j];
      j--;
    }
    order[j + 1] = x;
  }

  int no = 0;
  bool used[POOL_MAX] = {false};
  for (int i = 0; i < np && no < want; i++) { /* first pass: skip near copies */
    const skin_gen_skin* s = &pool[order[i]];
    bool dup = false;
    for (int k = 0; k < no; k++) if (similar(&out[k], s) > 0.8) { dup = true; break; }
    if (dup) continue;
    out[no++] = *s;
    used[i] = true;
  }
  for (int i = 0; i < np && no < want; i++) /* second pass: fill up */
    if (!used[i]) out[no++] = pool[order[i]];
  return no;
}

uint32_t skin_gen_new_seed(void) {
  static uint32_t counter = 0;
  uint32_t t = (uint32_t)time(NULL);
  uint32_t c = (uint32_t)clock();
  counter += 0x9e3779b9u;
  return mix_seed(t ^ (c * 2654435761u), (int)counter);
}

/* ======================================================================
   UI names
   ====================================================================== */

static const char* MOOD_NAMES[SKIN_MOOD_COUNT] = {"Cute", "Dangerous", "Random", "Pattern", "Unique"};
static const char* STYLE_NAMES[SKIN_STYLE_COUNT] = {
    "Stripes", "Thick & thin", "Gradient", "Rainbow", "Pulse", "Band", "Duo",
    "Fibonacci", "Weave", "Chaos", "Starry", "Comet", "Checker"};

const char* skin_gen_mood_name(int i) { return i >= 0 && i < SKIN_MOOD_COUNT ? MOOD_NAMES[i] : "?"; }
const char* skin_gen_style_name(int i) { return i >= 0 && i < SKIN_STYLE_COUNT ? STYLE_NAMES[i] : "?"; }
const char* skin_gen_theme_name(int i) { return i >= 0 && i < SKIN_THEME_COUNT ? THEME_NAMES[i] : "None"; }

const char* skin_gen_mood_list(void) { return "Cute\0Dangerous\0Random\0Pattern\0Unique\0"; }
const char* skin_gen_style_list(void) {
  return "Auto\0Stripes\0Thick & thin\0Gradient\0Rainbow\0Pulse\0Band\0Duo\0Fibonacci\0Weave\0Chaos\0Starry\0Comet\0Checker\0";
}
const char* skin_gen_theme_list(void) {
  return "None\0Fire\0Ice\0Toxic\0Galaxy\0Ocean\0Candy\0Sunset\0Royal\0Warning\0Blood\0Void\0Forest\0";
}
const char* skin_gen_tone_list(void) { return "Auto\0Light\0Dark\0Mixed\0"; }
const char* skin_gen_size_list(void) { return "Short bands\0Medium bands\0Long bands\0"; }
const char* skin_gen_sym_list(void) { return "Auto\0Mirrored\0Not mirrored\0"; }
