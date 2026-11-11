#ifndef QUICK_GRAPHICS_H
#define QUICK_GRAPHICS_H

#include "user_settings.h"

/* Quick graphics: three one-tap presets that just switch the EXISTING
   settings on/off (nothing new is stored, nothing is remembered).
   Settings > General > Quick graphics. */
enum { QG_LOW = 0, QG_NORMAL, QG_HIGH, QG_COUNT };

/* Writes the preset into both gameplay modes (modes[0] Normal, modes[1]
   Assist), food glow, NTL tags and Performance mode. Does not save. */
void quick_graphics_apply(user_settings* usrs, int level);
const char* quick_graphics_name(int level);

#endif
