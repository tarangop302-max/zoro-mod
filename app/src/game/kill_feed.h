#ifndef KILL_FEED_H
#define KILL_FEED_H

#include <thermite.h>

/* Draws (and internally tracks) the on-screen "kill" toast notifications.
   Call once per frame from ui_overlay(); it no-ops outside of a real,
   non-preview match. See kill_feed.c for why it can't show the victim's
   name. */
void kill_feed_draw(tenv *env);

#endif
