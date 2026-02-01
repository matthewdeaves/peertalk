/*
 * PeerTalk Version and Utility Functions
 */

#include "peertalk.h"
#include "pt_types.h"
#include "pt_internal.h"

/* ========================================================================== */
/* Version Information                                                        */
/* ========================================================================== */

const char *PeerTalk_Version(void)
{
    return PT_VERSION_STRING;
}

/* ========================================================================== */
/* Error Strings                                                              */
/* ========================================================================== */

const char *PeerTalk_ErrorString(PeerTalk_Error error)
{
    switch (error) {
    case PT_OK:
        return "Success";

    /* Parameter & State Errors */
    case PT_ERR_INVALID_PARAM:
        return "Invalid parameter";
    case PT_ERR_NO_MEMORY:
        return "Out of memory";
    case PT_ERR_NOT_INITIALIZED:
        return "Not initialized";
    case PT_ERR_ALREADY_INITIALIZED:
        return "Already initialized";
    case PT_ERR_INVALID_STATE:
        return "Invalid state";
    case PT_ERR_NOT_SUPPORTED:
        return "Not supported on this platform";

    /* Network Errors */
    case PT_ERR_NETWORK:
        return "Network error";
    case PT_ERR_TIMEOUT:
        return "Operation timed out";
    case PT_ERR_CONNECTION_REFUSED:
        return "Connection refused";
    case PT_ERR_CONNECTION_CLOSED:
        return "Connection closed";
    case PT_ERR_NO_NETWORK:
        return "No network available";
    case PT_ERR_NOT_CONNECTED:
        return "Not connected";
    case PT_ERR_WOULD_BLOCK:
        return "Operation would block";

    /* Buffer & Queue Errors */
    case PT_ERR_BUFFER_FULL:
        return "Buffer full";
    case PT_ERR_QUEUE_EMPTY:
        return "Queue empty";
    case PT_ERR_MESSAGE_TOO_LARGE:
        return "Message too large";
    case PT_ERR_BACKPRESSURE:
        return "Send backpressure (slow peer)";

    /* Peer Errors */
    case PT_ERR_PEER_NOT_FOUND:
        return "Peer not found";
    case PT_ERR_DISCOVERY_ACTIVE:
        return "Discovery already active";

    /* Protocol Errors (Phase 2) */
    case PT_ERR_CRC:
        return "CRC validation failed";
    case PT_ERR_MAGIC:
        return "Invalid magic number";
    case PT_ERR_TRUNCATED:
        return "Truncated message";
    case PT_ERR_VERSION:
        return "Protocol version mismatch";
    case PT_ERR_NOT_POWER2:
        return "Size must be power of 2";

    /* System Errors */
    case PT_ERR_PLATFORM:
        return "Platform-specific error";
    case PT_ERR_RESOURCE:
        return "Resource exhausted";
    case PT_ERR_INTERNAL:
        return "Internal error";

    default:
        return "Unknown error";
    }
}

/* ========================================================================== */
/* Available Transports                                                       */
/* ========================================================================== */

uint16_t PeerTalk_GetAvailableTransports(void)
{
    uint16_t transports = 0;

#ifdef PT_HAS_TCPIP
    transports |= PT_TRANSPORT_TCP | PT_TRANSPORT_UDP;
#endif

#if defined(PT_HAS_APPLETALK) || defined(PT_PLATFORM_APPLETALK)
    transports |= PT_TRANSPORT_APPLETALK;  /* ADSP | NBP */
#endif

    return transports;
}

/* ========================================================================== */
/* Peer Name Lookup                                                           */
/* ========================================================================== */

const char *PeerTalk_GetPeerName(PeerTalk_Context *ctx, uint8_t name_idx)
{
    if (!pt_context_valid(ctx))
        return NULL;

    return pt_get_peer_name(ctx, name_idx);
}
