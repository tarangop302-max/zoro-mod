/*
 * lag_sim: checks the steering/lead maths against a simulated server.
 *   gcc -O2 -o lag_sim tools/lag_sim.c app/src/game/lag_comp.c -lm && ./lag_sim
 *
 * A fake server turns toward the last steering byte it received at the real
 * turn rate. The "arrow" is scripted (turns, a U-turn, a wiggle). Packets cross
 * a network with a base one-way delay plus jitter. For every client frame we
 * compare where the client DRAWS my head with where the server's head really is
 * at that instant -- that gap is exactly "dying before touching".
 *
 *   OLD = lead capped at 100 ms (+10), turn shown min(owd,120) ms after send,
 *         lead heading = chord of the local turn (what zoro-mod did)
 *   NEW = lag_comp: replay of in-flight commands, no caps
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../app/src/game/lag_comp.h"

#define PI 3.14159265358979
#define TWO_PI (2 * PI)
static double wrap(double a) { a = fmod(a, TWO_PI); return a < 0 ? a + TWO_PI : a; }
static double norm(double a) { a = fmod(a, TWO_PI); return a > PI ? a - TWO_PI : a < -PI ? a + TWO_PI : a; }
static double turn(double a, double w, double r, double ms) {
  double d = norm(w - a), mx = r * ms;
  return a + (d > mx ? mx : d < -mx ? -mx : d);
}
static double rnd(void) { return rand() / (double)RAND_MAX; }

/* the player's arrow over time (rad) */
static double arrow(double t) {
  if (t < 600) return 0.0;
  if (t < 1500) return 1.5;          /* 86 deg right */
  if (t < 2500) return 1.5 + PI;     /* U-turn */
  if (t < 3100) return 1.5 + PI + 1.0 * sin((t - 2500) / 150.0); /* wiggle */
  return 0.5;
}

typedef struct { double t_arr; double a, w; double x, y; double s; } pkt; /* in flight to client */

static double up_frac = 0.5; /* real share of the RTT spent going up (0.5 = symmetric, what the client assumes) */
static int ang_every = 1; /* 1: every echo carries the heading; N: only every Nth (others carry just the turn target, like most real ones) */
static void run(double rtt, double jitter, double sp, int mode, double* mean_e, double* max_e, double* mean_h) {
  int echo_n = 0;
  lag_comp_reset();
  srand(7);
  const double r = 0.033 / 8.0;  /* rad/ms, default mamu, scang 1 */
  double upm = sp / 32.0;         /* units / ms */
  double up_true = rtt * up_frac, down_true = rtt * (1.0 - up_frac);
  double up_est = rtt / 2, down_est = rtt / 2; /* the client's smoothed estimate */

  /* server */
  double sa = 0, sw = 0, sx = 0, sy = 0;
  struct { double t_arr, w; } cmds[4096]; int ncmd = 0, ci = 0; /* commands in flight to server */
  pkt q[8192]; int qh = 0, qt = 0;

  /* client */
  double last_sent = -1e9; int last_byte = -1;
  double ca = 0, cw = 0;                 /* OLD local sim state */
  struct { double due; double w; } oq[256]; int on = 0;
  double hx = 0, hy = 0, hang = 0;       /* drawn head and heading */
  int have = 0;
  double es = 0, em = 0, eh = 0; long ne = 0;
  double next_frame = 0;
  double last_up_arr = 0, last_down_arr = 0; /* TCP: in-order delivery */
  double fdt = 11.0;
  double old_hold_cap = 120.0, lead_cap = 100.0;

  for (double t = 0; t < 3800; t += 1.0) {
    /* client: input + send */
    if (t - last_sent >= 33.0) {
      double tgt = arrow(t);
      int b = (int)floor(251.0 * wrap(tgt) / TWO_PI);
      if (b != last_byte) {
        last_byte = b; last_sent = t;
        double w = b * TWO_PI / 251.0;
        double arr = t + up_true + (rnd() * 2 - 1) * jitter;
        if (arr < last_up_arr) arr = last_up_arr; /* never overtakes */
        last_up_arr = arr;
        cmds[ncmd].t_arr = arr; cmds[ncmd].w = w; ncmd++;
        lag_comp_sent(t, (float)w);
        double hold = up_est < old_hold_cap ? up_est : old_hold_cap;
        oq[on].due = t + hold; oq[on].w = w; on++;
      }
    }
    /* server: apply arrived commands, step */
    while (ci < ncmd && cmds[ci].t_arr <= t) {
      sw = cmds[ci].w; ci++;
      /* broadcast heading echo immediately */
      double arr = t + down_true + (rnd() * 2 - 1) * jitter;
      if (arr < last_down_arr) arr = last_down_arr;
      last_down_arr = arr;
      pkt p = {arr, sa, sw, sx, sy, 1};
      q[qt++] = p;
    }
    sa = wrap(turn(sa, sw, r, 1.0));
    sx += cos(sa) * upm; sy += sin(sa) * upm;
    if (fmod(t, 30.0) < 1.0) {
      double arr = t + down_true + (rnd() * 2 - 1) * jitter;
      if (arr < last_down_arr) arr = last_down_arr;
      last_down_arr = arr;
      pkt p = {arr, sa, sw, sx, sy, 0}; q[qt++] = p;
    }

    /* client: receive */
    for (int i = qh; i < qt; i++) {
      if (q[i].t_arr > t) continue;
      if (q[i].t_arr < 0) continue;
      pkt p = q[i]; q[i].t_arr = -1;
      /* echo? (s==1): re-anchor heading */
      if (p.s == 1) {
        if (mode == 1) lag_comp_echo(t, (echo_n++ % ang_every == 0) ? (float)p.a : -1.0f, (float)p.w, (float)hang, (float)up_est, (float)down_est, (float)r);
        /* OLD: echoes ignored while a local turn is in flight (hold) */
      }
      /* position packet: pos is server pos at (t - down_true) */
      double lx, ly;
      if (mode == 0) {
        double lead = down_est < lead_cap ? down_est : lead_cap; lead += 10.0;
        /* chord heading of local turn over the lead time */
        double d = norm(cw - ca), per = r * lead, eff = fabs(d) < per ? fabs(d) : per;
        double avg = eff * (1.0 - (per > 0 ? eff / per : 0) * 0.5);
        double ang = ca + (d >= 0 ? avg : -avg);
        double cap = 0.9 * 42.0, dist = upm * lead; if (dist > cap) dist = cap;
        lx = p.x + cos(ang) * dist; ly = p.y + sin(ang) * dist;
      } else {
        float dx, dy;
        lag_comp_lead(t, (float)up_est, (float)down_est, (float)r, (float)upm, &dx, &dy);
        lx = p.x + dx; ly = p.y + dy;
      }
      hx = lx; hy = ly; have = 1;
    }
    while (qh < qt && q[qh].t_arr < 0) qh++;

    /* client frame */
    if (t >= next_frame) {
      next_frame += fdt;
      /* OLD local sim: apply due commands */
      int due = 0; while (due < on && oq[due].due <= t + 5.0) due++;
      if (due) { cw = oq[due - 1].w; memmove(oq, oq + due, (on - due) * sizeof oq[0]); on -= due; }
      ca = wrap(turn(ca, cw, r, fdt));
      double ang;
      if (mode == 0) ang = ca;
      else { float a, w; lag_comp_display(t, (float)up_est, (float)up_est, (float)r, &a, &w); ang = a; }
      hang = ang;
      if (have) { hx += cos(ang) * upm * fdt; hy += sin(ang) * upm * fdt; }
      if (have && t > 300) {
        double e = hypot(hx - sx, hy - sy);
        es += e; if (e > em) em = e;
        eh += fabs(norm(ang - sa)); ne++;
      }
    }
  }
  *mean_e = es / ne; *max_e = em; *mean_h = eh / ne;
}

int main(int argc, char** argv) {
  if (argc > 1) ang_every = atoi(argv[1]);
  if (ang_every < 1) ang_every = 1;
  if (argc > 2) up_frac = atof(argv[2]);
  double rtts[] = {40, 100, 200, 300, 450};
  printf("Head drawn vs real server head (game units; head radius ~15-30, segment 42)\n");
  printf("speed jitter  RTT |  OLD mean  max  heading | NEW mean  max  heading\n");
  for (int s = 0; s < 2; s++)
    for (int jj = 0; jj < 2; jj++) {
      double sp = s ? 14.0 : 5.78, jit = jj ? 30.0 : 0.0;
      for (int i = 0; i < 5; i++) {
        double m0, x0, h0, m1, x1, h1;
        run(rtts[i], jit, sp, 0, &m0, &x0, &h0);
        run(rtts[i], jit, sp, 1, &m1, &x1, &h1);
        printf("%-6s %4.0fms %4.0f | %6.1f %6.1f %6.2f   | %6.1f %6.1f %6.2f\n", s ? "boost" : "normal", jit, rtts[i], m0, x0, h0, m1, x1, h1);
      }
    }
  return 0;
}
