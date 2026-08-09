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
// JSR Railway relay
// ============================================================================

#define JSR_RAILWAY_HOST \
    "jsr-relay-server-production.up.railway.app"

// ============================================================================
// Internal helpers
// ============================================================================

static size_t jsr_safe_strlen(
    const char *text,
    size_t max_len
) {
    size_t length = 0;

    if (text == NULL) {
        return 0;
    }

    while (length < max_len &&
           text[length] != '\0') {
        length++;
    }

    return length;
}

static void jsr_network_send_join(jsr_network *net);

static void jsr_generate_peer_id(
    char *peer_id,
    size_t size
) {
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
        peer_id[i] =
            charset[rand() % charset_size];
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
    char safe_username[TCHAT_USERNAME_MAX];

    if (net == NULL ||
        net->chat == NULL ||
        username == NULL ||
        message == NULL) {
        return;
    }

    if (username_len >= TCHAT_USERNAME_MAX) {
        username_len =
            TCHAT_USERNAME_MAX - 1;
    }

    if (message_len >=
        TCHAT_MESSAGE_MAX_LEN) {
        message_len =
            TCHAT_MESSAGE_MAX_LEN - 1;
    }

    memcpy(
        safe_username,
        username,
        username_len
    );

    safe_username[username_len] = '\0';

    /*
     * Do not add our own message twice if the
     * relay sends the message back to us.
     */
    if (strcmp(
            safe_username,
            net->username
        ) == 0) {
        return;
    }

    /*
     * Remove the oldest message if the list
     * is already full.
     */
    if (net->chat->message_count >=
        TCHAT_MAX_MESSAGES) {
        memmove(
            &net->chat->messages[0],
            &net->chat->messages[1],
            sizeof(tchat_message) *
                (TCHAT_MAX_MESSAGES - 1)
        );

        net->chat->message_count =
            TCHAT_MAX_MESSAGES - 1;
    }

    if (net->chat->message_count < 0) {
        net->chat->message_count = 0;
    }

    chat_message =
        &net->chat->messages[
            net->chat->message_count++
        ];

    memset(
        chat_message,
        0,
        sizeof(*chat_message)
    );

    memcpy(
        chat_message->username,
        safe_username,
        username_len
    );

    chat_message->username[
        username_len
    ] = '\0';

    memcpy(
        chat_message->message,
        message,
        message_len
    );

    chat_message->message[
        message_len
    ] = '\0';

    chat_message->timestamp =
        time(NULL);

    chat_message->is_system_message =
        false;

    if (net->chat->on_message_received
        != NULL) {
        net->chat->on_message_received(
            chat_message
        );
    }
}

// ============================================================================
// Online roster tracking
// ============================================================================

#define JSR_ROSTER_MAX 64

static int jsr_roster_find(
    jsr_network *net,
    const char *name,
    size_t name_len
) {
    int i;

    for (i = 0; i < net->roster_count; i++) {
        if (
            jsr_safe_strlen(
                net->roster[i],
                sizeof(net->roster[i]) - 1
            ) == name_len &&
            strncmp(
                net->roster[i],
                name,
                name_len
            ) == 0
        ) {
            return i;
        }
    }

    return -1;
}

static void jsr_roster_add(
    jsr_network *net,
    const char *name,
    size_t name_len,
    const char *owner_name,
    size_t owner_len
) {
    if (name_len == 0 ||
        name_len >= sizeof(net->roster[0])) {
        return;
    }

    if (
        jsr_roster_find(net, name, name_len) >= 0
    ) {
        /* Already tracked -- avoid duplicates. */
        return;
    }

    if (net->roster_count >= JSR_ROSTER_MAX) {
        return;
    }

    memcpy(
        net->roster[net->roster_count],
        name,
        name_len
    );

    net->roster[net->roster_count][name_len] =
        '\0';

    if (owner_name != NULL) {
        if (owner_len >=
            sizeof(net->roster_owner[0])) {
            owner_len =
                sizeof(net->roster_owner[0]) - 1;
        }

        memcpy(
            net->roster_owner[net->roster_count],
            owner_name,
            owner_len
        );

        net->roster_owner[
            net->roster_count
        ][owner_len] = '\0';
    } else {
        net->roster_owner[
            net->roster_count
        ][0] = '\0';
    }

    net->roster_count++;
}

static void jsr_roster_remove(
    jsr_network *net,
    const char *name,
    size_t name_len
) {
    int index =
        jsr_roster_find(net, name, name_len);

    if (index < 0) {
        return;
    }

    memmove(
        &net->roster[index],
        &net->roster[index + 1],
        sizeof(net->roster[0]) *
            (net->roster_count - index - 1)
    );

    memmove(
        &net->roster_owner[index],
        &net->roster_owner[index + 1],
        sizeof(net->roster_owner[0]) *
            (net->roster_count - index - 1)
    );

    net->roster_count--;
}

// ============================================================================
// Location tracking
// ============================================================================

#define JSR_LOCATION_MAX 64

static int jsr_location_find(
    jsr_network *net,
    const char *name,
    size_t name_len
) {
    int i;

    for (i = 0; i < net->location_count; i++) {
        if (
            jsr_safe_strlen(
                net->locations[i].username,
                sizeof(net->locations[i].username) - 1
            ) == name_len &&
            strncmp(
                net->locations[i].username,
                name,
                name_len
            ) == 0
        ) {
            return i;
        }
    }

    return -1;
}

static void jsr_location_set(
    jsr_network *net,
    const char *name,
    size_t name_len,
    const char *server_ip,
    size_t server_ip_len,
    float x,
    float y
) {
    int index;

    if (name_len == 0 ||
        name_len >= sizeof(net->locations[0].username)) {
        return;
    }

    if (
        server_ip_len >=
        sizeof(net->locations[0].server_ip)
    ) {
        server_ip_len =
            sizeof(net->locations[0].server_ip) - 1;
    }

    index =
        jsr_location_find(net, name, name_len);

    if (index < 0) {
        if (net->location_count >= JSR_LOCATION_MAX) {
            return;
        }

        index = net->location_count;
        net->location_count++;

        memcpy(
            net->locations[index].username,
            name,
            name_len
        );

        net->locations[index].username[name_len] =
            '\0';
    }

    memcpy(
        net->locations[index].server_ip,
        server_ip,
        server_ip_len
    );

    net->locations[index].server_ip[server_ip_len] =
        '\0';

    net->locations[index].x = x;
    net->locations[index].y = y;
}

static void jsr_location_remove(
    jsr_network *net,
    const char *name,
    size_t name_len
) {
    int index =
        jsr_location_find(net, name, name_len);

    if (index < 0) {
        return;
    }

    memmove(
        &net->locations[index],
        &net->locations[index + 1],
        sizeof(net->locations[0]) *
            (net->location_count - index - 1)
    );

    net->location_count--;
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
        data == NULL ||
        data_len == 0) {
        return;
    }

    message_type =
        (uint8_t) data[0];

    /*
     * Chat format:
     *
     * [CHAT]
     * [username length]
     * [username]
     * [message]
     */
    if (message_type ==
        JSR_MSG_TYPE_CHAT) {
        size_t username_len;
        const char *username;
        const char *message;
        size_t message_len;

        if (data_len < 2) {
            return;
        }

        username_len =
            (uint8_t) data[1];

        if (username_len == 0 ||
            username_len >
                data_len - 2) {
            return;
        }

        username =
            data + 2;

        message =
            username + username_len;

        message_len =
            data_len -
            2 -
            username_len;

        if (message_len == 0) {
            return;
        }

        jsr_add_chat_message(
            net,
            username,
            username_len,
            message,
            message_len
        );
    }
    else if (
        message_type == 6
    ) {
        /*
         * The deployed relay's actual PONG byte is 6, not
         * this client's own JSR_MSG_TYPE_PONG (8).
         */
        net->last_ping =
            time(NULL);

        net->last_message_time =
            time(NULL);
    }
    else if (
        message_type == 2 ||
        message_type == 3
    ) {
        /*
         * The deployed relay's "user joined" (2) / "user
         * left" (3) notices:
         * [type][name_len][name][owner_len][owner]
         */
        size_t name_len;
        size_t owner_offset;
        size_t owner_len;
        char text[TCHAT_MESSAGE_MAX_LEN];
        int written;

        if (data_len < 2) {
            return;
        }

        name_len =
            (uint8_t) data[1];

        if (name_len == 0 ||
            name_len > data_len - 2) {
            return;
        }

        owner_offset = 2 + name_len;
        owner_len = 0;

        if (owner_offset < data_len) {
            owner_len =
                (uint8_t) data[owner_offset];

            if (
                owner_offset + 1 + owner_len >
                data_len
            ) {
                owner_len = 0;
            }
        }

        if (message_type == 2) {
            jsr_roster_add(
                net,
                data + 2,
                name_len,
                owner_len > 0 ?
                    data + owner_offset + 1 :
                    NULL,
                owner_len
            );
        } else {
            jsr_roster_remove(
                net,
                data + 2,
                name_len
            );

            jsr_location_remove(
                net,
                data + 2,
                name_len
            );
        }

        written =
            snprintf(
                text,
                sizeof(text),
                "%.*s %s public chat",
                (int) name_len,
                data + 2,
                message_type == 2 ?
                    "joined" : "left"
            );

        if (written > 0) {
            jsr_add_chat_message(
                net,
                "[SYSTEM]",
                8,
                text,
                (size_t) written <
                    sizeof(text) ?
                    (size_t) written :
                    sizeof(text) - 1
            );
        }
    }
    else if (message_type == 7) {
        /*
         * LOCATION broadcast, relayed by the server as:
         * [7][username_len][username][x:f32][y:f32]
         * [server_ip_len][server_ip]
         */
        size_t offset;
        size_t name_len;
        float x;
        float y;
        size_t srv_len;

        offset = 1;

        if (data_len < offset + 1) {
            return;
        }

        name_len =
            (uint8_t) data[offset];
        offset += 1;

        if (data_len < offset + name_len) {
            return;
        }

        const char *name = data + offset;
        offset += name_len;

        if (data_len < offset + 8) {
            return;
        }

        memcpy(&x, data + offset, 4);
        offset += 4;

        memcpy(&y, data + offset, 4);
        offset += 4;

        if (data_len < offset + 1) {
            return;
        }

        srv_len =
            (uint8_t) data[offset];
        offset += 1;

        if (data_len < offset + srv_len) {
            return;
        }

        const char *srv = data + offset;

        bool is_self =
            jsr_safe_strlen(
                net->username,
                sizeof(net->username) - 1
            ) == name_len &&
            strncmp(
                net->username,
                name,
                name_len
            ) == 0;

        if (
            name_len > 0 &&
            !is_self
        ) {
            /*
             * Only track OTHER players' positions -- we
             * already know our own.
             */
            jsr_location_set(
                net,
                name,
                name_len,
                srv,
                srv_len,
                x,
                y
            );
        }
    }
    else if (message_type ==
             JSR_MSG_TYPE_ERROR) {
        size_t error_len;

        if (data_len <= 1) {
            return;
        }

        error_len =
            data_len - 1;

        if (error_len >=
            sizeof(net->last_error)) {
            error_len =
                sizeof(net->last_error) - 1;
        }

        memcpy(
            net->last_error,
            data + 1,
            error_len
        );

        net->last_error[error_len] =
            '\0';

        /*
         * The only ERROR the deployed relay currently sends
         * is a bad/revoked access key rejection, right
         * before it closes the connection.
         */
        net->auth_rejected = true;

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

    net =
        (jsr_network *)
        connection->fn_data;

    if (net == NULL) {
        return;
    }

    if (event == MG_EV_OPEN) {
        printf(
            "JSR: TCP connection opened\n"
        );
    }
    else if (event ==
             MG_EV_CONNECT) {
        /*
         * Railway only accepts TLS on port 443. Without
         * this, mongoose silently falls back to plaintext
         * for wss:// connections (see the "user did not
         * call mg_tls_init()" checks in mongoose.c), and
         * the handshake against Railway's TLS-only edge
         * fails every time.
         */
        struct mg_tls_opts tls_opts = {
            .skip_verification = 1
        };

        mg_tls_init(
            connection,
            &tls_opts
        );

        printf(
            "JSR: TLS handshake started\n"
        );
    }
    else if (event ==
             MG_EV_WS_OPEN) {
        printf(
            "JSR: Connected to Railway relay\n"
        );

        net->is_connected = true;

        net->consecutive_failures = 0;

        net->reconnect_backoff =
            1.0;

        net->last_message_time =
            time(NULL);

        if (net->chat != NULL &&
            net->chat
                ->on_connection_changed
                != NULL) {
            net->chat
                ->on_connection_changed(
                    true
                );
        }

        /*
         * Register our username with the relay so it can
         * tag our outgoing CHAT messages. Without this the
         * server silently drops every message we send.
         */
        jsr_network_send_join(net);
    }
    else if (event ==
             MG_EV_WS_MSG) {
        struct mg_ws_message
            *message;

        message =
            (struct mg_ws_message *)
            event_data;

        if (message != NULL) {
            jsr_parse_message(
                net,
                (const char *)
                    message->data.buf,
                message->data.len
            );

            net->last_message_time =
                time(NULL);
        }
    }
    else if (event ==
             MG_EV_ERROR) {
        printf(
            "JSR: Railway connection error\n"
        );

        net->is_connected = false;

        net->consecutive_failures++;

        net->ws_connection = NULL;

        if (net->chat != NULL &&
            net->chat
                ->on_connection_changed
                != NULL) {
            net->chat
                ->on_connection_changed(
                    false
                );
        }
    }
    else if (event ==
             MG_EV_CLOSE) {
        printf(
            "JSR: Railway connection closed\n"
        );

        net->is_connected = false;

        net->ws_connection = NULL;

        if (net->chat != NULL &&
            net->chat
                ->on_connection_changed
                != NULL) {
            net->chat
                ->on_connection_changed(
                    false
                );
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

    /*
     * relay_host and relay_port are kept in
     * the function parameters because the
     * header/API may require them.
     */
    (void) relay_host;
    (void) relay_port;

    net = calloc(
        1,
        sizeof(*net)
    );

    if (net == NULL) {
        return NULL;
    }

    net->mgr = calloc(
        1,
        sizeof(*net->mgr)
    );

    if (net->mgr == NULL) {
        free(net);
        return NULL;
    }

    mg_mgr_init(
        net->mgr
    );

    /*
     * Always use the Railway domain.
     */
    strncpy(
        net->relay_host,
        JSR_RAILWAY_HOST,
        sizeof(net->relay_host) - 1
    );

    net->relay_host[
        sizeof(net->relay_host) - 1
    ] = '\0';

    /*
     * Railway uses secure WebSocket over
     * the normal HTTPS port.
     */
    net->relay_port = 443;

    snprintf(
        net->relay_url,
        sizeof(net->relay_url),
        "wss://%s/global",
        net->relay_host
    );

    printf(
        "JSR relay URL: %s\n",
        net->relay_url
    );

    net->ping_interval =
        30.0;

    net->connection_timeout =
        60.0;

    net->reconnect_backoff =
        1.0;

    net->last_ping =
        time(NULL);

    net->last_message_time =
        time(NULL);

    jsr_generate_peer_id(
        net->peer_id,
        sizeof(net->peer_id)
    );

    return net;
}

void jsr_network_destroy(
    jsr_network *net
) {
    if (net == NULL) {
        return;
    }

    jsr_network_disconnect(
        net
    );

    if (net->mgr != NULL) {
        mg_mgr_free(
            net->mgr
        );

        free(
            net->mgr
        );

        net->mgr = NULL;
    }

    free(net);
}

bool jsr_network_connect(
    jsr_network *net,
    const char *team_key,
    const char *username,
    const char *auth_key
) {
    struct mg_connection
        *connection;

    if (net == NULL ||
        net->mgr == NULL ||
        team_key == NULL ||
        username == NULL ||
        auth_key == NULL) {
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

    strncpy(
        net->auth_key,
        auth_key,
        sizeof(net->auth_key) - 1
    );

    net->auth_key[
        sizeof(net->auth_key) - 1
    ] = '\0';

    net->auth_rejected = false;
    net->last_error[0] = '\0';

    /*
     * The relay only tells us about OTHER users joining --
     * we have to add ourselves to the roster locally.
     */
    jsr_roster_add(
        net,
        net->username,
        jsr_safe_strlen(
            net->username,
            sizeof(net->username) - 1
        ),
        NULL,
        0
    );

    printf(
        "JSR: Connecting to %s\n",
        net->relay_url
    );

    connection =
        mg_ws_connect(
            net->mgr,
            net->relay_url,
            jsr_websocket_handler,
            net,
            NULL
        );

    if (connection == NULL) {
        printf(
            "JSR: Could not create "
            "WebSocket connection\n"
        );

        net->consecutive_failures++;

        return false;
    }

    net->ws_connection =
        connection;

    net->last_reconnect_attempt =
        time(NULL);

    return true;
}

void jsr_network_disconnect(
    jsr_network *net
) {
    if (net == NULL) {
        return;
    }

    if (net->ws_connection != NULL) {
        net->ws_connection
            ->is_closing = 1;

        net->ws_connection = NULL;
    }

    net->is_connected = false;
}

bool jsr_network_send_message(
    jsr_network *net,
    const char *message
) {
    size_t message_len;
    size_t username_len;

    /*
     * Wire format expected by the deployed relay for
     * client -> server CHAT is just [CHAT][message] --
     * the server tags the sender's username itself based
     * on the JOIN handshake, and re-broadcasts messages as
     * [CHAT][username_len][username][message] (see
     * jsr_parse_message()).
     */
    uint8_t buffer[
        1 + TCHAT_MESSAGE_MAX_LEN
    ];

    if (net == NULL ||
        message == NULL) {
        return false;
    }

    message_len =
        jsr_safe_strlen(
            message,
            TCHAT_MESSAGE_MAX_LEN - 1
        );

    if (message_len == 0) {
        return false;
    }

    username_len =
        jsr_safe_strlen(
            net->username,
            TCHAT_USERNAME_MAX - 1
        );

    if (username_len == 0) {
        return false;
    }

    if (!net->is_connected ||
        net->ws_connection == NULL) {
        if (net->pending_count >= 10) {
            return false;
        }

        strncpy(
            net->pending_messages[
                net->pending_count
            ],
            message,
            255
        );

        net->pending_messages[
            net->pending_count
        ][255] = '\0';

        net->pending_count++;

        return true;
    }

    buffer[0] =
        JSR_MSG_TYPE_CHAT;

    memcpy(
        buffer + 1,
        message,
        message_len
    );

    mg_ws_send(
        net->ws_connection,
        buffer,
        1 + message_len,
        WEBSOCKET_OP_BINARY
    );

    return true;
}

void jsr_network_update(
    jsr_network *net,
    float delta_time
) {
    time_t now;

    (void) delta_time;

    if (net == NULL ||
        net->mgr == NULL) {
        return;
    }

    mg_mgr_poll(
        net->mgr,
        0
    );

    now = time(NULL);

    if (net->is_connected &&
        difftime(
            now,
            net->last_message_time
        ) >
        net->connection_timeout) {
        printf(
            "JSR: Connection timed out\n"
        );

        jsr_network_disconnect(
            net
        );
    }

    if (net->is_connected &&
        difftime(
            now,
            net->last_ping
        ) >
        net->ping_interval &&
        net->ws_connection != NULL) {
        /*
         * The deployed relay expects client PING as raw
         * byte 4 (it replies with byte 6), which does not
         * match this client's own JSR_MSG_TYPE_PING (7)/
         * JSR_MSG_TYPE_PONG (8) enum values.
         */
        uint8_t ping = 4;

        mg_ws_send(
            net->ws_connection,
            &ping,
            1,
            WEBSOCKET_OP_BINARY
        );

        net->last_ping =
            now;
    }

    if (net->is_connected &&
        net->pending_count > 0) {
        int count =
            net->pending_count;

        net->pending_count = 0;

        for (int i = 0;
             i < count;
             i++) {
            jsr_network_send_message(
                net,
                net->pending_messages[i]
            );
        }
    }
}

bool jsr_network_is_connected(
    jsr_network *net
) {
    return net != NULL &&
           net->is_connected;
}

bool jsr_network_is_auth_rejected(
    jsr_network *net
) {
    return net != NULL &&
           net->auth_rejected;
}

const char *jsr_network_get_last_error(
    jsr_network *net
) {
    if (net == NULL) {
        return "";
    }

    return net->last_error;
}

int jsr_network_pending_count(
    jsr_network *net
) {
    if (net == NULL) {
        return 0;
    }

    return net->pending_count;
}

int jsr_network_roster_count(
    jsr_network *net
) {
    if (net == NULL) {
        return 0;
    }

    return net->roster_count;
}

bool jsr_network_roster_name(
    jsr_network *net,
    int index,
    char *out_name,
    size_t out_size
) {
    if (net == NULL ||
        out_name == NULL ||
        out_size == 0 ||
        index < 0 ||
        index >= net->roster_count) {
        return false;
    }

    strncpy(
        out_name,
        net->roster[index],
        out_size - 1
    );

    out_name[out_size - 1] = '\0';

    return true;
}

bool jsr_network_roster_owner(
    jsr_network *net,
    int index,
    char *out_name,
    size_t out_size
) {
    if (net == NULL ||
        out_name == NULL ||
        out_size == 0 ||
        index < 0 ||
        index >= net->roster_count) {
        return false;
    }

    strncpy(
        out_name,
        net->roster_owner[index],
        out_size - 1
    );

    out_name[out_size - 1] = '\0';

    return true;
}

bool jsr_network_send_location(
    jsr_network *net,
    float x,
    float y,
    const char *server_ip
) {
    uint8_t buffer[1 + 4 + 4 + 1 + 64];
    size_t srv_len;
    size_t offset;

    if (net == NULL ||
        server_ip == NULL ||
        !net->is_connected ||
        net->ws_connection == NULL) {
        return false;
    }

    srv_len =
        jsr_safe_strlen(
            server_ip,
            64 - 1
        );

    buffer[0] = 7;
    offset = 1;

    memcpy(buffer + offset, &x, 4);
    offset += 4;

    memcpy(buffer + offset, &y, 4);
    offset += 4;

    buffer[offset] = (uint8_t) srv_len;
    offset += 1;

    memcpy(
        buffer + offset,
        server_ip,
        srv_len
    );
    offset += srv_len;

    mg_ws_send(
        net->ws_connection,
        buffer,
        offset,
        WEBSOCKET_OP_BINARY
    );

    return true;
}

int jsr_network_location_count(
    jsr_network *net
) {
    if (net == NULL) {
        return 0;
    }

    return net->location_count;
}

bool jsr_network_get_location(
    jsr_network *net,
    int index,
    char *out_username,
    size_t username_size,
    char *out_server_ip,
    size_t server_ip_size,
    float *out_x,
    float *out_y
) {
    if (net == NULL ||
        index < 0 ||
        index >= net->location_count) {
        return false;
    }

    if (out_username != NULL &&
        username_size > 0) {
        strncpy(
            out_username,
            net->locations[index].username,
            username_size - 1
        );

        out_username[username_size - 1] = '\0';
    }

    if (out_server_ip != NULL &&
        server_ip_size > 0) {
        strncpy(
            out_server_ip,
            net->locations[index].server_ip,
            server_ip_size - 1
        );

        out_server_ip[server_ip_size - 1] = '\0';
    }

    if (out_x != NULL) {
        *out_x = net->locations[index].x;
    }

    if (out_y != NULL) {
        *out_y = net->locations[index].y;
    }

    return true;
}

/*
 * The deployed relay (jsr-relay-server) is a flat global
 * room with no team/peer concept. Its JOIN wire format is:
 * [5][username_len][username]
 * This is what actually registers ws.username server-side;
 * without it every CHAT message we send gets silently
 * dropped by the server.
 */
static void jsr_network_send_join(
    jsr_network *net
) {
    uint8_t buffer[
        2 + 96 +
        2 + TCHAT_USERNAME_MAX
    ];
    size_t username_len;
    size_t key_len;
    size_t offset;

    if (net == NULL ||
        net->ws_connection == NULL) {
        return;
    }

    username_len =
        jsr_safe_strlen(
            net->username,
            TCHAT_USERNAME_MAX - 1
        );

    if (username_len == 0) {
        return;
    }

    key_len =
        jsr_safe_strlen(
            net->auth_key,
            sizeof(net->auth_key) - 1
        );

    buffer[0] = 5;
    buffer[1] = (uint8_t) key_len;
    offset = 2;

    memcpy(
        buffer + offset,
        net->auth_key,
        key_len
    );
    offset += key_len;

    buffer[offset] = (uint8_t) username_len;
    offset += 1;

    memcpy(
        buffer + offset,
        net->username,
        username_len
    );
    offset += username_len;

    mg_ws_send(
        net->ws_connection,
        buffer,
        offset,
        WEBSOCKET_OP_BINARY
    );

    printf(
        "JSR: Sent join as %s\n",
        net->username
    );
}

void jsr_network_request_sync(
    jsr_network *net
) {
    uint8_t buffer[256];

    size_t position = 0;
    size_t team_len;
    size_t username_len;
    size_t peer_len;

    if (net == NULL ||
        net->ws_connection == NULL) {
        return;
    }

    team_len =
        jsr_safe_strlen(
            net->team_key,
            sizeof(net->team_key) - 1
        );

    username_len =
        jsr_safe_strlen(
            net->username,
            sizeof(net->username) - 1
        );

    peer_len =
        jsr_safe_strlen(
            net->peer_id,
            sizeof(net->peer_id) - 1
        );

    /*
     * The three lengths plus the message
     * type must fit inside buffer.
     */
    if (team_len == 0 ||
        username_len == 0 ||
        peer_len == 0 ||
        team_len > 255 ||
        username_len > 255 ||
        peer_len > 255 ||
        4 +
        team_len +
        username_len +
        peer_len >
        sizeof(buffer)) {
        return;
    }

    buffer[position++] =
        JSR_MSG_TYPE_SYNC;

    buffer[position++] =
        (uint8_t) team_len;

    memcpy(
        buffer + position,
        net->team_key,
        team_len
    );

    position += team_len;

    buffer[position++] =
        (uint8_t) username_len;

    memcpy(
        buffer + position,
        net->username,
        username_len
    );

    position += username_len;

    buffer[position++] =
        (uint8_t) peer_len;

    memcpy(
        buffer + position,
        net->peer_id,
        peer_len
    );

    position += peer_len;

    mg_ws_send(
        net->ws_connection,
        buffer,
        position,
        WEBSOCKET_OP_BINARY
    );

    printf(
        "JSR: Sent team sync\n"
    );
}

void jsr_network_set_timeout(
    jsr_network *net,
    double timeout_seconds
) {
    if (net == NULL ||
        timeout_seconds <= 0.0) {
        return;
    }

    net->connection_timeout =
        timeout_seconds;
}

void jsr_network_set_ping_interval(
    jsr_network *net,
    double interval_seconds
) {
    if (net == NULL ||
        interval_seconds <= 0.0) {
        return;
    }

    net->ping_interval =
        interval_seconds;
}

const char *jsr_network_get_url(
    jsr_network *net
) {
    if (net == NULL) {
        return "";
    }

    return net->relay_url;
}

const char *jsr_network_get_error(
    jsr_network *net
) {
    (void) net;

    return "";
}