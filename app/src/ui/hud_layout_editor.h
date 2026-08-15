#ifndef HUD_LAYOUT_EDITOR_H
#define HUD_LAYOUT_EDITOR_H

#include <thermite.h>

/* Call from the "Adjust HUD Layout" button in Settings. Snapshots the
 * current leaderboard/teammates/minimap position+size (so "Back" can
 * discard changes) and switches to the editor screen. */
void ui_hud_layout_editor_enter(tenv* env);

/* Draws the editor screen's own UI (title, instructions, Confirm/Back
 * buttons). The actual leaderboard/teammates/minimap being edited are
 * drawn by the normal ui_overlay(env) call bg_preview.c already makes
 * for this screen, using a hidden, isolated, self-respawning preview
 * snake -- nothing here touches the player's real game session. */
void ui_hud_layout_editor(tenv* env);

#endif
