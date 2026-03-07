/*
 * pt_discovery.c -- PeerTalk peer discovery
 *
 * UDP broadcast-based LAN discovery. Peers announce themselves
 * every 2 seconds. Peers not seen for 10 seconds are removed.
 */

#include "pt_internal.h"

/* ------------------------------------------------------------------ */
/* Discovery broadcast                                                 */
/* ------------------------------------------------------------------ */

void pt_discovery_broadcast(PT_Context_Internal *ctx)
{
    unsigned char packet[PT_DISCOVERY_MAX];
    size_t namelen;
    size_t pktlen;

    /* Build discovery packet: 4B magic + 1B version + name + null */
    packet[0] = PT_MAGIC_0;
    packet[1] = PT_MAGIC_1;
    packet[2] = PT_MAGIC_2;
    packet[3] = PT_MAGIC_3;
    packet[4] = PT_WIRE_VERSION;

    namelen = strlen(ctx->name);
    if (namelen > PT_NAME_MAX) namelen = PT_NAME_MAX;
    memcpy(packet + PT_DISCOVERY_HEADER, ctx->name, namelen);
    packet[PT_DISCOVERY_HEADER + namelen] = '\0';
    pktlen = PT_DISCOVERY_HEADER + namelen + 1;

    ctx->platform_ops->udp_broadcast(ctx, PT_DISCOVERY_PORT,
                                     packet, pktlen);
}

/* ------------------------------------------------------------------ */
/* Discovery receive                                                   */
/* ------------------------------------------------------------------ */

void pt_discovery_receive(PT_Context_Internal *ctx,
                          const void *data, size_t len,
                          unsigned long source_ip)
{
    const unsigned char *pkt = (const unsigned char *)data;
    const char *name;
    size_t namelen;
    PT_Peer_Internal *peer;

    /* Validate minimum size: header + at least 1 byte name + null */
    if (len < PT_DISCOVERY_HEADER + 2) return;

    /* Validate magic */
    if (pkt[0] != PT_MAGIC_0 || pkt[1] != PT_MAGIC_1 ||
        pkt[2] != PT_MAGIC_2 || pkt[3] != PT_MAGIC_3) return;

    /* Validate version */
    if (pkt[4] != PT_WIRE_VERSION) return;

    /* Extract name — must find null terminator within received bytes */
    name = (const char *)(pkt + PT_DISCOVERY_HEADER);
    {
        size_t name_region = len - PT_DISCOVERY_HEADER;
        const char *nul = (const char *)memchr(name, '\0', name_region);
        if (!nul) return; /* no null terminator — discard */
        namelen = (size_t)(nul - name);
    }
    if (namelen == 0 || namelen > PT_NAME_MAX) return;

    /* Filter own IP */
    if (source_ip == ctx->local_ip) return;

    /* Find existing peer or create new one */
    peer = pt_find_peer_by_ip(ctx, source_ip);
    if (peer) {
        /* Update last_seen */
        peer->last_seen = ctx->current_time;
        /* Update name if it changed (rare) */
        if (strcmp(peer->name, name) != 0) {
            memcpy(peer->name, name, namelen + 1);
        }
        /* Re-fire on_discovered for disconnected peers (R43) */
        if (peer->state == PT_PEER_DISCONNECTED) {
            peer->state = PT_PEER_DISCOVERED;
            CLOG_INFO("Re-discovered disconnected peer: %s", peer->name);
            if (ctx->callbacks.on_peer_discovered) {
                ctx->callbacks.on_peer_discovered(
                    (PT_Peer *)peer,
                    ctx->callbacks.on_peer_discovered_data);
            }
        }
        return;
    }

    /* New peer -- allocate slot */
    peer = pt_alloc_peer(ctx);
    if (!peer) {
        pt_fire_error(ctx, PT_ERR_NO_ROOM,
                      "No peer slots for discovered peer");
        return;
    }

    peer->ip_addr = source_ip;
    pt_format_ip(source_ip, peer->addr_str);
    memcpy(peer->name, name, namelen + 1);
    peer->last_seen = ctx->current_time;
    peer->state = PT_PEER_DISCOVERED;
    ctx->peer_count++;

    CLOG_INFO("Discovered peer: %s", peer->name);

    if (ctx->callbacks.on_peer_discovered) {
        ctx->callbacks.on_peer_discovered(
            (PT_Peer *)peer,
            ctx->callbacks.on_peer_discovered_data);
    }
}

/* ------------------------------------------------------------------ */
/* Discovery timeout check                                             */
/* ------------------------------------------------------------------ */

void pt_discovery_check_timeouts(PT_Context_Internal *ctx)
{
    int i;

    if (!ctx->discovery_listening) return;

    for (i = 0; i < ctx->max_peers; i++) {
        if (!ctx->peers[i].in_use) continue;
        if (ctx->peers[i].state == PT_PEER_CONNECTED) continue;

        /* Check if peer has timed out (no discovery for 10s) */
        if (ctx->peers[i].last_seen > 0 &&
            ctx->current_time - ctx->peers[i].last_seen >=
                PT_DISCOVERY_TIMEOUT) {

            CLOG_INFO("Peer lost (timeout): %s", ctx->peers[i].name);

            if (ctx->callbacks.on_peer_lost) {
                ctx->callbacks.on_peer_lost(
                    (PT_Peer *)&ctx->peers[i],
                    ctx->callbacks.on_peer_lost_data);
            }

            /* Free slot */
            ctx->peers[i].in_use = 0;
            ctx->peer_count--;
        }
    }
}
