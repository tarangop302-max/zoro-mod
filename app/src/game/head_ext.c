#include "head_ext.h"

#include <math.h>
#include <string.h>

#include <thermite.h>

void head_ext_drop(snake* o) {
  if (!o->pts) return;
  int n = (int)tdarray_length(o->pts);
  while (n > 0 && o->pts[n - 1].virt) {
    tdarray_pop(o->pts);
    n--;
  }
}

void head_ext_update(snake* o) {
  if (o->dead || !o->pts) return;
  float msl = o->msl;
  if (msl <= 0.0f) return;
  int n = (int)tdarray_length(o->pts);
  if (n < 2) return;

  float hx = o->xx + o->fx;
  float hy = o->yy + o->fy;

  /* How many temporary points are already at the end? */
  int virt = 0;
  while (virt < n && o->pts[n - 1 - virt].virt) virt++;

  for (;;) {
    body_part* last = o->pts + (n - 1);
    float lx = last->xx + last->fx;
    float ly = last->yy + last->fy;
    float dx = hx - lx, dy = hy - ly;
    float d = sqrtf(dx * dx + dy * dy);
    if (d <= msl * 1.02f || virt >= HEAD_EXT_MAX) break;
    body_part vp;
    memset(&vp, 0, sizeof vp);
    vp.xx = lx + dx / d * msl;
    vp.yy = ly + dy / d * msl;
    vp.smu = 1.0f;
    vp.ltn = 1.0f;
    vp.iang = last->iang;
    vp.ebx = vp.xx - lx;
    vp.eby = vp.yy - ly;
    vp.iiv = last->iiv;
    vp.virt = true;
    tdarray_push(&o->pts, &vp); /* may reallocate: `last` is not used again */
    n++;
    virt++;
  }
}
