#ifndef THERMITE_JSR_NETWORK_H
#define THERMITE_JSR_NETWORK_H

#include "tchat.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations for Mongoose.
struct mg_mgr;
struct mg_connection;

// JSR relay message types.
typedef enum {
    JSR_MSG_TYPE_CHAT = 1,
    JSR_MSG_TYPE_JOIN = 2,
    JSR_MSG_TYPE_LEAVE = 3,
    JSR_MSG_TYPE_MEMBER_LIST = 4,
    JSR_MSG_TYPE_ACK = 5,
    JSR_MSG_TYPE_SYNC = 6,
    JSR_MSG_TYPE_PING = 7,
    JSR_MSG_TYPE_PONG = 8,
    JSR_MSG_TYPE_ERROR = 9
} jsr_msg_type;

// JSR network context.
typedef struct jsr_network {
    // Mongoose event manager.
    struct mg_mgr *mgr;

    // Active WebSocket connection.
    struct mg_connection *ws_connection;

    // Connection status.
    bool is_connected;

    // Prevent automatic reconnect after a manual disconnect.
    bool manual_disconnect;

    // Local player information.
    char peer_id[32];
    char team_key[9];
    char username[32];

    // Relay information.
    char relay_url[256];
    char relay_host[128];
    int relay_port;

    // Last network error.
    char last_error[256];

    // Timing.
    double last_ping;
    double ping_interval;
    double connection_timeout;
    double last_message_time;

    // Reconnection.
    int consecutive_failures;
    double next_reconnect_time;
    double reconnect_backoff;
    double last_reconnect_attempt;

    // Messages waiting to be sent.
    char pending_messages[10][256];
    int pending_count;

    // Chat system connected to this network.
    tchat_system *chat;

} jsr_network;

// Create the JSR network system.
jsr_network *jsr_network_create(
    const char *relay_host,
    int relay_port
);

// Destroy the JSR network system.
void jsr_network_destroy(
    jsr_network *net
);

// Start connecting to the relay and join a team.
bool jsr_network_connect(
    jsr_network *net,
    const char *team_key,
    const char *username
);

// Disconnect from the relay.
void jsr_network_disconnect(
    jsr_network *net
);

// Send a team-chat message.
bool jsr_network_send_message(
    jsr_network *net,
    const char *message
);

// Process Mongoose events.
// This must be called repeatedly from the game's update loop.
void jsr_network_update(
    jsr_network *net,
    float delta_time
);

// Check whether the WebSocket is connected.
bool jsr_network_is_connected(
    jsr_network *net
);

// Get the number of queued messages.
int jsr_network_pending_count(
    jsr_network *net
);

// Send the team synchronization request.
void jsr_network_request_sync(
    jsr_network *net
);

// Set the connection timeout.
void jsr_network_set_timeout(
    jsr_network *net,
    double timeout_seconds
);

// Set the ping interval.
void jsr_network_set_ping_interval(
    jsr_network *net,
    double interval_seconds
);

// Get the relay URL.
const char *jsr_network_get_url(
    jsr_network *net
);

// Get the latest network error.
const char *jsr_network_get_error(
    jsr_network *net
);

#ifdef __cplusplus
}
#endif

#endif