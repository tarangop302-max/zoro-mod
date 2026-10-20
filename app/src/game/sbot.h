#ifndef SBOT_H
#define SBOT_H

#include <thermite.h>

typedef struct sbot {
  struct {
    float xm;
    float ym;
    bool accel;
  } output;
} sbot;

void sbot_init(tenv* env);
void sbot_go(tenv* env);
void sbot_destroy(tenv* env);

#define SBOT_DBG_MAX_COLL 64

typedef struct {
  bool  active;
  float bx, by;
  float brad;
  float head_cx, head_cy, head_cr;

  bool  arc_coll[16];
  bool  arc_food[16];

  bool  has_food;
  float food_x, food_y;
  float goal_x, goal_y;
  int   stage;
  bool  accel;

  int   coll_n;
  float coll_x[SBOT_DBG_MAX_COLL];
  float coll_y[SBOT_DBG_MAX_COLL];
  float coll_r[SBOT_DBG_MAX_COLL];
  int   coll_type[SBOT_DBG_MAX_COLL];
  float coll_d2[SBOT_DBG_MAX_COLL];

  bool  avoid_active;
  float avoid_ix, avoid_iy;
  float avoid_fwd_x, avoid_fwd_y;

  int   encircle_state;
  float encircle_r1;
  float encircle_r2;

  /* stage-2 (coil) threat debug */
  int   thr_kind;               /* 0 none, 1 body, 2 head arrival, 3 head-on */
  float thr_ex, thr_ey;         /* threat source */
  float thr_qx, thr_qy;         /* point on my path it threatens */
  int   arc_n;
  float arc_x[12], arc_y[12];   /* predicted arc */
  float safe_lim;

  /* coil zones: tier 1 free / 2 steady push / 3 react */
  int   zone_tier;
  float zone_ax, zone_ay;
  float zone_cx, zone_cy, zone_a, zone_b;     /* red zone   */
  float outer_cx, outer_cy, outer_a, outer_b; /* outer oval */
} sbot_dbg;

extern sbot_dbg g_bot_dbg;

#endif
