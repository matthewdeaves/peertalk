# PHASE 2: Protocol Layer

> **Status:** DONE
> **Depends on:** Phase 1 (Foundation)
> **Produces:** Message framing, peer data structures, queue implementation
> **Risk Level:** Low
> **Estimated Sessions:** 3
> **Review Applied:** 2026-01-28 (implementability review - field access alignment with Phase 1, OT atomic signedness, canary location, queue slot ordering, coalesce optimization, pt_peer_find_by_name for Phase 5.9, logging context fix, canary_corrupt flag, cascade backpressure logging, CONNECTED state logging, queue init logging, AppleTalk compatibility clarification)
> **Review Applied:** 2026-01-29 (comprehensive /review - added PT_MAGIC_UDP and UDP encode/decode functions, added pt_strerror() for error string mapping, added DOD architectural note for address/port in hot struct, enhanced logging documentation)
> **CSend Lessons:** See [CSEND-LESSONS.md](CSEND-LESSONS.md) Part C for cross-platform protocol gotchas

## Overview

Phase 2 implements the platform-agnostic protocol layer: message framing/parsing, peer lifecycle management, and the message queue system. This code is shared across all three platforms and contains no networking - it's pure data structure manipulation.

**Key Principle:** This layer must be rigorously tested since bugs here will manifest as mysterious network failures later.

**Portability Note:** All code in this phase MUST use the `pt_*` functions from `pt_compat.h` instead of standard library functions. This ensures the code works on Classic Mac where there is no standard C library.

| Standard Function | Use Instead | Notes |
|-------------------|-------------|-------|
| `memcpy()` | `pt_memcpy()` | Uses BlockMoveData on Mac |
| `memcpy()` in ISR | `pt_memcpy_isr()` | Manual byte copy, ISR-safe |
| `memset()` | `pt_memset()` | |
| `memcmp()` | `pt_memcmp()` | |
| `strlen()` | `pt_strlen()` | |
| `strcmp()` | `pt_strcmp()` | NOT YET IMPLEMENTED - use inline byte comparison |
| `strncpy()` | `pt_strncpy()` | Always null-terminates |
| `snprintf()` | `pt_snprintf()` | |
| `malloc()` | `pt_alloc()` | |
| `calloc()` | `pt_alloc_clear()` | |
| `free()` | `pt_free()` | |

**Phase 1 Compatibility Note:** This phase uses types and structures defined in Phase 1. Specifically:
- **Error codes:** Use `PeerTalk_Error` from `peertalk.h`, extended with protocol-specific codes
- **Peer state:** Use `pt_peer_state` from `pt_types.h`, extended with `PT_PEER_STATE_UNUSED`
- **Peer structure:** Extend `struct pt_peer` from `pt_internal.h` with framing buffers
- **Magic numbers:** Use `PT_CONTEXT_MAGIC`, `PT_PEER_MAGIC`, `PT_QUEUE_MAGIC` from `pt_types.h`

**Logging Categories:** Phase 2 uses these PT_Log categories from `pt_log.h`:

| Category | Usage |
|----------|-------|
| `PT_LOG_CAT_PROTOCOL` | Protocol encode/decode errors (CRC, magic, version, truncation) |
| `PT_LOG_CAT_CONNECT` | Peer lifecycle events (create, destroy, state transitions) |
| `PT_LOG_CAT_MEMORY` | Memory allocation, canary validation |
| `PT_LOG_CAT_PERF` | Queue backpressure warnings |

Applications can filter Phase 2 logs using `PT_LogSetCategories()`. For example, to see only connection events: `PT_LogSetCategories(log, PT_LOG_CAT_CONNECT)`.

## Phase 1 Prerequisites

**Note:** These changes have been incorporated into PHASE-1-FOUNDATION.md as of 2026-01-26.
This section documents what was added for reference.

### 1. Update `pt_peer_state` in `src/core/pt_types.h`

```c
/* Change from: */
typedef enum {
    PT_PEER_STATE_DISCOVERED = 0,
    PT_PEER_STATE_CONNECTING,
    PT_PEER_STATE_CONNECTED,
    PT_PEER_STATE_DISCONNECTING,
    PT_PEER_STATE_FAILED
} pt_peer_state;

/* To: */
typedef enum {
    PT_PEER_STATE_UNUSED = 0,       /* Slot available for allocation */
    PT_PEER_STATE_DISCOVERED,       /* Discovered but not connected */
    PT_PEER_STATE_CONNECTING,       /* Connection in progress */
    PT_PEER_STATE_CONNECTED,        /* Fully connected */
    PT_PEER_STATE_DISCONNECTING,    /* Disconnect in progress */
    PT_PEER_STATE_FAILED            /* Connection failed */
} pt_peer_state;
```

### 2. Add error codes to `PeerTalk_Error` in `include/peertalk.h`

```c
/* Add after PT_ERR_WOULD_BLOCK = -19: */
    PT_ERR_CRC              = -20,  /* CRC validation failed */
    PT_ERR_MAGIC            = -21,  /* Invalid magic number */
    PT_ERR_TRUNCATED        = -22,  /* Packet too short */
    PT_ERR_VERSION          = -23,  /* Protocol version mismatch */
    PT_ERR_NOT_POWER2       = -24,  /* Capacity not power of two */
```

### 3. Add framing fields to `struct pt_peer` in `src/core/pt_internal.h`

```c
/* Add at end of struct pt_peer, before closing brace: */

    /* Protocol framing buffers (Phase 2)
     *
     * IMPORTANT: Canaries MUST immediately follow their protected buffers
     * to detect overflows. Do not reorder these fields.
     */
    uint16_t obuflen;               /* Bytes in obuf */
    uint16_t ibuflen;               /* Bytes in ibuf */
    uint8_t obuf[768];              /* Output framing buffer */
    uint8_t ibuf[512];              /* Input framing buffer */

#ifdef PT_DEBUG
    /* Debug-only buffer overflow canaries (Phase 2)
     *
     * These fields are only present in debug builds to detect buffer overflows.
     * MUST immediately follow their respective buffers.
     * Saves ~8 bytes per peer in release builds.
     */
    uint32_t obuf_canary;           /* = PT_CANARY_OBUF (0xDEAD0B0F) */
    uint32_t ibuf_canary;           /* = PT_CANARY_IBUF (0xDEAD1B1F) */
#endif

    /* Connection handle and sequence numbers (Phase 2) */
    void *connection;               /* Platform-specific handle */
    uint8_t send_seq;               /* Send sequence number */
    uint8_t recv_seq;               /* Receive sequence number */
    pt_tick_t connect_start;        /* When connection was initiated */

    /* Queue references (Phase 2/3) */
    struct pt_queue *send_queue;
    struct pt_queue *recv_queue;
```

### 4. Add canary constants to `src/core/pt_types.h`

```c
/* Add after PT_QUEUE_MAGIC definition: */

/* Buffer canary values for overflow detection */
#define PT_CANARY_OBUF  0xDEAD0B0F  /* Output buffer canary */
#define PT_CANARY_IBUF  0xDEAD1B1F  /* Input buffer canary */
```

### 5. Verify Phase 1 Hot/Cold Data Split (Performance Critical)

**Before implementing Phase 2, verify that Phase 1 provides:**

1. **address and port in pt_peer_hot struct** (NOT in pt_peer_cold)
   - Required for cache-efficient `pt_peer_find_by_addr()` on 68030
   - Without this, every incoming packet causes ~22KB of cache thrashing
   - **CRITICAL:** This function is called on EVERY incoming packet - must access only hot data

2. **name_idx in pt_peer_hot struct** (within first 32 bytes)
   - Required for cache-efficient `pt_peer_find_by_name()`
   - Index into centralized `ctx->peer_names[]` array

3. **sizeof(pt_peer_hot) == 32 bytes** (compile-time assertion)
   - Ensures hot data fits in single cache line access

If Phase 1 does not provide these, add them before proceeding with Phase 2 implementation.

### 6. DOD Architectural Notes (from Plan Review 2026-01-29)

**Queue Metadata Separation (Fix Later - implementation optimization):**

The current `pt_queue_slot` design embeds 256-byte data in each slot (260 bytes total). With 16 slots, that's 4160 bytes per queue - far exceeding the 68030's 256-byte L1 cache.

**Future optimization:** Consider separating slot metadata (6 bytes: length, priority, flags, data_offset) from the data pool. This allows iterating metadata (to find ready slots) without pulling large data payloads into cache. Not required for Phase 2 implementation but documented for future optimization.

**CRC-16 Table Size Note:**

The 256-entry CRC lookup table is 512 bytes. On 68030 (256-byte cache), this exceeds cache capacity. For extreme optimization, consider a 16-entry nibble table (32 bytes) with two lookups per byte. Current implementation is acceptable for Phase 2.

---

## Goals

1. Implement wire protocol for discovery (UDP) and messaging (TCP)
2. Create peer tracking data structures with lifecycle management
3. Implement pre-allocated message queues for burst handling
4. Validate with comprehensive unit tests

## Session Scope Table

| Session | Focus | Status | Files Created/Modified | Tests | Verify |
|---------|-------|--------|------------------------|-------|--------|
| 2.1 | Message Framing | [DONE] | `src/core/protocol.c`, `src/core/protocol.h` | `tests/test_protocol.c` | Frame encode/decode round-trip |
| 2.2 | Peer Management | [DONE] | `src/core/peer.c`, `src/core/peer.h` | `tests/test_peer.c` | Peer add/remove/lookup, state transitions |
| 2.3 | Message Queues | [DONE] | `src/core/queue.c`, `src/core/queue.h` | `tests/test_queue.c` | Push/pop, backpressure, ISR-safe |

### Status Key
- **[OPEN]** - Not started
- **[IN PROGRESS]** - Currently being worked on
- **[READY TO TEST]** - Implementation complete, needs verification
- **[DONE]** - Verified and complete

---

## Session 2.1: Message Framing

### Objective
Implement the wire protocol for both discovery packets (UDP) and message frames (TCP), including CRC-16 validation.

### Protocol Specifications

#### Discovery Packet Format (UDP)
```
+--------+--------+--------+--------+
| Magic (4 bytes): "PTLK"           |
+--------+--------+--------+--------+
| Version (1) | Type (1) | Flags(2)|
+--------+--------+--------+--------+
| Sender Port (2) | Transports (1) |  Name Len (1)
+--------+---------+----------------+
| Peer Name (up to 31 bytes)       |
+----------------------------------+
| Checksum (2 bytes CRC-16)        |
+----------------------------------+

Total: 12 + name_len + 2 = 14-45 bytes

Types:
  0x01 = ANNOUNCE  (periodic broadcast)
  0x02 = QUERY     (request all peers to announce)
  0x03 = GOODBYE   (peer leaving)

Flags (big-endian, matches PT_PEER_FLAG_* from peertalk.h):
  0x0001 = HOST              (peer is game host)
  0x0002 = ACCEPTING         (accepting new connections)
  0x0004 = SPECTATOR         (spectator mode)
  0x0008 = READY             (ready to start)
  0x0100-0x8000 = APP flags  (application-defined)

Transports (bitmask of available transports on sender):
  0x01 = TCP available
  0x02 = UDP available
  0x04 = AppleTalk available
```

#### Message Frame Format (TCP)
```
+--------+--------+--------+--------+
| Magic (4 bytes): "PTMG"           |
+--------+--------+--------+--------+
| Version | Type   | Flags | Seq#   |
+--------+--------+--------+--------+
| Payload Length (2 bytes, big-end) |
+--------+--------+-----------------+
| Payload (0-65535 bytes)           |
+----------------------------------+
| CRC-16 (2 bytes)                 |
+----------------------------------+

Header: 10 bytes
Total: 10 + payload_len + 2 = 12 to 65547 bytes

Types:
  0x01 = DATA          (application message)
  0x02 = PING          (keepalive request)
  0x03 = PONG          (keepalive response)
  0x04 = DISCONNECT    (graceful close)
  0x05 = ACK           (acknowledgment)
  0x06 = REJECT        (connection rejected, payload = reason byte)

Flags (matches PT_SEND_* from peertalk.h):
  0x01 = UNRELIABLE    (don't require ACK)
  0x02 = COALESCABLE   (can be replaced by newer)
  0x04 = NO_DELAY      (disable Nagle for this message)
  0x08 = BATCH         (contains multiple sub-messages)
```

#### UDP Message Format (for unreliable messaging)
```
+--------+--------+--------+--------+
| Magic (4 bytes): "PTUD"           |
+--------+--------+--------+--------+
| Sender Port (2) | Payload Len (2) |
+--------+--------+-----------------+
| Payload (0-65535 bytes)           |
+----------------------------------+

Header: 8 bytes
Total: 8 + payload_len bytes

Note: No CRC for UDP messages - UDP has its own checksum.
This format is simpler than TCP framing since UDP preserves
message boundaries and doesn't require streaming reassembly.
```

### Tasks

#### Task 2.1.1: Create `src/core/protocol.h`

```c
/*
 * PeerTalk Wire Protocol
 *
 * Defines packet formats for discovery (UDP) and messaging (TCP).
 * All multi-byte values are big-endian (network byte order).
 */

#ifndef PT_PROTOCOL_H
#define PT_PROTOCOL_H

#include "pt_types.h"

/* Protocol version */
#define PT_PROTOCOL_VERSION 1

/*
 * Error codes - extends PeerTalk_Error from peertalk.h
 *
 * Phase 1 defines PT_OK (0) through PT_ERR_INTERNAL (-99).
 * Phase 2 adds protocol-specific errors in the -20 to -29 range.
 *
 * IMPORTANT: These values must be added to PeerTalk_Error in peertalk.h
 * before implementing this phase.
 */
#define PT_ERR_CRC          (-20)  /* CRC validation failed */
#define PT_ERR_MAGIC        (-21)  /* Invalid magic number */
#define PT_ERR_TRUNCATED    (-22)  /* Packet too short */
#define PT_ERR_VERSION      (-23)  /* Protocol version mismatch */
#define PT_ERR_NOT_POWER2   (-24)  /* Capacity not power of two */

/* Convenience aliases mapping to Phase 1 error codes */
#define PT_ERR_INVALID      PT_ERR_INVALID_PARAM   /* From peertalk.h */
#define PT_ERR_FULL         PT_ERR_BUFFER_FULL     /* From peertalk.h */
#define PT_ERR_EMPTY        PT_ERR_QUEUE_EMPTY     /* From peertalk.h */
#define PT_ERR_MEMORY       PT_ERR_NO_MEMORY       /* From peertalk.h */
#define PT_ERR_STATE        PT_ERR_INVALID_STATE   /* From peertalk.h */

/* Magic values (as 32-bit integers for comparison) */
#define PT_MAGIC_DISCOVERY  0x50544C4B  /* "PTLK" */
#define PT_MAGIC_MESSAGE    0x50544D47  /* "PTMG" */
#define PT_MAGIC_UDP        0x50545544  /* "PTUD" */

/* Discovery packet types */
#define PT_DISC_TYPE_ANNOUNCE   0x01
#define PT_DISC_TYPE_QUERY      0x02
#define PT_DISC_TYPE_GOODBYE    0x03

/* Discovery flags (match PT_PEER_FLAG_* from peertalk.h) */
#define PT_DISC_FLAG_HOST       0x0001
#define PT_DISC_FLAG_ACCEPTING  0x0002
#define PT_DISC_FLAG_SPECTATOR  0x0004
#define PT_DISC_FLAG_READY      0x0008
/* 0x0100-0x8000 reserved for application flags */

/* Transport bitmask (in discovery packet) */
#define PT_DISC_TRANSPORT_TCP       0x01
#define PT_DISC_TRANSPORT_UDP       0x02
#define PT_DISC_TRANSPORT_APPLETALK 0x04

/* Message types */
typedef enum {
    PT_MSG_DATA       = 0x01,
    PT_MSG_PING       = 0x02,
    PT_MSG_PONG       = 0x03,
    PT_MSG_DISCONNECT = 0x04,
    PT_MSG_ACK        = 0x05,
    PT_MSG_REJECT     = 0x06   /* Connection rejected (payload = reason) */
} pt_message_type;

/* Message flags (match PT_SEND_* from peertalk.h) */
#define PT_MSG_FLAG_UNRELIABLE  0x01
#define PT_MSG_FLAG_COALESCABLE 0x02
#define PT_MSG_FLAG_NO_DELAY    0x04
#define PT_MSG_FLAG_BATCH       0x08

/* Maximum sizes */
#define PT_DISCOVERY_MAX_SIZE   48
#define PT_PEER_NAME_MAX        31
#define PT_MESSAGE_MAX_PAYLOAD  65535
#define PT_MESSAGE_HEADER_SIZE  10
#define PT_UDP_HEADER_SIZE      8   /* Magic(4) + Port(2) + Len(2) */

/* Parsed discovery packet */
typedef struct {
    uint8_t  version;
    uint8_t  type;
    uint16_t flags;             /* PT_DISC_FLAG_* (matches PT_PEER_FLAG_*) */
    uint16_t sender_port;
    uint8_t  transports;        /* PT_DISC_TRANSPORT_* bitmask */
    uint8_t  name_len;
    char     name[PT_PEER_NAME_MAX + 1];
} pt_discovery_packet;

/* Parsed message header */
typedef struct {
    uint8_t  version;
    uint8_t  type;
    uint8_t  flags;
    uint8_t  sequence;
    uint16_t payload_len;
} pt_message_header;

/* Encode discovery packet. Returns bytes written or -1 on error. */
int pt_discovery_encode(const pt_discovery_packet *pkt,
                        uint8_t *buf, size_t buf_len);

/* Decode discovery packet. Returns 0 on success, error code on failure.
 * ctx: Optional logging context (can be NULL to disable logging) */
int pt_discovery_decode(struct pt_context *ctx, const uint8_t *buf, size_t len,
                        pt_discovery_packet *pkt);

/* Encode message header. Returns PT_MESSAGE_HEADER_SIZE on success. */
int pt_message_encode_header(const pt_message_header *hdr,
                             uint8_t *buf);

/* Decode message header. Returns 0 on success, error code on failure.
 * ctx: Optional logging context (can be NULL to disable logging) */
int pt_message_decode_header(struct pt_context *ctx, const uint8_t *buf, size_t len,
                             pt_message_header *hdr);

/* Calculate CRC-16 for buffer */
uint16_t pt_crc16(const uint8_t *data, size_t len);

/* Continue CRC-16 calculation with existing CRC value.
 * Used to compute CRC over non-contiguous data (header + payload). */
uint16_t pt_crc16_update(uint16_t crc, const uint8_t *data, size_t len);

/* Validate CRC-16 (returns 0 if valid) */
int pt_crc16_check(const uint8_t *data, size_t len, uint16_t expected);

/*============================================================================
 * UDP Message Framing (for unreliable messaging)
 *
 * UDP messages use a simpler 8-byte header without CRC (UDP has its own checksum).
 * Format: Magic(4) + SenderPort(2) + PayloadLen(2) + Payload
 *============================================================================*/

/* Encode UDP message. Returns total bytes written (header + payload) or -1 on error.
 * buf must have room for PT_UDP_HEADER_SIZE + payload_len bytes. */
int pt_udp_encode(const void *payload, uint16_t payload_len,
                  uint16_t sender_port, uint8_t *buf, size_t buf_len);

/* Decode UDP message header. Returns 0 on success, error code on failure.
 * On success: *sender_port_out, *payload_out (pointer into buf), *payload_len_out set.
 * ctx: Optional logging context (can be NULL to disable logging) */
int pt_udp_decode(struct pt_context *ctx, const uint8_t *buf, size_t len,
                  uint16_t *sender_port_out, const void **payload_out, uint16_t *payload_len_out);

/*============================================================================
 * Error String Mapping
 *============================================================================*/

/* Convert error code to human-readable string for logging.
 * Returns static string - do not free. Returns "Unknown error" for invalid codes. */
const char *pt_strerror(int err);

#endif /* PT_PROTOCOL_H */
```

#### Task 2.1.2: Create `src/core/protocol.c`

```c
/*
 * PeerTalk Wire Protocol Implementation
 */

#include "protocol.h"
#include "pt_compat.h"
#include "pt_log.h"

/*
 * CRC-16-CCITT lookup table
 *
 * Polynomial: x^16 + x^12 + x^5 + 1 (0x1021)
 * Reflected polynomial: 0x8408 (bit-reversed 0x1021)
 *
 * IMPORTANT: This uses init=0x0000 (not the more common init=0xFFFF variant).
 * Check value: pt_crc16("123456789", 9) == 0x2189
 *
 * Table generated for byte-at-a-time calculation with initial value 0x0000.
 */
static const uint16_t crc16_table[256] = {
    0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
    0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
    0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
    0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
    0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
    0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
    0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
    0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
    0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
    0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
    0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
    0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
    0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
    0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
    0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
    0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
    0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
    0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
    0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
    0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
    0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
    0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
    0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
    0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
    0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
    0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
    0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
    0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
    0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
    0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
    0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
    0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78
};

uint16_t pt_crc16(const uint8_t *data, size_t len) {
    /*
     * CRC-16 with reflected polynomial 0x8408 (bit-reversed 0x1021).
     * Init: 0x0000, no final XOR.
     * Check value: pt_crc16("123456789", 9) == 0x2189
     */
    uint16_t crc = 0x0000;  /* Initial value */

    if (!data || len == 0)
        return crc;

    while (len--) {
        crc = (crc >> 8) ^ crc16_table[(crc ^ *data++) & 0xFF];
    }
    return crc;
}

int pt_crc16_check(const uint8_t *data, size_t len, uint16_t expected) {
    return pt_crc16(data, len) == expected ? 0 : -1;
}

uint16_t pt_crc16_update(uint16_t crc, const uint8_t *data, size_t len) {
    /*
     * Continue CRC calculation with existing CRC value.
     * Used to compute CRC over non-contiguous data (header + payload).
     *
     * Example usage:
     *   crc = pt_crc16(header, header_len);
     *   crc = pt_crc16_update(crc, payload, payload_len);
     */
    if (!data || len == 0)
        return crc;

    while (len--) {
        crc = (crc >> 8) ^ crc16_table[(crc ^ *data++) & 0xFF];
    }
    return crc;
}

int pt_discovery_encode(const pt_discovery_packet *pkt,
                        uint8_t *buf, size_t buf_len) {
    size_t total_len;
    uint16_t crc;
    size_t pos = 0;

    /* Calculate total length */
    total_len = 12 + pkt->name_len + 2;  /* header + name + crc */

    if (buf_len < total_len)
        return -1;
    if (pkt->name_len > PT_PEER_NAME_MAX)
        return -1;

    /* Magic (big-endian) */
    buf[pos++] = 'P';
    buf[pos++] = 'T';
    buf[pos++] = 'L';
    buf[pos++] = 'K';

    /* Version, Type, Flags */
    buf[pos++] = PT_PROTOCOL_VERSION;
    buf[pos++] = pkt->type;
    buf[pos++] = (pkt->flags >> 8) & 0xFF;
    buf[pos++] = pkt->flags & 0xFF;

    /* Sender port (big-endian) */
    buf[pos++] = (pkt->sender_port >> 8) & 0xFF;
    buf[pos++] = pkt->sender_port & 0xFF;

    /* Transports and name length */
    buf[pos++] = pkt->transports;
    buf[pos++] = pkt->name_len;

    /* Name */
    pt_memcpy(buf + pos, pkt->name, pkt->name_len);
    pos += pkt->name_len;

    /* CRC-16 (big-endian) */
    crc = pt_crc16(buf, pos);
    buf[pos++] = (crc >> 8) & 0xFF;
    buf[pos++] = crc & 0xFF;

    return (int)pos;
}

int pt_discovery_decode(struct pt_context *ctx, const uint8_t *buf, size_t len,
                        pt_discovery_packet *pkt) {
    uint16_t crc_received, crc_calculated;
    size_t pos = 0;

    /* Minimum size check */
    if (len < 14) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_PROTOCOL,
            "Discovery packet truncated: %lu bytes (min 14)", (unsigned long)len);
        return PT_ERR_TRUNCATED;
    }

    /* Check magic */
    if (buf[0] != 'P' || buf[1] != 'T' ||
        buf[2] != 'L' || buf[3] != 'K') {
        PT_LOG_WARN(ctx, PT_LOG_CAT_PROTOCOL,
            "Discovery packet bad magic: %02X%02X%02X%02X (expected PTLK)",
            buf[0], buf[1], buf[2], buf[3]);
        return PT_ERR_MAGIC;
    }
    pos = 4;

    /* Version check */
    pkt->version = buf[pos++];
    if (pkt->version != PT_PROTOCOL_VERSION) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_PROTOCOL,
            "Discovery packet version mismatch: got %u, expected %u",
            pkt->version, PT_PROTOCOL_VERSION);
        return PT_ERR_VERSION;
    }

    /* Type */
    pkt->type = buf[pos++];
    if (pkt->type < PT_DISC_TYPE_ANNOUNCE || pkt->type > PT_DISC_TYPE_GOODBYE) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_PROTOCOL,
            "Discovery packet invalid type: %u", pkt->type);
        return PT_ERR_INVALID;
    }

    /* Flags (big-endian) */
    pkt->flags = (buf[pos] << 8) | buf[pos + 1];
    pos += 2;

    /* Sender port (big-endian) */
    pkt->sender_port = (buf[pos] << 8) | buf[pos + 1];
    pos += 2;

    /* Transports and name length */
    pkt->transports = buf[pos++];
    {
        uint8_t name_len = buf[pos++];

        if (name_len > PT_PEER_NAME_MAX) {
            PT_LOG_WARN(ctx, PT_LOG_CAT_PROTOCOL,
                "Discovery packet name too long: %u (max %u)",
                name_len, PT_PEER_NAME_MAX);
            return PT_ERR_INVALID;
        }
        if (len < 12 + name_len + 2) {
            PT_LOG_WARN(ctx, PT_LOG_CAT_PROTOCOL,
                "Discovery packet truncated: %lu bytes, need %u",
                (unsigned long)len, 12 + name_len + 2);
            return PT_ERR_TRUNCATED;
        }

        pkt->name_len = name_len;
    }

    /* Name */
    pt_memcpy(pkt->name, buf + pos, pkt->name_len);
    pkt->name[pkt->name_len] = '\0';
    pos += pkt->name_len;

    /* Verify CRC */
    crc_received = (buf[pos] << 8) | buf[pos + 1];
    crc_calculated = pt_crc16(buf, pos);

    if (crc_received != crc_calculated) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_PROTOCOL,
            "Discovery packet CRC mismatch: got 0x%04X, expected 0x%04X",
            crc_received, crc_calculated);
        return PT_ERR_CRC;
    }

    return PT_OK;
}

int pt_message_encode_header(const pt_message_header *hdr,
                             uint8_t *buf) {
    /* Magic */
    buf[0] = 'P';
    buf[1] = 'T';
    buf[2] = 'M';
    buf[3] = 'G';

    /* Header fields */
    buf[4] = PT_PROTOCOL_VERSION;
    buf[5] = hdr->type;
    buf[6] = hdr->flags;
    buf[7] = hdr->sequence;

    /* Payload length (big-endian) */
    buf[8] = (hdr->payload_len >> 8) & 0xFF;
    buf[9] = hdr->payload_len & 0xFF;

    return PT_MESSAGE_HEADER_SIZE;
}

int pt_message_decode_header(struct pt_context *ctx, const uint8_t *buf, size_t len,
                             pt_message_header *hdr) {
    if (len < PT_MESSAGE_HEADER_SIZE) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_PROTOCOL,
            "Message header truncated: %lu bytes (need %u)",
            (unsigned long)len, PT_MESSAGE_HEADER_SIZE);
        return PT_ERR_TRUNCATED;
    }

    /* Check magic */
    if (buf[0] != 'P' || buf[1] != 'T' ||
        buf[2] != 'M' || buf[3] != 'G') {
        PT_LOG_WARN(ctx, PT_LOG_CAT_PROTOCOL,
            "Message header bad magic: %02X%02X%02X%02X (expected PTMG)",
            buf[0], buf[1], buf[2], buf[3]);
        return PT_ERR_MAGIC;
    }

    /* Version check */
    hdr->version = buf[4];
    if (hdr->version != PT_PROTOCOL_VERSION) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_PROTOCOL,
            "Message header version mismatch: got %u, expected %u",
            hdr->version, PT_PROTOCOL_VERSION);
        return PT_ERR_VERSION;
    }

    hdr->type = buf[5];
    hdr->flags = buf[6];
    hdr->sequence = buf[7];
    hdr->payload_len = (buf[8] << 8) | buf[9];

    return PT_OK;
}

/*============================================================================
 * UDP Message Framing
 *============================================================================*/

int pt_udp_encode(const void *payload, uint16_t payload_len,
                  uint16_t sender_port, uint8_t *buf, size_t buf_len) {
    size_t total_size = PT_UDP_HEADER_SIZE + payload_len;

    if (buf_len < total_size)
        return -1;

    /* Magic: "PTUD" - byte-by-byte for portability */
    buf[0] = 'P';
    buf[1] = 'T';
    buf[2] = 'U';
    buf[3] = 'D';

    /* Sender port (big-endian) */
    buf[4] = (sender_port >> 8) & 0xFF;
    buf[5] = sender_port & 0xFF;

    /* Payload length (big-endian) */
    buf[6] = (payload_len >> 8) & 0xFF;
    buf[7] = payload_len & 0xFF;

    /* Copy payload */
    if (payload_len > 0 && payload)
        pt_memcpy(buf + PT_UDP_HEADER_SIZE, payload, payload_len);

    return (int)total_size;
}

int pt_udp_decode(struct pt_context *ctx, const uint8_t *buf, size_t len,
                  uint16_t *sender_port_out, const void **payload_out, uint16_t *payload_len_out) {
    uint16_t payload_len;

    if (len < PT_UDP_HEADER_SIZE) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_PROTOCOL,
            "UDP message too short: %u bytes (min %d)",
            (unsigned)len, PT_UDP_HEADER_SIZE);
        return PT_ERR_TRUNCATED;
    }

    /* Magic check: "PTUD" */
    if (buf[0] != 'P' || buf[1] != 'T' || buf[2] != 'U' || buf[3] != 'D') {
        PT_LOG_WARN(ctx, PT_LOG_CAT_PROTOCOL,
            "UDP message invalid magic: %02X%02X%02X%02X",
            buf[0], buf[1], buf[2], buf[3]);
        return PT_ERR_MAGIC;
    }

    /* Parse sender port (big-endian) */
    *sender_port_out = (buf[4] << 8) | buf[5];

    /* Parse payload length (big-endian) */
    payload_len = (buf[6] << 8) | buf[7];

    /* Verify we have enough data */
    if (len < PT_UDP_HEADER_SIZE + payload_len) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_PROTOCOL,
            "UDP message truncated: header says %u bytes, have %u",
            payload_len, (unsigned)(len - PT_UDP_HEADER_SIZE));
        return PT_ERR_TRUNCATED;
    }

    *payload_len_out = payload_len;
    *payload_out = (payload_len > 0) ? (buf + PT_UDP_HEADER_SIZE) : NULL;

    return PT_OK;
}

/*============================================================================
 * Error String Mapping
 *============================================================================*/

const char *pt_strerror(int err) {
    switch (err) {
    case PT_OK:             return "Success";
    case PT_ERR_CRC:        return "CRC validation failed";
    case PT_ERR_MAGIC:      return "Invalid magic number";
    case PT_ERR_TRUNCATED:  return "Packet too short";
    case PT_ERR_VERSION:    return "Protocol version mismatch";
    case PT_ERR_NOT_POWER2: return "Capacity not power of two";
    /* Phase 1 errors (from peertalk.h) */
    case PT_ERR_INVALID_PARAM:  return "Invalid parameter";
    case PT_ERR_NO_MEMORY:      return "Out of memory";
    case PT_ERR_BUFFER_FULL:    return "Buffer full";
    case PT_ERR_QUEUE_EMPTY:    return "Queue empty";
    case PT_ERR_PEER_NOT_FOUND: return "Peer not found";
    case PT_ERR_NOT_CONNECTED:  return "Not connected";
    case PT_ERR_TIMEOUT:        return "Operation timed out";
    case PT_ERR_WOULD_BLOCK:    return "Would block";
    case PT_ERR_BACKPRESSURE:   return "Queue backpressure";
    case PT_ERR_RESOURCE:       return "Resource exhausted";
    default:                    return "Unknown error";
    }
}
```

#### Task 2.1.3: Create `tests/test_protocol.c`

```c
/*
 * PeerTalk Protocol Tests
 */

#include <stdio.h>
#include <assert.h>
#include "protocol.h"
#include "pt_compat.h"

void test_discovery_round_trip(void) {
    pt_discovery_packet original, decoded;
    uint8_t buf[PT_DISCOVERY_MAX_SIZE];
    int len;

    /* Setup original packet */
    original.type = PT_DISC_TYPE_ANNOUNCE;
    original.flags = PT_DISC_FLAG_ACCEPTING;
    original.sender_port = 5001;
    original.transports = PT_DISC_TRANSPORT_TCP | PT_DISC_TRANSPORT_UDP;
    pt_strncpy(original.name, "TestPeer", sizeof(original.name));
    original.name_len = pt_strlen(original.name);

    /* Encode */
    len = pt_discovery_encode(&original, buf, sizeof(buf));
    assert(len > 0);

    /* Decode */
    assert(pt_discovery_decode(NULL, buf, len, &decoded) == 0);

    /* Verify */
    assert(decoded.type == original.type);
    assert(decoded.flags == original.flags);
    assert(decoded.sender_port == original.sender_port);
    assert(decoded.transports == original.transports);
    assert(decoded.name_len == original.name_len);
    assert(pt_memcmp(decoded.name, original.name, original.name_len) == 0);

    printf("test_discovery_round_trip: PASSED\n");
}

void test_message_header_round_trip(void) {
    pt_message_header original, decoded;
    uint8_t buf[PT_MESSAGE_HEADER_SIZE];

    /* Setup original */
    original.type = PT_MSG_DATA;
    original.flags = PT_MSG_FLAG_NO_DELAY | PT_MSG_FLAG_COALESCABLE;
    original.sequence = 42;
    original.payload_len = 1234;

    /* Encode */
    assert(pt_message_encode_header(&original, buf) == PT_MESSAGE_HEADER_SIZE);

    /* Decode */
    assert(pt_message_decode_header(NULL, buf, sizeof(buf), &decoded) == 0);

    /* Verify */
    assert(decoded.type == original.type);
    assert(decoded.flags == original.flags);
    assert(decoded.sequence == original.sequence);
    assert(decoded.payload_len == original.payload_len);

    printf("test_message_header_round_trip: PASSED\n");
}

void test_crc_corruption(void) {
    pt_discovery_packet pkt, decoded;
    uint8_t buf[PT_DISCOVERY_MAX_SIZE];
    int len;

    pkt.type = PT_DISC_TYPE_ANNOUNCE;
    pkt.flags = 0;
    pkt.sender_port = 5000;
    pkt.transports = PT_DISC_TRANSPORT_TCP;
    pt_strncpy(pkt.name, "Test", sizeof(pkt.name));
    pkt.name_len = 4;

    len = pt_discovery_encode(&pkt, buf, sizeof(buf));
    assert(len > 0);

    /* Corrupt one byte */
    buf[8] ^= 0xFF;

    /* Decode should fail */
    assert(pt_discovery_decode(NULL, buf, len, &decoded) != 0);

    printf("test_crc_corruption: PASSED\n");
}

void test_invalid_magic(void) {
    uint8_t buf[20] = "XXXX";  /* Invalid magic */
    pt_discovery_packet pkt;

    assert(pt_discovery_decode(NULL, buf, sizeof(buf), &pkt) != 0);
    printf("test_invalid_magic: PASSED\n");
}

void test_invalid_version(void) {
    pt_discovery_packet pkt, decoded;
    uint8_t buf[PT_DISCOVERY_MAX_SIZE];
    int len;

    pkt.type = PT_DISC_TYPE_ANNOUNCE;
    pkt.flags = 0;
    pkt.sender_port = 5000;
    pkt.transports = PT_DISC_TRANSPORT_TCP;
    pt_strncpy(pkt.name, "Test", sizeof(pkt.name));
    pkt.name_len = 4;

    len = pt_discovery_encode(&pkt, buf, sizeof(buf));
    assert(len > 0);

    /* Corrupt version byte */
    buf[4] = 99;

    /* Recalculate CRC to isolate version check */
    uint16_t crc = pt_crc16(buf, len - 2);
    buf[len - 2] = (crc >> 8) & 0xFF;
    buf[len - 1] = crc & 0xFF;

    /* Decode should fail due to version mismatch */
    assert(pt_discovery_decode(NULL, buf, len, &decoded) != 0);

    printf("test_invalid_version: PASSED\n");
}

void test_truncated_packet(void) {
    pt_discovery_packet pkt, decoded;
    uint8_t buf[PT_DISCOVERY_MAX_SIZE];
    int len;

    pkt.type = PT_DISC_TYPE_ANNOUNCE;
    pkt.flags = 0;
    pkt.sender_port = 5000;
    pkt.transports = PT_DISC_TRANSPORT_TCP;
    pt_strncpy(pkt.name, "TestPeer", sizeof(pkt.name));
    pkt.name_len = 8;

    len = pt_discovery_encode(&pkt, buf, sizeof(buf));
    assert(len > 0);

    /* Try to decode truncated packet */
    assert(pt_discovery_decode(NULL, buf, len - 5, &decoded) != 0);
    assert(pt_discovery_decode(NULL, buf, 10, &decoded) != 0);  /* Too short */

    printf("test_truncated_packet: PASSED\n");
}

void test_crc16_known_values(void) {
    /*
     * Test with known CRC-16 check value.
     * Our algorithm uses reflected poly 0x8408, init 0x0000.
     * Standard test: CRC of "123456789" = 0x2189
     */
    uint8_t data1[] = "123456789";
    uint16_t crc1 = pt_crc16(data1, 9);
    assert(crc1 == 0x2189);  /* Known check value */

    /* Empty/NULL data should return initial value */
    uint16_t crc2 = pt_crc16(NULL, 0);
    assert(crc2 == 0x0000);

    uint16_t crc3 = pt_crc16(data1, 0);
    assert(crc3 == 0x0000);  /* Zero length returns initial value */

    printf("test_crc16_known_values: PASSED\n");
}

void test_crc16_update(void) {
    /*
     * Test incremental CRC calculation for non-contiguous data.
     * CRC of whole buffer must equal CRC computed in two parts.
     */
    uint8_t full[] = "Hello, World!";
    uint8_t part1[] = "Hello, ";
    uint8_t part2[] = "World!";

    /* Full CRC in one call */
    uint16_t crc_full = pt_crc16(full, 13);

    /* CRC computed incrementally */
    uint16_t crc_part = pt_crc16(part1, 7);
    crc_part = pt_crc16_update(crc_part, part2, 6);

    assert(crc_full == crc_part);

    /* Test with header + payload pattern (typical message framing) */
    uint8_t header[10] = {0x50, 0x54, 0x4D, 0x47, 0x01, 0x01, 0x00, 0x00, 0x00, 0x05};
    uint8_t payload[5] = {'H', 'e', 'l', 'l', 'o'};
    uint8_t combined[15];
    pt_memcpy(combined, header, 10);
    pt_memcpy(combined + 10, payload, 5);

    uint16_t crc_combined = pt_crc16(combined, 15);
    uint16_t crc_incremental = pt_crc16(header, 10);
    crc_incremental = pt_crc16_update(crc_incremental, payload, 5);

    assert(crc_combined == crc_incremental);

    printf("test_crc16_update: PASSED\n");
}

void test_udp_round_trip(void) {
    uint8_t buf[128];
    const char *test_payload = "Hello UDP!";
    uint16_t payload_len = 10;
    uint16_t sender_port = 7355;
    int encoded_len;

    /* Encode */
    encoded_len = pt_udp_encode(test_payload, payload_len, sender_port, buf, sizeof(buf));
    assert(encoded_len == PT_UDP_HEADER_SIZE + payload_len);

    /* Verify magic */
    assert(buf[0] == 'P' && buf[1] == 'T' && buf[2] == 'U' && buf[3] == 'D');

    /* Decode */
    uint16_t decoded_port;
    const void *decoded_payload;
    uint16_t decoded_len;

    int result = pt_udp_decode(NULL, buf, encoded_len, &decoded_port, &decoded_payload, &decoded_len);
    assert(result == PT_OK);
    assert(decoded_port == sender_port);
    assert(decoded_len == payload_len);
    assert(pt_memcmp(decoded_payload, test_payload, payload_len) == 0);

    /* Test truncated */
    assert(pt_udp_decode(NULL, buf, 4, &decoded_port, &decoded_payload, &decoded_len) == PT_ERR_TRUNCATED);

    /* Test invalid magic */
    buf[0] = 'X';
    assert(pt_udp_decode(NULL, buf, encoded_len, &decoded_port, &decoded_payload, &decoded_len) == PT_ERR_MAGIC);

    printf("test_udp_round_trip: PASSED\n");
}

void test_strerror(void) {
    const char *msg;

    /* Test known error codes using PeerTalk_ErrorString() */
    msg = PeerTalk_ErrorString(PT_ERR_CRC);
    assert(strcmp(msg, "CRC validation failed") == 0);

    msg = PeerTalk_ErrorString(PT_ERR_MAGIC);
    assert(strcmp(msg, "Invalid magic number") == 0);

    /* Test code is POSIX-only, can use strcmp() directly */
    printf("test_strerror: PASSED\n");
}

int main(void) {
    printf("PeerTalk Protocol Tests\n");
    printf("=======================\n\n");

    test_discovery_round_trip();
    test_message_header_round_trip();
    test_crc_corruption();
    test_invalid_magic();
    test_invalid_version();
    test_truncated_packet();
    test_crc16_known_values();
    test_crc16_update();
    test_udp_round_trip();
    test_strerror();

    printf("\n=======================\n");
    printf("All protocol tests PASSED!\n");
    return 0;
}
```

### Acceptance Criteria
1. Discovery packet encode/decode round-trip succeeds
2. Message header encode/decode round-trip succeeds
3. CRC-16 detects single-bit corruption
4. Invalid magic is rejected
5. Invalid version is rejected
6. Truncated packets are rejected
7. Name length > 31 is rejected
8. **NEW:** UDP message encode/decode round-trip succeeds
9. **NEW:** UDP invalid magic and truncation are rejected
10. **NEW:** pt_strerror() returns correct strings for all error codes

---

## Session 2.2: Peer Management

### Objective
Implement peer tracking with state machine for lifecycle management, including timeout tracking and connection state.

### Tasks

#### Task 2.2.1: Create `src/core/peer.h`

**Phase 1 Integration Note:** This file extends Phase 1's `struct pt_peer` (from `pt_internal.h`) with protocol framing fields. Before implementing, Phase 1's `pt_types.h` must be updated to add `PT_PEER_STATE_UNUSED = 0` to `pt_peer_state`, shifting other values to 1-5.

```c
/*
 * PeerTalk Peer Management
 *
 * This module provides peer lifecycle operations for Phase 2.
 * It uses pt_peer_state and extends struct pt_peer from Phase 1.
 */

#ifndef PT_PEER_H
#define PT_PEER_H

#include "pt_types.h"
#include "pt_internal.h"

/* PT_CANARY_OBUF and PT_CANARY_IBUF are defined in pt_types.h */

/*
 * pt_tick_t: Platform tick type (defined in pt_types.h)
 *
 * Requirements:
 * - Must be monotonically increasing
 * - No wraparound expected within 49 days (uint32_t at 1000 ticks/sec)
 * - Resolution: at least 1ms granularity recommended
 *
 * Platform implementations:
 * - Classic Mac: Use TickCount() (60 ticks/sec = ~16.7ms resolution)
 * - POSIX: Use milliseconds from clock_gettime(CLOCK_MONOTONIC)
 *
 * Timeout calculations assume: (now - then) gives elapsed ticks
 * Convert between platforms:
 *   PT_TICKS_PER_SECOND_CLASSIC_MAC = 60
 *   PT_TICKS_PER_SECOND_POSIX       = 1000
 */

/*
 * Peer states - uses pt_peer_state from pt_types.h
 *
 * Phase 1 must define these values in pt_types.h:
 *   PT_PEER_STATE_UNUSED        = 0   (slot available for allocation)
 *   PT_PEER_STATE_DISCOVERED    = 1   (discovered but not connected)
 *   PT_PEER_STATE_CONNECTING    = 2   (connection in progress)
 *   PT_PEER_STATE_CONNECTED     = 3   (fully connected)
 *   PT_PEER_STATE_DISCONNECTING = 4   (disconnect in progress)
 *   PT_PEER_STATE_FAILED        = 5   (connection failed)
 *
 * This enum is defined in pt_types.h, NOT here.
 * We define convenience aliases for shorter code:
 */
#define PT_PEER_UNUSED        PT_PEER_STATE_UNUSED
#define PT_PEER_DISCOVERED    PT_PEER_STATE_DISCOVERED
#define PT_PEER_CONNECTING    PT_PEER_STATE_CONNECTING
#define PT_PEER_CONNECTED     PT_PEER_STATE_CONNECTED
#define PT_PEER_DISCONNECTING PT_PEER_STATE_DISCONNECTING
#define PT_PEER_FAILED        PT_PEER_STATE_FAILED

/*
 * Phase 2 extensions to struct pt_peer (from pt_internal.h)
 *
 * The base struct pt_peer is defined in pt_internal.h with:
 *   - magic, info, state, last_seen, last_discovery
 *   - addresses[], address_count, preferred_transport
 *   - peer_flags, stats
 *   - ping_sent_time, rtt_samples[], rtt_index, rtt_count
 *
 * Phase 1 must add these fields to struct pt_peer before implementing Phase 2:
 */

/* === BEGIN PHASE 1 ADDITIONS (add to struct pt_peer in pt_internal.h) === */
#if 0  /* These fields must be added to Phase 1's struct pt_peer */

    /* Protocol framing buffers (Phase 2) */
    /*
     * These buffers are for MESSAGE FRAMING (headers, reassembly),
     * NOT for full application payloads.
     *
     * For large DATA messages (up to PT_MAX_MESSAGE_SIZE = 8192):
     * - Network layer uses separate, dynamically-sized buffers
     * - Callbacks deliver data via zero-copy pointers when possible
     * - These buffers handle protocol overhead only
     *
     * Sizes chosen for:
     * - obuf: header (10) + small payload + CRC (2) + margin
     * - ibuf: partial frame reassembly for streaming
     *
     * IMPORTANT: Canaries MUST immediately follow their protected buffers
     * to detect overflows. Do not reorder these fields.
     */
    uint16_t obuflen;
    uint16_t ibuflen;
    uint8_t obuf[768];
    uint8_t ibuf[512];

#ifdef PT_DEBUG
    /* Debug-only buffer overflow canaries
     * MUST immediately follow their respective buffers */
    uint32_t obuf_canary;           /* = PT_CANARY_OBUF */
    uint32_t ibuf_canary;           /* = PT_CANARY_IBUF */
#endif

    /* Connection handle and sequence numbers (Phase 2) */
    void *connection;               /* Platform-specific connection handle */
    uint8_t send_seq;               /* Send sequence number */
    uint8_t recv_seq;               /* Receive sequence number */

    /* Queue references (Phase 2/3) */
    struct pt_queue *send_queue;
    struct pt_queue *recv_queue;

#endif
/* === END PHASE 1 ADDITIONS === */

/* Peer list operations */
int pt_peer_list_init(struct pt_context *ctx, uint16_t max_peers);
void pt_peer_list_free(struct pt_context *ctx);

/* Peer lookup */
struct pt_peer *pt_peer_find_by_id(struct pt_context *ctx, PeerTalk_PeerID id);

/* Find peer by address - HOT PATH, called on every incoming packet.
 * See implementation for DOD performance notes and optimization recommendations. */
struct pt_peer *pt_peer_find_by_addr(struct pt_context *ctx,
                                      uint32_t ip, uint16_t port);

/* Find peer by name - required by Phase 5.9 (AppleTalk Integration)
 * for cross-transport peer deduplication (same peer on TCP/IP and AppleTalk
 * appears as single entry). */
struct pt_peer *pt_peer_find_by_name(struct pt_context *ctx, const char *name);

struct pt_peer *pt_peer_find_unused(struct pt_context *ctx);

/* Peer lifecycle */
struct pt_peer *pt_peer_create(struct pt_context *ctx,
                               const char *name,
                               uint32_t ip, uint16_t port);
void pt_peer_destroy(struct pt_context *ctx, struct pt_peer *peer);

/* State transitions
 *
 * pt_peer_set_state validates transitions and logs state changes at DEBUG level.
 * Invalid transitions return -1 without modifying peer state.
 */
int pt_peer_set_state(struct pt_context *ctx, struct pt_peer *peer, pt_peer_state new_state);
const char *pt_peer_state_str(pt_peer_state state);

/* Timeout checking */
int pt_peer_is_timed_out(struct pt_peer *peer, pt_tick_t now,
                         pt_tick_t timeout_ticks);

/* Canary validation
 *
 * ISR-SAFETY WARNING: This function calls PT_Log and is NOT ISR-safe.
 * It MUST be called from the main event loop only, NOT from:
 * - MacTCP ASR callbacks
 * - Open Transport notifiers
 * - ADSP completion routines
 *
 * Returns: 0 if canaries are valid, -1 if corruption detected.
 * On corruption, also sets peer->canary_corrupt flag (volatile) for
 * checking from ISR context without logging.
 *
 * ctx: Logging context (can be NULL to disable logging, but flag still set)
 */
int pt_peer_check_canaries(struct pt_context *ctx, struct pt_peer *peer);

/* Fill PeerTalk_PeerInfo from internal peer */
void pt_peer_get_info(struct pt_peer *peer, PeerTalk_PeerInfo *info);

#endif /* PT_PEER_H */
```

#### Task 2.2.2: Create `src/core/peer.c`

```c
/*
 * PeerTalk Peer Management Implementation
 */

#include "peer.h"
#include "pt_internal.h"
#include "pt_log.h"
#include "pt_compat.h"

int pt_peer_list_init(struct pt_context *ctx, uint16_t max_peers) {
    size_t alloc_size;
    uint16_t i;

    alloc_size = sizeof(struct pt_peer) * max_peers;

    ctx->peers = (struct pt_peer *)pt_alloc_clear(alloc_size);

    if (!ctx->peers) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_MEMORY,
            "Failed to allocate peer list (%lu bytes)", (unsigned long)alloc_size);
        return -1;
    }

    ctx->max_peers = max_peers;
    ctx->peer_count = 0;

    /* Initialize all peers */
    for (i = 0; i < max_peers; i++) {
        ctx->peers[i].info.id = i + 1;  /* IDs start at 1, 0 is INVALID */
        ctx->peers[i].state = PT_PEER_UNUSED;
        ctx->peers[i].magic = 0;   /* Not valid until created */
    }

    PT_LOG_INFO(ctx, PT_LOG_CAT_MEMORY,
        "Peer list initialized: max=%u, size=%lu bytes",
        max_peers, (unsigned long)alloc_size);

    return 0;
}

void pt_peer_list_free(struct pt_context *ctx) {
    if (ctx->peers) {
        pt_free(ctx->peers);
        ctx->peers = NULL;
    }
    ctx->max_peers = 0;
    ctx->peer_count = 0;
}

struct pt_peer *pt_peer_find_by_id(struct pt_context *ctx, PeerTalk_PeerID id) {
    struct pt_peer *peer;

    if (id == 0 || id > ctx->max_peers)
        return NULL;

    peer = &ctx->peers[id - 1];
    if (peer->state == PT_PEER_UNUSED)
        return NULL;
    if (peer->magic != PT_PEER_MAGIC)
        return NULL;

    return peer;
}

/*
 * Find peer by IP address and port.
 *
 * DOD PERFORMANCE NOTE:
 * This function is called on EVERY incoming packet to identify which peer
 * it belongs to. Currently it accesses peer->info.address and peer->info.port
 * which are in cold storage (~1.4KB per peer). On 68030 with 256-byte cache,
 * scanning 16 peers touches 22KB+ causing severe cache thrashing.
 *
 * RECOMMENDED OPTIMIZATIONS (Phase 1 modifications):
 * 1. Move address/port to pt_peer_hot struct (adds 6 bytes to hot data)
 * 2. OR: Add peer_addr_hash[] lookup table to pt_context (similar to
 *    peer_id_to_index[]) using simple hash: (ip ^ port) & (PT_MAX_PEERS-1)
 *
 * For low peer counts (<8), the current linear scan is acceptable.
 * For higher counts or on 68000/68020 (no cache), optimization is critical.
 */
struct pt_peer *pt_peer_find_by_addr(struct pt_context *ctx,
                                      uint32_t ip, uint16_t port) {
    uint16_t i;

    for (i = 0; i < ctx->max_peers; i++) {
        struct pt_peer *peer = &ctx->peers[i];
        if (peer->hot.state != PT_PEER_UNUSED &&
            peer->info.address == ip && peer->info.port == port) {
            return peer;
        }
    }
    return NULL;
}

struct pt_peer *pt_peer_find_by_name(struct pt_context *ctx, const char *name) {
    uint16_t i;

    if (!name || !name[0])
        return NULL;

    /*
     * DOD Optimization: Use centralized peer_names[] table from Phase 1.
     * This avoids accessing cold storage (peer->info.name) which is ~1.4KB
     * per peer. On 68030 with 256-byte cache, this prevents severe cache
     * thrashing when scanning multiple peers.
     *
     * Phase 1 stores names in ctx->peer_names[name_idx] and the index
     * is stored in peer->hot.name_idx (hot storage, 32 bytes per peer).
     */
    for (i = 0; i < ctx->max_peers; i++) {
        struct pt_peer *peer = &ctx->peers[i];
        if (peer->hot.state != PT_PEER_UNUSED) {
            /* Access name via hot.name_idx -> centralized table */
            const char *peer_name = ctx->peer_names[peer->hot.name_idx];

            /* Compare strings manually (no pt_strcmp available) */
            {
                const char *a = peer_name;
                const char *b = name;
                while (*a && *b && *a == *b) {
                    a++;
                    b++;
                }
                if (*a == *b) {
                    return peer;
                }
            }
        }
    }
    return NULL;
}

struct pt_peer *pt_peer_find_unused(struct pt_context *ctx) {
    uint16_t i;
    for (i = 0; i < ctx->max_peers; i++) {
        if (ctx->peers[i].state == PT_PEER_UNUSED) {
            return &ctx->peers[i];
        }
    }
    return NULL;
}

struct pt_peer *pt_peer_create(struct pt_context *ctx,
                               const char *name,
                               uint32_t ip, uint16_t port) {
    struct pt_peer *peer;

    /* Check if peer already exists */
    peer = pt_peer_find_by_addr(ctx, ip, port);
    if (peer) {
        /* Update last_seen and name */
        peer->last_seen = ctx->plat->get_ticks();
        if (name && name[0]) {
            pt_strncpy(peer->info.name, name, sizeof(peer->info.name));
        }
        return peer;
    }

    /* Find unused slot */
    peer = pt_peer_find_unused(ctx);
    if (!peer) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
            "No unused peer slots available");
        return NULL;
    }

    /* Initialize peer - clear cold storage */
    pt_memset(&peer->cold, 0, sizeof(peer->cold));

    /* Clear buffer lengths */
    peer->cold.obuflen = 0;
    peer->cold.ibuflen = 0;

#ifdef PT_DEBUG
    /* Set canaries in debug mode */
    peer->cold.obuf_canary = PT_CANARY_OBUF;
    peer->cold.ibuf_canary = PT_CANARY_IBUF;
#endif

    peer->magic = PT_PEER_MAGIC;
    peer->info.address = ip;
    peer->info.port = port;
    /* peer->info.id is already set by pt_peer_list_init (array index + 1) */
    peer->info.connected = 0;
    peer->info.latency_ms = 0;
    peer->info.queue_pressure = 0;
    peer->state = PT_PEER_DISCOVERED;
    peer->last_seen = ctx->plat->get_ticks();
    peer->connect_start = 0;
    peer->send_seq = 0;
    peer->recv_seq = 0;
    peer->connection = NULL;

    if (name && name[0]) {
        pt_strncpy(peer->info.name, name, sizeof(peer->info.name));
    } else {
        peer->info.name[0] = '\0';
    }

    ctx->peer_count++;

    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Peer created: id=%u name=\"%s\" ip=0x%08lX port=%u",
        peer->info.id, peer->info.name, (unsigned long)ip, port);

    return peer;
}

void pt_peer_destroy(struct pt_context *ctx, struct pt_peer *peer) {
    if (!peer || peer->magic != PT_PEER_MAGIC)
        return;

    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Peer destroyed: id=%u name=\"%s\"",
        peer->info.id, peer->info.name);

    /* Clear sensitive data */
    peer->magic = 0;
    peer->state = PT_PEER_UNUSED;
    peer->info.name[0] = '\0';
    peer->info.address = 0;
    peer->info.port = 0;
    peer->info.connected = 0;
    peer->connection = NULL;

    ctx->peer_count--;
}

int pt_peer_set_state(struct pt_context *ctx, struct pt_peer *peer, pt_peer_state new_state) {
    pt_peer_state old_state;

    if (!peer || peer->magic != PT_PEER_MAGIC)
        return -1;

    old_state = peer->state;

    /* Validate state transition */
    switch (old_state) {
    case PT_PEER_UNUSED:
        /* Can only transition to DISCOVERED */
        if (new_state != PT_PEER_DISCOVERED) {
            PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
                "Invalid state transition for peer %u: %s -> %s",
                peer->info.id, pt_peer_state_str(old_state), pt_peer_state_str(new_state));
            return -1;
        }
        break;

    case PT_PEER_DISCOVERED:
        /* Can go to CONNECTING, CONNECTED (incoming), UNUSED, or stay DISCOVERED (refresh) */
        if (new_state != PT_PEER_CONNECTING &&
            new_state != PT_PEER_CONNECTED &&
            new_state != PT_PEER_DISCOVERED &&  /* Allow refresh */
            new_state != PT_PEER_UNUSED) {
            PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
                "Invalid state transition for peer %u: %s -> %s",
                peer->info.id, pt_peer_state_str(old_state), pt_peer_state_str(new_state));
            return -1;
        }
        break;

    case PT_PEER_CONNECTING:
        /* Can go to CONNECTED, FAILED, or UNUSED */
        if (new_state != PT_PEER_CONNECTED &&
            new_state != PT_PEER_FAILED &&
            new_state != PT_PEER_UNUSED) {
            PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
                "Invalid state transition for peer %u: %s -> %s",
                peer->info.id, pt_peer_state_str(old_state), pt_peer_state_str(new_state));
            return -1;
        }
        break;

    case PT_PEER_CONNECTED:
        /* Can go to DISCONNECTING, FAILED, or UNUSED */
        if (new_state != PT_PEER_DISCONNECTING &&
            new_state != PT_PEER_FAILED &&
            new_state != PT_PEER_UNUSED) {
            PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
                "Invalid state transition for peer %u: %s -> %s",
                peer->info.id, pt_peer_state_str(old_state), pt_peer_state_str(new_state));
            return -1;
        }
        break;

    case PT_PEER_DISCONNECTING:
        /* Can only go to UNUSED */
        if (new_state != PT_PEER_UNUSED) {
            PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
                "Invalid state transition for peer %u: %s -> %s",
                peer->info.id, pt_peer_state_str(old_state), pt_peer_state_str(new_state));
            return -1;
        }
        break;

    case PT_PEER_FAILED:
        /* Can go to UNUSED or DISCOVERED (recovery via re-announcement) */
        if (new_state != PT_PEER_UNUSED &&
            new_state != PT_PEER_DISCOVERED) {
            PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
                "Invalid state transition for peer %u: %s -> %s",
                peer->info.id, pt_peer_state_str(old_state), pt_peer_state_str(new_state));
            return -1;
        }
        break;
    }

    /* Log successful state transition
     *
     * Use PT_LOG_INFO for transitions TO CONNECTED (operational visibility)
     * Use PT_LOG_DEBUG for all other transitions (verbose diagnostics)
     */
    if (old_state != new_state) {
        if (new_state == PT_PEER_CONNECTED) {
            PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
                "Peer %u connected: %s -> CONNECTED",
                peer->info.id, pt_peer_state_str(old_state));
        } else {
            PT_LOG_DEBUG(ctx, PT_LOG_CAT_CONNECT,
                "Peer %u state: %s -> %s",
                peer->info.id, pt_peer_state_str(old_state), pt_peer_state_str(new_state));
        }
    }

    peer->state = new_state;
    return 0;
}

const char *pt_peer_state_str(pt_peer_state state) {
    switch (state) {
    case PT_PEER_UNUSED:        return "UNUSED";
    case PT_PEER_DISCOVERED:    return "DISCOVERED";
    case PT_PEER_CONNECTING:    return "CONNECTING";
    case PT_PEER_CONNECTED:     return "CONNECTED";
    case PT_PEER_DISCONNECTING: return "DISCONNECTING";
    case PT_PEER_FAILED:        return "FAILED";
    default:                    return "UNKNOWN";
    }
}

int pt_peer_is_timed_out(struct pt_peer *peer, pt_tick_t now,
                         pt_tick_t timeout_ticks) {
    if (peer->last_seen == 0)
        return 0;  /* Never seen, can't timeout */

    return (now - peer->last_seen) > timeout_ticks;
}

/*
 * Check buffer canaries for overflow detection.
 *
 * ISR-SAFETY: This function is NOT ISR-safe due to PT_Log calls.
 * Call from main event loop only. For ISR context, check
 * peer->canary_corrupt flag directly after this function sets it.
 *
 * Sets peer->canary_corrupt flag for ISR-safe detection:
 * - ISR code can check this flag without calling PT_Log
 * - Main loop should call this function periodically to update the flag
 *
 * Returns: 0 if canaries valid, -1 if corruption detected.
 */
int pt_peer_check_canaries(struct pt_context *ctx, struct pt_peer *peer) {
    int corrupted = 0;

    if (!peer) {
        return -1;
    }

#ifdef PT_DEBUG
    /* Check output buffer canary */
    if (peer->cold.obuf_canary != PT_CANARY_OBUF) {
        if (ctx) {
            PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
                      "Output buffer overflow detected (peer id=%u): "
                      "expected 0x%08X, got 0x%08X",
                      peer->hot.id, PT_CANARY_OBUF, peer->cold.obuf_canary);
        }
        corrupted = 1;
    }

    /* Check input buffer canary */
    if (peer->cold.ibuf_canary != PT_CANARY_IBUF) {
        if (ctx) {
            PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
                      "Input buffer overflow detected (peer id=%u): "
                      "expected 0x%08X, got 0x%08X",
                      peer->hot.id, PT_CANARY_IBUF, peer->cold.ibuf_canary);
        }
        corrupted = 1;
    }
#else
    /* In release builds, canaries are not present - always return valid */
    (void)ctx;  /* Suppress unused warning */
#endif

    return corrupted ? -1 : 0;
}

void pt_peer_get_info(struct pt_peer *peer, PeerTalk_PeerInfo *info) {
    if (!peer || !info) {
        return;
    }

    /* Copy peer info from cold storage */
    pt_memcpy(info, &peer->cold.info, sizeof(PeerTalk_PeerInfo));

    /* Update fields from hot data */
    info->id = peer->hot.id;
    info->latency_ms = peer->hot.latency_ms;
    info->name_idx = peer->hot.name_idx;

    /* Update connected field based on current state */
    info->connected = (peer->hot.state == PT_PEER_STATE_CONNECTED) ? 1 : 0;
}
```

#### Task 2.2.3: Create `tests/test_peer.c`

```c
/*
 * PeerTalk Peer Management Tests
 */

#include <stdio.h>
#include <assert.h>
#include "peer.h"
#include "pt_compat.h"

/* Mock context for testing */
static struct pt_context test_ctx;
static pt_platform_ops test_ops;

static pt_tick_t mock_ticks = 1000;
static pt_tick_t mock_get_ticks(void) { return mock_ticks; }
static unsigned long mock_get_free_mem(void) { return 1000000; }
static unsigned long mock_get_max_block(void) { return 500000; }

void setup_test_ctx(void) {
    pt_memset(&test_ctx, 0, sizeof(test_ctx));
    pt_memset(&test_ops, 0, sizeof(test_ops));

    test_ops.get_ticks = mock_get_ticks;
    test_ops.get_free_mem = mock_get_free_mem;
    test_ops.get_max_block = mock_get_max_block;

    test_ctx.magic = PT_CONTEXT_MAGIC;
    test_ctx.plat = &test_ops;
}

void test_peer_list_init(void) {
    setup_test_ctx();

    assert(pt_peer_list_init(&test_ctx, 8) == 0);
    assert(test_ctx.max_peers == 8);
    assert(test_ctx.peer_count == 0);

    pt_peer_list_free(&test_ctx);
    printf("test_peer_list_init: PASSED\n");
}

void test_peer_create_destroy(void) {
    struct pt_peer *peer;

    setup_test_ctx();
    pt_peer_list_init(&test_ctx, 8);

    peer = pt_peer_create(&test_ctx, "TestPeer", 0xC0A80001, 5001);
    assert(peer != NULL);
    assert(peer->magic == PT_PEER_MAGIC);
    assert(peer->state == PT_PEER_DISCOVERED);
    assert(test_ctx.peer_count == 1);

    pt_peer_destroy(&test_ctx, peer);
    assert(peer->state == PT_PEER_UNUSED);
    assert(test_ctx.peer_count == 0);

    pt_peer_list_free(&test_ctx);
    printf("test_peer_create_destroy: PASSED\n");
}

void test_peer_find(void) {
    struct pt_peer *peer, *found;

    setup_test_ctx();
    pt_peer_list_init(&test_ctx, 8);

    peer = pt_peer_create(&test_ctx, "FindMe", 0x0A000001, 5002);
    assert(peer != NULL);

    /* Find by ID */
    found = pt_peer_find_by_id(&test_ctx, peer->info.id);
    assert(found == peer);

    /* Find by address */
    found = pt_peer_find_by_addr(&test_ctx, 0x0A000001, 5002);
    assert(found == peer);

    /* Not found cases */
    assert(pt_peer_find_by_id(&test_ctx, 99) == NULL);
    assert(pt_peer_find_by_addr(&test_ctx, 0xFFFFFFFF, 0) == NULL);

    /* Find by name */
    found = pt_peer_find_by_name(&test_ctx, "FindMe");
    assert(found == peer);

    /* Name not found */
    assert(pt_peer_find_by_name(&test_ctx, "NotHere") == NULL);
    assert(pt_peer_find_by_name(&test_ctx, NULL) == NULL);
    assert(pt_peer_find_by_name(&test_ctx, "") == NULL);

    pt_peer_list_free(&test_ctx);
    printf("test_peer_find: PASSED\n");
}

void test_peer_state_transitions(void) {
    struct pt_peer *peer;

    setup_test_ctx();
    pt_peer_list_init(&test_ctx, 8);
    peer = pt_peer_create(&test_ctx, "StateTest", 0x7F000001, 5003);

    /* Valid transitions */
    assert(peer->state == PT_PEER_DISCOVERED);
    assert(pt_peer_set_state(&test_ctx, peer, PT_PEER_CONNECTING) == 0);
    assert(pt_peer_set_state(&test_ctx, peer, PT_PEER_CONNECTED) == 0);
    assert(pt_peer_set_state(&test_ctx, peer, PT_PEER_DISCONNECTING) == 0);
    assert(pt_peer_set_state(&test_ctx, peer, PT_PEER_UNUSED) == 0);

    /* Invalid transition: UNUSED -> CONNECTED */
    peer->state = PT_PEER_UNUSED;
    peer->magic = PT_PEER_MAGIC;  /* Re-validate for test */
    assert(pt_peer_set_state(&test_ctx, peer, PT_PEER_CONNECTED) != 0);

    /* Test DISCOVERED -> DISCOVERED refresh */
    peer->state = PT_PEER_DISCOVERED;
    assert(pt_peer_set_state(&test_ctx, peer, PT_PEER_DISCOVERED) == 0);

    /* Test FAILED -> DISCOVERED recovery */
    peer->state = PT_PEER_FAILED;
    assert(pt_peer_set_state(&test_ctx, peer, PT_PEER_DISCOVERED) == 0);

    pt_peer_list_free(&test_ctx);
    printf("test_peer_state_transitions: PASSED\n");
}

void test_peer_timeout(void) {
    struct pt_peer *peer;

    setup_test_ctx();
    pt_peer_list_init(&test_ctx, 8);
    peer = pt_peer_create(&test_ctx, "TimeoutTest", 0, 0);

    /* Not timed out yet */
    mock_ticks = 1100;
    assert(pt_peer_is_timed_out(peer, mock_ticks, 200) == 0);

    /* Now timed out */
    mock_ticks = 1300;
    assert(pt_peer_is_timed_out(peer, mock_ticks, 200) == 1);

    pt_peer_list_free(&test_ctx);
    printf("test_peer_timeout: PASSED\n");
}

void test_peer_canaries(void) {
    struct pt_peer *peer;

    setup_test_ctx();
    pt_peer_list_init(&test_ctx, 8);
    peer = pt_peer_create(&test_ctx, "CanaryTest", 0, 0);

    /* Canaries should be valid */
    assert(peer->obuf_canary == PT_CANARY_OBUF);
    assert(peer->ibuf_canary == PT_CANARY_IBUF);

    /* Should return 0 (valid) */
    assert(pt_peer_check_canaries(&test_ctx, peer) == 0);
    assert(peer->canary_corrupt == 0);  /* Flag should be clear */

    /* Corrupt a canary and verify detection */
    peer->obuf_canary = 0xBADCAFE;
    assert(pt_peer_check_canaries(&test_ctx, peer) == -1);
    assert(peer->canary_corrupt != 0);  /* Flag should be set */

    /* Restore and verify */
    peer->obuf_canary = PT_CANARY_OBUF;
    assert(pt_peer_check_canaries(&test_ctx, peer) == 0);
    assert(peer->canary_corrupt == 0);  /* Flag should be cleared */

    pt_peer_list_free(&test_ctx);
    printf("test_peer_canaries: PASSED\n");
}

void test_peer_get_info(void) {
    struct pt_peer *peer;
    PeerTalk_PeerInfo info;

    setup_test_ctx();
    pt_peer_list_init(&test_ctx, 8);
    peer = pt_peer_create(&test_ctx, "InfoTest", 0xC0A80102, 7354);
    peer->state = PT_PEER_CONNECTED;

    pt_peer_get_info(peer, &info);

    assert(info.id == peer->info.id);
    assert(pt_memcmp(info.name, "InfoTest", 8) == 0);
    assert(info.address == 0xC0A80102);
    assert(info.port == 7354);
    assert(info.connected == 1);

    pt_peer_list_free(&test_ctx);
    printf("test_peer_get_info: PASSED\n");
}

int main(void) {
    printf("PeerTalk Peer Management Tests\n");
    printf("==============================\n\n");

    test_peer_list_init();
    test_peer_create_destroy();
    test_peer_find();
    test_peer_state_transitions();
    test_peer_timeout();
    test_peer_canaries();
    test_peer_get_info();

    printf("\n==============================\n");
    printf("All peer tests PASSED!\n");
    return 0;
}
```

### Acceptance Criteria
1. Peer list init/free works correctly
2. Peer create returns valid peer with correct initial state
3. Peer destroy clears sensitive data
4. Find by ID works
5. Find by address works
6. State transition validation prevents invalid transitions
7. Timeout detection works
8. Buffer canaries are set correctly

---

## Session 2.3: Message Queues

### Objective
Implement pre-allocated, lock-free message queues that can be safely used from interrupt context (ASR/notifier) with backpressure signaling.

### Tasks

#### Task 2.3.1: Create `src/core/queue.h`

```c
/*
 * PeerTalk Message Queue
 *
 * Pre-allocated ring buffer queue for interrupt-safe message passing.
 * Designed for the single-CPU cooperative model of Classic Mac.
 *
 * ISR Pattern:
 * - ISR (ASR/notifier) can call pt_queue_push_isr() to add messages
 * - Main loop calls pt_queue_pop() to consume messages
 * - No allocation, no blocking in ISR path
 */

#ifndef PT_QUEUE_H
#define PT_QUEUE_H

#include "pt_types.h"

/* Queue magic for validation */
#define PT_QUEUE_MAGIC 0x50545155  /* "PTQU" */

/*
 * Queue slot for control messages and event notifications.
 *
 * IMPORTANT: This queue is for CONTROL messages (PING/PONG/ACK/DISCONNECT)
 * and discovery packets, NOT for full application data payloads.
 *
 * Large DATA messages (up to PT_MAX_MESSAGE_SIZE = 8192 bytes) are handled
 * differently: the ISR sets a flag, and the main loop reads directly from
 * network buffers or uses the per-peer ibuf/obuf for framing.
 *
 * 256 bytes is sufficient for:
 * - Discovery packets (max ~48 bytes)
 * - Control messages (header + minimal payload)
 * - Event notifications
 */
#define PT_QUEUE_SLOT_SIZE 256

/*
 * Queue slot layout optimized for cache efficiency on Classic Mac.
 *
 * Metadata (length, priority, flags) placed BEFORE data array so that
 * pt_queue_pop/peek can check flags in the first cache line access
 * before deciding whether to read the 256-byte data payload.
 *
 * On 68030 with 256-byte cache, this avoids pulling the entire slot
 * into cache just to check if it's ready.
 */
typedef struct {
    uint16_t length;                /* 2 bytes - checked first on pop */
    uint8_t  priority;              /* 1 byte */
    volatile uint8_t  flags;        /* 1 byte - volatile for OT atomic access */
    #define PT_SLOT_USED        0x01
    #define PT_SLOT_COALESCABLE 0x02
    #define PT_SLOT_READY       0x04  /* Set AFTER data is fully written (OT reentrancy) */
    uint8_t  data[PT_QUEUE_SLOT_SIZE];  /* 256 bytes - accessed only if flags valid */
} pt_queue_slot;

/*
 * Lock-free queue using ring buffer
 *
 * IMPORTANT: capacity MUST be a power of two for performance.
 * This allows using bitwise AND instead of modulo for index wrap,
 * which is critical on 68k processors where division takes 100+ cycles.
 *
 * ISR Safety (MacTCP ASR):
 *   MacTCP guarantees ASRs are NOT reentrant for the same stream.
 *   Standard single-writer queue operations are safe.
 *
 * ISR Safety (Open Transport Notifier):
 *   OT notifiers CAN be reentrant. If using OT with a shared queue,
 *   use pt_queue_push_isr_ot() which uses OTAtomicAdd16 for safety.
 */
typedef struct pt_queue {
    uint32_t magic;                 /* PT_QUEUE_MAGIC for validation */
    pt_queue_slot *slots;           /* Pre-allocated array */
    uint16_t capacity;              /* Number of slots (must be power of 2) */
    uint16_t capacity_mask;         /* capacity - 1, for fast wrap-around */
    volatile uint16_t write_idx;    /* Next slot to write */
    volatile uint16_t read_idx;     /* Next slot to read */
    volatile uint16_t count;        /* Current queue depth */
    volatile uint8_t has_data;      /* Flag for ISR signaling */
    uint8_t reserved;               /* Explicit padding for 20-byte struct */
} pt_queue;

/*============================================================================
 * Queue Management
 *
 * Return value conventions:
 *   - Functions returning int: 0 = success, -1 = error
 *   - pt_queue_pressure(): returns 0-100 (percentage)
 *   - pt_queue_is_full/is_empty(): returns 1 = true, 0 = false
 *============================================================================*/

/* Initialize queue with given capacity (MUST be power of 2).
 * ctx: Optional logging context (can be NULL to disable logging)
 * Returns: 0 on success, -1 on error (invalid params, alloc failure) */
int pt_queue_init(struct pt_context *ctx, pt_queue *q, uint16_t capacity);

/* Free queue resources. Safe to call on NULL or uninitialized queue. */
void pt_queue_free(pt_queue *q);

/* Reset queue to empty state. Does not free memory. */
void pt_queue_reset(pt_queue *q);

/*============================================================================
 * Push Operations
 *============================================================================*/

/* Push from main loop (uses pt_memcpy, logs backpressure warnings).
 * ctx: Optional logging context (can be NULL to disable logging)
 * Returns: 0 on success, -1 on error (queue full, invalid params) */
int pt_queue_push(struct pt_context *ctx, pt_queue *q, const void *data, uint16_t len,
                  uint8_t priority, uint8_t flags);

/* Push from ISR - MINIMAL work, no allocation, no logging.
 * Called from MacTCP ASR (single-threaded, safe).
 * Returns: 0 on success, -1 on error (queue full) */
int pt_queue_push_isr(pt_queue *q, const void *data, uint16_t len);

/*
 * Push from OT notifier - uses atomic operations for reentrancy safety.
 * Called from Open Transport notifier (may be reentrant).
 * Returns: 0 on success, -1 on error (queue full)
 *
 * Uses OTAtomicAdd16() from OpenTransport.h (verified in Retro68 headers).
 * See NetworkingOpenTransport.txt line 35707 for signature:
 *   SInt16 OTAtomicAdd16(SInt32 toAdd, SInt16* where)
 */
#ifdef PT_OPEN_TRANSPORT
int pt_queue_push_isr_ot(pt_queue *q, const void *data, uint16_t len);
#endif

/*============================================================================
 * Pop Operations
 *============================================================================*/

/* Pop and copy data to buffer (called from main loop only).
 * Returns: 0 on success (data copied, *len set), -1 on error (queue empty) */
int pt_queue_pop(pt_queue *q, void *data, uint16_t *len);

/* Peek at front slot without removing (zero-copy access).
 * Returns: 0 on success (*data points to slot, *len set), -1 on error (queue empty) */
int pt_queue_peek(pt_queue *q, void **data, uint16_t *len);

/* Mark peeked slot as consumed. Must call after processing peeked data. */
void pt_queue_consume(pt_queue *q);

/*============================================================================
 * Queue Status
 *============================================================================*/

/* Get current number of items in queue */
uint16_t pt_queue_count(pt_queue *q);

/* Get number of free slots */
uint16_t pt_queue_free_slots(pt_queue *q);

/* Get queue pressure as percentage (0-100).
 * Used for backpressure signaling and flow control. */
uint8_t pt_queue_pressure(pt_queue *q);

/* Check if queue is full. Returns: 1 = full, 0 = has space */
int pt_queue_is_full(pt_queue *q);

/* Check if queue is empty. Returns: 1 = empty, 0 = has data */
int pt_queue_is_empty(pt_queue *q);

/*============================================================================
 * Coalescing
 *============================================================================*/

/* Replace last coalescable message with new one (for state updates).
 * Returns: 0 on success, -1 on error (no coalescable message found) */
int pt_queue_coalesce(pt_queue *q, const void *data, uint16_t len);

#endif /* PT_QUEUE_H */
```

#### Task 2.3.2: Create `src/core/queue.c`

```c
/*
 * PeerTalk Message Queue Implementation
 */

#include "queue.h"
#include "pt_compat.h"
#include "pt_log.h"

/* Helper: check if value is power of two */
static int pt_is_power_of_two(uint16_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

int pt_queue_init(struct pt_context *ctx, pt_queue *q, uint16_t capacity) {
    size_t alloc_size;

    if (!q || capacity == 0)
        return -1;

    /* Capacity MUST be power of two for efficient wrap-around on 68k */
    if (!pt_is_power_of_two(capacity)) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_MEMORY,
            "Queue init failed: capacity %u is not power of 2", capacity);
        return -1;
    }

    alloc_size = sizeof(pt_queue_slot) * capacity;

    q->slots = (pt_queue_slot *)pt_alloc_clear(alloc_size);

    if (!q->slots) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_MEMORY,
            "Queue init failed: could not allocate %lu bytes for %u slots",
            (unsigned long)alloc_size, capacity);
        return -1;
    }

    q->magic = PT_QUEUE_MAGIC;
    q->capacity = capacity;
    q->capacity_mask = capacity - 1;  /* For fast modulo via bitwise AND */
    q->write_idx = 0;
    q->read_idx = 0;
    q->count = 0;
    q->has_data = 0;

    PT_LOG_INFO(ctx, PT_LOG_CAT_MEMORY,
        "Queue initialized: capacity=%u, slot_size=%lu, total=%lu bytes",
        capacity, (unsigned long)sizeof(pt_queue_slot), (unsigned long)alloc_size);

    return 0;
}

void pt_queue_free(pt_queue *q) {
    if (q && q->slots) {
        pt_free(q->slots);
        q->slots = NULL;
        q->magic = 0;
    }
}

void pt_queue_reset(pt_queue *q) {
    uint16_t i;
    uint16_t old_count = q->count;

    q->write_idx = 0;
    q->read_idx = 0;
    q->count = 0;
    q->has_data = 0;

    /* Only touch slots if queue had data - avoids touching 4KB+ of memory
     * when resetting an already-empty queue (important on 68k with no cache) */
    if (old_count > 0) {
        for (i = 0; i < q->capacity; i++) {
            q->slots[i].flags = 0;
            q->slots[i].length = 0;
        }
    }
}

/*
 * Push message to queue from main event loop.
 *
 * ctx: Optional logging context (can be NULL to disable logging)
 *
 * Returns: 0 on success, -1 on error (queue full or invalid params)
 *
 * Backpressure logging (cascade thresholds):
 * - Logs at WARN level when queue crosses 80%, 90%, or 95% capacity
 * - Logs at WARN level when push fails due to full queue
 * - Only logs once per threshold crossing (not on every push)
 *
 * Note: pt_queue_push_isr() does NOT log (ISR-safe, no PT_Log calls).
 */
int pt_queue_push(struct pt_context *ctx, pt_queue *q, const void *data, uint16_t len,
                  uint8_t priority, uint8_t flags) {
    pt_queue_slot *slot;
    uint16_t next_write;
    uint8_t pressure, prev_pressure;

    if (!q || !data || len == 0 || len > PT_QUEUE_SLOT_SIZE)
        return -1;

    /* Check if full */
    if (q->count >= q->capacity) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_PERF,
            "Queue push failed: queue full (%u/%u slots)",
            q->count, q->capacity);
        return -1;  /* QUEUE_FULL */
    }

    /* Log backpressure warning at cascade thresholds (80%, 90%, 95%)
     * Only log when CROSSING the threshold, not on every push above it.
     */
    pressure = (q->count * 100) / q->capacity;
    prev_pressure = q->count > 0 ? ((q->count - 1) * 100 / q->capacity) : 0;

    if (pressure >= 95 && prev_pressure < 95) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_PERF,
            "Queue CRITICAL: %u%% full (%u/%u slots)",
            pressure, q->count, q->capacity);
    } else if (pressure >= 90 && prev_pressure < 90) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_PERF,
            "Queue HIGH pressure: %u%% full (%u/%u slots)",
            pressure, q->count, q->capacity);
    } else if (pressure >= 80 && prev_pressure < 80) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_PERF,
            "Queue backpressure: %u%% full (%u/%u slots)",
            pressure, q->count, q->capacity);
    }

    slot = &q->slots[q->write_idx];

    /* Copy data */
    pt_memcpy(slot->data, data, len);
    slot->length = len;
    slot->priority = priority;
    slot->flags = PT_SLOT_USED | (flags & PT_SLOT_COALESCABLE);

    /* Advance write index (wrap around) */
    next_write = (q->write_idx + 1) & q->capacity_mask;

    /* Memory barrier would go here on SMP systems */
    /* On Classic Mac (single CPU), not needed */

    q->write_idx = next_write;
    q->count++;
    q->has_data = 1;

    return 0;
}

int pt_queue_push_isr(pt_queue *q, const void *data, uint16_t len) {
    /*
     * ISR-safe push: MINIMAL work
     * - No allocation
     * - No logging
     * - Uses pt_memcpy_isr (manual byte copy, NOT BlockMoveData)
     * Called from MacTCP ASR or Open Transport notifier
     *
     * CRITICAL: Must use pt_memcpy_isr(), NOT pt_memcpy()!
     * pt_memcpy() calls BlockMoveData which is FORBIDDEN at interrupt time.
     * See CLAUDE.md ASR rules table.
     */
    pt_queue_slot *slot;

    if (q->count >= q->capacity) {
        /* Queue full - drop message */
        return -1;
    }

    slot = &q->slots[q->write_idx];

    if (len > PT_QUEUE_SLOT_SIZE)
        len = PT_QUEUE_SLOT_SIZE;

    pt_memcpy_isr(slot->data, data, len);
    slot->length = len;
    slot->priority = 0;  /* Default priority in ISR */
    slot->flags = PT_SLOT_USED;

    q->write_idx = (q->write_idx + 1) & q->capacity_mask;
    q->count++;
    q->has_data = 1;

    return 0;
}

#ifdef PT_OPEN_TRANSPORT
#include <OpenTransport.h>

int pt_queue_push_isr_ot(pt_queue *q, const void *data, uint16_t len) {
    /*
     * OT-safe push using atomic operations.
     * Open Transport notifiers CAN be reentrant, so we need atomics.
     * (See NetworkingOpenTransport.txt line 5822)
     *
     * OTAtomicAdd16 is available in Retro68's OpenTransport.h.
     * Signature: SInt16 OTAtomicAdd16(SInt32 toAdd, SInt16* where)
     * Returns the NEW value after addition.
     *
     * Still uses pt_memcpy_isr (not OTMemcpy) for ISR safety.
     *
     * RACE CONDITION FIX:
     * We use PT_SLOT_READY flag to indicate when data is fully written.
     * The pop function checks this flag before reading the slot.
     * This prevents reading partially-written data when two notifiers
     * interleave and the main loop checks count between writes.
     */
    pt_queue_slot *slot;
    uint16_t my_idx;

    if (q->count >= q->capacity) {
        /* Queue full - drop message */
        return -1;
    }

    /*
     * Atomically claim a slot index.
     * Cast to SInt16* is safe: both uint16_t and SInt16 are 16-bit,
     * and we only care about the bit pattern for index wrap-around.
     *
     * OTAtomicAdd16 returns the NEW value after adding 1.
     * We subtract 1 to get the PREVIOUS value, which is our claimed slot.
     * Example: if write_idx was 5, OTAtomicAdd16 returns 6, we use slot 5.
     *
     * SIGNEDNESS NOTE: This cast is safe for queue capacities <= 32767.
     * Larger queues would require a separate atomic or mutex pattern.
     * In practice, message queues rarely exceed 256 slots.
     */
    my_idx = (uint16_t)(OTAtomicAdd16(1, (SInt16 *)&q->write_idx) - 1);
    my_idx = my_idx & q->capacity_mask;

    slot = &q->slots[my_idx];

    if (len > PT_QUEUE_SLOT_SIZE)
        len = PT_QUEUE_SLOT_SIZE;

    /* Clear READY flag first - slot is being written */
    slot->flags = PT_SLOT_USED;  /* USED but not READY */

    /* Copy data */
    pt_memcpy_isr(slot->data, data, len);
    slot->length = len;
    slot->priority = 0;

    /*
     * Set READY flag AFTER data is fully written.
     * Use OTAtomicSetBit for atomic flag update.
     * Bit 2 = PT_SLOT_READY (0x04)
     */
    OTAtomicSetBit((UInt8 *)&slot->flags, 2);  /* Set bit 2 = READY */

    OTAtomicAdd16(1, (SInt16 *)&q->count);
    q->has_data = 1;

    return 0;
}
#endif /* PT_OPEN_TRANSPORT */

int pt_queue_pop(pt_queue *q, void *data, uint16_t *len) {
    pt_queue_slot *slot;

    if (!q || q->count == 0)
        return -1;

    slot = &q->slots[q->read_idx];

    if (!(slot->flags & PT_SLOT_USED))
        return -1;  /* Slot not valid */

    /*
     * For OT builds: check READY flag to ensure data is fully written.
     * This handles the race condition where OT notifiers interleave and
     * count is incremented before data copy completes.
     *
     * MacTCP builds don't need this check (ASRs aren't reentrant).
     */
#ifdef PT_PLATFORM_OT
    if (!(slot->flags & PT_SLOT_READY))
        return -1;  /* Data not fully written yet - try again later */
#endif

    if (data && len) {
        pt_memcpy(data, slot->data, slot->length);
        *len = slot->length;
    }

    /* Clear slot */
    slot->flags = 0;
    slot->length = 0;

    /* Advance read index */
    q->read_idx = (q->read_idx + 1) & q->capacity_mask;
    q->count--;

    if (q->count == 0)
        q->has_data = 0;

    return 0;
}

int pt_queue_peek(pt_queue *q, void **data, uint16_t *len) {
    pt_queue_slot *slot;

    if (!q || q->count == 0)
        return -1;

    slot = &q->slots[q->read_idx];

    if (!(slot->flags & PT_SLOT_USED))
        return -1;

    /* OT builds: ensure data is fully written (see pop for explanation) */
#ifdef PT_PLATFORM_OT
    if (!(slot->flags & PT_SLOT_READY))
        return -1;  /* Data not fully written yet */
#endif

    if (data)
        *data = slot->data;
    if (len)
        *len = slot->length;

    return 0;
}

void pt_queue_consume(pt_queue *q) {
    if (q && q->count > 0) {
        q->slots[q->read_idx].flags = 0;
        q->read_idx = (q->read_idx + 1) & q->capacity_mask;
        q->count--;

        if (q->count == 0)
            q->has_data = 0;
    }
}

uint16_t pt_queue_count(pt_queue *q) {
    return q ? q->count : 0;
}

uint16_t pt_queue_free_slots(pt_queue *q) {
    return q ? (q->capacity - q->count) : 0;
}

uint8_t pt_queue_pressure(pt_queue *q) {
    if (!q || q->capacity == 0)
        return 100;
    /* Cast to uint32_t to prevent overflow when count > 655 */
    return (uint8_t)(((uint32_t)q->count * 100) / q->capacity);
}

int pt_queue_is_full(pt_queue *q) {
    return q && q->count >= q->capacity;
}

int pt_queue_is_empty(pt_queue *q) {
    return !q || q->count == 0;
}

int pt_queue_coalesce(pt_queue *q, const void *data, uint16_t len) {
    pt_queue_slot *slot;
    uint16_t i;
    uint16_t search_limit;

    if (!q || !data || len == 0 || len > PT_QUEUE_SLOT_SIZE)
        return -1;

    /*
     * Find the last coalescable message and replace it.
     * Search from newest to oldest, but limit to last 4 slots.
     *
     * Rationale: Position updates typically only need to coalesce with
     * very recent messages. Limiting the search avoids reverse-scanning
     * the entire queue, which causes cache misses on 68030 (256-byte cache).
     */
    search_limit = (q->count < 4) ? q->count : 4;
    for (i = 0; i < search_limit; i++) {
        uint16_t idx = (q->write_idx - 1 - i + q->capacity) & q->capacity_mask;
        slot = &q->slots[idx];

        if ((slot->flags & PT_SLOT_COALESCABLE) &&
            (slot->flags & PT_SLOT_USED)) {
            /* Replace this message */
            pt_memcpy(slot->data, data, len);
            slot->length = len;
            return 0;  /* Coalesced */
        }
    }

    /* No coalescable message found, push new */
    return pt_queue_push(q, data, len, 0, PT_SLOT_COALESCABLE);
}
```

#### Task 2.3.3: Create `tests/test_queue.c`

```c
/*
 * PeerTalk Queue Tests
 */

#include <stdio.h>
#include <assert.h>
#include "queue.h"
#include "pt_compat.h"

void test_queue_init_free(void) {
    pt_queue q;

    assert(pt_queue_init(NULL, &q, 16) == 0);
    assert(q.magic == PT_QUEUE_MAGIC);
    assert(q.capacity == 16);
    assert(q.capacity_mask == 15);
    assert(q.count == 0);
    assert(pt_queue_is_empty(&q));

    pt_queue_free(&q);
    assert(q.magic == 0);  /* Magic cleared on free */
    printf("test_queue_init_free: PASSED\n");
}

void test_queue_push_pop(void) {
    pt_queue q;
    char msg[] = "Hello, World!";
    char buf[256];
    uint16_t len;

    pt_queue_init(NULL, &q, 8);

    /* Push */
    assert(pt_queue_push(NULL, &q, msg, pt_strlen(msg) + 1, 0, 0) == 0);
    assert(q.count == 1);

    /* Pop */
    len = sizeof(buf);
    assert(pt_queue_pop(&q, buf, &len) == 0);
    assert(len == pt_strlen(msg) + 1);
    assert(pt_memcmp(buf, msg, len) == 0);
    assert(q.count == 0);

    pt_queue_free(&q);
    printf("test_queue_push_pop: PASSED\n");
}

void test_queue_fifo_order(void) {
    pt_queue q;
    char buf[256];
    uint16_t len;

    pt_queue_init(NULL, &q, 8);

    pt_queue_push(NULL, &q, "first", 6, 0, 0);
    pt_queue_push(NULL, &q, "second", 7, 0, 0);
    pt_queue_push(NULL, &q, "third", 6, 0, 0);

    assert(q.count == 3);

    pt_queue_pop(&q, buf, &len);
    assert(pt_memcmp(buf, "first", 5) == 0);

    pt_queue_pop(&q, buf, &len);
    assert(pt_memcmp(buf, "second", 6) == 0);

    pt_queue_pop(&q, buf, &len);
    assert(pt_memcmp(buf, "third", 5) == 0);

    assert(pt_queue_is_empty(&q));

    pt_queue_free(&q);
    printf("test_queue_fifo_order: PASSED\n");
}

void test_queue_full(void) {
    pt_queue q;
    int i;

    pt_queue_init(NULL, &q, 4);

    /* Fill queue */
    for (i = 0; i < 4; i++) {
        assert(pt_queue_push(NULL, &q, "x", 1, 0, 0) == 0);
    }

    assert(pt_queue_is_full(&q));

    /* Push should fail */
    assert(pt_queue_push(NULL, &q, "y", 1, 0, 0) != 0);

    pt_queue_free(&q);
    printf("test_queue_full: PASSED\n");
}

void test_queue_pressure(void) {
    pt_queue q;
    int i;

    /* Use power-of-two capacity (128) */
    pt_queue_init(NULL, &q, 128);

    /* Fill to 96/128 = 75% */
    for (i = 0; i < 96; i++) {
        pt_queue_push(NULL, &q, "x", 1, 0, 0);
    }

    assert(pt_queue_pressure(&q) == 75);

    pt_queue_free(&q);
    printf("test_queue_pressure: PASSED\n");
}

void test_queue_wrap_around(void) {
    pt_queue q;
    char buf[256];
    uint16_t len;
    int i;

    pt_queue_init(NULL, &q, 4);

    /* Fill and drain multiple times to test wrap */
    for (i = 0; i < 10; i++) {
        pt_queue_push(NULL, &q, "wrap", 5, 0, 0);
        pt_queue_pop(&q, buf, &len);
        assert(pt_memcmp(buf, "wrap", 4) == 0);
    }

    assert(pt_queue_is_empty(&q));

    pt_queue_free(&q);
    printf("test_queue_wrap_around: PASSED\n");
}

void test_queue_coalesce(void) {
    pt_queue q;
    char buf[256];
    uint16_t len;

    pt_queue_init(NULL, &q, 8);

    /* Push coalescable message */
    pt_queue_push(NULL, &q, "old", 4, 0, PT_SLOT_COALESCABLE);
    assert(q.count == 1);

    /* Coalesce should replace */
    pt_queue_coalesce(&q, "new", 4);
    assert(q.count == 1);  /* Still only 1 message */

    pt_queue_pop(&q, buf, &len);
    assert(pt_memcmp(buf, "new", 3) == 0);

    pt_queue_free(&q);
    printf("test_queue_coalesce: PASSED\n");
}

void test_queue_peek_consume(void) {
    pt_queue q;
    void *data;
    uint16_t len;

    pt_queue_init(NULL, &q, 4);

    pt_queue_push(NULL, &q, "peek", 5, 0, 0);

    /* Peek should not remove */
    assert(pt_queue_peek(&q, &data, &len) == 0);
    assert(len == 5);
    assert(pt_memcmp(data, "peek", 4) == 0);
    assert(q.count == 1);

    /* Consume should remove */
    pt_queue_consume(&q);
    assert(q.count == 0);

    pt_queue_free(&q);
    printf("test_queue_peek_consume: PASSED\n");
}

void test_queue_isr_push(void) {
    pt_queue q;
    char buf[256];
    uint16_t len;

    pt_queue_init(NULL, &q, 8);

    /* Simulate ISR pushing data */
    assert(pt_queue_push_isr(&q, "isr_data", 9) == 0);
    assert(q.count == 1);
    assert(q.has_data == 1);

    /* Pop from main loop */
    assert(pt_queue_pop(&q, buf, &len) == 0);
    assert(pt_memcmp(buf, "isr_data", 8) == 0);

    pt_queue_free(&q);
    printf("test_queue_isr_push: PASSED\n");
}

void test_queue_power_of_two(void) {
    pt_queue q;

    /* Valid: power of two capacities */
    assert(pt_queue_init(NULL, &q, 1) == 0);
    assert(q.capacity_mask == 0);
    pt_queue_free(&q);

    assert(pt_queue_init(NULL, &q, 2) == 0);
    assert(q.capacity_mask == 1);
    pt_queue_free(&q);

    assert(pt_queue_init(NULL, &q, 16) == 0);
    assert(q.capacity_mask == 15);
    pt_queue_free(&q);

    assert(pt_queue_init(NULL, &q, 256) == 0);
    assert(q.capacity_mask == 255);
    pt_queue_free(&q);

    /* Invalid: non-power-of-two should fail */
    assert(pt_queue_init(NULL, &q, 3) != 0);
    assert(pt_queue_init(NULL, &q, 5) != 0);
    assert(pt_queue_init(NULL, &q, 100) != 0);
    assert(pt_queue_init(NULL, &q, 0) != 0);

    printf("test_queue_power_of_two: PASSED\n");
}

void test_queue_magic(void) {
    pt_queue q;

    pt_queue_init(NULL, &q, 8);
    assert(q.magic == PT_QUEUE_MAGIC);

    pt_queue_free(&q);
    assert(q.magic == 0);  /* Magic cleared on free */

    printf("test_queue_magic: PASSED\n");
}

void test_queue_pressure_overflow(void) {
    /*
     * Test that pressure calculation doesn't overflow.
     * With uint16_t count * 100, overflow occurs when count > 655.
     * We use a capacity of 1024 and fill to 800 to verify.
     */
    pt_queue q;
    int i;
    uint8_t pressure;

    pt_queue_init(NULL, &q, 1024);

    /* Push 800 messages (would overflow if count * 100 used uint16_t) */
    for (i = 0; i < 800; i++) {
        assert(pt_queue_push(NULL, &q, "x", 1, 0, 0) == 0);
    }

    pressure = pt_queue_pressure(&q);
    /* 800/1024 * 100 = 78.125, should be 78 */
    assert(pressure == 78);

    pt_queue_free(&q);
    printf("test_queue_pressure_overflow: PASSED\n");
}

int main(void) {
    printf("PeerTalk Queue Tests\n");
    printf("====================\n\n");

    test_queue_init_free();
    test_queue_push_pop();
    test_queue_fifo_order();
    test_queue_full();
    test_queue_pressure();
    test_queue_wrap_around();
    test_queue_coalesce();
    test_queue_peek_consume();
    test_queue_isr_push();
    test_queue_power_of_two();
    test_queue_magic();
    test_queue_pressure_overflow();

    printf("\n====================\n");
    printf("All queue tests PASSED!\n");
    return 0;
}
```

### Acceptance Criteria
1. Queue init/free works correctly
2. Push/pop maintains FIFO order
3. Queue full condition is detected and handled
4. Queue pressure calculation is correct (no overflow with high counts)
5. Wrap-around works (uses bitwise AND, not modulo)
6. Coalescing replaces existing coalescable message
7. Peek/consume allows zero-copy access
8. ISR push is safe (no allocation, minimal work)
9. Capacity must be power of two (rejected otherwise)
10. Queue magic validation works (PT_QUEUE_MAGIC)
11. OT-safe push variant available (pt_queue_push_isr_ot)
12. OT race condition handled via PT_SLOT_READY flag (pop waits for data)

---

## Build Sequencing

This section documents the recommended build order for PeerTalk phases and their dependencies.

### Build Order

```
Phase 0 (PT_Log)
    ↓
Phase 1 (Foundation)
    ↓
Phase 2 (Protocol)  ← YOU ARE HERE
    ├──→ Phase 3 (Queue enhancements)
    │         ↓
    ├──→ Phase 4 (POSIX)
    │         ↓
    │    Phase 3.5 (SendEx API) ← requires Phase 4's send_udp callback
    │
    ├──→ Phase 5 (MacTCP) ← Session 5.9 requires pt_peer_find_by_name()
    │
    ├──→ Phase 6 (Open Transport)
    │
    └──→ Phase 7 (AppleTalk)
```

### Key Dependencies

| Phase/Session | Depends On | Critical Exports |
|---------------|------------|------------------|
| Phase 2 | Phase 1 (types, pt_peer extensions) | Protocol encode/decode, CRC-16, peer management, queue |
| Phase 3 | Phase 2 (pt_queue_pressure) | Enhanced queue operations |
| Phase 3.5 | Phase 3, Phase 4 (send_udp callback) | PeerTalk_SendEx API |
| Phase 4 | Phase 2 | POSIX networking implementation |
| Phase 5 | Phase 2 | MacTCP networking implementation |
| Phase 5.9 | Phase 2 (pt_peer_find_by_name) | Cross-transport peer deduplication |
| Phase 6 | Phase 2 | Open Transport networking implementation |
| Phase 7 | Phase 2 | AppleTalk networking implementation |

### Notes

1. **Phase 3.5 is separate**: Phase 3 (queue enhancements) can be built immediately after Phase 2. Phase 3.5 (SendEx API) requires Phase 4 because it uses the `send_udp()` callback which is platform-specific.

2. **Phase 5.9 dependency**: The MacTCP AppleTalk integration session requires `pt_peer_find_by_name()` for cross-transport peer deduplication (same peer reachable via both TCP/IP and AppleTalk appears as single entry).

3. **Parallel builds**: Phases 4, 5, 6, and 7 can be built in parallel once Phase 2 is complete. They share the same protocol layer but have independent platform implementations.

---

## Phase 2 Complete Checklist

### Protocol (Session 2.1)
- [ ] `src/core/protocol.h` and `protocol.c` complete
- [ ] Complete CRC-16 lookup table included
- [ ] Discovery packet encode/decode tested
- [ ] Message header encode/decode tested
- [ ] pt_discovery_decode() and pt_message_decode_header() accept ctx for logging
- [ ] CRC-16 validation tested (check value 0x2189 for "123456789")
- [ ] `pt_crc16_update()` for incremental CRC over non-contiguous data
- [ ] Error codes extend Phase 1's PeerTalk_Error (PT_ERR_CRC, PT_ERR_MAGIC, etc.)
- [ ] **NEW:** PT_MAGIC_UDP constant defined (0x50545544 = "PTUD")
- [ ] **NEW:** pt_udp_encode() and pt_udp_decode() implemented
- [ ] **NEW:** pt_strerror() maps error codes to human-readable strings

### Peer Management (Session 2.2)
- [ ] `src/core/peer.h` and `peer.c` complete
- [ ] Uses pt_* functions consistently (no direct stdlib calls)
- [ ] Phase 1 pt_peer_state extended with PT_PEER_STATE_UNUSED
- [ ] Phase 1 struct pt_peer extended with framing buffers and canary_corrupt flag
- [ ] Peer list init/free tested
- [ ] Peer create/destroy tested
- [ ] Peer state transitions validated
- [ ] CONNECTED state transitions logged at PT_LOG_INFO level
- [ ] Peer timeout detection tested
- [ ] Buffer canaries implemented with volatile canary_corrupt flag
- [ ] pt_peer_check_canaries() sets canary_corrupt for ISR-safe detection
- [ ] pt_peer_find_by_name() implemented (required by Phase 5.9)
- [ ] pt_tick_t requirements documented

### Message Queues (Session 2.3)
- [ ] `src/core/queue.h` and `queue.c` complete
- [ ] Uses pt_* functions consistently
- [ ] Queue FIFO order tested
- [ ] Queue full/empty detection tested
- [ ] Queue wrap-around tested (bitwise AND, not modulo)
- [ ] Queue coalescing tested
- [ ] ISR-safe push implemented (pt_queue_push_isr)
- [ ] OT-safe push implemented (pt_queue_push_isr_ot)
- [ ] OT race condition fix: PT_SLOT_READY flag prevents reading incomplete data
- [ ] Power-of-two capacity enforced
- [ ] Queue magic validation (PT_QUEUE_MAGIC)
- [ ] Pressure calculation tested with high counts (no overflow)
- [ ] pt_queue_init() logs capacity and allocation size at PT_LOG_INFO
- [ ] pt_queue_push() logs cascade backpressure (80%, 90%, 95% thresholds)
- [ ] All functions accept ctx parameter for logging context
- [ ] All tests pass on POSIX

### CSend Lessons Applied (see [CSEND-LESSONS.md](CSEND-LESSONS.md) Part C)
- [ ] Magic numbers written byte-by-byte as ASCII (C.1 Approach A - inherently portable)
- [ ] Unsigned subtraction handles tick wraparound naturally (C.2 - two's complement)
- [ ] N/A: Protocol uses ticks for timeout, not wall-clock timestamps (C.3)
- [ ] Platform-specific tick rates defined (60 ticks/sec on Mac) (C.4)
- [ ] N/A: Phase 2 uses pt_snprintf from Phase 1 compat layer (C.5)
- [ ] pt_strncpy used consistently (C.6 - Phase 1 provides null-termination)
- [ ] Graceful parsing for malformed packets (C.8)
- [ ] Cross-platform: POSIX and Mac peers discover each other
- [ ] Cross-platform: Messages transmit correctly both directions

---

## AppleTalk Compatibility Clarification (Phase 7 Integration)

This section addresses compatibility questions raised during plan review for Phase 7 (AppleTalk) integration.

### Discovery Format Compatibility

**Question:** Is `pt_discovery_packet` compatible with AppleTalk's NBP (Name Binding Protocol)?

**Answer:** No, NBP uses a different metadata model. Phase 7 will need an adapter layer:

- **UDP Discovery (TCP/IP):** Uses `pt_discovery_packet` with IP address, port, transports bitmask
- **NBP Discovery (AppleTalk):** Uses name:type@zone format with network/node/socket addresses

**Approach for Phase 7:**
1. NBP registration uses peer name from `pt_discovery_packet.name`
2. NBP lookup results are converted to `pt_discovery_packet` format for consistency
3. `pt_peer_find_by_name()` enables cross-transport deduplication (same peer on both transports)
4. Peer flags and transports are stored in the unified peer record

### ADSP Message Header Compatibility

**Question:** Is `pt_message_header` compatible with ADSP's EOM (End-of-Message) framing?

**Answer:** Yes, with adaptation:

- **TCP Messaging:** Uses `pt_message_header` (10 bytes) + payload + CRC-16 as a byte stream
- **ADSP Messaging:** Uses same `pt_message_header` format, but relies on ADSP's native EOM flag for message boundaries instead of TCP stream reassembly

**Approach for Phase 7:**
1. Encode: Same `pt_message_encode_header()` + payload + CRC-16
2. Send: Use ADSP `dspWrite` with `eom=true` flag (marks message boundary)
3. Receive: ADSP delivers complete messages at EOM boundaries (no reassembly needed)
4. Decode: Same `pt_message_decode_header()` + CRC-16 validation

The CRC-16 is retained even though ADSP has its own checksumming, for consistency with TCP messaging and additional application-level integrity verification.

### Cross-Transport Peer Deduplication

When a peer is reachable via both TCP/IP and AppleTalk:

1. First discovery (either transport) creates peer entry
2. Second discovery (other transport) finds existing peer via `pt_peer_find_by_name()`
3. Both transport addresses are stored in `peer->addresses[]`
4. `peer->transports` bitmask updated with PT_DISC_TRANSPORT_APPLETALK
5. Application sees single peer with multiple transport options

---

## References

- Subtext session.c: Buffer canary pattern (`CANARY_OBUF`, `CANARY_IBUF`)
- MacTCP Programmer's Guide: ASR restrictions (no allocation, no sync calls)
- NetworkingOpenTransport.txt Ch.5: Interrupt-time restrictions
- OpenTransport.h: OTAtomicAdd16, OTAtomicSetBit for notifier-safe operations
- QEMU util/crc-ccitt.c: CRC-16-CCITT lookup table (check value 0x2189)
- CLAUDE.md: Magic numbers, system limits
- Programming_With_AppleTalk_1991.txt: NBP registration, ADSP EOM framing
- PHASE-2-PROTOCOL-REVIEW.md: Design review with corrections (2026-01-24)

## Test Coverage Updates (2026-02-03)

**Additional tests implemented** to address gaps identified in TEST_GAP_ANALYSIS.md:

1. **test_discovery_name_overflow()** (HIGH) - Verifies malformed packet with name_len > PT_MAX_PEER_NAME is rejected
2. **test_message_header_malformed()** (HIGH) - Verifies wrong magic, wrong version, and invalid type are detected
3. **test_crc16_error_detection()** (MEDIUM) - Verifies CRC catches single bit flips, byte swaps, and length changes

All tests pass (14/14). Phase 2 test coverage: 85%+
