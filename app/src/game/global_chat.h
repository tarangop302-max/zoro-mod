#ifndef GLOBAL_CHAT_H
#define GLOBAL_CHAT_H

#include <thermite.h>

/* Initialize the ZORO Public Chat system. */
void global_chat_init(tenv* env);

/* Update the chat system every frame. */
void global_chat_update(tenv* env);

/* Draw the small chat button/HUD while playing. */
void global_chat_draw(tenv* env);

/* Draw the full Public Chat panel. */
void global_chat_panel(tenv* env);

/* Release Public Chat resources. */
void global_chat_destroy(tenv* env);

#endif