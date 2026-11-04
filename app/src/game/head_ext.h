#ifndef HEAD_EXT_H
#define HEAD_EXT_H

#include "snake.h"

/*
 * Head extension.
 *
 * Heads are drawn ahead of the newest server point (latency lead). The body
 * renderer only joins the head to the newest point across at most one segment
 * (msl), so a head further ahead than that was drawn detached -- which is
 * exactly what happens at high ping, where the lead is several segments.
 * head_ext_update() bridges the gap with a few temporary body points placed
 * along the line to the head; head_ext_drop() removes them again just before a
 * real server point is appended.
 */

/* Most temporary points one snake can carry (also bounds the lead: HEAD_EXT_MAX-1
   segments, ~340 units). */
#define HEAD_EXT_MAX 8

/* Remove the trailing temporary points. Call before chaining a new real point. */
void head_ext_drop(snake* o);

/* Ensure the head is within one segment of the newest point. Call each frame
   after the head has moved. */
void head_ext_update(snake* o);

#endif
