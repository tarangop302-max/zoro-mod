#ifndef NTL_TEAM_H
#define NTL_TEAM_H

#include <thermite.h>

/* cimgui.h has two branches: a plain-C struct/enum definition set, and a
 * C++-only fallback (raw templates like ImPool<T>) used when this macro is
 * absent. This project compiles as C (-std=gnu11), so it must always be
 * defined before cimgui.h is included -- same as user.h does. Defining it
 * twice (e.g. if user.h also includes cimgui.h later) is harmless: it's an
 * empty macro and cimgui.h has its own include guard. */
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "../cimgui/cimgui.h"

/* Colour emoji sprites (app/res/textures/emoji_sheet.png, see ntl_emoji.h),
 * shared with the TEAM CHAT so both show the same pictures.
 * ntl_emoji_ready(): the sheet is uploaded and can be drawn.
 * index_for_cp: sprite index of a Unicode codepoint, -1 if not in the set.
 * cp_for_index: the codepoint of a sprite (0 if out of range).
 * ntl_emoji_draw: draw sprite idx into the rect a..b. */
bool ntl_emoji_ready(void);
int ntl_emoji_index_for_cp(unsigned int cp);
unsigned int ntl_emoji_cp_for_index(int idx);
void ntl_emoji_draw(ImDrawList* dl, ImVec2 a, ImVec2 b, int idx, ImU32 tint);

void ntl_team_init(tenv* env);
void ntl_team_update(tenv* env);
void ntl_team_draw(tenv* env);
void ntl_team_draw_minimap(tenv* env, float x, float y, float size);
void ntl_team_consume_ui_touch(tenv* env);
void ntl_team_panel(tenv* env);
void ntl_team_destroy(tenv* env);

/* Shared minimap marker rendering (circle/diamond/triangle
 * with a dark border), also used by global_chat.c so both
 * systems' dots render identically. */
void ntl_draw_marker(
    ImDrawList* dl,
    ImVec2 p,
    float radius,
    int shape,
    ImU32 fill
);

/* Same as ntl_draw_marker, plus an optional pulse: pulse_interval is the
 * length of one pulse in seconds (<= 0 = no pulse). Shapes >=
 * NTL_SHAPE_EMOJI_BASE (ntl_emoji.h) draw a colour emoji on a disc of the
 * player's colour. */
void ntl_draw_marker_pulse(
    ImDrawList* dl,
    ImVec2 p,
    float radius,
    int shape,
    ImU32 fill,
    float pulse_interval
);

#endif
