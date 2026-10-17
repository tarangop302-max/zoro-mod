#ifndef SQUEEZE_H
#define SQUEEZE_H

#include <stdbool.h>

#include "game_data.h"
#include "snake.h"

/*
 * Squeeze mode.
 *
 * Two dots decide whether the local snake dies: the head centre (RED) and the
 * head tip (BLUE). Everything else may overlap enemy bodies freely. While
 * Squeeze is on, the steering vector (xm, ym) is filtered so neither dot ever
 * enters an enemy snake. Enemy heads and bodies are treated alike: static
 * circles, no movement prediction.
 *
 * Two behaviours, chosen at the moment Squeeze is switched on:
 *  - You were circling: the circle is locked in. The snake keeps circling at
 *    the tightest turn and ignores the mouse; it only leaves the circle when
 *    an enemy body is in the way, finds the safest gap (however narrow),
 *    and goes back to circling once the way is clear.
 *  - You were not circling: you steer with the mouse as normal. The filter
 *    only stops the two dots from entering an enemy body, sliding along it
 *    without vibrating.
 */

/* Call EVERY frame (also while Squeeze is off): tracks your turning so the
   circle can be locked in the moment Squeeze is switched on. */
void squeeze_observe(game_data* gdata, snake* me, bool active);

/* Call every frame while Squeeze is on. Returns true if (xm, ym) changed. */
bool squeeze_filter(game_data* gdata, snake* me, int* xm, int* ym);

/* For an on-screen indicator. */
bool squeeze_is_deflecting(void);
bool squeeze_is_circling(void);

#endif
