#ifndef GLOBAL_CHAT_H
#define GLOBAL_CHAT_H

#include <thermite.h>

/* Initialize the ZORO Global Chat system. */
void global_chat_init(tenv* env);

/* Update the chat connection and receive messages. */
void global_chat_update(tenv* env);

/* Draw the full Global Chat panel. */
void global_chat_panel(tenv* env);

/* Release Global Chat resources. */
void global_chat_destroy(tenv* env);

#endif