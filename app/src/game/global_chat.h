#ifndef GLOBAL_CHAT_H
#define GLOBAL_CHAT_H

#include <thermite.h>
#include "thermite/jsr_network.h"

/* Initialize the ZORO Public Chat system. */
void global_chat_init(tenv* env);

/* Update the chat system every frame. */
void global_chat_update(tenv* env);

/* Draw the Public Chat window (button when collapsed,
 * full chat box when expanded). */
void global_chat_draw(tenv* env);

/* Draw a dot on the minimap for every Public Chat player
 * on the same game server as us. Call this right after
 * ntl_team_draw_minimap() with the same x/y/size. */
void global_chat_draw_minimap_markers(
    tenv* env,
    float x,
    float y,
    float size
);

/* Read-only access to the underlying JSR network connection
 * (NULL if not initialized yet), for other systems that want
 * to show the same roster/location data -- e.g. the "Players
 * & minimap" tab under Team Chat. */
jsr_network* global_chat_get_network(void);

/* Release Public Chat resources. */
void global_chat_destroy(tenv* env);

#endif