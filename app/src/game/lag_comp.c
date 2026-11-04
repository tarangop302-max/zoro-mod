#include "lag_comp.h"

#include <math.h>
#include <string.h>

#define LC_PI 3.14159265358979323846
#define LC_TWO_PI 6.28318530717958647692
#define LC_LOG 64
#define LC_STEP_MS 8.0 /* integration step for the lead path (server tick) */

typedef struct lc_cmd {
  double c; /* client time the byte was sent */
  float w;  /* target angle (rad) */
} lc_cmd;

static struct {
  bool anchored;
  double at;   /* server time of the anchor, on the client clock */
  float aa;    /* server heading at the anchor */
  float aw;    /* server turn target at the anchor */
  lc_cmd log[LC_LOG];
  int n;
} lc;

static double lc_norm(double a) {
  a = fmod(a, LC_TWO_PI);
  if (a > LC_PI) a -= LC_TWO_PI;
  else if (a < -LC_PI) a += LC_TWO_PI;
  return a;
}

static double lc_wrap(double a) {
  a = fmod(a, LC_TWO_PI);
  if (a < 0) a += LC_TWO_PI;
  return a;
}

/* Heading after turning from a toward w at rate r for ms (the server's rule:
   fixed turn rate, never overshoots). */
static double lc_turn(double a, double w, double r, double ms) {
  if (ms <= 0) return a;
  double d = lc_norm(w - a), mx = r * ms;
  return a + (d > mx ? mx : d < -mx ? -mx : d);
}

void lag_comp_reset(void) { memset(&lc, 0, sizeof lc); }

bool lag_comp_ready(void) { return lc.anchored; }

void lag_comp_sent(double now_ms, float target) {
  if (lc.n >= LC_LOG) {
    memmove(lc.log, lc.log + 1, (LC_LOG - 1) * sizeof(lc_cmd));
    lc.n = LC_LOG - 1;
  }
  lc.log[lc.n].c = now_ms;
  lc.log[lc.n].w = (float)lc_wrap(target);
  lc.n++;
}

/* Replay the commands the server had not yet received at the anchor, each one
   taking effect hold_ms after it was sent, up to time T. */
static void lc_replay(double T, double hold, double up, double rate,
                      double* a_out, double* w_out) {
  double a = lc.aa, w = lc.aw, t = lc.at;
  if (T < t) T = t;
  for (int i = 0; i < lc.n; i++) {
    if (lc.log[i].c + up <= lc.at) continue; /* already inside the anchor */
    double te = lc.log[i].c + hold;
    if (te < t) te = t; /* cannot act before the replay clock */
    if (te >= T) break;
    a = lc_turn(a, w, rate, te - t);
    t = te;
    w = lc.log[i].w;
  }
  a = lc_turn(a, w, rate, T - t);
  *a_out = lc_wrap(a);
  *w_out = lc_wrap(w);
}

void lag_comp_echo(double now_ms, float ang, float wang, float cur_ang,
                   float up_ms, float down_ms, float rate) {
  double at = now_ms - down_ms;
  double na, nw;
  if (lc.anchored) {
    double ma, mw;
    lc_replay(at, up_ms, up_ms, rate, &ma, &mw); /* the model, at that moment */
    na = ang >= 0 ? ang : ma;
    nw = wang >= 0 ? wang : mw;
  } else {
    na = ang >= 0 ? ang : cur_ang;
    nw = wang >= 0 ? wang : na;
  }
  if (lc.anchored && at < lc.at) return; /* out-of-order: keep the newer anchor */
  lc.anchored = true;
  lc.at = at;
  lc.aa = (float)lc_wrap(na);
  lc.aw = (float)lc_wrap(nw);
  /* Commands the server has certainly got by the anchor are baked into it. */
  int drop = 0;
  while (drop < lc.n && lc.log[drop].c + up_ms + 300.0 < lc.at) drop++;
  if (drop) {
    memmove(lc.log, lc.log + drop, (size_t)(lc.n - drop) * sizeof(lc_cmd));
    lc.n -= drop;
  }
}

void lag_comp_display(double now_ms, float up_ms, float hold_ms, float rate,
                      float* ang, float* wang) {
  double a, w;
  lc_replay(now_ms, hold_ms, up_ms, rate, &a, &w);
  *ang = (float)a;
  *wang = (float)w;
}

void lag_comp_lead(double now_ms, float up_ms, float down_ms, float rate,
                   float units_per_ms, float* dx, float* dy) {
  double x = 0, y = 0;
  double t0 = now_ms - down_ms;
  if (t0 < lc.at) t0 = lc.at;
  for (double t = t0; t < now_ms;) {
    double dt = now_ms - t;
    if (dt > LC_STEP_MS) dt = LC_STEP_MS;
    double a, w;
    lc_replay(t + dt * 0.5, up_ms, up_ms, rate, &a, &w); /* mid-step heading */
    x += cos(a) * units_per_ms * dt;
    y += sin(a) * units_per_ms * dt;
    t += dt;
  }
  *dx = (float)x;
  *dy = (float)y;
}
