#ifndef LOBBY_BG_H
#define LOBBY_BG_H

#include <thermite.h>

/* Lobby (title screen) background.
 *
 * Only the main lobby screen uses this -- every other menu screen shows the
 * live game behind it and keeps using crystal_draw_background().
 *
 *   - ui_lobby_bg_draw():   draws the selected background (original gradient,
 *                           one of the built-in presets, or the picture the
 *                           player uploaded). Call where
 *                           crystal_draw_background() used to be called.
 *   - ui_lobby_bg_button(): the "Background" button (top-right, under the
 *                           Snakey Rain button) and its popup.
 *   - ui_lobby_bg_destroy(): frees the uploaded picture's texture.
 */
void ui_lobby_bg_draw(tenv* env);
void ui_lobby_bg_button(tenv* env, float frame_height);
void ui_lobby_bg_destroy(tenv* env);

#endif
