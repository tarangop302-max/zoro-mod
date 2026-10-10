#ifndef EYES_BACK_H
#define EYES_BACK_H

#include <stdbool.h>

#include "game_data.h"
#include "snake.h"

/*
 * Eyes Back (ported from NTL VANCED).
 *
 * While it is on, the angle byte sent to the server every ~33 ms is not the
 * mouse/arrow angle but "straight behind the head", alternating a few steps to
 * the left and right of exactly 180 degrees. The server keeps turning toward
 * whichever side was sent last, so the head still follows your mouse / arrow
 * (duty-cycle steering), while the target angle the server broadcasts -- which
 * is what every other player sees as your eyes -- stays pointed backwards.
 *
 * A small model of the server's own turning (replaying the commands that are
 * still in flight, re-anchored on every heading the server echoes back)
 * decides which side to send next.
 */

/* Forget all state (call when the feature switches on, or a new game starts). */
void eyes_back_reset(void);

/* The server just told us our own heading (radians, 0..2PI). */
void eyes_back_on_server_ang(game_data* gdata, float ang, double now_ms);

/* Returns the 0..250 angle byte to send now. has_target is false when the
   mouse sits on the head (then the current heading is simply held). */
int eyes_back_tick(game_data* gdata, snake* me, bool has_target, float target,
                   double now_ms);

#endif
