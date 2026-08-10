#ifndef NTL_TEAM_H
#define NTL_TEAM_H

#include <thermite.h>
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
