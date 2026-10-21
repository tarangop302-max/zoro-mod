#ifndef SETTINGS_H
#define SETTINGS_H

#include <thermite.h>

void ui_settings_init(tenv* env);
void ui_settings(tenv* env);
void ui_settings_destroy(tenv* env);

/* Touch drag-scroll for the current ImGui child window (idx = owner id). */
void ui_touch_scroll(tenv* env, int idx);

#endif
