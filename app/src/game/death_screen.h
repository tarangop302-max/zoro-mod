#ifndef DEATH_SCREEN_H
#define DEATH_SCREEN_H

#include <thermite.h>

/* Draws the death/run-end popup on top of whatever's currently on screen
 * (the game keeps rendering normally behind it -- see loop.c) and handles
 * its own Lobby/Restart buttons. Call this once game_loop's normal
 * per-frame CONNECTED work is done, only while gdata->death_pending is
 * true and the short post-death delay has elapsed. */
void ui_death_screen(tenv* env);

#endif
