#include "thermite/jsr_network.h"
#include "thermite/tchat.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * jsr_network.c is inside:
 *     thermite/src/
 *
 * The repository's app folder is:
 *     app/src/external/
 *
 * Therefore we must go up two directories:
 *     thermite/src -> thermite -> repository root
 */
#include "../../app/src/external/mongoose.h"

// ============================================================================
// Internal Helper Functions
// ============================================================================

static size_t jsr_safe_strlen(const char *text, size_t max_len) {
    size_t length = 0;

    if (text == NULL) {
        return 0;
    }

    while (length < max_len && text[length] != '\0') {
        length++;
    }

    return length;
}

// Generate a peer ID
static void jsr_generate_peer_id(char *peer_id, size_t size) {
    static const char charset[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789";

    static bool random_seeded = false;

    if (peer_id == NULL || size == 0) {
        return;
    }

    if (!random_seeded) {
        srand((unsigned int) time(NULL));
        random_seeded = true;
    }

    size_t charset_size = sizeof(charset) - 1;

    for (size_t i = 0; i + 1 < size; i++) {
        peer_id[i] = charset[rand() % charset_size];
    }

    peer_id[size - 1] = '\0';
}

// Add a received chat message safely
static void jsr_add_chat_message(
    jsr_network *net,
    const char *username,
    size_t username_len,
    const char *message,
    size_t message_len
) {
    if (net == NULL ||
        net->chat == NULL ||
        username == NULL ||
        message == NULL) {
        return;
    }

    if (username_len >= TCHAT_USERNAME_MAX) {
        username_len = TCHAT_USERNAME_MAX - 1;
    }

    if (message_len >= TCHAT_MESSAGE_MAX_LEN) {
        message_len = TCHAT_MESSAGE_MAX_LEN - 1;
    }

    char safe_username[TCHAT_USERNAME_MAX];

    memcpy(safe_username, username, username_len);
    safe_username[username_len] = '\0';

    // Do not show our own message a second time if the relay echoes it back.
    if (strcmp(safe_username, net->username) == 0) {
        return;
    }

    // Remove the oldest message when the chat is full.
    if (net->chat->message_count >= TCHAT_MAX_MESSAGES) {
        memmove(
            &net->chat->messages[0],
            &net->chat->messages[1],
            sizeof(tchat_message) * (TCHAT_MAX_MESSAGES - 1)
        );

        net->chat->message_count = TCHAT_MAX_MESSAGES - 1;
    }

    if (net->chat->message_count < 0) {
        net->chat->message_count = 0;
    }

    tchat_message *chat_message =
        &net->chat->messages[net->chat->message_count++];

    memset(chat_message, 0, sizeof(*chat_message));

    memcpy(
        chat_message->username,
        safe_username,
        username_len
    );

    chat_message->username[username_len] = '\0';

   