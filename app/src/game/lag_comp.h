#ifndef LAG_COMP_H
#define LAG_COMP_H

#include <stdbool.h>

/*
 * Server-true prediction of MY OWN snake (client-side prediction with server
 * reconciliation).
 *
 * Timeline, on the client clock, with up = delay client->server and
 * down = delay server->client (each ~RTT/2):
 *   - a steering byte sent at time c reaches the server at c + up;
 *   - a heading echo or position packet that arrives now describes the server
 *     at now - down, so it is already `down` old, and it does NOT yet include
 *     the commands the server received after that moment.
 *
 * This module logs every steering byte we send and, every time the server
 * tells us our own heading, re-anchors on that heading and replays the
 * commands that were still in flight. The result is
 *   - the heading the server has right now (or the one to SHOW, if the player
 *     asked for turns to appear earlier than the server applies them), and
 *   - how far the server head has travelled since the packet it sent us.
 *
 * Pure functions on a private state: no game_data dependency, so it can be
 * unit-tested (tools/lag_sim.c).
 */

void lag_comp_reset(void);

/* A steering byte (target angle in radians) left the phone at now_ms. */
void lag_comp_sent(double now_ms, float target);

/* The server reported our own heading. ang / wang are radians, or < 0 when the
   packet did not carry that field. cur_ang is only a fallback for the very
   first anchor. rate = server turn rate in rad/ms. */
void lag_comp_echo(double now_ms, float ang, float wang, float cur_ang,
                   float up_ms, float down_ms, float rate);

bool lag_comp_ready(void);

/* Heading and turn target to show at now_ms. hold_ms is how long after
   sending a turn is shown (up_ms = exactly when the server applies it,
   0 = instantly). */
void lag_comp_display(double now_ms, float up_ms, float hold_ms, float rate,
                      float* ang, float* wang);

/* Displacement of the server head over the last `down_ms` (what to add to the
   position of a server packet to get where the server head is now). */
void lag_comp_lead(double now_ms, float up_ms, float down_ms, float rate,
                   float units_per_ms, float* dx, float* dy);

#endif
