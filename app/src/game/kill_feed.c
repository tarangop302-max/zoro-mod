#include "kill_feed.h"

#include <stdio.h>

#include "../user.h"
#include "screenshot.h"

/* The server only ever tells the client "you got a kill" -- a bare 'k'
   command with no payload at all (see the `cmd == 'k'` case in
   network/callback.c) -- it never names the victim. So this can't show
   "You killed <name>"; that would mean guessing at whichever nearby
   snake happened to die around the same tick, which could easily credit
   the wrong player. Instead this is an honest "+1 Kill" toast plus a
   running total, triggered purely by watching gdata->data.kills
   increase frame to frame -- no network/parsing changes needed at all. */

#define MAX_TOASTS 5
#define TOAST_LIFETIME 2.2
#define TOAST_FADE_IN 0.15
#define TOAST_FADE_OUT 0.5

typedef struct {
  bool active;
  double spawn_time;
  int kill_number;
} kill_toast;

static kill_toast s_toasts[MAX_TOASTS];
static int s_last_seen_kills = -1;

static ImU32 kf_col(float r, float g, float b, float a) {
  return igColorConvertFloat4ToU32((ImVec4){r, g, b, a});
}

void kill_feed_draw(tenv *env) {
  tuser_data *usr = env->usr;
  game_data *gdata = &usr->gdata;
  tcontext *ctx = env->ctx;

  int snakes_len = tdarray_length(gdata->data.snakes);
  bool in_real_match =
      snakes_len > 0 && !gdata->preview_active && gdata->data.follow_view;

  if (!in_real_match) {
    /* Not in a real match (e.g. the bot preview on Settings/Controls) --
       don't track or show anything, and reset so a fresh match doesn't
       replay a stale kill-count delta as toasts. */
    s_last_seen_kills = -1;
    for (int i = 0; i < MAX_TOASTS; i++) s_toasts[i].active = false;
    return;
  }

  double now = igGetTime();

  if (s_last_seen_kills < 0) {
    s_last_seen_kills = gdata->data.kills;
  } else if (gdata->data.kills > s_last_seen_kills) {
    int gained = gdata->data.kills - s_last_seen_kills;
    for (int k = 0; k < gained; k++) {
      for (int i = 0; i < MAX_TOASTS; i++) {
        if (!s_toasts[i].active) {
          s_toasts[i].active = true;
          s_toasts[i].spawn_time = now;
          s_toasts[i].kill_number = s_last_seen_kills + k + 1;
          screenshot_request(s_toasts[i].kill_number);
          break;
        }
      }
    }
    s_last_seen_kills = gdata->data.kills;
  }

  int visible[MAX_TOASTS];
  int visible_count = 0;
  for (int i = 0; i < MAX_TOASTS; i++) {
    if (!s_toasts[i].active) continue;
    double age = now - s_toasts[i].spawn_time;
    if (age > TOAST_LIFETIME) {
      s_toasts[i].active = false;
      continue;
    }
    visible[visible_count++] = i;
  }
  if (visible_count == 0 && gdata->data.kills <= 0) return;

  /* Oldest first, so the newest toast lands at the bottom of the stack
     and older ones sit above it, already drifted up a bit. */
  for (int a = 0; a < visible_count; a++)
    for (int b = a + 1; b < visible_count; b++)
      if (s_toasts[visible[a]].spawn_time > s_toasts[visible[b]].spawn_time) {
        int t = visible[a];
        visible[a] = visible[b];
        visible[b] = t;
      }

  ImDrawList *dl = igGetForegroundDrawList_ViewportPtr(igGetMainViewport());
  float cx = ctx->size[0] * 0.5f;
  float top_y = ctx->size[1] * 0.22f;

  for (int vi = 0; vi < visible_count; vi++) {
    kill_toast *t = &s_toasts[visible[vi]];
    double age = now - t->spawn_time;
    float alpha;
    if (age < TOAST_FADE_IN) {
      alpha = (float)(age / TOAST_FADE_IN);
    } else if (age > TOAST_LIFETIME - TOAST_FADE_OUT) {
      alpha = (float)((TOAST_LIFETIME - age) / TOAST_FADE_OUT);
    } else {
      alpha = 1.0f;
    }
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    /* Slight upward drift, and a quick scale-in pop on spawn. */
    float rise = (float)age * 14.0f;
    float scale = age < TOAST_FADE_IN
                     ? (0.75f + 0.25f * (float)(age / TOAST_FADE_IN))
                     : 1.0f;
    float y = top_y - rise + vi * 46.0f;

    char buf[48];
    snprintf(buf, sizeof(buf), "\ueaeb Kill #%d", t->kill_number);

    ImVec2 ts;
    igCalcTextSize(&ts, buf, NULL, false, -1.0f);
    float pw = (ts.x + 36.0f) * scale;
    float ph = (ts.y + 18.0f) * scale;
    float px = cx - pw * 0.5f;
    float py = y - ph * 0.5f;

    ImDrawList_AddRectFilled(dl, (ImVec2){px, py}, (ImVec2){px + pw, py + ph},
                             kf_col(0.373f, 0.290f, 0.607f, 0.55f * alpha),
                             ph * 0.5f, ImDrawFlags_None);
    ImDrawList_AddRect(dl, (ImVec2){px, py}, (ImVec2){px + pw, py + ph},
                       kf_col(0.690f, 0.580f, 0.960f, 0.85f * alpha),
                       ph * 0.5f, ImDrawFlags_None, 1.5f);
    ImDrawList_AddText_Vec2(
        dl, (ImVec2){cx - ts.x * 0.5f * scale, y - ts.y * 0.5f * scale},
        kf_col(1.0f, 0.925f, 0.55f, alpha), buf, NULL);
  }

  /* Small persistent running total, upper-right, so a streak of kills is
     visible at a glance without waiting on a toast. */
  if (gdata->data.kills > 0) {
    char total_buf[32];
    snprintf(total_buf, sizeof(total_buf), "\ueaeb %d", gdata->data.kills);
    ImVec2 tts;
    igCalcTextSize(&tts, total_buf, NULL, false, -1.0f);
    float tx = ctx->size[0] - tts.x - 22.0f;
    float ty = 16.0f;
    ImDrawList_AddRectFilled(dl, (ImVec2){tx - 10.0f, ty - 5.0f},
                             (ImVec2){tx + tts.x + 10.0f, ty + tts.y + 5.0f},
                             kf_col(0.114f, 0.063f, 0.212f, 0.55f),
                             (tts.y + 10.0f) * 0.5f, ImDrawFlags_None);
    ImDrawList_AddText_Vec2(dl, (ImVec2){tx, ty},
                            kf_col(0.945f, 0.925f, 1.0f, 0.92f), total_buf,
                            NULL);
  }
}
