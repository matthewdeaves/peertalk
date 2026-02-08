/*
 * PeerTalk Internal Types
 * Platform detection, magic numbers, and internal type definitions
 */

#ifndef PT_TYPES_H
#define PT_TYPES_H

#include <stddef.h>  /* For NULL, size_t */
#include "peertalk.h"

/* ========================================================================== */
/* Platform Detection                                                         */
/* ========================================================================== */

/*
 * Primary platform macros (mutually exclusive, exactly one must be defined):
 *   PT_PLATFORM_POSIX      - Linux/macOS (modern systems)
 *   PT_PLATFORM_MACTCP     - Classic Mac with MacTCP (68k, System 6-7.5)
 *   PT_PLATFORM_OT         - Classic Mac with Open Transport (PPC, 7.6.1+)
 *   PT_PLATFORM_APPLETALK  - AppleTalk-only build (any Classic Mac)
 *
 * Optional secondary macros:
 *   PT_HAS_APPLETALK       - For unified builds (MacTCP+AT or OT+AT)
 */

/* Validate platform detection */
#if !defined(PT_PLATFORM_POSIX) && \
    !defined(PT_PLATFORM_MACTCP) && \
    !defined(PT_PLATFORM_OT) && \
    !defined(PT_PLATFORM_APPLETALK)
    #error "No platform defined. Define one of: PT_PLATFORM_POSIX, PT_PLATFORM_MACTCP, PT_PLATFORM_OT, PT_PLATFORM_APPLETALK"
#endif

/* Ensure mutual exclusivity of primary platforms */
#if (defined(PT_PLATFORM_POSIX) + defined(PT_PLATFORM_MACTCP) + \
     defined(PT_PLATFORM_OT) + defined(PT_PLATFORM_APPLETALK)) > 1
    #error "Multiple primary platforms defined. Only one of POSIX, MACTCP, OT, APPLETALK allowed"
#endif

/* POSIX cannot be combined with Classic Mac platforms */
#if defined(PT_PLATFORM_POSIX) && \
    (defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT) || \
     defined(PT_PLATFORM_APPLETALK) || defined(PT_HAS_APPLETALK))
    #error "PT_PLATFORM_POSIX cannot be combined with Classic Mac platforms"
#endif

/* Secondary platform detection */
#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT) || defined(PT_PLATFORM_APPLETALK)
    #define PT_PLATFORM_CLASSIC_MAC 1
#endif

#if defined(PT_PLATFORM_POSIX) || defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
    #define PT_HAS_TCPIP 1
#endif

#if (defined(PT_PLATFORM_MACTCP) && defined(PT_HAS_APPLETALK)) || \
    (defined(PT_PLATFORM_OT) && defined(PT_HAS_APPLETALK)) || \
    defined(PT_PLATFORM_APPLETALK)
    #define PT_MULTI_TRANSPORT 1
#endif

/* ========================================================================== */
/* Magic Numbers (from CLAUDE.md - DO NOT CHANGE)                            */
/* ========================================================================== */

#define PT_CONTEXT_MAGIC    0x5054434E  /* "PTCN" - context validation */
#define PT_PEER_MAGIC       0x50545052  /* "PTPR" - peer validation */
#define PT_QUEUE_MAGIC      0x50545155  /* "PTQU" - queue validation */
#define PT_CANARY           0xDEADBEEF  /* Buffer overflow detection */

/* Buffer canaries (Phase 2 - debug builds only) */
#ifdef PT_DEBUG
    #define PT_CANARY_OBUF  0xDEAD0B0F  /* Output buffer canary */
    #define PT_CANARY_IBUF  0xDEAD1B1F  /* Input buffer canary */
#endif

/* ========================================================================== */
/* Protocol Constants                                                         */
/* ========================================================================== */

#define PT_PROTOCOL_VERSION 1           /* Wire protocol version */
#define PT_DISCOVERY_MAGIC  "PTLK"      /* UDP discovery packets */
#define PT_MESSAGE_MAGIC    "PTMG"      /* TCP message frames */

/**
 * Framing buffer size for peer I/O.
 *
 * This is the staging buffer for TCP receive/send framing. Must be large
 * enough to hold the largest expected message plus header (10) + CRC (2).
 *
 * 8192 bytes balances throughput (fewer copies) with memory usage.
 * MacTCP recommends 16KB for block apps, but 8KB is sufficient for
 * most messages and reduces memory pressure on 4-8MB Macs.
 *
 * With PT_MAX_PEERS=8, ibuf+obuf = 16KB per peer = 128KB total.
 */
#define PT_FRAME_BUF_SIZE   8192

/* ========================================================================== */
/* Internal Types                                                             */
/* ========================================================================== */

/**
 * Platform-neutral tick count (milliseconds)
 */
typedef uint32_t pt_tick_t;

/**
 * Connection state (uint8_t for memory savings)
 */
typedef uint8_t pt_peer_state;

#define PT_PEER_STATE_UNUSED        0   /* Slot available for allocation (Phase 2) */
#define PT_PEER_STATE_DISCOVERED    1   /* Discovered but not connected */
#define PT_PEER_STATE_CONNECTING    2   /* Connection in progress */
#define PT_PEER_STATE_CONNECTED     3   /* Fully connected */
#define PT_PEER_STATE_DISCONNECTING 4   /* Disconnect in progress */
#define PT_PEER_STATE_FAILED        5   /* Connection failed */

/* ========================================================================== */
/* Forward Declarations                                                       */
/* ========================================================================== */

struct pt_context;
struct pt_peer;
struct pt_queue;

/*
 * NOTE: Validation helper functions are defined in pt_internal.h after
 * struct definitions are complete. Validation macros are used in implementation
 * files only, not in headers.
 */

#endif /* PT_TYPES_H */
