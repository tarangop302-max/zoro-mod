#ifndef THERMITE_TCHAT_H
#define THERMITE_TCHAT_H

#include <time.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum message length
#define TCHAT_MESSAGE_MAX_LEN 256
#define TCHAT_TEAM_KEY_LEN 8
#define TCHAT_USERNAME_MAX 32
#define TCHAT_MAX_MESSAGES 500
#define TCHAT_MAX_TEAM_MEMBERS 64

// Chat message structure
typedef struct {
    char username[TCHAT_USERNAME_MAX];
    char message[TCHAT_MESSAGE_MAX_LEN];
    time_t timestamp;
    bool is_system_message; // System messages (join/leave/error)
} tchat_message;

// Team member info
typedef struct {
    char username[TCHAT_USERNAME_MAX];
    char peer_id[32]; // Unique peer identifier
    bool is_online;
    time_t joined_at;
    time_t last_seen;
} tchat_member;

// Chat system state
typedef struct tchat_system {
    // Team info
    char team_key[TCHAT_TEAM_KEY_LEN + 1]; // Unique 8-char team code
    char team_name[64];
    char local_username[TCHAT_USERNAME_MAX];
    bool is_team_leader;
    
    // Messages
    tchat_message messages[TCHAT_MAX_MESSAGES];
    int message_count;
    int message_read_index; // Track which messages have been read
    
    // Team members
    tchat_member members[TCHAT_MAX_TEAM_MEMBERS];
    int member_count;
    
    // UI state
    char input_buffer[TCHAT_MESSAGE_MAX_LEN];
    char join_key_buffer[TCHAT_TEAM_KEY_LEN + 1];
    bool is_chat_open;
    bool should_scroll_to_bottom;
    
    // Network state
    bool is_connected;
    void* network_context; // Pointer to network handler (mongoose/socket)
    
    // Callbacks
    void (*on_message_received)(tchat_message* msg);
    void (*on_member_joined)(tchat_member* member);
    void (*on_member_left)(const char* username);
    void (*on_connection_changed)(bool connected);
} tchat_system;

// API Functions

/**
 * Initialize the chat system
 */
tchat_system* tchat_create(const char* local_username);

/**
 * Cleanup and destroy the chat system
 */
void tchat_destroy(tchat_system* chat);

/**
 * Generate a new team key (only leader can do this)
 * Returns the generated team key
 */
const char* tchat_generate_team_key(tchat_system* chat, const char* team_name);

/**
 * Join an existing team using a team key
 */
bool tchat_join_team(tchat_system* chat, const char* team_key);

/**
 * Leave current team
 */
void tchat_leave_team(tchat_system* chat);

/**
 * Send a message to the team
 */
bool tchat_send_message(tchat_system* chat, const char* message);

/**
 * Get all messages
 */
const tchat_message* tchat_get_messages(tchat_system* chat, int* out_count);

/**
 * Get team members
 */
const tchat_member* tchat_get_members(tchat_system* chat, int* out_count);

/**
 * Check for unread messages
 */
int tchat_get_unread_count(tchat_system* chat);

/**
 * Mark all messages as read
 */
void tchat_mark_all_read(tchat_system* chat);

/**
 * Update chat system (call once per frame)
 */
void tchat_update(tchat_system* chat, float delta_time);

/**
 * Set network context (for integration with networking backend)
 */
void tchat_set_network_context(tchat_system* chat, void* context);

/**
 * Register callbacks
 */
void tchat_set_on_message_callback(tchat_system* chat, void (*callback)(tchat_message*));
void tchat_set_on_member_joined_callback(tchat_system* chat, void (*callback)(tchat_member*));
void tchat_set_on_member_left_callback(tchat_system* chat, void (*callback)(const char*));
void tchat_set_on_connection_callback(tchat_system* chat, void (*callback)(bool));

#ifdef __cplusplus
}
#endif

#endif // THERMITE_TCHAT_H
