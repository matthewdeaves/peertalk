/**
 * @file discovery_mactcp.c
 * @brief MacTCP UDP Discovery Protocol Implementation
 *
 * Discovery broadcasts for peer detection over UDP.
 * Uses the UDP stream from udp_mactcp.c.
 *
 * References:
 * - MacTCP Programmer's Guide (1989), Chapter 4: "UDP"
 */

#include "mactcp_defs.h"
#include "protocol.h"
#include "peer.h"
#include "pt_internal.h"
#include "pt_compat.h"

#if defined(PT_PLATFORM_MACTCP)

#include <OSUtils.h>  /* For TickCount() */

/* ========================================================================== */
/* External Accessors                                                         */
/* ========================================================================== */

/* From udp_mactcp.c */
extern int pt_mactcp_udp_create(struct pt_context *ctx, udp_port local_port);
extern int pt_mactcp_udp_release(struct pt_context *ctx);
extern int pt_mactcp_udp_send(struct pt_context *ctx,
                               ip_addr dest_ip, udp_port dest_port,
                               const void *data, unsigned short len);
extern int pt_mactcp_udp_recv(struct pt_context *ctx,
                               ip_addr *from_ip, udp_port *from_port,
                               void *data, unsigned short *len);

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */

#define DEFAULT_DISCOVERY_PORT 7353
#define DEFAULT_TCP_PORT       7354

#define DISCOVERY_PORT(ctx) \
    ((ctx)->config.discovery_port > 0 ? (ctx)->config.discovery_port : DEFAULT_DISCOVERY_PORT)

#define TCP_PORT(ctx) \
    ((ctx)->config.tcp_port > 0 ? (ctx)->config.tcp_port : DEFAULT_TCP_PORT)

/* ========================================================================== */
/* Helper Functions                                                           */
/* ========================================================================== */

/**
 * Get human-readable name for discovery packet type.
 */
static const char *pt_discovery_type_str(uint8_t type)
{
    switch (type) {
    case PT_DISC_TYPE_ANNOUNCE: return "ANNOUNCE";
    case PT_DISC_TYPE_QUERY:    return "QUERY";
    case PT_DISC_TYPE_GOODBYE:  return "GOODBYE";
    default:                    return "UNKNOWN";
    }
}

/* ========================================================================== */
/* Discovery Send                                                             */
/* ========================================================================== */

/**
 * Send discovery packet (announce, query, or goodbye).
 *
 * @param ctx   PeerTalk context
 * @param type  PT_DISC_TYPE_ANNOUNCE, PT_DISC_TYPE_QUERY, or PT_DISC_TYPE_GOODBYE
 * @return      0 on success, -1 on error
 */
int pt_mactcp_discovery_send(struct pt_context *ctx, uint8_t type)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_discovery_packet pkt;
    uint8_t buf[PT_DISCOVERY_MAX_SIZE];
    int len;
    ip_addr broadcast;

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
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_NETWORK,
            "Failed to encode discovery packet: %d", len);
        return -1;
    }

    /* Calculate broadcast address: (local_ip & net_mask) | ~net_mask */
    broadcast = (md->local_ip & md->net_mask) | ~md->net_mask;

    PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK,
        "Sending %s to %lu.%lu.%lu.%lu:%u",
        pt_discovery_type_str(type),
        (broadcast >> 24) & 0xFF,
        (broadcast >> 16) & 0xFF,
        (broadcast >> 8) & 0xFF,
        broadcast & 0xFF,
        (unsigned)DISCOVERY_PORT(ctx));

    return pt_mactcp_udp_send(ctx, broadcast, DISCOVERY_PORT(ctx), buf, (unsigned short)len);
}

/* ========================================================================== */
/* Discovery Start/Stop                                                       */
/* ========================================================================== */

/**
 * Start discovery - create UDP stream and send initial announce.
 *
 * @param ctx  PeerTalk context
 * @return     0 on success, -1 on error
 */
int pt_mactcp_discovery_start(struct pt_context *ctx)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    int result;

    /* Create UDP stream for discovery */
    result = pt_mactcp_udp_create(ctx, DISCOVERY_PORT(ctx));
    if (result < 0)
        return result;

    /* Send initial announcement */
    pt_mactcp_discovery_send(ctx, PT_DISC_TYPE_ANNOUNCE);

    md->last_announce_tick = (unsigned long)TickCount();

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_NETWORK,
        "Discovery started on port %u", (unsigned)DISCOVERY_PORT(ctx));

    return 0;
}

/**
 * Stop discovery - send goodbye and release UDP stream.
 *
 * @param ctx  PeerTalk context
 */
void pt_mactcp_discovery_stop(struct pt_context *ctx)
{
    /* Send goodbye before releasing stream */
    pt_mactcp_discovery_send(ctx, PT_DISC_TYPE_GOODBYE);

    /* Release UDP stream */
    pt_mactcp_udp_release(ctx);

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_NETWORK, "Discovery stopped");
}

/* ========================================================================== */
/* Discovery Poll                                                             */
/* ========================================================================== */

/**
 * Poll for discovery packets.
 *
 * Checks if ASR has flagged data arrival, receives the packet,
 * and processes it according to type.
 *
 * @param ctx  PeerTalk context
 * @return     1 if packet processed, 0 if no data, -1 on error
 */
int pt_mactcp_discovery_poll(struct pt_context *ctx)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    uint8_t buf[PT_DISCOVERY_MAX_SIZE];
    unsigned short len = sizeof(buf);
    ip_addr from_ip;
    udp_port from_port;
    pt_discovery_packet pkt;
    struct pt_peer *peer;
    int result;

    /* Try to receive - returns 0 if no data (ASR flag not set) */
    result = pt_mactcp_udp_recv(ctx, &from_ip, &from_port, buf, &len);
    if (result <= 0)
        return result;

    /* Ignore our own broadcasts */
    if (from_ip == md->local_ip)
        return 0;

    /* Decode packet */
    if (pt_discovery_decode(ctx, buf, len, &pkt) != 0) {
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK,
            "Invalid discovery packet from %lu.%lu.%lu.%lu",
            (from_ip >> 24) & 0xFF,
            (from_ip >> 16) & 0xFF,
            (from_ip >> 8) & 0xFF,
            from_ip & 0xFF);
        return 0;
    }

    PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK,
        "Discovery %s from \"%s\" at %lu.%lu.%lu.%lu:%u",
        pt_discovery_type_str(pkt.type),
        pkt.name,
        (from_ip >> 24) & 0xFF,
        (from_ip >> 16) & 0xFF,
        (from_ip >> 8) & 0xFF,
        from_ip & 0xFF,
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
        pt_mactcp_discovery_send(ctx, PT_DISC_TYPE_ANNOUNCE);
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
        /* Unknown type - ignore */
        break;
    }

    return 1;  /* Packet processed */
}

#endif /* PT_PLATFORM_MACTCP */
