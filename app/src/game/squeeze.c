#include "squeeze.h"

#include <math.h>
#include <stdlib.h>

#include "../user.h"

/* ---- tuning ------------------------------------------------------------ */

/* Extra clearance (game units) kept between the two protected dots and an
   enemy body edge. Very small on purpose so the dots hug the body as tightly
   as physically possible. Raise it (try 2-4) if you still get killed while
   squeezing: the server sees your head slightly behind / ahead of what you
   see, by about (ping x speed). */
#define SQ_MARGIN 2.0f

/* Two dots are protected: the RED dot (the head centre) and the BLUE dot (the
   tip of the head), SQ_TIP_FACTOR head-radii in front of the red one.
   1.0 = the very front edge of the head. Set to 0 to protect only the red
   dot. */
#define SQ_TIP_FACTOR 1.0f

/* Extra clearance around enemy HEADS (they move and can turn into you). */
#define SQ_HEAD_EXTRA 6.0f

/* Look-ahead used to decide WHEN to step in: SQ_LOOK_STEPS simulated frames.
   Squeeze stays out of the way until a dot is about to touch a body, or the
   path ahead leads into a dead end (see viable() below). A longer value makes
   the snake start steering away from far-off enemies. Don't go much below
   40: the snake needs about that many frames to turn away from a body it is
   heading straight into. */
#define SQ_LOOK_STEPS 45
#define SQ_STEP_FRAMES 1.0f

/* A heading is only accepted as an escape route if it stays safe for the whole
   time the snake needs to turn onto it PLUS SQ_LOOK_STEPS frames straight
   after that, capped at SQ_LOOK_MAX. */
#define SQ_LOOK_MAX 220

/* Never pick a heading that needs a (nearly) 180 degree turn: the turn
   direction of a head-on reversal is ambiguous (the game and this code can
   pick opposite ways round), which flips the snake into the body mid-turn. */
#define SQ_MAX_TURN 2.95f /* radians, about 169 degrees */

/* Candidate headings tried around the wanted one (every 7.5 degrees), then
   refined by bisection down to a fraction of a degree. */
#define SQ_CAND_STEP 0.1309f /* 7.5 deg in radians */
#define SQ_CAND_MAX 24       /* 24 * 7.5 = 180 deg each side */

/* Dead-end check budget per frame (it is the expensive part). */
#define SQ_VIABLE_BUDGET 10

#define SQ_MAX_OBS 768

typedef struct {
  float x, y;   /* position now */
  float vx, vy; /* movement per game frame (heads only) */
  float r;      /* radius the dots must stay outside of */
} sq_obs;

typedef struct {
  float x, y, ang;
} sq_state;

static sq_obs g_obs[SQ_MAX_OBS];
static float g_reff[SQ_MAX_OBS];
static int g_obs_n;
static int g_viable_budget;
static bool g_deflecting = false;

bool squeeze_is_deflecting(void) { return g_deflecting; }

static float wrap_pi(float a) {
  a = fmodf(a, 2.0f * (float)M_PI);
  if (a > (float)M_PI) a -= 2.0f * (float)M_PI;
  if (a < -(float)M_PI) a += 2.0f * (float)M_PI;
  return a;
}

static void add_obs(float x, float y, float vx, float vy, float r) {
  if (g_obs_n >= SQ_MAX_OBS) return;
  g_obs[g_obs_n++] = (sq_obs){x, y, vx, vy, r};
}

static void gather(game_data* gdata, snake* me, float reach) {
  g_obs_n = 0;
  int ns = tdarray_length(gdata->data.snakes);
  for (int i = 0; i < ns; i++) {
    snake* s = gdata->data.snakes + i;
    if (s == me || s->id == me->id) continue;

    float sr = roundf(s->sc * 29.0f) * 0.5f;

    /* Enemy head: it keeps moving, so it gets a velocity. */
    float hv = s->sp / 4.0f;
    float hdx = s->xx - me->xx, hdy = s->yy - me->yy;
    float hr = reach + 100.0f;
    if (hdx * hdx + hdy * hdy < hr * hr)
      add_obs(s->xx, s->yy, cosf(s->ang) * hv, sinf(s->ang) * hv,
              sr + SQ_HEAD_EXTRA);

    int pn = tdarray_length(s->pts);
    for (int j = 0; j < pn; j++) {
      body_part* po = s->pts + j;
      if (po->dying) continue;
      float dx = po->xx - me->xx, dy = po->yy - me->yy;
      float rr = reach + sr;
      if (dx * dx + dy * dy > rr * rr) continue;
      add_obs(po->xx, po->yy, 0.0f, 0.0f, sr);
    }
  }
}

/* Turn the head toward `target` for `steps` frames, moving along the way, and
   return how many frames it survived without either dot touching anything
   (== steps means fully safe). t0 = frames already elapsed (enemy heads keep
   moving). `end` receives the final state. */
static int simulate(sq_state st, float target, float csp, float mang, float tip,
                    int steps, int t0, sq_state* end) {
  float x = st.x, y = st.y, ang = st.ang;
  int survived = steps;
  for (int i = 1; i <= steps; i++) {
    float diff = wrap_pi(target - ang);
    float turn = mang * SQ_STEP_FRAMES;
    if (fabsf(diff) <= turn)
      ang = target;
    else
      ang += diff < 0 ? -turn : turn;

    x += cosf(ang) * csp * SQ_STEP_FRAMES;
    y += sinf(ang) * csp * SQ_STEP_FRAMES;
    float t = (float)(t0 + i) * SQ_STEP_FRAMES;

    float bx = x + cosf(ang) * tip; /* blue dot (head tip) */
    float by = y + sinf(ang) * tip;

    bool hit = false;
    for (int k = 0; k < g_obs_n && !hit; k++) {
      const sq_obs* o = &g_obs[k];
      float ox = o->x + o->vx * t;
      float oy = o->y + o->vy * t;
      float r = g_reff[k];
      float dx = x - ox, dy = y - oy; /* red dot */
      if (dx * dx + dy * dy < r * r) hit = true;
      else if (tip > 0.0f) {
        dx = bx - ox;
        dy = by - oy;
        if (dx * dx + dy * dy < r * r) hit = true;
      }
    }
    if (hit) {
      survived = i - 1;
      break;
    }
  }
  if (end) *end = (sq_state){x, y, ang};
  return survived;
}

/* Look-ahead needed to trust `target` as an escape route (see SQ_LOOK_MAX),
   or -1 if that heading is not allowed (see SQ_MAX_TURN). */
static int commit_steps(float cur, float target, float mang) {
  float d = fabsf(wrap_pi(target - cur));
  if (d > SQ_MAX_TURN) return -1;
  int n = SQ_LOOK_STEPS + (int)(d / (mang * SQ_STEP_FRAMES));
  return n > SQ_LOOK_MAX ? SQ_LOOK_MAX : n;
}

/* Dead-end check: from state `st` (reached after t0 frames), is there still at
   least one heading that stays safe? This is what stops the snake from
   following your aim into a wedge / pocket between bodies whose far end is
   too narrow to turn around in. */
static bool viable(const sq_state* st, int t0, float csp, float mang,
                   float tip) {
  if (g_viable_budget <= 0) return false;
  g_viable_budget--;
  for (int j = 0; j <= 5; j++) {
    for (int sg = -1; sg <= 1; sg += 2) {
      if (j == 0 && sg == 1) continue;
      float tgt = st->ang + (float)sg * (float)j * 0.5236f; /* 30 deg steps */
      int need = commit_steps(st->ang, tgt, mang);
      if (need < 0) continue;
      if (simulate(*st, tgt, csp, mang, tip, need, t0, NULL) == need)
        return true;
    }
  }
  return false;
}

/* Is heading `target` fully safe for `steps` frames AND does it still leave a
   way out afterwards? */
static bool heading_ok(sq_state st, float target, float csp, float mang,
                       float tip, int steps, int* survived) {
  sq_state end;
  int sv = simulate(st, target, csp, mang, tip, steps, 0, &end);
  if (survived) *survived = sv;
  if (sv != steps) return false;
  return viable(&end, steps, csp, mang, tip);
}

bool squeeze_filter(game_data* gdata, snake* me, int* xm, int* ym) {
  g_deflecting = false;
  if (!me || me->dead) return false;

  float csp = me->sp / 4.0f;
  if (csp < 0.5f) csp = 0.5f;
  float mang = gdata->data.mamu * me->scang * me->spang;
  if (mang < 0.001f) mang = 0.001f;

  float my_r = roundf(me->sc * 29.0f) * 0.5f;
  float tip = my_r * SQ_TIP_FACTOR;
  float tipx = me->xx + cosf(me->ang) * tip;
  float tipy = me->yy + sinf(me->ang) * tip;

  float reach =
      csp * SQ_STEP_FRAMES * (SQ_LOOK_STEPS * 2 + 60) + 40.0f + tip;
  gather(gdata, me, reach);
  if (g_obs_n == 0) return false;

  /* Effective radius per obstacle. Normally radius + margin. If a dot is
     already inside the margin ring, only forbid getting any closer, so a way
     out is still found. */
  for (int k = 0; k < g_obs_n; k++) {
    const sq_obs* o = &g_obs[k];
    float want = o->r + SQ_MARGIN;
    float dx = me->xx - o->x, dy = me->yy - o->y;
    float d0 = sqrtf(dx * dx + dy * dy);
    dx = tipx - o->x;
    dy = tipy - o->y;
    float dt = sqrtf(dx * dx + dy * dy);
    if (dt < d0) d0 = dt; /* the closer of the two dots */
    if (d0 < want) {
      want = d0 - 0.02f; /* tiny float tolerance only: any bigger and the
                            snake creeps inward a little every frame */
      if (want < 0.5f) want = 0.5f;
    }
    g_reff[k] = want;
  }

  /* Wanted heading from the mouse / joystick (hold current if centred). */
  float wanted;
  if ((*xm) * (*xm) + (*ym) * (*ym) > 256)
    wanted = atan2f((float)*ym, (float)*xm);
  else
    wanted = me->ang;

  sq_state now = {me->xx, me->yy, me->ang};
  g_viable_budget = SQ_VIABLE_BUDGET;

  if (heading_ok(now, wanted, csp, mang, tip, SQ_LOOK_STEPS, NULL))
    return false; /* requested heading is fine: don't touch it */

  float best = wanted;
  float found_prev = wanted;   /* last rejected heading before the good one */
  bool found = false;
  bool have_nv = false;        /* safe but leads into a dead end */
  float nv_best = wanted;
  int best_steps = -1;
  float surv_best = wanted;

  for (int k = 1; k <= SQ_CAND_MAX && !found; k++) {
    float a1 = wanted + k * SQ_CAND_STEP;
    float a2 = wanted - k * SQ_CAND_STEP;
    /* Try first the side that needs the smaller turn from the current
       heading, so the snake doesn't flip around needlessly. */
    if (fabsf(wrap_pi(a2 - me->ang)) < fabsf(wrap_pi(a1 - me->ang))) {
      float t = a1;
      a1 = a2;
      a2 = t;
    }
    float cands[2] = {a1, a2};
    for (int c = 0; c < 2; c++) {
      int need = commit_steps(me->ang, cands[c], mang);
      if (need < 0) continue; /* reversal: ambiguous turn direction */
      sq_state end;
      int sv = simulate(now, cands[c], csp, mang, tip, need, 0, &end);
      if (sv == need) {
        if (viable(&end, need, csp, mang, tip)) {
          best = cands[c];
          float sgn = wrap_pi(cands[c] - wanted) >= 0 ? 1.0f : -1.0f;
          found_prev = wanted + sgn * (k - 1) * SQ_CAND_STEP;
          found = true;
          break;
        }
        if (!have_nv) {
          have_nv = true;
          nv_best = cands[c];
        }
      } else if (sv > best_steps) {
        best_steps = sv;
        surv_best = cands[c];
      }
    }
  }

  if (found) {
    /* Refine: the coarse search only says "somewhere between found_prev
       (rejected) and best (accepted)". Bisect to the tightest accepted
       heading, i.e. the one that grazes the enemy body, so the dots hug it as
       closely as possible instead of overshooting by up to 7.5 degrees. */
    float lo = found_prev, hi = best;
    for (int it = 0; it < 7; it++) {
      float mid = lo + wrap_pi(hi - lo) * 0.5f;
      int need = commit_steps(me->ang, mid, mang);
      if (need > 0 && heading_ok(now, mid, csp, mang, tip, need, NULL))
        hi = mid;
      else
        lo = mid;
    }
    best = hi;
  } else if (have_nv) {
    /* No heading with a guaranteed way out afterwards: at least take one that
       is safe for the whole look-ahead. */
    best = nv_best;
  } else {
    /* Boxed in: take the heading that survives the longest, the best chance
       of a gap opening up. */
    best = surv_best;
  }

  *xm = (int)lroundf(cosf(best) * 256.0f);
  *ym = (int)lroundf(sinf(best) * 256.0f);
  g_deflecting = true;
  return true;
}
