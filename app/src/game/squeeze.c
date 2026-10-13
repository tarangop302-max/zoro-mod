#include "squeeze.h"

#include <math.h>
#include <stdlib.h>

#include "../user.h"

/* ---- tuning ------------------------------------------------------------ */

/* Extra clearance (game units) kept between the centre line and an enemy
   body edge. Covers network latency / server rounding. Raise it if you still
   get killed while squeezing, lower it to hug bodies tighter. */
#define SQ_MARGIN 3.0f

/* The BLUE dot: the tip of your head, this many head-radii in front of the
   head centre (the RED dot). Both dots are kept out of enemy bodies; the blue
   one leads, so it is the one that decides how close you get. 1.0 = the very
   front edge of the head. Set to 0 to protect only the centre (red) dot. */
#define SQ_TIP_FACTOR 1.0f

/* Extra clearance around enemy HEADS (they move and can turn into you). */
#define SQ_HEAD_EXTRA 6.0f

/* Look-ahead: SQ_LOOK_STEPS iterations of SQ_STEP_FRAMES game frames each. */
#define SQ_LOOK_STEPS 40
#define SQ_STEP_FRAMES 2.0f

/* Candidate headings tried around the wanted one (every 7.5 degrees). */
#define SQ_CAND_STEP 0.1309f /* 7.5 deg in radians */
#define SQ_CAND_MAX 24       /* 24 * 7.5 = 180 deg each side */

#define SQ_MAX_OBS 768

typedef struct {
  float x, y;   /* position now */
  float vx, vy; /* movement per game frame (heads only) */
  float r;      /* radius the centre line must stay outside of */
} sq_obs;

static sq_obs g_obs[SQ_MAX_OBS];
static int g_obs_n;
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
  float reach2 = reach * reach;
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
      (void)reach2;
      add_obs(po->xx, po->yy, 0.0f, 0.0f, sr);
    }
  }
}

/* Turn the head toward `target` for `steps` iterations, moving along the way,
   and return how many iterations it survived without the centre line touching
   anything (SQ_LOOK_STEPS = fully safe). */
static int simulate(float x, float y, float ang, float target, float csp,
                    float mang, float tip, const float* r_eff) {
  for (int i = 1; i <= SQ_LOOK_STEPS; i++) {
    float diff = wrap_pi(target - ang);
    float turn = mang * SQ_STEP_FRAMES;
    if (fabsf(diff) <= turn)
      ang = target;
    else
      ang += diff < 0 ? -turn : turn;

    x += cosf(ang) * csp * SQ_STEP_FRAMES;
    y += sinf(ang) * csp * SQ_STEP_FRAMES;
    float t = (float)i * SQ_STEP_FRAMES;

    float bx = x + cosf(ang) * tip; /* blue dot (head tip) */
    float by = y + sinf(ang) * tip;

    for (int k = 0; k < g_obs_n; k++) {
      const sq_obs* o = &g_obs[k];
      float ox = o->x + o->vx * t;
      float oy = o->y + o->vy * t;
      float r = r_eff[k];
      float dx = x - ox, dy = y - oy; /* red dot */
      if (dx * dx + dy * dy < r * r) return i - 1;
      if (tip > 0.0f) {
        dx = bx - ox;
        dy = by - oy;
        if (dx * dx + dy * dy < r * r) return i - 1;
      }
    }
  }
  return SQ_LOOK_STEPS;
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

  float reach = csp * SQ_STEP_FRAMES * SQ_LOOK_STEPS + 40.0f + tip;
  gather(gdata, me, reach);
  if (g_obs_n == 0) return false;

  /* Effective radius per obstacle. Normally radius + margin. If the head is
     already inside the margin ring (but not inside the body), only forbid
     getting any closer, otherwise every heading would count as unsafe. */
  static float r_eff[SQ_MAX_OBS];
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
      /* Already inside the margin ring (or touching): only forbid getting
         any closer, so a way out is still found. */
      want = d0 - 0.5f;
      if (want < 0.5f) want = 0.5f;
    }
    r_eff[k] = want;
  }

  /* Wanted heading from the mouse / joystick (hold current if centred). */
  float wanted;
  if ((*xm) * (*xm) + (*ym) * (*ym) > 256)
    wanted = atan2f((float)*ym, (float)*xm);
  else
    wanted = me->ang;

  if (simulate(me->xx, me->yy, me->ang, wanted, csp, mang, tip, r_eff) ==
      SQ_LOOK_STEPS)
    return false; /* requested heading is already safe: don't touch it */

  float best = wanted;
  int best_steps = -1;
  bool found = false;

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
      int st = simulate(me->xx, me->yy, me->ang, cands[c], csp, mang, tip, r_eff);
      if (st == SQ_LOOK_STEPS) {
        best = cands[c];
        found = true;
        break;
      }
      if (st > best_steps) {
        best_steps = st;
        best = cands[c];
      }
    }
  }
  /* Nothing fully safe (boxed in): `best` is the heading that survives the
     longest, which is the best chance of the gap opening up. */

  *xm = (int)lroundf(cosf(best) * 256.0f);
  *ym = (int)lroundf(sinf(best) * 256.0f);
  g_deflecting = true;
  return true;
}
