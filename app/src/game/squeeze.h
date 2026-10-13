#ifndef SQUEEZE_H
#define SQUEEZE_H

#include <stdbool.h>

#include "game_data.h"
#include "snake.h"

/*
 * Squeeze mode.
 *
 * Only the head's centre point can ever kill the local snake -- the rest of
 * the body is allowed to overlap enemy bodies freely. While Squeeze is on, the
 * steering vector (xm, ym) that input() is about to turn into an angle byte is
 * filtered so that the centre line of the head never enters any enemy body
 * circle (or an enemy head), no matter where the mouse / joystick points.
 *
 * If the requested heading is already safe it is left completely untouched,
 * so you can still push straight into an enemy's body: the filter only starts
 * bending the heading once the centre line would actually touch something,
 * and it then picks the safe heading closest to the one you asked for (i.e.
 * the snake slides along the enemy's edge / squeezes through gaps).
 *
 * Returns true when the heading was changed.
 */
bool squeeze_filter(game_data* gdata, snake* me, int* xm, int* ym);

/* Last result, for drawing a small on-screen indicator. */
bool squeeze_is_deflecting(void);

#endif
