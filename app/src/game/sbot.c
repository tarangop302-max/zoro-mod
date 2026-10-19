#include "sbot.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

sbot_dbg g_bot_dbg;

#include "../user.h"

#define ARC_SIZE (M_PI / 8.0f)
#define MAXARC 16
#define SPEED_BASE 5.78f
#define FRONT_ANGLE (M_PI / 2.0f)
#define ENCIRCLE_THRESH 0.5625f
#define ENCIRCLE_ALL_THRESH 0.5625f
#define ENCIRCLE_DIST_MULT 20.0f
#define BORDER_PT_RADIUS 20.0f
#define COLLISION_DELAY 10
#define ACTION_FRAMES 2

#define MAX_BODY_PTS 8192
#define MAX_HULL_PTS 32
#define MAX_INSIDE_PTS 512
#define MAX_COLL_PTS 512

typedef struct {
  float x, y;
} v2;
typedef struct {
  float x, y, r;
} circ;
typedef struct {
  float x, y, a;
} isect;

typedef struct {
  v2 pts[MAX_HULL_PTS];
  int n;
  float minx, maxx, miny, maxy;
} poly_box;

typedef struct {
  v2 pts[MAX_INSIDE_PTS];
  int n;
  float minx, maxx, miny, maxy;
} poly_box_large;

typedef struct {
  float x, y, d2, r;
  int si, type;
} coll_pt;
typedef struct {
  float x, y, ang, d2, r;
  int si, aIndex;
} coll_ang;
typedef struct {
  float x, y, ang, da, d2, sz, score;
} food_ang;
typedef struct {
  float x, y, len;
} body_pt;

typedef struct {
  int stage;
  int delay_frame;
  int default_accel;
  int circle_dir;
  bool enable_encircle;
  bool enable_follow_circle;

  int id;
  float x, y, ang, ca, sa;
  float speed_mult, width, radius;
  int snake_len;

  circ head_circle, side_l, side_r;

  coll_ang coll_angles[MAXARC];
  bool coll_angles_set[MAXARC];
  food_ang food_angles[MAXARC];
  bool food_angles_set[MAXARC];

  food_ang current_food;
  bool has_food;

  coll_pt coll_pts[MAX_COLL_PTS];
  int coll_pts_n;

  v2 goal;

  body_pt bpts[MAX_BODY_PTS];
  int bpts_n;
  float bpts_len;

  float radius_mult;
  float follow_circle_length;
} bot_state;

static bot_state B;

static inline float dist2(float ax, float ay, float bx, float by) {
  float dx = ax - bx, dy = ay - by;
  return dx * dx + dy * dy;
}

static inline float ang_between(float a, float b) {
  float r1 = fmodf(a - b, (float)M_PI);
  float r2 = fmodf(b - a, (float)M_PI);
  return r1 < r2 ? -r1 : r2;
}

static inline int ang_index(float angle) {
  float a = angle < 0 ? angle + PI2 : angle;
  int i = (int)roundf(a * (1.0f / ARC_SIZE));
  return i == MAXARC ? 0 : i;
}

static inline float snake_width(float sc) { return roundf(sc * 29.0f); }

static inline v2 unit_vec(v2 v) {
  float l = sqrtf(v.x * v.x + v.y * v.y);
  return l > 0 ? (v2){v.x / l, v.y / l} : (v2){0, 0};
}

static inline float cross2(v2 o, v2 a, v2 b) {
  return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

static int hull_cmp(const void* a, const void* b) {
  v2 pa = *(v2*)a, pb = *(v2*)b;
  if (pa.x != pb.x) return pa.x < pb.x ? -1 : 1;
  return pa.y < pb.y ? -1 : pa.y > pb.y ? 1 : 0;
}

static int convex_hull(v2* pts, int n, v2* out) {
  if (n < 2) {
    for (int i = 0; i < n; i++) out[i] = pts[i];
    return n;
  }
  qsort(pts, n, sizeof(v2), hull_cmp);

  v2 stk[MAX_HULL_PTS * 2];
  int k = 0;

  for (int i = 0; i < n; i++) {
    while (k >= 2 && cross2(stk[k - 2], stk[k - 1], pts[i]) <= 0) k--;
    stk[k++] = pts[i];
  }

  int lo = k + 1;
  for (int i = n - 2; i >= 0; i--) {
    while (k >= lo && cross2(stk[k - 2], stk[k - 1], pts[i]) <= 0) k--;
    stk[k++] = pts[i];
  }
  k--;
  for (int i = 0; i < k; i++) out[i] = stk[i];
  return k;
}

static bool point_in_poly(v2 p, const poly_box* pb) {
  if (p.x < pb->minx || p.x > pb->maxx || p.y < pb->miny || p.y > pb->maxy)
    return false;
  bool c = false;
  int l = pb->n;
  for (int i = 0, j = l - 1; i < l; j = i++) {
    if ((pb->pts[i].y > p.y) != (pb->pts[j].y > p.y) &&
        p.x < (pb->pts[j].x - pb->pts[i].x) * (p.y - pb->pts[i].y) /
                      (pb->pts[j].y - pb->pts[i].y) +
                  pb->pts[i].x)
      c = !c;
  }
  return c;
}

static bool point_in_poly_large(v2 p, const poly_box_large* pb) {
  if (p.x < pb->minx || p.x > pb->maxx || p.y < pb->miny || p.y > pb->maxy)
    return false;
  bool c = false;
  int l = pb->n;
  for (int i = 0, j = l - 1; i < l; j = i++) {
    if ((pb->pts[i].y > p.y) != (pb->pts[j].y > p.y) &&
        p.x < (pb->pts[j].x - pb->pts[i].x) * (p.y - pb->pts[i].y) /
                      (pb->pts[j].y - pb->pts[i].y) +
                  pb->pts[i].x)
      c = !c;
  }
  return c;
}

static void poly_box_aabb(poly_box* pb) {
  pb->minx = pb->maxx = pb->pts[0].x;
  pb->miny = pb->maxy = pb->pts[0].y;
  for (int i = 1; i < pb->n; i++) {
    if (pb->pts[i].x < pb->minx) pb->minx = pb->pts[i].x;
    if (pb->pts[i].x > pb->maxx) pb->maxx = pb->pts[i].x;
    if (pb->pts[i].y < pb->miny) pb->miny = pb->pts[i].y;
    if (pb->pts[i].y > pb->maxy) pb->maxy = pb->pts[i].y;
  }
}

static void poly_box_large_aabb(poly_box_large* pb) {
  pb->minx = pb->maxx = pb->pts[0].x;
  pb->miny = pb->maxy = pb->pts[0].y;
  for (int i = 1; i < pb->n; i++) {
    if (pb->pts[i].x < pb->minx) pb->minx = pb->pts[i].x;
    if (pb->pts[i].x > pb->maxx) pb->maxx = pb->pts[i].x;
    if (pb->pts[i].y < pb->miny) pb->miny = pb->pts[i].y;
    if (pb->pts[i].y > pb->maxy) pb->maxy = pb->pts[i].y;
  }
}

static bool circle_intersect(circ c1, circ c2, isect* out) {
  float br = c1.r + c2.r;
  if (c1.x + br <= c2.x || c2.x + br <= c1.x) return false;
  if (c1.y + br <= c2.y || c2.y + br <= c1.y) return false;
  if (dist2(c1.x, c1.y, c2.x, c2.y) >= br * br) return false;
  if (out) {
    out->x = (c1.x * c2.r + c2.x * c1.r) / br;
    out->y = (c1.y * c2.r + c2.y * c1.r) / br;
    out->a = atan2f(out->y - B.y, out->x - B.x);
  }
  return true;
}

static v2 heading_abs(float angle) {
  return (v2){roundf(B.x + 500.0f * cosf(angle)),
              roundf(B.y + 500.0f * sinf(angle))};
}

static v2 heading_rel(float angle) {
  float hx = B.x + 500.0f * B.ca, hy = B.y + 500.0f * B.sa;
  float c = cosf(-angle), s = sinf(-angle);
  return (v2){roundf(c * (hx - B.x) - s * (hy - B.y) + B.x),
              roundf(s * (hx - B.x) + c * (hy - B.y) + B.y)};
}

static inline bool is_left(v2 start, v2 end, v2 p) {
  return (end.x - start.x) * (p.y - start.y) -
             (end.y - start.y) * (p.x - start.x) >
         0;
}

static void add_coll_angle(coll_pt* sp) {
  float ang = atan2f(roundf(sp->y - B.y), roundf(sp->x - B.x));
  int ai = ang_index(ang);
  float adj = sqrtf(sp->d2) - sp->r;
  float ad2 = roundf(adj * adj);
  if (!B.coll_angles_set[ai] || B.coll_angles[ai].d2 > ad2) {
    B.coll_angles[ai] =
        (coll_ang){roundf(sp->x), roundf(sp->y), ang, ad2, sp->r, sp->si, ai};
    B.coll_angles_set[ai] = true;
  }
}

static void add_food_angle(float fx, float fy, float fd2, float fsz) {
  float ang = atan2f(roundf(fy - B.y), roundf(fx - B.x));
  int ai = ang_index(ang);
  if (B.coll_angles_set[ai]) {
    float cd = sqrtf(B.coll_angles[ai].d2);
    float fd = sqrtf(fd2);
    if (cd <= fd + (B.radius * B.radius_mult * B.speed_mult) / 2.0f) return;
  }
  float score = fsz * fsz / fd2;
  if (!B.food_angles_set[ai]) {
    B.food_angles[ai] =
        (food_ang){roundf(fx), roundf(fy), ang,  fabsf(ang_between(ang, B.ang)),
                   fd2,        fsz,        score};
    B.food_angles_set[ai] = true;
  } else {
    food_ang* fa = &B.food_angles[ai];
    fa->sz += roundf(fsz);
    fa->score += score;
    if (fa->d2 > fd2) {
      fa->x = roundf(fx);
      fa->y = roundf(fy);
      fa->d2 = fd2;
    }
  }
}

static int coll_pt_cmp(const void* a, const void* b) {
  float da = ((coll_pt*)a)->d2, db = ((coll_pt*)b)->d2;
  return (da > db) - (da < db);
}

static void get_collision_points(game_data* gdata) {
  B.coll_pts_n = 0;
  memset(B.coll_angles_set, 0, sizeof(B.coll_angles_set));

  float far_d2 = (B.width * 50.0f) * (B.width * 50.0f);
  int ns = tdarray_length(gdata->data.snakes);

  for (int i = 0; i < ns; i++) {
    snake* s = gdata->data.snakes + i;
    if (s->id == B.id) continue;

    float sr = snake_width(s->sc) / 2.0f;
    float smul = fminf(1.0f, s->sp / SPEED_BASE - 1.0f);
    float srm = sr * smul * B.radius_mult;

    float hx = s->xx + cosf(s->ang) * srm / 2.0f;
    float hy = s->yy + sinf(s->ang) * srm / 2.0f;
    float hd2 = dist2(B.x, B.y, hx, hy);

    coll_pt head = {hx, hy, hd2, B.head_circle.r, i, 0};
    add_coll_angle(&head);
    if (B.coll_pts_n < MAX_COLL_PTS) B.coll_pts[B.coll_pts_n++] = head;

    int pn = tdarray_length(s->pts);
    for (int j = 0; j < pn; j++) {
      body_part* po = s->pts + j;
      if (po->dying) continue;
      float pd2 = dist2(B.x, B.y, po->xx, po->yy);
      if (pd2 > far_d2) continue;
      coll_pt bp = {po->xx, po->yy, pd2, sr, i, 1};
      add_coll_angle(&bp);
      float tr = B.head_circle.r + sr;
      if (pd2 <= tr * tr && B.coll_pts_n < MAX_COLL_PTS)
        B.coll_pts[B.coll_pts_n++] = bp;
    }
  }

  float view_ang = atan2f(gdata->data.view_yy - gdata->data.grd,
                          gdata->data.view_xx - gdata->data.grd);
  float dist_to_ctr = sqrtf(dist2(B.x, B.y, gdata->data.grd, gdata->data.grd));
  bool near_wall = (gdata->data.flux_grd - dist_to_ctr) < 1000.0f;

  if (near_wall) {
    float bpw = BORDER_PT_RADIUS * 2.0f;
    float brad = gdata->data.flux_grd + BORDER_PT_RADIUS;
    for (int k = -3; k <= 3; k++) {
      float wa = view_ang + (k * bpw) / gdata->data.flux_grd;
      float wx = gdata->data.grd + brad * cosf(wa);
      float wy = gdata->data.grd + brad * sinf(wa);
      float wd2 = dist2(B.x, B.y, wx, wy);
      coll_pt wp = {wx, wy, wd2, BORDER_PT_RADIUS, -1, 2};
      if (B.coll_pts_n < MAX_COLL_PTS) B.coll_pts[B.coll_pts_n++] = wp;
      add_coll_angle(&wp);
    }
  }

  qsort(B.coll_pts, B.coll_pts_n, sizeof(coll_pt), coll_pt_cmp);
}

static bool in_front(float px, float py) {
  float ang = atan2f(roundf(py - B.y), roundf(px - B.x));
  return fabsf(ang_between(ang, B.ang)) < FRONT_ANGLE;
}

static void avoid_point(isect* ip) {
  v2 head = {B.x, B.y};
  v2 end = {B.x + 2000.0f * B.ca, B.y + 2000.0f * B.sa};
  v2 pt = {ip->x, ip->y};
  float new_ang =
      is_left(head, end, pt) ? ip->a - (float)M_PI : ip->a + (float)M_PI;
  B.goal = heading_abs(new_ang);

  g_bot_dbg.avoid_active  = true;
  g_bot_dbg.avoid_ix      = ip->x;
  g_bot_dbg.avoid_iy      = ip->y;
  g_bot_dbg.avoid_fwd_x   = end.x;
  g_bot_dbg.avoid_fwd_y   = end.y;
}

static void heading_best_angle(void) {
  typedef struct {
    int s, e, sz;
  } oa;
  oa opens[MAXARC];
  int no = 0, open_start = -1;
  int best_ai = -1;
  float best_d = -1.0f;

  for (int i = 0; i < MAXARC; i++) {
    if (!B.coll_angles_set[i]) {
      if (open_start < 0) open_start = i;
    } else {
      float d = B.coll_angles[i].d2;
      if (best_ai < 0 || (d > best_d && d != 0.0f)) {
        best_d = d;
        best_ai = i;
      }
      if (open_start >= 0) {
        opens[no++] = (oa){open_start, i - 1, i - 1 - open_start};
        open_start = -1;
      }
    }
  }
  if (open_start >= 0) {
    if (no > 0) {
      opens[0].s = open_start;
      opens[0].sz = opens[0].e - open_start;
      if (opens[0].sz < 0) opens[0].sz += MAXARC;
    } else {
      opens[no++] = (oa){open_start, open_start, 0};
    }
  }

  if (no > 0) {
    for (int a = 0; a < no - 1; a++)
      for (int b = a + 1; b < no; b++)
        if (opens[b].sz > opens[a].sz) {
          oa t = opens[a];
          opens[a] = opens[b];
          opens[b] = t;
        }
    float mid = (opens[0].e - opens[0].sz / 2.0f) * ARC_SIZE;
    B.goal = heading_abs(mid);
  } else if (best_ai >= 0) {
    B.goal = heading_abs(best_ai * ARC_SIZE);
  }
}

static bool check_collision(game_data* gdata) {
  get_collision_points(gdata);
  if (B.coll_pts_n == 0) return false;
  for (int i = 0; i < B.coll_pts_n; i++) {
    coll_pt* cp = B.coll_pts + i;
    circ col = {cp->x, cp->y, cp->r};
    isect ip;
    if (circle_intersect(B.head_circle, col, &ip) && in_front(ip.x, ip.y)) {
      if (cp->type == 0) {
        snake* s = gdata->data.snakes + cp->si;
        gdata->bot.output.accel = s->sp > 10.0f;
      } else {
        gdata->bot.output.accel = (bool)B.default_accel;
      }
      avoid_point(&ip);
      return true;
    }
  }
  gdata->bot.output.accel = (bool)B.default_accel;
  return false;
}

static bool check_encircle(game_data* gdata) {
  if (!B.enable_encircle) return false;
  int en[512];
  memset(en, 0, sizeof(en));
  int high = 0, high_si = 0, en_all = 0;
  for (int i = 0; i < MAXARC; i++) {
    if (!B.coll_angles_set[i]) continue;
    coll_ang* ca = &B.coll_angles[i];
    if (ca->si >= 0 && ca->si < 512) {
      if (++en[ca->si] > high) {
        high = en[ca->si];
        high_si = ca->si;
      }
    }
    float ed = B.radius * ENCIRCLE_DIST_MULT;
    if (ca->d2 < ed * ed) en_all++;
  }
  if (high > (int)(MAXARC * ENCIRCLE_THRESH)) {
    heading_best_angle();
    int ns = tdarray_length(gdata->data.snakes);
    if (high != MAXARC && high_si < ns)
      gdata->bot.output.accel = gdata->data.snakes[high_si].sp > 10.0f;
    else
      gdata->bot.output.accel = (bool)B.default_accel;
    g_bot_dbg.encircle_state = 1;
    return true;
  }
  if (en_all > (int)(MAXARC * ENCIRCLE_ALL_THRESH)) {
    heading_best_angle();
    gdata->bot.output.accel = (bool)B.default_accel;
    g_bot_dbg.encircle_state = 2;
    return true;
  }
  gdata->bot.output.accel = (bool)B.default_accel;
  g_bot_dbg.encircle_state = 0;
  return false;
}

static void populate_pts(game_data* gdata) {
  B.bpts_n = 0;
  B.bpts_len = 0.0f;
  int ns = tdarray_length(gdata->data.snakes);
  snake* me = NULL;
  for (int i = 0; i < ns; i++)
    if (gdata->data.snakes[i].id == B.id) {
      me = gdata->data.snakes + i;
      break;
    }
  if (!me) return;

  B.bpts[B.bpts_n++] = (body_pt){B.x, B.y, 0.0f};
  float lx = B.x, ly = B.y, l = 0.0f;
  int pn = tdarray_length(me->pts);

  for (int p = pn - 1; p >= 0 && B.bpts_n < MAX_BODY_PTS; p--) {
    body_part* po = me->pts + p;
    if (po->dying) continue;
    l += sqrtf(dist2(lx, ly, po->xx, po->yy));
    B.bpts[B.bpts_n++] = (body_pt){po->xx, po->yy, l};
    lx = po->xx;
    ly = po->yy;
  }
  B.bpts_len = l;
}

static v2 smooth_point(float t) {
  if (B.bpts_n <= 0) return (v2){B.x, B.y};
  if (t <= 0.0f) return (v2){B.bpts[0].x, B.bpts[0].y};
  if (t >= B.bpts_len) {
    int e = B.bpts_n - 1;
    return (v2){B.bpts[e].x, B.bpts[e].y};
  }
  int lo = 0, hi = B.bpts_n - 1;
  while (hi - lo > 1) {
    int m = (lo + hi) / 2;
    if (t > B.bpts[m].len)
      lo = m;
    else
      hi = m;
  }
  float wp = B.bpts[hi].len - t, wq = t - B.bpts[lo].len, w = wp + wq;
  if (w <= 1e-6f) return (v2){B.bpts[hi].x, B.bpts[hi].y};
  return (v2){(wp * B.bpts[lo].x + wq * B.bpts[hi].x) / w,
              (wp * B.bpts[lo].y + wq * B.bpts[hi].y) / w};
}

static float closest_body_point(void) {
  if (B.bpts_n < 3) return B.bpts_len;
  int start_n = 0;
  float start_d2 = 0.0f;
  for (;;) {
    float prev = start_d2;
    start_n++;
    if (start_n >= B.bpts_n) break;
    start_d2 = dist2(B.x, B.y, B.bpts[start_n].x, B.bpts[start_n].y);
    if (start_d2 < prev || start_n == B.bpts_n - 1) break;
  }
  if (start_n >= B.bpts_n || start_n <= 1) return B.bpts_len;

  int min_n = start_n;
  float min_d2 = start_d2;
  for (int n = min_n + 1; n < B.bpts_n; n++) {
    float d2 = dist2(B.x, B.y, B.bpts[n].x, B.bpts[n].y);
    if (d2 < min_d2) {
      min_n = n;
      min_d2 = d2;
    }
  }

  int next_n;
  float next_d2;
  if (min_n == B.bpts_n - 1) {
    next_n = min_n - 1;
    next_d2 = dist2(B.x, B.y, B.bpts[next_n].x, B.bpts[next_n].y);
  } else {
    float dm = dist2(B.x, B.y, B.bpts[min_n - 1].x, B.bpts[min_n - 1].y);
    float dp = dist2(B.x, B.y, B.bpts[min_n + 1].x, B.bpts[min_n + 1].y);
    if (dm < dp) {
      next_n = min_n - 1;
      next_d2 = dm;
    } else {
      next_n = min_n + 1;
      next_d2 = dp;
    }
  }

  float t2 = B.bpts[min_n].len - B.bpts[next_n].len;
  t2 *= t2;
  if (t2 == 0.0f) return B.bpts[min_n].len;
  float min_w = t2 - (min_d2 - next_d2);
  float next_w = t2 + (min_d2 - next_d2);
  return (B.bpts[min_n].len * min_w + B.bpts[next_n].len * next_w) /
         (2.0f * t2);
}

static void determine_circle_dir(void) {
  if (B.bpts_n <= 0) return;
  float cx = 0, cy = 0;
  for (int i = 0; i < B.bpts_n; i++) {
    cx += B.bpts[i].x;
    cy += B.bpts[i].y;
  }
  cx /= B.bpts_n;
  cy /= B.bpts_n;
  float dx = B.x - cx, dy = B.y - cy;
  B.circle_dir = (-dy * B.ca + dx * B.sa > 0.0f) ? -1 : +1;
}

static void body_danger_zone(float offset, v2 target_pt,
                             v2 target_norm,
                             float close_dist,
                             v2 past_target, v2 close_pt, poly_box* out) {
  int o = B.circle_dir;
  v2 raw[10];
  raw[0] = (v2){B.x - o * offset * B.sa, B.y + o * offset * B.ca};
  raw[1] = (v2){B.x + B.width * B.ca + offset * (B.ca - o * B.sa),
                B.y + B.width * B.sa + offset * (B.sa + o * B.ca)};
  raw[2] = (v2){B.x + 1.75f * B.width * B.ca + o * 0.3f * B.width * B.sa +
                    offset * (B.ca - o * B.sa),
                B.y + 1.75f * B.width * B.sa - o * 0.3f * B.width * B.ca +
                    offset * (B.sa + o * B.ca)};
  raw[3] = (v2){B.x + 2.5f * B.width * B.ca + o * 0.7f * B.width * B.sa +
                    offset * (B.ca - o * B.sa),
                B.y + 2.5f * B.width * B.sa - o * 0.7f * B.width * B.ca +
                    offset * (B.sa + o * B.ca)};
  raw[4] = (v2){
      B.x + 3.0f * B.width * B.ca + o * 1.2f * B.width * B.sa + offset * B.ca,
      B.y + 3.0f * B.width * B.sa - o * 1.2f * B.width * B.ca + offset * B.sa};
  float cpd_half = 0.5f * fmaxf(close_dist, 0.0f);
  raw[5] = (v2){target_pt.x + target_norm.x * (offset + cpd_half),
                target_pt.y + target_norm.y * (offset + cpd_half)};
  raw[6] = (v2){past_target.x + target_norm.x * offset,
                past_target.y + target_norm.y * offset};
  raw[7] = past_target;
  raw[8] = target_pt;
  raw[9] = close_pt;

  v2 scratch[10];
  memcpy(scratch, raw, sizeof(raw));
  out->n = convex_hull(scratch, 10, out->pts);
  poly_box_aabb(out);
}

static void construct_inside_polygon(float close_t, poly_box_large* out) {
  float start_t = 5.0f * B.width;
  float end_t = close_t + 5.0f * B.width;

  out->n = 0;
  out->pts[out->n++] = smooth_point(end_t);
  out->pts[out->n++] = smooth_point(start_t);
  for (float t = start_t; t < end_t && out->n < MAX_INSIDE_PTS; t += B.width)
    out->pts[out->n++] = smooth_point(t);

  poly_box_large_aabb(out);
}

static void follow_circle_legacy(game_data* gdata) {
  populate_pts(gdata);
  determine_circle_dir();
  int o = B.circle_dir;

  /*
   * The corridor/spine projection below needs enough looped body (9
   * body-widths) to have a stable near/far reference point -- on a
   * small or freshly-formed loop that isn't met yet. This matches the
   * faithful original: just leave B.goal untouched and wait for the
   * loop to grow, rather than actively steering here. An earlier
   * version of this fix nudged the heading directly, but that nudge
   * had no awareness of the arena center or the wall -- if this
   * condition kept re-triggering it could walk the snake straight
   * toward the boundary with nothing pulling it back. Freezing the
   * goal is safe because check_collision()/check_encircle() (stage 1,
   * run before the snake ever reaches stage 2) already handle wall
   * and enemy avoidance reactively; this early-return only matters
   * for the brief window right as a loop first forms.
   */
  if (B.bpts_len < 9.0f * B.width) return;

  float close_t = closest_body_point();
  v2 close_pt = smooth_point(close_t);
  v2 close_next = smooth_point(close_t - B.width);
  v2 close_tang =
      unit_vec((v2){close_next.x - close_pt.x, close_next.y - close_pt.y});
  v2 close_norm = (v2){-o * close_tang.y, o * close_tang.x};

  float cur_course = asinf(
      fmaxf(-1.0f, fminf(1.0f, B.ca * close_norm.x + B.sa * close_norm.y)));

  float close_dist =
      (B.x - close_pt.x) * close_norm.x + (B.y - close_pt.y) * close_norm.y;

  poly_box_large inside_poly;
  construct_inside_polygon(close_t, &inside_poly);

  float target_t = close_t;
  float target_far = 0.0f;
  float step = B.width / 64.0f;
  for (float h = close_dist, a = cur_course; h >= 0.125f * B.width;) {
    target_t -= step;
    target_far += step * cosf(a);
    h += step * sinf(a);
    a = fmaxf(-(float)M_PI / 4.0f, a - step / B.width);
  }
  v2 target_pt = smooth_point(target_t);
  v2 past_target = smooth_point(target_t - 3.0f * B.width);

  float offset_incr = 0.0625f * B.width;

  /*
   * Track the nearest enemy head and the nearest enemy body point
   * separately, each as one plain min-distance scan (no per-point
   * polygon-offset search -- that's what caused the earlier
   * instability, not the general idea of reacting to proximity).
   * Deliberately no tight "collision imminent" distance gate here: a
   * gate tight enough to only fire once contact is nearly certain
   * doesn't leave enough frames for target_course to actually turn the
   * loop away in time, especially while the loop itself is actively
   * expanding outward -- confirmed against real play, not assumed.
   * The smooth falloff + hard cap below is what keeps this from
   * overreacting to merely-nearby, harmless enemies; it doesn't need
   * a hard range cutoff on top of that to behave.
   */
  float enemy_head_d2 = 64.0f * 64.0f * B.width * B.width;
  float enemy_body_d2 = 64.0f * 64.0f * B.width * B.width;

  int ns = tdarray_length(gdata->data.snakes);
  for (int i = 0; i < ns; i++) {
    snake* sk = gdata->data.snakes + i;
    if (sk->id == B.id) continue;

    v2 eh = {sk->xx, sk->yy};
    v2 ea = {eh.x + cosf(sk->ang) * B.width, eh.y + sinf(sk->ang) * B.width};

    if (!point_in_poly_large(eh, &inside_poly)) {
      float d1 = dist2(eh.x, eh.y, target_pt.x, target_pt.y);
      float d2_ = dist2(ea.x, ea.y, target_pt.x, target_pt.y);
      float dm = d1 < d2_ ? d1 : d2_;
      if (dm < enemy_head_d2) enemy_head_d2 = dm;
    }

    /*
     * Same idea as the head check above, but for the enemy's body --
     * catches a snake trapping the loop by parking its body nearby
     * while its head moves elsewhere (or leaves entirely), which was
     * invisible to head_prox alone.
     */
    int pn = tdarray_length(sk->pts);
    for (int j = 0; j < pn; j++) {
      body_part* po = sk->pts + j;
      if (po->dying) continue;
      v2 pt = {po->xx, po->yy};
      if (point_in_poly_large(pt, &inside_poly)) continue;

      float dbp = dist2(pt.x, pt.y, target_pt.x, target_pt.y);
      if (dbp < enemy_body_d2) enemy_body_d2 = dbp;
    }
  }

  float dist_to_ctr = sqrtf(dist2(B.x, B.y, gdata->data.grd, gdata->data.grd));
  bool near_wall = (gdata->data.flux_grd - dist_to_ctr) < 1000.0f;
  float wall_od = 0.0f;

  if (near_wall) {
    float view_ang = atan2f(gdata->data.view_yy - gdata->data.grd,
                            gdata->data.view_xx - gdata->data.grd);
    float bpw = BORDER_PT_RADIUS * 2.0f;
    float brad = gdata->data.flux_grd + BORDER_PT_RADIUS;
    float wall_off = 0.5f * (B.width + BORDER_PT_RADIUS);
    bool offset_set = false;
    float offset = 0.0f;
    poly_box cpolbody;

    for (int k = -2; k <= 2; k++) {
      float wa = view_ang + (k * bpw) / gdata->data.flux_grd;
      v2 wp = {gdata->data.grd + brad * cosf(wa),
               gdata->data.grd + brad * sinf(wa)};

      while (!offset_set ||
             (wall_od >= -B.width && point_in_poly(wp, &cpolbody))) {
        if (!offset_set) {
          offset_set = true;
        } else {
          wall_od -= offset_incr;
        }
        offset = wall_off + wall_od;
        body_danger_zone(offset, target_pt, close_norm, close_dist, past_target,
                         close_pt, &cpolbody);
      }
    }
  }

  float enemy_head_dist = sqrtf(enemy_head_d2);

  float target_course = cur_course + 0.25f;

  float head_prox = -1.0f - (2.0f * target_far - enemy_head_dist) / B.width;
  if (head_prox > 0)
    head_prox = 0.125f * head_prox * head_prox;
  else
    head_prox = -0.500f * head_prox * head_prox;
  /* In a dense crowd, the single nearest enemy head is often just close
   * by from sheer density, not on an actual collision line -- without a
   * floor here, that still forces target_course arbitrarily negative,
   * which yanks the goal toward the center of the loop instead of
   * continuing to circle. Cap the pull so a genuinely close call still
   * gets a strong reaction, but bystanders can't override circling. */
  head_prox = fmaxf(head_prox, -2.0f);
  target_course = fminf(target_course, head_prox);

  float enemy_body_dist = sqrtf(enemy_body_d2);

  float body_prox = -1.0f - (2.0f * target_far - enemy_body_dist) / B.width;
  if (body_prox > 0)
    body_prox = 0.125f * body_prox * body_prox;
  else
    body_prox = -0.500f * body_prox * body_prox;
  /* Same reasoning as head_prox's cap: a body segment merely nearby in a
   * crowd shouldn't override circling, only one actually on the
   * approach line should pull the target hard. */
  body_prox = fmaxf(body_prox, -2.0f);
  target_course = fminf(target_course, body_prox);

  if (near_wall) {
    float adj_wall = (wall_od - 0.0625f * B.width) / B.width;
    target_course = fminf(target_course, target_course + adj_wall);
  }

  float tail_behind = B.bpts_len - close_t;
  float allow_tail = B.width * 2.0f;
  target_course =
      fminf(target_course,
            (tail_behind - allow_tail + (B.width - close_dist)) / B.width);

  target_course =
      fminf(target_course, (-0.5f * (close_dist - 4.0f * B.width)) / B.width);

  target_course = fmaxf(target_course, (-0.75f * close_dist) / B.width);
  target_course = fminf(target_course, 1.0f);

  v2 goal_dir = {close_tang.x * cosf(target_course) -
                     o * close_tang.y * sinf(target_course),
                 close_tang.y * cosf(target_course) +
                     o * close_tang.x * sinf(target_course)};
  v2 goal = {B.x + goal_dir.x * 4.0f * B.width,
             B.y + goal_dir.y * 4.0f * B.width};

  if (fabsf(goal.x - B.goal.x) < 1000.0f &&
      fabsf(goal.y - B.goal.y) < 1000.0f) {
    B.goal.x = roundf(goal.x * 0.25f + B.goal.x * 0.75f);
    B.goal.y = roundf(goal.y * 0.25f + B.goal.y * 0.75f);
  } else {
    B.goal = (v2){roundf(goal.x), roundf(goal.y)};
  }
}

/* ======================================================================
 * Circle planner (stage 2)
 *
 * The loop geometry (how far out / in the head may drift relative to its
 * own body) is still computed the same way as before and gives a
 * "preferred" course.  What changed is how enemies are handled:
 *
 *   - Every enemy body point and head near us is considered, including
 *     the ones inside our own loop (nothing is ignored any more).
 *   - A handful of candidate courses (outward .. inward) are simulated a
 *     few body-lengths ahead using our real turn rate.  Each simulated
 *     path is "5 steps with the candidate course, then a hard turn back
 *     toward our own body", so a candidate only counts as safe if we also
 *     still have a clear way back (the escape arc).
 *   - Enemy heads are predicted forward in time, and a worst-case
 *     "could it boost to that point before we pass it" check is applied.
 *     Enemies behind / far side of us take longer to arrive, so the bot
 *     can keep expanding when only those are around.
 *   - An enemy that is aimed at the loop locks expansion for a short
 *     while (the loop holds its size instead of growing into it).
 *   - If no candidate is safe, a breakout search over absolute headings
 *     (with and without boost) picks the best way out of the squeeze.
 * ====================================================================== */

#define PL_MAX_OBS 384
#define PL_MAX_HEADS 24
#define PL_STEPS 12
#define PL_PHASE_A 5
#define PL_EM_STEPS 5
#define PL_VETO_STEPS 8
#define PL_ESC_STEPS 10
#define PL_MAX_CAND 12
#define PL_STEP_W 0.75f
#define PL_HARD_MARGIN_W 0.35f
#define PL_CLR_CAP_W 2.0f
#define PL_RET_COURSE (-0.9f)
#define PL_COURSE_MIN (-1.4f)
#define PL_COURSE_MAX 1.2f
#define PL_BOOST_SP 12.0f
#define PL_INT_BUF_W 3.0f
#define PL_LOCK_FRAMES 20
#define PL_LOCK_RANGE_W 20.0f
#define PL_LOCK_AIM 0.64f
#define PL_ESC_HEADINGS 16
#define PL_GAP_MAX_W 4.0f
#define PL_PURSUE_RANGE_W 16.0f
#define PL_INT_CAP 8.0f
#define PL_COMFORT_W 0.8f
#define PL_COMFORT_INT 2.5f
#define PL_ESC_RUN_MAX 24
#define PL_ESC_COOLDOWN 45

typedef struct {
  float x, y, r;
} pl_obs;

typedef struct {
  float x, y, dx, dy, sp, hr, rturn, d2;
} pl_head;

typedef struct {
  float x, y, a;
} pl_hs;

typedef struct {
  int fail;       /* first step breaking a hard margin, PL_STEPS if none */
  float clear;    /* smallest gap seen, in body widths (capped) */
  float intercept; /* soft penalty: enemy heads that could cut the path */
  int n;
  float x[PL_STEPS], y[PL_STEPS];
} pl_roll;

typedef struct {
  pl_obs obs[PL_MAX_OBS];
  int obs_n;
  pl_head heads[PL_MAX_HEADS];
  int heads_n;
  float ds, my_sp, turn_per_d;
  bool wall_on;
  float wall_c, wall_r;
  int lock;
  float prev_course;
  bool prev_valid;
  float esc_ang;
  bool esc_valid;
  int esc_run;
  int esc_cool;
} pl_state;

static pl_state P;

static inline float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

/* Wraps to [-pi, pi]; NaN in -> NaN out (never loops forever). */
static inline float wrap_pi(float a) {
  float r = fmodf(a + (float)M_PI, 2.0f * (float)M_PI);
  if (r < 0.0f) r += 2.0f * (float)M_PI;
  return r - (float)M_PI;
}

static snake* find_me(game_data* gdata) {
  int ns = tdarray_length(gdata->data.snakes);
  for (int i = 0; i < ns; i++)
    if (gdata->data.snakes[i].id == B.id) return gdata->data.snakes + i;
  return NULL;
}

/* Gather enemy heads and body points near us.  Nearest points win if
 * there are too many: the scan radius shrinks until everything fits. */
static void pl_collect(game_data* gdata) {
  P.obs_n = 0;
  P.heads_n = 0;
  int ns = tdarray_length(gdata->data.snakes);

  float head_range = fminf(40.0f * B.width, 2400.0f);
  float head_r2 = head_range * head_range;
  for (int i = 0; i < ns; i++) {
    snake* s = gdata->data.snakes + i;
    if (s->id == B.id || s->dead) continue;
    float d2 = dist2(B.x, B.y, s->xx, s->yy);
    if (!(d2 <= head_r2)) continue; /* also rejects NaN */
    pl_head h;
    h.x = s->xx;
    h.y = s->yy;
    h.dx = cosf(s->ang);
    h.dy = sinf(s->ang);
    h.sp = fmaxf(s->sp, 1.0f);
    h.hr = snake_width(s->sc) / 2.0f;
    float kt = gdata->data.mamu * fmaxf(s->scang, 0.2f);
    h.rturn = 1.445f / fmaxf(kt, 0.005f);
    h.d2 = d2;
    if (P.heads_n < PL_MAX_HEADS) {
      P.heads[P.heads_n++] = h;
    } else {
      int far_i = 0;
      for (int k = 1; k < PL_MAX_HEADS; k++)
        if (P.heads[k].d2 > P.heads[far_i].d2) far_i = k;
      if (d2 < P.heads[far_i].d2) P.heads[far_i] = h;
    }
  }

  static const float bands[3] = {32.0f, 16.0f, 8.0f};
  for (int b = 0; b < 3; b++) {
    float rr = fminf(bands[b] * B.width, 2000.0f - 400.0f * (float)b);
    float rr2 = rr * rr;
    bool overflow = false;
    P.obs_n = 0;
    for (int i = 0; i < ns && !overflow; i++) {
      snake* s = gdata->data.snakes + i;
      if (s->id == B.id || s->dead) continue;
      float sr = snake_width(s->sc) / 2.0f;
      int pn = tdarray_length(s->pts);
      for (int j = 0; j < pn; j++) {
        body_part* po = s->pts + j;
        if (po->dying) continue;
        float d2 = dist2(B.x, B.y, po->xx, po->yy);
        if (!(d2 <= rr2)) continue;
        if (P.obs_n >= PL_MAX_OBS) {
          overflow = true;
          break;
        }
        P.obs[P.obs_n++] = (pl_obs){po->xx, po->yy, sr};
      }
    }
    if (!overflow) break;
  }

  float dctr = sqrtf(dist2(B.x, B.y, gdata->data.grd, gdata->data.grd));
  P.wall_on = (gdata->data.flux_grd - dctr) < 60.0f * B.width;
  P.wall_c = gdata->data.grd;
  P.wall_r = gdata->data.flux_grd;
}

/* One simulated position: updates hard-fail step, clearance and the
 * per-head interception margins (hm[], in body widths; small = bad).
 * hs[] are the enemy heads' simulated states; enemies close to us are
 * assumed to steer at us (pursuit), the rest keep their heading. */
static void pl_step_check(pl_roll* r, int k, float px, float py, float S,
                          float my_speed, float* hm, pl_hs* hs,
                          const bool* pursue) {
  float w = B.width;
  float hard = PL_HARD_MARGIN_W * w;
  float cap = PL_CLR_CAP_W * w;
  float best = cap;
  bool fail = false;

  for (int i = 0; i < P.obs_n; i++) {
    const pl_obs* o = &P.obs[i];
    float reach = B.radius + o->r + cap;
    float dx = px - o->x;
    if (dx > reach || dx < -reach) continue;
    float dy = py - o->y;
    if (dy > reach || dy < -reach) continue;
    float d2 = dx * dx + dy * dy;
    if (d2 >= reach * reach) continue;
    float gap = sqrtf(d2) - (B.radius + o->r);
    if (gap < best) best = gap;
  }

  if (P.wall_on) {
    float dc = sqrtf(dist2(px, py, P.wall_c, P.wall_c));
    float gap = (P.wall_r - dc) - B.radius;
    if (gap < best) best = gap;
  }

  if (best < hard) fail = true;

  for (int h = 0; h < P.heads_n; h++) {
    const pl_head* e = &P.heads[h];

    /* advance the enemy head one step (same elapsed time as our step) */
    float adv = P.ds * e->sp / my_speed;
    if (pursue[h]) {
      float want = atan2f(py - hs[h].y, px - hs[h].x);
      float lim = adv / e->rturn;
      hs[h].a += clampf(wrap_pi(want - hs[h].a), -lim, lim);
      hs[h].x += cosf(hs[h].a) * adv;
      hs[h].y += sinf(hs[h].a) * adv;
    } else {
      hs[h].x += e->dx * adv;
      hs[h].y += e->dy * adv;
    }

    float gap = sqrtf(dist2(px, py, hs[h].x, hs[h].y)) - (B.radius + e->hr);
    if (gap < best) best = gap;
    if (gap < hard + 0.06f * S) fail = true;

    /* worst case: it turns and boosts straight at this point */
    float rx = px - e->x, ry = py - e->y;
    float dd = sqrtf(rx * rx + ry * ry);
    if (dd > 34.0f * w) continue; /* too far to matter within the horizon */
    float de = dd - (B.radius + e->hr);
    if (de < 0.0f) de = 0.0f;
    float ca = dd > 1e-3f ? (rx * e->dx + ry * e->dy) / dd : 1.0f;
    float off = acosf(clampf(ca, -1.0f, 1.0f));
    float t_en = (de + 0.5f * off * e->rturn) * my_speed / PL_BOOST_SP;
    float m = (t_en - S) / w;
    if (m < hm[h]) hm[h] = m;
  }

  if (fail && r->fail > k) r->fail = k;
  float cw = best / w;
  if (cw < r->clear) r->clear = cw;
}

static void pl_init_heads(float* hm, pl_hs* hs, bool* pursue) {
  float range = PL_PURSUE_RANGE_W * B.width;
  for (int h = 0; h < PL_MAX_HEADS; h++) hm[h] = 1e9f;
  for (int h = 0; h < P.heads_n; h++) {
    const pl_head* e = &P.heads[h];
    hs[h] = (pl_hs){e->x, e->y, atan2f(e->dy, e->dx)};
    pursue[h] = e->d2 < range * range;
  }
}

static void pl_finish(pl_roll* r, const float* hm) {
  float pen = 0.0f;
  for (int h = 0; h < P.heads_n; h++) {
    float m = hm[h];
    if (m < PL_INT_BUF_W) {
      float v = (PL_INT_BUF_W - m) / PL_INT_BUF_W;
      if (v > 1.5f) v = 1.5f;
      pen += 1.5f * v;
    }
  }
  r->intercept = pen > PL_INT_CAP ? PL_INT_CAP : pen;
}

/* Candidate path following our own body: `course` for PL_PHASE_A steps,
 * then a hard turn back toward the body (escape arc). */
static void pl_roll_spine(float course, float t0, float period, pl_roll* r) {
  int o = B.circle_dir;
  float x = B.x, y = B.y, ang = B.ang, tc = t0;
  float ds = P.ds, max_turn = P.turn_per_d * ds;
  float hm[PL_MAX_HEADS];
  pl_hs hs[PL_MAX_HEADS];
  bool pursue[PL_MAX_HEADS];
  pl_init_heads(hm, hs, pursue);
  r->fail = PL_STEPS;
  r->clear = PL_CLR_CAP_W;
  r->intercept = 0.0f;
  r->n = 0;
  bool can_wrap = period > 2.0f * B.width;

  for (int k = 0; k < PL_STEPS; k++) {
    float c = course;
    if (k >= PL_PHASE_A && c > PL_RET_COURSE) c = PL_RET_COURSE;

    float ta = tc, tb = tc - B.width;
    if (tb < 0.0f && can_wrap) {
      ta += period;
      tb += period;
    }
    v2 pa = smooth_point(ta), pb = smooth_point(tb);
    float tx = pb.x - pa.x, ty = pb.y - pa.y;
    float tang = (tx * tx + ty * ty > 1e-4f) ? atan2f(ty, tx) : ang;

    float psi = tang + (float)o * c;
    float da = clampf(wrap_pi(psi - ang), -max_turn, max_turn);
    ang += da;
    x += ds * cosf(ang);
    y += ds * sinf(ang);

    tc -= ds * fmaxf(cosf(wrap_pi(ang - tang)), 0.2f);
    if (tc < 0.0f && can_wrap) tc += period;

    r->x[k] = x;
    r->y[k] = y;
    r->n = k + 1;
    pl_step_check(r, k, x, y, (float)(k + 1) * ds, P.my_sp, hm, hs, pursue);
    if (r->fail <= k) break;
  }
  pl_finish(r, hm);
}

/* Breakout path: turn toward an absolute heading, then run straight. */
static void pl_roll_dir(float heading, bool boost, int steps, pl_roll* r) {
  float speed = boost ? PL_BOOST_SP : P.my_sp;
  float tpd = boost ? P.turn_per_d * (P.my_sp / PL_BOOST_SP) : P.turn_per_d;
  float x = B.x, y = B.y, ang = B.ang;
  float ds = P.ds, max_turn = tpd * ds;
  float hm[PL_MAX_HEADS];
  pl_hs hs[PL_MAX_HEADS];
  bool pursue[PL_MAX_HEADS];
  pl_init_heads(hm, hs, pursue);
  r->fail = PL_STEPS;
  r->clear = PL_CLR_CAP_W;
  r->intercept = 0.0f;
  r->n = 0;

  for (int k = 0; k < steps && k < PL_STEPS; k++) {
    float da = clampf(wrap_pi(heading - ang), -max_turn, max_turn);
    ang += da;
    x += ds * cosf(ang);
    y += ds * sinf(ang);
    r->x[k] = x;
    r->y[k] = y;
    r->n = k + 1;
    pl_step_check(r, k, x, y, (float)(k + 1) * ds, speed, hm, hs, pursue);
    if (r->fail <= k) break;
  }
  pl_finish(r, hm);
}

static float pl_score_common(const pl_roll* r) {
  float s = 0.0f;
  if (r->fail < PL_STEPS) s -= 1000.0f - 20.0f * (float)r->fail;
  s += fminf(r->clear, PL_CLR_CAP_W);
  s -= r->intercept;
  return s;
}

static float pl_score_spine(const pl_roll* r, float course, float c_cap,
                            float c_floor) {
  float s = pl_score_common(r);
  float dev = 0.0f;
  if (course > c_cap)
    dev += 3.0f * (course - c_cap);
  else
    dev += c_cap - course;
  if (course < c_floor) dev += 3.0f * (c_floor - course);
  s -= dev;
  if (P.prev_valid && fabsf(course - P.prev_course) < 0.12f) s += 0.15f;
  return s;
}

static float pl_score_esc(const pl_roll* r, float heading, bool boost) {
  float s = pl_score_common(r);
  if (boost) s -= 0.5f;
  s -= 0.3f * fabsf(wrap_pi(heading - B.ang)) / (float)M_PI;
  if (P.esc_valid && fabsf(wrap_pi(heading - P.esc_ang)) < 0.2f) s += 0.4f;
  return s;
}

/* An enemy heading at us from within range: hold the loop's size. */
static bool pl_head_on(void) {
  float range = PL_LOCK_RANGE_W * B.width;
  for (int h = 0; h < P.heads_n; h++) {
    const pl_head* e = &P.heads[h];
    float rx = B.x - e->x, ry = B.y - e->y;
    float d = sqrtf(rx * rx + ry * ry);
    if (d > range || d < 1.0f) continue;
    float aim = (e->dx * rx + e->dy * ry) / d;
    if (aim > PL_LOCK_AIM) return true;
  }
  return false;
}

typedef struct {
  bool breakout;
  float course;
  float esc_ang;
  bool esc_boost;
  int mode; /* 0 calm, 1 pressured, 2 expansion locked, 3 breakout */
  pl_roll path;
  bool has_path;
} pl_result;

static void pl_plan(float c_geo, float c_floor, float t0, float period,
                    pl_result* out) {
  out->breakout = false;
  out->esc_boost = false;
  out->esc_ang = B.ang;
  out->has_path = false;

  if (pl_head_on())
    P.lock = PL_LOCK_FRAMES;
  else if (P.lock > 0)
    P.lock--;

  float c_cap = c_geo;
  if (P.lock > 0) c_cap = fmaxf(fminf(c_geo, 0.0f), c_floor);
  int calm_mode = P.lock > 0 ? 2 : 0;

  /* nothing around at all: just take the geometry course */
  if (P.obs_n == 0 && P.heads_n == 0 && !P.wall_on) {
    out->course = c_cap;
    out->mode = calm_mode;
    P.prev_course = c_cap;
    P.prev_valid = true;
    P.esc_valid = false;
    return;
  }

  float cand[PL_MAX_CAND];
  int nc = 0;
  static const float offs[6] = {0.0f, -0.15f, -0.3f, -0.5f, -0.75f, -1.0f};
  float extra[4] = {c_floor, PL_COURSE_MIN, c_cap + 0.35f, PL_COURSE_MAX};
  for (int i = 0; i < 10; i++) {
    float c = i < 6 ? c_cap + offs[i] : extra[i - 6];
    c = clampf(c, PL_COURSE_MIN, PL_COURSE_MAX);
    bool dup = false;
    for (int j = 0; j < nc; j++)
      if (fabsf(cand[j] - c) < 0.04f) dup = true;
    if (!dup && nc < PL_MAX_CAND) cand[nc++] = c;
  }

  pl_roll roll, best_roll;
  float best_score = -1e30f, best_c = c_cap;
  bool have = false;

  /* The geometry course is what has always kept the loop; stick with it
   * whenever it is comfortably safe, and only look for alternatives
   * when it is not. */
  pl_roll_spine(cand[0], t0, period, &roll);
  if (roll.fail >= PL_STEPS && roll.clear >= PL_COMFORT_W &&
      roll.intercept < PL_COMFORT_INT) {
    out->course = cand[0];
    out->mode = calm_mode;
    out->path = roll;
    out->has_path = true;
    P.prev_course = cand[0];
    P.prev_valid = true;
    P.esc_valid = false;
    P.esc_run = 0;
    if (P.esc_cool > 0) P.esc_cool--;
    return;
  }

  for (int i = 0; i < nc; i++) {
    pl_roll_spine(cand[i], t0, period, &roll);
    float s = pl_score_spine(&roll, cand[i], c_cap, c_floor);
    if (!have || s > best_score) {
      best_score = s;
      best_c = cand[i];
      best_roll = roll;
      have = true;
    }
  }
  if (!have) {
    out->course = c_cap;
    out->mode = calm_mode;
    return;
  }

  /* squeezed: look for a way out in any direction.  Boost is a last
   * resort: only used if no normal-speed escape is fully clear. */
  if (P.esc_cool > 0) P.esc_cool--;
  if (P.esc_cool == 0 && best_roll.fail < PL_EM_STEPS) {
    float nb_score = -1e30f, nb_h = B.ang;
    float bo_score = -1e30f, bo_h = B.ang;
    bool nb_have = false, bo_have = false;
    pl_roll esc_roll, nb_roll, bo_roll;
    for (int a = 0; a < PL_ESC_HEADINGS; a++) {
      float h = wrap_pi(B.ang + (2.0f * (float)M_PI * (float)a) /
                                    (float)PL_ESC_HEADINGS);
      for (int bst = 0; bst < 2; bst++) {
        pl_roll_dir(h, bst != 0, PL_ESC_STEPS, &esc_roll);
        float s = pl_score_esc(&esc_roll, h, bst != 0);
        if (bst == 0) {
          if (!nb_have || s > nb_score) {
            nb_score = s;
            nb_h = h;
            nb_roll = esc_roll;
            nb_have = true;
          }
        } else if (!bo_have || s > bo_score) {
          bo_score = s;
          bo_h = h;
          bo_roll = esc_roll;
          bo_have = true;
        }
      }
    }
    bool use_boost = bo_have && (!nb_have || (nb_roll.fail < PL_STEPS &&
                                              bo_score > nb_score));
    bool esc_have = nb_have || bo_have;
    float esc_best = use_boost ? bo_score : nb_score;
    if (esc_have && esc_best > best_score) {
      out->breakout = true;
      out->esc_ang = use_boost ? bo_h : nb_h;
      out->esc_boost = use_boost;
      out->course = best_c;
      out->mode = 3;
      out->path = use_boost ? bo_roll : nb_roll;
      out->has_path = true;
      P.esc_ang = out->esc_ang;
      P.esc_valid = true;
      P.prev_valid = false;
      if (++P.esc_run >= PL_ESC_RUN_MAX) {
        P.esc_run = 0;
        P.esc_cool = PL_ESC_COOLDOWN;
      }
      return;
    }
  }

  P.esc_valid = false;
  P.esc_run = 0;
  P.prev_course = best_c;
  P.prev_valid = true;
  out->course = best_c;
  out->path = best_roll;
  out->has_path = true;
  out->mode = (best_c >= c_cap - 0.06f) ? calm_mode : 1;
}

/* Loop geometry relative to our own body: where the closest body point
 * is, its direction, and how far outward the head may drift. */
typedef struct {
  float close_t;
  v2 tang;
  float c_geo, c_floor;
} pl_geom;

static void pl_compute_geom(pl_geom* g) {
  int o = B.circle_dir;
  float close_t = closest_body_point();
  v2 close_pt = smooth_point(close_t);
  v2 close_next = smooth_point(close_t - B.width);
  v2 close_tang =
      unit_vec((v2){close_next.x - close_pt.x, close_next.y - close_pt.y});
  if (close_tang.x == 0.0f && close_tang.y == 0.0f)
    close_tang = (v2){B.ca, B.sa};
  v2 close_norm = (v2){-o * close_tang.y, o * close_tang.x};

  float cur_course = asinf(clampf(B.ca * close_norm.x + B.sa * close_norm.y,
                                  -1.0f, 1.0f));
  float close_dist =
      (B.x - close_pt.x) * close_norm.x + (B.y - close_pt.y) * close_norm.y;

  /* geometry: how far outward the loop is allowed to drift */
  float c_geo = cur_course + 0.25f;
  float tail_behind = B.bpts_len - close_t;
  float allow_tail = B.width * 2.0f;
  c_geo = fminf(c_geo, (tail_behind - allow_tail + (B.width - close_dist)) /
                           B.width);
  c_geo = fminf(c_geo, (-0.5f * (close_dist - PL_GAP_MAX_W * B.width)) /
                           B.width);
  float c_floor = (-0.75f * close_dist) / B.width;
  c_geo = fmaxf(c_geo, c_floor);
  c_geo = fminf(c_geo, 1.0f);
  if (!isfinite(c_geo)) c_geo = 0.0f;
  if (!isfinite(c_floor)) c_floor = 0.0f;
  c_geo = clampf(c_geo, PL_COURSE_MIN, 1.0f);
  c_floor = fminf(clampf(c_floor, PL_COURSE_MIN, 1.0f), c_geo);

  g->close_t = close_t;
  g->tang = close_tang;
  g->c_geo = c_geo;
  g->c_floor = c_floor;
}

/* Per-frame inputs shared by the planner and the veto check. */
static void pl_setup(game_data* gdata) {
  snake* me = find_me(gdata);
  float mamu = gdata->data.mamu > 0.0f ? gdata->data.mamu : 0.033f;
  float scang = me ? fmaxf(me->scang, 0.2f) : 1.0f;
  float spang = me ? clampf(me->spang, 0.5f, 1.0f) : 1.0f;
  P.my_sp = me ? fmaxf(me->sp, 4.0f) : SPEED_BASE;
  P.turn_per_d = (mamu * scang * spang) / (P.my_sp / 4.0f);
  P.ds = PL_STEP_W * B.width;
  pl_collect(gdata);
}

static void follow_circle_planner(game_data* gdata) {
  populate_pts(gdata);
  determine_circle_dir();
  int o = B.circle_dir;
  g_bot_dbg.plan_n = 0;
  g_bot_dbg.plan_mode = 0;

  /* Too little looped body for a stable spine reference: behave exactly
   * like the original code did (it simply keeps its last goal). */
  if (B.bpts_n < 3 || B.bpts_len < 9.0f * B.width) {
    follow_circle_legacy(gdata);
    return;
  }

  pl_geom geo;
  pl_compute_geom(&geo);
  float close_t = geo.close_t;
  v2 close_tang = geo.tang;
  float c_geo = geo.c_geo;
  float c_floor = geo.c_floor;

  pl_setup(gdata);

  pl_result res;
  pl_plan(c_geo, c_floor, close_t, close_t, &res);

  g_bot_dbg.plan_mode = res.mode;
  if (res.has_path) {
    int n = res.path.n < SBOT_DBG_PLAN ? res.path.n : SBOT_DBG_PLAN;
    for (int i = 0; i < n; i++) {
      g_bot_dbg.plan_x[i] = res.path.x[i];
      g_bot_dbg.plan_y[i] = res.path.y[i];
    }
    g_bot_dbg.plan_n = n;
  }

  if (res.breakout) {
    B.goal = heading_abs(res.esc_ang);
    gdata->bot.output.accel = res.esc_boost;
    return;
  }

  float course = res.course;
  v2 goal_dir = {close_tang.x * cosf(course) - o * close_tang.y * sinf(course),
                 close_tang.y * cosf(course) + o * close_tang.x * sinf(course)};
  v2 goal = {B.x + goal_dir.x * 4.0f * B.width,
             B.y + goal_dir.y * 4.0f * B.width};
  if (!isfinite(goal.x) || !isfinite(goal.y)) return;

  /* react faster than the calm smoothing when the planner is steering
   * away from the geometry course */
  float blend = res.mode == 1 ? 0.5f : 0.25f;
  if (fabsf(goal.x - B.goal.x) < 1000.0f &&
      fabsf(goal.y - B.goal.y) < 1000.0f) {
    B.goal.x = roundf(goal.x * blend + B.goal.x * (1.0f - blend));
    B.goal.y = roundf(goal.y * blend + B.goal.y * (1.0f - blend));
  } else {
    B.goal = (v2){roundf(goal.x), roundf(goal.y)};
  }
}


/*
 * Circle mode (set SBOT_CIRCLE_MODE at build time, or edit here):
 *   0 = original circling logic, untouched
 *   1 = original logic drives; the planner only steps in when the
 *       original path is predicted to run into something (default)
 *   2 = planner drives all the time
 */
#ifndef SBOT_CIRCLE_MODE
#define SBOT_CIRCLE_MODE 1
#endif

static void follow_circle_hybrid(game_data* gdata) {
  g_bot_dbg.plan_n = 0;
  g_bot_dbg.plan_mode = 0;
  follow_circle_legacy(gdata);
  if (B.bpts_n < 3 || B.bpts_len < 9.0f * B.width) return;

  pl_setup(gdata);
  if (P.obs_n == 0 && P.heads_n == 0 && !P.wall_on) return;

  /* the course the original logic just chose, relative to our body */
  pl_geom geo;
  pl_compute_geom(&geo);
  float tang_ang = atan2f(geo.tang.y, geo.tang.x);
  float goal_ang = atan2f(B.goal.y - B.y, B.goal.x - B.x);
  float c_leg = (float)B.circle_dir * wrap_pi(goal_ang - tang_ang);
  if (!isfinite(c_leg)) return;
  c_leg = clampf(c_leg, PL_COURSE_MIN, PL_COURSE_MAX);

  pl_roll r;
  pl_roll_spine(c_leg, geo.close_t, geo.close_t, &r);
  if (r.fail >= PL_VETO_STEPS) return; /* original path is clear */

  follow_circle_planner(gdata);
}

static void follow_circle_self(game_data* gdata) {
  if (SBOT_CIRCLE_MODE == 0)
    follow_circle_legacy(gdata);
  else if (SBOT_CIRCLE_MODE == 2)
    follow_circle_planner(gdata);
  else
    follow_circle_hybrid(gdata);
}

static void to_circle(game_data* gdata) {
  if (!B.enable_follow_circle) return;
  int ns = tdarray_length(gdata->data.snakes);
  snake* me = NULL;
  for (int i = 0; i < ns; i++)
    if (gdata->data.snakes[i].id == B.id) {
      me = gdata->data.snakes + i;
      break;
    }
  if (!me) return;

  /*
   * Scan the WHOLE body for the head looping back onto it, not just
   * the first 20 non-dying points encountered. That arbitrary cap
   * only reliably finds the self-intersection on a small, regular
   * coil -- on anything else (a larger loop, an irregular shape, or
   * simply having eaten/grown since the loop started forming), the
   * actual crossing point can sit well past the 20th point, so it's
   * never found. B.stage then never advances to 2, and the bot is
   * left running the reactive, turn-by-turn avoidance in stage 1
   * (check_collision/check_encircle) indefinitely instead of
   * settling into the smooth, dedicated circle-following behavior
   * below -- which looks exactly like "boosting and running here
   * and there" instead of holding a steady loop.
   */
  int pn = tdarray_length(me->pts);
  for (int i = 0; i < pn; i++) {
    body_part* po = me->pts + i;
    if (po->dying) continue;
    circ tc = {po->xx, po->yy, B.radius};
    if (circle_intersect(B.head_circle, tc, NULL)) {
      B.stage = 2;
      return;
    }
  }
  gdata->bot.output.accel = (bool)B.default_accel;
  B.goal = heading_rel((B.circle_dir * (float)M_PI) / 32.0f);
}

static void compute_food_goal(game_data* gdata) {
  memset(B.food_angles_set, 0, sizeof(B.food_angles_set));
  int fn = tdarray_length(gdata->data.foods);
  for (int i = 0; i < fn; i++) {
    food* fo = gdata->data.foods + i;
    if (fo->eaten) continue;
    float fd2 = dist2(B.x, B.y, fo->xx, fo->yy);
    circ fc = {fo->xx, fo->yy, 2.0f};
    if (circle_intersect(fc, B.side_l, NULL)) continue;
    if (circle_intersect(fc, B.side_r, NULL)) continue;
    add_food_angle(fo->xx, fo->yy, fd2, fo->sz);
  }
  B.has_food = false;
  float best = -1.0f;
  for (int i = 0; i < MAXARC; i++) {
    if (!B.food_angles_set[i] || B.food_angles[i].sz <= 0.0f) continue;
    if (B.food_angles[i].score > best) {
      best = B.food_angles[i].score;
      B.current_food = B.food_angles[i];
      B.has_food = true;
    }
  }
}

static void delay_action(game_data* gdata) {
  if (B.delay_frame == -1) return;
  if (B.delay_frame > 0) {
    B.delay_frame--;
    return;
  }

  int ns = tdarray_length(gdata->data.snakes);
  bool playing = false;
  for (int i = 0; i < ns; i++)
    if (gdata->data.snakes[i].id == B.id && !gdata->data.snakes[i].dead) {
      playing = true;
      break;
    }

  if (playing) {
    if (B.stage == 0) {
      compute_food_goal(gdata);
      B.goal = B.has_food ? (v2){B.current_food.x, B.current_food.y}
                          : (v2){gdata->data.grd, gdata->data.grd};
    } else if (B.stage == 1) {
      to_circle(gdata);
    }
  }
  B.delay_frame = -1;
}

static int snake_score(game_data* gdata, snake* sk) {
  if (sk->sct < 0 || sk->fam < 0 || sk->rsc < 0) return 0;
  int sct = sk->sct + sk->rsc;
  return (int)(15.0f * (gdata->data.fpsls[sct] +
                        sk->fam / gdata->data.fmlts[sct] - 1) -
               5);
}

static void every(game_data* gdata) {
  int ns = tdarray_length(gdata->data.snakes);
  if (ns == 0) return;
  snake* me = NULL;
  for (int i = 0; i < ns; i++)
    if (gdata->data.snakes[i].id == B.id) {
      me = gdata->data.snakes + i;
      break;
    }
  if (!me) {
    me = gdata->data.snakes + (ns - 1);
    B.id = me->id;
    /* We only land here when we don't have a live snake to track yet --
     * bot just turned on, or the previous snake died and this is a
     * fresh one after respawn. Start at the food-chasing grow stage,
     * same as sbot_init -- the score check in sbot_go below is what
     * actually decides when to switch into loop-forming, so this just
     * needs to not be stage 2 (which would try to run circleControl
     * with no body to loop yet). */
    B.stage = 0;
  }

  B.x = me->xx;
  B.y = me->yy;
  B.ang = me->ang;
  B.ca = cosf(B.ang);
  B.sa = sinf(B.ang);
  B.speed_mult = me->sp / SPEED_BASE;
  B.width = snake_width(me->sc);
  B.radius = B.width / 2.0f;
  B.snake_len = snake_score(gdata, me);

  float spf = fminf(1.0f, B.speed_mult - 1.0f) * B.radius_mult;
  B.head_circle = (circ){B.x + (B.ca * spf / 2.0f) * B.radius,
                         B.y + (B.sa * spf / 2.0f) * B.radius,
                         (B.radius_mult / 2.0f) * B.radius};
  B.side_r = (circ){B.x - (B.y + B.sa * B.width - B.y),
                    B.y + (B.x + B.ca * B.width - B.x), B.width * B.speed_mult};
  B.side_l = (circ){B.x + (B.y + B.sa * B.width - B.y),
                    B.y - (B.x + B.ca * B.width - B.x), B.width * B.speed_mult};
}

void sbot_init(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;
  memset(&B, 0, sizeof(B));
  memset(&P, 0, sizeof(P));
  B.stage = 0;
  B.delay_frame = -1;
  B.default_accel = 0;
  B.circle_dir = +1;
  B.enable_encircle = true;
  B.enable_follow_circle = true;
  gdata->bot.output.accel = false;
  gdata->bot.output.xm = 0;
  gdata->bot.output.ym = 0;
}

void sbot_go(tenv* env) {
  tuser_data* usr = env->usr;
  user_settings* usrs = &usr->usrs;
  game_data* gdata = &usr->gdata;
  sbot* bot = &gdata->bot;
  B.radius_mult = usrs->bot_radius_mult;
  B.follow_circle_length = usrs->bot_follow_circle_score;

  g_bot_dbg.avoid_active   = false;
  g_bot_dbg.encircle_state = 0;

  if (tdarray_length(gdata->data.snakes) == 0) return;

  every(gdata);

  /* Below the configured score (usrs->bot_follow_circle_score,
   * B.follow_circle_length), stay in/return to the food-chasing grow
   * stage -- normal reactive play, no forced loop-forming. Once past
   * it, stage promotes to 1 (loop-forming) below and stays there via
   * every()'s respawn handling using stage 0 as the restart point, not
   * this check re-triggering mid-circle. */
  if (B.snake_len < B.follow_circle_length) B.stage = 0;
  if (B.has_food && B.stage != 0) B.has_food = false;

  if (B.stage == 2) {
    bot->output.accel = (bool)B.default_accel;
    follow_circle_self(gdata);
  } else {
    /* Score-gated stage promotion runs every frame, regardless of
     * whether a collision/encircle dodge is also active right now --
     * previously this was only checked on a fully "clear" frame, which
     * a persistently crowded area (lots of nearby bodies, not
     * necessarily an actual collision course) could prevent from ever
     * happening. That left the bot stuck reacting frame-to-frame with
     * no coherent plan, unable to even attempt to_circle() -- so an
     * already-formed loop was never recognized either. */
    if (B.snake_len > B.follow_circle_length) B.stage = 1;

    if (check_collision(gdata) || check_encircle(gdata)) {
      /*
       * Only start a fresh cooldown from idle (delay_frame == -1),
       * matching the same idiom the non-colliding path below already
       * uses -- don't keep refreshing an already-idle timer back to
       * -1 forever. This still gives an active dodge time to finish
       * (a countdown already in progress is left alone), but under
       * sustained/continuous collision or encircle triggering -- like
       * simply being in a crowded area -- it guarantees to_circle()
       * still gets a chance to run roughly every COLLISION_DELAY
       * frames instead of being starved indefinitely.
       */
      if (B.delay_frame == -1) B.delay_frame = COLLISION_DELAY;
    } else {
      if (B.delay_frame == -1) B.delay_frame = ACTION_FRAMES;
      bot->output.accel = (bool)B.default_accel;
    }
  }

  delay_action(gdata);

  bot->output.xm = (B.goal.x - gdata->data.view_xx) * gdata->data.gsc;
  bot->output.ym = (B.goal.y - gdata->data.view_yy) * gdata->data.gsc;

  g_bot_dbg.active        = true;
  g_bot_dbg.bx            = B.x;
  g_bot_dbg.by            = B.y;
  g_bot_dbg.brad          = B.radius;
  g_bot_dbg.head_cx       = B.head_circle.x;
  g_bot_dbg.head_cy       = B.head_circle.y;
  g_bot_dbg.head_cr       = B.head_circle.r;
  g_bot_dbg.has_food      = B.has_food;
  g_bot_dbg.food_x        = B.current_food.x;
  g_bot_dbg.food_y        = B.current_food.y;
  g_bot_dbg.goal_x        = B.goal.x;
  g_bot_dbg.goal_y        = B.goal.y;
  g_bot_dbg.stage         = B.stage;
  g_bot_dbg.accel         = bot->output.accel;
  g_bot_dbg.encircle_r1   = B.radius * (float)B.radius_mult;
  g_bot_dbg.encircle_r2   = B.radius * ENCIRCLE_DIST_MULT;
  for (int _i = 0; _i < 16; _i++) {
    g_bot_dbg.arc_coll[_i] = B.coll_angles_set[_i];
    g_bot_dbg.arc_food[_i] = B.food_angles_set[_i];
  }
  g_bot_dbg.coll_n = 0;
  for (int _i = 0; _i < B.coll_pts_n && _i < SBOT_DBG_MAX_COLL; _i++) {
    g_bot_dbg.coll_x[_i]    = B.coll_pts[_i].x;
    g_bot_dbg.coll_y[_i]    = B.coll_pts[_i].y;
    g_bot_dbg.coll_r[_i]    = B.coll_pts[_i].r;
    g_bot_dbg.coll_type[_i] = B.coll_pts[_i].type;
    g_bot_dbg.coll_d2[_i]   = B.coll_pts[_i].d2;
    g_bot_dbg.coll_n++;
  }
}

void sbot_destroy(tenv* env) { (void)env; }
