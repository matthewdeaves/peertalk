# Data Model: PeerTalk SDK

**Branch**: `001-peertalk-sdk` | **Date**: 2026-02-28

## Entity Overview

```
PT_Context (1)
├── PT_PlatformOps (1) ─── platform vtable
├── PT_Peer[] (N) ─── pre-allocated peer slots
│   ├── buffers ─── per-peer TCP/UDP buffers
│   └── reassembly state
├── message_types[256] ─── type → transport mapping
├── callbacks ─── registered event handlers
└── memory_block ─── single contiguous allocation
```

## PT_Context (Internal)

The top-level SDK handle. One per application instance.
Opaque to the developer — only a pointer is exposed.

| Field | Description |
|-------|-------------|
| name | Peer name, null-terminated, max 31 chars + null |
| local_ip | Local IP address, obtained at init |
| platform_ops | Pointer to platform vtable (10 function pointers) |
| platform_state | Platform-specific opaque state (sockets, streams, endpoints) |
| peers | Array of PT_Peer, pre-allocated at init |
| max_peers | Number of peer slots allocated |
| peer_count | Current number of known peers |
| message_types | Array of 256 entries mapping type → PT_Transport |
| callbacks | Struct holding all registered callbacks + user_data pointers |
| discovery_active | Whether broadcasting is active |
| discovery_listening | Whether receiving discovery packets |
| discovery_timer | Timestamp for next broadcast (2-second interval) |
| memory_block | Single contiguous allocation, base pointer |
| memory_size | Total bytes allocated |
| current_time | Updated each PT_Poll, used for timeouts |

**Invariants**:
- `peer_count <= max_peers` always
- `memory_block` is allocated once at `PT_Init` and freed once
  at `PT_Shutdown`
- No field is modified from interrupt/ASR context — only
  platform_state's internal flags are set from callbacks

## PT_Peer (Internal)

Represents a network participant. Pre-allocated as array
within PT_Context.

| Field | Description |
|-------|-------------|
| name | Peer name, copied from discovery packet |
| state | PT_PEER_DISCOVERED, PT_PEER_CONNECTED, or PT_PEER_DISCONNECTED |
| ip_addr | Peer's IP address (from discovery source or connection) |
| last_seen | Timestamp of last discovery broadcast received |
| last_tcp_activity | Timestamp of last TCP data for inactivity timeout |
| connect_start | Timestamp when TCP connect was initiated (10s timeout) |
| in_use | Whether this slot is occupied |
| tcp_recv_buf | Pointer into memory_block, TCP receive buffer |
| tcp_recv_size | Size of TCP receive buffer |
| tcp_recv_len | Bytes currently buffered in tcp_recv_buf |
| tcp_send_buf | Pointer into memory_block, TCP send buffer |
| tcp_send_size | Size of TCP send buffer |
| udp_buf | Pointer into memory_block, UDP buffer |
| udp_buf_size | Size of UDP buffer |
| reassembly_buf | Pointer into memory_block, for chunked message reassembly |
| reassembly_buf_size | Size of reassembly buffer |
| reassembly_type | Message type being reassembled |
| reassembly_received | Count of chunks received |
| reassembly_total | Total chunks expected |
| reassembly_timer | Timestamp when reassembly started (5-second timeout) |
| reassembly_stride | Payload size of first chunk (for offset calculation) |
| platform_peer | Platform-specific per-peer state (fd, StreamPtr, EndpointRef) |

**State transitions**:
```
[empty slot]
    │
    ▼ (discovery packet received)
DISCOVERED ──────────────────────────────┐
    │                                    │
    │ PT_Connect()                       │ 10s no broadcast
    ▼                                    ▼
CONNECTED                          [slot freed]
    │                              on_peer_lost fires
    │ disconnect (quit/timeout/error)
    ▼
DISCONNECTED
    │
    │ still broadcasting → stays in list
    │ 10s no broadcast → slot freed
    │
    │ PT_Connect() again
    ▼
CONNECTED
```

**Invariants**:
- A peer in DISCOVERED state has valid ip_addr and name
- A peer in CONNECTED state has valid platform_peer (active
  TCP connection + UDP capability)
- A peer in DISCONNECTED state retains ip_addr and name
  but platform_peer resources are released back to pool
- Reassembly state is only valid when reassembly_total > 0

## PT_PlatformOps (Internal)

Platform abstraction vtable. 10 function pointers,
populated at init by the compiled-in platform module.

| Function | Description |
|----------|-------------|
| init | Initialize platform networking (open driver, bind ports) |
| shutdown | Release all platform resources |
| udp_broadcast | Send UDP datagram to 255.255.255.255 on given port |
| udp_send | Send UDP datagram to a specific peer |
| udp_listen | Begin listening on a UDP port |
| tcp_listen | Begin listening on a TCP port for incoming connections |
| tcp_connect | Initiate TCP connection to a peer |
| tcp_send | Send data over TCP to a peer |
| tcp_disconnect | Close TCP connection to a peer |
| poll | Check for I/O events and dispatch callbacks |

**Invariants**:
- All 10 function pointers are non-NULL after init
- The core never calls platform APIs directly — always
  through this vtable
- Each platform module populates this table exactly once

## Callbacks (Internal)

Struct within PT_Context holding all registered callbacks.

| Field | Callback Signature | Trigger |
|-------|--------------------|---------|
| on_peer_discovered | `void (*)(PT_Peer*, void*)` | New peer seen in discovery |
| on_peer_discovered_data | `void*` | User data for above |
| on_peer_lost | `void (*)(PT_Peer*, void*)` | Peer not seen for 10 seconds |
| on_peer_lost_data | `void*` | User data for above |
| on_connected | `void (*)(PT_Peer*, void*)` | TCP connection established |
| on_connected_data | `void*` | User data for above |
| on_disconnected | `void (*)(PT_Peer*, PT_DisconnectReason, void*)` | Connection lost |
| on_disconnected_data | `void*` | User data for above |
| on_message[256] | `void (*)(PT_Peer*, const void*, size_t, void*)` | Message received |
| on_message_data[256] | `void*` | User data per message type |
| on_error | `void (*)(PT_Status, const char*, void*)` | Error occurred |
| on_error_data | `void*` | User data for above |

**Invariants**:
- All callback pointers default to NULL (no-op if not
  registered)
- Callbacks are only invoked from within `PT_Poll` — never
  from interrupt/ASR context
- User data pointers are stored but never dereferenced by
  the SDK

## Message Type Registry (Internal)

Array of 256 entries within PT_Context.

| Index | Value | Meaning |
|-------|-------|---------|
| 0-254 | PT_Transport | PT_FAST (UDP) or PT_RELIABLE (TCP) |
| 255 | Reserved | Internal goodbye message, not settable by developer |

**Default**: All entries default to PT_RELIABLE (TCP) unless
explicitly registered with `PT_RegisterMessage`.

## Wire Protocol Structures

### Discovery Packet (UDP, port 7353)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | Magic "PTLK" (0x50, 0x54, 0x4C, 0x4B) |
| 4 | 1 | Version (2) |
| 5 | 1 | Flags (0x00 = announce, 0x01 = leave) |
| 6 | N | Peer name (null-terminated, max 32 bytes incl. null) |

Total: 38 bytes max

The **leave** flag (0x01) signals immediate peer removal. Sent automatically
by `PT_Shutdown()` before teardown so lobby peers remove the quitting peer
instantly instead of waiting for the 15-second discovery timeout.

### TCP Message Header

| Offset | Size | Field |
|--------|------|-------|
| 0 | 2 | Payload length (network byte order) |
| 2 | 1 | Message type (0-255) |
| 3 | 1 | Flags (bit 0: chunked) |

Total overhead: 4 bytes

### TCP Chunked Message Header

| Offset | Size | Field |
|--------|------|-------|
| 0 | 2 | Chunk payload length (network byte order) |
| 2 | 1 | Message type |
| 3 | 1 | Flags (bit 0 = 1) |
| 4 | 2 | Chunk sequence (network byte order) |
| 6 | 2 | Total chunks (network byte order) |

Total overhead: 8 bytes

### UDP Message Header

| Offset | Size | Field |
|--------|------|-------|
| 0 | 2 | Payload length (network byte order) |
| 2 | 1 | Message type |

Total overhead: 3 bytes

### Goodbye Message (TCP, internal)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 2 | Length: 0 |
| 2 | 1 | Type: 255 |
| 3 | 1 | Flags: 0 |

Total: 4 bytes

## Memory Layout

Single contiguous block allocated at `PT_Init`:

```
┌─────────────────────────────────────┐
│ Global State                        │
│   discovery_buffer (256 B)          │
│   platform_state (varies)           │
├─────────────────────────────────────┤
│ Peer Slot 0                         │
│   metadata (~100 B)                 │
│   tcp_recv_buf (2-8 KB)             │
│   tcp_send_buf (1-4 KB)             │
│   udp_buf (512 B)                   │
│   reassembly_buf (varies)           │
├─────────────────────────────────────┤
│ Peer Slot 1                         │
│   (same layout as slot 0)           │
├─────────────────────────────────────┤
│ ...                                 │
├─────────────────────────────────────┤
│ Peer Slot N-1                       │
│   (same layout as slot 0)           │
└─────────────────────────────────────┘
```

**Sizing formula**:
```
global_overhead = ~1 KB (discovery + platform state + context)
per_peer_cost = metadata + tcp_recv + tcp_send + udp + reassembly
              = 100 + tcp_recv_size + tcp_send_size + 512 + reassembly_size

max_peers = (available_memory - global_overhead) / per_peer_cost
```

Buffer sizes scale with available memory:

| Available RAM | tcp_recv | tcp_send | reassembly | max_peers |
|---------------|----------|----------|------------|-----------|
| ~500 KB (4 MB Mac) | 2 KB | 1 KB | 4 KB | 8-12 |
| ~2 MB (8 MB Mac) | 4 KB | 2 KB | 16 KB | 16-24 |
| ~8 MB+ (48 MB Mac) | 8 KB | 4 KB | 64 KB | 32 (cap) |
| POSIX (generous) | 8 KB | 4 KB | 64 KB | 32 (cap) |
