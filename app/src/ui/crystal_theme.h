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

/* ---------------------------------------------------------------------------
 * Theme palette that follows the selected lobby background.
 *
 * All UI colours in the mod were designed in the default purple. Instead of
 * editing every literal, crystal_tint() re-maps any purple-ish colour onto the
 * current theme's hue / saturation (greys, whites, reds, greens... are left
 * alone). crystal_col() and every igPushStyleColor_Vec4() call (macro below)
 * go through it automatically, so buttons, frames, sliders, borders and the
 * gradient backgrounds all change together with the lobby background.
 * ------------------------------------------------------------------------- */
ImVec4 crystal_tint(ImVec4 c);
ImU32 crystal_tint_u32(ImU32 c);
/* Sets the theme from an accent colour (e.g. a preset's main glow colour). */
void crystal_theme_set_accent(float r, float g, float b);
void crystal_theme_reset_default(void);
/* Same as crystal_draw_glow_blob but WITHOUT theme tinting (used to draw the
   lobby background presets, which have their own exact colours). */
void crystal_draw_glow_blob_raw(ImDrawList* dl, ImVec2 center, float radius,
                                float r, float g, float b, float peak_alpha);

/* Every style colour pushed anywhere in the UI is tinted to the theme. The
   variadic form is needed because compound literals contain commas. */
#define igPushStyleColor_Vec4(idx, ...) \
  igPushStyleColor_Vec4((idx), crystal_tint(__VA_ARGS__))

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
/* Soft circular glow (used by the built-in lobby backgrounds too). */
void crystal_draw_glow_blob(ImDrawList* dl, ImVec2 center, float radius,
                            float r, float g, float b, float peak_alpha);
void crystal_sheen(void);
void crystal_glow_rect(ImVec2 pos, ImVec2 size, float r, float g, float b);
/* ---------------------------------------------------------------------------
 * Themed, FIXED-SIZE modal panel (used by the Snakey Rain and Lobby
 * Background panels).
 *
 * Why not igBeginPopup(): ImGui popups are always auto-resized to their
 * contents every frame. Widgets sized from the available width (-1 sliders,
 * equal-width thumbnails, ...) then shrink the window a bit more each frame,
 * which is exactly the "panel keeps getting smaller / slider stops working"
 * bug. A modal popup with an explicit, always-applied size has no such
 * feedback loop.
 *
 * Usage (open it first with igOpenPopup_Str(id, 0)):
 *     if (crystal_panel_begin(env, "my_panel", "TITLE", "subtitle", 460)) {
 *       ...contents (scrolls on its own; add touch scroll if you like)...
 *       crystal_panel_end();
 *     }
 * crystal_panel_end() must be called ONLY when begin returned true.
 * ------------------------------------------------------------------------- */
bool crystal_panel_begin(tenv* env, const char* id, const char* title,
                         const char* subtitle, float max_w);
void crystal_panel_end(void);
/* Optional, one-shot (cleared by the next crystal_panel_begin): size the panel
   as a fraction of the screen instead of the default (e.g. 0.52 x 0.88 = the
   same footprint as the lobby Settings / Controls popups), and reserve
   `footer_h` pixels at the bottom for a pinned footer (buttons). After the
   body is done call crystal_panel_end_body(), draw the footer, then
   crystal_panel_end(). */
void crystal_panel_set_layout(float w_frac, float h_frac, float footer_h);
void crystal_panel_end_body(void);
/* Round themed "X" button drawn at screen position `pos` with diameter `d`.
   Returns true when tapped. Call inside a window. */
bool crystal_close_button(const char* id, ImVec2 pos, float d);
/* Small accent bar + purple label, replaces igSeparatorText inside panels. */
void crystal_section(const char* label);
/* Rounded translucent card behind the next `h` pixels of content width-wide.
   Call BEFORE the content; returns the card's screen rect via out params. */
void crystal_card_bg(ImVec2 pos, ImVec2 size, float r, float g, float b);

void crystal_push_theme(void);
void crystal_pop_theme(void);

#endif
