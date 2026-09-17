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
/* Same background, but with up to two rectangular holes left untouched --
   for screens where content is drawn by a separate renderer underneath
   ImGui (e.g. the skin editor's live preview + color grid) and must not
   be tinted by this overlay at all, rather than just tinted less. Pass a
   zero-size rect (e.g. min == max) for excl2 if there's only one hole.
   Rects are expected top-to-bottom (excl1 above excl2); the space around,
   between, and below them is filled with the same gradient + glow as
   crystal_draw_background_alpha, seamlessly (each band's corner colors
   are interpolated from the same 4 base corners, so there's no visible
   seam at the hole edges). */
void crystal_draw_background_alpha_excl2(tenv* env, float alpha,
                                         ImVec2 excl1_min, ImVec2 excl1_max,
                                         ImVec2 excl2_min, ImVec2 excl2_max);
void crystal_sheen(void);
void crystal_glow_rect(ImVec2 pos, ImVec2 size, float r, float g, float b);
void crystal_push_theme(void);
void crystal_pop_theme(void);

#endif
