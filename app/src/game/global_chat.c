#include "global_chat.h"

#include <string.h>

/*
 * ZORO Global Chat
 *
 * This is the first stage:
 * - The module initializes correctly
 * - It has its own update function
 * - It has its own panel
 * - It does not modify or interfere with NTL Team
 *
 * Network connection and message sending will be added
 * after the new screen is connected to main.c.
 */

static bool global_chat_initialized = false;

void global_chat_init(tenv* env) {
    (void)env;

    global_chat_initialized = true;
}

void global_chat_update(tenv* env) {
    (void)env;

    if (!global_chat_initialized) {
        return;
    }

    /*
     * Railway/WebSocket updates will be added here later.
     */
}

void global_chat_panel(tenv* env) {
    (void)env;

    if (!global_chat_initialized) {
        return;
    }

    /*
     * The visible Global Chat UI will be added after
     * we connect this module to the app screen system.
     */
}

void global_chat_destroy(tenv* env) {
    (void)env;

    global_chat_initialized = false;
}