# JSR (Jetstream Relay) - Team Chat System

A lightweight, real-time team chat system for zoro-mod with WebSocket-based relay server support.

## Overview

**JSR** provides:
- ✅ **Team key generation** - Leader generates 8-char alphanumeric codes
- ✅ **Easy joining** - Other players enter key to connect
- ✅ **Real-time messaging** - WebSocket relay syncs messages instantly
- ✅ **Member tracking** - See who's online in team
- ✅ **Message queuing** - Messages queue when offline, send when reconnected
- ✅ **Auto-reconnection** - Exponential backoff reconnect logic
- ✅ **Cross-platform** - Desktop (Linux/Mac/Windows) & Android support

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Game Client (zoro-mod)                   │
├──────────────────────┬──────────────────────────────────────┤
│   tchat_system       │   jsr_network                        │
│  (local chat logic)  │  (network client)                    │
│                      │                                      │
│ - Generate keys      │ - WebSocket connection               │
│ - Track members      │ - Send/receive messages              │
│ - Store history      │ - Message queueing                   │
│ - UI state           │ - Reconnection logic                 │
└──────────────────────┴──────────────────────────────────────┘
              │                           │
              │   (WebSocket / Binary)    │
              └───────────────┬───────────┘
                              │
                    ┌─────────▼────────────┐
                    │   JSR Relay Server   │
                    ├──────────────────────┤
                    │ - Team state manager │
                    │ - Message relay      │
                    │ - Member sync        │
                    │ - History storage    │
                    └──────────────────────┘
```

## Quick Start

### 1. Create a Chat System

```c
#include "thermite/tchat.h"
#include "thermite/jsr_network.h"

// Initialize chat
tchat_system* chat = tchat_create("PlayerName");

// Register callbacks
tchat_set_on_message_callback(chat, my_on_message);
tchat_set_on_member_joined_callback(chat, my_on_join);
tchat_set_on_connection_callback(chat, my_on_connect);
```

### 2. Leader: Generate Team Key

```c
// Generate unique 8-char team key
const char* team_key = tchat_generate_team_key(chat, "My Team");
// Returns: "ZORO4K2X"

printf("Share this key: %s\n", team_key);
```

### 3. Initialize JSR Network

```c
// Create network relay client
jsr_network* net = jsr_network_create("relay.example.com", 8080);

// Connect to relay
jsr_network_connect(net, team_key, "PlayerName");
```

### 4. Send Messages

```c
// Send local message (stored in history)
tchat_send_message(chat, "Hello team!");

// Send through relay (syncs to all players)
jsr_network_send_message(net, "Hello team!");
```

### 5. Update Each Frame

```c
// In your game loop:
void game_update(float delta_time) {
    jsr_network_update(net, delta_time);  // Process network events
    
    // ... render chat UI, display messages, etc
}
```

## API Reference

### Team Chat System (tchat_system)

| Function | Purpose |
|----------|---------|
| `tchat_create(username)` | Initialize chat for a player |
| `tchat_generate_team_key(chat, team_name)` | Leader: create team & get key |
| `tchat_join_team(chat, team_key)` | Join existing team |
| `tchat_leave_team(chat)` | Leave team |
| `tchat_send_message(chat, message)` | Send message (stored locally) |
| `tchat_get_messages(chat, &count)` | Get all messages |
| `tchat_get_members(chat, &count)` | Get team members |
| `tchat_get_unread_count(chat)` | Get unread message count |
| `tchat_mark_all_read(chat)` | Mark all as read |
| `tchat_destroy(chat)` | Cleanup |

**Callbacks:**
```c
void on_message_received(tchat_message* msg);
void on_member_joined(tchat_member* member);
void on_member_left(const char* username);
void on_connection_changed(bool connected);
```

### JSR Network (jsr_network)

| Function | Purpose |
|----------|---------|
| `jsr_network_create(host, port)` | Initialize relay client |
| `jsr_network_connect(net, team_key, username)` | Connect to relay |
| `jsr_network_disconnect(net)` | Disconnect from relay |
| `jsr_network_send_message(net, message)` | Send message via relay |
| `jsr_network_update(net, delta_time)` | Process events (call every frame) |
| `jsr_network_is_connected(net)` | Check connection status |
| `jsr_network_pending_count(net)` | Get queued message count |
| `jsr_network_request_sync(net)` | Request full team state |
| `jsr_network_set_timeout(net, seconds)` | Set connection timeout |
| `jsr_network_set_ping_interval(net, seconds)` | Set heartbeat interval |
| `jsr_network_destroy(net)` | Cleanup |

## Message Format (Binary Protocol)

All messages use simple binary format for efficiency:

```
[msg_type: 1 byte][payload: variable]
```

### Message Types

| Type | Value | Format |
|------|-------|--------|
| CHAT | 1 | `[username_len][username][message]` |
| JOIN | 2 | `[username_len][username]` |
| LEAVE | 3 | `[username_len][username]` |
| MEMBER_LIST | 4 | `[count][member1...memberN]` |
| ACK | 5 | (empty) |
| SYNC | 6 | `[key_len][key][name_len][name][peer_len][peer]` |
| PING | 7 | (empty) |
| PONG | 8 | (empty) |
| ERROR | 9 | `[error_message]` |

## Features

### 1. Team Key Generation

```c
// Generate random 8-character alphanumeric key
const char* key = tchat_generate_team_key(chat, "Team Name");
// Example output: "ZORO4K2X", "ABC12DEF", etc.
```

### 2. Join with Key

```c
// Players join by entering the key
bool success = tchat_join_team(chat, "ZORO4K2X");

// Validate:
// - Must be exactly 8 characters
// - Alphanumeric only (A-Z, 0-9)
```

### 3. Real-time Relay

Messages sent through JSR are instantly relayed to all team members:

```c
tchat_send_message(chat, "Message");      // Local history
jsr_network_send_message(net, "Message"); // Send to relay -> all players
```

### 4. Message Queuing (Offline Support)

If player loses connection, messages queue and send when reconnected:

```c
jsr_network_send_message(net, "Msg 1");  // Queued if offline
jsr_network_send_message(net, "Msg 2");  // Queued if offline
// ...later when reconnected...
jsr_network_update(net, 0.016f);  // Sends all queued messages
```

### 5. Auto-Reconnection

Disconnections trigger exponential backoff reconnect:

```
Attempt 1: Wait 1.0 seconds
Attempt 2: Wait 1.5 seconds
Attempt 3: Wait 2.25 seconds
...
Attempt N: Wait 32.0 seconds (max cap)
```

### 6. Heartbeat/Keepalive

Automatic pings every 30 seconds (configurable):

```c
jsr_network_set_ping_interval(net, 60.0);  // Ping every 60 seconds
jsr_network_set_timeout(net, 90.0);        // Timeout after 90 seconds
```

### 7. Full Sync on Connect

When connecting, automatically requests full team state:

```c
jsr_network_connect(net, key, name);  // Automatically requests sync
// Relay sends:
// - Team member list
// - Recent message history
// - Current team state
```

## Limits

| Limit | Value | Notes |
|-------|-------|-------|
| Team Key Length | 8 chars | Alphanumeric (A-Z, 0-9) |
| Username Length | 32 chars | Printable ASCII |
| Message Length | 256 chars | Per message |
| Message History | 500 msgs | Per player, local storage |
| Team Members | 64 max | Per team |
| Pending Queue | 10 msgs | Offline message queue |
| Reconnect Backoff | 32 seconds | Maximum wait time |
| Connection Timeout | 60 seconds | Configurable |
| Ping Interval | 30 seconds | Configurable |

## Example: Complete Integration

```c
#include "thermite/tchat.h"
#include "thermite/jsr_network.h"

// Application state
typedef struct {
    tchat_system* chat;
    jsr_network* net;
} app;

// Initialize
app g_app;

void init_chat() {
    // Create chat system
    g_app.chat = tchat_create("Player1");
    tchat_set_on_message_callback(g_app.chat, on_msg);
    tchat_set_on_connection_callback(g_app.chat, on_connect);
    
    // Create network client
    g_app.net = jsr_network_create("chat.example.com", 8080);
}

void join_team(const char* team_key) {
    // Join locally
    tchat_join_team(g_app.chat, team_key);
    
    // Connect to relay
    jsr_network_connect(g_app.net, team_key, "Player1");
}

void on_msg(tchat_message* msg) {
    // Render message in UI
    printf("[%s] %s\n", msg->username, msg->message);
}

void on_connect(bool connected) {
    printf("Connection: %s\n", connected ? "ON" : "OFF");
}

void game_loop() {
    while (running) {
        // Update network (process WebSocket events)
        jsr_network_update(g_app.net, delta_time);
        
        // Handle user input
        if (user_typed_message) {
            // Send to relay
            jsr_network_send_message(g_app.net, user_input);
            // Store locally
            tchat_send_message(g_app.chat, user_input);
        }
        
        // Render chat UI
        render_chat_ui(g_app.chat);
        
        // Check status
        if (jsr_network_is_connected(g_app.net)) {
            printf("Connected, pending: %d\n", 
                   jsr_network_pending_count(g_app.net));
        }
    }
}

void cleanup() {
    jsr_network_destroy(g_app.net);
    tchat_destroy(g_app.chat);
}
```

## JSR Relay Server

To run a JSR relay server (Node.js example):

```javascript
// Simple WebSocket relay server
const WebSocket = require('ws');
const http = require('http');

const server = http.createServer();
const wss = new WebSocket.Server({ server, path: '/jsr' });

const teams = new Map(); // team_key -> Set of connections

wss.on('connection', (ws) => {
    let team_key = null;
    let username = null;
    
    ws.on('message', (data) => {
        const msg_type = data[0];
        
        if (msg_type === 6) { // SYNC
            // Parse team_key and username
            // Broadcast to all in team
            if (team_key && teams.has(team_key)) {
                teams.get(team_key).forEach(conn => {
                    if (conn !== ws && conn.readyState === WebSocket.OPEN) {
                        conn.send(data);
                    }
                });
            }
        } else if (msg_type === 1) { // CHAT
            // Relay to all team members
            if (team_key && teams.has(team_key)) {
                teams.get(team_key).forEach(conn => {
                    if (conn.readyState === WebSocket.OPEN) {
                        conn.send(data);
                    }
                });
            }
        }
    });
    
    ws.on('close', () => {
        if (team_key && teams.has(team_key)) {
            teams.get(team_key).delete(ws);
        }
    });
});

server.listen(8080, () => {
    console.log('JSR relay server listening on port 8080');
});
```

## Files

```
thermite/include/thermite/
  ├── tchat.h              # Core chat API
  └── jsr_network.h        # Network relay API

thermite/src/
  ├── tchat.c              # Chat implementation
  └── jsr_network.c        # Network implementation

app/examples/
  ├── tchat_example.c      # Basic chat examples
  └── jsr_network_example.c # Network + relay examples
```

## Integration Steps

1. **Copy files** to your thermite build
2. **Link mongoose** (already in your codebase)
3. **Include headers**:
   ```c
   #include "thermite/tchat.h"
   #include "thermite/jsr_network.h"
   ```
4. **Call in game loop**:
   ```c
   jsr_network_update(net, delta_time);
   ```
5. **Build UI** to render chat messages and member list

## Testing

Run the examples:

```bash
# Compile examples (requires mongoose already linked)
gcc -I. app/examples/tchat_example.c thermite/src/tchat.c -o tchat_test
gcc -I. app/examples/jsr_network_example.c thermite/src/tchat.c thermite/src/jsr_network.c app/src/external/mongoose.c -o jsr_test

./tchat_test
./jsr_test
```

## Future Enhancements

- [ ] Message encryption (TLS)
- [ ] Persistent message database
- [ ] Voice chat support
- [ ] File sharing
- [ ] Mobile app (React Native)
- [ ] Admin moderation tools
- [ ] Message reactions/emojis
- [ ] Team roles (admin, moderator, member)

## License

GPL-3.0 (same as zoro-mod)
