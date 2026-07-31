/**
 * Team Chat System Example
 * 
 * This example demonstrates how to use the team chat system:
 * 1. Leader generates a team key
 * 2. Other players join using the key
 * 3. Real-time messaging between team members
 */

#include <stdio.h>
#include <string.h>
#include "thermite/tchat.h"

// ============================================================================
// Callback Functions
// ============================================================================

void on_message_received(tchat_message* msg) {
    printf("[%s] %s: %s\n", 
           msg->is_system_message ? "SYSTEM" : msg->username,
           msg->is_system_message ? "" : "",
           msg->message);
}

void on_member_joined(tchat_member* member) {
    printf(">>> %s joined the team\n", member->username);
}

void on_member_left(const char* username) {
    printf("<<< %s left the team\n", username);
}

void on_connection_changed(bool connected) {
    printf("Connection status: %s\n", connected ? "CONNECTED" : "DISCONNECTED");
}

// ============================================================================
// Example: Team Leader Creates Team & Generates Key
// ============================================================================

void example_leader() {
    printf("\n=== TEAM LEADER EXAMPLE ===\n\n");
    
    // Create chat system for the leader
    tchat_system* chat = tchat_create("PlayerA");
    
    // Register callbacks
    tchat_set_on_message_callback(chat, on_message_received);
    tchat_set_on_member_joined_callback(chat, on_member_joined);
    tchat_set_on_member_left_callback(chat, on_member_left);
    tchat_set_on_connection_callback(chat, on_connection_changed);
    
    // Generate a new team key
    const char* team_key = tchat_generate_team_key(chat, "Dragon Slayers");
    printf("Generated team key: %s\n", team_key);
    printf("Share this key with your teammates!\n\n");
    
    // Send some messages
    tchat_send_message(chat, "Welcome to the team!");
    tchat_send_message(chat, "Let's conquer this game together!");
    
    // Display team info
    printf("Team Name: %s\n", chat->team_name);
    printf("Team Key: %s\n", chat->team_key);
    printf("Is Leader: %s\n", chat->is_team_leader ? "YES" : "NO");
    printf("Members: %d\n\n", chat->member_count);
    
    // Get and display messages
    int msg_count;
    const tchat_message* messages = tchat_get_messages(chat, &msg_count);
    printf("Messages:\n");
    for (int i = 0; i < msg_count; i++) {
        printf("  [%s] %s: %s\n", 
               messages[i].is_system_message ? "SYS" : "MSG",
               messages[i].username,
               messages[i].message);
    }
    
    tchat_destroy(chat);
}

// ============================================================================
// Example: Team Member Joins Using Key
// ============================================================================

void example_member_joins() {
    printf("\n=== TEAM MEMBER JOINS EXAMPLE ===\n\n");
    
    // Player B wants to join
    tchat_system* chat = tchat_create("PlayerB");
    
    // Register callbacks
    tchat_set_on_message_callback(chat, on_message_received);
    tchat_set_on_member_joined_callback(chat, on_member_joined);
    tchat_set_on_member_left_callback(chat, on_member_left);
    tchat_set_on_connection_callback(chat, on_connection_changed);
    
    // Join team using the key shared by leader
    const char* shared_key = "ZORO4K2X"; // Example key
    printf("Joining team with key: %s\n\n", shared_key);
    
    bool joined = tchat_join_team(chat, shared_key);
    if (joined) {
        printf("Successfully joined team!\n");
        printf("Team Key: %s\n", chat->team_key);
        printf("Is Leader: %s\n\n", chat->is_team_leader ? "YES" : "NO");
        
        // Send message
        tchat_send_message(chat, "Hey team! I'm here!");
        
        // Get team members
        int member_count;
        const tchat_member* members = tchat_get_members(chat, &member_count);
        printf("Team Members (%d):\n", member_count);
        for (int i = 0; i < member_count; i++) {
            printf("  - %s (status: %s)\n", 
                   members[i].username,
                   members[i].is_online ? "online" : "offline");
        }
    } else {
        printf("Failed to join team!\n");
    }
    
    tchat_destroy(chat);
}

// ============================================================================
// Example: Multiple Players & Chat Flow
// ============================================================================

void example_full_chat_flow() {
    printf("\n=== FULL CHAT FLOW EXAMPLE ===\n\n");
    
    // Create leader
    tchat_system* leader = tchat_create("Alice");
    tchat_set_on_message_callback(leader, on_message_received);
    tchat_set_on_member_joined_callback(leader, on_member_joined);
    
    // Leader generates team
    const char* team_key = tchat_generate_team_key(leader, "Raid Group");
    printf("[LEADER] Generated team key: %s\n\n", team_key);
    
    // Create members
    tchat_system* member1 = tchat_create("Bob");
    tchat_set_on_message_callback(member1, on_message_received);
    
    tchat_system* member2 = tchat_create("Charlie");
    tchat_set_on_message_callback(member2, on_message_received);
    
    // Members join
    printf("[BOB] Joining team...\n");
    tchat_join_team(member1, team_key);
    
    printf("[CHARLIE] Joining team...\n");
    tchat_join_team(member2, team_key);
    
    printf("\n--- CHAT SIMULATION ---\n\n");
    
    // Chat messages
    printf("Alice: ");
    tchat_send_message(leader, "Ready for the raid?");
    
    printf("Bob: ");
    tchat_send_message(member1, "Let's go!");
    
    printf("Charlie: ");
    tchat_send_message(member2, "I'm prepared!");
    
    printf("\n--- TEAM STATUS ---\n\n");
    
    // Show leader's view
    printf("Alice's view - Team members:\n");
    int count;
    const tchat_member* members = tchat_get_members(leader, &count);
    for (int i = 0; i < count; i++) {
        printf("  - %s: %s\n", members[i].username, 
               members[i].is_online ? "online" : "offline");
    }
    
    printf("\nAlice's chat history:\n");
    const tchat_message* messages = tchat_get_messages(leader, &count);
    for (int i = 0; i < count; i++) {
        printf("  [%s] %s\n", messages[i].username, messages[i].message);
    }
    
    printf("\nUnread messages for Alice: %d\n", tchat_get_unread_count(leader));
    tchat_mark_all_read(leader);
    printf("After marking as read: %d\n", tchat_get_unread_count(leader));
    
    // Cleanup
    tchat_destroy(leader);
    tchat_destroy(member1);
    tchat_destroy(member2);
}

// ============================================================================
// Invalid Key Handling
// ============================================================================

void example_invalid_key() {
    printf("\n=== INVALID KEY HANDLING ===\n\n");
    
    tchat_system* chat = tchat_create("PlayerX");
    tchat_set_on_message_callback(chat, on_message_received);
    
    printf("Test 1: Key too short\n");
    bool result = tchat_join_team(chat, "SHORT");
    printf("Result: %s\n\n", result ? "SUCCESS" : "FAILED");
    
    printf("Test 2: Key with invalid characters\n");
    result = tchat_join_team(chat, "ZORO@#$!");
    printf("Result: %s\n\n", result ? "SUCCESS" : "FAILED");
    
    printf("Test 3: Valid key\n");
    result = tchat_join_team(chat, "ZORO1234");
    printf("Result: %s\n\n", result ? "SUCCESS" : "FAILED");
    
    tchat_destroy(chat);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║         TEAM CHAT SYSTEM - USAGE EXAMPLES                ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    
    example_leader();
    example_member_joins();
    example_full_chat_flow();
    example_invalid_key();
    
    printf("\n=== ALL EXAMPLES COMPLETED ===\n");
    
    return 0;
}
