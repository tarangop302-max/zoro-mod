#ifndef GLOBAL_CHAT_H
#define GLOBAL_CHAT_H

#include <thermite.h>

/* Initialize the ZORO Public Chat system. */
void global_chat_init(tenv* env);

/* Update the chat system every frame. */
void global_chat_update(tenv* env);

/* Draw the Public Chat window (button when collapsed,
 * full chat box when expanded). */
void global_chat_draw(tenv* env);

/* Release Public Chat resources. */
void global_chat_destroy(tenv* env);

#endif