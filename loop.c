#ifdef ANDROID
#include "../android_glfw_shim.h"
#include <android/log.h>
#define DLOG(fmt,...) do{char _b[256];snprintf(_b,sizeof(_b),fmt,##__VA_ARGS__);    __android_log_print(ANDROID_LOG_ERROR,"vlither","%s",_b);}while(0)
#else
#define DLOG(fmt,...) do{}while(0)
#endif
#include "loop.h"

#include "../network/server.h"
#include "../user.h"
#include "death_screen.h"
#include "screenshot.h"
#include "recorder.h"
#include "input.h"
#include "kill_feed.h"
#include "oef.h"
#include "redraw.h"
#include "ui_overlay.h"

void game_loop(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;

  if (!env->config.running) gdata->conn = DISCONNECTED;

  switch (gdata->conn) {
    case CONNECTING: {
      usr->r->global.bg_opacity = 0;
      usr->r->global.bd_opacity = 0;
      usr->r->global.minimap_opacity = 0;

      if (glfwGetTime() > TIMEOUT) {
        gdata->connection->is_closing = true;
        DLOG("TIMEOUT: glfwGetTime()=%.2f > %d", glfwGetTime(), TIMEOUT);
        printf("Connection timed out.");
      }

      server_poll(env);

      vec2 loading_bar = {500, 12};
      igSetCursorPosX(ctx->size[0] * 0.5f - loading_bar[0] * 0.5f);
      igSetCursorPosY(ctx->size[1] * 0.5f - loading_bar[1] * 0.5f);

      igPushStyleColor_Vec4(ImGuiCol_PlotHistogram,
                            (ImVec4){0.168f, 0.668f, 0.375f, 1});
      igProgressBar(-glfwGetTime(), (ImVec2){loading_bar[0], loading_bar[1]},
                    NULL);
      igPopStyleColor(1);

      if (gdata->closed) {
        gdata->conn = DISCONNECTED;
        gdata->closed = false;
      }
      break;
    }
    case CONNECTED:
      time_step(env);

      // Movement/touch input is deliberately skipped for the entire
      // post-death window (delay + popup) below -- there's no snake left
      // to move, and more importantly, this game's own touch handling
      // doesn't check ImGui's io->WantCaptureMouse before acting, so
      // without this gate a tap on the popup's Lobby/Restart buttons was
      // also being read as a movement command underneath them, and the
      // buttons never visibly responded to clicks.
      if (!gdata->death_pending) {
        input(env);
      }

      server_poll(env);
      oef(env);
      redraw(env);
      ui_overlay(env);
      /* Detects new kills, shows the "+1 Kill" toast and queues the kill
         screenshot (screenshot_request). Was never being called, so no
         kill was ever captured and the death popup / gallery had nothing
         to show. */
      kill_feed_draw(env);
      recorder_update();
      recorder_button_draw(env);

      if (!gdata->death_pending) {
        if (usrs->hotkeys[HOTKEY_QUIT].active ||
            (usrs->quit_mc &&
             tmouse_button_pressed(env->ms, GLFW_MOUSE_BUTTON_MIDDLE))) {
          gdata->connection->is_closing = true;
        } else if (usrs->hotkeys[HOTKEY_RESTART].active ||
                   (usrs->restart_rc &&
                    tmouse_button_pressed(env->ms, GLFW_MOUSE_BUTTON_RIGHT))) {
          gdata->connection->is_closing = true;
          gdata->restart_req = true;
        }
      } else if (glfwGetTime() - gdata->death_anim_start >=
                 DEATH_ANIM_SECONDS) {
        // Popup only appears once the short fixed delay above has
        // elapsed -- until then the player just sees their own death
        // play out normally (existing fade/spectator behavior) instead
        // of a screen that suddenly looks frozen.
        ui_death_screen(env);
      }

      // While death_pending is true (from the moment of death until the
      // player actually clicks Lobby/Restart), a server-initiated close
      // is deliberately NOT acted on here -- that's exactly what used to
      // silently dump the player back to the lobby on the server's own
      // timing, even with the popup up and without them clicking
      // anything. gdata->closed just stays true and gets handled the
      // instant death_pending flips back to false (see the button
      // handlers in death_screen.c), whether that happens because the
      // connection had already closed by then or closes right after.
      if (gdata->closed && !gdata->death_pending) {
        game_data_reset(env);

        if (gdata->kill_review_pending) {
          gdata->kill_review_pending = false;
          gdata->curr_screen = KILL_SHOTS_REVIEW;
          usr->gdata.conn = DISCONNECTED;
        } else if (gdata->restart_req) {
          usr->gdata.conn = CONNECTING;
          glfwSetTime(0);
          screenshot_run_reset();
          server_connect(env);
          gdata->restart_req = false;
        } else {
          usr->gdata.conn = DISCONNECTED;
        }
        gdata->closed = false;
      }

      break;
    case DISCONNECTED:
      usr->r->global.bg_opacity = 0;
      usr->r->global.bd_opacity = 0;
      usr->r->global.minimap_opacity = 0;

      gdata->curr_screen = TITLE_SCREEN;

      game_data_reset(env);
      server_poll(env);

      break;
  }
}
