# JSR Quick Start - Create Key & Chat with Friends

## Simple 3-Step Setup

### Step 1: One Friend Creates a Team Key

```c
#include "thermite/tchat.h"

// Friend 1 (Team Leader) creates the chat system
tchat_system* chat = tchat_create("Alice");

// Generate a team key - this creates the team!
const char* team_key = tchat_generate_team_key(chat, "My Squad");

// This returns something like: "ZORO4K2X"
printf("Team created! Share this key: %s\n", team_key);
```

**That's it!** The key is now generated. Alice needs to share it with friends.

---

### Step 2: Friends Join Using the Key

```c
#include "thermite/tchat.h"

// Friend 2 (Bob) creates their chat system
tchat_system* chat = tchat_create("Bob");

// Bob joins using Alice's key
bool success = tchat_join_team(chat, "ZORO4K2X");

if (success) {
    printf("Joined Alice's team!\n");
}
```

```c
// Friend 3 (Charlie) does the same
tchat_system* chat = tchat_create("Charlie");
tchat_join_team(chat, "ZORO4K2X");
```

**Done!** All three are now in the same team locally.

---

### Step 3: Setup Network & Start Chatting

For messages to sync in **real-time**, you need the JSR relay server.

#### A. Start JSR Relay Server

First, run the relay server (assuming you have Node.js):

```bash
# Create a simple relay server
node jsr_relay_server.js
# Server running on localhost:8080
```

(See JSR_DOCS.md for relay server code)

#### B. Connect Each Client to Relay

```c
#include "thermite/tchat.h"
#include "thermite/jsr_network.h"

// Alice (Leader)
tchat_system* chat = tchat_create("Alice");
const char* key = tchat_generate_team_key(chat, "My Squad");

// Connect to relay server
jsr_network* net = jsr_network_create("localhost", 8080);
jsr_network_connect(net, key, "Alice");

printf("Alice connected to relay!\n");
```

```c
// Bob (Member)
tchat_system* chat = tchat_create("Bob");
tchat_join_team(chat, "ZORO4K2X");

// Connect to same relay server using the same key
jsr_network* net = jsr_network_create("localhost", 8080);
jsr_network_connect(net, "ZORO4K2X", "Bob");

printf("Bob connected to relay!\n");
```

```c
// Charlie (Member)
tchat_system* chat = tchat_create("Charlie");
tchat_join_team(chat, "ZORO4K2X");

jsr_network* net = jsr_network_create("localhost", 8080);
jsr_network_connect(net, "ZORO4K2X", "Charlie");

printf("Charlie connected to relay!\n");
```

---

### Step 4: Send Messages (Inside Game Loop)

```c
// In your game loop:
void game_loop() {
    while (running) {
        // CRITICAL: Update network every frame!
        jsr_network_update(net, delta_time);
        
        // If player types something:
        if (user_typed_message) {
            // Send message through relay
            jsr_network_send_message(net, user_input);
            
            // Also store locally
            tchat_send_message(chat, user_input);
        }
        
        // Display all messages
        int count;
        const tchat_message* messages = tchat_get_messages(chat, &count);
        for (int i = 0; i < count; i++) {
            printf("[%s] %s\n", messages[i].username, messages[i].message);
        }
        
        // Show team members
        const tchat_member* members = tchat_get_members(chat, &count);
        for (int i = 0; i < count; i++) {
            printf("  - %s (%s)\n", members[i].username,
                   members[i].is_online ? "online" : "offline");
        }
    }
}
```

---

## Complete Working Example

Here's a complete runnable example with all 3 friends:

```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "thermite/tchat.h"
#include "thermite/jsr_network.h"

// Create a "virtual player" for testing
typedef struct {
    tchat_system* chat;
    jsr_network* net;
    char name[32];
} player;

int main() {
    printf("=== JSR TEAM CHAT - 3 FRIENDS ===\n\n");
    
    // ============================================
    // Step 1: Alice creates team
    // ============================================
    printf("STEP 1: Alice creates team...\n");
    
    player alice = {0};
    strcpy(alice.name, "Alice");
    alice.chat = tchat_create("Alice");
    alice.net = jsr_network_create("localhost", 8080);
    
    // Generate team key
    const char* team_key = tchat_generate_team_key(alice.chat, "Squad");
    printf("  Team key: %s\n\n", team_key);
    
    // Connect to relay
    jsr_network_connect(alice.net, team_key, "Alice");
    sleep(1);
    jsr_network_update(alice.net, 0.016f);
    
    // ============================================
    // Step 2: Bob joins team
    // ============================================
    printf("STEP 2: Bob joins using key...\n");
    
    player bob = {0};
    strcpy(bob.name, "Bob");
    bob.chat = tchat_create("Bob");
    bob.net = jsr_network_create("localhost", 8080);
    
    tchat_join_team(bob.chat, team_key);
    jsr_network_connect(bob.net, team_key, "Bob");
    sleep(1);
    jsr_network_update(bob.net, 0.016f);
    printf("  Bob joined!\n\n");
    
    // ============================================
    // Step 3: Charlie joins team
    // ============================================
    printf("STEP 3: Charlie joins using key...\n");
    
    player charlie = {0};
    strcpy(charlie.name, "Charlie");
    charlie.chat = tchat_create("Charlie");
    charlie.net = jsr_network_create("localhost", 8080);
    
    tchat_join_team(charlie.chat, team_key);
    jsr_network_connect(charlie.net, team_key, "Charlie");
    sleep(1);
    jsr_network_update(charlie.net, 0.016f);
    printf("  Charlie joined!\n\n");
    
    // ============================================
    // Step 4: Send messages
    // ============================================
    printf("STEP 4: Chatting...\n\n");
    
    // Alice sends message
    printf("Alice: Hello everyone!\n");
    tchat_send_message(alice.chat, "Hello everyone!");
    jsr_network_send_message(alice.net, "Hello everyone!");
    jsr_network_update(alice.net, 0.016f);
    sleep(1);
    
    // Bob sends message
    printf("Bob: Hey Alice!\n");
    tchat_send_message(bob.chat, "Hey Alice!");
    jsr_network_send_message(bob.net, "Hey Alice!");
    jsr_network_update(bob.net, 0.016f);
    sleep(1);
    
    // Charlie sends message
    printf("Charlie: Let's chat!\n");
    tchat_send_message(charlie.chat, "Let's chat!");
    jsr_network_send_message(charlie.net, "Let's chat!");
    jsr_network_update(charlie.net, 0.016f);
    sleep(1);
    
    // ============================================
    // Step 5: View chat from each person's perspective
    // ============================================
    printf("\n--- ALICE'S VIEW ---\n");
    int count;
    const tchat_message* messages = tchat_get_messages(alice.chat, &count);
    for (int i = 0; i < count; i++) {
        printf("[%s] %s\n", messages[i].username, messages[i].message);
    }
    
    printf("\nAlice's team members:\n");
    const tchat_member* members = tchat_get_members(alice.chat, &count);
    for (int i = 0; i < count; i++) {
        printf("  - %s (%s)\n", members[i].username,
               members[i].is_online ? "online" : "offline");
    }
    
    printf("\n--- BOB'S VIEW ---\n");
    messages = tchat_get_messages(bob.chat, &count);
    for (int i = 0; i < count; i++) {
        printf("[%s] %s\n", messages[i].username, messages[i].message);
    }
    
    printf("\n--- CHARLIE'S VIEW ---\n");
    messages = tchat_get_messages(charlie.chat, &count);
    for (int i = 0; i < count; i++) {
        printf("[%s] %s\n", messages[i].username, messages[i].message);
    }
    
    // ============================================
    // Cleanup
    // ============================================
    jsr_network_destroy(alice.net);
    jsr_network_destroy(bob.net);
    jsr_network_destroy(charlie.net);
    
    tchat_destroy(alice.chat);
    tchat_destroy(bob.chat);
    tchat_destroy(charlie.chat);
    
    printf("\n=== DONE ===\n");
    return 0;
}
```

---

## The Key Flow Visualized

```
┌──────────────────────────────────────────────────────┐
│ Alice creates team                                   │
│ tchat_generate_team_key(chat, "Squad")              │
│ ↓                                                    │
│ Returns: "ZORO4K2X"  ← SHARE THIS WITH FRIENDS     │
└──────────────────────────────────────────────────────┘
         ↓ Alice gives key to Bob & Charlie
         
┌──────────────────┬────────────────────┬─────────────────┐
│ Bob enters key   │ Charlie enters key  │ Alice uses key  │
│ ZORO4K2X         │ ZORO4K2X            │ ZORO4K2X        │
│ tchat_join_team  │ tchat_join_team     │ tchat_create    │
└──────────────────┴────────────────────┴─────────────────┘
         ↓                  ↓                     ↓
         └──────────────────┴─────────────────────┘
                      ↓
            All connect to relay
            jsr_network_connect(net, key, name)
                      ↓
            ┌─────────────────────┐
            │  JSR Relay Server   │
            │  (localhost:8080)   │
            └─────────────────────┘
                      ↓
            Messages sync in real-time
            Alice → Bob, Charlie
            Bob → Alice, Charlie
            Charlie → Alice, Bob
```

---

## Checklist

- [ ] Friend 1 generates team key: `tchat_generate_team_key()`
- [ ] Share key with friends (Discord, message, etc)
- [ ] Each friend joins: `tchat_join_team(chat, key)`
- [ ] Start JSR relay server on `localhost:8080`
- [ ] Each connects to relay: `jsr_network_connect(net, key, name)`
- [ ] Call `jsr_network_update()` every frame
- [ ] Send messages: `jsr_network_send_message(net, message)`
- [ ] Display messages from `tchat_get_messages()`
- [ ] Show team members from `tchat_get_members()`

---

## Key Points

✅ **Step 1**: One friend generates key → Share it  
✅ **Step 2**: Other friends join with key  
✅ **Step 3**: Everyone connects to relay server  
✅ **Step 4**: Call `jsr_network_update()` in game loop  
✅ **Step 5**: Send messages through `jsr_network_send_message()`  

**That's all you need to chat in real-time!**

---

## Troubleshooting

### "Connection failed"
- Make sure JSR relay server is running on `localhost:8080`
- Check port isn't blocked by firewall

### "Messages not appearing"
- Confirm `jsr_network_update()` is called every frame
- Check relay server is handling WebSocket connections

### "Friends can't join team"
- Verify team key is exactly 8 characters
- Key must only contain A-Z and 0-9
- Both must use same relay server address

### "No relay server setup?"
- See JSR_DOCS.md for relay server code
- Or skip relay and use local chat only (no real-time sync between friends)

---

## Local Chat Only (No Relay)

If you don't want to setup a relay server, you can still use local chat:

```c
tchat_system* chat = tchat_create("Alice");
tchat_join_team(chat, "ZORO4K2X");

// Messages only stored locally, not synced to others
tchat_send_message(chat, "Hello!");

// No network needed
```

But then each friend only sees their own messages!

**To sync between friends, you NEED the relay server and JSR network layer.**
