#ifndef LB_TEXT_FX_H
#define LB_TEXT_FX_H

#include <thermite.h>

/* ImVec4 / ImDrawList etc. come from cimgui.h. Without this define it declares
   them as opaque types, so define it first -- same as user.h / net_graph.h. */
#ifndef CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#endif
#include "../cimgui/cimgui.h"

#include "user_settings.h"

/* Leaderboard text effects (Settings > General > Leaderboard ...).
 *
 * lb_fx_active() is false for the plain "Solid colour, no outline, no shadow"
 * setting, in which case callers keep using igTextColored() exactly as before.
 *
 * lb_fx_text() draws `text` at the current ImGui cursor in the CURRENT font
 * (push it first) and advances the layout like a normal text item, so it drops
 * into a table cell or a plain window the same way igTextColored() does.
 *
 *   c1, c2   start / end colour (alpha included; the gradient, wave and pulse
 *            blend alpha too, rainbow keeps c1's alpha)
 *   t0, t1   where along the effect this text sits: the first character gets
 *            t0, the last t1. 0..1 spreads the effect across the text; passing
 *            the same value for both gives the whole text one colour of the
 *            effect (used to spread a gradient down the list, row by row). */
bool lb_fx_active(const lb_text_fx* fx);
void lb_fx_text(const lb_text_fx* fx, ImVec4 c1, ImVec4 c2, float t0, float t1,
                const char* text);

/* fx->color2 as an ImVec4 with its alpha multiplied by `alpha_scale`. */
ImVec4 lb_fx_color2(const lb_text_fx* fx, float alpha_scale);

#endif
