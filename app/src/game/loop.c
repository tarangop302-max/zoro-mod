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
#include "input.h"
#include "oef.h"
#include "redraw.h"
#include "ui_overlay.h"

/* Fixed delay between the player's own death and the death popup
 * appearing (see gdata->death_pending in game_data.h) -- long enough for
 * the death to actually read as having happened, short enough that it
 * never feels like the old indeterminate freeze. */
#define DEATH_ANIM_SECONDS 1.0

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
      input(env);
      server_poll(env);
      oef(env);
      redraw(env);
      ui_overlay(env);

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
        // of a screen that suddenly looks frozen. The quit/restart
        // hotkeys are deliberately disabled above once death_pending is
        // set, so an old habit like right-click-to-restart can't bypass
        // this popup once it's about to appear.
        ui_death_screen(env);
      }

      if (gdata->closed) {
        game_data_reset(env);

        if (gdata->restart_req) {
          usr->gdata.conn = CONNECTING;
          glfwSetTime(0);
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
