/**
 * @file discovery_ot.c
 * @brief Open Transport UDP Discovery Protocol Implementation
 *
 * Discovery broadcasts for peer detection over UDP.
 * Uses the UDP endpoint from udp_ot.c.
 *
 * Mirrors discovery_mactcp.c patterns adapted for OT API.
 *
 * References:
 * - Networking With Open Transport (1997), Chapter 7: "Connectionless"
 */

#include "ot_defs.h"
#include "protocol.h"
#include "peer.h"
#include "pt_internal.h"
#include "pt_compat.h"

#if defined(PT_PLATFORM_OT)

#include <OSUtils.h>  /* TickCount() - main loop only */

/* ========================================================================== */
/* External Functions (from udp_ot.c)                                          */
/* ========================================================================== */

extern int pt_ot_udp_create(struct pt_context *ctx, InetPort port);
extern int pt_ot_udp_send(struct pt_context *ctx,
                            InetHost dest_ip, InetPort dest_port,
                            const void *data, size_t len);
extern int pt_ot_udp_recv(struct pt_context *ctx,
                            InetHost *from_ip, InetPort *from_port,
                            void *data, size_t *len);
extern void pt_ot_udp_clear_error(struct pt_context *ctx);
extern void pt_ot_udp_close(struct pt_context *ctx);

/* ========================================================================== */
/* Constants                                                                   */
/* ========================================================================== */

#define DEFAULT_DISCOVERY_PORT 7353
#define DEFAULT_TCP_PORT       7354

#define DISCOVERY_PORT(ctx) \
    ((ctx)->config.discovery_port > 0 \
     ? (ctx)->config.discovery_port : DEFAULT_DISCOVERY_PORT)

#define TCP_PORT(ctx) \
    ((ctx)->config.tcp_port > 0 ? (ctx)->config.tcp_port : DEFAULT_TCP_PORT)

/* ========================================================================== */
/* Helper Functions                                                            */
/* ========================================================================== */

/**
 * Get human-readable name for discovery packet type.
 */
static const char *pt_ot_disc_type_str(uint8_t type)
{
    switch (type) {
    case PT_DISC_TYPE_ANNOUNCE: return "ANNOUNCE";
    case PT_DISC_TYPE_QUERY:    return "QUERY";
    case PT_DISC_TYPE_GOODBYE:  return "GOODBYE";
    default:                    return "UNKNOWN";
    }
}

/* ========================================================================== */
/* Discovery Send                                                              */
/* ========================================================================== */

/**
 * Send discovery packet (announce, query, or goodbye).
 *
 * Uses InetInterfaceInfo.fBroadcastAddr for the broadcast address
 * (retrieved at init and cached in pt_ot_data).
 *
 * @param ctx   PeerTalk context
 * @param type  PT_DISC_TYPE_ANNOUNCE, PT_DISC_TYPE_QUERY, or PT_DISC_TYPE_GOODBYE
 * @return      0 on success, -1 on error
 */
int pt_ot_discovery_send(struct pt_context *ctx, uint8_t type)
{
    pt_ot_data *od = pt_ot_get(ctx);
    pt_discovery_packet pkt;
    uint8_t buf[PT_DISCOVERY_MAX_SIZE];
    int len;
    InetHost broadcast;

    /* Build packet */
    pt_memset(&pkt, 0, sizeof(pkt));
    pkt.version = PT_PROTOCOL_VERSION;
    pkt.type = type;
    pkt.flags = PT_DISC_FLAG_ACCEPTING;
    pkt.sender_port = TCP_PORT(ctx);
    pkt.transports = PT_DISC_TRANSPORT_TCP;

    if (ctx->config.local_name[0] != '\0') {
        pt_strncpy(pkt.name, ctx->config.local_name, PT_PEER_NAME_MAX);
    } else {
        pt_strncpy(pkt.name, "PeerTalk", PT_PEER_NAME_MAX);
    }
    pkt.name[PT_PEER_NAME_MAX] = '\0';
    pkt.name_len = (uint8_t)pt_strlen(pkt.name);

    len = pt_discovery_encode(&pkt, buf, sizeof(buf));
    if (len < 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_NETWORK,
            "Failed to encode discovery packet: %d", len);
        return -1;
    }

    /* Calculate broadcast address from cached values */
    broadcast = (od->local_ip & od->net_mask) | ~od->net_mask;

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_NETWORK,
        "Sending %s to %lu.%lu.%lu.%lu:%u",
        pt_ot_disc_type_str(type),
        (unsigned long)(broadcast >> 24) & 0xFF,
        (unsigned long)(broadcast >> 16) & 0xFF,
        (unsigned long)(broadcast >> 8) & 0xFF,
        (unsigned long)broadcast & 0xFF,
        (unsigned)DISCOVERY_PORT(ctx));

    return pt_ot_udp_send(ctx, broadcast, DISCOVERY_PORT(ctx), buf, (size_t)len);
}

/* ========================================================================== */
/* Discovery Start / Stop                                                      */
/* ========================================================================== */

/**
 * Start discovery - create UDP endpoint and send initial announce.
 *
 * @param ctx  PeerTalk context
 * @return     0 on success, -1 on error
 */
int pt_ot_discovery_start(struct pt_context *ctx)
{
    pt_ot_data *od = pt_ot_get(ctx);
    int result;

    /* Create UDP endpoint for discovery */
    result = pt_ot_udp_create(ctx, DISCOVERY_PORT(ctx));
    if (result < 0)
        return result;

    /* Send initial announcement */
    pt_ot_discovery_send(ctx, PT_DISC_TYPE_ANNOUNCE);

    od->last_announce_tick = (unsigned long)TickCount();

    PT_CTX_INFO(ctx, PT_LOG_CAT_NETWORK,
        "Discovery started on port %u", (unsigned)DISCOVERY_PORT(ctx));

    return 0;
}

/**
 * Stop discovery - send goodbye and close UDP endpoint.
 *
 * @param ctx  PeerTalk context
 */
void pt_ot_discovery_stop(struct pt_context *ctx)
{
    /* Send goodbye before closing endpoint */
    pt_ot_discovery_send(ctx, PT_DISC_TYPE_GOODBYE);

    /* Close UDP endpoint */
    pt_ot_udp_close(ctx);

    PT_CTX_INFO(ctx, PT_LOG_CAT_NETWORK, "Discovery stopped");
}

/* ========================================================================== */
/* Discovery Poll                                                              */
/* ========================================================================== */

/**
 * Poll for discovery packets.
 *
 * Checks if notifier has flagged data arrival, receives the packet,
 * and processes it according to type.
 *
 * @param ctx  PeerTalk context
 * @return     1 if packet processed, 0 if no data, -1 on error
 */
int pt_ot_discovery_poll(struct pt_context *ctx)
{
    pt_ot_data *od = pt_ot_get(ctx);
    uint8_t buf[PT_DISCOVERY_MAX_SIZE];
    size_t len = sizeof(buf);
    InetHost from_ip;
    InetPort from_port;
    pt_discovery_packet pkt;
    struct pt_peer *peer;
    int result;

    /* Clear any pending UDP errors first */
    pt_ot_udp_clear_error(ctx);

    /* Try to receive - returns 0 if no data */
    result = pt_ot_udp_recv(ctx, &from_ip, &from_port, buf, &len);
    if (result <= 0)
        return result;

    /* Ignore our own broadcasts */
    if (from_ip == od->local_ip)
        return 0;

    /* Decode packet */
    if (pt_discovery_decode(ctx, buf, len, &pkt) != 0) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_NETWORK,
            "Invalid discovery packet from %lu.%lu.%lu.%lu",
            (unsigned long)(from_ip >> 24) & 0xFF,
            (unsigned long)(from_ip >> 16) & 0xFF,
            (unsigned long)(from_ip >> 8) & 0xFF,
            (unsigned long)from_ip & 0xFF);
        return 0;
    }

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_NETWORK,
        "Discovery %s from \"%s\" at %lu.%lu.%lu.%lu:%u",
        pt_ot_disc_type_str(pkt.type),
        pkt.name,
        (unsigned long)(from_ip >> 24) & 0xFF,
        (unsigned long)(from_ip >> 16) & 0xFF,
        (unsigned long)(from_ip >> 8) & 0xFF,
        (unsigned long)from_ip & 0xFF,
        (unsigned)pkt.sender_port);

    switch (pkt.type) {
    case PT_DISC_TYPE_ANNOUNCE:
        /* Create or update peer entry */
        peer = pt_peer_create(ctx, pkt.name, from_ip, pkt.sender_port);
        if (peer != NULL && ctx->callbacks.on_peer_discovered != NULL) {
            PeerTalk_PeerInfo info;
            pt_peer_get_info(peer, &info);
            ctx->callbacks.on_peer_discovered((PeerTalk_Context *)ctx,
                                              &info,
                                              ctx->callbacks.user_data);
        }
        break;

    case PT_DISC_TYPE_QUERY:
        /* Respond to query with our announcement */
        pt_ot_discovery_send(ctx, PT_DISC_TYPE_ANNOUNCE);
        break;

    case PT_DISC_TYPE_GOODBYE:
        /* Find and remove peer */
        peer = pt_peer_find_by_addr(ctx, from_ip, pkt.sender_port);
        if (peer != NULL) {
            if (ctx->callbacks.on_peer_lost != NULL) {
                ctx->callbacks.on_peer_lost((PeerTalk_Context *)ctx,
                                            peer->hot.id,
                                            ctx->callbacks.user_data);
            }
            pt_peer_destroy(ctx, peer);
        }
        break;

    default:
        break;
    }

    return 1;
}

#endif /* PT_PLATFORM_OT */
