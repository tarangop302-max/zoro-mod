#include "eyes_back.h"

#include <math.h>
#include <string.h>

/* ---- tuning (same values as NTL VANCED) ---------------------------------- */
#define EB_EPS 8.0          /* steps away from exactly 180 deg (251 steps = 360 deg) */
#define EB_TURN_MARGIN 4.0  /* extra steps while turning hard (ping jitter) */
#define EB_EPS_MAX 22.0     /* cap for the adaptive part of eps */
#define EB_SNAP 0.55        /* rad: model off by more than this -> re-anchor */
#define EB_BAND 2.5         /* steering proportional band, in ticks of max turn */

#define EB_TWO_PI 6.283185307179586
#define EB_PI 3.141592653589793
#define EB_LOG_CAP 160

typedef struct eb_cmd {
  double te;  /* client time the command takes effect on the server */
  double w;   /* target angle sent (rad) */
  double r;   /* server turn rate at that moment (rad / ms) */
} eb_cmd;

static struct {
  bool anchor_ok;
  double anchor_t, anchor_a; /* server heading anchor: time + angle */
  eb_cmd log[EB_LOG_CAP];
  int log_n;
  double err;       /* sigma-delta accumulator */
  double last_t;    /* last tick time (ms), 0 = none */
  double res_max;   /* recent worst model error (rad) */
  bool hold_ok;
  double hold;      /* heading held while the mouse is on the head */
} eb;

static double eb_norm(double a) {
  a = fmod(a, EB_TWO_PI);
  if (a > EB_PI) a -= EB_TWO_PI;
  else if (a < -EB_PI) a += EB_TWO_PI;
  return a;
}

static double eb_rtt(game_data* g) {
  double v = (double)g->data.owd_ms * 2.0;
  if (v <= 0) v = 100.0;
  return v > 600.0 ? 600.0 : v;
}

void eyes_back_reset(void) { memset(&eb, 0, sizeof eb); }

/* Server heading after turning from a toward w at rate r for ms. */
static double eb_turn(double a, double w, double r, double ms) {
  double d = eb_norm(w - a), mx = r * ms;
  return a + (d > mx ? mx : d < -mx ? -mx : d);
}

/* Predicted server heading at client time tau: replay the in-flight commands
   from the anchor, each turning toward its target at max rate. */
static double eb_heading_at(double tau) {
  double a = eb.anchor_a, t = eb.anchor_t;
  const eb_cmd* cmd = NULL;
  int i = 0;
  for (; i < eb.log_n && eb.log[i].te <= t; i++) cmd = &eb.log[i];
  for (; i < eb.log_n && eb.log[i].te < tau; i++) {
    if (cmd) a = eb_turn(a, cmd->w, cmd->r, eb.log[i].te - t);
    t = eb.log[i].te;
    cmd = &eb.log[i];
  }
  return cmd ? eb_turn(a, cmd->w, cmd->r, tau - t) : a;
}

/* A fresh server heading v arrived at client time now (it describes the
   server ~rtt/2 ago): measure how far the model was off, adapt, re-anchor. */
static void eb_sample(double v, double now, double rtt) {
  double tau = now - rtt / 2.0;
  if (eb.anchor_ok) {
    double res = fabs(eb_norm(v - eb_heading_at(tau)));
    eb.res_max = res > eb.res_max * 0.92 ? res : eb.res_max * 0.92;
    if (res > EB_SNAP) eb.log_n = 0; /* model diverged -> drop assumed turns */
  }
  eb.anchor_ok = true;
  eb.anchor_t = tau;
  eb.anchor_a = v;
}

void eyes_back_on_server_ang(game_data* gdata, float ang, double now_ms) {
  /* Only meaningful while the engine is running (tick() sets last_t). */
  if (eb.last_t == 0) return;
  eb_sample((double)ang, now_ms, eb_rtt(gdata));
}

int eyes_back_tick(game_data* gdata, snake* me, bool has_target, float target_f,
                   double now) {
  double rtt = eb_rtt(gdata);
  double target = target_f;

  /* Not ticked for a while (feature was off / new game): start clean. */
  if (eb.last_t != 0 && now - eb.last_t > 1000.0) eyes_back_reset();
  if (!eb.anchor_ok) eb_sample((double)me->ang, now - 16.0, rtt);

  double dt = eb.last_t ? now - eb.last_t : 33.0;
  eb.last_t = now > 0 ? now : 1.0;
  if (dt > 200.0) dt = 200.0;
  if (dt < 1.0) dt = 1.0;

  double te = now + rtt / 2.0; /* when this command reaches the server */
  double r = (double)gdata->data.mamu * me->scang * me->spang / 8.0; /* rad/ms */
  if (r < 1e-6) r = 1e-6;
  double est = eb_heading_at(te);

  if (!has_target) {
    if (!eb.hold_ok) { eb.hold = est; eb.hold_ok = true; }
    target = eb.hold;
  } else {
    eb.hold_ok = false;
  }

  double d = eb_norm(target - est);
  double k = d / (r * dt * EB_BAND);
  if (k > 1) k = 1; else if (k < -1) k = -1;
  eb.err += (1.0 + k) / 2.0; /* share of ticks that turn in the + direction */
  bool plus = eb.err >= 1.0;
  if (plus) eb.err -= 1.0;

  double adapt = 1.3 * eb.res_max * 251.0 / EB_TWO_PI;
  if (adapt > EB_EPS_MAX) adapt = EB_EPS_MAX;
  double eps = EB_EPS + fabs(k) * EB_TURN_MARGIN + adapt;

  double base = 251.0 * fmod(fmod(est + EB_PI, EB_TWO_PI) + EB_TWO_PI, EB_TWO_PI) / EB_TWO_PI;
  long b = lround(plus ? base - eps : base + eps);
  b = ((b % 251) + 251) % 251;

  if (eb.log_n >= EB_LOG_CAP) {
    memmove(eb.log, eb.log + 1, (EB_LOG_CAP - 1) * sizeof(eb_cmd));
    eb.log_n = EB_LOG_CAP - 1;
  }
  eb.log[eb.log_n++] = (eb_cmd){te, (double)b * EB_TWO_PI / 251.0, r};
  while (eb.log_n > 1 && eb.log[1].te < now - 2000.0) {
    memmove(eb.log, eb.log + 1, (size_t)(eb.log_n - 1) * sizeof(eb_cmd));
    eb.log_n--;
  }
  return (int)b;
}
