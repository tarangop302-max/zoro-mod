#include <string.h>    
#ifdef ANDROID    
#include "../android_glfw_shim.h"    
#endif    
#include "input.h"    
    
#include "../user.h"    
    
#if STEER_PREDICT
/* Short history of the steering angle (one entry per frame). The head is steered
 * from the entry that is STEER_PRED_LEAD_MS short of a full round trip old, i.e.
 * it shows your turn at most that much before the server really makes it. */
#define STEER_RING 128
static double s_ring_t[STEER_RING];
static int s_ring_a[STEER_RING];
static int s_ring_head = 0; /* next slot to write */
static int s_ring_n = 0;
static int s_ring_owner = -2;

static void steer_ring_push(double t, int a) {
  s_ring_t[s_ring_head] = t;
  s_ring_a[s_ring_head] = a;
  s_ring_head = (s_ring_head + 1) % STEER_RING;
  if (s_ring_n < STEER_RING) s_ring_n++;
}

/* Newest entry that is at least as old as t_cut. 0 if there is none yet. */
static int steer_ring_at(double t_cut, int* out) {
  for (int i = 1; i <= s_ring_n; i++) {
    int idx = (s_ring_head - i + STEER_RING) % STEER_RING;
    if (s_ring_t[idx] <= t_cut) {
      *out = s_ring_a[idx];
      return 1;
    }
  }
  return 0;
}
#endif

/*
 * Steering prediction. The server only starts turning the snake when our
 * steering packet reaches it, and we only see the turn when its echo comes
 * back -- a full round trip (plus the 33 ms send gate) before the head visibly
 * reacts to the arrow. So the moment a steering packet is sent we start the
 * same turn locally, at exactly the turn rate the server applies. The
 * server's echoes about our own heading are held back for a short window (see
 * callback.c) so they can't drag the head back to where it was a round trip
 * ago; once the window closes the normal smoothing reconciles any leftover.
 */
static void steer_predict(game_data* gdata, snake* me, int sang,
                          double now_ms) {
#if STEER_PREDICT
  if (me->dead) return;
  float target = (float)sang * PI2 / 251.0f;
  float vang = fmodf(target - me->ang, PI2);
  if (vang < 0) vang += PI2;
  if (vang > PI) vang -= PI2;
  if (fabsf(vang) < 0.0005f) return;
  me->wang = target;
  me->dir = vang < 0 ? 1 : 2;
  float window = 2.0f * gdata->data.owd_ms + 50.0f;
  if (window < STEER_PRED_MIN_WINDOW_MS) window = STEER_PRED_MIN_WINDOW_MS;
  if (window > STEER_PRED_MAX_WINDOW_MS) window = STEER_PRED_MAX_WINDOW_MS;
  gdata->data.steer_pred_until_ms = now_ms + window;
#else
  (void)gdata;
  (void)me;
  (void)sang;
  (void)now_ms;
#endif
}

void input(tenv* env) {    
  tuser_data* usr = env->usr;    
  tcontext* ctx = env->ctx;    
  game_data* gdata = &usr->gdata;    
  user_settings* usrs = &usr->usrs;    
  struct mg_connection* connection = gdata->connection;    
    
  if (!gdata->data.wfpr) {    
    if (gdata->data.ctm - gdata->data.last_ping_mtm > 250) {    
      gdata->data.last_ping_mtm = gdata->data.ctm;    
      gdata->data.wfpr = true;    
      /* Real send time, so the pong measures true round-trip ms instead    
         of a value rounded to whole frames. */    
      gdata->data.rt_ping_sent_ms = glfwGetTime() * 1000.0;    
      mg_ws_send(connection, (uint8_t[]){251}, 1, WEBSOCKET_OP_BINARY);    
    }    
  }    
    
  if (gdata->data.follow_view) {    
    int xm;    
    int ym;    
    
    int snakes_len = tdarray_length(gdata->data.snakes);    
    snake* me = gdata->data.snakes + (snakes_len - 1);    
    
    if (!usrs->hotkeys[HOTKEY_BOT].active) {
      if (twindow_key_down(env->wnd, GLFW_KEY_LEFT))
        gdata->data.kd_l_frb += gdata->data.vfrb;
      if (twindow_key_down(env->wnd, GLFW_KEY_RIGHT))
        gdata->data.kd_r_frb += gdata->data.vfrb;

      if (gdata->data.kd_l_frb > 0 || gdata->data.kd_r_frb > 0)
        if (gdata->data.ctm - gdata->data.lkstm > 150) {
          gdata->data.lkstm = gdata->data.ctm;
          if (gdata->data.kd_r_frb > 0)
            if (gdata->data.kd_l_frb > gdata->data.kd_r_frb) {
              gdata->data.kd_l_frb -= gdata->data.kd_r_frb;
              gdata->data.kd_r_frb = 0;
            }
          if (gdata->data.kd_l_frb > 0)
            if (gdata->data.kd_r_frb > gdata->data.kd_l_frb) {
              gdata->data.kd_r_frb -= gdata->data.kd_l_frb;
              gdata->data.kd_l_frb = 0;
            }
          if (gdata->data.kd_l_frb > 0) {
            int v = gdata->data.kd_l_frb;
            if (v > 127) v = 127;
            gdata->data.kd_l_frb -= v;
            me->eang -= gdata->data.mamu * v * me->scang * me->spang;
            mg_ws_send(connection, (uint8_t[]){252, (uint8_t)v}, 2,
                       WEBSOCKET_OP_BINARY);
          } else if (gdata->data.kd_r_frb > 0) {
            int v = gdata->data.kd_r_frb;
            if (v > 127) v = 127;
            gdata->data.kd_r_frb -= v;
            me->eang += gdata->data.mamu * v * me->scang * me->spang;
            v += 128;
            mg_ws_send(connection, (uint8_t[]){252, (uint8_t)v}, 2,
                       WEBSOCKET_OP_BINARY);
          }
        }
    }

    /*
     * Touch/joystick/mouse tracking always runs now, bot mode or not --
     * previously this whole block was skipped while the bot was active,
     * which is why the arrow used to freeze wherever it was and
     * dragging it did nothing: tp_cursor_x/y simply never updated. Now
     * it keeps updating live, so the arrow follows your finger and is
     * already aimed correctly the instant you turn the bot off. The
     * bot.output override below (not this block) is what keeps that
     * tracking from actually steering the snake while the bot is on.
     */
#ifdef ANDROID

      /*
       * The touch state was last sampled at the top of the frame, BEFORE the
       * vsync wait inside tcontext_begin(). Under vsync that wait is most of a
       * frame, so the finger position used to steer was often 8-16 ms stale.
       * Drain the input queue again right now so the arrow and the boost
       * button use where the finger is at this instant.
       */
      twindow_pump_input(env->wnd);

      float tx = env->wnd->touch.x;
      float ty = env->wnd->touch.y;

      if (usrs->ctrl_mode_trackpad) {

        #define NTL_FORBIDDEN_R  23.0f
        #define NTL_SPAWN_R      44.0f
        #define NTL_VEL_DECAY    0.85f
        #define NTL_VEL_WEIGHT   0.15f

        float sw = (float)ctx->size[0];
        float sh = (float)ctx->size[1];
        float cx = sw * 0.5f;
        float cy = sh * 0.5f;

        bool touch_down = env->wnd->touch.down;

        if (touch_down) {

          if (env->wnd->touch.just_down || !gdata->touch_ctrl.tp_tracking) {

            gdata->touch_ctrl.tp_tracking     = true;
            gdata->touch_ctrl.tp_visible      = true;
            gdata->touch_ctrl.tp_last_touch_x = tx;
            gdata->touch_ctrl.tp_last_touch_y = ty;
            gdata->touch_ctrl.tp_touch_down_x = tx;
            gdata->touch_ctrl.tp_touch_down_y = ty;
            gdata->touch_ctrl.tp_vx           = 0.0f;
            gdata->touch_ctrl.tp_vy           = 0.0f;

            if (usrs->ctrl_trackpad_direct) {
              /* Direct mode: don't fall back to the old heading-based
               * placement even on this very first frame. At the instant
               * of touch-down there's no drag yet (finger == anchor), so
               * this should read as "no direction input" -- but it must
               * NOT be an exact (0,0) offset from center: me->eang =
               * atan2f(ym, xm) runs unconditionally every frame further
               * down, and atan2f(0,0) snaps to angle 0 (due east) for
               * that one frame, which visibly glitches the rendered
               * heading for a frame before the real heading returns.
               * A 1px nudge would round back to (0,0) once xm/ym get
               * cast to int downstream if the heading is near a diagonal
               * (e.g. ~45 degrees splits it ~0.7px/~0.7px, each
               * truncating to 0) -- so use 2px, which survives that
               * worst case while still safely under the dead-zone
               * threshold (so nothing gets sent to the server yet).
               * The very next frame's dead-zone check below takes over
               * once you actually start dragging. */
              gdata->touch_ctrl.tp_cursor_x = cx + 2.0f * cosf(me->eang);
              gdata->touch_ctrl.tp_cursor_y = cy + 2.0f * sinf(me->eang);
            } else {
              float ang = me->eang;
              float spawn_x = cx + NTL_SPAWN_R * cosf(ang);
              float spawn_y = cy + NTL_SPAWN_R * sinf(ang);

              gdata->touch_ctrl.tp_anchor_x = spawn_x;
              gdata->touch_ctrl.tp_anchor_y = spawn_y;
              gdata->touch_ctrl.tp_cursor_x = spawn_x;
              gdata->touch_ctrl.tp_cursor_y = spawn_y;
            }
          }

          if (usrs->ctrl_trackpad_direct) {

            /* Direct/instant mode: the arrow points straight from the
             * touch-down point to wherever your finger is right now --
             * no accumulated per-frame deltas, no velocity/momentum.
             * This mirrors Vlither Enhanced's absolute arrow-steering,
             * so the snake reorients as fast as you move your finger.
             * Runs on every frame -- including the first -- so there's
             * no leftover-heading placement before you start dragging. */
            #define NTL_DIRECT_DEAD_R 6.0f

            float fdx  = tx - gdata->touch_ctrl.tp_touch_down_x;
            float fdy  = ty - gdata->touch_ctrl.tp_touch_down_y;
            float dist = sqrtf(fdx * fdx + fdy * fdy);

            if (dist > NTL_DIRECT_DEAD_R) {
              float ux = fdx / dist;
              float uy = fdy / dist;
              gdata->touch_ctrl.tp_cursor_x = cx + ux * NTL_SPAWN_R;
              gdata->touch_ctrl.tp_cursor_y = cy + uy * NTL_SPAWN_R;
            }
            /* Below the dead zone: keep pointing the last direction
             * instead of snapping to zero, so it doesn't jitter right
             * at the touch-down point. */

            gdata->touch_ctrl.tp_vx = 0.0f;
            gdata->touch_ctrl.tp_vy = 0.0f;
          } else if (!(env->wnd->touch.just_down || !gdata->touch_ctrl.tp_tracking)) {

            float nx = gdata->touch_ctrl.tp_anchor_x

                     + (tx - gdata->touch_ctrl.tp_last_touch_x) * usrs->arrow_sensitivity;
            float ny = gdata->touch_ctrl.tp_anchor_y
                     + (ty - gdata->touch_ctrl.tp_last_touch_y) * usrs->arrow_sensitivity;

            nx = GLM_MAX(0.0f, GLM_MIN(sw, nx));
            ny = GLM_MAX(0.0f, GLM_MIN(sh, ny));

            float fdx  = nx - cx;
            float fdy  = ny - cy;
            float dist = sqrtf(fdx * fdx + fdy * fdy);
            if (dist < NTL_FORBIDDEN_R && dist > 0.001f) {
              float a = atan2f(fdy, fdx);
              nx = cx + cosf(a) * NTL_FORBIDDEN_R;
              ny = cy + sinf(a) * NTL_FORBIDDEN_R;
            }

            float mdx = nx - gdata->touch_ctrl.tp_cursor_x;
            float mdy = ny - gdata->touch_ctrl.tp_cursor_y;
            gdata->touch_ctrl.tp_vx =
                gdata->touch_ctrl.tp_vx * NTL_VEL_DECAY + mdx * NTL_VEL_WEIGHT;
            gdata->touch_ctrl.tp_vy =
                gdata->touch_ctrl.tp_vy * NTL_VEL_DECAY + mdy * NTL_VEL_WEIGHT;

            gdata->touch_ctrl.tp_cursor_x = nx;
            gdata->touch_ctrl.tp_cursor_y = ny;
          }

          gdata->touch_ctrl.tp_cursor_angle_deg =
              atan2f(cy - gdata->touch_ctrl.tp_cursor_y,
                     cx  - gdata->touch_ctrl.tp_cursor_x) *
              (180.0f / PI);

          xm = (int)(gdata->touch_ctrl.tp_cursor_x - cx);
          ym = (int)(gdata->touch_ctrl.tp_cursor_y - cy);

        } else {

          if (gdata->touch_ctrl.tp_tracking) {
            gdata->touch_ctrl.tp_disappear_angle =
                atan2f(gdata->touch_ctrl.tp_cursor_y - cy,
                       gdata->touch_ctrl.tp_cursor_x - cx);
            gdata->touch_ctrl.tp_tracking = false;
            gdata->touch_ctrl.tp_visible  = false;
          }

          xm = (int)(gdata->touch_ctrl.tp_cursor_x - cx);
          ym = (int)(gdata->touch_ctrl.tp_cursor_y - cy);
        }

      } else {

        if (env->wnd->touch.down) {
          /* Match Slither mobile's joystick mode: the base is fixed at the
             on-screen ring (same jr/jcx/jcy the ring is drawn at in
             ui_overlay.c) and the touch's angle from that base steers the
             snake. The old code anchored to wherever the finger first
             touched down -- anywhere on screen, not necessarily on the
             ring -- and scaled the raw pixel offset from there, so the
             visible ring and the actual steering input were unrelated to
             each other. */
          float sw     = (float)ctx->size[0];
          float sh     = (float)ctx->size[1];
          float margin = sw * 0.025f;
          float jr, jcx, jcy;
          if (usrs->joy_pos_custom) {
            jr  = sh * usrs->joy_rel_size;
            jcx = sw * usrs->joy_rel_x;
            jcy = sh * usrs->joy_rel_y;
          } else {
            jr  = sh * 0.175f;
            jcx = usrs->ctrl_swap_sides ? (sw - jr - margin) : (jr + margin);
            jcy = sh - jr - margin;
          }

          float dx = tx - jcx;
          float dy = ty - jcy;
          if (dx * dx + dy * dy > 0.0001f) {
            gdata->touch_ctrl.joy_angle = atan2f(dy, dx);
            gdata->touch_ctrl.joy_has_direction = true;
          }
          gdata->touch_ctrl.joy_tracking = true;

          float steer_len = GLM_MAX(256.0f, jr * 4.0f);
          xm = (int)(cosf(gdata->touch_ctrl.joy_angle) * steer_len);
          ym = (int)(sinf(gdata->touch_ctrl.joy_angle) * steer_len);

          gdata->touch_ctrl.joy_last_xm = xm;
          gdata->touch_ctrl.joy_last_ym = ym;
        } else {
          if (!env->wnd->touch.down) gdata->touch_ctrl.joy_tracking = false;

          xm = gdata->touch_ctrl.joy_last_xm;
          ym = gdata->touch_ctrl.joy_last_ym;
        }
      }
#else
      xm = (int)env->ms->pos[0] - ctx->size[0] / 2;
      ym = (int)env->ms->pos[1] - ctx->size[1] / 2;
#endif

    /*
     * Only place bot mode actually overrides movement: whatever the
     * tracking above just computed from your finger is discarded here
     * and replaced with the bot's own decision, so dragging the arrow
     * around never nudges the snake while the bot is driving. Turn the
     * bot off and the very next frame uses your already-updated cursor
     * position with no lag.
     */
    if (usrs->hotkeys[HOTKEY_BOT].active) {
      xm = gdata->bot.output.xm;
      ym = gdata->bot.output.ym;
    }
#ifdef ANDROID    
    
    gdata->data.wmd = env->wnd->touch.boost_down || gdata->bot.output.accel;    
#else    
    gdata->data.wmd = twindow_button_down(env->wnd, GLFW_MOUSE_BUTTON_LEFT) ||    
                      twindow_key_down(env->wnd, GLFW_KEY_SPACE) ||    
                      twindow_key_down(env->wnd, GLFW_KEY_UP) ||    
                      gdata->bot.output.accel;    
#endif    
    
    double now_ms = glfwGetTime() * 1000.0;    
    if (gdata->data.md != gdata->data.wmd &&    
        rt_gate(now_ms, gdata->data.rt_last_accel_ms, STEER_BOOST_MIN_MS)) {    
      gdata->data.md = gdata->data.wmd;    
      gdata->data.rt_last_accel_ms = now_ms;    
      gdata->data.last_accel_mtm = gdata->data.ctm;    
      mg_ws_send(connection, (uint8_t[]){gdata->data.md ? 253 : 254}, 1,    
                 WEBSOCKET_OP_BINARY);    
    }    
    
#if STEER_PREDICT
    /* Steer the head locally EVERY frame, not only on the frames a packet goes
       out (every ~20-30 ms): the head used to be given a new target only at send
       time, reached it within a frame or two and then sat still until the next
       send -- the turn / stop / turn / stop. The target is the finger angle from
       (round trip - STEER_PRED_LEAD_MS) ago, quantised like the packet (251
       steps), so the head turns shortly before the server does and not a whole
       round trip before it. */
    if (me->id != s_ring_owner) {
      s_ring_owner = me->id;
      s_ring_n = 0;
    }
    if ((float)xm * (float)xm + (float)ym * (float)ym > 256.0f) {
      float la = fmodf(atan2f((float)ym, (float)xm), PI2);
      if (la < 0) la += PI2;
      int lsang_now = (int)floorf((250 + 1) * la / PI2);
      if (lsang_now > 250) lsang_now = 250;
      steer_ring_push(now_ms, lsang_now);
      double d_ms = 2.0 * gdata->data.owd_ms + 10.0 - STEER_PRED_LEAD_MS;
      if (d_ms < 0.0) d_ms = 0.0;
      if (d_ms > STEER_PRED_MAX_WINDOW_MS) d_ms = STEER_PRED_MAX_WINDOW_MS;
      int a_delayed;
      if (steer_ring_at(now_ms - d_ms, &a_delayed))
        steer_predict(gdata, me, a_delayed, now_ms);
    }
#endif

    bool want_e = false;    
    if (xm != gdata->data.lsxm || ym != gdata->data.lsym) want_e = true;    
    me->eang = atan2f(ym, xm);    
    float ang;    
    if (want_e &&    
        rt_gate(now_ms, gdata->data.rt_last_e_ms, STEER_ANGLE_MIN_MS)) {    
      want_e = false;    
      gdata->data.rt_last_e_ms = now_ms;    
      gdata->data.last_e_mtm = gdata->data.ctm;    
      gdata->data.lsxm = xm;    
      gdata->data.lsym = ym;    
      float d2 = xm * xm + ym * ym;    
      if (d2 > 256) {    
        ang = atan2f(ym, xm);    
        me->eang = ang;    
      } else    
        ang = me->wang;    
      ang = fmodf(ang, PI2);    
      if (ang < 0) ang += PI2;    
      int sang = (int)floorf((250 + 1) * ang / PI2);    
      if (sang != gdata->data.lsang) {    
        gdata->data.lsang = sang;    
        mg_ws_send(connection, (uint8_t[]){sang & 255}, 1, WEBSOCKET_OP_BINARY);    
      }    
    }    
  }    
    
  gdata->data.ms_zoom *= expf(env->ms->dwheel * usrs->zoom_step);    
    
  if (tkeyboard_key_pressed(env->kb, GLFW_KEY_N) ||    
      (GLFW_KEY_N < 512 && gdata->data.fake_key_pressed[GLFW_KEY_N]))    
    gdata->data.ms_zoom *= expf(1 * usrs->zoom_step);    
  else if (tkeyboard_key_pressed(env->kb, GLFW_KEY_M) ||    
           (GLFW_KEY_M < 512 && gdata->data.fake_key_pressed[GLFW_KEY_M]))    
    gdata->data.ms_zoom *= expf(-1 * usrs->zoom_step);    
    
  gdata->data.ms_zoom =    
      GLM_MAX(MAX_ZOOM_OUT, GLM_MIN(gdata->data.ms_zoom, MAX_ZOOM_IN));    
    
  usrs->hotkeys[HOTKEY_RESTART].active = false;    
  usrs->hotkeys[HOTKEY_QUIT].active = false;    
    
  for (int i = 0; i < NUM_HOTKEYS; i++) {    
    hotkey* hk = usrs->hotkeys + i;    
    bool real_down    = twindow_key_down(env->wnd, hk->key);    
    bool real_pressed = tkeyboard_key_pressed(env->kb, hk->key);    
    bool fake_down    = (hk->key >= 0 && hk->key < 512) &&    
                        gdata->data.fake_key_down[hk->key];    
    bool fake_pressed = (hk->key >= 0 && hk->key < 512) &&    
                        gdata->data.fake_key_pressed[hk->key];    
    if (hk->mode)    
      hk->active = real_down || fake_down;    
    else    
      hk->active ^= (real_pressed || fake_pressed);    
  }    
    
  memset(gdata->data.fake_key_pressed, 0,    
         sizeof(gdata->data.fake_key_pressed));    
    
  if (gdata->data.follow_view) {    
    snake* me = gdata->data.snakes + (tdarray_length(gdata->data.snakes) - 1);    
    int score = (int)floorf((gdata->data.fpsls[me->sct] +    
                             me->fam / gdata->data.fmlts[me->sct] - 1) *    
                                15 -    
                            5) /    
                1;    
    if (score >= 1000) {    
      usrs->hotkeys[HOTKEY_RESTART].active = false;    
    }    
  }    
    
  gameplay_mode* mode = usrs->modes + usrs->hotkeys[HOTKEY_ASSIST].active;    
  if (mode->show_crosshair) igSetMouseCursor(ImGuiMouseCursor_None);    
}