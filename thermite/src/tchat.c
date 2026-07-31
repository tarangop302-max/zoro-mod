#include "thermite/tchat.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// Generate a random alphanumeric string
static void generate_random_key(char* buffer, int length) {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    int charset_size = sizeof(charset) - 1;
    
    for (int i = 0; i < length; i++) {
        buffer[i] = charset[rand() % charset_size];
    }
    buffer[length] = '\0';
}

// Add system message to chat
static void add_system_message(tchat_system* chat, const char* message) {
    if (chat->message_count >= TCHAT_MAX_MESSAGES) {
        // Shift messages down
        memmove(&chat->messages[0], &chat->messages[1], 
                sizeof(tchat_message) * (TCHAT_MAX_MESSAGES - 1));
        chat->message_count = TCHAT_MAX_MESSAGES - 1;
    }
    
    tchat_message* msg = &chat->messages[chat->message_count++];
    strcpy(msg->username, "[SYSTEM]");
    strcpy(msg->message, message);
    msg->timestamp = time(NULL);
    msg->is_system_message = true;
}

tchat_system* tchat_create(const char* local_username) {
    tchat_system* chat = (tchat_system*)calloc(1, sizeof(tchat_system));
    if (!chat) return NULL;
    
    strncpy(chat->local_username, local_username, TCHAT_USERNAME_MAX - 1);
    chat->local_username[TCHAT_USERNAME_MAX - 1] = '\0';
    
    chat->message_count = 0;
    chat->message_read_index = 0;
    chat->member_count = 0;
    chat->is_chat_open = false;
    chat->is_team_leader = false;
    chat->is_connected = false;
    
    memset(chat->team_key, 0, sizeof(chat->team_key));
    memset(chat->team_name, 0, sizeof(chat->team_name));
    memset(chat->input_buffer, 0, sizeof(chat->input_buffer));
    memset(chat->join_key_buffer, 0, sizeof(chat->join_key_buffer));
    
    srand(time(NULL));
    
    return chat;
}

void tchat_destroy(tchat_system* chat) {
    if (!chat) return;
    
    // Clean up network context if needed
    if (chat->network_context) {
        // TODO: Close network connections
    }
    
    free(chat);
}

const char* tchat_generate_team_key(tchat_system* chat, const char* team_name) {
    if (!chat) return NULL;
    
    // Generate random 8-character team key
    generate_random_key(chat->team_key, TCHAT_TEAM_KEY_LEN);
    
    // Set team name
    strncpy(chat->team_name, team_name, sizeof(chat->team_name) - 1);
    chat->team_name[sizeof(chat->team_name) - 1] = '\0';
    
    // Mark this player as team leader
    chat->is_team_leader = true;
    
    // Add the leader as first member
    chat->member_count = 0;
    tchat_member* leader = &chat->members[chat->member_count++];
    strcpy(leader->username, chat->local_username);
    strcpy(leader->peer_id, "local");
    leader->is_online = true;
    leader->joined_at = time(NULL);
    leader->last_seen = time(NULL);
    
    // Add system message
    char sys_msg[256];
    snprintf(sys_msg, sizeof(sys_msg), "Team '%s' created with key: %s", 
             team_name, chat->team_key);
    add_system_message(chat, sys_msg);
    
    chat->is_connected = true;
    
    if (chat->on_connection_changed) {
        chat->on_connection_changed(true);
    }
    
    return chat->team_key;
}

bool tchat_join_team(tchat_system* chat, const char* team_key) {
    if (!chat || !team_key) return false;
    
    // Validate key format (should be 8 chars, alphanumeric)
    if (strlen(team_key) != TCHAT_TEAM_KEY_LEN) {
        add_system_message(chat, "ERROR: Invalid team key format (must be 8 characters)");
        return false;
    }
    
    for (int i = 0; i < TCHAT_TEAM_KEY_LEN; i++) {
        char c = team_key[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
            add_system_message(chat, "ERROR: Team key must contain only letters and numbers");
            return false;
        }
    }
    
    // Copy the team key
    strcpy(chat->team_key, team_key);
    chat->is_team_leader = false;
    
    // Add yourself as a member
    if (chat->member_count < TCHAT_MAX_TEAM_MEMBERS) {
        tchat_member* member = &chat->members[chat->member_count++];
        strcpy(member->username, chat->local_username);
        strcpy(member->peer_id, "local");
        member->is_online = true;
        member->joined_at = time(NULL);
        member->last_seen = time(NULL);
    }
    
    chat->is_connected = true;
    
    char sys_msg[256];
    snprintf(sys_msg, sizeof(sys_msg), "%s joined the team", chat->local_username);
    add_system_message(chat, sys_msg);
    
    if (chat->on_connection_changed) {
        chat->on_connection_changed(true);
    }
    
    if (chat->on_member_joined) {
        tchat_member joined_member;
        strcpy(joined_member.username, chat->local_username);
        strcpy(joined_member.peer_id, "local");
        joined_member.is_online = true;
        joined_member.joined_at = time(NULL);
        joined_member.last_seen = time(NULL);
        chat->on_member_joined(&joined_member);
    }
    
    return true;
}

void tchat_leave_team(tchat_system* chat) {
    if (!chat || !chat->is_connected) return;
    
    char sys_msg[256];
    snprintf(sys_msg, sizeof(sys_msg), "%s left the team", chat->local_username);
    add_system_message(chat, sys_msg);
    
    chat->is_connected = false;
    chat->is_team_leader = false;
    chat->member_count = 0;
    memset(chat->team_key, 0, sizeof(chat->team_key));
    memset(chat->team_name, 0, sizeof(chat->team_name));
    
    if (chat->on_connection_changed) {
        chat->on_connection_changed(false);
    }
}

bool tchat_send_message(tchat_system* chat, const char* message) {
    if (!chat || !message || !chat->is_connected) return false;
    
    if (strlen(message) == 0) return false;
    if (strlen(message) > TCHAT_MESSAGE_MAX_LEN - 1) return false;
    
    // Add message to local history
    if (chat->message_count >= TCHAT_MAX_MESSAGES) {
        // Shift messages down
        memmove(&chat->messages[0], &chat->messages[1], 
                sizeof(tchat_message) * (TCHAT_MAX_MESSAGES - 1));
        chat->message_count = TCHAT_MAX_MESSAGES - 1;
    }
    
    tchat_message* msg = &chat->messages[chat->message_count++];
    strcpy(msg->username, chat->local_username);
    strcpy(msg->message, message);
    msg->timestamp = time(NULL);
    msg->is_system_message = false;
    
    chat->should_scroll_to_bottom = true;
    
    if (chat->on_message_received) {
        chat->on_message_received(msg);
    }
    
    // TODO: Send to network (mongoose/WebSocket)
    // This will relay the message to other team members
    
    return true;
}

const tchat_message* tchat_get_messages(tchat_system* chat, int* out_count) {
    if (!chat || !out_count) return NULL;
    
    *out_count = chat->message_count;
    return chat->messages;
}

const tchat_member* tchat_get_members(tchat_system* chat, int* out_count) {
    if (!chat || !out_count) return NULL;
    
    *out_count = chat->member_count;
    return chat->members;
}

int tchat_get_unread_count(tchat_system* chat) {
    if (!chat) return 0;
    
    return chat->message_count - chat->message_read_index;
}

void tchat_mark_all_read(tchat_system* chat) {
    if (!chat) return;
    
    chat->message_read_index = chat->message_count;
}

void tchat_update(tchat_system* chat, float delta_time) {
    if (!chat) return;
    
    // TODO: Process network events
    // TODO: Check for disconnections/timeouts
    // TODO: Sync with other team members
    
    (void)delta_time; // Suppress unused warning for now
}

void tchat_set_network_context(tchat_system* chat, void* context) {
    if (!chat) return;
    
    chat->network_context = context;
}

void tchat_set_on_message_callback(tchat_system* chat, void (*callback)(tchat_message*)) {
    if (!chat) return;
    
    chat->on_message_received = callback;
}

void tchat_set_on_member_joined_callback(tchat_system* chat, void (*callback)(tchat_member*)) {
    if (!chat) return;
    
    chat->on_member_joined = callback;
}

void tchat_set_on_member_left_callback(tchat_system* chat, void (*callback)(const char*)) {
    if (!chat) return;
    
    chat->on_member_left = callback;
}

void tchat_set_on_connection_callback(tchat_system* chat, void (*callback)(bool)) {
    if (!chat) return;
    
    chat->on_connection_changed = callback;
}
