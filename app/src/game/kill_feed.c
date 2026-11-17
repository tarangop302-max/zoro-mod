#include "kill_feed.h"

#include "../user.h"
#include "screenshot.h"

/* The on-screen kill popup ("+1 Kill" toast and the running-total pill) has
   been removed. This only keeps watching gdata->data.kills so each new kill
   still triggers the kill-shot capture (screenshot_request); nothing is
   drawn. */

static int s_last_seen_kills = -1;

void kill_feed_draw(tenv *env) {
  tuser_data *usr = env->usr;
  game_data *gdata = &usr->gdata;

  int snakes_len = tdarray_length(gdata->data.snakes);
  bool in_real_match =
      snakes_len > 0 && !gdata->preview_active && gdata->data.follow_view;

  if (!in_real_match) {
    /* Not in a real match (e.g. the bot preview) -- reset so a fresh match
       doesn't replay a stale kill-count delta as kill shots. */
    s_last_seen_kills = -1;
    return;
  }

  if (s_last_seen_kills < 0) {
    s_last_seen_kills = gdata->data.kills;
  } else if (gdata->data.kills > s_last_seen_kills) {
    for (int k = s_last_seen_kills + 1; k <= gdata->data.kills; k++)
      screenshot_request(k);
    s_last_seen_kills = gdata->data.kills;
  }
}
