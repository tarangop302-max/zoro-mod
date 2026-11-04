#ifndef STEER_MODEL_H
#define STEER_MODEL_H

#include <stdbool.h>

#include "game_data.h"
#include "lag_comp.h"
#include "snake.h"

/*
 * Glue between lag_comp (the server-true model of my own snake) and the game:
 * which delay / hold / turn rate to feed it.
 */

/* Longest one-way delay the model will believe (ms). */
#define STEER_MODEL_MAX_MS 600.0f

/* The model steers my snake unless Eyes back owns the steering byte (it sends
   a deliberately "wrong" angle, so the model's commands would be fiction). */
static inline bool steer_model_on(const game_data* g) {
#if STEER_PREDICT
  return !g->data.eb_eye_on;
#else
  (void)g;
  return false;
#endif
}

/* One-way delay in ms (same for both directions; RTT / 2). */
static inline float steer_up_ms(const game_data* g) {
  float v = g->data.owd_ms;
  if (v < 0.0f) v = 0.0f;
  return v > STEER_MODEL_MAX_MS ? STEER_MODEL_MAX_MS : v;
}

/* How long after sending a turn the screen shows it. Latency compensation OFF
   shows it instantly; ON shows it turn_delay_frac of the way to the moment the
   server applies it (100% = exactly then). */
static inline float steer_hold_ms(const game_data* g) {
  if (!g->data.lat_comp) return 0.0f;
  float h = steer_up_ms(g) * g->data.turn_delay_frac;
  return h < 0.0f ? 0.0f : h;
}

/* Server turn rate of a snake in rad per ms. */
static inline float steer_rate(const game_data* g, const snake* o) {
  float r = g->data.mamu * o->scang * o->spang / 8.0f;
  return r < 1e-6f ? 1e-6f : r;
}

#endif
