#include "thermite/tchat.h"
#include "thermite/jsr_network.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ============================================================================
// Railway JSR relay configuration
// ============================================================================

#define JSR_RELAY_HOST \
    "jsr-relay-server-production.up.railway.app"

#define JSR_RELAY_PORT 443

// ============================================================================
// Internal helpers
// ============================================================================

// Generate a random uppercase alphanumeric team key.
static void generate_random_key(char *buffer, int length) {
    static const char charset[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";

    int charset_size = (int) sizeof(charset) - 1;

    if (buffer == NULL || length <= 0) {
        return;
    }

    for (int i = 0; i < length; i++) {
        buffer[i] = charset[rand() % charset_size];
    }

    buffer[length] = '\0';
}

// Add a system message safely.
static void add_system_message(
    tchat_system *chat,
    const char *message
) {
    tchat_message *msg;

    if (chat == NULL || message == NULL) {
        return;
    }

    if (chat->message_count >= TCHAT_MAX_MESSAGES) {
        memmove(
            &chat->messages[0],
            &chat->messages[1],
            sizeof(tchat_message) *
                (TCHAT_MAX_MESSAGES - 1)
        );

        chat->message_count =
            TCHAT_MAX_MESSAGES - 1;
    }

    if (chat->message_count < 0) {
        chat->message_count = 0;
    }

    msg = &chat->messages[
        chat->message_count++
    ];

    memset(msg, 0, sizeof(*msg));

    strncpy(
        msg->username,
        "[SYSTEM]",
        TCHAT_USERNAME_MAX - 1
    );

    msg->username[
        TCHAT_USERNAME_MAX - 1
    ] = '\0';

    strncpy(
        msg->message,
        message,
        TCHAT_MESSAGE_MAX_LEN - 1
    );

    msg->message[
        TCHAT_MESSAGE_MAX_LEN - 1
    ] = '\0';

    msg->timestamp = time(NULL);
    msg->is_system_message = true;

    chat->should_scroll_to_bottom = true;

    if (chat->on_message_received != NULL) {
        chat->on_message_received(msg);
    }
}

// Get the JSR network stored in the chat context.
static jsr_network *get_network(
    tchat_system *chat
) {
    if (chat == NULL ||
        chat->network_context == NULL) {
        return NULL;
    }

    return (jsr_network *)
        chat->network_context;
}

// Create the network object if it does not exist.
static jsr_network *ensure_network(
    tchat_system *chat
) {
    jsr_network *network;

    if (chat == NULL) {
        return NULL;
    }

    network = get_network(chat);

    if (network != NULL) {
        return network;
    }

    network = jsr_network_create(
        JSR_RELAY_HOST,
        JSR_RELAY_PORT
    );

    if (network == NULL) {
        add_system_message(
            chat,
            "ERROR: Could not create JSR network"
        );

        return NULL;
    }

    // Connect the network object to this chat.
    network->chat = chat;

    chat->network_context = network;

    return network;
}

// Connect the chat to Railway.
static bool connect_to_relay(
    tchat_system *chat
) {
    jsr_network *network;

    if (chat == NULL ||
        chat->team_key[0] == '\0' ||
        chat->local_username[0] == '\0') {
        return false;
    }

    network = ensure_network(chat);

    if (network == NULL) {
        return false;
    }

    network->chat = chat;

    if (jsr_network_is_connected(network)) {
        return true;
    }

    if (!jsr_network_connect(
            network,
            chat->team_key,
            chat->local_username,
            ""
        )) {
        add_system_message(
            chat,
            "ERROR: Could not start relay connection"
        );

        return false;
    }

    add_system_message(
        chat,
        "Connecting to JSR relay..."
    );

    return true;
}

// ============================================================================
// Public API
// ============================================================================

tchat_system *tchat_create(
    const char *local_username
) {
    tchat_system *chat;

    if (local_username == NULL) {
        return NULL;
    }

    chat = (tchat_system *)
        calloc(1, sizeof(*chat));

    if (chat == NULL) {
        return NULL;
    }

    strncpy(
        chat->local_username,
        local_username,
        TCHAT_USERNAME_MAX - 1
    );

    chat->local_username[
        TCHAT_USERNAME_MAX - 1
    ] = '\0';

    chat->message_count = 0;
    chat->message_read_index = 0;
    chat->member_count = 0;

    chat->is_chat_open = false;
    chat->is_team_leader = false;
    chat->is_connected = false;

    chat->should_scroll_to_bottom = false;

    chat->network_context = NULL;

    memset(
        chat->team_key,
        0,
        sizeof(chat->team_key)
    );

    memset(
        chat->team_name,
        0,
        sizeof(chat->team_name)
    );

    memset(
        chat->input_buffer,
        0,
        sizeof(chat->input_buffer)
    );

    memset(
        chat->join_key_buffer,
        0,
        sizeof(chat->join_key_buffer)
    );

    srand((unsigned int) time(NULL));

    return chat;
}

void tchat_destroy(
    tchat_system *chat
) {
    jsr_network *network;

    if (chat == NULL) {
        return;
    }

    network = get_network(chat);

    if (network != NULL) {
        network->chat = NULL;

        jsr_network_destroy(network);

        chat->network_context = NULL;
    }

    free(chat);
}

const char *tchat_generate_team_key(
    tchat_system *chat,
    const char *team_name
) {
    tchat_member *leader;
    char sys_msg[256];

    if (chat == NULL ||
        team_name == NULL) {
        return NULL;
    }

    generate_random_key(
        chat->team_key,
        TCHAT_TEAM_KEY_LEN
    );

    strncpy(
        chat->team_name,
        team_name,
        sizeof(chat->team_name) - 1
    );

    chat->team_name[
        sizeof(chat->team_name) - 1
    ] = '\0';

    chat->is_team_leader = true;

    chat->member_count = 0;

    leader = &chat->members[
        chat->member_count++
    ];

    memset(
        leader,
        0,
        sizeof(*leader)
    );

    strncpy(
        leader->username,
        chat->local_username,
        TCHAT_USERNAME_MAX - 1
    );

    strncpy(
        leader->peer_id,
        "local",
        sizeof(leader->peer_id) - 1
    );

    leader->is_online = true;
    leader->joined_at = time(NULL);
    leader->last_seen = time(NULL);

    snprintf(
        sys_msg,
        sizeof(sys_msg),
        "Team '%s' created. Key: %s",
        chat->team_name,
        chat->team_key
    );

    add_system_message(
        chat,
        sys_msg
    );

    // Do not claim that the relay is connected yet.
    chat->is_connected = false;

    connect_to_relay(chat);

    return chat->team_key;
}

bool tchat_join_team(
    tchat_system *chat,
    const char *team_key
) {
    tchat_member *member;
    char sys_msg[256];

    if (chat == NULL ||
        team_key == NULL) {
        return false;
    }

    if (strlen(team_key) !=
        TCHAT_TEAM_KEY_LEN) {
        add_system_message(
            chat,
            "ERROR: Team key must be 8 characters"
        );

        return false;
    }

    for (
        int i = 0;
        i < TCHAT_TEAM_KEY_LEN;
        i++
    ) {
        char c = team_key[i];

        if (!(
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')
        )) {
            add_system_message(
                chat,
                "ERROR: Invalid team key"
            );

            return false;
        }
    }

    strncpy(
        chat->team_key,
        team_key,
        TCHAT_TEAM_KEY_LEN
    );

    chat->team_key[
        TCHAT_TEAM_KEY_LEN
    ] = '\0';

    chat->is_team_leader = false;

    chat->member_count = 0;

    if (chat->member_count <
        TCHAT_MAX_TEAM_MEMBERS) {
        member = &chat->members[
            chat->member_count++
        ];

        memset(
            member,
            0,
            sizeof(*member)
        );

        strncpy(
            member->username,
            chat->local_username,
            TCHAT_USERNAME_MAX - 1
        );

        strncpy(
            member->peer_id,
            "local",
            sizeof(member->peer_id) - 1
        );

        member->is_online = true;
        member->joined_at = time(NULL);
        member->last_seen = time(NULL);
    }

    chat->is_connected = false;

    snprintf(
        sys_msg,
        sizeof(sys_msg),
        "%s is joining the team...",
        chat->local_username
    );

    add_system_message(
        chat,
        sys_msg
    );

    return connect_to_relay(chat);
}

void tchat_leave_team(
    tchat_system *chat
) {
    jsr_network *network;
    char sys_msg[256];

    if (chat == NULL) {
        return;
    }

    snprintf(
        sys_msg,
        sizeof(sys_msg),
        "%s left the team",
        chat->local_username
    );

    add_system_message(
        chat,
        sys_msg
    );

    network = get_network(chat);

    if (network != NULL) {
        jsr_network_disconnect(network);
    }

    chat->is_connected = false;
    chat->is_team_leader = false;
    chat->member_count = 0;

    memset(
        chat->team_key,
        0,
        sizeof(chat->team_key)
    );

    memset(
        chat->team_name,
        0,
        sizeof(chat->team_name)
    );

    if (chat->on_connection_changed != NULL) {
        chat->on_connection_changed(false);
    }
}

bool tchat_send_message(
    tchat_system *chat,
    const char *message
) {
    tchat_message *msg;
    jsr_network *network;

    if (chat == NULL ||
        message == NULL) {
        return false;
    }

    if (message[0] == '\0') {
        return false;
    }

    if (strlen(message) >=
        TCHAT_MESSAGE_MAX_LEN) {
        return false;
    }

    network = get_network(chat);

    if (network == NULL) {
        add_system_message(
            chat,
            "ERROR: Network is not initialized"
        );

        return false;
    }

    // Add our message locally immediately.
    if (chat->message_count >=
        TCHAT_MAX_MESSAGES) {
        memmove(
            &chat->messages[0],
            &chat->messages[1],
            sizeof(tchat_message) *
                (TCHAT_MAX_MESSAGES - 1)
        );

        chat->message_count =
            TCHAT_MAX_MESSAGES - 1;
    }

    msg = &chat->messages[
        chat->message_count++
    ];

    memset(
        msg,
        0,
        sizeof(*msg)
    );

    strncpy(
        msg->username,
        chat->local_username,
        TCHAT_USERNAME_MAX - 1
    );

    strncpy(
        msg->message,
        message,
        TCHAT_MESSAGE_MAX_LEN - 1
    );

    msg->timestamp = time(NULL);
    msg->is_system_message = false;

    chat->should_scroll_to_bottom = true;

    if (chat->on_message_received != NULL) {
        chat->on_message_received(msg);
    }

    // Send to Railway.
    if (!jsr_network_send_message(
            network,
            message
        )) {
        add_system_message(
            chat,
            "ERROR: Message could not be sent"
        );

        return false;
    }

    return true;
}

const tchat_message *tchat_get_messages(
    tchat_system *chat,
    int *out_count
) {
    if (chat == NULL ||
        out_count == NULL) {
        return NULL;
    }

    *out_count =
        chat->message_count;

    return chat->messages;
}

const tchat_member *tchat_get_members(
    tchat_system *chat,
    int *out_count
) {
    if (chat == NULL ||
        out_count == NULL) {
        return NULL;
    }

    *out_count =
        chat->member_count;

    return chat->members;
}

int tchat_get_unread_count(
    tchat_system *chat
) {
    int unread;

    if (chat == NULL) {
        return 0;
    }

    unread =
        chat->message_count -
        chat->message_read_index;

    return unread > 0 ? unread : 0;
}

void tchat_mark_all_read(
    tchat_system *chat
) {
    if (chat == NULL) {
        return;
    }

    chat->message_read_index =
        chat->message_count;
}

void tchat_update(
    tchat_system *chat,
    float delta_time
) {
    jsr_network *network;

    if (chat == NULL) {
        return;
    }

    network = get_network(chat);

    if (network == NULL) {
        return;
    }

    network->chat = chat;

    // This processes WebSocket events every frame.
    jsr_network_update(
        network,
        delta_time
    );

    // Keep chat status synchronized with the real
    // WebSocket connection state.
    chat->is_connected =
        jsr_network_is_connected(network);
}

void tchat_set_network_context(
    tchat_system *chat,
    void *context
) {
    if (chat == NULL) {
        return;
    }

    chat->network_context = context;

    if (context != NULL) {
        jsr_network *network =
            (jsr_network *) context;

        network->chat = chat;
    }
}

void tchat_set_on_message_callback(
    tchat_system *chat,
    void (*callback)(
        tchat_message *
    )
) {
    if (chat == NULL) {
        return;
    }

    chat->on_message_received =
        callback;
}

void tchat_set_on_member_joined_callback(
    tchat_system *chat,
    void (*callback)(
        tchat_member *
    )
) {
    if (chat == NULL) {
        return;
    }

    chat->on_member_joined =
        callback;
}

void tchat_set_on_member_left_callback(
    tchat_system *chat,
    void (*callback)(
        const char *
    )
) {
    if (chat == NULL) {
        return;
    }

    chat->on_member_left =
        callback;
}

void tchat_set_on_connection_callback(
    tchat_system *chat,
    void (*callback)(
        bool
    )
) {
    if (chat == NULL) {
        return;
    }

    chat->on_connection_changed =
        callback;
}