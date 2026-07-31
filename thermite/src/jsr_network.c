#include "thermite/jsr_network.h"
#include "thermite/tchat.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "../app/src/external/mongoose.h"

// ============================================================================
// Internal Helper Functions
// ============================================================================

// Generate a unique peer ID
static void jsr_generate_peer_id(char* peer_id, int len) {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    int charset_size = sizeof(charset) - 1;
    
    srand((unsigned int)time(NULL));
    for (int i = 0; i < len - 1; i++) {
        peer_id[i] = charset[rand() % charset_size];
    }
    peer_id[len - 1] = '\0';
}

// Parse incoming message from relay server
static void jsr_parse_message(jsr_network* net, const char* data, int len) {
    if (!net || !net->chat || len < 2) return;
    
    // Message format: [type][payload]
    uint8_t msg_type = (uint8_t)data[0];
    const char* payload = data + 1;
    int payload_len = len - 1;
    
    switch (msg_type) {
        case JSR_MSG_TYPE_CHAT: {
            // Format: [username_len][username][message]
            if (payload_len < 2) break;
            
            uint8_t username_len = (uint8_t)payload[0];
            if (payload_len < 1 + username_len + 1) break;
            
            char username[TCHAT_USERNAME_MAX];
            strncpy(username, &payload[1], username_len);
            username[username_len] = '\0';
            
            const char* message = &payload[1 + username_len];
            int message_len = payload_len - 1 - username_len;
            
            // Add message to chat if it's not from ourselves
            if (strcmp(username, net->username) != 0) {
                if (net->chat->message_count >= TCHAT_MAX_MESSAGES) {
                    memmove(&net->chat->messages[0], &net->chat->messages[1], 
                            sizeof(tchat_message) * (TCHAT_MAX_MESSAGES - 1));
                    net->chat->message_count = TCHAT_MAX_MESSAGES - 1;
                }
                
                tchat_message* msg = &net->chat->messages[net->chat->message_count++];
                strncpy(msg->username, username, TCHAT_USERNAME_MAX - 1);
                msg->username[TCHAT_USERNAME_MAX - 1] = '\0';
                strncpy(msg->message, message, TCHAT_MESSAGE_MAX_LEN - 1);
                msg->message[TCHAT_MESSAGE_MAX_LEN - 1] = '\0';
                msg->timestamp = time(NULL);
                msg->is_system_message = false;
                
                if (net->chat->on_message_received) {
                    net->chat->on_message_received(msg);
                }
            }
            break;
        }
        
        case JSR_MSG_TYPE_JOIN: {
            // Format: [username_len][username]
            if (payload_len < 2) break;
            
            uint8_t username_len = (uint8_t)payload[0];
            if (payload_len < 1 + username_len) break;
            
            char username[TCHAT_USERNAME_MAX];
            strncpy(username, &payload[1], username_len);
            username[username_len] = '\0';
            
            // Add member to list
            if (net->chat->member_count < TCHAT_MAX_TEAM_MEMBERS) {
                tchat_member* member = &net->chat->members[net->chat->member_count++];
                strncpy(member->username, username, TCHAT_USERNAME_MAX - 1);
                member->username[TCHAT_USERNAME_MAX - 1] = '\0';
                strncpy(member->peer_id, net->peer_id, 31);
                member->peer_id[31] = '\0';
                member->is_online = true;
                member->joined_at = time(NULL);
                member->last_seen = time(NULL);
                
                if (net->chat->on_member_joined) {
                    net->chat->on_member_joined(member);
                }
            }
            break;
        }
        
        case JSR_MSG_TYPE_LEAVE: {
            // Format: [username_len][username]
            if (payload_len < 2) break;
            
            uint8_t username_len = (uint8_t)payload[0];
            if (payload_len < 1 + username_len) break;
            
            char username[TCHAT_USERNAME_MAX];
            strncpy(username, &payload[1], username_len);
            username[username_len] = '\0';
            
            // Mark member as offline or remove
            for (int i = 0; i < net->chat->member_count; i++) {
                if (strcmp(net->chat->members[i].username, username) == 0) {
                    net->chat->members[i].is_online = false;
                    if (net->chat->on_member_left) {
                        net->chat->on_member_left(username);
                    }
                    break;
                }
            }
            break;
        }
        
        case JSR_MSG_TYPE_PONG: {
            // Ping response - just update last_ping time
            net->last_ping = time(NULL);
            break;
        }
        
        case JSR_MSG_TYPE_ERROR: {
            // Error message from relay
            char error_msg[256] = "[JSR ERROR] ";
            strncat(error_msg, payload, payload_len);
            // Could log or display error
            printf("JSR: %s\n", error_msg);
            break;
        }
        
        default:
            break;
    }
}

// WebSocket event handler callback
static void jsr_websocket_handler(struct mg_connection* c, int ev, void* ev_data) {
    jsr_network* net = (jsr_network*)c->fn_data;
    
    if (ev == MG_EV_OPEN) {
        // TCP connection opened
        printf("JSR: TCP connection opened\n");
    } 
    else if (ev == MG_EV_WS_OPEN) {
        // WebSocket handshake complete
        printf("JSR: WebSocket connected to relay\n");
        net->is_connected = true;
        net->consecutive_failures = 0;
        net->last_message_time = time(NULL);
        
        if (net->chat && net->chat->on_connection_changed) {
            net->chat->on_connection_changed(true);
        }
        
        // Send join message
        jsr_network_request_sync(net);
    } 
    else if (ev == MG_EV_WS_MSG) {
        // Received WebSocket message
        struct mg_ws_message* msg = (struct mg_ws_message*)ev_data;
        jsr_parse_message(net, msg->data.buf, (int)msg->data.len);
        net->last_message_time = time(NULL);
    } 
    else if (ev == MG_EV_ERROR) {
        printf("JSR: Connection error\n");
        net->is_connected = false;
        net->consecutive_failures++;
        
        if (net->chat && net->chat->on_connection_changed) {
            net->chat->on_connection_changed(false);
        }
    } 
    else if (ev == MG_EV_CLOSE) {
        printf("JSR: Connection closed\n");
        net->is_connected = false;
        net->ws_connection = NULL;
        
        if (net->chat && net->chat->on_connection_changed) {
            net->chat->on_connection_changed(false);
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

jsr_network* jsr_network_create(const char* relay_host, int relay_port) {
    jsr_network* net = (jsr_network*)calloc(1, sizeof(jsr_network));
    if (!net) return NULL;
    
    // Initialize mongoose manager
    net->mgr = (struct mg_mgr*)calloc(1, sizeof(struct mg_mgr));
    if (!net->mgr) {
        free(net);
        return NULL;
    }
    mg_mgr_init(net->mgr);
    
    // Set relay server info
    strncpy(net->relay_host, relay_host, sizeof(net->relay_host) - 1);
    net->relay_host[sizeof(net->relay_host) - 1] = '\0';
    net->relay_port = relay_port;
    
    // Build WebSocket URL
    snprintf(net->relay_url, sizeof(net->relay_url), 
             "ws://%s:%d/jsr", relay_host, relay_port);
    
    // Initialize timing
    net->ping_interval = 30.0;  // Send ping every 30 seconds
    net->connection_timeout = 60.0;  // Connection timeout after 60 seconds
    net->last_ping = time(NULL);
    net->last_message_time = time(NULL);
    
    // Initialize retry logic
    net->reconnect_backoff = 1.0;
    net->next_reconnect_time = 0.0;
    
    // Generate unique peer ID
    jsr_generate_peer_id(net->peer_id, sizeof(net->peer_id));
    
    // Initialize message queue
    net->pending_count = 0;
    
    return net;
}

void jsr_network_destroy(jsr_network* net) {
    if (!net) return;
    
    jsr_network_disconnect(net);
    
    if (net->mgr) {
        mg_mgr_free(net->mgr);
        free(net->mgr);
    }
    
    free(net);
}

bool jsr_network_connect(jsr_network* net, const char* team_key, const char* username) {
    if (!net || !team_key || !username) return false;
    
    // Copy credentials
    strncpy(net->team_key, team_key, 8);
    net->team_key[8] = '\0';
    strncpy(net->username, username, sizeof(net->username) - 1);
    net->username[sizeof(net->username) - 1] = '\0';
    
    // Create WebSocket connection
    struct mg_connection* c = mg_ws_connect(net->mgr, net->relay_url, jsr_websocket_handler, net);
    if (!c) {
        printf("JSR: Failed to create WebSocket connection\n");
        return false;
    }
    
    net->ws_connection = c;
    net->last_reconnect_attempt = time(NULL);
    
    printf("JSR: Connecting to %s\n", net->relay_url);
    return true;
}

void jsr_network_disconnect(jsr_network* net) {
    if (!net) return;
    
    if (net->ws_connection) {
        net->ws_connection->is_closing = 1;
        net->ws_connection = NULL;
    }
    
    net->is_connected = false;
}

bool jsr_network_send_message(jsr_network* net, const char* message) {
    if (!net || !message) return false;
    
    if (net->is_connected && net->ws_connection) {
        // Send immediately
        int msg_len = strlen(message) + 1;
        uint8_t* buf = (uint8_t*)malloc(msg_len + 1);
        if (!buf) return false;
        
        buf[0] = JSR_MSG_TYPE_CHAT;
        memcpy(&buf[1], message, msg_len);
        
        mg_ws_send(net->ws_connection, buf, msg_len + 1, WEBSOCKET_OP_BINARY);
        free(buf);
        
        return true;
    } else {
        // Queue message for later
        if (net->pending_count < 10) {
            strncpy(net->pending_messages[net->pending_count], message, 255);
            net->pending_messages[net->pending_count][255] = '\0';
            net->pending_count++;
            return true;
        }
        return false;
    }
}

void jsr_network_update(jsr_network* net, float delta_time) {
    if (!net || !net->mgr) return;
    
    // Poll mongoose for events
    mg_mgr_poll(net->mgr, 0);
    
    double now = time(NULL);
    
    // Check connection timeout
    if (net->is_connected && 
        (now - net->last_message_time) > net->connection_timeout) {
        printf("JSR: Connection timeout\n");
        jsr_network_disconnect(net);
    }
    
    // Send periodic pings
    if (net->is_connected && (now - net->last_ping) > net->ping_interval) {
        uint8_t ping_msg = JSR_MSG_TYPE_PING;
        if (net->ws_connection) {
            mg_ws_send(net->ws_connection, &ping_msg, 1, WEBSOCKET_OP_BINARY);
        }
        net->last_ping = now;
    }
    
    // Send queued messages
    if (net->is_connected && net->pending_count > 0) {
        for (int i = 0; i < net->pending_count; i++) {
            jsr_network_send_message(net, net->pending_messages[i]);
        }
        net->pending_count = 0;
    }
    
    // Handle reconnection
    if (!net->is_connected && net->consecutive_failures > 0 &&
        now >= net->next_reconnect_time) {
        printf("JSR: Attempting to reconnect...\n");
        jsr_network_connect(net, net->team_key, net->username);
        
        // Exponential backoff
        net->reconnect_backoff = (net->reconnect_backoff < 32.0) ? 
                                  net->reconnect_backoff * 1.5 : 32.0;
        net->next_reconnect_time = now + net->reconnect_backoff;
    }
    
    (void)delta_time;
}

bool jsr_network_is_connected(jsr_network* net) {
    if (!net) return false;
    return net->is_connected;
}

int jsr_network_pending_count(jsr_network* net) {
    if (!net) return 0;
    return net->pending_count;
}

void jsr_network_request_sync(jsr_network* net) {
    if (!net) return;
    
    // Send sync request to relay server
    uint8_t sync_msg[256];
    int pos = 0;
    
    sync_msg[pos++] = JSR_MSG_TYPE_SYNC;
    
    // Add team key
    int key_len = strlen(net->team_key);
    sync_msg[pos++] = (uint8_t)key_len;
    memcpy(&sync_msg[pos], net->team_key, key_len);
    pos += key_len;
    
    // Add username
    int name_len = strlen(net->username);
    sync_msg[pos++] = (uint8_t)name_len;
    memcpy(&sync_msg[pos], net->username, name_len);
    pos += name_len;
    
    // Add peer ID
    int peer_len = strlen(net->peer_id);
    sync_msg[pos++] = (uint8_t)peer_len;
    memcpy(&sync_msg[pos], net->peer_id, peer_len);
    pos += peer_len;
    
    if (net->ws_connection) {
        mg_ws_send(net->ws_connection, sync_msg, pos, WEBSOCKET_OP_BINARY);
    }
}

void jsr_network_set_timeout(jsr_network* net, double timeout_seconds) {
    if (!net) return;
    net->connection_timeout = timeout_seconds;
}

void jsr_network_set_ping_interval(jsr_network* net, double interval_seconds) {
    if (!net) return;
    net->ping_interval = interval_seconds;
}

const char* jsr_network_get_url(jsr_network* net) {
    if (!net) return "";
    return net->relay_url;
}

const char* jsr_network_get_error(jsr_network* net) {
    if (!net) return "";
    // TODO: Add error message storage
    return "";
}
