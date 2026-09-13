#ifndef DEATH_SCREEN_H
#define DEATH_SCREEN_H

#include <thermite.h>

/* Fixed delay between the player's own death and the death popup
 * appearing (see gdata->death_pending in game_data.h) -- long enough for
 * the death to actually read as having happened, short enough that it
 * never feels like the old indeterminate freeze. Shared between loop.c
 * (gates when the popup starts drawing) and main.c (gates when the
 * background blur behind it kicks in), so both flip on at exactly the
 * same moment. */
#define DEATH_ANIM_SECONDS 1.0

/* Draws the death/run-end popup on top of whatever's currently on screen
 * (the game keeps rendering normally behind it -- see loop.c) and handles
 * its own Lobby/Restart buttons. Call this once game_loop's normal
 * per-frame CONNECTED work is done, only while gdata->death_pending is
 * true and the short post-death delay has elapsed. */
void ui_death_screen(tenv* env);

#endif
