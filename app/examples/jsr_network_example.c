/**
 * JSR (Jetstream Relay) Network Integration Example
 * 
 * This example demonstrates how to:
 * 1. Create a chat system
 * 2. Initialize JSR network relay
 * 3. Connect to relay server
 * 4. Send and receive messages in real-time
 * 5. Handle team member joins/leaves
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "thermite/tchat.h"
#include "thermite/jsr_network.h"

// ============================================================================
// Global State (for example purposes)
// ============================================================================

typedef struct {
    tchat_system* chat;
    jsr_network* network;
    bool running;
} app_state;

app_state g_app;

// ============================================================================
// Callback Functions
// ============================================================================

void on_message_received(tchat_message* msg) {
    printf("[MSG] %s: %s\n", msg->username, msg->message);
}

void on_member_joined(tchat_member* member) {
    printf(">>> %s joined the team\n", member->username);
}

void on_member_left(const char* username) {
    printf("<<< %s left the team\n", username);
}

void on_connection_changed(bool connected) {
    printf("[JSR] Connection status: %s\n", connected ? "CONNECTED" : "DISCONNECTED");
}

// ============================================================================
// Example 1: Leader Creates Team & Connects via JSR
// ============================================================================

void example_leader_with_relay() {
    printf("\n=== LEADER WITH JSR RELAY ===\n\n");
    
    // Create chat system
    tchat_system* chat = tchat_create("AliceLeader");
    tchat_set_on_message_callback(chat, on_message_received);
    tchat_set_on_member_joined_callback(chat, on_member_joined);
    tchat_set_on_member_left_callback(chat, on_member_left);
    tchat_set_on_connection_callback(chat, on_connection_changed);
    
    // Generate team key locally
    const char* team_key = tchat_generate_team_key(chat, "JSR Team Alpha");
    printf("Generated team key: %s\n", team_key);
    printf("Share this key with teammates!\n\n");
    
    // Initialize JSR network (connecting to localhost:8080 for testing)
    jsr_network* net = jsr_network_create("localhost", 8080);
    if (!net) {
        printf("Failed to initialize JSR network\n");
        tchat_destroy(chat);
        return;
    }
    
    // Connect to relay and join team
    printf("Connecting to JSR relay server...\n");
    if (!jsr_network_connect(net, team_key, "AliceLeader")) {
        printf("Failed to connect to relay\n");
        jsr_network_destroy(net);
        tchat_destroy(chat);
        return;
    }
    
    // Simulate some activity
    printf("\nSending messages...\n\n");
    for (int i = 0; i < 3; i++) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Message %d from Alice", i + 1);
        tchat_send_message(chat, msg);
        jsr_network_send_message(net, msg);
        
        // Update network (process events)
        jsr_network_update(net, 0.016f);
        sleep(1);
    }
    
    // Show status
    printf("\nTeam Status:\n");
    printf("  Key: %s\n", chat->team_key);
    printf("  Name: %s\n", chat->team_name);
    printf("  Is Leader: %s\n", chat->is_team_leader ? "YES" : "NO");
    printf("  Connected: %s\n", jsr_network_is_connected(net) ? "YES" : "NO");
    printf("  Pending Messages: %d\n", jsr_network_pending_count(net));
    
    int count;
    const tchat_message* messages = tchat_get_messages(chat, &count);
    printf("  Messages: %d\n", count);
    
    const tchat_member* members = tchat_get_members(chat, &count);
    printf("  Members: %d\n", count);
    
    // Cleanup
    jsr_network_destroy(net);
    tchat_destroy(chat);
}

// ============================================================================
// Example 2: Team Member Joins via JSR
// ============================================================================

void example_member_joins_with_relay() {
    printf("\n=== TEAM MEMBER JOINS VIA JSR ===\n\n");
    
    // Create chat system for new member
    tchat_system* chat = tchat_create("BobMember");
    tchat_set_on_message_callback(chat, on_message_received);
    tchat_set_on_member_joined_callback(chat, on_member_joined);
    tchat_set_on_member_left_callback(chat, on_member_left);
    tchat_set_on_connection_callback(chat, on_connection_changed);
    
    // Join team using shared key
    const char* shared_key = "ABC12XYZ"; // Key from leader
    printf("Joining team with key: %s\n\n", shared_key);
    
    if (!tchat_join_team(chat, shared_key)) {
        printf("Failed to join team (invalid key)\n");
        tchat_destroy(chat);
        return;
    }
    
    // Initialize JSR network
    jsr_network* net = jsr_network_create("localhost", 8080);
    if (!net) {
        printf("Failed to initialize JSR network\n");
        tchat_destroy(chat);
        return;
    }
    
    // Connect to relay with team key
    printf("Connecting to JSR relay...\n");
    if (!jsr_network_connect(net, shared_key, "BobMember")) {
        printf("Failed to connect to relay\n");
        jsr_network_destroy(net);
        tchat_destroy(chat);
        return;
    }
    
    // Simulate activity
    printf("\nSending messages...\n\n");
    for (int i = 0; i < 2; i++) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Message %d from Bob", i + 1);
        tchat_send_message(chat, msg);
        jsr_network_send_message(net, msg);
        
        jsr_network_update(net, 0.016f);
        sleep(1);
    }
    
    printf("\nMember Status:\n");
    printf("  Team Key: %s\n", chat->team_key);
    printf("  Is Leader: %s\n", chat->is_team_leader ? "YES" : "NO");
    printf("  Connected: %s\n", jsr_network_is_connected(net) ? "YES" : "NO");
    
    // Cleanup
    jsr_network_destroy(net);
    tchat_destroy(chat);
}

// ============================================================================
// Example 3: Full Multi-Player Simulation with JSR
// ============================================================================

void example_multiplayer_with_relay() {
    printf("\n=== MULTI-PLAYER JSR RELAY SIMULATION ===\n\n");
    
    // Create leader
    printf("[LEADER] Creating team...\n");
    tchat_system* leader_chat = tchat_create("Leader");
    tchat_set_on_message_callback(leader_chat, on_message_received);
    tchat_set_on_connection_callback(leader_chat, on_connection_changed);
    
    const char* team_key = tchat_generate_team_key(leader_chat, "Relay Team");
    printf("[LEADER] Team key: %s\n\n", team_key);
    
    // Create leader's JSR connection
    jsr_network* leader_net = jsr_network_create("localhost", 8080);
    jsr_network_connect(leader_net, team_key, "Leader");
    
    // Create member 1
    printf("[MEMBER1] Creating client...\n");
    tchat_system* member1_chat = tchat_create("Member1");
    tchat_set_on_message_callback(member1_chat, on_message_received);
    tchat_set_on_connection_callback(member1_chat, on_connection_changed);
    
    tchat_join_team(member1_chat, team_key);
    
    jsr_network* member1_net = jsr_network_create("localhost", 8080);
    jsr_network_connect(member1_net, team_key, "Member1");
    
    // Create member 2
    printf("[MEMBER2] Creating client...\n");
    tchat_system* member2_chat = tchat_create("Member2");
    tchat_set_on_message_callback(member2_chat, on_message_received);
    tchat_set_on_connection_callback(member2_chat, on_connection_changed);
    
    tchat_join_team(member2_chat, team_key);
    
    jsr_network* member2_net = jsr_network_create("localhost", 8080);
    jsr_network_connect(member2_net, team_key, "Member2");
    
    printf("\n--- SIMULATING CHAT ---\n\n");
    
    // Simulate chat
    printf("Leader: ");
    tchat_send_message(leader_chat, "Welcome to the team!");
    jsr_network_send_message(leader_net, "Welcome to the team!");
    jsr_network_update(leader_net, 0.016f);
    sleep(1);
    
    printf("Member1: ");
    tchat_send_message(member1_chat, "Thanks! Excited to be here!");
    jsr_network_send_message(member1_net, "Thanks! Excited to be here!");
    jsr_network_update(member1_net, 0.016f);
    sleep(1);
    
    printf("Member2: ");
    tchat_send_message(member2_chat, "Let's do this!");
    jsr_network_send_message(member2_net, "Let's do this!");
    jsr_network_update(member2_net, 0.016f);
    sleep(1);
    
    printf("Leader: ");
    tchat_send_message(leader_chat, "Great! Let's get started");
    jsr_network_send_message(leader_net, "Great! Let's get started");
    jsr_network_update(leader_net, 0.016f);
    
    printf("\n--- TEAM STATUS ---\n\n");
    
    printf("Leader's view:\n");
    int count;
    const tchat_member* members = tchat_get_members(leader_chat, &count);
    printf("  Members: %d\n", count);
    for (int i = 0; i < count; i++) {
        printf("    - %s (%s)\n", members[i].username,
               members[i].is_online ? "online" : "offline");
    }
    
    const tchat_message* messages = tchat_get_messages(leader_chat, &count);
    printf("  Messages: %d\n\n", count);
    
    // Cleanup
    jsr_network_destroy(leader_net);
    jsr_network_destroy(member1_net);
    jsr_network_destroy(member2_net);
    
    tchat_destroy(leader_chat);
    tchat_destroy(member1_chat);
    tchat_destroy(member2_chat);
}

// ============================================================================
// Example 4: Message Queuing (Offline Mode)
// ============================================================================

void example_message_queuing() {
    printf("\n=== MESSAGE QUEUING (OFFLINE MODE) ===\n\n");
    
    // Create chat and network
    tchat_system* chat = tchat_create("OfflinePlayer");
    tchat_set_on_message_callback(chat, on_message_received);
    
    tchat_join_team(chat, "TEST1234");
    
    jsr_network* net = jsr_network_create("localhost", 8080);
    // Don't connect yet
    
    printf("Queuing messages while offline...\n\n");
    
    // Try to send messages while disconnected
    bool sent1 = jsr_network_send_message(net, "Message 1");
    bool sent2 = jsr_network_send_message(net, "Message 2");
    bool sent3 = jsr_network_send_message(net, "Message 3");
    
    printf("Message 1 queued: %s\n", sent1 ? "YES" : "NO");
    printf("Message 2 queued: %s\n", sent2 ? "YES" : "NO");
    printf("Message 3 queued: %s\n", sent3 ? "YES" : "NO");
    printf("Pending messages: %d\n\n", jsr_network_pending_count(net));
    
    printf("Now connecting to relay...\n");
    jsr_network_connect(net, "TEST1234", "OfflinePlayer");
    
    // Update will send queued messages
    jsr_network_update(net, 0.016f);
    printf("After connect - Pending messages: %d\n", jsr_network_pending_count(net));
    
    // Cleanup
    jsr_network_destroy(net);
    tchat_destroy(chat);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║         JSR (JETSTREAM RELAY) - USAGE EXAMPLES           ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    
    printf("\nNOTE: These examples assume JSR relay server is running on localhost:8080\n");
    printf("      For testing without a server, message queuing will still work.\n");
    
    example_leader_with_relay();
    example_member_joins_with_relay();
    example_multiplayer_with_relay();
    example_message_queuing();
    
    printf("\n=== ALL EXAMPLES COMPLETED ===\n\n");
    
    return 0;
}
