#include "ntl_bot_adapter.h"
#include "ntl_bot_core.h"

#include "../user.h"

#include <math.h>
#include <string.h>

/* Bridges the NTL bot's decision-making core (ntl_bot_core.c/h, copied
 * unmodified from https://github.com/disis-om/NTL-bot-script) to this
 * game's real snake/food/world state, and converts its decision back
 * into the exact gdata->bot.output.xm/ym/accel contract sbot_go()
 * already produces -- so nothing downstream needs to change to use it.
 *
 * Fixed, capped scratch buffers are used instead of per-tick heap
 * allocation (matching sbot.c's own style). Caps are generous but
 * bounded, so an unusually long snake or a very crowded server can
 * never overflow them -- anything past the cap is simply left out of
 * that tick's decision, which only affects bot quality, never safety.
 */

#define NTLA_MAX_ENEMIES 96
#define NTLA_MAX_SEGS_PER_SNAKE 1500
#define NTLA_MAX_FOOD 2048

static NtlSegment s_self_segs[NTLA_MAX_SEGS_PER_SNAKE];
static NtlSegment s_enemy_segs[NTLA_MAX_ENEMIES][NTLA_MAX_SEGS_PER_SNAKE];
static NtlSnakeView s_enemies[NTLA_MAX_ENEMIES];
static NtlFoodView s_foods[NTLA_MAX_FOOD];

static int fill_segments(body_part* pts, NtlSegment* out, int cap) {
  int n = (int)tdarray_length(pts);
  if (n > cap) n = cap;
  for (int i = 0; i < n; i++) {
    body_part* p = pts + i;
    out[i].x = p->xx;
    out[i].y = p->yy;
    out[i].fx = 0.0f;
    out[i].fy = 0.0f;
    out[i].dying = p->dying ? 1 : 0;
  }
  return n;
}

void ntl_bot_adapter_go(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;

  int ns = (int)tdarray_length(gdata->data.snakes);
  if (ns == 0) return;

  /* Same "our snake is the last one in the list" convention input.c
   * already relies on -- sbot.c tracks its own id privately, which
   * isn't exposed outside sbot.c, so we can't reuse that directly. */
  snake* me = gdata->data.snakes + (ns - 1);
  if (me->dead) return;

  NtlSnakeView self;
  memset(&self, 0, sizeof(self));
  self.id = me->id;
  self.x = me->xx;
  self.y = me->yy;
  self.heading = me->ang;
  self.speed = me->sp;
  self.mass = me->sc;
  self.length_score = me->fam;
  self.segments = s_self_segs;
  self.segment_count =
      (size_t)fill_segments(me->pts, s_self_segs, NTLA_MAX_SEGS_PER_SNAKE);
  self.alive = 1;

  int enemy_n = 0;
  for (int i = 0; i < ns && enemy_n < NTLA_MAX_ENEMIES; i++) {
    snake* s = gdata->data.snakes + i;
    if (s->id == self.id || s->dead) continue;
    NtlSnakeView* ev = s_enemies + enemy_n;
    memset(ev, 0, sizeof(*ev));
    ev->id = s->id;
    ev->x = s->xx;
    ev->y = s->yy;
    ev->heading = s->ang;
    ev->speed = s->sp;
    ev->mass = s->sc;
    ev->length_score = s->fam;
    ev->segments = s_enemy_segs[enemy_n];
    ev->segment_count = (size_t)fill_segments(s->pts, s_enemy_segs[enemy_n],
                                               NTLA_MAX_SEGS_PER_SNAKE);
    ev->alive = 1;
    enemy_n++;
  }

  int food_n = 0;
  int fn = (int)tdarray_length(gdata->data.foods);
  for (int i = 0; i < fn && food_n < NTLA_MAX_FOOD; i++) {
    food* f = gdata->data.foods + i;
    if (f->eaten) continue;
    s_foods[food_n].x = f->xx;
    s_foods[food_n].y = f->yy;
    s_foods[food_n].mass = f->sz;
    s_foods[food_n].eaten = 0;
    food_n++;
  }

  NtlWorldView world;
  memset(&world, 0, sizeof(world));
  world.center.x = gdata->data.grd;
  world.center.y = gdata->data.grd;
  world.center_heading = 0.0f;
  world.radius = gdata->data.flux_grd;
  world.enemy_snakes = s_enemies;
  world.enemy_count = (size_t)enemy_n;
  world.foods = s_foods;
  world.food_count = (size_t)food_n;

  NtlBotConfig cfg;
  ntl_bot_default_config(&cfg);

  NtlBotDecision decision;
  ntl_bot_update(&world, &self, &cfg, &decision);

  /* Same convention sbot.c itself uses for bot->output.xm/ym: target
   * world position relative to the camera view position, scaled by
   * the game's world-to-screen scale factor -- not a synthetic
   * angle-based guess, so it matches exactly what input.c expects. */
  gdata->bot.output.xm =
      (decision.target.x - gdata->data.view_xx) * gdata->data.gsc;
  gdata->bot.output.ym =
      (decision.target.y - gdata->data.view_yy) * gdata->data.gsc;
  gdata->bot.output.accel = decision.boost != 0;
}
