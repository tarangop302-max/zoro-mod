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

/* Once the bot commits to circling its own body, don't let a single
 * frame's differing decision (e.g. food briefly crossing in front of
 * the path, or a borderline occupancy reading) kick it back out to
 * food/recenter mode -- that's what produced the "circles then
 * immediately darts off" behavior. Hold the circle for at least this
 * long before allowing a *non-danger* mode switch away from it.
 * Genuine danger (EVADE) always overrides this immediately -- safety
 * is never debounced. */
#define NTLA_CIRCLE_MIN_HOLD_MS 800.0

static NtlSegment s_self_segs[NTLA_MAX_SEGS_PER_SNAKE];
static NtlSegment s_enemy_segs[NTLA_MAX_ENEMIES][NTLA_MAX_SEGS_PER_SNAKE];
static NtlSnakeView s_enemies[NTLA_MAX_ENEMIES];
static NtlFoodView s_foods[NTLA_MAX_FOOD];

static NtlBotMode s_committed_mode = NTL_BOT_MODE_RECENTER;
static double s_mode_start_ms = 0.0;
static NtlVec2 s_last_circle_target = {0.0f, 0.0f};
static float s_last_circle_angle = 0.0f;
static int s_have_circle_target = 0;

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
  user_settings* usrs = &usr->usrs;

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

  /* "Bot circle after score" is entered as the same *displayed* score
   * the UI shows, but the bot compares against the raw fam accumulator
   * (self.length_score above). Convert using the exact inverse of the
   * formula the game itself uses to derive displayed score from fam
   * (see input.c's own score computation), so the slider means what it
   * says regardless of which skin/category multiplier is active. */
  {
    float fpsl = gdata->data.fpsls[me->sct];
    float fmlt = gdata->data.fmlts[me->sct];
    float displayed_threshold = (float)usrs->bot_follow_circle_score;
    cfg.circle_length_threshold =
        fmlt * ((displayed_threshold + 5.0f) / 15.0f - fpsl + 1.0f);
  }

  /* "Bot radius multiplier" previously only drove sbot.c's own circling
   * math. Scale NTL's own offset distance proportionally around its
   * built-in default (1.25 at the settings' own default of 20x), so
   * moving the slider still meaningfully widens/tightens the circle. */
  cfg.body_follow_offset = 1.25f * ((float)usrs->bot_radius_mult / 20.0f);

  NtlBotDecision decision;
  ntl_bot_update(&world, &self, &cfg, &decision);

  if (decision.mode == NTL_BOT_MODE_BODY_FOLLOW) {
    s_last_circle_target = decision.target;
    s_last_circle_angle = decision.aim_angle;
    s_have_circle_target = 1;
  }

  NtlVec2 out_target = decision.target;
  float out_angle = decision.aim_angle;
  int out_boost = decision.boost;

  if (decision.mode != s_committed_mode) {
    int allow_switch = 1;
    if (s_committed_mode == NTL_BOT_MODE_BODY_FOLLOW &&
        decision.mode != NTL_BOT_MODE_EVADE &&
        (gdata->data.ctm - s_mode_start_ms) < NTLA_CIRCLE_MIN_HOLD_MS) {
      allow_switch = 0;
    }
    if (allow_switch) {
      s_committed_mode = decision.mode;
      s_mode_start_ms = gdata->data.ctm;
    } else if (s_have_circle_target) {
      /* Still within the hold window: keep steering toward the last
       * good circling point instead of flickering out. */
      out_target = s_last_circle_target;
      out_angle = s_last_circle_angle;
      out_boost = 0;
    }
  }

  /* Same convention sbot.c itself uses for bot->output.xm/ym: target
   * world position relative to the camera view position, scaled by
   * the game's world-to-screen scale factor -- not a synthetic
   * angle-based guess, so it matches exactly what input.c expects. */
  gdata->bot.output.xm = (out_target.x - gdata->data.view_xx) * gdata->data.gsc;
  gdata->bot.output.ym = (out_target.y - gdata->data.view_yy) * gdata->data.gsc;
  gdata->bot.output.accel = out_boost != 0;
  (void)out_angle;
}
