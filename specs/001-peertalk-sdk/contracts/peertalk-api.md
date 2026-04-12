# Public API Contract: peertalk.h

**Branch**: `001-peertalk-sdk` | **Date**: 2026-02-28

This document defines the complete public API surface of the
PeerTalk SDK. The API is 29 functions, 4 enums, 4 callback
typedefs, and 2 opaque types. All types are C89-compatible.

## Opaque Types

```c
typedef struct PT_Context PT_Context;
typedef struct PT_Peer    PT_Peer;
```

Both are forward-declared structs. The developer never
accesses their internals.

## Enums

### PT_Status — Return codes

```c
typedef enum {
    PT_OK = 0,
    PT_ERR_INIT,
    PT_ERR_NOT_CONNECTED,
    PT_ERR_SEND_FAILED,
    PT_ERR_INVALID_ARG,
    PT_ERR_NO_ROOM
} PT_Status;
```

### PT_PeerState — Peer lifecycle states

```c
typedef enum {
    PT_PEER_DISCOVERED,
    PT_PEER_CONNECTED,
    PT_PEER_DISCONNECTED
} PT_PeerState;
```

### PT_Transport — Message delivery mode

```c
typedef enum {
    PT_FAST,
    PT_RELIABLE
} PT_Transport;
```

### PT_DisconnectReason — Why a peer disconnected

```c
typedef enum {
    PT_QUIT,
    PT_TIMEOUT,
    PT_DISCONNECT_ERROR
} PT_DisconnectReason;
```

## Callback Typedefs

```c
typedef void (*PT_PeerCallback)(
    PT_Peer *peer,
    void *user_data
);

typedef void (*PT_DisconnectCallback)(
    PT_Peer *peer,
    PT_DisconnectReason reason,
    void *user_data
);

typedef void (*PT_MessageCallback)(
    PT_Peer *peer,
    const void *data,
    size_t len,
    void *user_data
);

typedef void (*PT_ErrorCallback)(
    PT_Peer *peer,
    PT_Status error,
    const char *description,
    void *user_data
);
```

## Functions (29 total)

### Lifecycle (2)

```c
PT_Status PT_Init(PT_Context **ctx, const char *name);
```
- Creates and initializes an SDK context
- `name`: peer name, max 31 characters, null-terminated
- `ctx`: receives the allocated context pointer
- Returns: PT_OK on success, PT_ERR_INIT on failure
- Allocates all memory (single block), opens network driver
- After this call, zero further allocations occur

```c
void PT_Shutdown(PT_Context *ctx);
```
- Sends goodbye to all connected peers
- Closes all connections and releases all resources
- Frees the memory block allocated at init
- After this call, `ctx` is invalid

### Discovery (2)

```c
PT_Status PT_StartDiscovery(PT_Context *ctx);
```
- Begins broadcasting discovery packets every 2 seconds
- Also begins listening for incoming discovery packets
- Returns: PT_OK on success

```c
void PT_StopDiscovery(PT_Context *ctx);
```
- Stops broadcasting but continues listening
- Already-discovered peers can still refresh their presence

### Connections (3)

```c
PT_Status PT_Connect(PT_Context *ctx, PT_Peer *peer);
```
- Initiates TCP connection to a discovered peer
- Remote side auto-accepts
- Both sides receive on_connected callback
- `peer` must be in DISCOVERED or DISCONNECTED state
- Returns: PT_OK on success, PT_ERR_NOT_CONNECTED if peer
  not in valid state, PT_ERR_NO_ROOM if resources exhausted

```c
void PT_Disconnect(PT_Context *ctx, PT_Peer *peer);
```
- Sends goodbye message and closes TCP connection
- Peer state changes to DISCONNECTED
- Fires on_disconnected with PT_QUIT on both sides

```c
void PT_DisconnectAll(PT_Context *ctx);
```
- Disconnects all currently connected peers with goodbye frames
- Convenience function for lifecycle transitions (game to lobby)
- Iterates all peer slots; only CONNECTED peers are affected
- Fires on_disconnected with PT_QUIT for each disconnected peer
- Discovery state is not affected
- No-op if no peers are connected or ctx is NULL
- Safe to call from on_disconnected callback (re-entrant)

### Messaging (3)

```c
void PT_RegisterMessage(
    PT_Context *ctx,
    unsigned char type,
    PT_Transport transport
);
```
- Declares how messages of `type` should be delivered
- `type`: 0-254 (255 is reserved for goodbye)
- `transport`: PT_FAST (UDP) or PT_RELIABLE (TCP)
- Unregistered types default to PT_RELIABLE

```c
PT_Status PT_Send(
    PT_Context *ctx,
    PT_Peer *peer,
    unsigned char type,
    const void *data,
    size_t len
);
```
- Sends a message to a specific peer
- Transport determined by registered type
- For PT_RELIABLE: chunks automatically if len > buffer size
- For PT_FAST: fails with PT_ERR_SEND_FAILED if len > ~1400
- Returns: PT_OK on success

```c
PT_Status PT_Broadcast(
    PT_Context *ctx,
    unsigned char type,
    const void *data,
    size_t len
);
```
- Sends a message to all connected peers
- Same chunking/size rules as PT_Send
- Returns: PT_OK if sent to at least one peer

### Event Loop (1)

```c
void PT_Poll(PT_Context *ctx);
```
- Drives all I/O: checks sockets/streams/endpoints for
  events, processes discovery, fires callbacks
- Must be called regularly (at least once per frame or
  event loop iteration)
- All callbacks are invoked from within this function
- Never called from interrupt context

### Callback Registration (6)

```c
void PT_OnPeerDiscovered(
    PT_Context *ctx,
    PT_PeerCallback cb,
    void *user_data
);
```

```c
void PT_OnPeerLost(
    PT_Context *ctx,
    PT_PeerCallback cb,
    void *user_data
);
```

```c
void PT_OnConnected(
    PT_Context *ctx,
    PT_PeerCallback cb,
    void *user_data
);
```

```c
void PT_OnDisconnected(
    PT_Context *ctx,
    PT_DisconnectCallback cb,
    void *user_data
);
```

```c
void PT_OnMessage(
    PT_Context *ctx,
    unsigned char type,
    PT_MessageCallback cb,
    void *user_data
);
```

```c
void PT_OnError(
    PT_Context *ctx,
    PT_ErrorCallback cb,
    void *user_data
);
```

### Configuration (1)

```c
PT_Status PT_SetName(PT_Context *ctx, const char *name);
```
- Changes the local peer name after init
- `name`: new peer name, max 31 characters, null-terminated, must not be NULL
- Returns: PT_OK on success, PT_ERR_INVALID_ARG if ctx/name is NULL or name too long
- The next discovery broadcast will advertise the new name
- Does not allocate memory

### Peer Info (7)

```c
int PT_GetPeerCount(PT_Context *ctx);
```
- Returns number of known peers (all states)

```c
PT_Peer *PT_GetPeer(PT_Context *ctx, int index);
```
- Returns peer at index, or NULL if out of range
- Index range: 0 to PT_GetPeerCount()-1

```c
const char *PT_PeerName(PT_Peer *peer);
```
- Returns the peer's display name (null-terminated)

```c
const char *PT_PeerAddress(PT_Peer *peer);
```
- Returns the peer's IP address as a dotted-quad string (e.g. "10.0.1.5")
- Returns empty string if peer is NULL
- The string is stored in the peer's internal buffer (valid for the peer's lifetime)

```c
const char *PT_LocalAddress(const PT_Context *ctx);
```
- Returns the local peer's IP address as a dotted-quad string
- Returns empty string if ctx is NULL
- The string is stored in a static internal buffer (valid until next call)

```c
PT_Status PT_SendUDPBroadcast(PT_Context *ctx, unsigned short port,
                              const void *data, size_t len);
```
- Sends a raw UDP broadcast to the specified port
- Used for application-level broadcast (e.g., clog UDP sink)
- Returns: PT_OK on success, PT_ERR_INVALID_ARG if ctx/data is NULL

```c
PT_PeerState PT_GetPeerState(const PT_Peer *peer);
```
- Returns the peer's current state

### Peer Ranking (1)

```c
int PT_GetPeerRank(const PT_Context *ctx, const PT_Peer *peer);
```
- Returns 0-based rank of `peer` among all connected peers + self, sorted by IP address
- Lowest IP = rank 0
- Pass `NULL` for `peer` to get the local machine's rank
- Returns -1 on error (NULL ctx, peer not connected)
- Uses internal `ip_addr` fields directly — no string parsing

### Debug Broadcast (3)

```c
PT_Status PT_EnableDebugBroadcast(PT_Context *ctx, unsigned short port);
```
- Enables UDP debug broadcast channel
- `port`: UDP port to broadcast on. Pass 0 for default (7356)
- Builds a prefix string `[name@ip] ` used by `PT_DebugSend`
- Returns: PT_OK on success, PT_ERR_INVALID_ARG if ctx is NULL
- If `PT_SetName()` is called while broadcast is active, the prefix is rebuilt automatically

```c
void PT_DebugSend(PT_Context *ctx, const char *msg, size_t len);
```
- Broadcasts a debug text message via UDP
- Auto-prefixes with `[name@ip] ` and appends newline
- No-op if debug broadcast is not enabled or ctx is NULL
- Uses a separate static buffer (not `udp_send_buf`) — safe to call from message callbacks
- Maximum message length: ~200 bytes (truncated to fit 256-byte buffer with prefix)
- No clog dependency — apps wire clog's network sink into this function if desired

```c
void PT_DisableDebugBroadcast(PT_Context *ctx);
```
- Disables debug broadcast
- Idempotent — safe to call multiple times or when already disabled

## C89 Compatibility Notes

- All types use C89 primitives: `unsigned char`,
  `unsigned short`, `size_t`, `int`, `const char *`
- No `<stdint.h>` dependency in the public header
- No `bool` type — functions return `void` or `PT_Status`
- Opaque struct pointers avoid exposing internal layout
- The public header requires no platform-specific includes

## Thread Safety

The SDK is NOT thread-safe. All functions must be called
from the same thread/context that called `PT_Init`. This is
by design — Classic Mac has no threading model, and the
poll-based architecture eliminates the need for locks.

## Ports

| Port | Protocol | Purpose |
|------|----------|---------|
| 7353 | UDP | Discovery broadcast/listen |
| 7354 | TCP | Reliable messages + connections |
| 7355 | UDP | Fast messages |
| 7356 | UDP | Debug broadcast (default, configurable) |

## Reserved Values

| Value | Reserved For |
|-------|-------------|
| Message type 254 | Internal TCP keepalive |
| Message type 255 | Internal goodbye message |
| Discovery magic "PTLK" | Protocol identification |
| Discovery version 1 | Current wire protocol version |
| Port 7356 | Default debug broadcast port (`PT_DEBUG_PORT`) |
