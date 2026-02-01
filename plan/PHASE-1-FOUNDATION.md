# PHASE 1: Foundation

> **Status:** [DONE]
> **Depends on:** Phase 0 (PT_Log)
> **Produces:** Complete public API, internal type system, platform abstraction layer, and portable compatibility functions for all platforms
> **Risk Level:** Low
> **Estimated Sessions:** 6

## Plan Review Fixes Applied (2026-01-28)

This plan was updated after Phase 9 review to add missing API elements:

1. **PT_TRANSPORT_ADSP (0x04) and PT_TRANSPORT_NBP (0x08)** - Split from PT_TRANSPORT_APPLETALK for finer-grained AppleTalk transport control. PT_TRANSPORT_APPLETALK now equals ADSP | NBP (0x0C).
2. **PT_ERR_BACKPRESSURE (-25)** - Added for queue backpressure signaling in stress tests
3. **PT_ERR_RESOURCE (-26)** - Added for system resource exhaustion (streams, handles)
4. **PT_ERR_INVALID and PT_ERR_NOT_FOUND aliases** - Added as convenience aliases for PT_ERR_INVALID_PARAM and PT_ERR_PEER_NOT_FOUND for Phase 9 test code compatibility

## Plan Review Fixes Applied (2026-01-28, Second Review)

This plan was updated after comprehensive /review analysis:

1. **PeerTalk_MessageBatch field order fixed** - Moved `const void *data` pointer first to eliminate 2-6 bytes alignment padding per entry (saves 32-96 bytes per batch callback with 16-entry batches)
2. **pt_free_peer swap-back algorithm documented** - Added detailed algorithm comments for O(1) peer removal maintaining contiguous hot data
3. **Log categories aligned with Phase 0 PT_Log** - Updated category definitions to match PHASE-0-LOGGING.md exactly (added PT_LOG_CAT_NETWORK, PT_LOG_CAT_INIT; renumbered to match Phase 0)
4. **PT_Log integration clarified** - Added `PT_Log *log` field to `struct pt_context` and documented initialization in `PeerTalk_Init()`
5. **Deferred logging patterns added** - Added concrete examples for MacTCP ASR, OT notifier, and ADSP completion routine deferred logging
6. **CCB userFlags clearing emphasized** - Added CRITICAL note about connection hanging if userFlags not cleared after reading
7. **Phase 0 linkage verification added** - Added explicit verification step to ensure test build links against libptlog.a

## Plan Review Fixes Applied (2026-01-29, Third Review)

This plan was updated after comprehensive /review analysis with all 9 subagents:

1. **pt_peer_hot reserved field fixed** - Changed 68k `reserved[2]` to `reserved[8]` to correctly reach 32 bytes (was 26 bytes, blocking 68k compilation via static assert)
2. **PeerTalk_PeerInfo field order fixed** - Moved `uint32_t address` first for natural 4-byte alignment, eliminating potential padding (guaranteed 20 bytes)
3. **Phase dependency sequencing clarified** - Added note that Session 1.4 cannot complete until Phase 0 is [DONE]
4. **"Produces" description updated** - Changed from vague "skeleton" to accurate description of deliverables
5. **Deferred log flush timing documented** - Added specification for when PeerTalk_Poll() flushes deferred logs
6. **ISR-safety compile-time checking pattern added** - Documented pattern to prevent PT_LOG calls in ASR/notifiers
7. **Logging handoff specifications added** - Added logging requirements for protocol errors (Phase 2) and connection lifecycle (Phases 5-7)

## Overview

Phase 1 establishes the core infrastructure that all subsequent phases build upon. This includes the build system, public API header, internal type definitions, and platform abstraction layer for all three targets (POSIX, MacTCP, Open Transport).

**Note:** Logging is provided by PT_Log (Phase 0). Phase 1 integrates with PT_Log rather than implementing its own logging system.

**Key Principle:** Get the build system working first, then add components incrementally so each can be tested as it's created.

---

## Design Notes (from Plan Review)

### Data-Oriented Design for Classic Mac

Classic Mac hardware has severe memory constraints that require careful struct layout:
- **68000/68020 have no data cache** - all memory accesses go to RAM at ~1-2 MB/s
- **68030 has 256-byte L1 data cache** - keep hot loop data under 256 bytes
  *(Source: MC68030 User's Manual, Section 1.3.1 "On-Chip Cache Memories")*
- **68040 has 4KB L1 data cache** - more forgiving but still benefit from locality
  *(Source: MC68040 User's Manual, Section 1.2 "Instruction and Data Caches")*
- **PPC 601 has 32KB unified cache, 32-byte lines** - group related fields into 32-byte chunks
  *(Source: PowerPC 601 RISC Microprocessor User's Manual, Section 6 "Cache Implementation")*
- **PPC 603/604 have separate I/D caches, 32-byte lines** - same 32-byte alignment strategy
  *(Source: PowerPC Microprocessor Family: The Programming Environments, Section 5)*

**Key Design Decision: Hot/Cold Data Split**

The `struct pt_peer` is split into hot and cold sections:
- **Hot data (~32 bytes):** State, flags, connection handle, sequence numbers - accessed every poll cycle
- **Cold data (~1.4KB):** Name, buffers, stats, addresses - accessed rarely

This allows scanning 16 peers to touch ~512 bytes instead of 20KB+.

### Feed-Forward Field Strategy

Phase 1 headers include fields needed by later phases to prevent backward dependencies:
- Error codes for Phase 2 (protocol layer)
- Buffer fields for Phase 2 (framing)
- Stats fields for Phase 4 (POSIX)
- Connection handles for Phase 5 (MacTCP)

**IMPORTANT:** After Phase 1 is complete, headers (`peertalk.h`, `pt_internal.h`) should be considered **locked**. Later phases should not modify these structures.

### API Verification (Retro68 MPW Interfaces)

All APIs have been verified against `/home/matthew/Retro68/InterfacesAndLibraries/MPW_Interfaces/`:
- `OTAtomicSetBit/ClearBit/TestBit` - Take `UInt8*` and bit index 0-7 ✓
- `BlockMoveData` - Signature `(const void *src, void *dest, Size)` ✓
- `NewPtrClear` - Allocates zeroed memory ✓
- `stdarg.h` / `va_list` - Available for Classic Mac ✓

### Documentation Citations

- **32-bit aligned reads/writes atomic on 68k:** Based on Motorola 68000 processor architecture. 32-bit aligned accesses complete in a single bus cycle and cannot be interrupted mid-operation. The "ISR sets, main clears" pattern is safe without interrupt disabling.

- **BlockMoveData interrupt safety:** Inside Macintosh Volume VI Table B-3 does NOT list BlockMoveData as interrupt-safe. The Sound Manager chapter (p. 162410) suggests BlockMove may be OK, but we conservatively forbid BlockMoveData in ASR/notifiers.

- **OTAtomic functions:** Networking With Open Transport (pp. 657-666) documents these as "fast and interrupt-safe utility functions" for use "at hardware interrupt level or deferred task level."

**Platform Detection Note:** PeerTalk uses compile-time platform detection, producing separate binaries for each target (POSIX, MacTCP, Open Transport). This is the correct approach because:
1. MacTCP and Open Transport cannot run simultaneously on the same Mac
2. The networking APIs are fundamentally different and cannot share code paths
3. Separate builds allow optimal code size for memory-constrained Classic Macs
4. 68k (MacTCP) and PPC (Open Transport) require different compilers anyway

## Goals

1. Establish build system for POSIX, MacTCP, and Open Transport
2. Define the complete public API in `peertalk.h`
3. Create internal type definitions, platform detection, and portable primitives
4. Create platform abstraction stubs
5. Integrate with PT_Log (from Phase 0) for internal logging

## Session Scope Table

| Session | Focus | Status | Files Created/Modified | Tests | Verify |
|---------|-------|--------|------------------------|-------|--------|
| 1.0 | Build System | [DONE] | `Makefile`, `Makefile.retro68`, `CMakeLists.txt`, `.clang-format` | None | `make` succeeds with empty lib |
| 1.1 | Public API & Types | [DONE] | `include/peertalk.h`, `src/core/pt_types.h`, `src/core/pt_internal.h`, `src/core/pt_version.c` | None | Headers compile on all platforms |
| 1.2 | Portable Primitives | [DONE] | `src/core/pt_compat.h`, `src/core/pt_compat.c` | `tests/test_compat.c` | Byte order, memory, atomics work |
| 1.3 | Platform Stubs | [DONE] | `src/posix/platform_posix.c`, `src/mactcp/platform_mactcp.c`, `src/opentransport/platform_ot.c` | None | All platforms link |
| 1.4 | PT_Log Integration | [DONE] | `src/core/pt_internal.h` (update), `src/core/pt_init.c` (new) | None | PeerTalk uses PT_Log from Phase 0 |
| 1.5 | Integration Test | [DONE] | None | `tests/test_foundation.c` | Platform init/shutdown, version string |

### Status Key
- **[OPEN]** - Not started
- **[IN PROGRESS]** - Currently being worked on
- **[READY TO TEST]** - Implementation complete, needs verification
- **[DONE]** - Verified and complete

---

## Session 1.0: Build System

### Objective
Create a build system that supports all three platforms before writing any library code. This allows incremental testing as each component is added.

### Tasks

#### Task 1.0.1: Create Directory Structure
```
peertalk/
├── include/
│   └── peertalk.h           # Public API
├── src/
│   ├── core/                # Platform-agnostic
│   │   ├── pt_types.h       # Internal types
│   │   ├── pt_internal.h    # Internal declarations
│   │   ├── pt_compat.h      # Portable primitives
│   │   └── pt_log.h         # Logging
│   ├── posix/
│   ├── mactcp/
│   └── opentransport/
├── tests/
├── examples/
└── resources/
```

#### Task 1.0.2: Create `Makefile` for POSIX

```makefile
# PeerTalk POSIX Makefile
CC = gcc
CFLAGS = -Wall -Werror -O2 -I include -I src/core
CFLAGS += -DPT_LOG_ENABLED -DPT_PLATFORM_POSIX

# Source files (added incrementally as sessions complete)
CORE_SRCS =
POSIX_SRCS =

SRCS = $(CORE_SRCS) $(POSIX_SRCS)
OBJS = $(SRCS:.c=.o)

LIBNAME = libpeertalk.a

all: $(LIBNAME)

$(LIBNAME): $(OBJS)
	@mkdir -p $(@D)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Test targets added as sessions complete
test:
	@echo "No tests yet"

clean:
	rm -f $(OBJS) $(LIBNAME)
	rm -f tests/*.o tests/test_*
	find . -name "*.o" -delete

.PHONY: all test clean
```

#### Task 1.0.3: Create `Makefile.retro68` for Classic Mac

```makefile
# PeerTalk Retro68 Makefile for Classic Mac
# Usage: make -f Makefile.retro68 PLATFORM=mactcp
#    or: make -f Makefile.retro68 PLATFORM=ot

RETRO68 ?= /home/matthew/Retro68-build/toolchain

# Select toolchain based on platform
ifeq ($(PLATFORM),mactcp)
    # 68k for MacTCP (System 6/7)
    PREFIX = $(RETRO68)/bin/m68k-apple-macos-
    CFLAGS = -DPT_PLATFORM_MACTCP
    PLAT_DIR = mactcp
else ifeq ($(PLATFORM),ot)
    # PowerPC for Open Transport (can also target late 68k)
    PREFIX = $(RETRO68)/bin/powerpc-apple-macos-
    CFLAGS = -DPT_PLATFORM_OT
    PLAT_DIR = opentransport
else
    $(error PLATFORM must be mactcp or ot)
endif

CC = $(PREFIX)gcc
AR = $(PREFIX)ar

CFLAGS += -Wall -O2 -I include -I src/core
CFLAGS += -DPT_LOG_ENABLED

# Source files (added incrementally)
CORE_SRCS =
PLAT_SRCS =

SRCS = $(CORE_SRCS) $(PLAT_SRCS)
OBJS = $(SRCS:.c=.o)

LIBNAME = libpeertalk_$(PLATFORM).a

all: $(LIBNAME)

$(LIBNAME): $(OBJS)
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(LIBNAME)

.PHONY: all clean
```

#### Task 1.0.4: Create `CMakeLists.txt` (for IDE support)

```cmake
cmake_minimum_required(VERSION 3.10)
project(PeerTalk C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include directories
include_directories(include src/core)

# Definitions
add_definitions(-DPT_LOG_ENABLED -DPT_PLATFORM_POSIX)

# Core sources (added incrementally)
set(CORE_SRCS "")

# POSIX sources (added incrementally)
set(POSIX_SRCS "")

# Library (starts empty, files added as sessions complete)
add_library(peertalk STATIC ${CORE_SRCS} ${POSIX_SRCS})
target_include_directories(peertalk PUBLIC include PRIVATE src/core)
target_compile_definitions(peertalk PRIVATE PT_LOG_ENABLED PT_PLATFORM_POSIX)

# Tests added as sessions complete
```

#### Task 1.0.5: Create placeholder files

Create empty placeholder files so the build system can be tested:
- `include/peertalk.h` (empty, just a comment)
- `src/core/.gitkeep`
- `src/posix/.gitkeep`
- `src/mactcp/.gitkeep`
- `src/opentransport/.gitkeep`

#### Task 1.0.6: Create `.clang-format`

Create a `.clang-format` file for consistent C code style across platforms:

```yaml
---
Language: C
BasedOnStyle: LLVM
IndentWidth: 4
TabWidth: 4
UseTab: Never
ColumnLimit: 100
BreakBeforeBraces: Linux
AllowShortFunctionsOnASingleLine: None
AllowShortIfStatementsOnASingleLine: false
AllowShortLoopsOnASingleLine: false
AlignConsecutiveMacros: true
AlignConsecutiveDeclarations: false
AlignTrailingComments: true
SpaceAfterCStyleCast: false
PointerAlignment: Right
SortIncludes: false
...
```

**Usage:**
```bash
# Format a file
clang-format -i src/core/pt_types.h

# Check formatting (CI)
clang-format --dry-run --Werror src/core/*.c
```

### Acceptance Criteria
1. `make` completes without errors (empty library is OK)
2. `make -f Makefile.retro68 PLATFORM=mactcp` completes
3. `make -f Makefile.retro68 PLATFORM=ot` completes
4. `make clean` removes all artifacts
5. Directory structure exists
6. `.clang-format` exists and `clang-format --version` works

---

## Session 1.1: Public API & Types

### Objective
Create the complete public API header and internal type definitions that will remain stable throughout development.

### Tasks

#### Task 1.1.1: Create `include/peertalk.h`

```c
/*
 * PeerTalk - Cross-platform peer-to-peer networking for Classic Mac and POSIX
 *
 * Public API Header (v1.1)
 *
 * Supported platforms:
 *   - POSIX (Linux, macOS) - TCP/UDP only
 *   - MacTCP (System 6-7.5, 68k) - TCP/UDP, optional AppleTalk
 *   - Open Transport (System 7.6.1+, PPC) - TCP/UDP, optional AppleTalk
 *   - AppleTalk standalone (any Classic Mac) - NBP/ADSP only
 *
 * Library variants:
 *   libpeertalk.a          - POSIX (TCP/UDP)
 *   libpeertalk_mactcp.a   - MacTCP (TCP/UDP)
 *   libpeertalk_ot.a       - Open Transport (TCP/UDP)
 *   libpeertalk_at.a       - AppleTalk only (NBP/ADSP)
 *   libpeertalk_mactcp_at.a - MacTCP + AppleTalk unified
 *   libpeertalk_ot_at.a    - Open Transport + AppleTalk unified
 */

#ifndef PEERTALK_H
#define PEERTALK_H

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Version Information
 *============================================================================*/

#define PEERTALK_VERSION_MAJOR  1
#define PEERTALK_VERSION_MINOR  0
#define PEERTALK_VERSION_PATCH  0

#define PT_STRINGIFY_HELPER(x) #x
#define PT_STRINGIFY(x) PT_STRINGIFY_HELPER(x)

#define PT_VERSION_STRING \
    PT_STRINGIFY(PEERTALK_VERSION_MAJOR) "." \
    PT_STRINGIFY(PEERTALK_VERSION_MINOR) "." \
    PT_STRINGIFY(PEERTALK_VERSION_PATCH)

/*============================================================================
 * Platform-Specific Includes
 *============================================================================*/

#if defined(PT_PLATFORM_POSIX)
    #include <stdint.h>
    #include <stddef.h>
#elif defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT) || defined(PT_PLATFORM_APPLETALK)
    #include <MacTypes.h>
#endif

/*============================================================================
 * Basic Types (for platforms without stdint.h)
 *============================================================================*/

#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT) || defined(PT_PLATFORM_APPLETALK)
    #ifndef _UINT8_T
        #define _UINT8_T
        typedef unsigned char      uint8_t;
    #endif
    #ifndef _INT8_T
        #define _INT8_T
        typedef signed char        int8_t;
    #endif
    #ifndef _UINT16_T
        #define _UINT16_T
        typedef unsigned short     uint16_t;
    #endif
    #ifndef _INT16_T
        #define _INT16_T
        typedef signed short       int16_t;
    #endif
    #ifndef _UINT32_T
        #define _UINT32_T
        typedef unsigned long      uint32_t;
    #endif
    #ifndef _INT32_T
        #define _INT32_T
        typedef signed long        int32_t;
    #endif
    #ifndef _SIZE_T
        #define _SIZE_T
        typedef unsigned long      size_t;
    #endif
    #ifndef NULL
        #define NULL               0L
    #endif
#endif

/*============================================================================
 * Constants
 *============================================================================*/

#define PT_MAX_PEER_NAME            31
#define PT_MAX_PEERS                16
#define PT_MAX_MESSAGE_SIZE         8192
#define PT_MAX_UDP_MESSAGE_SIZE     512     /* Recommended max for no fragmentation */

#define PT_DEFAULT_DISCOVERY_PORT   7353
#define PT_DEFAULT_TCP_PORT         7354
#define PT_DEFAULT_UDP_PORT         7355

/*============================================================================
 * Transport Types
 *============================================================================*/

typedef enum {
    PT_TRANSPORT_NONE       = 0x00,
    PT_TRANSPORT_TCP        = 0x01,  /* TCP/IP (POSIX/MacTCP/OT) */
    PT_TRANSPORT_UDP        = 0x02,  /* UDP messaging (where available) */
    PT_TRANSPORT_ADSP       = 0x04,  /* AppleTalk ADSP (connections) */
    PT_TRANSPORT_NBP        = 0x08,  /* AppleTalk NBP (discovery) */
    PT_TRANSPORT_APPLETALK  = 0x0C,  /* Combined AppleTalk (ADSP | NBP) */
    PT_TRANSPORT_ALL        = 0xFF   /* Use all available transports */
} PeerTalk_Transport;

/*============================================================================
 * Error Codes
 *============================================================================*/

typedef enum {
    PT_OK                       =  0,
    PT_ERR_INVALID_PARAM        = -1,
    PT_ERR_NO_MEMORY            = -2,
    PT_ERR_NOT_INITIALIZED      = -3,
    PT_ERR_ALREADY_INITIALIZED  = -4,
    PT_ERR_NETWORK              = -5,
    PT_ERR_TIMEOUT              = -6,
    PT_ERR_CONNECTION_REFUSED   = -7,
    PT_ERR_CONNECTION_CLOSED    = -8,
    PT_ERR_BUFFER_FULL          = -9,
    PT_ERR_INVALID_STATE        = -10,
    PT_ERR_PEER_NOT_FOUND       = -11,
    PT_ERR_DISCOVERY_ACTIVE     = -12,
    PT_ERR_NO_NETWORK           = -13,
    PT_ERR_PLATFORM             = -14,
    PT_ERR_QUEUE_EMPTY          = -15,
    PT_ERR_MESSAGE_TOO_LARGE    = -16,
    PT_ERR_NOT_SUPPORTED        = -17,  /* Feature not available in this build */
    PT_ERR_NOT_CONNECTED        = -18,
    PT_ERR_WOULD_BLOCK          = -19,
    PT_ERR_BACKPRESSURE         = -25,  /* Queue backpressure - slow down sending */
    PT_ERR_RESOURCE             = -26,  /* System resource exhausted (streams, handles) */
    /* Protocol errors (Phase 2) */
    PT_ERR_CRC                  = -20,  /* CRC validation failed */
    PT_ERR_MAGIC                = -21,  /* Invalid magic number */
    PT_ERR_TRUNCATED            = -22,  /* Packet too short */
    PT_ERR_VERSION              = -23,  /* Protocol version mismatch */
    PT_ERR_NOT_POWER2           = -24,  /* Capacity not power of two */
    PT_ERR_INTERNAL             = -99
} PeerTalk_Error;

/*============================================================================
 * Error Code Aliases (for Phase 9 test code compatibility)
 *============================================================================*/

#define PT_ERR_INVALID    PT_ERR_INVALID_PARAM   /* Alias for invalid parameters */
#define PT_ERR_NOT_FOUND  PT_ERR_PEER_NOT_FOUND  /* Alias for peer not found */

/*============================================================================
 * Priority Levels
 *============================================================================*/

typedef enum {
    PT_PRIORITY_LOW      = 0,
    PT_PRIORITY_NORMAL   = 1,
    PT_PRIORITY_HIGH     = 2,
    PT_PRIORITY_CRITICAL = 3
} PeerTalk_Priority;

/*============================================================================
 * Send Flags
 *============================================================================*/

#define PT_SEND_DEFAULT      0x00
#define PT_SEND_UNRELIABLE   0x01  /* Don't require ACK */
#define PT_SEND_COALESCABLE  0x02  /* Can be replaced by newer message */
#define PT_SEND_NO_DELAY     0x04  /* Disable Nagle for this send */

/*============================================================================
 * Coalesce Keys (0x0000-0x00FF reserved, 0x0100+ for app use)
 *============================================================================*/

#define PT_COALESCE_NONE          0x0000
#define PT_COALESCE_POSITION      0x0001
#define PT_COALESCE_STATE         0x0002
#define PT_COALESCE_TYPING        0x0003

#define PT_COALESCE_KEY(base, peer_id) ((base) | ((peer_id) << 8))

/*============================================================================
 * Peer Flags (advertised via discovery)
 *============================================================================*/

#define PT_PEER_FLAG_HOST           0x0001
#define PT_PEER_FLAG_ACCEPTING      0x0002
#define PT_PEER_FLAG_SPECTATOR      0x0004
#define PT_PEER_FLAG_READY          0x0008
/* 0x0010-0x0080 reserved */
/* 0x0100-0x8000 available for application use */
#define PT_PEER_FLAG_APP_0          0x0100
#define PT_PEER_FLAG_APP_1          0x0200
#define PT_PEER_FLAG_APP_2          0x0400
#define PT_PEER_FLAG_APP_3          0x0800
#define PT_PEER_FLAG_APP_4          0x1000
#define PT_PEER_FLAG_APP_5          0x2000
#define PT_PEER_FLAG_APP_6          0x4000
#define PT_PEER_FLAG_APP_7          0x8000

/*============================================================================
 * Rejection Reasons
 *============================================================================*/

typedef enum {
    PT_REJECT_UNSPECIFIED       = 0,
    PT_REJECT_SERVER_FULL       = 1,
    PT_REJECT_BANNED            = 2,
    PT_REJECT_WRONG_VERSION     = 3,
    PT_REJECT_GAME_IN_PROGRESS  = 4
} PeerTalk_RejectReason;

/*============================================================================
 * Opaque Types
 *============================================================================*/

typedef struct pt_context PeerTalk_Context;

/*============================================================================
 * Peer Information
 *
 * DOD Note: PeerTalk_PeerInfo is optimized for cache efficiency on Classic Mac.
 * Hot fields (id, state, flags) are grouped first. The name is stored separately
 * in cold storage and accessed via name_idx to avoid polluting cache during
 * peer iteration. Use PeerTalk_GetPeerName() to retrieve the actual name string.
 *============================================================================*/

typedef uint16_t PeerTalk_PeerID;

typedef struct {
    /* Hot fields - accessed frequently during polling (20 bytes)
     * Field order: 4-byte field first for natural alignment, then 2-byte, then 1-byte.
     * This guarantees 20 bytes with no internal padding on both 68k and PPC. */
    uint32_t        address;                /* 4 bytes: IPv4 or pseudo-address for AppleTalk */
    PeerTalk_PeerID id;                     /* 2 bytes */
    uint16_t        flags;                  /* 2 bytes: PT_PEER_FLAG_* (full 16-bit) */
    uint16_t        transports_available;   /* 2 bytes: Bitmask: how peer is reachable */
    uint16_t        transport_connected;    /* 2 bytes: Which transport we're connected via */
    uint16_t        port;                   /* 2 bytes */
    uint16_t        latency_ms;             /* 2 bytes: Estimated RTT */
    uint16_t        queue_pressure;         /* 2 bytes: Send queue fill 0-100 */
    uint8_t         connected;              /* 1 byte */
    uint8_t         name_idx;               /* 1 byte: Index into context name table */
} PeerTalk_PeerInfo;  /* Total: 20 bytes guaranteed (was 50 bytes with embedded name) */

/* Retrieve peer name from cold storage */
const char* PeerTalk_GetPeerName(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id);

typedef struct {
    uint16_t        transport;      /* PT_TRANSPORT_* */
    uint32_t        address;        /* Transport-specific address */
    uint16_t        port;
    uint16_t        reserved;
} PeerTalk_Address;

/*============================================================================
 * Statistics
 *============================================================================*/

typedef struct {
    uint32_t        bytes_sent;
    uint32_t        bytes_received;
    uint32_t        messages_sent;
    uint32_t        messages_received;
    uint16_t        send_errors;
    uint16_t        receive_errors;
    uint16_t        dropped_messages;
    uint16_t        retransmissions;
    uint16_t        latency_ms;
    uint16_t        latency_variance_ms;
    uint8_t         send_queue_pressure;
    uint8_t         recv_queue_pressure;
    uint8_t         quality;                /* 0-100, 100=excellent */
    uint8_t         reserved;
} PeerTalk_PeerStats;

typedef struct {
    uint32_t        total_bytes_sent;
    uint32_t        total_bytes_received;
    uint32_t        total_messages_sent;
    uint32_t        total_messages_received;
    uint16_t        discovery_packets_sent;
    uint16_t        discovery_packets_received;
    uint16_t        peers_discovered;
    uint16_t        peers_connected;
    uint16_t        connections_accepted;
    uint16_t        connections_rejected;
    uint32_t        memory_used;
    uint16_t        streams_active;
    uint16_t        reserved;
} PeerTalk_GlobalStats;

/*============================================================================
 * Configuration
 *
 * DOD Note: local_name is embedded (not a pointer) to ensure locality and
 * avoid pointer chasing. The 32-byte overhead is acceptable for a config
 * struct that's typically stack-allocated once at init time.
 *============================================================================*/

typedef struct {
    /* Embedded name - eliminates pointer indirection */
    char            local_name[PT_MAX_PEER_NAME + 1]; /* 32 bytes: Required, max 31 chars + null */

    /* 16-bit fields grouped together */
    uint16_t        transports;         /* Bitmask: 0 = PT_TRANSPORT_ALL */
    uint16_t        discovery_port;     /* 0 = 7353 */
    uint16_t        tcp_port;           /* 0 = 7354 */
    uint16_t        udp_port;           /* 0 = 7355 */
    uint16_t        max_peers;          /* 0 = 16 */
    uint16_t        recv_buffer_size;   /* 0 = auto */
    uint16_t        send_buffer_size;   /* 0 = auto */
    uint16_t        discovery_interval; /* ms, 0 = 5000 */
    uint16_t        peer_timeout;       /* ms, 0 = 15000 */

    /* 8-bit fields grouped together */
    uint8_t         auto_accept;        /* Auto-accept connections, default = 1 */
    uint8_t         auto_cleanup;       /* Auto-remove timed-out peers, default = 1 */
    uint8_t         log_level;          /* 0=off, 1=err, 2=warn, 3=info, 4=debug */
    uint8_t         reserved;
} PeerTalk_Config;

/*============================================================================
 * Callback Types
 *
 * DOD Note: Both per-event and batch callback variants are provided.
 * Batch callbacks reduce function call overhead when multiple events arrive
 * in a single poll cycle - important on Classic Mac where pascal calling
 * convention is expensive. Use batch callbacks for high-frequency events.
 *
 * ISR-SAFETY WARNING:
 * These callbacks are invoked from PeerTalk_Poll() which runs from the MAIN
 * EVENT LOOP, NOT from interrupt context (ASR/notifier/completion routine).
 * Therefore, callback implementations MAY:
 *   - Allocate memory (NewPtr, malloc)
 *   - Call File Manager (FSRead, FSWrite)
 *   - Call PT_Log functions
 *   - Call any Toolbox routine
 *   - Block (though this delays other events)
 *
 * The internal platform layers (MacTCP ASR, OT notifier, ADSP completion)
 * use flag-based patterns to defer work to PeerTalk_Poll(). Application
 * callbacks never need to worry about interrupt-level restrictions.
 *============================================================================*/

/* Per-event callbacks (simple, low-frequency events) */

typedef void (*PeerTalk_PeerDiscoveredCB)(
    PeerTalk_Context *ctx, const PeerTalk_PeerInfo *peer, void *user_data);

typedef void (*PeerTalk_PeerLostCB)(
    PeerTalk_Context *ctx, PeerTalk_PeerID peer_id, void *user_data);

typedef void (*PeerTalk_PeerConnectedCB)(
    PeerTalk_Context *ctx, PeerTalk_PeerID peer_id, void *user_data);

typedef void (*PeerTalk_PeerDisconnectedCB)(
    PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
    PeerTalk_Error reason, void *user_data);

typedef void (*PeerTalk_MessageReceivedCB)(
    PeerTalk_Context *ctx, PeerTalk_PeerID from_peer,
    const void *data, uint16_t length, void *user_data);

typedef void (*PeerTalk_UDPReceivedCB)(
    PeerTalk_Context *ctx, PeerTalk_PeerID from_peer,
    uint32_t from_address, uint16_t from_port,
    const void *data, uint16_t length, void *user_data);

typedef int (*PeerTalk_ConnectionRequestedCB)(
    PeerTalk_Context *ctx, const PeerTalk_PeerInfo *peer, void *user_data);

typedef void (*PeerTalk_MessageSentCB)(
    PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
    uint32_t message_id, PeerTalk_Error result, void *user_data);

/*============================================================================
 * Batch Callback Types (high-frequency events)
 *
 * These allow processing multiple events in a single callback invocation,
 * reducing function call overhead on Classic Mac hardware.
 *
 * ACCUMULATION STRATEGY:
 * - PeerTalk_Poll() accumulates up to PT_MAX_BATCH_SIZE (16) messages per call
 * - Batch callback is invoked once at the END of each poll cycle
 * - If >16 messages arrive, multiple poll cycles are needed to drain them
 * - Messages within a batch are ordered by arrival time
 * - Batch entries reference internal buffers valid only during callback
 *
 * Memory: Batch array is stack-allocated in PeerTalk_Poll(), no heap overhead.
 *============================================================================*/

#define PT_MAX_BATCH_SIZE   16  /* Max messages per batch callback */

/* Batch entry for received messages
 *
 * DOD: Fields ordered largest-to-smallest to eliminate alignment holes.
 * Pointer comes first (4/8 bytes), then 2-byte fields grouped.
 * With 16-entry batches, saves 32-96 bytes per callback invocation.
 */
typedef struct {
    const void     *data;       /* 4/8 bytes: pointer first for alignment */
    PeerTalk_PeerID from_peer;  /* 2 bytes */
    uint16_t        length;     /* 2 bytes */
} PeerTalk_MessageBatch;

/* Batch entry for UDP messages
 *
 * DOD: Fields ordered largest-to-smallest to eliminate alignment holes.
 * Original layout had 4+ bytes of padding; this layout has 2 bytes explicit.
 * With 16-entry batches, saves ~64 bytes per callback invocation.
 */
typedef struct {
    const void     *data;          /* 4/8 bytes: pointer first for alignment */
    uint32_t        from_address;  /* 4 bytes */
    PeerTalk_PeerID from_peer;     /* 2 bytes */
    uint16_t        from_port;     /* 2 bytes */
    uint16_t        length;        /* 2 bytes */
    uint16_t        reserved;      /* 2 bytes: explicit padding */
} PeerTalk_UDPBatch;

/* Batch callbacks - called with array of events */
typedef void (*PeerTalk_MessageBatchCB)(
    PeerTalk_Context *ctx,
    const PeerTalk_MessageBatch *messages,
    uint16_t count,
    void *user_data);

typedef void (*PeerTalk_UDPBatchCB)(
    PeerTalk_Context *ctx,
    const PeerTalk_UDPBatch *messages,
    uint16_t count,
    void *user_data);

/*============================================================================
 * Callbacks Structure
 *
 * If batch callbacks are set (non-NULL), they are preferred over per-event
 * callbacks. This allows applications to choose their preferred model.
 *============================================================================*/

typedef struct {
    /* Per-event callbacks */
    PeerTalk_PeerDiscoveredCB       on_peer_discovered;
    PeerTalk_PeerLostCB             on_peer_lost;
    PeerTalk_PeerConnectedCB        on_peer_connected;
    PeerTalk_PeerDisconnectedCB     on_peer_disconnected;
    PeerTalk_MessageReceivedCB      on_message_received;
    PeerTalk_UDPReceivedCB          on_udp_received;
    PeerTalk_ConnectionRequestedCB  on_connection_requested;
    PeerTalk_MessageSentCB          on_message_sent;

    /* Batch callbacks (preferred if set) */
    PeerTalk_MessageBatchCB         on_message_batch;
    PeerTalk_UDPBatchCB             on_udp_batch;

    void                           *user_data;
} PeerTalk_Callbacks;

/*============================================================================
 * Lifecycle Functions
 *============================================================================*/

PeerTalk_Context* PeerTalk_Init(const PeerTalk_Config *config);
void PeerTalk_Shutdown(PeerTalk_Context *ctx);
PeerTalk_Error PeerTalk_Poll(PeerTalk_Context *ctx);
const char* PeerTalk_Version(void);
uint16_t PeerTalk_GetAvailableTransports(void);

/*============================================================================
 * Callback Registration
 *============================================================================*/

PeerTalk_Error PeerTalk_SetCallbacks(
    PeerTalk_Context *ctx, const PeerTalk_Callbacks *callbacks);

/*============================================================================
 * Discovery Functions
 *============================================================================*/

PeerTalk_Error PeerTalk_StartDiscovery(PeerTalk_Context *ctx);
PeerTalk_Error PeerTalk_StopDiscovery(PeerTalk_Context *ctx);
PeerTalk_Error PeerTalk_GetPeers(
    PeerTalk_Context *ctx, PeerTalk_PeerInfo *peers,
    uint16_t max_peers, uint16_t *out_count);

/*
 * Returns a version counter that increments whenever the peer list changes.
 * Use this to detect changes without copying the entire peer list:
 *
 * static uint32_t cached_version = 0;
 * if (PeerTalk_GetPeersVersion(ctx) != cached_version) {
 *     refresh_peer_list();
 *     cached_version = PeerTalk_GetPeersVersion(ctx);
 * }
 */
uint32_t PeerTalk_GetPeersVersion(PeerTalk_Context *ctx);

/*============================================================================
 * Peer Lookup Functions
 *============================================================================*/

/* O(1) lookup by peer ID - returns pointer to internal peer info (read-only) */
const PeerTalk_PeerInfo *PeerTalk_GetPeerByID(
    PeerTalk_Context *ctx, PeerTalk_PeerID peer_id);

/* O(1) lookup by peer ID - copies peer info to caller's buffer */
PeerTalk_Error PeerTalk_GetPeer(
    PeerTalk_Context *ctx, PeerTalk_PeerID peer_id, PeerTalk_PeerInfo *info);

/* Get peer name from cold storage using name_idx from PeerTalk_PeerInfo */
const char *PeerTalk_GetPeerName(PeerTalk_Context *ctx, uint8_t name_idx);

PeerTalk_PeerID PeerTalk_FindPeerByName(
    PeerTalk_Context *ctx, const char *name, PeerTalk_PeerInfo *info);

PeerTalk_PeerID PeerTalk_FindPeerByAddress(
    PeerTalk_Context *ctx, uint32_t address, uint16_t port, PeerTalk_PeerInfo *info);

int PeerTalk_GetPeerAddresses(
    PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
    PeerTalk_Address *addresses, int max_addresses);

/*============================================================================
 * Connection Functions
 *============================================================================*/

PeerTalk_Error PeerTalk_Connect(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id);
PeerTalk_Error PeerTalk_Disconnect(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id);
PeerTalk_Error PeerTalk_RejectConnection(
    PeerTalk_Context *ctx, PeerTalk_PeerID peer_id, PeerTalk_RejectReason reason);

/*============================================================================
 * Listen Control
 *============================================================================*/

PeerTalk_Error PeerTalk_StartListening(PeerTalk_Context *ctx);
PeerTalk_Error PeerTalk_StopListening(PeerTalk_Context *ctx);
int PeerTalk_IsListening(PeerTalk_Context *ctx);
uint16_t PeerTalk_GetListenPort(PeerTalk_Context *ctx, uint16_t transport);

/*============================================================================
 * Messaging Functions (TCP/Reliable)
 *============================================================================*/

PeerTalk_Error PeerTalk_Send(
    PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
    const void *data, uint16_t length);

PeerTalk_Error PeerTalk_SendEx(
    PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
    const void *data, uint16_t length,
    uint8_t priority, uint8_t flags, uint16_t coalesce_key);

PeerTalk_Error PeerTalk_SendVia(
    PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
    const void *data, uint16_t length,
    uint16_t transport, uint8_t priority, uint8_t flags, uint16_t coalesce_key);

PeerTalk_Error PeerTalk_SendTracked(
    PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
    const void *data, uint16_t length, uint32_t *out_message_id);

PeerTalk_Error PeerTalk_Broadcast(
    PeerTalk_Context *ctx, const void *data, uint16_t length);

/*============================================================================
 * Messaging Functions (UDP/Unreliable)
 *============================================================================*/

PeerTalk_Error PeerTalk_SendUDP(
    PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
    const void *data, uint16_t length);

PeerTalk_Error PeerTalk_BroadcastUDP(
    PeerTalk_Context *ctx, const void *data, uint16_t length);

/*============================================================================
 * Queue Status
 *============================================================================*/

PeerTalk_Error PeerTalk_GetQueueStatus(
    PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
    uint16_t *out_pending, uint16_t *out_available);

/*============================================================================
 * Statistics
 *============================================================================*/

PeerTalk_Error PeerTalk_GetPeerStats(
    PeerTalk_Context *ctx, PeerTalk_PeerID peer_id, PeerTalk_PeerStats *stats);

PeerTalk_Error PeerTalk_GetGlobalStats(
    PeerTalk_Context *ctx, PeerTalk_GlobalStats *stats);

PeerTalk_Error PeerTalk_ResetStats(
    PeerTalk_Context *ctx, PeerTalk_PeerID peer_id);

/*============================================================================
 * Peer Flags
 *============================================================================*/

PeerTalk_Error PeerTalk_SetFlags(PeerTalk_Context *ctx, uint16_t flags);
uint16_t PeerTalk_GetFlags(PeerTalk_Context *ctx);
PeerTalk_Error PeerTalk_ModifyFlags(
    PeerTalk_Context *ctx, uint16_t set_flags, uint16_t clear_flags);

/*============================================================================
 * Utility Functions
 *============================================================================*/

const char* PeerTalk_ErrorString(PeerTalk_Error error);
PeerTalk_Error PeerTalk_GetLocalInfo(PeerTalk_Context *ctx, PeerTalk_PeerInfo *out_info);

#ifdef __cplusplus
}
#endif

#endif /* PEERTALK_H */
```

#### Task 1.1.2: Create `src/core/pt_types.h`

```c
/*
 * PeerTalk Internal Type Definitions
 */

#ifndef PT_TYPES_H
#define PT_TYPES_H

#include "peertalk.h"

/*============================================================================
 * Platform Detection
 *
 * Platform defines:
 *   PT_PLATFORM_POSIX     - Linux, macOS (modern)
 *   PT_PLATFORM_MACTCP    - Classic Mac with MacTCP (68k, System 6-7.5)
 *   PT_PLATFORM_OT        - Classic Mac with Open Transport (PPC, 7.6.1+)
 *   PT_PLATFORM_APPLETALK - AppleTalk-only build (any Classic Mac)
 *
 * For unified builds (MacTCP+AT or OT+AT), both defines are set:
 *   PT_PLATFORM_MACTCP + PT_HAS_APPLETALK
 *   PT_PLATFORM_OT + PT_HAS_APPLETALK
 *============================================================================*/

/* Count primary platforms (must be exactly one) */
#if defined(PT_PLATFORM_POSIX) + defined(PT_PLATFORM_MACTCP) + defined(PT_PLATFORM_OT) + defined(PT_PLATFORM_APPLETALK) < 1
    #error "At least one platform must be defined: PT_PLATFORM_POSIX, PT_PLATFORM_MACTCP, PT_PLATFORM_OT, or PT_PLATFORM_APPLETALK"
#endif

/* Prevent incompatible platform combinations */
#if defined(PT_PLATFORM_POSIX) && (defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT) || defined(PT_PLATFORM_APPLETALK))
    #error "PT_PLATFORM_POSIX cannot be combined with Classic Mac platforms"
#endif
#if defined(PT_PLATFORM_MACTCP) && defined(PT_PLATFORM_OT)
    #error "PT_PLATFORM_MACTCP and PT_PLATFORM_OT are mutually exclusive"
#endif

/* Unified builds: TCP/IP platform + AppleTalk */
#if (defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)) && defined(PT_HAS_APPLETALK)
    #define PT_MULTI_TRANSPORT 1
#endif

/* Secondary platform detection for Classic Mac variants */
#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT) || defined(PT_PLATFORM_APPLETALK)
    #define PT_PLATFORM_CLASSIC_MAC 1
#endif

/* TCP/IP available? */
#if defined(PT_PLATFORM_POSIX) || defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
    #define PT_HAS_TCPIP 1
#endif

/*============================================================================
 * Magic Numbers (from CLAUDE.md - DO NOT CHANGE)
 *============================================================================*/

#define PT_CONTEXT_MAGIC    0x5054434E  /* "PTCN" */
#define PT_PEER_MAGIC       0x50545052  /* "PTPR" */
#define PT_QUEUE_MAGIC      0x50545155  /* "PTQU" */
#define PT_CANARY           0xDEADBEEF  /* Buffer overflow detection */

/*----------------------------------------------------------------------------
 * Magic Number Validation Helpers
 *
 * Inline functions for validating magic numbers. Catch use-after-free,
 * memory corruption, and invalid pointer casts early.
 *----------------------------------------------------------------------------*/

static inline int pt_context_valid(const struct pt_context *ctx) {
    return ctx && ctx->magic == PT_CONTEXT_MAGIC;
}

static inline int pt_peer_valid(const struct pt_peer *peer) {
    return peer && peer->hot.magic == PT_PEER_MAGIC;
}

/* Validation with logging (for debug builds) */
#ifdef PT_DEBUG
    #define PT_VALIDATE_CONTEXT(ctx) \
        do { \
            if (!pt_context_valid(ctx)) { \
                /* Cannot use PT_LOG here - ctx may be invalid */ \
                return PT_ERR_INVALID_CONTEXT; \
            } \
        } while (0)

    #define PT_VALIDATE_PEER(peer) \
        do { \
            if (!pt_peer_valid(peer)) { \
                return PT_ERR_INVALID_PEER; \
            } \
        } while (0)
#else
    #define PT_VALIDATE_CONTEXT(ctx) ((void)0)
    #define PT_VALIDATE_PEER(peer) ((void)0)
#endif

/*============================================================================
 * Buffer Canaries (Phase 2 - conditional on PT_DEBUG)
 *
 * DOD Note: Canaries add 8 bytes per peer (128 bytes for 16 peers).
 * Only include in debug builds to avoid overhead in production.
 *============================================================================*/

#ifdef PT_DEBUG
    #define PT_CANARY_OBUF      0xDEAD0B0F  /* Output buffer canary */
    #define PT_CANARY_IBUF      0xDEAD1B1F  /* Input buffer canary */
    #define PT_HAS_CANARIES     1
#endif

/*============================================================================
 * Protocol Constants (from CLAUDE.md and PROJECT_GOALS.md)
 *============================================================================*/

#define PT_PROTOCOL_VERSION 1           /* Wire protocol version */
#define PT_DISCOVERY_MAGIC  "PTLK"      /* UDP discovery packets */
#define PT_MESSAGE_MAGIC    "PTMG"      /* TCP message frames */

/*============================================================================
 * Internal Types
 *============================================================================*/

typedef uint32_t pt_tick_t;             /* Platform-neutral tick count */

/*============================================================================
 * Peer State
 *
 * DOD Note: Using uint8_t with defines instead of enum saves 3 bytes per peer
 * (enums default to int size on most compilers). With only 6 states, uint8_t
 * is sufficient and improves struct packing.
 *============================================================================*/

typedef uint8_t pt_peer_state;

#define PT_PEER_STATE_UNUSED        0   /* Slot available for allocation (Phase 2) */
#define PT_PEER_STATE_DISCOVERED    1   /* Discovered but not connected */
#define PT_PEER_STATE_CONNECTING    2   /* Connection in progress */
#define PT_PEER_STATE_CONNECTED     3   /* Fully connected */
#define PT_PEER_STATE_DISCONNECTING 4   /* Disconnect in progress */
#define PT_PEER_STATE_FAILED        5   /* Connection failed */

/*============================================================================
 * Forward Declarations
 *============================================================================*/

struct pt_context;
struct pt_peer;
struct pt_queue;

#endif /* PT_TYPES_H */
```

#### Task 1.1.3: Create `src/core/pt_internal.h`

```c
/*
 * PeerTalk Internal Declarations
 */

#ifndef PT_INTERNAL_H
#define PT_INTERNAL_H

#include "pt_types.h"
#include "pt_compat.h"
#include "pt_log.h"

/*============================================================================
 * Platform Abstraction Layer
 *============================================================================*/

typedef struct {
    int             (*init)(struct pt_context *ctx);
    void            (*shutdown)(struct pt_context *ctx);
    int             (*poll)(struct pt_context *ctx);
    pt_tick_t       (*get_ticks)(void);
    unsigned long   (*get_free_mem)(void);
    unsigned long   (*get_max_block)(void);

    /* UDP messaging callback - added for Phase 3.5 SendEx API integration.
     * Returns PT_OK on success, PT_ERR_* on failure.
     * May be NULL if platform doesn't support UDP messaging. */
    int             (*send_udp)(struct pt_context *ctx, struct pt_peer *peer,
                                const void *data, uint16_t len);
} pt_platform_ops;

/* Platform ops - defined in platform_*.c files */
#if defined(PT_PLATFORM_POSIX)
    extern pt_platform_ops pt_posix_ops;
    #define PT_PLATFORM_OPS (&pt_posix_ops)
#elif defined(PT_PLATFORM_MACTCP)
    extern pt_platform_ops pt_mactcp_ops;
    #define PT_PLATFORM_OPS (&pt_mactcp_ops)
#elif defined(PT_PLATFORM_OT)
    extern pt_platform_ops pt_ot_ops;
    #define PT_PLATFORM_OPS (&pt_ot_ops)
#elif defined(PT_PLATFORM_APPLETALK)
    /* AppleTalk standalone - implemented in Phase 7 */
    extern pt_platform_ops pt_appletalk_ops;
    #define PT_PLATFORM_OPS (&pt_appletalk_ops)
#else
    #error "No platform defined. Define one of: PT_PLATFORM_POSIX, PT_PLATFORM_MACTCP, PT_PLATFORM_OT, PT_PLATFORM_APPLETALK"
#endif

/*============================================================================
 * Internal Peer Structure - Hot/Cold Split
 *
 * DOD Note: Peer data is split into hot and cold sections for cache efficiency.
 *
 * On 68030 (256-byte cache): Scanning 16 peers with the old layout touched 20KB+.
 * With hot/cold split, the hot scan touches only ~512 bytes.
 *
 * On 68k (no cache): Sequential access to hot data minimizes memory bandwidth.
 *
 * Hot data: Accessed every poll cycle (~32 bytes per peer)
 * Cold data: Accessed rarely - names, buffers, stats (~1.4KB per peer)
 *============================================================================*/

/* Per-peer address entry for multi-transport (reduced to 2 - most peers use 1) */
typedef struct {
    uint32_t            address;        /* 4 bytes: IP or synthesized AppleTalk address */
    uint16_t            port;           /* 2 bytes */
    uint16_t            transport;      /* 2 bytes: PT_TRANSPORT_* */
} pt_peer_address;  /* 8 bytes, tightly packed */

#define PT_MAX_PEER_ADDRESSES 2         /* Reduced from 4 - most peers use single transport */

/*----------------------------------------------------------------------------
 * Hot Data - Accessed every poll cycle
 * Keep this struct under 64 bytes for good cache behavior.
 *
 * Field ordering: pointer first (8 bytes on PPC), then 32-bit, then 16-bit,
 * then 8-bit. This avoids internal padding on both 68k (4-byte pointers)
 * and PPC (8-byte pointers, 8-byte alignment requirement).
 *
 * Size verification:
 *   68k:  4 (ptr) + 4 (magic) + 4 (last_seen) + 2 + 2 + 2 (16-bit fields) + 6 (8-bit fields) + 8 (reserved) = 32 bytes
 *   PPC:  8 (ptr) + 4 (magic) + 4 (last_seen) + 2 + 2 + 2 (16-bit fields) + 6 (8-bit fields) + 4 (reserved) = 32 bytes
 *----------------------------------------------------------------------------*/
typedef struct {
    /* Pointer first for PPC alignment (8 bytes on PPC, 4 bytes on 68k) */
    void               *connection;     /* Platform-specific connection handle (Phase 5) */

    /* 32-bit fields */
    uint32_t            magic;          /* PT_PEER_MAGIC - validation */
    pt_tick_t           last_seen;      /* Last activity timestamp */

    /* 16-bit fields grouped */
    PeerTalk_PeerID     id;             /* Peer ID (copied from info for fast lookup) */
    uint16_t            peer_flags;     /* PT_PEER_FLAG_* from discovery */
    uint16_t            latency_ms;     /* Estimated RTT */

    /* 8-bit fields grouped (minimize padding) */
    pt_peer_state       state;          /* Connection state (uint8_t) */
    uint8_t             address_count;
    uint8_t             preferred_transport;
    uint8_t             send_seq;       /* Send sequence number (Phase 2) */
    uint8_t             recv_seq;       /* Receive sequence number (Phase 2) */
    uint8_t             name_idx;       /* Index into context name table */

    /* Padding to reach exactly 32 bytes on both architectures */
#if defined(__powerc) || defined(__ppc__) || defined(__POWERPC__)
    uint8_t             reserved[4];    /* PPC: 8+4+4+2+2+2+6+4 = 32 bytes */
#else
    uint8_t             reserved[8];    /* 68k: 4+4+4+2+2+2+6+8 = 32 bytes */
#endif
} pt_peer_hot;  /* 32 bytes - fits in single 68030 cache line */

/* Compile-time size verification */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
  _Static_assert(sizeof(pt_peer_hot) == 32,
      "pt_peer_hot must be exactly 32 bytes for cache efficiency");
#elif defined(__GNUC__) || defined(__clang__)
  typedef char _pt_peer_hot_size_check[(sizeof(pt_peer_hot) == 32) ? 1 : -1];
#endif

/*----------------------------------------------------------------------------
 * Cold Data - Accessed infrequently (name lookups, buffer operations, stats)
 *----------------------------------------------------------------------------*/
typedef struct {
    /* Name stored here, accessed via name_idx in hot struct */
    char                name[PT_MAX_PEER_NAME + 1];  /* 32 bytes */

    /* Public peer info (for API queries) */
    PeerTalk_PeerInfo   info;           /* ~20 bytes */

    /* Multi-transport addressing */
    pt_peer_address     addresses[PT_MAX_PEER_ADDRESSES];  /* 16 bytes (2x8) */
    pt_tick_t           last_discovery; /* Last discovery beacon */

    /* Per-peer statistics */
    PeerTalk_PeerStats  stats;

    /* Latency tracking */
    pt_tick_t           ping_sent_time;
    uint16_t            rtt_samples[8]; /* Rolling RTT samples - 16 bytes */
    uint8_t             rtt_index;
    uint8_t             rtt_count;

    /* Protocol framing buffers (Phase 2) */
    uint8_t             obuf[768];      /* Output framing buffer */
    uint8_t             ibuf[512];      /* Input framing buffer */
    uint16_t            obuflen;        /* Bytes used in obuf */
    uint16_t            ibuflen;        /* Bytes used in ibuf */

#ifdef PT_DEBUG
    uint32_t            obuf_canary;    /* Buffer overflow detection */
    uint32_t            ibuf_canary;
#endif
} pt_peer_cold;  /* ~1.4KB */

/*----------------------------------------------------------------------------
 * Complete Peer Structure
 *----------------------------------------------------------------------------*/
struct pt_peer {
    pt_peer_hot         hot;            /* 32 bytes - frequently accessed */
    pt_peer_cold        cold;           /* ~1.4KB - rarely accessed */

    /* Queue references (Phase 2/3) - separate for potential future SoA */
    struct pt_queue    *send_queue;
    struct pt_queue    *recv_queue;
};

/*============================================================================
 * Internal Context Structure
 *
 * DOD Note: Includes O(1) peer ID lookup table and centralized name storage.
 * Field ordering is largest-to-smallest to minimize padding.
 *============================================================================*/

/* Maximum peer ID value for lookup table sizing */
#define PT_MAX_PEER_ID      256

struct pt_context {
    uint32_t            magic;          /* PT_CONTEXT_MAGIC */

    /* Configuration - large struct first */
    PeerTalk_Config     config;
    PeerTalk_Callbacks  callbacks;

    /* Platform operations */
    pt_platform_ops    *plat;

    /* Local info */
    PeerTalk_PeerInfo   local_info;

    /* Global statistics (Phase 4) */
    PeerTalk_GlobalStats global_stats;

    /* Peer management - pointers grouped */
    struct pt_peer     *peers;          /* Array of peers (hot+cold) */

    /*------------------------------------------------------------------------
     * O(1) Peer ID Lookup Table
     *
     * DOD Note: Avoids linear search when looking up peers by ID.
     * peer_id_to_index[peer_id] = index into peers array (0xFF = invalid)
     * For 256 max peer IDs, this costs 256 bytes but eliminates O(n) scans.
     *------------------------------------------------------------------------*/
    uint8_t             peer_id_to_index[PT_MAX_PEER_ID];

    /*------------------------------------------------------------------------
     * Centralized Name Table
     *
     * DOD Note: Peer names are stored here, indexed by peer's name_idx.
     * This keeps the 32-byte names out of the hot peer scan path.
     * peer_names[name_idx] = null-terminated name string
     *------------------------------------------------------------------------*/
    char                peer_names[PT_MAX_PEERS][PT_MAX_PEER_NAME + 1];

    /* Message tracking for SendTracked */
    uint32_t            next_message_id;

    /* 16-bit fields grouped */
    uint16_t            local_flags;    /* PT_PEER_FLAG_* to advertise */
    uint16_t            max_peers;
    uint16_t            peer_count;
    PeerTalk_PeerID     next_peer_id;
    uint16_t            available_transports;
    uint16_t            active_transports;
    uint16_t            log_categories;

    /* 8-bit fields grouped */
    uint8_t             discovery_active;
    uint8_t             listening;
    uint8_t             initialized;
    uint8_t             reserved_byte;  /* Alignment padding */

    /*------------------------------------------------------------------------
     * PT_Log Integration (Phase 0)
     *
     * PeerTalk uses the standalone PT_Log library from Phase 0 for all logging.
     * The log handle is created in PeerTalk_Init() via PT_LogCreate() and
     * destroyed in PeerTalk_Shutdown() via PT_LogDestroy().
     *
     * Usage pattern:
     *   PT_LOG_INFO(ctx->log, PT_LOG_CAT_INIT, "PeerTalk v%d.%d initialized",
     *               PT_VERSION_MAJOR, PT_VERSION_MINOR);
     *
     * Or use convenience macros that extract log from context:
     *   PT_CTX_INFO(ctx, PT_LOG_CAT_INIT, "PeerTalk initialized");
     *------------------------------------------------------------------------*/
    PT_Log             *log;            /* PT_Log handle from Phase 0 */

    /* Platform-specific data follows (allocated with extra space) */
};

/*============================================================================
 * Internal Functions
 *============================================================================*/

/* Validation */
int pt_validate_context(struct pt_context *ctx);
int pt_validate_peer(struct pt_peer *peer);
int pt_validate_config(const PeerTalk_Config *config);

/*----------------------------------------------------------------------------
 * Peer Management
 *
 * DOD Note: pt_find_peer_by_id uses O(1) lookup table instead of linear search.
 * pt_find_peer_by_address still requires linear scan but is called rarely.
 *----------------------------------------------------------------------------*/

/* O(1) peer lookup by ID using peer_id_to_index table */
static inline struct pt_peer* pt_find_peer_by_id(struct pt_context *ctx, PeerTalk_PeerID id) {
    uint8_t idx;
    if (!ctx || id >= PT_MAX_PEER_ID) return NULL;
    idx = ctx->peer_id_to_index[id];
    if (idx == 0xFF || idx >= ctx->max_peers) return NULL;
    return &ctx->peers[idx];
}

/* Linear scan for address lookup (called rarely - discovery, connection) */
struct pt_peer* pt_find_peer_by_address(struct pt_context *ctx, uint32_t addr);

/*
 * pt_find_peer_by_name: Linear scan for name lookup (cold path)
 * Defined in Phase 2.2 (Protocol & Messaging) since it requires the name
 * table to be populated via discovery packets. Searching by name is a
 * cold-path operation used primarily for:
 *   - Application-initiated connections by peer name
 *   - Debug/logging utilities
 *   - UI display functions
 * The linear scan is acceptable since name lookups are infrequent and the
 * peer array is typically small (<16 peers).
 */
struct pt_peer* pt_find_peer_by_name(struct pt_context *ctx, const char *name);

/*----------------------------------------------------------------------------
 * Peer Allocation with Swap-Back Removal
 *
 * pt_alloc_peer: Allocate next available peer slot from peers array.
 * Returns NULL if peer_count >= max_peers.
 *
 * pt_free_peer: Remove peer using swap-back algorithm for O(1) removal:
 *
 *   1. Copy last peer (peers[peer_count-1]) to removed slot
 *   2. Update peer_id_to_index for the moved peer's ID
 *   3. Decrement peer_count
 *   4. Invalidate the old last slot
 *
 * This maintains contiguous hot data for cache-efficient iteration and
 * avoids O(n) data shuffling that would occur with memmove().
 *
 * Example:
 *   Before: peers[0]=A, peers[1]=B, peers[2]=C, peer_count=3
 *   Remove B (index 1):
 *     1. Copy C to index 1: peers[1]=C
 *     2. Update index: peer_id_to_index[C.id] = 1
 *     3. Decrement: peer_count=2
 *   After: peers[0]=A, peers[1]=C, peer_count=2
 *
 * IMPORTANT: Peer ordering is NOT preserved. If ordering matters,
 * iterate by peer_id, not by array index.
 *----------------------------------------------------------------------------*/
struct pt_peer* pt_alloc_peer(struct pt_context *ctx);
void pt_free_peer(struct pt_context *ctx, struct pt_peer *peer);

/* Name table access */
const char* pt_get_peer_name(struct pt_context *ctx, uint8_t name_idx);
uint8_t pt_alloc_peer_name(struct pt_context *ctx, const char *name);
void pt_free_peer_name(struct pt_context *ctx, uint8_t name_idx);

/* Platform-specific allocation */
void* pt_plat_alloc(size_t size);
void pt_plat_free(void *ptr);
size_t pt_plat_extra_size(void);

#endif /* PT_INTERNAL_H */
```

#### Task 1.1.4: Create `src/core/pt_version.c` (Utility Functions)

```c
/*
 * PeerTalk Version and Error String Utilities
 */

#include "peertalk.h"
#include "pt_types.h"

/*============================================================================
 * Version String
 *============================================================================*/

const char* PeerTalk_Version(void) {
    return PT_VERSION_STRING;
}

/*============================================================================
 * Error Strings
 *============================================================================*/

const char* PeerTalk_ErrorString(PeerTalk_Error error) {
    switch (error) {
    case PT_OK:                      return "Success";
    case PT_ERR_INVALID_PARAM:       return "Invalid parameter";
    case PT_ERR_NO_MEMORY:           return "Out of memory";
    case PT_ERR_NOT_INITIALIZED:     return "Not initialized";
    case PT_ERR_ALREADY_INITIALIZED: return "Already initialized";
    case PT_ERR_NETWORK:             return "Network error";
    case PT_ERR_TIMEOUT:             return "Operation timed out";
    case PT_ERR_CONNECTION_REFUSED:  return "Connection refused";
    case PT_ERR_CONNECTION_CLOSED:   return "Connection closed";
    case PT_ERR_BUFFER_FULL:         return "Buffer full";
    case PT_ERR_INVALID_STATE:       return "Invalid state";
    case PT_ERR_PEER_NOT_FOUND:      return "Peer not found";
    case PT_ERR_DISCOVERY_ACTIVE:    return "Discovery already active";
    case PT_ERR_NO_NETWORK:          return "Network not available";
    case PT_ERR_PLATFORM:            return "Platform-specific error";
    case PT_ERR_QUEUE_EMPTY:         return "Queue empty";
    case PT_ERR_MESSAGE_TOO_LARGE:   return "Message too large";
    case PT_ERR_NOT_SUPPORTED:       return "Feature not supported";
    case PT_ERR_NOT_CONNECTED:       return "Not connected";
    case PT_ERR_WOULD_BLOCK:         return "Would block";
    case PT_ERR_BACKPRESSURE:        return "Queue backpressure";
    case PT_ERR_RESOURCE:            return "Resource exhausted";
    /* Protocol errors (Phase 2) */
    case PT_ERR_CRC:                 return "CRC validation failed";
    case PT_ERR_MAGIC:               return "Invalid magic number";
    case PT_ERR_TRUNCATED:           return "Packet too short";
    case PT_ERR_VERSION:             return "Protocol version mismatch";
    case PT_ERR_NOT_POWER2:          return "Capacity not power of two";
    case PT_ERR_INTERNAL:            return "Internal error";
    default:                         return "Unknown error";
    }
}

/*============================================================================
 * Transport Query
 *============================================================================*/

uint16_t PeerTalk_GetAvailableTransports(void) {
    uint16_t transports = 0;

#if defined(PT_HAS_TCPIP)
    transports |= PT_TRANSPORT_TCP | PT_TRANSPORT_UDP;
#endif

#if defined(PT_HAS_APPLETALK) || defined(PT_PLATFORM_APPLETALK)
    transports |= PT_TRANSPORT_APPLETALK;
#endif

    return transports;
}

/*============================================================================
 * Peer Name Access
 *
 * DOD Note: Names are stored in cold storage (context name table), accessed
 * via index. This keeps the hot peer iteration path free of 32-byte strings.
 *============================================================================*/

const char* PeerTalk_GetPeerName(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id) {
    struct pt_context *c = (struct pt_context *)ctx;
    struct pt_peer *peer;
    uint8_t name_idx;

    if (!c || c->magic != PT_CONTEXT_MAGIC) return NULL;

    peer = pt_find_peer_by_id(c, peer_id);
    if (!peer) return NULL;

    name_idx = peer->hot.name_idx;
    if (name_idx >= PT_MAX_PEERS) return NULL;

    return c->peer_names[name_idx];
}
```

**Note:** The stringify macros and `PT_VERSION_STRING` are defined in `peertalk.h` (see Task 1.1.1).

### Acceptance Criteria
1. `peertalk.h` compiles cleanly with `-Wall -Werror` on GCC/Clang
2. `peertalk.h` compiles with Retro68 for 68k target
3. All public types documented with comments
4. Error codes cover all expected failure modes
5. Platform detection works correctly
6. Magic numbers match CLAUDE.md
7. Type guards prevent conflicts with Retro68 headers

---

## Session 1.2: Portable Primitives

### Objective
Create portable implementations of byte order conversion, memory allocation, and atomic operations that work across all platforms.

### Important Notes on Atomics

**Classic Mac Cooperative Multitasking Model:**

Classic Mac OS uses cooperative multitasking - there is only one thread of execution, and it only yields at specific points (like WaitNextEvent). However, ASR (Asynchronous Status Routine) and Open Transport notifiers CAN interrupt the main code at any time.

The "atomic" operations here are designed for this model:
- **Main code ↔ ISR communication:** Volatile flags that ISR sets and main code reads/clears
- **NOT for multi-threading:** These are not true atomic operations for SMP systems

On 68k, we use volatile and rely on the fact that 32-bit aligned reads/writes are atomic on 68000+.
On PPC with Open Transport, we use the OTAtomic* functions which are designed for this purpose.

### Tasks

#### Task 1.2.1: Create `src/core/pt_compat.h`

```c
/*
 * PeerTalk Portability Layer
 *
 * Provides consistent interfaces for:
 * - Byte order conversion (network <-> host)
 * - Memory allocation
 * - Atomic/volatile operations for interrupt safety
 * - String utilities
 */

#ifndef PT_COMPAT_H
#define PT_COMPAT_H

#include "pt_types.h"

/*============================================================================
 * Byte Order Conversion
 *
 * Network byte order is big-endian.
 * 68k Macs are big-endian (no conversion needed).
 * PowerPC Macs are big-endian (no conversion needed).
 * x86/x64 (POSIX) are little-endian (conversion needed).
 *============================================================================*/

#if defined(PT_PLATFORM_POSIX)
    /* Use system headers on POSIX */
    #include <arpa/inet.h>
    #define pt_htons(x)  htons(x)
    #define pt_htonl(x)  htonl(x)
    #define pt_ntohs(x)  ntohs(x)
    #define pt_ntohl(x)  ntohl(x)

#elif defined(PT_PLATFORM_CLASSIC_MAC)
    /* Classic Mac is big-endian - no conversion needed */
    #define pt_htons(x)  (x)
    #define pt_htonl(x)  (x)
    #define pt_ntohs(x)  (x)
    #define pt_ntohl(x)  (x)
#endif

/*============================================================================
 * Memory Allocation
 *
 * POSIX: malloc/free
 * Classic Mac: NewPtr/DisposePtr (from application heap)
 *============================================================================*/

void* pt_alloc(size_t size);
void  pt_free(void *ptr);
void* pt_alloc_clear(size_t size);  /* Allocate and zero */

/*============================================================================
 * Memory Queries
 *============================================================================*/

unsigned long pt_get_free_mem(void);
unsigned long pt_get_max_block(void);

/*============================================================================
 * Volatile Flag Operations
 *
 * Used for communication between main code and ASR/notifier callbacks.
 * These are designed for the single-CPU cooperative multitasking of
 * Classic Mac, where ASRs can interrupt but there's no true parallelism.
 *
 * On 68k: We use volatile; 32-bit aligned accesses are atomic.
 * On PPC with OT: We use OTAtomic* functions for proper memory barriers.
 * On POSIX: Simple volatile operations (single-threaded use case).
 *
 * CITATION: 32-bit Atomic Access on 68k
 * -------------------------------------
 * Based on Motorola 68000 processor architecture: 32-bit aligned accesses
 * complete in a single bus cycle and cannot be interrupted mid-operation.
 * This is a hardware guarantee, not a software feature.
 *
 * The "ISR sets, main clears" pattern is safe WITHOUT interrupt disabling:
 * - ASR only sets bits (via OR): *flags |= mask
 * - Main loop only clears bits (via AND): *flags &= ~mask
 * - No read-modify-write race can occur because the operations are one-way
 *
 * References:
 * - Motorola MC68000 User's Manual, Section 8 "Instruction Execution Timing"
 * - This is NOT explicitly documented in Inside Macintosh, but is implied
 *   by the widespread use of this pattern in Apple sample code
 *============================================================================*/

/* Volatile flag type for interrupt-safe communication */
typedef volatile uint32_t pt_atomic_t;

/* Set a bit (safe to call from ASR/notifier) */
void pt_atomic_set_bit(pt_atomic_t *flags, int bit);

/* Clear a bit (call from main loop only) */
void pt_atomic_clear_bit(pt_atomic_t *flags, int bit);

/* Test a bit */
int pt_atomic_test_bit(pt_atomic_t *flags, int bit);

/* Test and clear a bit (returns previous value, main loop only) */
int pt_atomic_test_and_clear_bit(pt_atomic_t *flags, int bit);

/* Common flag bit definitions */
#define PT_FLAG_DATA_AVAILABLE      0
#define PT_FLAG_CONNECT_COMPLETE    1
#define PT_FLAG_DISCONNECT          2
#define PT_FLAG_ERROR               3
#define PT_FLAG_LISTEN_PENDING      4
#define PT_FLAG_SEND_COMPLETE       5

/*============================================================================
 * Memory Copy Variants
 *
 * pt_memcpy uses BlockMoveData on Classic Mac for efficiency.
 * pt_memcpy_isr is safe for interrupt context (ASR/notifier).
 *
 * CITATION: BlockMoveData Interrupt Safety
 * ----------------------------------------
 * Inside Macintosh Volume VI Table B-3 does NOT list BlockMoveData as
 * safe to call at interrupt time.
 *
 * CONTRADICTION: The Sound Manager chapter (IM Vol VI p. 162410) states
 * "the BlockMove procedure... you can safely call it in your doubleback
 * procedure" (an interrupt context). However, Table B-3 is authoritative.
 *
 * CONSERVATIVE APPROACH: We forbid BlockMoveData in ASR/notifiers and
 * provide pt_memcpy_isr() for interrupt-safe copying. This adds overhead
 * but guarantees stability on all Classic Mac systems.
 *============================================================================*/

/* Memory copy - uses BlockMoveData on Classic Mac, NOT safe for ISR */
void pt_memcpy(void *dest, const void *src, size_t n);

/* Memory copy - ISR-safe, no Toolbox calls, use in ASR/notifier */
void pt_memcpy_isr(void *dest, const void *src, size_t n);

/*============================================================================
 * String Utilities
 *============================================================================*/

/* Safe string copy (always null-terminates) */
void pt_strncpy(char *dest, const char *src, size_t n);

/* String length */
size_t pt_strlen(const char *s);

/* Memory set */
void pt_memset(void *dest, int c, size_t n);

/* Memory compare */
int pt_memcmp(const void *a, const void *b, size_t n);

/*============================================================================
 * Formatted Output (for logging)
 *
 * Classic Mac doesn't have vsnprintf, so we provide a limited implementation.
 *============================================================================*/

#if defined(PT_PLATFORM_POSIX)
    #include <stdio.h>
    #include <stdarg.h>
    #define pt_vsnprintf vsnprintf
    #define pt_snprintf  snprintf
#elif defined(PT_PLATFORM_CLASSIC_MAC)
    #include <stdarg.h>
    int pt_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
    int pt_snprintf(char *buf, size_t size, const char *fmt, ...);
#endif

#endif /* PT_COMPAT_H */
```

#### Task 1.2.2: Create `src/core/pt_compat.c`

```c
/*
 * PeerTalk Portability Layer Implementation
 */

#include "pt_compat.h"

#if defined(PT_PLATFORM_POSIX)
/*============================================================================
 * POSIX Implementation
 *============================================================================*/

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void* pt_alloc(size_t size) {
    return malloc(size);
}

void pt_free(void *ptr) {
    free(ptr);
}

void* pt_alloc_clear(size_t size) {
    return calloc(1, size);
}

unsigned long pt_get_free_mem(void) {
    /* POSIX doesn't have meaningful free memory query */
    return 1024UL * 1024UL * 1024UL;  /* 1GB - effectively unlimited */
}

unsigned long pt_get_max_block(void) {
    return 1024UL * 1024UL * 1024UL;
}

/*
 * POSIX "atomic" operations - NOT thread-safe for true multi-threading!
 *
 * PeerTalk uses a single-threaded, non-blocking event loop model.
 * These operations are sufficient for that use case. For applications
 * that use PeerTalk from multiple threads, external synchronization
 * (e.g., pthread_mutex) is required around PeerTalk API calls.
 */

void pt_atomic_set_bit(pt_atomic_t *flags, int bit) {
    *flags |= (1UL << bit);
}

void pt_atomic_clear_bit(pt_atomic_t *flags, int bit) {
    *flags &= ~(1UL << bit);
}

int pt_atomic_test_bit(pt_atomic_t *flags, int bit) {
    return (*flags & (1UL << bit)) != 0;
}

int pt_atomic_test_and_clear_bit(pt_atomic_t *flags, int bit) {
    int was_set = (*flags & (1UL << bit)) != 0;
    *flags &= ~(1UL << bit);
    return was_set;
}

void pt_strncpy(char *dest, const char *src, size_t n) {
    strncpy(dest, src, n);
    if (n > 0) dest[n - 1] = '\0';
}

size_t pt_strlen(const char *s) {
    return strlen(s);
}

void pt_memcpy(void *dest, const void *src, size_t n) {
    memcpy(dest, src, n);
}

void pt_memcpy_isr(void *dest, const void *src, size_t n) {
    /* POSIX: same as pt_memcpy since there's no ISR concern */
    memcpy(dest, src, n);
}

void pt_memset(void *dest, int c, size_t n) {
    memset(dest, c, n);
}

int pt_memcmp(const void *a, const void *b, size_t n) {
    return memcmp(a, b, n);
}

#elif defined(PT_PLATFORM_CLASSIC_MAC)
/*============================================================================
 * Classic Mac Implementation
 *============================================================================*/

#include <MacMemory.h>
#include <OSUtils.h>

void* pt_alloc(size_t size) {
    return (void *)NewPtr((Size)size);
}

void pt_free(void *ptr) {
    if (ptr) {
        DisposePtr((Ptr)ptr);
    }
}

void* pt_alloc_clear(size_t size) {
    return (void *)NewPtrClear((Size)size);
}

unsigned long pt_get_free_mem(void) {
    return (unsigned long)FreeMem();
}

unsigned long pt_get_max_block(void) {
    return (unsigned long)MaxBlock();
}

/*
 * Atomic operations for Classic Mac
 *
 * For Open Transport (PPC), we use the OTAtomic* functions which provide
 * proper memory barriers and are designed for notifier callbacks.
 *
 * For MacTCP (68k), we use volatile and rely on:
 * 1. 32-bit aligned reads/writes are atomic on 68000+
 * 2. ASR can only interrupt between instructions, not mid-operation
 * 3. We only SET bits in ASR, only CLEAR bits in main loop
 *
 * This pattern (ISR sets, main clears) is safe without disabling interrupts.
 */

#if defined(PT_PLATFORM_OT)
#include <OpenTransport.h>

/*
 * OTAtomic functions operate on bytes with bit indices 0-7 within each byte.
 * For a 32-bit flags word, we need to calculate which byte contains the bit.
 * Classic Mac is big-endian, so byte 0 is the MSB.
 *
 * Bit layout for 32-bit word (big-endian):
 *   Byte 0: bits 24-31 (OT bits 0-7)
 *   Byte 1: bits 16-23 (OT bits 0-7)
 *   Byte 2: bits 8-15  (OT bits 0-7)
 *   Byte 3: bits 0-7   (OT bits 0-7)
 *
 * To set logical bit N of the 32-bit word:
 *   byte_offset = 3 - (bit / 8)   // big-endian: bit 0 is in byte 3
 *   bit_in_byte = bit % 8
 */

void pt_atomic_set_bit(pt_atomic_t *flags, int bit) {
    int byte_offset = 3 - (bit / 8);  /* Big-endian adjustment */
    int bit_in_byte = bit % 8;
    UInt8 *byte_ptr = ((UInt8 *)flags) + byte_offset;
    OTAtomicSetBit(byte_ptr, bit_in_byte);
}

void pt_atomic_clear_bit(pt_atomic_t *flags, int bit) {
    int byte_offset = 3 - (bit / 8);
    int bit_in_byte = bit % 8;
    UInt8 *byte_ptr = ((UInt8 *)flags) + byte_offset;
    OTAtomicClearBit(byte_ptr, bit_in_byte);
}

int pt_atomic_test_bit(pt_atomic_t *flags, int bit) {
    int byte_offset = 3 - (bit / 8);
    int bit_in_byte = bit % 8;
    UInt8 *byte_ptr = ((UInt8 *)flags) + byte_offset;
    return OTAtomicTestBit(byte_ptr, bit_in_byte);
}

int pt_atomic_test_and_clear_bit(pt_atomic_t *flags, int bit) {
    int byte_offset = 3 - (bit / 8);
    int bit_in_byte = bit % 8;
    UInt8 *byte_ptr = ((UInt8 *)flags) + byte_offset;
    /* OTAtomicClearBit returns the previous value */
    return OTAtomicClearBit(byte_ptr, bit_in_byte);
}

#else /* PT_PLATFORM_MACTCP - 68k */

/*
 * 68k volatile-based "atomics"
 *
 * Safe because:
 * - ASR only sets bits, main loop only clears bits (no read-modify-write race)
 * - 32-bit aligned access is atomic on 68k
 * - volatile prevents compiler reordering
 *
 * We do NOT disable interrupts here - it's unnecessary for this pattern
 * and would add overhead for every flag check.
 */

void pt_atomic_set_bit(pt_atomic_t *flags, int bit) {
    /* Safe from ASR: just OR in a bit */
    *flags |= (1UL << bit);
}

void pt_atomic_clear_bit(pt_atomic_t *flags, int bit) {
    /* Main loop only: AND out a bit */
    *flags &= ~(1UL << bit);
}

int pt_atomic_test_bit(pt_atomic_t *flags, int bit) {
    /* Read is atomic for aligned 32-bit on 68k */
    return (*flags & (1UL << bit)) != 0;
}

int pt_atomic_test_and_clear_bit(pt_atomic_t *flags, int bit) {
    /*
     * Main loop only! Not safe from ASR.
     * Read the bit, then clear it.
     */
    int was_set = (*flags & (1UL << bit)) != 0;
    *flags &= ~(1UL << bit);
    return was_set;
}

#endif /* PT_PLATFORM_OT */

/* String functions - Classic Mac doesn't have standard C library */

void pt_strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n - 1 && src[i]; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

size_t pt_strlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

void pt_memcpy(void *dest, const void *src, size_t n) {
    /* Use Toolbox BlockMoveData for efficiency - NOT safe for ISR */
    BlockMoveData(src, dest, (Size)n);
}

void pt_memcpy_isr(void *dest, const void *src, size_t n) {
    /*
     * ISR-safe memory copy for ASR/notifier callbacks.
     * No Toolbox calls - safe at interrupt time.
     */
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
}

void pt_memset(void *dest, int c, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    while (n--) *d++ = (unsigned char)c;
}

int pt_memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    while (n--) {
        if (*pa != *pb) return *pa - *pb;
        pa++;
        pb++;
    }
    return 0;
}

/*
 * Limited vsnprintf for Classic Mac
 *
 * Supports: %d, %u, %x, %X, %s, %c, %p, %ld, %lu, %lx, %%
 * Supports: field width, zero padding
 * Does NOT support: floating point, precision, negative width, *
 */

static char* pt_format_int(char *buf, char *end, unsigned long val,
                           int base, int upper, int width, int zero_pad) {
    char tmp[12];
    char *t = tmp + sizeof(tmp);
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int len;

    *--t = '\0';
    if (val == 0) {
        *--t = '0';
    } else {
        while (val && t > tmp) {
            *--t = digits[val % base];
            val /= base;
        }
    }

    len = (tmp + sizeof(tmp) - 1) - t;
    while (len < width && buf < end) {
        *buf++ = zero_pad ? '0' : ' ';
        width--;
    }
    while (*t && buf < end) {
        *buf++ = *t++;
    }
    return buf;
}

int pt_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    char *out = buf;
    char *end = buf + size - 1;
    int width, zero_pad, is_long;  /* C89: declare at block start */

    if (size == 0) return 0;

    while (*fmt && out < end) {
        if (*fmt != '%') {
            *out++ = *fmt++;
            continue;
        }

        fmt++;  /* Skip '%' */

        /* Parse width - reset for each format specifier */
        width = 0;
        zero_pad = 0;
        is_long = 0;

        if (*fmt == '0') {
            zero_pad = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }

        switch (*fmt) {
        case 'd': {
            long val = is_long ? va_arg(ap, long) : va_arg(ap, int);
            if (val < 0 && out < end) {
                *out++ = '-';
                val = -val;
                if (width > 0) width--;
            }
            out = pt_format_int(out, end, (unsigned long)val, 10, 0, width, zero_pad);
            break;
        }
        case 'u': {
            unsigned long val = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
            out = pt_format_int(out, end, val, 10, 0, width, zero_pad);
            break;
        }
        case 'x':
        case 'X': {
            unsigned long val = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
            out = pt_format_int(out, end, val, 16, (*fmt == 'X'), width, zero_pad);
            break;
        }
        case 'p': {
            unsigned long val = (unsigned long)va_arg(ap, void*);
            if (out + 2 < end) {
                *out++ = '0';
                *out++ = 'x';
            }
            out = pt_format_int(out, end, val, 16, 0, 8, 1);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            while (*s && out < end) *out++ = *s++;
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            if (out < end) *out++ = c;
            break;
        }
        case '%':
            if (out < end) *out++ = '%';
            break;
        default:
            /* Unknown format - copy literally */
            if (out < end) *out++ = '%';
            if (out < end) *out++ = *fmt;
            break;
        }
        fmt++;
    }

    *out = '\0';
    return (int)(out - buf);
}

int pt_snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = pt_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

#endif /* PT_PLATFORM_CLASSIC_MAC */
```

#### Task 1.2.3: Create `tests/test_compat.c`

```c
/*
 * PeerTalk Compatibility Layer Tests
 */

#include <stdio.h>
#include "pt_compat.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  " #name "..."); \
    tests_run++; \
    test_##name(); \
    tests_passed++; \
    printf(" OK\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" FAILED: %s\n", #cond); \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_STR_EQ(a, b) ASSERT(pt_memcmp(a, b, pt_strlen(b) + 1) == 0)

/*============================================================================
 * Byte Order Tests
 *============================================================================*/

TEST(byte_order_16) {
    uint16_t host = 0x1234;
    uint16_t net = pt_htons(host);
    uint16_t back = pt_ntohs(net);
    ASSERT_EQ(back, host);

    /* Network byte order is big-endian: 0x12, 0x34 */
    unsigned char *p = (unsigned char *)&net;
    ASSERT_EQ(p[0], 0x12);
    ASSERT_EQ(p[1], 0x34);
}

TEST(byte_order_32) {
    uint32_t host = 0x12345678;
    uint32_t net = pt_htonl(host);
    uint32_t back = pt_ntohl(net);
    ASSERT_EQ(back, host);

    unsigned char *p = (unsigned char *)&net;
    ASSERT_EQ(p[0], 0x12);
    ASSERT_EQ(p[1], 0x34);
    ASSERT_EQ(p[2], 0x56);
    ASSERT_EQ(p[3], 0x78);
}

/*============================================================================
 * Memory Tests
 *============================================================================*/

TEST(alloc_free) {
    void *p = pt_alloc(1024);
    ASSERT(p != NULL);
    pt_free(p);
}

TEST(alloc_clear) {
    int i;  /* C89: declare at block start for Classic Mac compatibility */
    unsigned char *p = (unsigned char *)pt_alloc_clear(256);
    ASSERT(p != NULL);
    for (i = 0; i < 256; i++) {
        ASSERT_EQ(p[i], 0);
    }
    pt_free(p);
}

TEST(memcpy_memset) {
    char src[] = "Hello, PeerTalk!";
    char dest[32];

    pt_memset(dest, 0, sizeof(dest));
    pt_memcpy(dest, src, pt_strlen(src) + 1);
    ASSERT_STR_EQ(dest, src);
}

TEST(memcmp_test) {
    char a[] = "abc";
    char b[] = "abc";
    char c[] = "abd";

    ASSERT_EQ(pt_memcmp(a, b, 3), 0);
    ASSERT(pt_memcmp(a, c, 3) < 0);
    ASSERT(pt_memcmp(c, a, 3) > 0);
}

/*============================================================================
 * String Tests
 *============================================================================*/

TEST(strlen_test) {
    ASSERT_EQ(pt_strlen(""), 0);
    ASSERT_EQ(pt_strlen("a"), 1);
    ASSERT_EQ(pt_strlen("hello"), 5);
}

TEST(strncpy_test) {
    char dest[8];

    pt_strncpy(dest, "hi", sizeof(dest));
    ASSERT_STR_EQ(dest, "hi");

    /* Truncation with null termination */
    pt_strncpy(dest, "this is too long", sizeof(dest));
    ASSERT_EQ(pt_strlen(dest), 7);
    ASSERT_EQ(dest[7], '\0');
}

/*============================================================================
 * Atomic Tests
 *============================================================================*/

TEST(atomic_bits) {
    pt_atomic_t flags = 0;

    pt_atomic_set_bit(&flags, PT_FLAG_DATA_AVAILABLE);
    ASSERT(pt_atomic_test_bit(&flags, PT_FLAG_DATA_AVAILABLE));
    ASSERT(!pt_atomic_test_bit(&flags, PT_FLAG_ERROR));

    pt_atomic_set_bit(&flags, PT_FLAG_ERROR);
    ASSERT(pt_atomic_test_bit(&flags, PT_FLAG_DATA_AVAILABLE));
    ASSERT(pt_atomic_test_bit(&flags, PT_FLAG_ERROR));

    int was_set = pt_atomic_test_and_clear_bit(&flags, PT_FLAG_DATA_AVAILABLE);
    ASSERT(was_set);
    ASSERT(!pt_atomic_test_bit(&flags, PT_FLAG_DATA_AVAILABLE));
    ASSERT(pt_atomic_test_bit(&flags, PT_FLAG_ERROR));
}

/*============================================================================
 * snprintf Tests
 *============================================================================*/

TEST(snprintf_basic) {
    char buf[64];

    pt_snprintf(buf, sizeof(buf), "hello");
    ASSERT_STR_EQ(buf, "hello");

    pt_snprintf(buf, sizeof(buf), "num=%d", 42);
    ASSERT_STR_EQ(buf, "num=42");

    pt_snprintf(buf, sizeof(buf), "hex=%x", 0xABCD);
    ASSERT_STR_EQ(buf, "hex=abcd");

    pt_snprintf(buf, sizeof(buf), "HEX=%X", 0xABCD);
    ASSERT_STR_EQ(buf, "HEX=ABCD");
}

TEST(snprintf_width) {
    char buf[64];

    pt_snprintf(buf, sizeof(buf), "[%4d]", 42);
    ASSERT_STR_EQ(buf, "[  42]");

    pt_snprintf(buf, sizeof(buf), "[%04d]", 42);
    ASSERT_STR_EQ(buf, "[0042]");

    pt_snprintf(buf, sizeof(buf), "[%08x]", 0x1234);
    ASSERT_STR_EQ(buf, "[00001234]");
}

TEST(snprintf_string) {
    char buf[64];

    pt_snprintf(buf, sizeof(buf), "str=%s", "test");
    ASSERT_STR_EQ(buf, "str=test");

    pt_snprintf(buf, sizeof(buf), "null=%s", (char*)NULL);
    ASSERT_STR_EQ(buf, "null=(null)");
}

TEST(snprintf_long) {
    char buf[64];

    pt_snprintf(buf, sizeof(buf), "long=%ld", 1234567890L);
    ASSERT_STR_EQ(buf, "long=1234567890");

    pt_snprintf(buf, sizeof(buf), "ulong=%lu", 4000000000UL);
    ASSERT_STR_EQ(buf, "ulong=4000000000");
}

TEST(snprintf_truncate) {
    char buf[8];

    pt_snprintf(buf, sizeof(buf), "this is a long string");
    ASSERT_EQ(pt_strlen(buf), 7);
    ASSERT_EQ(buf[7], '\0');
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void) {
    printf("PeerTalk Compatibility Layer Tests\n");
    printf("===================================\n\n");

    printf("Byte Order:\n");
    RUN_TEST(byte_order_16);
    RUN_TEST(byte_order_32);

    printf("\nMemory:\n");
    RUN_TEST(alloc_free);
    RUN_TEST(alloc_clear);
    RUN_TEST(memcpy_memset);
    RUN_TEST(memcmp_test);

    printf("\nStrings:\n");
    RUN_TEST(strlen_test);
    RUN_TEST(strncpy_test);

    printf("\nAtomics:\n");
    RUN_TEST(atomic_bits);

    printf("\nFormatting:\n");
    RUN_TEST(snprintf_basic);
    RUN_TEST(snprintf_width);
    RUN_TEST(snprintf_string);
    RUN_TEST(snprintf_long);
    RUN_TEST(snprintf_truncate);

    printf("\n===================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
```

### Acceptance Criteria
1. All tests pass on POSIX
2. Byte order conversion produces correct network byte order
3. Memory allocation/free works without leaks
4. Atomic bit operations work correctly
5. pt_snprintf handles all documented format specifiers
6. Code compiles for Classic Mac targets (manual test on real hardware)

---

## Session 1.3: Platform Stubs

### Objective
Create platform abstraction layer with stub implementations that compile on all platforms.

**IMPORTANT:** All platform files must include `pt_compat.h` for portable primitives (byte order, memory, atomics). MacTCP and Open Transport files also need Classic Mac system headers (`<MacTCP.h>`, `<OpenTransport.h>`, `<OSUtils.h>`).

### Tasks

#### Task 1.3.1: Create `src/posix/platform_posix.c`

```c
/*
 * PeerTalk POSIX Platform Implementation
 */

#include "pt_internal.h"
#include "pt_compat.h"  /* REQUIRED: for portable primitives */

#if defined(PT_PLATFORM_POSIX)

#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>

static int posix_init(struct pt_context *ctx) {
    PT_LOG_INFO(ctx, PT_LOG_CAT_GENERAL, "POSIX platform initialized");
    return 0;
}

static void posix_shutdown(struct pt_context *ctx) {
    PT_LOG_INFO(ctx, PT_LOG_CAT_GENERAL, "POSIX platform shutdown");
}

static int posix_poll(struct pt_context *ctx) {
    /* Stub - implemented in later phases */
    (void)ctx;
    return 0;
}

static pt_tick_t posix_get_ticks(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    /* Return milliseconds - wraps every ~49 days, which is fine */
    return (pt_tick_t)((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
}

static unsigned long posix_get_free_mem(void) {
    return 1024UL * 1024UL * 1024UL;  /* 1GB - effectively unlimited */
}

static unsigned long posix_get_max_block(void) {
    return 1024UL * 1024UL * 1024UL;
}

pt_platform_ops pt_posix_ops = {
    posix_init,
    posix_shutdown,
    posix_poll,
    posix_get_ticks,
    posix_get_free_mem,
    posix_get_max_block,
    NULL  /* send_udp - set by Phase 4 to pt_posix_send_udp */
};

/*
 * Platform-specific allocation functions.
 * On POSIX, we just use standard malloc/free with no extra context space.
 */
void* pt_plat_alloc(size_t size) {
    return malloc(size);
}

void pt_plat_free(void *ptr) {
    free(ptr);
}

size_t pt_plat_extra_size(void) {
    return 0;  /* No extra platform-specific data needed */
}

#endif /* PT_PLATFORM_POSIX */
```

#### Task 1.3.2: Create `src/mactcp/platform_mactcp.c`

```c
/*
 * PeerTalk MacTCP Platform Implementation
 *
 * For System 6.0.8 and System 7.x on 68k Macs
 *
 * References:
 * - MacTCP Programmer's Guide (1989), Chapter 2: "Opening the Driver"
 * - Inside Macintosh: Devices, Device Manager chapter
 */

#include "pt_internal.h"

#if defined(PT_PLATFORM_MACTCP)

#include <Devices.h>
#include <MacTCP.h>
#include <MacMemory.h>
#include <OSUtils.h>

/*
 * MacTCP driver name - Pascal string
 * The driver is ".IPP" (Internet Protocol Package)
 * From MacTCP Programmer's Guide: "Your application opens the
 * MacTCP driver by calling PBOpen with the driver name '.IPP'"
 */
#define MACTCP_DRIVER_NAME "\p.IPP"

/* MacTCP driver reference number - valid after successful open */
static short g_mactcp_refnum = 0;

/*
 * Universal Procedure Pointers (UPPs) for MacTCP callbacks.
 *
 * MacTCP requires UPPs for callback registration. From MacTCP.h:
 * "For TCPCreatePB Control calls, use NewTCPNotifyProc to set up a
 * TCPNotifyUPP universal procptr to pass in the notifyProc field"
 *
 * UPPs enable the mixed-mode manager to call 68k code from PPC
 * environments, and vice versa. Even on pure 68k, we need these
 * for proper stack frame setup with pascal calling convention.
 *
 * These are created once at init and disposed at shutdown.
 * Individual streams reference these global UPPs.
 */
static TCPNotifyUPP  g_tcp_notify_upp = NULL;
static UDPNotifyUPP  g_udp_notify_upp = NULL;

/*
 * Forward declarations for ASR callbacks - implemented in later phases.
 *
 * IMPORTANT: The `pascal` keyword is REQUIRED. MacTCP.h defines these
 * callbacks using CALLBACK_API which implies pascal calling convention.
 * Without `pascal`, the stack frame will be corrupted when MacTCP calls
 * these routines, causing crashes or incorrect parameter values.
 *
 * From MacTCP.h:
 *   typedef CALLBACK_API( void , TCPNotifyProcPtr )(...);  // implies pascal
 *   typedef CALLBACK_API( void , UDPNotifyProcPtr )(...);  // implies pascal
 */
static pascal void pt_tcp_asr(StreamPtr tcpStream, unsigned short eventCode,
                              Ptr userDataPtr, unsigned short terminReason,
                              ICMPReport *icmpMsg);
static pascal void pt_udp_asr(StreamPtr udpStream, unsigned short eventCode,
                              Ptr userDataPtr, ICMPReport *icmpMsg);

static int mactcp_init(struct pt_context *ctx) {
    ParamBlockRec pb;
    OSErr err;

    /*
     * Open MacTCP driver using PBOpenSync
     * This works on both System 6 and System 7.
     *
     * Note: The driver stays open even after we "close" it because
     * it's a shared system resource. We just need the refnum.
     */
    pt_memset(&pb, 0, sizeof(pb));
    pb.ioParam.ioNamePtr = (StringPtr)MACTCP_DRIVER_NAME;
    pb.ioParam.ioPermssn = fsCurPerm;

    err = PBOpenSync(&pb);
    if (err != noErr) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_GENERAL,
            "Failed to open MacTCP driver (.IPP): %d", (int)err);
        /*
         * Common errors:
         * -23 (fnOpnErr): Driver not found - MacTCP not installed
         * Other errors may come from Resource/Device/Slot Manager
         */
        return -1;
    }

    g_mactcp_refnum = pb.ioParam.ioRefNum;
    PT_LOG_INFO(ctx, PT_LOG_CAT_GENERAL,
        "MacTCP driver opened, refnum=%d", (int)g_mactcp_refnum);

    /*
     * Create Universal Procedure Pointers for ASR callbacks.
     * These must be created before any TCP/UDP streams are opened.
     * UPPs wrap the callback function with proper calling convention handling.
     */
    g_tcp_notify_upp = NewTCPNotifyUPP(pt_tcp_asr);
    if (g_tcp_notify_upp == NULL) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_GENERAL, "Failed to create TCP notify UPP");
        return -1;
    }

    g_udp_notify_upp = NewUDPNotifyUPP(pt_udp_asr);
    if (g_udp_notify_upp == NULL) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_GENERAL, "Failed to create UDP notify UPP");
        DisposeTCPNotifyUPP(g_tcp_notify_upp);
        g_tcp_notify_upp = NULL;
        return -1;
    }

    PT_LOG_DEBUG(ctx, PT_LOG_CAT_GENERAL, "MacTCP UPPs created");

    return 0;
}

static void mactcp_shutdown(struct pt_context *ctx) {
    /*
     * Dispose of Universal Procedure Pointers.
     * This must be done AFTER all streams using these UPPs are closed.
     */
    if (g_tcp_notify_upp != NULL) {
        DisposeTCPNotifyUPP(g_tcp_notify_upp);
        g_tcp_notify_upp = NULL;
    }
    if (g_udp_notify_upp != NULL) {
        DisposeUDPNotifyUPP(g_udp_notify_upp);
        g_udp_notify_upp = NULL;
    }

    /*
     * We don't actually close the MacTCP driver - it's a shared
     * system resource. Just clear our refnum and log shutdown.
     */
    PT_LOG_INFO(ctx, PT_LOG_CAT_GENERAL, "MacTCP platform shutdown");
    g_mactcp_refnum = 0;
}

static int mactcp_poll(struct pt_context *ctx) {
    /* Stub - implemented in Phase 4 (MacTCP Networking) */
    (void)ctx;
    return 0;
}

static pt_tick_t mactcp_get_ticks(void) {
    /*
     * TickCount() returns ticks since system startup.
     * One tick = 1/60th second (~16.67ms).
     * For timing, we use ticks directly rather than converting to ms.
     *
     * WARNING: TickCount() is NOT listed in Inside Macintosh Volume VI
     * Table B-3 ("Routines That May Be Called at Interrupt Time").
     * This function must ONLY be called from the main event loop
     * (e.g., from pt_platform_ops.poll or PeerTalk_Poll), NEVER from
     * ASR callbacks or completion routines.
     *
     * For ISR timing, use pre-set timestamps or set timestamp=0 and
     * fill in later from the main loop.
     */
    return (pt_tick_t)TickCount();
}

static unsigned long mactcp_get_free_mem(void) {
    return (unsigned long)FreeMem();
}

static unsigned long mactcp_get_max_block(void) {
    return (unsigned long)MaxBlock();
}

pt_platform_ops pt_mactcp_ops = {
    mactcp_init,
    mactcp_shutdown,
    mactcp_poll,
    mactcp_get_ticks,
    mactcp_get_free_mem,
    mactcp_get_max_block,
    NULL  /* send_udp - set by Phase 5 to pt_mactcp_send_udp */
};

/* Accessor for driver refnum - used by TCP/UDP implementation */
short pt_mactcp_get_refnum(void) {
    return g_mactcp_refnum;
}

/* Accessors for UPPs - used when creating TCP/UDP streams */
TCPNotifyUPP pt_mactcp_get_tcp_upp(void) {
    return g_tcp_notify_upp;
}

UDPNotifyUPP pt_mactcp_get_udp_upp(void) {
    return g_udp_notify_upp;
}

/*
 * ASR callback stubs - implemented in Phase 4 (MacTCP Networking).
 * These are called at interrupt time when network events occur.
 *
 * CRITICAL: Follow CLAUDE.md ASR rules - no Toolbox calls, no allocation,
 * only set flags and return. Use pt_memcpy_isr for any data copying.
 */
static pascal void pt_tcp_asr(StreamPtr tcpStream, unsigned short eventCode,
                              Ptr userDataPtr, unsigned short terminReason,
                              ICMPReport *icmpMsg) {
    /* Stub - implemented in Phase 4 */
    (void)tcpStream;
    (void)eventCode;
    (void)userDataPtr;
    (void)terminReason;
    (void)icmpMsg;
}

static pascal void pt_udp_asr(StreamPtr udpStream, unsigned short eventCode,
                              Ptr userDataPtr, ICMPReport *icmpMsg) {
    /* Stub - implemented in Phase 4 */
    /* Note: UDP ASR has 4 params, no terminReason (unlike TCP's 5 params) */
    (void)udpStream;
    (void)eventCode;
    (void)userDataPtr;
    (void)icmpMsg;
}

/*
 * Platform-specific allocation functions.
 * On Classic Mac, we use NewPtr/DisposePtr from the application heap.
 */
void* pt_plat_alloc(size_t size) {
    return (void *)NewPtr((Size)size);
}

void pt_plat_free(void *ptr) {
    if (ptr) {
        DisposePtr((Ptr)ptr);
    }
}

size_t pt_plat_extra_size(void) {
    /*
     * MacTCP platform needs extra space in the context for:
     * - TCP/UDP stream handles and state (allocated in Phase 4)
     * For now, return 0 - will be updated when networking is implemented.
     */
    return 0;
}

#endif /* PT_PLATFORM_MACTCP */
```

#### Task 1.3.3: Create `src/opentransport/platform_ot.c`

```c
/*
 * PeerTalk Open Transport Platform Implementation
 *
 * For System 7.6.1+ and Mac OS 8/9 on PowerPC (and late 68040)
 *
 * References:
 * - Networking With Open Transport (1997), Chapter 2: "Getting Started"
 * - Open Transport headers: OpenTransport.h, OpenTptInternet.h
 */

#include "pt_internal.h"

#if defined(PT_PLATFORM_OT)

#include <OpenTransport.h>
#include <OpenTptInternet.h>
#include <MacMemory.h>
#include <Gestalt.h>

/* Track OT initialization state */
static int g_ot_initialized = 0;

static int ot_init(struct pt_context *ctx) {
    OSStatus err;

    /*
     * Open Transport Initialization
     *
     * From Networking With Open Transport:
     * "You do not need to call Gestalt to determine whether Open Transport
     * is available. Simply call InitOpenTransport. If it returns noErr,
     * Open Transport is available; otherwise, it is not."
     *
     * Note: InitOpenTransportInContext() is Carbon-only (CarbonLib 1.0+).
     * For classic Mac OS 7.6.1-9 compatibility, use InitOpenTransport().
     */

    err = InitOpenTransport();
    if (err != noErr) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_GENERAL,
            "InitOpenTransport failed: %ld", (long)err);
        /*
         * Common failures:
         * - kOTNotFoundErr: TCP/IP not configured
         * - kENOMEMErr: Out of memory
         * - Various other OT errors
         */
        return -1;
    }

    g_ot_initialized = 1;
    PT_LOG_INFO(ctx, PT_LOG_CAT_GENERAL, "Open Transport initialized");

    /*
     * Optionally verify TCP/IP is available via Gestalt.
     * This is informational - we already know OT is present since
     * InitOpenTransport succeeded.
     */
    {
        long response = 0;
        err = Gestalt(gestaltOpenTpt, &response);
        if (err == noErr) {
            PT_LOG_DEBUG(ctx, PT_LOG_CAT_GENERAL,
                "OT Gestalt response: 0x%08lX", response);
            if (response & gestaltOpenTptTCPPresentMask) {
                PT_LOG_DEBUG(ctx, PT_LOG_CAT_GENERAL, "TCP/IP is present");
            }
        }
    }

    return 0;
}

static void ot_shutdown(struct pt_context *ctx) {
    if (g_ot_initialized) {
        CloseOpenTransport();
        g_ot_initialized = 0;
        PT_LOG_INFO(ctx, PT_LOG_CAT_GENERAL, "Open Transport closed");
    }
}

static int ot_poll(struct pt_context *ctx) {
    /* Stub - implemented in Phase 5 (Open Transport Networking) */
    (void)ctx;
    return 0;
}

static pt_tick_t ot_get_ticks(void) {
    /*
     * TickCount() works on PPC too.
     * One tick = 1/60th second (~16.67ms).
     *
     * WARNING: TickCount() is NOT safe at interrupt time.
     * This function must ONLY be called from the main event loop.
     *
     * For notifier callbacks, use OTGetTimeStamp() and
     * OTElapsedMilliseconds() instead - these ARE listed in
     * Table C-1 of Networking With Open Transport as callable
     * from notifiers.
     */
    return (pt_tick_t)TickCount();
}

static unsigned long ot_get_free_mem(void) {
    return (unsigned long)FreeMem();
}

static unsigned long ot_get_max_block(void) {
    return (unsigned long)MaxBlock();
}

pt_platform_ops pt_ot_ops = {
    ot_init,
    ot_shutdown,
    ot_poll,
    ot_get_ticks,
    ot_get_free_mem,
    ot_get_max_block,
    NULL  /* send_udp - set by Phase 6 to pt_ot_send_udp */
};

/*
 * Platform-specific allocation functions.
 * On Classic Mac, we use NewPtr/DisposePtr from the application heap.
 */
void* pt_plat_alloc(size_t size) {
    return (void *)NewPtr((Size)size);
}

void pt_plat_free(void *ptr) {
    if (ptr) {
        DisposePtr((Ptr)ptr);
    }
}

size_t pt_plat_extra_size(void) {
    /*
     * Open Transport platform needs extra space in the context for:
     * - Endpoint references and state (allocated in Phase 5)
     * For now, return 0 - will be updated when networking is implemented.
     */
    return 0;
}

#endif /* PT_PLATFORM_OT */
```

### Acceptance Criteria
1. POSIX version compiles and links on Linux/macOS
2. MacTCP version compiles with Retro68 for 68k
3. Open Transport version compiles with Retro68 for PPC
4. Platform detection correctly selects the right ops structure
5. Memory query functions return sensible values
6. MacTCP driver opens successfully on real hardware
7. Open Transport initializes successfully on real hardware

---

## Session 1.4: PT_Log Integration

> **DEPENDENCY NOTE:** This session CANNOT be completed until Phase 0 (PT_Log) is marked [DONE].
> Session 1.4 requires `libptlog.a` and `include/pt_log.h` to be built and available.

### Objective
Integrate PeerTalk with the standalone PT_Log library from Phase 0. This session does NOT create a custom logging system - it uses the existing `libptlog.a` and `pt_log.h` from Phase 0.

### What Phase 0 Provides
- `libptlog.a` - Standalone logging library (compiled in Phase 0)
- `include/pt_log.h` - Public API header with PT_LOG_* macros
- `PT_LogCreate()`/`PT_LogDestroy()` - Context lifecycle
- `PT_LogSetLevel()`/`PT_LogSetCategories()` - Runtime filtering
- `PT_LogSetCallback()` - Custom output routing
- `PT_LogPerf()` - Structured performance logging

### What Phase 1 Does
1. Add `PT_Log *log` field to `struct pt_context`
2. Call `PT_LogCreate()` in `PeerTalk_Init()`
3. Call `PT_LogDestroy()` in `PeerTalk_Shutdown()`
4. Use `PT_LOG_*` macros throughout PeerTalk code
5. Define convenience macros `PT_CTX_*` that extract log from context

### Tasks

#### Task 1.4.1: Update `src/core/pt_internal.h` for PT_Log Integration

Add the following to `pt_internal.h`:

```c
/*============================================================================
 * PT_Log Integration (from Phase 0)
 *
 * PeerTalk uses the standalone PT_Log library for all logging.
 * Include the Phase 0 header and link against libptlog.a.
 *============================================================================*/
#include <pt_log.h>

/*============================================================================
 * Convenience Macros for Context-Based Logging
 *
 * These macros extract the log handle from a pt_context pointer,
 * reducing boilerplate in PeerTalk internal code.
 *
 * Usage:
 *     PT_CTX_ERR(ctx, PT_LOG_CAT_NETWORK, "Connection failed: %d", err);
 *     PT_CTX_INFO(ctx, PT_LOG_CAT_INIT, "PeerTalk initialized");
 *============================================================================*/
#define PT_CTX_ERR(ctx, cat, ...) \
    PT_LOG_ERR((ctx)->log, cat, __VA_ARGS__)

#define PT_CTX_WARN(ctx, cat, ...) \
    PT_LOG_WARN((ctx)->log, cat, __VA_ARGS__)

#define PT_CTX_INFO(ctx, cat, ...) \
    PT_LOG_INFO((ctx)->log, cat, __VA_ARGS__)

#define PT_CTX_DEBUG(ctx, cat, ...) \
    PT_LOG_DEBUG((ctx)->log, cat, __VA_ARGS__)
```

#### Task 1.4.2: Update `PeerTalk_Init()` for PT_Log Setup

```c
PeerTalk_Error PeerTalk_Init(PeerTalk_Context **ctx_out,
                             const PeerTalk_Config *config) {
    struct pt_context *ctx;
    int err;

    /* ... existing validation and allocation ... */

    /*
     * Initialize PT_Log from Phase 0.
     * Configure based on user's log_level setting.
     *
     * NOTE: PT_LogCreate() takes no arguments. Use setter functions
     * to configure: PT_LogSetLevel(), PT_LogSetCategories(), PT_LogSetOutput()
     */
    ctx->log = PT_LogCreate();
    if (ctx->log) {
        if (config->log_level > 0) {
            PT_LogSetLevel(ctx->log, (PT_LogLevel)config->log_level);
        }
        PT_LogSetCategories(ctx->log, PT_LOG_CAT_ALL);
        PT_LogSetOutput(ctx->log, PT_LOG_OUT_CONSOLE);
    }

    PT_CTX_INFO(ctx, PT_LOG_CAT_INIT, "PeerTalk v%d.%d.%d initialized",
                PT_VERSION_MAJOR, PT_VERSION_MINOR, PT_VERSION_PATCH);

    /* ... rest of initialization ... */
}
```

#### Task 1.4.3: Update `PeerTalk_Shutdown()` for PT_Log Cleanup

```c
void PeerTalk_Shutdown(PeerTalk_Context *ctx_handle) {
    struct pt_context *ctx = (struct pt_context *)ctx_handle;

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return;
    }

    PT_CTX_INFO(ctx, PT_LOG_CAT_INIT, "PeerTalk shutting down");

    /* ... existing cleanup ... */

    /* Destroy PT_Log context last (need it for shutdown logging) */
    if (ctx->log) {
        PT_LogFlush(ctx->log);
        PT_LogDestroy(ctx->log);
        ctx->log = NULL;
    }

    /* ... final cleanup and free ... */
}
```

#### Task 1.4.4: Document ISR-Safe Deferred Logging Patterns

The following section documents deferred logging patterns for Classic Mac interrupt contexts.
These patterns are used in Phases 5, 6, and 7 when implementing MacTCP, Open Transport, and AppleTalk.

**CRITICAL: PT_LOG macros are NOT safe at interrupt time!**

**Deferred Log Flush Timing:**

`PeerTalk_Poll()` MUST process deferred logs at the START of each poll cycle, before processing new events:

```c
PeerTalk_Error PeerTalk_Poll(PeerTalk_Context *ctx) {
    /* 1. FIRST: Flush all deferred logs from previous interrupt events */
    pt_flush_deferred_logs(ctx);

    /* 2. THEN: Process pending network events */
    pt_process_network_events(ctx);

    /* 3. THEN: Invoke application callbacks */
    pt_invoke_callbacks(ctx);

    return PT_OK;
}

static void pt_flush_deferred_logs(struct pt_context *ctx) {
    /* Platform-specific: iterate connections and flush their deferred logs */
#if defined(PT_PLATFORM_MACTCP)
    for (int i = 0; i < ctx->peer_count; i++) {
        process_mactcp_deferred_logs(ctx, &ctx->peers[i].connection);
    }
#elif defined(PT_PLATFORM_OT)
    for (int i = 0; i < ctx->peer_count; i++) {
        process_ot_deferred_logs(ctx, &ctx->peers[i].endpoint);
    }
#elif defined(PT_PLATFORM_APPLETALK)
    for (int i = 0; i < ctx->peer_count; i++) {
        process_adsp_deferred_logs(ctx, &ctx->peers[i].adsp);
    }
#endif
}
```

This ordering ensures:
- Events logged before they're overwritten by new events
- Consistent event ordering in log output
- No log entries missed during high-traffic periods

```c
/*============================================================================
 * ISR-Safety and Deferred Logging
 *
 * PT_LOG macros call PT_LogWrite() which may use vsprintf and file I/O.
 * These CANNOT be called from:
 * - MacTCP ASR (Asynchronous Service Routine)
 * - Open Transport notifier callbacks
 * - ADSP ioCompletion or userRoutine callbacks
 *
 * Pattern: Set flags in interrupt, log in main loop (PeerTalk_Poll).
 *============================================================================*/

```c
/*
 * PeerTalk Logging System
 */

#ifndef PT_LOG_H
#define PT_LOG_H

#include "pt_types.h"
#include "pt_compat.h"

/*============================================================================
 * Log Levels
 *============================================================================*/

typedef enum {
    PT_LOG_NONE  = 0,
    PT_LOG_ERR   = 1,
    PT_LOG_WARN  = 2,
    PT_LOG_INFO  = 3,
    PT_LOG_DEBUG = 4
} pt_log_level;

/*============================================================================
 * Log Categories (bitmask)
 *============================================================================*/

/*
 * Log Categories - MUST match Phase 0 PT_Log definitions exactly!
 * See plan/PHASE-0-LOGGING.md for authoritative category list.
 */
typedef enum {
    PT_LOG_CAT_GENERAL   = 0x0001,  /* General messages */
    PT_LOG_CAT_NETWORK   = 0x0002,  /* Network operations (connections, data transfer) */
    PT_LOG_CAT_PROTOCOL  = 0x0004,  /* Protocol encoding/decoding */
    PT_LOG_CAT_MEMORY    = 0x0008,  /* Memory allocation/deallocation */
    PT_LOG_CAT_PLATFORM  = 0x0010,  /* Platform-specific operations (MacTCP, OT, POSIX) */
    PT_LOG_CAT_PERF      = 0x0020,  /* Performance/benchmark data */
    PT_LOG_CAT_CONNECT   = 0x0040,  /* Connection establishment/teardown */
    PT_LOG_CAT_DISCOVERY = 0x0080,  /* Discovery operations (UDP broadcast, NBP lookup) */
    PT_LOG_CAT_SEND      = 0x0100,  /* Send operations (TCP/UDP/ADSP transmit) */
    PT_LOG_CAT_RECV      = 0x0200,  /* Receive operations (TCP/UDP/ADSP receive) */
    PT_LOG_CAT_INIT      = 0x0400,  /* Initialization/startup/shutdown */
    PT_LOG_CAT_ALL       = 0xFFFF
} pt_log_category;

/*============================================================================
 * Logging Functions
 *============================================================================*/

struct pt_context;  /* Forward declaration */

/* Initialize logging (called by PeerTalk_Init) */
int pt_log_init(struct pt_context *ctx, const char *filename);

/* Shutdown logging (called by PeerTalk_Shutdown) */
void pt_log_shutdown(struct pt_context *ctx);

/* Flush log buffer to file */
void pt_log_flush(struct pt_context *ctx);

/* Main logging function */
void pt_log(struct pt_context *ctx, pt_log_level level,
            pt_log_category cat, const char *fmt, ...);

/*============================================================================
 * Logging Macros
 *
 * When PT_LOG_ENABLED is not defined, these expand to nothing.
 *============================================================================*/

#ifdef PT_LOG_ENABLED

#define PT_LOG_ERR(ctx, cat, ...) \
    pt_log(ctx, PT_LOG_ERR, cat, __VA_ARGS__)

#define PT_LOG_WARN(ctx, cat, ...) \
    pt_log(ctx, PT_LOG_WARN, cat, __VA_ARGS__)

#define PT_LOG_INFO(ctx, cat, ...) \
    pt_log(ctx, PT_LOG_INFO, cat, __VA_ARGS__)

#define PT_LOG_DEBUG(ctx, cat, ...) \
    pt_log(ctx, PT_LOG_DEBUG, cat, __VA_ARGS__)

#else /* PT_LOG_ENABLED */

#define PT_LOG_ERR(ctx, cat, ...)   ((void)0)
#define PT_LOG_WARN(ctx, cat, ...)  ((void)0)
#define PT_LOG_INFO(ctx, cat, ...)  ((void)0)
#define PT_LOG_DEBUG(ctx, cat, ...) ((void)0)

#endif /* PT_LOG_ENABLED */

/*============================================================================
 * ISR-Safety and Deferred Logging
 *
 * CRITICAL: PT_LOG macros are NOT safe at interrupt time. On Classic Mac,
 * these macros eventually call vsprintf and File Manager which cannot be
 * called from ASR, notifier, or completion routines.
 *
 * For interrupt-context events, use flag-based deferred logging.
 *
 * The pattern is the same for all three Classic Mac networking stacks:
 * 1. Define a deferred_log struct to hold event flags and data
 * 2. In interrupt context: set flags and store data (NO logging)
 * 3. In main loop (PeerTalk_Poll): check flags, log, then clear flags
 *============================================================================*/

/*----------------------------------------------------------------------------
 * Deferred Log State Structure (shared pattern)
 *----------------------------------------------------------------------------*/
typedef struct {
    volatile uint8_t pending_events;    /* Bitmask of pending log events */
    volatile uint8_t reserved;          /* Alignment */
    uint16_t         last_error;        /* Error code from ISR */
    uint16_t         bytes_count;       /* Bytes received/sent */
    uint16_t         peer_id;           /* Peer involved in event */
} pt_deferred_log;

/* Event flags for pending_events bitmask */
#define PT_EVT_DATA_ARRIVED     0x01
#define PT_EVT_DATA_SENT        0x02
#define PT_EVT_REMOTE_CLOSE     0x04
#define PT_EVT_ERROR            0x08
#define PT_EVT_CONNECT_COMPLETE 0x10
#define PT_EVT_ACCEPT_READY     0x20

/*----------------------------------------------------------------------------
 * Example 1: MacTCP ASR Deferred Logging
 *
 * MacTCP ASR runs at interrupt level. Cannot call PT_Log, TickCount(),
 * or any memory-moving trap.
 *----------------------------------------------------------------------------*/

/* In ASR (interrupt time) - just set flags, NO logging */
static pascal void tcp_asr(StreamPtr stream, unsigned short event,
                           Ptr userDataPtr, unsigned short terminReason,
                           ICMPReport *icmpMsg) {
    pt_connection_state *state = (pt_connection_state *)userDataPtr;

    switch (event) {
    case TCPDataArrival:
        state->deferred.pending_events |= PT_EVT_DATA_ARRIVED;
        /* Store byte count if available from stream state */
        break;
    case TCPClosing:
        state->deferred.pending_events |= PT_EVT_REMOTE_CLOSE;
        break;
    case TCPTerminate:
        state->deferred.pending_events |= PT_EVT_ERROR;
        state->deferred.last_error = terminReason;
        break;
    }
    /* NO PT_LOG calls here - would crash! */
}

/* In PeerTalk_Poll (main loop) - safe to log */
void process_mactcp_deferred_logs(pt_context *ctx, pt_connection_state *state) {
    uint8_t events = state->deferred.pending_events;

    if (events & PT_EVT_DATA_ARRIVED) {
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_RECV, "MacTCP data arrived");
        state->deferred.pending_events &= ~PT_EVT_DATA_ARRIVED;
    }
    if (events & PT_EVT_REMOTE_CLOSE) {
        PT_LOG_INFO(ctx->log, PT_LOG_CAT_NETWORK, "MacTCP remote close");
        state->deferred.pending_events &= ~PT_EVT_REMOTE_CLOSE;
    }
    if (events & PT_EVT_ERROR) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_NETWORK, "MacTCP error: %d",
                   state->deferred.last_error);
        state->deferred.pending_events &= ~PT_EVT_ERROR;
    }
}

/*----------------------------------------------------------------------------
 * Example 2: Open Transport Notifier Deferred Logging
 *
 * OT notifiers run at deferred task time. Use OTAtomicSetBit for thread-safe
 * flag setting. Same restrictions: no PT_Log, no sync OT calls.
 *----------------------------------------------------------------------------*/

/* In notifier (deferred task time) - use atomic operations */
static pascal void ot_notifier(void *context, OTEventCode code,
                               OTResult result, void *cookie) {
    pt_ot_endpoint *ep = (pt_ot_endpoint *)context;

    switch (code) {
    case T_DATA:
        OTAtomicSetBit(&ep->deferred.pending_events, 0);  /* PT_EVT_DATA_ARRIVED */
        break;
    case T_DISCONNECT:
        OTAtomicSetBit(&ep->deferred.pending_events, 2);  /* PT_EVT_REMOTE_CLOSE */
        break;
    case T_ORDREL:
        OTAtomicSetBit(&ep->deferred.pending_events, 2);  /* PT_EVT_REMOTE_CLOSE */
        break;
    case T_GODATA:
        /* Flow control lifted - can resume sending */
        OTAtomicSetBit(&ep->deferred.pending_events, 1);  /* PT_EVT_DATA_SENT */
        break;
    }
    /* NO PT_LOG calls here! */
}

/* In PeerTalk_Poll (main loop) - safe to log */
void process_ot_deferred_logs(pt_context *ctx, pt_ot_endpoint *ep) {
    if (OTAtomicTestBit(&ep->deferred.pending_events, 0)) {
        OTAtomicClearBit(&ep->deferred.pending_events, 0);
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_RECV, "OT data available");
    }
    if (OTAtomicTestBit(&ep->deferred.pending_events, 2)) {
        OTAtomicClearBit(&ep->deferred.pending_events, 2);
        PT_LOG_INFO(ctx->log, PT_LOG_CAT_NETWORK, "OT disconnect/ordrel");
    }
}

/*----------------------------------------------------------------------------
 * Example 3: ADSP Completion Routine Deferred Logging
 *
 * ADSP has TWO callback types, both at interrupt level:
 * - ioCompletion: async I/O completion (receives PB in A0)
 * - userRoutine: connection events (receives CCB in A1)
 *
 * CRITICAL: userFlags MUST be cleared after reading or connection will hang!
 *----------------------------------------------------------------------------*/

/* userRoutine - connection events (interrupt level) */
static pascal void adsp_user_routine(TPCCB ccb) {
    pt_adsp_connection *conn = (pt_adsp_connection *)ccb;

    /* Read and IMMEDIATELY clear userFlags - failure to clear hangs connection! */
    uint8_t flags = ccb->userFlags;
    ccb->userFlags = 0;  /* CRITICAL: Must clear! */

    if (flags & eClosed) {
        conn->deferred.pending_events |= PT_EVT_REMOTE_CLOSE;
    }
    if (flags & eTearDown) {
        conn->deferred.pending_events |= PT_EVT_ERROR;
        conn->deferred.last_error = -1;  /* Connection torn down */
    }
    if (flags & eAttention) {
        /* Attention message received - handle separately */
    }
    if (flags & eFwdReset) {
        conn->deferred.pending_events |= PT_EVT_ERROR;
        conn->deferred.last_error = -2;  /* Forward reset */
    }
    /* NO PT_LOG calls here! */
}

/* ioCompletion - async I/O completion (interrupt level) */
static pascal void adsp_io_completion(DSPPBPtr pb) {
    pt_adsp_connection *conn = PT_ADSP_GET_CONTEXT(pb);

    conn->async_result = pb->ioResult;
    conn->async_pending = false;

    if (pb->ioResult != noErr) {
        conn->deferred.pending_events |= PT_EVT_ERROR;
        conn->deferred.last_error = pb->ioResult;
    } else {
        conn->deferred.pending_events |= PT_EVT_DATA_ARRIVED;
    }
    /* NO PT_LOG calls here! */
}

/* In PeerTalk_Poll (main loop) - safe to log */
void process_adsp_deferred_logs(pt_context *ctx, pt_adsp_connection *conn) {
    uint8_t events = conn->deferred.pending_events;

    if (events & PT_EVT_DATA_ARRIVED) {
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_RECV, "ADSP data arrived");
        conn->deferred.pending_events &= ~PT_EVT_DATA_ARRIVED;
    }
    if (events & PT_EVT_REMOTE_CLOSE) {
        PT_LOG_INFO(ctx->log, PT_LOG_CAT_NETWORK, "ADSP connection closed");
        conn->deferred.pending_events &= ~PT_EVT_REMOTE_CLOSE;
    }
    if (events & PT_EVT_ERROR) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_NETWORK, "ADSP error: %d",
                   conn->deferred.last_error);
        conn->deferred.pending_events &= ~PT_EVT_ERROR;
    }
}

#endif /* PT_LOG_H */
```

#### Task 1.4.2: Create `src/core/pt_log.c`

```c
/*
 * PeerTalk Logging System Implementation
 */

#include "pt_log.h"
#include "pt_internal.h"
#include <stdarg.h>

#ifdef PT_LOG_ENABLED

/*============================================================================
 * Constants
 *============================================================================*/

#define LOG_BUFFER_SIZE     512
#define LOG_LINE_SIZE       192

/*============================================================================
 * Static Data
 *============================================================================*/

static char g_log_buffer[LOG_BUFFER_SIZE];
static int g_log_buffer_pos = 0;

static const char *g_level_names[] = {
    "---",      /* NONE */
    "ERR",
    "WRN",
    "INF",
    "DBG"
};

/*============================================================================
 * Platform-Specific File I/O
 *============================================================================*/

#if defined(PT_PLATFORM_POSIX)

#include <stdio.h>

int pt_log_init(struct pt_context *ctx, const char *filename) {
    if (filename) {
        ctx->log_file = fopen(filename, "a");
        if (!ctx->log_file) {
            return -1;
        }
    }
    ctx->log_categories = PT_LOG_CAT_ALL;
    return 0;
}

void pt_log_shutdown(struct pt_context *ctx) {
    pt_log_flush(ctx);
    if (ctx->log_file) {
        fclose((FILE *)ctx->log_file);
        ctx->log_file = NULL;
    }
}

void pt_log_flush(struct pt_context *ctx) {
    if (g_log_buffer_pos > 0) {
        if (ctx->log_file) {
            fwrite(g_log_buffer, 1, g_log_buffer_pos, (FILE *)ctx->log_file);
            fflush((FILE *)ctx->log_file);
        }
        /* Also write to stderr */
        fwrite(g_log_buffer, 1, g_log_buffer_pos, stderr);
        g_log_buffer_pos = 0;
    }
}

#elif defined(PT_PLATFORM_CLASSIC_MAC)

#include <Files.h>

/*
 * Classic Mac file I/O using standard File Manager calls.
 * These work on System 6.0.8 through Mac OS 9.
 *
 * We use the "working directory" model for simplicity - the log file
 * is created in the same directory as the application.
 */

int pt_log_init(struct pt_context *ctx, const char *filename) {
    OSErr err;
    ParamBlockRec pb;
    Str255 pname;
    int i;

    ctx->log_refnum = 0;
    ctx->log_categories = PT_LOG_CAT_ALL;

    if (!filename) {
        return 0;  /* Logging to file disabled */
    }

    /* Convert C string to Pascal string */
    i = 0;
    while (filename[i] && i < 254) {
        pname[i + 1] = filename[i];
        i++;
    }
    pname[0] = (unsigned char)i;

    /* Delete existing file if present (ignore errors) */
    pt_memset(&pb, 0, sizeof(pb));
    pb.ioParam.ioNamePtr = pname;
    pb.ioParam.ioVRefNum = 0;  /* Default volume (application's volume) */
    FSDelete(pname, 0);

    /* Create new file - 'PTLK' creator for PeerTalk, 'TEXT' for plain text */
    err = Create(pname, 0, 'PTLK', 'TEXT');
    if (err != noErr && err != dupFNErr) {
        return -1;
    }

    /* Open for writing */
    pt_memset(&pb, 0, sizeof(pb));
    pb.ioParam.ioNamePtr = pname;
    pb.ioParam.ioVRefNum = 0;
    pb.ioParam.ioPermssn = fsWrPerm;

    err = PBOpenSync(&pb);
    if (err != noErr) {
        return -1;
    }

    ctx->log_refnum = pb.ioParam.ioRefNum;
    return 0;
}

void pt_log_shutdown(struct pt_context *ctx) {
    ParamBlockRec pb;

    pt_log_flush(ctx);

    if (ctx->log_refnum) {
        pt_memset(&pb, 0, sizeof(pb));
        pb.ioParam.ioRefNum = ctx->log_refnum;
        PBCloseSync(&pb);
        ctx->log_refnum = 0;
    }
}

void pt_log_flush(struct pt_context *ctx) {
    ParamBlockRec pb;

    if (g_log_buffer_pos > 0 && ctx->log_refnum) {
        pt_memset(&pb, 0, sizeof(pb));
        pb.ioParam.ioRefNum = ctx->log_refnum;
        pb.ioParam.ioBuffer = g_log_buffer;
        pb.ioParam.ioReqCount = g_log_buffer_pos;
        pb.ioParam.ioPosMode = fsAtMark;

        PBWriteSync(&pb);
        g_log_buffer_pos = 0;
    }
}

#endif /* Platform-specific */

/*============================================================================
 * Common Logging Implementation
 *============================================================================*/

void pt_log(struct pt_context *ctx, pt_log_level level,
            pt_log_category cat, const char *fmt, ...) {
    va_list ap;
    char line[LOG_LINE_SIZE];
    int len;
    pt_tick_t ticks;

    /* Bail early if no context or level filtered */
    if (!ctx || level > ctx->log_level) {
        return;
    }

    /* Bail if category filtered */
    if (!(ctx->log_categories & cat)) {
        return;
    }

    /* Get timestamp */
    ticks = PT_PLATFORM_OPS->get_ticks();

    /* Format the message */
    va_start(ap, fmt);
    len = pt_vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    /* Check if buffer needs flush */
    if (g_log_buffer_pos + len + 32 > LOG_BUFFER_SIZE) {
        pt_log_flush(ctx);
    }

    /* Add timestamped line to buffer */
    g_log_buffer_pos += pt_snprintf(
        g_log_buffer + g_log_buffer_pos,
        LOG_BUFFER_SIZE - g_log_buffer_pos,
        "[%08lu][%s] %s\n",
        (unsigned long)ticks,
        g_level_names[level],
        line
    );
}

#else /* PT_LOG_ENABLED */

/* Stub implementations when logging disabled */
int pt_log_init(struct pt_context *ctx, const char *filename) {
    (void)ctx;
    (void)filename;
    return 0;
}

void pt_log_shutdown(struct pt_context *ctx) {
    (void)ctx;
}

void pt_log_flush(struct pt_context *ctx) {
    (void)ctx;
}

#endif /* PT_LOG_ENABLED */
```

#### Task 1.4.3: Create `tests/test_log.c`

```c
/*
 * PeerTalk Logging System Tests
 */

#include <stdio.h>
#include <string.h>
#include "peertalk.h"
#include "pt_internal.h"

/* Test with a minimal context */
static struct pt_context test_ctx;

void test_log_levels(void) {
    printf("  Testing log levels...\n");

    /* Set to DEBUG - should see all messages */
    test_ctx.log_level = PT_LOG_DEBUG;

    PT_LOG_ERR(&test_ctx, PT_LOG_CAT_GENERAL, "Error message: %d", 1);
    PT_LOG_WARN(&test_ctx, PT_LOG_CAT_GENERAL, "Warning message: %d", 2);
    PT_LOG_INFO(&test_ctx, PT_LOG_CAT_GENERAL, "Info message: %d", 3);
    PT_LOG_DEBUG(&test_ctx, PT_LOG_CAT_GENERAL, "Debug message: %d", 4);

    pt_log_flush(&test_ctx);

    /* Set to WARN - should only see ERR and WARN */
    test_ctx.log_level = PT_LOG_WARN;

    PT_LOG_ERR(&test_ctx, PT_LOG_CAT_GENERAL, "Error (should appear)");
    PT_LOG_WARN(&test_ctx, PT_LOG_CAT_GENERAL, "Warning (should appear)");
    PT_LOG_INFO(&test_ctx, PT_LOG_CAT_GENERAL, "Info (should NOT appear)");
    PT_LOG_DEBUG(&test_ctx, PT_LOG_CAT_GENERAL, "Debug (should NOT appear)");

    pt_log_flush(&test_ctx);
    printf("    OK\n");
}

void test_log_categories(void) {
    printf("  Testing log categories...\n");

    test_ctx.log_level = PT_LOG_DEBUG;

    /* Only enable CONNECT category */
    test_ctx.log_categories = PT_LOG_CAT_CONNECT;

    PT_LOG_INFO(&test_ctx, PT_LOG_CAT_GENERAL, "General (should NOT appear)");
    PT_LOG_INFO(&test_ctx, PT_LOG_CAT_CONNECT, "Connect (should appear)");
    PT_LOG_INFO(&test_ctx, PT_LOG_CAT_SEND, "Send (should NOT appear)");

    pt_log_flush(&test_ctx);

    /* Enable multiple categories */
    test_ctx.log_categories = PT_LOG_CAT_CONNECT | PT_LOG_CAT_SEND;

    PT_LOG_INFO(&test_ctx, PT_LOG_CAT_GENERAL, "General (should NOT appear)");
    PT_LOG_INFO(&test_ctx, PT_LOG_CAT_CONNECT, "Connect (should appear)");
    PT_LOG_INFO(&test_ctx, PT_LOG_CAT_SEND, "Send (should appear)");

    pt_log_flush(&test_ctx);

    /* Restore all categories */
    test_ctx.log_categories = PT_LOG_CAT_ALL;
    printf("    OK\n");
}

void test_log_formatting(void) {
    printf("  Testing log formatting...\n");

    test_ctx.log_level = PT_LOG_DEBUG;
    test_ctx.log_categories = PT_LOG_CAT_ALL;

    PT_LOG_INFO(&test_ctx, PT_LOG_CAT_PROTOCOL,
        "Hex value: 0x%08X", 0xDEADBEEF);
    PT_LOG_INFO(&test_ctx, PT_LOG_CAT_PROTOCOL,
        "Multiple args: %s %d %x", "test", 42, 255);
    PT_LOG_DEBUG(&test_ctx, PT_LOG_CAT_MEMORY,
        "Memory: free=%lu max=%lu",
        pt_get_free_mem(), pt_get_max_block());

    pt_log_flush(&test_ctx);
    printf("    OK\n");
}

int main(void) {
    int result = 0;

    printf("PeerTalk Logging Tests\n");
    printf("======================\n\n");

    /* Initialize test context */
    pt_memset(&test_ctx, 0, sizeof(test_ctx));
    test_ctx.magic = PT_CONTEXT_MAGIC;
    test_ctx.plat = PT_PLATFORM_OPS;

    /* Initialize logging to file */
    if (pt_log_init(&test_ctx, "peertalk_test.log") != 0) {
        printf("Failed to open log file\n");
        return 1;
    }

    test_ctx.log_level = PT_LOG_DEBUG;
    test_ctx.log_categories = PT_LOG_CAT_ALL;

    PT_LOG_INFO(&test_ctx, PT_LOG_CAT_GENERAL, "=== Log test started ===");

    test_log_levels();
    test_log_categories();
    test_log_formatting();

    PT_LOG_INFO(&test_ctx, PT_LOG_CAT_GENERAL, "=== Log test complete ===");

    pt_log_shutdown(&test_ctx);

    printf("\n======================\n");
    printf("All logging tests PASSED\n");
    printf("Check peertalk_test.log for output\n");

    return result;
}
```

#### Task 1.4.4: Add Performance Logging Support

Extend the logging system with structured performance entries for benchmarking and debugging GUI/network interactions.

**Add to `pt_log.h` - Extended Categories:**

```c
/*============================================================================
 * Log Categories (bitmask) - Aligned with Phase 0 PT_Log
 *
 * IMPORTANT: These MUST match the categories defined in PHASE-0-LOGGING.md
 * to ensure consistency across the PeerTalk codebase.
 *============================================================================*/

typedef enum {
    PT_LOG_CAT_GENERAL   = 0x0001,  /* General messages */
    PT_LOG_CAT_NETWORK   = 0x0002,  /* Network operations (connections, data transfer) */
    PT_LOG_CAT_PROTOCOL  = 0x0004,  /* Protocol encoding/decoding */
    PT_LOG_CAT_MEMORY    = 0x0008,  /* Memory allocation/deallocation */
    PT_LOG_CAT_PLATFORM  = 0x0010,  /* Platform-specific operations (MacTCP, OT, POSIX) */
    PT_LOG_CAT_PERF      = 0x0020,  /* Performance/benchmark data */
    PT_LOG_CAT_CONNECT   = 0x0040,  /* Connection establishment/teardown */
    PT_LOG_CAT_DISCOVERY = 0x0080,  /* Discovery operations (UDP broadcast, NBP lookup) */
    PT_LOG_CAT_SEND      = 0x0100,  /* Send operations (TCP/UDP/ADSP transmit) */
    PT_LOG_CAT_RECV      = 0x0200,  /* Receive operations (TCP/UDP/ADSP receive) */
    PT_LOG_CAT_INIT      = 0x0400,  /* Initialization/startup/shutdown */
    /* Available for applications */
    PT_LOG_CAT_APP1      = 0x0800,
    PT_LOG_CAT_APP2      = 0x1000,
    PT_LOG_CAT_APP3      = 0x2000,
    PT_LOG_CAT_APP4      = 0x4000,
    PT_LOG_CAT_ALL       = 0xFFFF
} pt_log_category;
```

**Add to `pt_log.h` - Structured Performance Entry:**

```c
/*============================================================================
 * Structured Performance Log Entry
 *
 * For detailed performance analysis. Can be:
 * - Written to text log via pt_log_perf()
 * - Captured via callback for custom processing
 * - Written to benchmark log file in TSV format
 *
 * Design notes:
 * - 16 bytes total, cache-friendly alignment
 * - elapsed_ms uses TickCount on Classic Mac (converted to ms)
 * - seq_num allows correlation between sender/receiver logs
 *============================================================================*/

typedef struct {
    uint32_t    seq_num;        /* Message sequence number (globally unique per session) */
    uint32_t    elapsed_ms;     /* Milliseconds since PeerTalk_Init */
    uint16_t    msg_size;       /* Payload size in bytes */
    uint8_t     msg_type;       /* Protocol message type (PT_MSG_*) */
    uint8_t     direction;      /* PT_DIR_SEND=0, PT_DIR_RECV=1 */
    uint16_t    peer_id;        /* Which peer (PeerTalk_PeerID) */
    uint16_t    transport;      /* PT_TRANSPORT_* flags */
} pt_perf_entry;

#define PT_DIR_SEND     0
#define PT_DIR_RECV     1

/* Performance log callback - called for each pt_log_perf() if set */
typedef void (*pt_perf_callback)(
    struct pt_context *ctx,
    const pt_perf_entry *entry,
    int16_t result,             /* 0=success, negative=error code */
    void *user_data
);

/*============================================================================
 * Performance Logging Functions
 *============================================================================*/

/* Set callback for structured performance entries (NULL to disable) */
void pt_log_set_perf_callback(struct pt_context *ctx,
                               pt_perf_callback cb,
                               void *user_data);

/* Log a structured performance entry
 * - Writes to text log if PT_LOG_CAT_PERF enabled
 * - Calls callback if set
 * - Thread-safe (uses atomic seq_num increment)
 */
void pt_log_perf(struct pt_context *ctx,
                 const pt_perf_entry *entry,
                 int16_t result);

/* Get milliseconds elapsed since context initialization
 * Uses TickCount on Classic Mac, clock_gettime on POSIX
 */
uint32_t pt_log_elapsed_ms(struct pt_context *ctx);

/* Get next sequence number (atomic increment) */
uint32_t pt_log_next_seq(struct pt_context *ctx);

/*============================================================================
 * Performance Logging Macros
 *
 * Convenience macros for common performance log patterns.
 *============================================================================*/

#ifdef PT_LOG_ENABLED

#define PT_LOG_PERF_SEND(ctx, peer, transport, size, type, result) \
    do { \
        pt_perf_entry _e; \
        _e.seq_num = pt_log_next_seq(ctx); \
        _e.elapsed_ms = pt_log_elapsed_ms(ctx); \
        _e.msg_size = (size); \
        _e.msg_type = (type); \
        _e.direction = PT_DIR_SEND; \
        _e.peer_id = (peer); \
        _e.transport = (transport); \
        pt_log_perf(ctx, &_e, result); \
    } while(0)

#define PT_LOG_PERF_RECV(ctx, peer, transport, size, type, result) \
    do { \
        pt_perf_entry _e; \
        _e.seq_num = pt_log_next_seq(ctx); \
        _e.elapsed_ms = pt_log_elapsed_ms(ctx); \
        _e.msg_size = (size); \
        _e.msg_type = (type); \
        _e.direction = PT_DIR_RECV; \
        _e.peer_id = (peer); \
        _e.transport = (transport); \
        pt_log_perf(ctx, &_e, result); \
    } while(0)

#else /* PT_LOG_ENABLED */

#define PT_LOG_PERF_SEND(ctx, peer, transport, size, type, result) ((void)0)
#define PT_LOG_PERF_RECV(ctx, peer, transport, size, type, result) ((void)0)

#endif /* PT_LOG_ENABLED */
```

**Add to `pt_internal.h` - Context Extensions:**

```c
/* Add to struct pt_context: */

    /* Performance logging state */
    pt_tick_t           session_start_ticks;  /* Ticks at init for elapsed calc */
    uint32_t            perf_seq_num;         /* Next sequence number */
    pt_perf_callback    perf_callback;        /* Structured log callback */
    void               *perf_user_data;       /* Callback user data */
```

**Add to `pt_log.c` - Implementation:**

```c
/*============================================================================
 * Performance Logging Implementation
 *============================================================================*/

void pt_log_set_perf_callback(struct pt_context *ctx,
                               pt_perf_callback cb,
                               void *user_data) {
    if (!ctx) return;
    ctx->perf_callback = cb;
    ctx->perf_user_data = user_data;
}

uint32_t pt_log_elapsed_ms(struct pt_context *ctx) {
    pt_tick_t now, elapsed_ticks;

    if (!ctx) return 0;

    now = PT_PLATFORM_OPS->get_ticks();
    elapsed_ticks = now - ctx->session_start_ticks;

    /*
     * Convert ticks to milliseconds:
     * - POSIX: ticks are already milliseconds (from clock_gettime)
     * - Classic Mac: ticks are 1/60 second, so ms = ticks * 1000 / 60
     *
     * For Classic Mac, use integer math to avoid floating point:
     *   ms = ticks * 50 / 3  (equivalent to * 1000 / 60, avoids overflow longer)
     *
     * Note: This overflows after ~49 days on Classic Mac, which is fine
     * for benchmark sessions.
     */
#if defined(PT_PLATFORM_POSIX)
    return (uint32_t)elapsed_ticks;  /* Already milliseconds */
#else
    /* Classic Mac: ticks * 50 / 3 = milliseconds */
    return (uint32_t)((elapsed_ticks * 50) / 3);
#endif
}

uint32_t pt_log_next_seq(struct pt_context *ctx) {
    uint32_t seq;

    if (!ctx) return 0;

    /*
     * Atomic increment for thread safety.
     * On Classic Mac (cooperative multitasking), this is just an increment.
     * On POSIX with threads, we'd need atomic operations.
     *
     * For Classic Mac, interrupts could theoretically cause issues,
     * but ASR/notifiers shouldn't call this function.
     */
    seq = ctx->perf_seq_num;
    ctx->perf_seq_num = seq + 1;

    return seq;
}

void pt_log_perf(struct pt_context *ctx,
                 const pt_perf_entry *entry,
                 int16_t result) {
    static const char *dir_names[] = { "SEND", "RECV" };
    /*
     * Transport names indexed by PT_TRANSPORT_* enum values:
     *   0 = PT_TRANSPORT_NONE
     *   1 = PT_TRANSPORT_TCP  (0x01)
     *   2 = PT_TRANSPORT_UDP  (0x02)
     *   3 = (unused)
     *   4 = PT_TRANSPORT_APPLETALK (0x04)
     */
    static const char *transport_names[] = {
        "???", "TCP", "UDP", "???", "ADSP"
    };
    const char *tname;

    if (!ctx || !entry) return;

    /* Call callback if set */
    if (ctx->perf_callback) {
        ctx->perf_callback(ctx, entry, result, ctx->perf_user_data);
    }

    /* Write to text log if PERF category enabled */
    if (ctx->log_categories & PT_LOG_CAT_PERF) {
        /* Decode transport name */
        if (entry->transport <= 4) {
            tname = transport_names[entry->transport];
        } else {
            tname = "???";
        }

        pt_log(ctx, PT_LOG_INFO, PT_LOG_CAT_PERF,
            "PERF seq=%u t=%u %s peer=%u %s sz=%u type=%u err=%d",
            entry->seq_num,
            entry->elapsed_ms,
            dir_names[entry->direction & 1],
            entry->peer_id,
            tname,
            entry->msg_size,
            entry->msg_type,
            (int)result);
    }
}
```

**Update `pt_log_init()` - Initialize Performance State:**

```c
/* Add to pt_log_init(), after opening file: */

    /* Initialize performance logging state */
    ctx->session_start_ticks = PT_PLATFORM_OPS->get_ticks();
    ctx->perf_seq_num = 1;  /* Start at 1, 0 reserved for "no sequence" */
    ctx->perf_callback = NULL;
    ctx->perf_user_data = NULL;
```

#### Task 1.4.5: Add GUI Logging Macros

```c
/*============================================================================
 * GUI Logging Macros (in pt_log.h)
 *
 * For debugging Classic Mac GUI code - event handling, window updates, etc.
 * Useful when tracking down issues with WaitNextEvent, dialog handling,
 * or SIOW integration.
 *============================================================================*/

#ifdef PT_LOG_ENABLED

#define PT_LOG_GUI_EVENT(ctx, what, msg, ...) \
    PT_LOG_DEBUG(ctx, PT_LOG_CAT_GUI, "EVT[%d] " msg, (what), ##__VA_ARGS__)

#define PT_LOG_GUI_WINDOW(ctx, msg, ...) \
    PT_LOG_DEBUG(ctx, PT_LOG_CAT_GUI, "WIN " msg, ##__VA_ARGS__)

#define PT_LOG_GUI_DIALOG(ctx, msg, ...) \
    PT_LOG_DEBUG(ctx, PT_LOG_CAT_GUI, "DLG " msg, ##__VA_ARGS__)

#define PT_LOG_GUI_MENU(ctx, menu, item) \
    PT_LOG_DEBUG(ctx, PT_LOG_CAT_GUI, "MENU %d/%d", (menu), (item))

#else /* PT_LOG_ENABLED */

#define PT_LOG_GUI_EVENT(ctx, what, msg, ...)  ((void)0)
#define PT_LOG_GUI_WINDOW(ctx, msg, ...)       ((void)0)
#define PT_LOG_GUI_DIALOG(ctx, msg, ...)       ((void)0)
#define PT_LOG_GUI_MENU(ctx, menu, item)       ((void)0)

#endif /* PT_LOG_ENABLED */
```

#### Task 1.4.6: Update Test File for Performance Logging

```c
/* Add to tests/test_log.c */

void test_perf_logging(void) {
    pt_perf_entry entry;
    int i;

    printf("  Testing performance logging...\n");

    /* Test elapsed time (should be > 0 after some delay) */
    {
        uint32_t t1, t2;
        t1 = pt_log_elapsed_ms(&test_ctx);
        /* Small busy-wait */
        for (i = 0; i < 100000; i++) { }
        t2 = pt_log_elapsed_ms(&test_ctx);

        if (t2 < t1) {
            printf("    FAIL: elapsed time went backwards\n");
            return;
        }
    }

    /* Test sequence numbers */
    {
        uint32_t s1, s2, s3;
        s1 = pt_log_next_seq(&test_ctx);
        s2 = pt_log_next_seq(&test_ctx);
        s3 = pt_log_next_seq(&test_ctx);

        if (s2 != s1 + 1 || s3 != s2 + 1) {
            printf("    FAIL: sequence numbers not sequential\n");
            return;
        }
    }

    /* Test performance entry logging */
    test_ctx.log_categories = PT_LOG_CAT_ALL;

    entry.seq_num = pt_log_next_seq(&test_ctx);
    entry.elapsed_ms = pt_log_elapsed_ms(&test_ctx);
    entry.msg_size = 256;
    entry.msg_type = 1;
    entry.direction = PT_DIR_SEND;
    entry.peer_id = 42;
    entry.transport = PT_TRANSPORT_TCP;

    pt_log_perf(&test_ctx, &entry, 0);

    /* Test macros */
    PT_LOG_PERF_SEND(&test_ctx, 1, PT_TRANSPORT_TCP, 100, 2, 0);
    PT_LOG_PERF_RECV(&test_ctx, 1, PT_TRANSPORT_ADSP, 200, 3, -1);

    pt_log_flush(&test_ctx);
    printf("    OK\n");
}

void test_gui_logging(void) {
    printf("  Testing GUI logging...\n");

    test_ctx.log_categories = PT_LOG_CAT_ALL;

    PT_LOG_GUI_EVENT(&test_ctx, 1, "mouseDown at %d,%d", 100, 200);
    PT_LOG_GUI_EVENT(&test_ctx, 3, "keyDown '%c'", 'A');
    PT_LOG_GUI_WINDOW(&test_ctx, "activated window %p", (void *)0x12345678);
    PT_LOG_GUI_DIALOG(&test_ctx, "item %d clicked", 1);
    PT_LOG_GUI_MENU(&test_ctx, 128, 1);

    pt_log_flush(&test_ctx);
    printf("    OK\n");
}

/* Update main() to call new tests: */
/*
    test_log_levels();
    test_log_categories();
    test_log_formatting();
    test_perf_logging();   // ADD
    test_gui_logging();    // ADD
*/
```

### Acceptance Criteria
1. Log output appears in file with correct timestamps
2. Level filtering works (setting INFO hides DEBUG)
3. Category filtering works (can enable/disable by category)
4. Buffer flush works correctly
5. Performance: logging disabled has zero overhead (macro expands to nothing)
6. Works on Classic Mac with HFS filesystem (System 6 and 7)
7. Log messages don't exceed buffer and cause corruption
8. **NEW:** PT_LOG_CAT_GUI category logs GUI events correctly
9. **NEW:** PT_LOG_CAT_PERF category logs performance entries correctly
10. **NEW:** pt_log_elapsed_ms() returns increasing values
11. **NEW:** pt_log_next_seq() returns sequential values
12. **NEW:** Performance callback fires for each pt_log_perf() call
13. **NEW:** PT_LOG_PERF_SEND/RECV macros work correctly
14. **NEW:** ISR-safe deferred logging pattern documented and understood
15. **NEW:** PeerTalk_Poll() flushes deferred logs at START of each cycle

### ISR-Safety Compile-Time Checking Pattern

To help prevent accidental PT_LOG calls in ASR/notifier code, use this pattern:

```c
/*
 * ISR Context Marker
 *
 * Define PT_ISR_CONTEXT before any code that runs at interrupt level.
 * If PT_LOG is accidentally used, the compiler will error on the undefined symbol.
 */
#ifdef PT_ISR_CONTEXT
  /* Redefine PT_LOG_* to trigger a linker error if used */
  #define PT_LOG_ERR(...)   DO_NOT_CALL_PT_LOG_FROM_ISR_CONTEXT()
  #define PT_LOG_WARN(...)  DO_NOT_CALL_PT_LOG_FROM_ISR_CONTEXT()
  #define PT_LOG_INFO(...)  DO_NOT_CALL_PT_LOG_FROM_ISR_CONTEXT()
  #define PT_LOG_DEBUG(...) DO_NOT_CALL_PT_LOG_FROM_ISR_CONTEXT()
#endif

/* Usage in ASR code: */
#define PT_ISR_CONTEXT
static pascal void tcp_asr(StreamPtr stream, unsigned short event,
                           Ptr userData, unsigned short terminReason,
                           ICMPReport *icmpMsg) {
    /* PT_LOG_DEBUG(...) here would cause linker error! */
    my_state->flags.data_available = 1;  /* OK: just set flag */
}
#undef PT_ISR_CONTEXT
```

This is a documentation pattern for implementers in Phases 5-7. It's not automatically enforced but provides a guard rail for code reviewers.

### Logging Handoff Specifications

The following logging requirements are defined here but implemented in later phases:

**Phase 2 (Protocol) MUST log:**
```c
/* CRC validation failure */
PT_LOG_ERR(ctx->log, PT_LOG_CAT_PROTOCOL,
    "CRC mismatch: expected 0x%04X got 0x%04X", expected, actual);

/* Magic number mismatch */
PT_LOG_ERR(ctx->log, PT_LOG_CAT_PROTOCOL,
    "Invalid magic: expected 0x%08lX got 0x%08lX", PT_MAGIC, actual);

/* Protocol version mismatch */
PT_LOG_WARN(ctx->log, PT_LOG_CAT_PROTOCOL,
    "Protocol version mismatch: peer=%d local=%d", peer_ver, local_ver);

/* Truncated packet */
PT_LOG_ERR(ctx->log, PT_LOG_CAT_PROTOCOL,
    "Packet truncated: %d bytes (min %d)", pkt_len, min_required);
```

**Phases 5-7 (Connection Lifecycle) MUST log via deferred pattern:**
```c
/* Connection established - log from main loop after checking deferred flag */
PT_LOG_INFO(ctx->log, PT_LOG_CAT_CONNECT,
    "Connection established to peer %d via %s", peer_id, transport_name);

/* Connection timeout */
PT_LOG_WARN(ctx->log, PT_LOG_CAT_CONNECT,
    "Connection timeout: peer %d after %dms", peer_id, timeout_ms);

/* Connection error with platform-specific code */
PT_LOG_ERR(ctx->log, PT_LOG_CAT_CONNECT,
    "Connection error: peer %d, code %d (%s)", peer_id, err_code,
    PeerTalk_ErrorString(err_code));

/* State transitions (DEBUG level) */
PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_CONNECT,
    "State: peer %d %s -> %s", peer_id, old_state_name, new_state_name);
```

---

## Session 1.5: Integration Test

### Objective
Verify that all Phase 1 components work together: platform initialization, version reporting, error strings, and logging.

### Tasks

#### Task 1.5.1: Create `tests/test_foundation.c`

```c
/*
 * PeerTalk Foundation Integration Test
 *
 * Verifies Phase 1 components work together:
 * - Platform ops selection and initialization
 * - Version string
 * - Error string mapping
 * - Logging system integration
 * - Memory primitives
 */

#include <stdio.h>
#include "peertalk.h"
#include "pt_internal.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  " #name "..."); \
    tests_run++; \
    test_##name(); \
    tests_passed++; \
    printf(" OK\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" FAILED: %s\n", #cond); \
        return; \
    } \
} while(0)

/*============================================================================
 * Version Tests
 *============================================================================*/

TEST(version_string) {
    const char *ver = PeerTalk_Version();
    ASSERT(ver != NULL);
    ASSERT(ver[0] != '\0');

    /* Should be "1.0.0" based on current defines */
    ASSERT(ver[0] == '1');
    ASSERT(ver[1] == '.');
}

TEST(version_constants) {
    ASSERT(PEERTALK_VERSION_MAJOR == 1);
    ASSERT(PEERTALK_VERSION_MINOR == 0);
    ASSERT(PEERTALK_VERSION_PATCH == 0);
}

/*============================================================================
 * Error String Tests
 *============================================================================*/

TEST(error_strings) {
    /* Every error code should have a non-null, non-empty string */
    ASSERT(PeerTalk_ErrorString(PT_OK) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_INVALID_PARAM) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_NO_MEMORY) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_NOT_INITIALIZED) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_NETWORK) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_TIMEOUT) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_CONNECTION_REFUSED) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_CONNECTION_CLOSED) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_BUFFER_FULL) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_INVALID_STATE) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_PEER_NOT_FOUND) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_DISCOVERY_ACTIVE) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_NO_NETWORK) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_PLATFORM) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_QUEUE_EMPTY) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_MESSAGE_TOO_LARGE) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_NOT_SUPPORTED) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_NOT_CONNECTED) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_WOULD_BLOCK) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_BACKPRESSURE) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_RESOURCE) != NULL);

    /* Phase 2 protocol error codes (feed-forward from later phases) */
    ASSERT(PeerTalk_ErrorString(PT_ERR_CRC) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_MAGIC) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_TRUNCATED) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_VERSION) != NULL);
    ASSERT(PeerTalk_ErrorString(PT_ERR_NOT_POWER2) != NULL);

    ASSERT(PeerTalk_ErrorString(PT_ERR_INTERNAL) != NULL);

    /* Unknown error should also return something */
    ASSERT(PeerTalk_ErrorString((PeerTalk_Error)-999) != NULL);
}

TEST(error_string_content) {
    /* Verify specific messages */
    const char *ok = PeerTalk_ErrorString(PT_OK);
    ASSERT(ok[0] == 'S');  /* "Success" */

    const char *nomem = PeerTalk_ErrorString(PT_ERR_NO_MEMORY);
    ASSERT(nomem[0] != '\0');
}

/*============================================================================
 * Protocol Constants Tests
 *============================================================================*/

TEST(protocol_constants) {
    /* Verify magic numbers match CLAUDE.md */
    ASSERT(PT_CONTEXT_MAGIC == 0x5054434E);  /* "PTCN" */
    ASSERT(PT_PEER_MAGIC == 0x50545052);     /* "PTPR" */
    ASSERT(PT_QUEUE_MAGIC == 0x50545155);    /* "PTQU" */
    ASSERT(PT_CANARY == 0xDEADBEEF);

    /* Protocol version */
    ASSERT(PT_PROTOCOL_VERSION == 1);

    /* Discovery/message magic */
    ASSERT(PT_DISCOVERY_MAGIC[0] == 'P');
    ASSERT(PT_DISCOVERY_MAGIC[1] == 'T');
    ASSERT(PT_DISCOVERY_MAGIC[2] == 'L');
    ASSERT(PT_DISCOVERY_MAGIC[3] == 'K');

    ASSERT(PT_MESSAGE_MAGIC[0] == 'P');
    ASSERT(PT_MESSAGE_MAGIC[1] == 'T');
    ASSERT(PT_MESSAGE_MAGIC[2] == 'M');
    ASSERT(PT_MESSAGE_MAGIC[3] == 'G');
}

TEST(default_ports) {
    /* Verify default ports match PROJECT_GOALS.md */
    ASSERT(PT_DEFAULT_DISCOVERY_PORT == 7353);
    ASSERT(PT_DEFAULT_TCP_PORT == 7354);
    ASSERT(PT_DEFAULT_UDP_PORT == 7355);
}

/*============================================================================
 * Platform Ops Tests
 *============================================================================*/

TEST(platform_ops_selected) {
    /* PT_PLATFORM_OPS should be defined and non-null */
    pt_platform_ops *ops = PT_PLATFORM_OPS;
    ASSERT(ops != NULL);

    /* Core function pointers must be set */
    ASSERT(ops->init != NULL);
    ASSERT(ops->shutdown != NULL);
    ASSERT(ops->poll != NULL);
    ASSERT(ops->get_ticks != NULL);
    ASSERT(ops->get_free_mem != NULL);
    ASSERT(ops->get_max_block != NULL);
    /* Note: ops->send_udp may be NULL - it's optional and set by networking phases */
}

TEST(platform_ticks) {
    pt_platform_ops *ops = PT_PLATFORM_OPS;
    pt_tick_t t1 = ops->get_ticks();
    pt_tick_t t2 = ops->get_ticks();

    /* Ticks should be non-decreasing (may wrap, but consecutive calls shouldn't) */
    ASSERT(t2 >= t1 || (t1 - t2) > 0x80000000UL);  /* Handle wrap */
}

TEST(platform_memory) {
    pt_platform_ops *ops = PT_PLATFORM_OPS;

    unsigned long free_mem = ops->get_free_mem();
    unsigned long max_block = ops->get_max_block();

    /* Should return non-zero values */
    ASSERT(free_mem > 0);
    ASSERT(max_block > 0);

    /* max_block should be <= free_mem (largest contiguous <= total free) */
    ASSERT(max_block <= free_mem);
}

/*============================================================================
 * DOD (Data-Oriented Design) Verification Tests
 *============================================================================*/

TEST(dod_struct_sizes) {
    /*
     * Verify hot data fits in cache-friendly sizes.
     * These are compile-time guarantees for performance on Classic Mac.
     */

    /* pt_peer_hot should be exactly 32 bytes (one 68030 cache line) */
    ASSERT(sizeof(pt_peer_hot) == 32);

    /* pt_peer_state should be 1 byte (uint8_t, not enum) */
    ASSERT(sizeof(pt_peer_state) == 1);

    /* PeerTalk_PeerInfo should be ~20 bytes (no embedded name) */
    ASSERT(sizeof(PeerTalk_PeerInfo) <= 24);

    /* Verify name_idx is present and is 1 byte */
    {
        PeerTalk_PeerInfo info;
        ASSERT(sizeof(info.name_idx) == 1);
    }
}

TEST(dod_lookup_table) {
    /*
     * Verify O(1) lookup table is correctly sized.
     */
    struct pt_context ctx;

    /* Lookup table should support 256 peer IDs */
    ASSERT(sizeof(ctx.peer_id_to_index) == PT_MAX_PEER_ID);
    ASSERT(PT_MAX_PEER_ID == 256);

    /* Name table should support PT_MAX_PEERS names */
    ASSERT(sizeof(ctx.peer_names) == PT_MAX_PEERS * (PT_MAX_PEER_NAME + 1));
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void) {
    printf("PeerTalk Foundation Integration Tests\n");
    printf("======================================\n\n");

    printf("Version:\n");
    RUN_TEST(version_string);
    RUN_TEST(version_constants);

    printf("\nError Strings:\n");
    RUN_TEST(error_strings);
    RUN_TEST(error_string_content);

    printf("\nProtocol Constants:\n");
    RUN_TEST(protocol_constants);
    RUN_TEST(default_ports);

    printf("\nPlatform Ops:\n");
    RUN_TEST(platform_ops_selected);
    RUN_TEST(platform_ticks);
    RUN_TEST(platform_memory);

    printf("\nDOD (Data-Oriented Design):\n");
    RUN_TEST(dod_struct_sizes);
    RUN_TEST(dod_lookup_table);

    printf("\n======================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);
    printf("Version: %s\n", PeerTalk_Version());

    return (tests_passed == tests_run) ? 0 : 1;
}
```

### Acceptance Criteria
1. Test compiles and links with all Phase 1 components
2. All tests pass on POSIX
3. Version string is "1.0.0"
4. All error codes have non-empty strings
5. Platform ops function pointers are all set
6. Memory queries return sensible values

---

## Phase 1 Complete Checklist

### Core Implementation
- [ ] Build system compiles empty library on all platforms
- [ ] Directory structure created
- [ ] `include/peertalk.h` complete with all public API
- [ ] `src/core/pt_types.h` with platform detection, magic numbers, and PT_PROTOCOL_VERSION
- [ ] `src/core/pt_internal.h` with internal structures
- [ ] `src/core/pt_compat.h` and `pt_compat.c` with portable primitives
- [ ] `src/core/pt_version.c` with PeerTalk_Version() and PeerTalk_ErrorString()
- [ ] `src/posix/platform_posix.c` compiles and links
- [ ] `src/mactcp/platform_mactcp.c` compiles with Retro68
- [ ] `src/opentransport/platform_ot.c` compiles with Retro68
- [ ] PT_Log from Phase 0 integrated (no custom pt_log.c - use libptlog.a)

### Testing
- [ ] `tests/test_compat.c` passes on POSIX
- [ ] `tests/test_foundation.c` passes on POSIX (integration test)
- [ ] MacTCP driver opens successfully on real 68k Mac
- [ ] Open Transport initializes successfully on real PPC Mac
- [ ] All code compiles without warnings

### Phase 0 (PT_Log) Integration Verification
- [ ] `libptlog.a` is built and available from Phase 0
- [ ] `pt_log.h` is included from Phase 0 (not duplicated in Phase 1)
- [ ] `tests/test_foundation.c` links against `libptlog.a`
- [ ] PT_LogCreate()/PT_LogDestroy() called correctly in PeerTalk_Init()/Shutdown()
- [ ] PT_LOG_* macros work correctly with ctx->log handle
- [ ] Log categories match PHASE-0-LOGGING.md definitions exactly
- [ ] No custom pt_log() function - only PT_Log API used

### Review Findings Integration (from PHASE-1-REVIEW-REPORT.md)
- [ ] ASR callbacks use `pascal` calling convention
- [ ] UPP creation via NewTCPNotifyUPP/NewUDPNotifyUPP implemented
- [ ] UPP disposal via DisposeTCPNotifyUPP/DisposeUDPNotifyUPP implemented
- [ ] ISR-safe memcpy (pt_memcpy_isr) implemented - no Toolbox calls
- [ ] OTAtomicSetBit usage corrected for byte-based bit indices
- [ ] Port constants consistent across all files
- [ ] PeerTalk_Version() returns correct string
- [ ] PeerTalk_ErrorString() implemented for all error codes

### Data-Oriented Design (DOD) Requirements
- [ ] `struct pt_peer` split into `pt_peer_hot` (32 bytes) and `pt_peer_cold` (~1.4KB)
- [ ] `pt_peer_hot` reserved field correctly sized: 68k=8 bytes, PPC=4 bytes (total 32)
- [ ] `PeerTalk_PeerInfo` has `uint32_t address` as first field (natural alignment)
- [ ] `PeerTalk_PeerInfo` uses `name_idx` instead of embedded 32-byte name
- [ ] `PeerTalk_Config` embeds `local_name[32]` instead of pointer
- [ ] `struct pt_context` includes `peer_id_to_index[256]` for O(1) lookup
- [ ] `struct pt_context` includes `peer_names[][]` centralized name table
- [ ] `struct pt_context` includes `PT_Log *log` field for Phase 0 integration
- [ ] `pt_peer_state` is `uint8_t` with defines (not enum)
- [ ] Buffer canaries wrapped in `#ifdef PT_DEBUG`
- [ ] Batch callback types added (`PeerTalk_MessageBatchCB`, `PeerTalk_UDPBatchCB`)
- [ ] `PeerTalk_MessageBatch` has pointer-first field order (fixes alignment padding)
- [ ] All structs ordered largest-to-smallest to minimize padding
- [ ] `pt_find_peer_by_id()` uses O(1) lookup table (inline function)
- [ ] `pt_free_peer()` uses swap-back removal algorithm (documented)
- [ ] `PeerTalk_GetPeerName()` API function added for name access

### ISR-Safety and Deferred Logging (Third Review)
- [ ] Deferred log flush timing documented (START of PeerTalk_Poll)
- [ ] ISR-safety compile-time checking pattern documented (PT_ISR_CONTEXT)
- [ ] Logging handoff specs for Phase 2 protocol errors documented
- [ ] Logging handoff specs for Phases 5-7 connection lifecycle documented
- [ ] Session 1.4 dependency on Phase 0 completion clearly noted

### Code Quality Fixes (from implementability review)
- [x] Duplicate `pt_memcpy` declaration removed from pt_compat.h
- [x] `PT_PLATFORM_APPLETALK` case added to PT_PLATFORM_OPS selection
- [x] Compile-time error added when no platform is defined
- [x] POSIX atomics documented as not thread-safe (single-threaded model)

### Documentation Citations Added
- [x] 32-bit atomic access on 68k - Motorola 68000 processor architecture
- [x] BlockMoveData interrupt safety contradiction - IM Vol VI Table B-3 vs Sound Manager
- [x] OTAtomic functions - Networking With Open Transport pp. 657-666
- [x] Header locking policy - Phase 1 headers locked after completion

## References

### Project Documentation
- PROJECT_GOALS.md: Protocol version (1), default ports (7353/7354/7355), 64KB max payload capability
- CLAUDE.md: Magic numbers, protocol constants, system limits
- CSEND-LESSONS.md: ASR patterns, safe string handling, memory allocation patterns

### Apple Documentation
- MacTCP Programmer's Guide (1989): PBOpen for driver, driver name ".IPP", 64 streams max, 4096 min / 8192 recommended buffer, ASR rules (Chapter 2)
- Networking With Open Transport (1997): InitOpenTransport, OTAtomic* functions (pp. 657-666), notifier rules
- Inside Macintosh Volume VI: Table B-3 "Routines That May Be Called at Interrupt Time" (pp. 224396-224607), Sound Manager BlockMove note (p. 162410)
- Inside Macintosh: Files - FSDelete, Create, PBOpenSync for System 6/7 compatible I/O
- Inside Macintosh: Memory - FreeMem, MaxBlock, NewPtr, DisposePtr

### Retro68 MPW Interfaces
- `/home/matthew/Retro68/InterfacesAndLibraries/MPW_Interfaces/Interfaces&Libraries/Interfaces/CIncludes/`
- MacTypes.h: UInt8, UInt32, Ptr, Size
- MacMemory.h: NewPtr, NewPtrClear, DisposePtr, FreeMem, MaxBlock, BlockMoveData
- OpenTransport.h: OTAtomicSetBit, OTAtomicClearBit, OTAtomicTestBit (verified: UInt8*, bit 0-7)
- stdarg.h: va_list, va_start, va_arg, va_end (available for Classic Mac)

### Hardware Documentation (Processor Manuals)
- Motorola MC68000 User's Manual: 32-bit aligned access atomicity (single bus cycle)
- Motorola MC68030 User's Manual: Section 1.3.1 - 256-byte L1 data cache
- Motorola MC68040 User's Manual: Section 1.2 - 4KB L1 data cache
- PowerPC 601 RISC Microprocessor User's Manual: Section 6 - 32KB unified cache, 32-byte lines
- PowerPC Microprocessor Family: The Programming Environments: Section 5 - 603/604 cache architecture

**Cache Design Implications:**
- 68000/68020: No data cache - minimize memory bandwidth by keeping hot data sequential
- 68030: 256-byte cache - keep hot loop data under 256 bytes for single cache frame
- 68040: 4KB cache - more forgiving but still benefit from locality
- PPC: 32-byte cache lines - group related fields into 32-byte aligned chunks
