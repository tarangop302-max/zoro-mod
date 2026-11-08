#include <string.h>    
#ifdef ANDROID    
#include "../android_glfw_shim.h"    
#endif    
#include "input.h"    
#include "eyes_back.h"    
#include "lag_comp.h"
#include "steer_model.h"
    
#include "../user.h"    
#include "../ui/key_buttons.h"    
#include "global_chat.h"
    
/*
 * Steering prediction (server-true). The server only starts turning the snake
 * when our steering packet reaches it, and tells us about the turn a full round
 * trip later. lag_comp.c keeps a model of the real server heading: every byte
 * we send is logged here, every heading the server reports re-anchors the model
 * (callback.c) and the turns still in flight are replayed on top of it.
 *
 * The "Latency compensation" settings only choose WHEN a turn is shown:
 *   OFF            = the instant it is sent (feels instant, but the on-screen
 *                    head is ahead of the real one by one-way delay);
 *   ON, Turn delay = that share of the one-way delay after it is sent; 100% =
 *                    exactly when the server applies it, so the head on screen
 *                    is where the real head is.
 * Either way the model keeps the head and the server in step, so there is no
 * "ignore the server for a while, then snap" window any more.
 */

/* Called when a steering packet is sent: tell the model. */
static void steer_predict(game_data* gdata, snake* me, int sang,
                          double now_ms) {
#if STEER_PREDICT
  (void)gdata;
  if (me->dead) return;
  lag_comp_sent(now_ms, (float)sang * PI2 / 251.0f);
#else
  (void)gdata; (void)me; (void)sang; (void)now_ms;
#endif
}

/* Every frame: point my head at the turn the model says should be showing now. */
static void steer_apply_due(game_data* gdata, snake* me, double now_ms) {
#if STEER_PREDICT
  if (me->dead || !steer_model_on(gdata) || !lag_comp_ready()) return;
  float shown_ang, shown_wang;
  lag_comp_display(now_ms, steer_up_ms(gdata), steer_hold_ms(gdata),
                   steer_rate(gdata, me), &shown_ang, &shown_wang);
  me->wang = shown_wang;
  float vang = fmodf(shown_wang - me->ang, PI2);
  if (vang < 0) vang += PI2;
  if (vang > PI) vang -= PI2;
  me->dir = fabsf(vang) < 0.0005f ? 0 : (vang < 0 ? 1 : 2);
#else
  (void)gdata; (void)me; (void)now_ms;
#endif
}

void input(tenv* env) {    
  tuser_data* usr = env->usr;    
  tcontext* ctx = env->ctx;    
  game_data* gdata = &usr->gdata;    
  user_settings* usrs = &usr->usrs;    
  struct mg_connection* connection = gdata->connection;    
  gdata->data.lat_comp = user_settings_latency_comp(usrs);
  gdata->data.turn_delay_frac = user_settings_turn_delay_pct(usrs) / 100.0f;
    
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
    steer_apply_due(gdata, me, glfwGetTime() * 1000.0);
    
    if (!usrs->hotkeys[HOTKEY_BOT].active) {
      if (twindow_key_down(env->wnd, GLFW_KEY_LEFT))
        gdata->data.kd_l_frb += gdata->data.vfrb;
      if (twindow_key_down(env->wnd, GLFW_KEY_RIGHT))
        gdata->data.kd_r_frb += gdata->data.vfrb;

      if (gdata->data.kd_l_frb > 0 || gdata->data.kd_r_frb > 0)
        if (gdata->data.ctm - gdata->data.lkstm > NTL_KEY_BATCH_MS) {
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

            float ang = me->eang;
            float spawn_x = cx + NTL_SPAWN_R * cosf(ang);
            float spawn_y = cy + NTL_SPAWN_R * sinf(ang);

            gdata->touch_ctrl.tp_anchor_x = spawn_x;
            gdata->touch_ctrl.tp_anchor_y = spawn_y;
            gdata->touch_ctrl.tp_cursor_x = spawn_x;
            gdata->touch_ctrl.tp_cursor_y = spawn_y;
          }

          if (!(env->wnd->touch.just_down || !gdata->touch_ctrl.tp_tracking)) {

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
    } else if ((gdata->curr_screen == KEYBOARD_EDITOR ||
                gdata->curr_screen == HUD_LAYOUT_EDITOR) &&
               gdata->data.lsxm != -2147483647) {
      /* A layout editor (keyboard / HUD) is open mid-match: dragging buttons must
         not steer the snake, so keep the last direction that was sent. */
      xm = gdata->data.lsxm;
      ym = gdata->data.lsym;
    }
#ifdef ANDROID    
    
    gdata->data.wmd = (!gdata->settings_popup && !(gdata->curr_screen == KEYBOARD_EDITOR || gdata->curr_screen == HUD_LAYOUT_EDITOR) && env->wnd->touch.boost_down) || gdata->bot.output.accel;    
#else    
    /* While the settings popup is open, clicks belong to the popup and must
       not boost; the bot still boosts on its own. */    
    gdata->data.wmd = (!gdata->settings_popup &&    
                       !(gdata->curr_screen == KEYBOARD_EDITOR || gdata->curr_screen == HUD_LAYOUT_EDITOR) &&    
                       (twindow_button_down(env->wnd, GLFW_MOUSE_BUTTON_LEFT) ||    
                        twindow_key_down(env->wnd, GLFW_KEY_SPACE) ||    
                        twindow_key_down(env->wnd, GLFW_KEY_UP))) ||    
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
    
    /* Eyes back: send "behind the head" instead of the mouse angle (see    
       eyes_back.c). Not while the bot is driving or the snake is dead. */    
    static bool eb_was_on = false;    
    bool eb_on = usrs->hotkey_eyes_back.active &&    
                 !usrs->hotkeys[HOTKEY_BOT].active && !me->dead;    
    if (eb_was_on && !eb_on) {    
      /* Switched off: the server is still chasing the last "behind" angle,    
         so force the real steering angle out on the very next frame. */    
      gdata->data.lsxm = -2147483647;    
      gdata->data.lsang = -1;    
    }    
    if (eb_was_on != eb_on) lag_comp_reset(); /* the model's commands no longer match */
    eb_was_on = eb_on;    
    gdata->data.eb_eye_on = eb_on;    
    
    bool want_e = false;    
    if (xm != gdata->data.lsxm || ym != gdata->data.lsym) want_e = true;    
    /* Keep alternating even while the mouse is perfectly still. */    
    if (eb_on) want_e = true;    
    /* NTL sets snake.eang only when it sends a steering packet (below). */    
    float ang;    
    if (want_e &&    
        rt_gate(now_ms, gdata->data.rt_last_e_ms, STEER_ANGLE_MIN_MS)) {    
      want_e = false;    
      gdata->data.rt_last_e_ms = now_ms;    
      gdata->data.last_e_mtm = gdata->data.ctm;    
      gdata->data.lsxm = xm;    
      gdata->data.lsym = ym;    
      float d2 = xm * xm + ym * ym;    
      if (d2 > NTL_MOUSE_DEAD_D2) {    
        ang = atan2f(ym, xm);    
        me->eang = ang;    
      } else    
        ang = me->wang;    
      ang = fmodf(ang, PI2);    
      if (ang < 0) ang += PI2;    
      int sang = (int)floorf((250 + 1) * ang / PI2);    
      if (eb_on) sang = eyes_back_tick(gdata, me, d2 > NTL_MOUSE_DEAD_D2, ang, now_ms);    
      if (eb_on) gdata->data.eb_eye_ang = (float)sang * PI2 / 251.0f;    
      if (sang != gdata->data.lsang) {    
        gdata->data.lsang = sang;    
        mg_ws_send(connection, (uint8_t[]){sang & 255}, 1, WEBSOCKET_OP_BINARY);    
        /* No local turn prediction while Eyes back is on: the sent angle    
           alternates behind the head, and the head must follow the real    
           (echoed) server heading instead. */    
        if (!eb_on) steer_predict(gdata, me, sang, now_ms);    
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
    if (i == HOTKEY_OPEN_SETTINGS) continue; /* see input_settings_hotkey() */    
    if (i == HOTKEY_KEY_EDITOR) continue;    /* see input_key_editor_hotkey() */    
    if (i == HOTKEY_TEAM_CHAT) continue;     /* see input_team_chat_hotkey() */    
    hotkey* hk = usr_hotkey(usrs, i);    
    if (i == HOTKEY_EYES_BACK) {    
      /* Typing in the team chat must not toggle Eyes back. */    
      ImGuiIO* eb_io = igGetIO_Nil();    
      if (eb_io && eb_io->WantTextInput) {    
        if (hk->mode) hk->active = false;    
        continue;    
      }    
    }    
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

void input_settings_hotkey(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;
  hotkey* hk = &usrs->hotkey_open_settings;
  static bool prev_active = false;

  /* Don't fire while the player is typing (e.g. in the team chat box). */
  ImGuiIO* io = igGetIO_Nil();
  bool typing = io && io->WantTextInput;

  bool in_range = hk->key >= 0 && hk->key < 512;
  bool real_down    = !typing && twindow_key_down(env->wnd, hk->key);
  bool real_pressed = !typing && tkeyboard_key_pressed(env->kb, hk->key);
  bool fake_down    = !typing && in_range && gdata->data.fake_key_down[hk->key];
  bool fake_pressed = !typing && in_range && gdata->data.fake_key_pressed[hk->key];
  if (in_range) gdata->data.fake_key_pressed[hk->key] = false;

  if (hk->mode)
    hk->active = real_down || fake_down;
  else
    hk->active ^= (real_pressed || fake_pressed);

  /* Only available while bot mode is on: it can't be opened otherwise, and
     it closes if the bot is switched off. */
  if (!usrs->hotkeys[HOTKEY_BOT].active) hk->active = false;

  if (hk->active && !prev_active) {
    gdata->settings_popup = true;
  } else if (!hk->active && prev_active) {
    gdata->settings_popup = false;
  }
  prev_active = hk->active;
}

void input_key_editor_hotkey(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;
  hotkey* hk = &usrs->hotkey_key_editor;

  /* Only mid-match, never while typing (team chat, nickname...). */
  if (gdata->curr_screen != PLAYING || gdata->settings_popup) return;
  ImGuiIO* io = igGetIO_Nil();
  bool typing = io && io->WantTextInput;

  bool in_range = hk->key >= 0 && hk->key < 512;
  bool real_pressed = !typing && tkeyboard_key_pressed(env->kb, hk->key);
  bool fake_pressed = !typing && in_range && gdata->data.fake_key_pressed[hk->key];
  if (in_range) gdata->data.fake_key_pressed[hk->key] = false;

  hk->active = false;
  if (real_pressed || fake_pressed) ui_key_buttons_open_editor(env);
}

void input_team_chat_hotkey(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;
  hotkey* hk = &usrs->hotkey_team_chat;

  /* Mid-match only, and never while typing: letters typed into the chat box
     (or nickname etc.) must not toggle the window. */
  if (gdata->curr_screen != PLAYING || gdata->settings_popup) return;
  ImGuiIO* io = igGetIO_Nil();
  bool typing = io && io->WantTextInput;

  bool in_range = hk->key >= 0 && hk->key < 512;
  bool real_pressed = !typing && tkeyboard_key_pressed(env->kb, hk->key);
  bool fake_pressed = !typing && in_range && gdata->data.fake_key_pressed[hk->key];
  if (in_range) gdata->data.fake_key_pressed[hk->key] = false;

  hk->active = false;
  if (real_pressed || fake_pressed) global_chat_toggle_open();
}
