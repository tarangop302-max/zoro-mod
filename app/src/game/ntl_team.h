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

#endif
