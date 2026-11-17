#include "kill_feed.h"

#include <stdio.h>

#include <string.h>

#ifdef ANDROID
#include "../android_path.h"
#endif
#include "../network/server.h"
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

/* Battledome-only kill counters.
   - s_bd_live:  the "real-time" kills, read from our own row of the server's
                 leaderboard (Battledome puts kills there; the server resets
                 it on death and lowers it over time).
   - s_bd_total: kills since we joined this Battledome server. Counted from
                 the 'k' kill events, so it survives dying. It is cleared
                 only when the live kills decay to 0 while we are alive, or
                 when we move to another server. */
static int s_bd_total = 0;
static int s_bd_live = 0;
static bool s_bd_armed = false; /* live kills were > 0 during this life */
static unsigned s_bd_lb_serial = 0;
static char s_bd_ip[64] = {0};
static unsigned s_bd_k_serial = 0;
static bool s_bd_on_board = false; /* our row is in the leaderboard list */

/* Data log used to work out how Battledome decays the live kills:
   bd_kill_log.csv with lines "time_seconds,event,value".
   K = we got a kill (value = our run's kill count), L = live kills read
   from the leaderboard, O = we are not on the leaderboard (value = rank),
   C = server kill counter received in a 'k' packet, N = new life started. */
static void bd_log(const char *ev, int v) {
  FILE *f;
#ifdef ANDROID
  char path[512];
  android_build_path(path, sizeof(path), "bd_kill_log.csv");
  f = fopen(path, "ab");
#else
  f = fopen("bd_kill_log.csv", "ab");
#endif
  if (!f) return;
  fprintf(f, "%.1f,%s,%d\n", igGetTime(), ev, v);
  fclose(f);
}

static void bd_pill(ImDrawList *dl, float cx, float y, const char *buf) {
  ImVec2 ts;
  igCalcTextSize(&ts, buf, NULL, false, -1.0f);
  float tx = cx - ts.x * 0.5f;
  ImDrawList_AddRectFilled(dl, (ImVec2){tx - 10.0f, y - 5.0f},
                           (ImVec2){tx + ts.x + 10.0f, y + ts.y + 5.0f},
                           igColorConvertFloat4ToU32((ImVec4){0.114f, 0.063f, 0.212f, 0.55f}),
                           (ts.y + 10.0f) * 0.5f, ImDrawFlags_None);
  ImDrawList_AddText_Vec2(dl, (ImVec2){tx, y},
                          igColorConvertFloat4ToU32((ImVec4){0.945f, 0.925f, 1.0f, 0.92f}),
                          buf, NULL);
}

static ImU32 kf_col(float r, float g, float b, float a) {
  return igColorConvertFloat4ToU32((ImVec4){r, g, b, a});
}

void kill_feed_draw(tenv *env) {
  tuser_data *usr = env->usr;
  game_data *gdata = &usr->gdata;
  tcontext *ctx = env->ctx;

  const bool bd = server_is_battledome(usr->usrs.ipv4);
  if (!bd || strncmp(s_bd_ip, usr->usrs.ipv4, sizeof s_bd_ip - 1) != 0) {
    /* Left Battledome or switched server: start the total fresh. */
    s_bd_total = 0;
    s_bd_live = 0;
    s_bd_armed = false;
    strncpy(s_bd_ip, bd ? usr->usrs.ipv4 : "", sizeof s_bd_ip - 1);
  }

  int snakes_len = tdarray_length(gdata->data.snakes);
  bool in_real_match =
      snakes_len > 0 && !gdata->preview_active && gdata->data.follow_view;

  if (!in_real_match) {
    /* Not in a real match (e.g. the bot preview on Settings/Controls) --
       don't track or show anything, and reset so a fresh match doesn't
       replay a stale kill-count delta as toasts. */
    s_last_seen_kills = -1;
    for (int i = 0; i < MAX_TOASTS; i++) s_toasts[i].active = false;
    /* New life: the server restarts the live kills at 0, which must not
       count as a decay-to-zero reset of the total. */
    if (bd && s_bd_armed) bd_log("N", 0);
    s_bd_live = 0;
    s_bd_armed = false;
    s_bd_on_board = false;
    s_bd_lb_serial = gdata->data.lb_serial;
    s_bd_k_serial = gdata->data.bd_k_serial;
    return;
  }

  double now = igGetTime();

  if (s_last_seen_kills < 0) {
    s_last_seen_kills = gdata->data.kills;
  } else if (gdata->data.kills > s_last_seen_kills) {
    int gained = gdata->data.kills - s_last_seen_kills;
    if (bd) {
      s_bd_total += gained;
      bd_log("K", gdata->data.kills);
    }
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

  if (bd) {
    /* Server's own counter from the 'k' packet (what NTL shows). It is only
       refreshed when we get a kill, so it is the fallback for when we are
       not on the leaderboard. */
    if (gdata->data.bd_k_serial != s_bd_k_serial) {
      s_bd_k_serial = gdata->data.bd_k_serial;
      bd_log("C", gdata->data.bd_k_counter);
      if (!s_bd_on_board) s_bd_live = gdata->data.bd_k_counter;
    }
    if (gdata->data.lb_serial != s_bd_lb_serial) {
      s_bd_lb_serial = gdata->data.lb_serial;
      const int pos = gdata->data.lb_pos;
      if (gdata->data.gotlb && !gdata->death_pending &&
          !(pos >= 1 && pos <= NUM_LEADERBOARD_ENTRIES)) {
        if (s_bd_on_board) bd_log("O", gdata->data.rank);
        s_bd_on_board = false;
      }
      if (gdata->data.gotlb && !gdata->death_pending && pos >= 1 &&
          pos <= NUM_LEADERBOARD_ENTRIES) {
        s_bd_on_board = true;
        int live = gdata->data.lb.entries[pos - 1].score;
        if (live != s_bd_live) bd_log("L", live);
        if (live < 0) live = 0;
        if (live > 0) {
          s_bd_armed = true;
        } else if (s_bd_armed) {
          s_bd_total = 0; /* live kills decayed to 0 -> reset both */
          s_bd_armed = false;
        }
        s_bd_live = live;
      }
    }
    ImDrawList *bdl = igGetForegroundDrawList_ViewportPtr(igGetMainViewport());
    char tb[48], lb[48];
    snprintf(tb, sizeof tb, "\ueaeb Total kills: %d", s_bd_total);
    if (s_bd_on_board)
      snprintf(lb, sizeof lb, "\ueaeb Live kills: %d", s_bd_live);
    else if (gdata->data.bd_k_valid)
      snprintf(lb, sizeof lb, "\ueaeb Live kills: %d (last kill)", s_bd_live);
    else
      snprintf(lb, sizeof lb, "\ueaeb Live kills: -- (off board)");
    bd_pill(bdl, ctx->size[0] * 0.5f, 16.0f, tb);
    bd_pill(bdl, ctx->size[0] * 0.5f, 16.0f + 34.0f, lb);
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
  if (!bd && gdata->data.kills > 0) {
    char total_buf[32];
    snprintf(total_buf, sizeof(total_buf), "\ueaeb %d", gdata->data.kills);
    ImVec2 tts;
    igCalcTextSize(&tts, total_buf, NULL, false, -1.0f);
    float tx = (ctx->size[0] - tts.x) * 0.5f; /* top-centre: top-right is
                                                 where the leaderboard is */
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
