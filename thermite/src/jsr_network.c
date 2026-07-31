#include "thermite/jsr_network.h"
#include "thermite/tchat.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../app/src/external/mongoose.h"

// ============================================================================
// Internal helpers
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

static void jsr_generate_peer_id(char *peer_id, size_t size) {
    static const char charset[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789";

    static bool random_seeded = false;
    size_t charset_size;

    if (peer_id == NULL || size == 0) {
        return;
    }

    if (!random_seeded) {
        srand((unsigned int) time(NULL));
        random_seeded = true;
    }

    charset_size = sizeof(charset) - 1;

    for (size_t i = 0; i + 1 < size; i++) {
        peer_id[i] = charset[rand() % charset_size];
    }

    peer_id[size - 1] = '\0';
}

static void jsr_add_chat_message(
    jsr_network *net,
    const char *username,
    size_t username_len,
    const char *message,
    size_t message_len
) {
    tchat_message *chat_message;

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

    if (strcmp(safe_username, net->username) == 0) {
        return;
    }

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

    chat_message =
        &net->chat->messages[net->chat->message_count++];

    memset(chat_message, 0, sizeof(*chat_message));

    memcpy(
        chat_message->username,
        safe_username,
        username_len
    );

    chat_message->username[username_len] = '\0';

    memcpy(
        chat_message->message,
        message,
        message_len
    );

    chat_message->message[message_len] = '\0';
    chat_message->timestamp = time(NULL);
    chat_message->is_system_message = false;

    if (net->chat->on_message_received != NULL) {
        net->chat->on_message_received(chat_message);
    }
}

// ============================================================================
// Message parser
// ============================================================================

static void jsr_parse_message(
    jsr_network *net,
    const char *data,
    size_t data_len
) {
    uint8_t message_type;

    if (net == NULL ||
        net->chat == NULL ||
        data == NULL ||
        data_len == 0) {
        return;
    }

    message_type = (uint8_t) data[0];

    if (message_type == JSR_MSG_TYPE_CHAT) {
        size_t username_len;
        const char *username;
        const char *message;
        size_t message_len;

        if (data_len < 2) {
            return;
        }

        username_len = (uint8_t) data[1];

        if (username_len == 0 ||
            username_len >= data_len - 1) {
            return;
        }

        username = data + 2;
        message = username + username_len;
        message_len = data_len - 2 - username_len;

        jsr_add_chat_message(
            net,
            username,
            username_len,
            message,
            message_len
        );
    }
    else if (message_type == JSR_MSG_TYPE_PONG) {
        net->last_ping = time(NULL);
        net->last_message_time = time(NULL);
    }
    else if (message_type == JSR_MSG_TYPE_ERROR) {
        size_t error_len = data_len - 1;

        printf(
            "JSR relay error: %.*s\n",
            (int) error_len,
            data + 1
        );
    }
}

// ============================================================================
// WebSocket callback
// ============================================================================

static void jsr_websocket_handler(
    struct mg_connection *connection,
    int event,
    void *event_data
) {
    jsr_network *net;

    if (connection == NULL) {
        return;
    }

    net = (jsr_network *) connection->fn_data;

    if (net == NULL) {
        return;
    }

    if (event == MG_EV_WS_OPEN) {
        net->is_connected = true;
        net->consecutive_failures = 0;
        net->reconnect_backoff = 1.0;
        net->last_message_time = time(NULL);

        if (net->chat != NULL &&
            net->chat->on_connection_changed != NULL) {
            net->chat->on_connection_changed(true);
        }

        jsr_network_request_sync(net);
    }
    else if (event == MG_EV_WS_MSG) {
        struct mg_ws_message *message =
            (struct mg_ws_message *) event_data;

        if (message != NULL) {
            jsr_parse_message(
                net,
                (const char *) message->data.buf,
                message->data.len
            );

            net->last_message_time = time(NULL);
        }
    }
    else if (event == MG_EV_ERROR) {
        net->is_connected = false;
        net->consecutive_failures++;

        if (net->chat != NULL &&
            net->chat->on_connection_changed != NULL) {
            net->chat->on_connection_changed(false);
        }
    }
    else if (event == MG_EV_CLOSE) {
        net->is_connected = false;
        net->ws_connection = NULL;

        if (net->chat != NULL &&
            net->chat->on_connection_changed != NULL) {
            net->chat->on_connection_changed(false);
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

jsr_network *jsr_network_create(
    const char *relay_host,
    int relay_port
) {
    jsr_network *net;

    if (relay_host == NULL || relay_port <= 0) {
        return NULL;
    }

    net = calloc(1, sizeof(*net));

    if (net == NULL) {
        return NULL;
    }

    net->mgr = calloc(1, sizeof(*net->mgr));

    if (net->mgr == NULL) {
        free(net);
        return NULL;
    }

    mg_mgr_init(net->mgr);

    strncpy(
        net->relay_host,
        relay_host,
        sizeof(net->relay_host) - 1
    );

    net->relay_host[
        sizeof(net->relay_host) - 1
    ] = '\0';

    net->relay_port = relay_port;

    snprintf(
        net->relay_url,
        sizeof(net->relay_url),
        "ws://%s:%d/jsr",
        net->relay_host,
        net->relay_port
    );

    net->ping_interval = 30.0;
    net->connection_timeout = 60.0;
    net->reconnect_backoff = 1.0;

    net->last_ping = time(NULL);
    net->last_message_time = time(NULL);

    jsr_generate_peer_id(
        net->peer_id,
        sizeof(net->peer_id)
    );

    return net;
}

void jsr_network_destroy(jsr_network *net) {
    if (net == NULL) {
        return;
    }

    jsr_network_disconnect(net);

    if (net->mgr != NULL) {
        mg_mgr_free(net->mgr);
        free(net->mgr);
        net->mgr = NULL;
    }

    free(net);
}

bool jsr_network_connect(
    jsr_network *net,
    const char *team_key,
    const char *username
) {
    struct mg_connection *connection;

    if (net == NULL ||
        net->mgr == NULL ||
        team_key == NULL ||
        username == NULL) {
        return false;
    }

    if (net->ws_connection != NULL) {
        return true;
    }

    strncpy(
        net->team_key,
        team_key,
        sizeof(net->team_key) - 1
    );

    net->team_key[
        sizeof(net->team_key) - 1
    ] = '\0';

    strncpy(
        net->username,
        username,
        sizeof(net->username) - 1
    );

    net->username[
        sizeof(net->username) - 1
    ] = '\0';

    connection = mg_ws_connect(
        net->mgr,
        net->relay_url,
        jsr_websocket_handler,
        net,
        NULL
    );

    if (connection == NULL) {
        net->consecutive_failures++;
        return false;
    }

    net->ws_connection = connection;
    net->last_reconnect_attempt = time(NULL);

    return true;
}

void jsr_network_disconnect(jsr_network *net) {
    if (net == NULL) {
        return;
    }

    if (net->ws_connection != NULL) {
        net->ws_connection->is_closing = 1;
        net->ws_connection = NULL;
    }

    net->is_connected = false;
}

bool jsr_network_send_message(
    jsr_network *net,
    const char *message
) {
    size_t message_len;
    uint8_t buffer[1 + TCHAT_MESSAGE_MAX_LEN];

    if (net == NULL || message == NULL) {
        return false;
    }

    message_len = jsr_safe_strlen(
        message,
        TCHAT_MESSAGE_MAX_LEN - 1
    );

    if (!net->is_connected ||
        net->ws_connection == NULL) {
        if (net->pending_count >= 10) {
            return false;
        }

        strncpy(
            net->pending_messages[net->pending_count],
            message,
            255
        );

        net->pending_messages[
            net->pending_count
        ][255] = '\0';

        net->pending_count++;

        return true;
    }