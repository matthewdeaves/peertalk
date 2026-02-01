# PeerTalk: Project Goals

## What is PeerTalk?

**PeerTalk is a C SDK for adding peer-to-peer LAN networking to applications.**

It is *not* an application itself. PeerTalk is a library that developers integrate into their own software—whether that's a chess game, a chat client, a collaborative drawing tool, or anything else that needs to discover and communicate with peers on a local network.

```
┌─────────────────────────────────────────────────────────────────┐
│  YOUR APPLICATION                                               │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐             │
│  │ Chess Game  │  │ Chat Client │  │ File Sync   │  ... etc    │
│  │    UI       │  │     UI      │  │    UI       │             │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘             │
│         │                │                │                     │
│         └────────────────┼────────────────┘                     │
│                          │                                      │
│                          ▼                                      │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │                    PeerTalk SDK                           │ │
│  │  • Peer discovery      • Connection management            │ │
│  │  • UDP messaging       • TCP reliable streams             │ │
│  │  • Structured data     • Event callbacks                  │ │
│  └───────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
                               │
          ┌────────────────────┼────────────────────┐
          ▼                    ▼                    ▼
    ┌──────────┐        ┌──────────┐        ┌──────────┐
    │  POSIX   │        │  MacTCP  │        │ Open     │
    │ Sockets  │        │  (68k)   │        │Transport │
    └──────────┘        └──────────┘        └──────────┘
```

## Target Platforms

| Platform | Systems | Use Case | Transport |
|----------|---------|----------|-----------|
| **POSIX** | Linux, macOS, BSD | Modern development, reference implementation | TCP/UDP |
| **MacTCP** | System 6.0.8 - 7.5.5 | Classic 68k Macs (required for 68000/68020) | TCP/UDP |
| **Open Transport** | System 7.6.1+ / Mac OS 8-9 | PPC Macs, late 68040s | TCP/UDP |
| **AppleTalk** | System 6+ (any Mac) | Networks without TCP/IP, LocalTalk | ADSP |

### Multi-Transport Support

Macs with both TCP/IP and AppleTalk can participate in **both** networks simultaneously using unified libraries:

| Library | Description |
|---------|-------------|
| `libpeertalk_mactcp_at.a` | MacTCP + AppleTalk for 68k Macs |
| `libpeertalk_ot_at.a` | Open Transport + AppleTalk for PPC Macs |

With unified libraries, a single Mac can discover and communicate with:
- POSIX peers via TCP/IP
- Other MacTCP/OT Macs via TCP/IP
- AppleTalk-only Macs via ADSP

**Note:** POSIX peers cannot communicate with AppleTalk-only peers (no AppleTalk implementation for Linux/macOS).

## What Developers Get

### 1. PT_Log: Cross-Platform Logging

PeerTalk includes **PT_Log**, a standalone logging library that works independently or alongside the networking SDK. Any C application targeting POSIX or Classic Mac can use it.

```c
/* Standalone usage - no PeerTalk needed */
PT_Log *log = PT_LogCreate();
PT_LogSetFile(log, "myapp.log");
PT_LogSetLevel(log, PT_LOG_INFO);  /* ERR, WARN, INFO visible; DEBUG hidden */

/* Use app-defined categories */
#define LOG_UI    PT_LOG_CAT_APP1
#define LOG_GAME  PT_LOG_CAT_APP2

PT_INFO(log, LOG_UI, "Window opened: %s", window_name);
PT_ERR(log, LOG_GAME, "Failed to load level %d", level_num);
PT_DEBUG(log, LOG_GAME, "Player at %d,%d", x, y);  /* Filtered out */

PT_LogDestroy(log);
```

**PT_Log features:**

| Feature | Description |
|---------|-------------|
| **Level filtering** | ERR, WARN, INFO, DEBUG — runtime configurable |
| **Category filtering** | Bitmask for subsystems (8 reserved + 8 app-defined) |
| **Multiple outputs** | File, console, and custom callback simultaneously |
| **Performance logging** | Structured entries for benchmarking |
| **Production-ready** | Included by default; opt-in stripping for minimal builds |
| **Zero platform code** | Same API on Linux, macOS, System 6-9 |

**Build options:**

| Define | Behavior |
|--------|----------|
| (default) | Full logging, runtime level control |
| `PT_LOG_STRIP` | All logging removed (zero overhead) |
| `PT_LOG_MIN_LEVEL=2` | Only ERR/WARN compiled in |

**Capturing logs programmatically:**

```c
void my_log_handler(PT_LogLevel level, PT_LogCategory cat,
                    uint32_t timestamp_ms, const char *msg, void *user) {
    /* Display in your UI, send to server, etc. */
    if (level == PT_LOG_ERR) {
        show_error_dialog(msg);
    }
}

PT_LogSetCallback(log, my_log_handler, NULL);
PT_LogSetOutput(log, PT_LOG_OUT_FILE | PT_LOG_OUT_CALLBACK);
```

**PeerTalk uses PT_Log internally.** You can share a log context or let PeerTalk create its own:

```c
/* Option 1: Let PeerTalk create its own log */
PeerTalk_Config config = {0};
config.log_filename = "peertalk.log";
PeerTalk_Context *ctx = PeerTalk_Init(&config);

/* Option 2: Share your app's log with PeerTalk */
PT_Log *log = PT_LogCreate();
PT_LogSetFile(log, "myapp.log");

PeerTalk_Config config = {0};
config.log = log;  /* PeerTalk uses your log */
PeerTalk_Context *ctx = PeerTalk_Init(&config);

/* Both app and PeerTalk messages go to same file */
PT_INFO(log, PT_LOG_CAT_APP1, "App started");
/* PeerTalk logs: "[00001234][INF] Peer discovered: Alice's Mac" */
```

### 2. Structured Data for Your UI

PeerTalk maintains internal state and exposes it through clean data structures. Your application queries what it needs and displays it however you want:

```c
/* Get the current peer list for your UI */
PeerTalk_PeerInfo peers[16];
uint16_t count;
PeerTalk_GetPeers(ctx, peers, 16, &count);

/* Each peer has structured data your UI can display */
for (int i = 0; i < count; i++) {
    printf("Peer: %s - %s (latency: %dms)\n",
           peers[i].name,              /* "Alice's Mac SE" */
           peers[i].connected ? "Connected" : "Available",
           peers[i].latency_ms);       /* Network latency */

    /* Check available transports */
    if (peers[i].transports_available & PT_TRANSPORT_TCP)
        printf("  - TCP/IP available\n");
    if (peers[i].transports_available & PT_TRANSPORT_APPLETALK)
        printf("  - AppleTalk available\n");
}
```

**Data structures the SDK surfaces:**
- **Peer list**: Name, transports available, connection state, flags
- **Connection state**: Connecting, connected, disconnecting, error codes
- **Message queue status**: Pending sends, buffer space available, queue pressure
- **Network statistics**: Bytes sent/received, latency estimates, connection quality
- **Transport info**: Which transports are available (TCP, UDP, AppleTalk)

### 2. Event-Driven Callbacks

Your application registers callbacks for network events. PeerTalk notifies you; you update your UI:

```c
void on_peer_discovered(PeerTalk_Context *ctx,
                        const PeerTalk_PeerInfo *peer,
                        void *user_data) {
    MyGameLobby *lobby = (MyGameLobby *)user_data;
    lobby_add_player(lobby, peer->name, peer->id);
    lobby_refresh_ui(lobby);
}

void on_message_received(PeerTalk_Context *ctx,
                         PeerTalk_PeerID from,
                         const void *data,
                         uint16_t length,
                         void *user_data) {
    MyChessGame *game = (MyChessGame *)user_data;
    ChessMove *move = (ChessMove *)data;
    game_apply_opponent_move(game, move);
    game_redraw_board(game);
}
```

**Events your application can handle:**
- `on_peer_discovered` - New peer appeared on network
- `on_peer_lost` - Peer went offline or timed out
- `on_peer_connected` - Connection established
- `on_peer_disconnected` - Connection closed (with reason)
- `on_message_received` - Data arrived from a peer (TCP or UDP)
- `on_connection_request` - Incoming connection request (accept/reject with reason)
- `on_error` - Network error occurred (recoverable or fatal)

### 3. Flexible Messaging

PeerTalk doesn't dictate your message format. Send whatever your application needs:

```c
/* Chess game: send a move */
ChessMove move = { .from = "e2", .to = "e4", .piece = PAWN };
PeerTalk_Send(ctx, opponent_id, &move, sizeof(move));

/* Chat app: send a text message */
const char *msg = "Hello, world!";
PeerTalk_Send(ctx, peer_id, msg, strlen(msg) + 1);

/* File sync: send a chunk */
PeerTalk_Send(ctx, peer_id, file_chunk, chunk_size);

/* Game state: send via UDP for low latency */
GameState state = get_current_state();
PeerTalk_SendUDP(ctx, peer_id, &state, sizeof(state));

/* Advanced: send with priority and coalescing for real-time games */
PlayerPosition pos = get_player_position();
PeerTalk_SendEx(ctx, peer_id, &pos, sizeof(pos),
                PT_SEND_UNRELIABLE | PT_SEND_COALESCE_NEWEST,
                PT_PRIORITY_REALTIME,
                PT_COALESCE_PLAYER_POS);  /* Only latest position matters */
```

**Transport options:**
- **TCP**: Reliable, ordered delivery for commands, chat, files
- **UDP**: Low-latency, best-effort for real-time game state
- **AppleTalk ADSP**: Reliable streams for Macs without TCP/IP
- **Both simultaneously**: Use TCP for critical data, UDP for frequent updates

**Message priorities:**
- `PT_PRIORITY_REALTIME` - Immediate send (game state, input)
- `PT_PRIORITY_HIGH` - Important messages (commands, chat)
- `PT_PRIORITY_NORMAL` - Default (general data)
- `PT_PRIORITY_LOW` - Background (file transfers, bulk data)

**Coalescing options:**
- `PT_COALESCE_NONE` - Send all messages
- `PT_COALESCE_NEWEST` - Replace older messages with same coalesce ID
- `PT_COALESCE_OLDEST` - Keep first message, discard duplicates

### 4. Zero Platform-Specific Code

Your application code is identical across all platforms:

```c
/* This exact code works on Linux, Mac OS 9, and System 6 */
PeerTalk_Context *ctx = PeerTalk_Init(&config);
PeerTalk_StartDiscovery(ctx);

while (app_running) {
    PeerTalk_Poll(ctx);      /* Process network events */
    update_game_logic();
    render_frame();
}

PeerTalk_Shutdown(ctx);
```

You link against the appropriate library for your target:
- `libpeertalk.a` for POSIX (TCP/UDP)
- `libpeertalk_mactcp.a` for MacTCP (TCP/UDP)
- `libpeertalk_ot.a` for Open Transport (TCP/UDP)
- `libpeertalk_at.a` for AppleTalk only (ADSP)
- `libpeertalk_mactcp_at.a` for MacTCP + AppleTalk (unified)
- `libpeertalk_ot_at.a` for Open Transport + AppleTalk (unified)

## API Design Philosophy

### Simple and Intuitive

```c
/* Initialize */
PeerTalk_Context *ctx = PeerTalk_Init(&config);

/* Discover peers */
PeerTalk_StartDiscovery(ctx);

/* Connect to a peer */
PeerTalk_Connect(ctx, peer_id);

/* Send data */
PeerTalk_Send(ctx, peer_id, data, length);

/* Process events (call regularly) */
PeerTalk_Poll(ctx);

/* Clean up */
PeerTalk_Shutdown(ctx);
```

### Predictable Error Handling

Every function returns a status code. No exceptions, no hidden failures:

```c
PeerTalk_Error err = PeerTalk_Connect(ctx, peer_id);
if (err != PT_OK) {
    switch (err) {
        case PT_ERR_NOT_FOUND:    /* Peer doesn't exist */
        case PT_ERR_ALREADY:      /* Already connected */
        case PT_ERR_RESOURCE:     /* Out of connections */
        case PT_ERR_NETWORK:      /* Network failure */
    }
}
```

### Non-Blocking by Default

PeerTalk never blocks your application. All operations are asynchronous:

```c
/* This returns immediately */
PeerTalk_Connect(ctx, peer_id);

/* Connection completes later; you're notified via callback */
void on_connected(PeerTalk_Context *ctx, PeerTalk_PeerID peer, void *ud) {
    /* Now you can send data */
}
```

### Network Statistics

Monitor network health and performance in real-time:

```c
/* Global statistics */
PeerTalk_Stats stats;
PeerTalk_GetStats(ctx, &stats);
printf("Total: %lu bytes sent, %lu received\n",
       stats.bytes_sent, stats.bytes_received);

/* Per-peer statistics */
PeerTalk_PeerStats peer_stats;
PeerTalk_GetPeerStats(ctx, peer_id, &peer_stats);
printf("Peer latency: %d ms, quality: %d%%\n",
       peer_stats.latency_ms, peer_stats.quality);
```

### Resource-Aware

On memory-constrained Classic Macs, PeerTalk adapts automatically:

| Available RAM | Max Connections | Buffer Size |
|---------------|-----------------|-------------|
| > 500 KB | 8-12 | 16 KB each |
| 200-500 KB | 4-6 | 8 KB each |
| < 200 KB | 2-4 | 4 KB each |

## Example Applications

The SDK includes example applications for **testing and demonstration only**. These are not the product—they exist to:

1. **Validate the API** during development (dogfooding)
2. **Demonstrate integration patterns** for developers
3. **Test cross-platform communication** between POSIX and Classic Mac
4. **Serve as starting points** for your own applications
5. **Showcase PT_Log best practices** as a gold-standard logging example

| Example | Platform | Purpose |
|---------|----------|---------|
| `chat_posix` | Linux/macOS | Reference implementation, ncurses UI |
| `chat_mac` | Classic Mac | Minimal console UI, shows event loop integration |

### Logging in Example Apps

The example chat applications demonstrate PT_Log best practices:

```c
/* Define app-specific categories */
#define LOG_UI      PT_LOG_CAT_APP1   /* UI events */
#define LOG_CHAT    PT_LOG_CAT_APP2   /* Chat messages */
#define LOG_NET     PT_LOG_CAT_NETWORK /* Network (shared with PeerTalk) */

/* Create shared log for app + PeerTalk */
PT_Log *log = PT_LogCreate();
PT_LogSetFile(log, "chat.log");
PT_LogSetLevel(log, PT_LOG_INFO);
PT_LogSetOutput(log, PT_LOG_OUT_FILE | PT_LOG_OUT_CALLBACK);

/* Callback displays errors in UI */
PT_LogSetCallback(log, chat_log_handler, &chat_state);

/* Pass to PeerTalk */
config.log = log;
ctx = PeerTalk_Init(&config);

/* App logging */
PT_INFO(log, LOG_UI, "Chat window opened");
PT_INFO(log, LOG_CHAT, "Message from %s: %s", peer_name, message);
PT_DEBUG(log, LOG_NET, "Packet: %d bytes", packet_len);

/* PeerTalk logs to same file with its own categories */
/* Result: unified log with all app + network activity */
```

**Your application will have its own UI, its own logic, and its own purpose.** PeerTalk and PT_Log just handle the infrastructure.

## Supported Application Types

PeerTalk is designed to support a wide range of networked applications:

| Application Type | Example Use Cases | Recommended Transport |
|------------------|-------------------|----------------------|
| **Turn-based Games** | Chess, card games, board games | TCP (reliable moves) |
| **Real-time Games** | Action games, simulations | UDP (state) + TCP (commands) |
| **Chat Applications** | Text chat, presence | TCP (messages) |
| **File Transfer** | Document sharing, sync | TCP (reliable, ordered) |
| **Collaborative Tools** | Shared whiteboards, editors | TCP or UDP depending on data |
| **Custom Protocols** | Anything you design | Your choice |

## SDK Deliverables

### Public Headers
| Component | Description |
|-----------|-------------|
| `include/peertalk.h` | PeerTalk networking API |
| `include/pt_log.h` | PT_Log standalone logging API |

### PT_Log Library
| Component | Description |
|-----------|-------------|
| `libptlog.a` | POSIX logging library |
| `libptlog_mac.a` | Classic Mac logging library (68k and PPC) |

**Note:** PT_Log can be used standalone without PeerTalk. It's also statically linked into all PeerTalk libraries below.

### Single-Transport Libraries
| Component | Description |
|-----------|-------------|
| `libpeertalk.a` | POSIX static library (TCP/UDP) |
| `libpeertalk_mactcp.a` | MacTCP static library (68k, TCP/UDP) |
| `libpeertalk_ot.a` | Open Transport static library (PPC, TCP/UDP) |
| `libpeertalk_at.a` | AppleTalk static library (all Macs, ADSP only) |

### Unified Multi-Transport Libraries
| Component | Description |
|-----------|-------------|
| `libpeertalk_mactcp_at.a` | MacTCP + AppleTalk (68k, both networks) |
| `libpeertalk_ot_at.a` | Open Transport + AppleTalk (PPC, both networks) |

### Unified Library Features
When using `libpeertalk_mactcp_at.a` or `libpeertalk_ot_at.a`:
- Discover peers on **both** TCP/IP and AppleTalk simultaneously
- Same peer appearing on both networks is deduplicated (one entry, multiple transports)
- Send to AppleTalk-only Macs and TCP/IP peers from the same context
- Query available transports via `PeerTalk_GetAvailableTransports()`

## Technical Details

### Protocol Constants

| Constant | Value | Purpose |
|----------|-------|---------|
| Discovery magic | `PTLK` | Identifies UDP discovery packets |
| Message magic | `PTMG` | Identifies TCP/UDP message frames |
| Protocol version | 1 | For future compatibility |
| Max peer name | 31 chars | Pascal string compatible |

### Configuration Options

| Setting | Default | Description |
|---------|---------|-------------|
| Discovery port | 7353 | UDP broadcast port |
| TCP port | 7354 | Default connection port |
| UDP port | 7355 | Default messaging port |
| Discovery interval | 5000 ms | Broadcast frequency |
| Peer timeout | 15000 ms | Inactivity removal |

All settings configurable at runtime via `PeerTalk_Config`.

### Network Stack Details

**Compile-Time Selection**: Each platform has a separate library. This is intentional:
- Different architectures require different compilers (68k vs PPC)
- Network APIs are fundamentally incompatible
- Smaller binaries for memory-constrained systems

**AppleTalk Note**: AppleTalk peers can communicate with any peer that has AppleTalk capability. For Macs with both TCP/IP and AppleTalk, use unified libraries (`libpeertalk_mactcp_at.a` or `libpeertalk_ot_at.a`) to participate in both networks. POSIX peers cannot reach AppleTalk-only peers (no bridging).

## Definition of Success

PeerTalk succeeds when a developer can:

1. **Add LAN networking to their application in minutes**, not days
2. **Write zero platform-specific code**—same logic on POSIX and Classic Mac
3. **Access clean, structured data** for their UI (peer lists, states, statistics)
4. **Handle events naturally** through callbacks, not polling raw sockets
5. **Send any data format** they need—the SDK doesn't impose structure on payloads
6. **Trust the library** to manage resources appropriately on constrained hardware
7. **Add production-quality logging** with PT_Log—same API everywhere, zero overhead when stripped

In short: **two headers, unified APIs, any platform, your application.**

PT_Log succeeds when:
- Classic Mac developers have a logging library that "just works" on System 6-9
- Applications can log in production builds without sacrificing performance
- The same logging code compiles unchanged on POSIX and Classic Mac
- Developers can capture, filter, and redirect logs with minimal effort

## Development Resources

### Quick Reference

See `CLAUDE.md` for interrupt-safety rules, buffer management patterns, and platform-specific constraints.

### Reference Documentation

| Document | Location | Coverage |
|----------|----------|----------|
| MacTCP Programmer's Guide | `books/` | ASR rules, stream lifecycle |
| Networking With Open Transport | `books/` | Notifiers, endpoints |
| Programming With AppleTalk | `books/` | NBP, ADSP |
| Inside Macintosh Vol VI | `books/` | Memory Manager, interrupts |
| VintageMacTCPIP Reference | `books/` | System compatibility guide |

### Build Environment

- **POSIX**: Standard gcc/clang
- **Classic Mac**: Retro68 cross-compiler at `/home/matthew/Retro68`
