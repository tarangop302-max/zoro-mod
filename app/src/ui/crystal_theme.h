#ifndef CRYSTAL_THEME_H
#define CRYSTAL_THEME_H

#include <thermite.h>
#include "../user.h"

/* ============================================================================
 * Shared crystal / purple glass theme.
 *
 * Originally lived only in title_screen.c; pulled out here so any screen
 * (skin editor, settings, etc.) can opt into the exact same look:
 *   - crystal_draw_background(): 4-corner gradient + glow blobs, call once
 *     before any widgets so it sits behind everything. Opaque -- use this
 *     on screens with nothing else drawn underneath.
 *   - crystal_draw_background_alpha(): same, but with an adjustable overall
 *     opacity (0-1). Use this on a screen that has its own content drawn
 *     underneath the UI layer (e.g. the skin editor's live skin preview,
 *     which is drawn by a separate renderer before ImGui runs) so that
 *     content can still show through the tint instead of being covered.
 *   - crystal_push_theme() / crystal_pop_theme(): flat translucent-purple
 *     frame/button/header colors for the whole screen -- push once near the
 *     top, pop once at the very end
 *   - crystal_sheen(): thin light line along a widget's top edge, call right
 *     after drawing that widget
 *   - crystal_glow_rect(): soft rounded glow behind an upcoming widget, call
 *     BEFORE drawing it with its known screen-space position/size
 * ============================================================================
 */

void crystal_draw_background(tenv* env);
void crystal_draw_background_alpha(tenv* env, float alpha);
void crystal_sheen(void);
void crystal_glow_rect(ImVec2 pos, ImVec2 size, float r, float g, float b);
void crystal_push_theme(void);
void crystal_pop_theme(void);

#endif
