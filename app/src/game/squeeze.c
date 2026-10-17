#include "squeeze.h"

#include <math.h>
#include <stdlib.h>

#include "../user.h"

/* ---- tuning ------------------------------------------------------------ */

/* Clearance (game units) kept between the two protected dots and an enemy
   body edge. Tiny on purpose: it is what lets the snake squeeze through a gap
   barely wider than the centre line. Raise it (try 1.5-3) if you still die
   while squeezing: the server sees your head slightly behind / ahead of what
   you see, by about (ping x speed). */
#define SQ_MARGIN 0.5f

/* The BLUE dot (head tip) sits SQ_TIP_FACTOR head-radii in front of the RED
   dot (head centre). 0 = protect the red dot only. */
#define SQ_TIP_FACTOR 1.0f

/* Frames looked ahead to decide WHEN to step in. Don't go much below 40: the
   snake needs about that many frames to turn away from a body it heads into.*/
#define SQ_LOOK_STEPS 45
#define SQ_STEP_FRAMES 1.0f

/* Longest look-ahead used to trust an escape heading. */
#define SQ_LOOK_MAX 160

/* Never pick a (nearly) 180 degree reversal: its turn direction is ambiguous
   and flips the snake into the body mid-turn. */
#define SQ_MAX_TURN 2.95f

/* Circle lock: the snake is told to aim this far (radians) ahead of its heading
   on the circle's side, i.e. keep turning at the maximum rate. */
#define SQ_CIRCLE_LEAD 1.2f
/* Per-frame turn (as a fraction of the max turn rate) above which the snake
   counts as "circling" when Squeeze is switched on. */
#define SQ_CIRCLE_DETECT 0.4f

/* Anti-vibration. While sliding along a body the chosen side is kept (a
   head-on push is symmetric, so without this it flips left/right every
   frame); the heading only changes when the change is bigger than the
   deadband. */
/* Release hysteresis: once Squeeze has stepped in, the wanted heading must be
   safe over SQ_RELEASE_FACTOR times the normal look-ahead before control goes
   back to the mouse / circle. Without this the snake sits on the edge of "safe"
   and flips between your aim and the deflection every frame (vibration). */
#define SQ_RELEASE_FACTOR 2
#define SQ_SIDE_PENALTY 0.5f
#define SQ_CONT_WEIGHT 0.15f
#define SQ_DEADBAND 0.0175f /* ~1 degree */

/* The dead-end check looks this many frames further than the normal check, so
   a funnel / closing coil is noticed while there is still room to turn round.
   Circling on the spot counts as a way out, so a pen big enough to circle in is
   never mistaken for a dead end. */
#define SQ_VIABLE_EXTRA 150

#define SQ_MAX_OBS 3072
#define SQ_MAX_CAND 400
/* "Strong" way out = a circle that still fits with this much spare room on
   every side. Preferred whenever one exists, so the snake leaves a closing
   funnel while it still has room to turn instead of riding the very edge of
   "just barely possible". If none exists (a tight pen) the bare minimum is
   accepted. */
#define SQ_VIABLE_SLACK 20.0f

#define SQ_VIABLE_BUDGET 30

/* ------------------------------------------------------------------------ */

typedef struct {
  float x, y, r;
} sq_obs;
typedef struct {
  float x, y, ang;
} sq_state;
typedef struct {
  float lo, hi;
} sq_iv;
typedef struct {
  float ang, cost;
} sq_cand;

static sq_obs g_obs[SQ_MAX_OBS];
static float g_reff[SQ_MAX_OBS];
static int g_obs_n;
static int g_viable_budget;

/* Spatial grid over the obstacles so a collision check only looks at the few
   circles near a dot instead of all of them. */
#define SQ_GN 64
static int g_cell_start[SQ_GN * SQ_GN + 1];
static int g_cell_idx[SQ_MAX_OBS];
static float g_gx0, g_gy0, g_cell;

static sq_iv g_iv[4 * SQ_MAX_OBS + 8];
static int g_iv_n;
static sq_cand g_cand[SQ_MAX_CAND];
static int g_cand_n;

static bool g_deflecting = false;
static int g_circle_dir = 0; /* -1 / +1 locked circle, 0 = manual */
static int g_side = 0;       /* side the snake slides on, 0 = none yet */
static int g_quiet = 0;      /* frames since the last deflection */
static bool g_have_prev = false;
static float g_prev_cmd = 0.0f;

bool squeeze_is_deflecting(void) { return g_deflecting; }
bool squeeze_is_circling(void) { return g_circle_dir != 0; }

static float wrap_pi(float a) {
  a = fmodf(a, 2.0f * (float)M_PI);
  if (a > (float)M_PI) a -= 2.0f * (float)M_PI;
  if (a < -(float)M_PI) a += 2.0f * (float)M_PI;
  return a;
}

static float turn_rate(game_data* gdata, snake* me) {
  float m = gdata->data.mamu * me->scang * me->spang;
  return m < 0.001f ? 0.001f : m;
}

/* ---- circle detection --------------------------------------------------- */

void squeeze_observe(game_data* gdata, snake* me, bool active) {
  static bool have = false, was_active = false;
  static float prev_ang = 0.0f, ema = 0.0f;

  if (!me || me->dead) {
    have = false;
    g_circle_dir = 0;
    g_have_prev = false;
    was_active = active;
    return;
  }
  if (have)
    ema = ema * 0.85f + wrap_pi(me->ang - prev_ang) * 0.15f;
  else
    ema = 0.0f;
  prev_ang = me->ang;
  have = true;

  if (active && !was_active) {
    g_circle_dir = 0;
    if (fabsf(ema) > SQ_CIRCLE_DETECT * turn_rate(gdata, me))
      g_circle_dir = ema > 0 ? 1 : -1;
    g_side = 0;
    g_quiet = 1000;
    g_have_prev = false;
  }
  if (!active) {
    g_circle_dir = 0;
    g_have_prev = false;
  }
  was_active = active;
}

/* ---- obstacles ---------------------------------------------------------- */

/* Enemy heads and bodies are treated exactly alike: static circles. */
static void add_obs(float x, float y, float r) {
  if (g_obs_n >= SQ_MAX_OBS) return;
  g_obs[g_obs_n++] = (sq_obs){x, y, r};
}

static void gather(game_data* gdata, snake* me, float reach) {
  g_obs_n = 0;
  int ns = tdarray_length(gdata->data.snakes);
  for (int i = 0; i < ns; i++) {
    snake* s = gdata->data.snakes + i;
    if (s == me || s->id == me->id) continue;
    float sr = roundf(s->sc * 29.0f) * 0.5f;
    float rr = (reach + sr) * (reach + sr);

    float hx = s->xx - me->xx, hy = s->yy - me->yy;
    if (hx * hx + hy * hy <= rr) add_obs(s->xx, s->yy, sr);

    int pn = tdarray_length(s->pts);
    for (int j = 0; j < pn; j++) {
      body_part* po = s->pts + j;
      if (po->dying) continue;
      float dx = po->xx - me->xx, dy = po->yy - me->yy;
      if (dx * dx + dy * dy <= rr) add_obs(po->xx, po->yy, sr);
    }
  }
}

static int cell_of(float v, float v0, int* out) {
  int c = (int)floorf((v - v0) / g_cell);
  if (c < 0 || c >= SQ_GN) return 0;
  *out = c;
  return 1;
}

/* Build the grid. `maxr` = largest radius any obstacle can be asked to keep
   a dot out of (including the viable() slack); the cell is never smaller, so
   checking the 3x3 cells around a dot is always enough. */
static void build_grid(const sq_state* now, float reach, float maxr) {
  g_cell = maxr + 1.0f;
  float need = 2.0f * (reach + 120.0f) / (float)SQ_GN;
  if (g_cell < need) g_cell = need;
  g_gx0 = now->x - g_cell * SQ_GN * 0.5f;
  g_gy0 = now->y - g_cell * SQ_GN * 0.5f;

  static int cnt[SQ_GN * SQ_GN + 1];
  static int cellof[SQ_MAX_OBS];
  for (int i = 0; i <= SQ_GN * SQ_GN; i++) cnt[i] = 0;
  for (int k = 0; k < g_obs_n; k++) {
    int cx, cy;
    if (cell_of(g_obs[k].x, g_gx0, &cx) && cell_of(g_obs[k].y, g_gy0, &cy))
      cellof[k] = cy * SQ_GN + cx;
    else
      cellof[k] = -1;
    if (cellof[k] >= 0) cnt[cellof[k] + 1]++;
  }
  for (int i = 0; i < SQ_GN * SQ_GN; i++) cnt[i + 1] += cnt[i];
  for (int i = 0; i <= SQ_GN * SQ_GN; i++) g_cell_start[i] = cnt[i];
  static int fill[SQ_GN * SQ_GN];
  for (int i = 0; i < SQ_GN * SQ_GN; i++) fill[i] = 0;
  for (int k = 0; k < g_obs_n; k++)
    if (cellof[k] >= 0) {
      int c = cellof[k];
      g_cell_idx[g_cell_start[c] + fill[c]++] = k;
    }
}

/* Is the point inside any obstacle's keep-out circle? */
static bool point_hits(float px, float py) {
  int cx, cy;
  if (!cell_of(px, g_gx0, &cx) || !cell_of(py, g_gy0, &cy)) return false;
  for (int yy = cy - 1; yy <= cy + 1; yy++) {
    if (yy < 0 || yy >= SQ_GN) continue;
    for (int xx = cx - 1; xx <= cx + 1; xx++) {
      if (xx < 0 || xx >= SQ_GN) continue;
      int c = yy * SQ_GN + xx;
      for (int i = g_cell_start[c]; i < g_cell_start[c + 1]; i++) {
        int k = g_cell_idx[i];
        float dx = px - g_obs[k].x, dy = py - g_obs[k].y;
        if (dx * dx + dy * dy < g_reff[k] * g_reff[k]) return true;
      }
    }
  }
  return false;
}

/* ---- simulation ---------------------------------------------------------- */

/* Move the head for `steps` frames and return how many it survived without
   either dot touching anything (== steps: fully safe). cturn != 0: keep
   turning that way at the maximum rate (circling); otherwise turn toward the
   fixed heading `target` and then go straight. */
static int simulate(sq_state st, float target, int cturn, float csp, float mang,
                    float tip, int steps, sq_state* end) {
  float x = st.x, y = st.y, ang = st.ang;
  int survived = steps;
  float turn = mang * SQ_STEP_FRAMES;
  for (int i = 1; i <= steps; i++) {
    if (cturn)
      ang += cturn * turn;
    else {
      float diff = wrap_pi(target - ang);
      if (fabsf(diff) <= turn)
        ang = target;
      else
        ang += diff < 0 ? -turn : turn;
    }
    x += cosf(ang) * csp * SQ_STEP_FRAMES;
    y += sinf(ang) * csp * SQ_STEP_FRAMES;
    float bx = x + cosf(ang) * tip; /* blue dot */
    float by = y + sinf(ang) * tip;

    bool hit = point_hits(x, y) || (tip > 0.0f && point_hits(bx, by));
    if (hit) {
      survived = i - 1;
      break;
    }
  }
  if (end) *end = (sq_state){x, y, ang};
  return survived;
}

static int commit_steps(float cur, float target, float mang) {
  float d = fabsf(wrap_pi(target - cur));
  if (d > SQ_MAX_TURN) return -1;
  int n = SQ_LOOK_STEPS + (int)(d / (mang * SQ_STEP_FRAMES));
  return n > SQ_LOOK_MAX ? SQ_LOOK_MAX : n;
}

/* Dead-end check: from `st`, does at least one heading stay safe? Stops the
   snake from entering a pocket whose far end is too narrow to turn in. */
static bool viable(const sq_state* st, float csp, float mang, float tip,
                   bool strong) {
  if (g_viable_budget <= 0) return true; /* out of budget: be optimistic */
  g_viable_budget--;
  /* Circling on the spot is a way out if the circle is free (strong: with
     spare room around it). */
  {
    int n = SQ_LOOK_STEPS + SQ_VIABLE_EXTRA;
    if (n > SQ_LOOK_MAX) n = SQ_LOOK_MAX;
    float slack = strong ? SQ_VIABLE_SLACK : 0.0f;
    if (slack > 0)
      for (int k = 0; k < g_obs_n; k++) g_reff[k] += slack;
    bool ok = simulate(*st, 0, 1, csp, mang, tip, n, NULL) == n ||
              simulate(*st, 0, -1, csp, mang, tip, n, NULL) == n;
    if (slack > 0)
      for (int k = 0; k < g_obs_n; k++) g_reff[k] -= slack;
    if (ok) return true;
  }
  for (int j = 0; j <= 3; j++) {
    for (int sg = -1; sg <= 1; sg += 2) {
      if (j == 0 && sg == 1) continue;
      float tgt = st->ang + (float)sg * (float)j * 0.5236f;
      int need = commit_steps(st->ang, tgt, mang);
      if (need < 0) continue;
      need += SQ_VIABLE_EXTRA;
      if (need > SQ_LOOK_MAX + SQ_VIABLE_EXTRA) need = SQ_LOOK_MAX + SQ_VIABLE_EXTRA;
      if (simulate(*st, tgt, 0, csp, mang, tip, need, NULL) == need) return true;
    }
  }
  return false;
}

/* ---- candidate headings from free angular gaps --------------------------- */

static int cmp_iv(const void* a, const void* b) {
  float d = ((const sq_iv*)a)->lo - ((const sq_iv*)b)->lo;
  return d < 0 ? -1 : (d > 0 ? 1 : 0);
}
static int cmp_cand(const void* a, const void* b) {
  float d = ((const sq_cand*)a)->cost - ((const sq_cand*)b)->cost;
  return d < 0 ? -1 : (d > 0 ? 1 : 0);
}

static void add_iv(float lo, float hi) {
  const float SQ_PI = (float)M_PI;
  if (g_iv_n + 2 >= (int)(sizeof g_iv / sizeof g_iv[0])) return;
  if (lo < -SQ_PI) {
    g_iv[g_iv_n++] = (sq_iv){lo + 2 * SQ_PI, SQ_PI};
    g_iv[g_iv_n++] = (sq_iv){-SQ_PI, hi};
  } else if (hi > SQ_PI) {
    g_iv[g_iv_n++] = (sq_iv){lo, SQ_PI};
    g_iv[g_iv_n++] = (sq_iv){-SQ_PI, hi - 2 * SQ_PI};
  } else {
    g_iv[g_iv_n++] = (sq_iv){lo, hi};
  }
}

static void add_cand(float ang, float wanted, float cur_ang) {
  if (g_cand_n >= SQ_MAX_CAND) return;
  float off = wrap_pi(ang - wanted);
  float cost = fabsf(off);
  int side = off >= 0 ? 1 : -1;
  if (g_side != 0 && side != g_side) cost += SQ_SIDE_PENALTY;
  float ref = g_have_prev ? g_prev_cmd : cur_ang;
  cost += SQ_CONT_WEIGHT * fabsf(wrap_pi(ang - ref));
  g_cand[g_cand_n++] = (sq_cand){ang, cost};
}

/* Build the candidate list: every free angular gap between the enemy circles
   (seen from the head, ray model) gets candidates hugging its edges plus its
   middle, so even a gap a fraction of a degree wide is found; a coarse fan
   around the wanted heading is added as a fallback. */
static void build_candidates(const sq_state* now, float wanted, float csp,
                             float tip) {
  const float SQ_PI = (float)M_PI;
  g_iv_n = 0;
  g_cand_n = 0;
  float ray = csp * SQ_LOOK_STEPS * SQ_STEP_FRAMES + tip + 10.0f;

  for (int k = 0; k < g_obs_n; k++) {
    float dx = g_obs[k].x - now->x, dy = g_obs[k].y - now->y;
    float d = sqrtf(dx * dx + dy * dy);
    float R = g_reff[k];
    if (d < 0.001f) continue;
    float off = wrap_pi(atan2f(dy, dx) - wanted);
    if (d - R < ray) {
      float s = R / d;
      float half = asinf(s > 1.0f ? 1.0f : s);
      add_iv(off - half, off + half);
    }
    if (tip > 0.0f && fabsf(d - tip) < R) {
      float c = (tip * tip + d * d - R * R) / (2.0f * tip * d);
      float half = acosf(c < -1.0f ? -1.0f : (c > 1.0f ? 1.0f : c));
      add_iv(off - half, off + half);
    }
  }
  qsort(g_iv, g_iv_n, sizeof g_iv[0], cmp_iv);

  static const float T[] = {0.0015f, 0.005f, 0.012f, 0.03f, 0.065f, 0.13f, 0.26f};
  float cursor = -SQ_PI;
  for (int i = 0; i <= g_iv_n; i++) {
    float lo = (i < g_iv_n) ? g_iv[i].lo : SQ_PI;
    if (lo > cursor + 1e-5f) {
      float a = cursor, b = lo, w = b - a;
      add_cand(wanted + (a + b) * 0.5f, wanted, now->ang);
      for (int t = 0; t < (int)(sizeof T / sizeof T[0]); t++) {
        if (T[t] >= w * 0.5f) break;
        add_cand(wanted + a + T[t], wanted, now->ang);
        add_cand(wanted + b - T[t], wanted, now->ang);
      }
    }
    if (i < g_iv_n && g_iv[i].hi > cursor) cursor = g_iv[i].hi;
  }
  for (int k = 1; k <= 24; k++) {
    add_cand(wanted + k * 0.1309f, wanted, now->ang);
    add_cand(wanted - k * 0.1309f, wanted, now->ang);
  }
  qsort(g_cand, g_cand_n, sizeof g_cand[0], cmp_cand);
}

/* ---- main filter ----------------------------------------------------------- */

static void set_cmd(int* xm, int* ym, float ang) {
  *xm = (int)lroundf(cosf(ang) * 256.0f);
  *ym = (int)lroundf(sinf(ang) * 256.0f);
}

/* Is the wanted heading (or, when circling, the continuing circle) safe, with
   a way out at the end? */
static bool wanted_ok(sq_state now, float wanted, float csp, float mang,
                      float tip, int steps, bool strong) {
  sq_state end;
  int sv = simulate(now, wanted, g_circle_dir, csp, mang, tip, steps, &end);
  if (sv != steps) return false;
  return viable(&end, csp, mang, tip, strong);
}

bool squeeze_filter(game_data* gdata, snake* me, int* xm, int* ym) {
  g_deflecting = false;
  if (!me || me->dead) return false;

  float csp = me->sp / 4.0f;
  if (csp < 0.5f) csp = 0.5f;
  float mang = turn_rate(gdata, me);
  float tip = roundf(me->sc * 29.0f) * 0.5f * SQ_TIP_FACTOR;
  sq_state now = {me->xx, me->yy, me->ang};

  /* Heading the player wants: the locked circle, or the mouse / joystick. */
  float wanted;
  if (g_circle_dir != 0)
    wanted = me->ang + (float)g_circle_dir * SQ_CIRCLE_LEAD;
  else if ((*xm) * (*xm) + (*ym) * (*ym) > 256)
    wanted = atan2f((float)*ym, (float)*xm);
  else
    wanted = me->ang;

  float reach =
      csp * (SQ_LOOK_MAX + SQ_VIABLE_EXTRA) * SQ_STEP_FRAMES + tip + 60.0f;
  if (reach > 900.0f) reach = 900.0f;
  gather(gdata, me, reach);

  bool circling = g_circle_dir != 0;

  if (g_obs_n == 0) {
    if (g_quiet < 1000) g_quiet++;
    if (g_quiet > 20) g_side = 0;
    g_have_prev = false;
    if (!circling) return false;
    set_cmd(xm, ym, wanted);
    return true;
  }

  /* Effective radius per obstacle: radius + margin; if a dot is already
     inside that ring, only forbid getting any closer so a way out exists. */
  float tipx = me->xx + cosf(me->ang) * tip;
  float tipy = me->yy + sinf(me->ang) * tip;
  for (int k = 0; k < g_obs_n; k++) {
    const sq_obs* o = &g_obs[k];
    float want = o->r + SQ_MARGIN;
    float dx = me->xx - o->x, dy = me->yy - o->y;
    float d0 = sqrtf(dx * dx + dy * dy);
    dx = tipx - o->x;
    dy = tipy - o->y;
    float dt = sqrtf(dx * dx + dy * dy);
    if (dt < d0) d0 = dt;
    if (d0 < want) {
      want = d0 - 0.02f;
      if (want < 0.5f) want = 0.5f;
    }
    g_reff[k] = want;
  }

  {
    float maxr = 0.0f;
    for (int k = 0; k < g_obs_n; k++)
      if (g_reff[k] > maxr) maxr = g_reff[k];
    build_grid(&now, reach, maxr + SQ_VIABLE_SLACK);
  }

  g_viable_budget = SQ_VIABLE_BUDGET;

  /* Already deflecting? Then demand a clearer all-clear before releasing. */
  int check_steps = SQ_LOOK_STEPS;
  if (g_have_prev) {
    check_steps = SQ_LOOK_STEPS * SQ_RELEASE_FACTOR;
    if (check_steps > SQ_LOOK_MAX) check_steps = SQ_LOOK_MAX;
  }

  if (wanted_ok(now, wanted, csp, mang, tip, check_steps, true)) {
    if (g_quiet < 1000) g_quiet++;
    if (g_quiet > 20) g_side = 0;
    g_have_prev = false;
    if (!circling) return false; /* mouse heading is fine: untouched */
    set_cmd(xm, ym, wanted);
    return true;
  }

  /* Blocked: search the nearest safe heading, biased to keep the side already
     being slid on. */
  build_candidates(&now, wanted, csp, tip);

  float best = wanted;
  bool found = false, have_weak = false, have_nv = false;
  float weak_best = wanted, nv_best = wanted, surv_best = wanted;
  int best_steps = -1;

  for (int i = 0; i < g_cand_n && !found; i++) {
    float c = g_cand[i].ang;
    int need = commit_steps(me->ang, c, mang);
    if (need < 0) continue;
    sq_state end;
    int sv = simulate(now, c, 0, csp, mang, tip, need, &end);
    if (sv == need) {
      if (viable(&end, csp, mang, tip, true)) {
        best = c;
        found = true;
      } else if (!have_weak && viable(&end, csp, mang, tip, false)) {
        have_weak = true;
        weak_best = c;
      } else if (!have_nv) {
        have_nv = true;
        nv_best = c;
      }
    } else if (sv > best_steps) {
      best_steps = sv;
      surv_best = c;
    }
  }
  if (!found) {
    /* No heading with spare room. If the wanted heading still has the bare
       minimum way out, trust it rather than swerve for nothing. */
    if (wanted_ok(now, wanted, csp, mang, tip, check_steps, false)) {
      if (g_quiet < 1000) g_quiet++;
      g_have_prev = false;
      if (!circling) return false;
      set_cmd(xm, ym, wanted);
      return true;
    }
    best = have_weak ? weak_best : (have_nv ? nv_best : surv_best);
  }

  /* Deadband: keep last frame's heading if it is still safe and almost the
     same, so the command doesn't shiver. */
  if (g_have_prev && fabsf(wrap_pi(g_prev_cmd - best)) < SQ_DEADBAND) {
    int need = commit_steps(me->ang, g_prev_cmd, mang);
    if (need > 0 &&
        simulate(now, g_prev_cmd, 0, csp, mang, tip, need, NULL) == need)
      best = g_prev_cmd;
  }

  g_side = wrap_pi(best - wanted) >= 0 ? 1 : -1;
  g_quiet = 0;
  g_prev_cmd = best;
  g_have_prev = true;

  set_cmd(xm, ym, best);
  g_deflecting = true;
  return true;
}
