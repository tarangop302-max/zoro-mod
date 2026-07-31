#ifndef THERMITE_JSR_NETWORK_H
#define THERMITE_JSR_NETWORK_H

#include "tchat.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declare mongoose types
struct mg_mgr;
struct mg_connection;

// JSR (Jetstream Relay) Network message types
typedef enum {
    JSR_MSG_TYPE_CHAT = 1,           // Regular chat message
    JSR_MSG_TYPE_JOIN = 2,           // Player joined team
    JSR_MSG_TYPE_LEAVE = 3,          // Player left team
    JSR_MSG_TYPE_MEMBER_LIST = 4,    // Team member list update
    JSR_MSG_TYPE_ACK = 5,            // Acknowledgement
    JSR_MSG_TYPE_SYNC = 6,           // Full sync (team key, members, history)
    JSR_MSG_TYPE_PING = 7,           // Heartbeat/keepalive
    JSR_MSG_TYPE_PONG = 8,           // Ping response
    JSR_MSG_TYPE_ERROR = 9,          // Error message
} jsr_msg_type;

// Network context structure for JSR
typedef struct jsr_network {
    // Mongoose manager
    struct mg_mgr* mgr;
    
    // Connection state
    struct mg_connection* ws_connection;
    bool is_connected;
    
    // Local info
    char peer_id[32];
    char team_key[9];
    char username[32];
    
    // Relay server info
    char relay_url[256];
    int relay_port;
    char relay_host[128];
    
    // Timing
    double last_ping;
    double ping_interval;
    double connection_timeout;
    double last_message_time;
    
    // Retry logic
    int consecutive_failures;
    double next_reconnect_time;
    double reconnect_backoff;
    double last_reconnect_attempt;
    
    // Message queue
    char pending_messages[10][256];
    int pending_count;
    
    // Reference to chat system
    tchat_system* chat;
    
} jsr_network;

// ============================================================================
// JSR Network API
// ============================================================================

/**
 * Initialize JSR network system (creates mongoose manager)
 * @param relay_host Relay server host/IP (e.g., "localhost" or "relay.example.com")
 * @param relay_port Relay server port (e.g., 8080)
 * @return Network context
 */
jsr_network* jsr_network_create(const char* relay_host, int relay_port);

/**
 * Cleanup JSR network system
 */
void jsr_network_destroy(jsr_network* net);

/**
 * Connect to JSR relay and join team
 * @param net Network context
 * @param team_key Team key to join (8 chars)
 * @param username Local player username
 * @return true if connection initiated successfully
 */
bool jsr_network_connect(jsr_network* net, const char* team_key, const char* username);

/**
 * Disconnect from JSR relay
 */
void jsr_network_disconnect(jsr_network* net);

/**
 * Send a chat message through JSR relay
 * (Will be queued if not connected and sent when online)
 */
bool jsr_network_send_message(jsr_network* net, const char* message);

/**
 * Update JSR network system (call once per frame)
 * Processes mongoose events, handles reconnection, sends queued messages
 */
void jsr_network_update(jsr_network* net, float delta_time);

/**
 * Get connection status
 */
bool jsr_network_is_connected(jsr_network* net);

/**
 * Get number of pending messages in queue
 */
int jsr_network_pending_count(jsr_network* net);

/**
 * Request full sync from relay server (team members, recent chat history)
 */
void jsr_network_request_sync(jsr_network* net);

/**
 * Set connection timeout (seconds) - how long to wait before considering connection dead
 */
void jsr_network_set_timeout(jsr_network* net, double timeout_seconds);

/**
 * Set ping/keepalive interval (seconds) - how often to send heartbeat
 */
void jsr_network_set_ping_interval(jsr_network* net, double interval_seconds);

/**
 * Get relay URL that is currently being used
 */
const char* jsr_network_get_url(jsr_network* net);

/**
 * Get connection error message (if any)
 */
const char* jsr_network_get_error(jsr_network* net);

#ifdef __cplusplus
}
#endif

#endif // THERMITE_JSR_NETWORK_H
