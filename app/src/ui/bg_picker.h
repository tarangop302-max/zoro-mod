#ifndef BG_PICKER_H
#define BG_PICKER_H

#include <thermite.h>

/* Settings > General > Background.
 *
 * Draws one full-width button labelled with the current in-game background.
 * Tapping it opens a popup with every background in game/backgrounds.h as a
 * thumbnail grid; tapping a thumbnail selects it, saves the setting and closes
 * the popup. Call it from inside a settings row, every frame. */
void ui_bg_picker_button(tenv* env);

#endif
