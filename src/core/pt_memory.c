/*
 * pt_memory.c -- PeerTalk memory allocation
 *
 * Single contiguous block allocated at init. Zero malloc after init.
 * The peers array is carved from the front of the block, followed by
 * per-peer buffers. Only one allocation is made (Principle V / R14).
 */

#include "pt_internal.h"

#include <stdlib.h> /* malloc, free */

#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
#include <Memory.h>
#endif

size_t pt_memory_calculate_size(int max_peers, size_t tcp_recv,
                                size_t tcp_send, size_t udp_buf,
                                size_t reassembly)
{
    size_t per_peer;
    size_t peers_array;

    per_peer = PT_PEER_METADATA + tcp_recv + tcp_send + udp_buf + reassembly;
    peers_array = (size_t)max_peers * sizeof(PT_Peer_Internal);
    return PT_GLOBAL_OVERHEAD + peers_array + ((size_t)max_peers * per_peer);
}

/* Classic Mac: determine buffer sizes from available memory */
#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)

/*
 * Platform buffer overhead: memory allocated by the network stack
 * per TCP stream/endpoint, plus fixed UDP overhead. These allocations
 * happen inside platform init (via TCPCreate/OTOpenEndpoint) and
 * must be accounted for in the FreeMem() budget. (R15)
 *
 * MacTCP: ~8 KB per TCP stream buffer + 2 KB per UDP stream (x2 UDP)
 * OT:     ~4 KB per TCP endpoint + 4 KB per UDP endpoint (x2 UDP)
 */
#if defined(PT_PLATFORM_MACTCP)
#define PT_PLATFORM_PER_PEER_OVERHEAD  8192  /* per TCP stream */
#define PT_PLATFORM_FIXED_OVERHEAD     4096  /* 2 UDP streams x 2 KB (T126) */
#else /* PT_PLATFORM_OT */
#define PT_PLATFORM_PER_PEER_OVERHEAD  4096  /* per TCP endpoint */
#define PT_PLATFORM_FIXED_OVERHEAD     8192  /* 2 UDP endpoints x 4 KB */
#endif

static void mac_size_from_memory(long avail,
                                 int *out_max_peers,
                                 size_t *out_tcp_recv,
                                 size_t *out_tcp_send,
                                 size_t *out_udp_buf,
                                 size_t *out_reassembly)
{
    size_t per_peer;
    long budget;

    if (avail >= 8L * 1024L * 1024L) {
        /* 8 MB+ free: generous sizing */
        *out_tcp_recv = 8192;
        *out_tcp_send = 4096;
        *out_reassembly = 65536;
    } else if (avail >= 2L * 1024L * 1024L) {
        /* 2-8 MB free: medium sizing */
        *out_tcp_recv = 4100;
        *out_tcp_send = 2048;
        *out_reassembly = 16384;
    } else {
        /* < 2 MB free: conservative sizing.
         * tcp_recv must be >= 4100 to hold a complete chunk frame from
         * a POSIX sender (tcp_send_size=4096) plus partial next header.
         * Without this, chunked messages from POSIX peers time out. (R21) */
        *out_tcp_recv = 4100;
        *out_tcp_send = 1024;
        *out_reassembly = 4096;
    }

    *out_udp_buf = 512;

    /* Subtract fixed platform overhead from available budget */
    budget = avail - PT_PLATFORM_FIXED_OVERHEAD;
    if (budget < (long)PT_GLOBAL_OVERHEAD) budget = (long)PT_GLOBAL_OVERHEAD + 1;

    /* Include platform per-peer overhead in the per-peer cost */
    per_peer = sizeof(PT_Peer_Internal) + PT_PEER_METADATA +
               *out_tcp_recv + *out_tcp_send +
               *out_udp_buf + *out_reassembly +
               PT_PLATFORM_PER_PEER_OVERHEAD;

    *out_max_peers = (int)(((size_t)budget - PT_GLOBAL_OVERHEAD) / per_peer);
    if (*out_max_peers < 2) *out_max_peers = 2;
    if (*out_max_peers > 32) *out_max_peers = 32;
}
#endif

int pt_memory_allocate(PT_Context_Internal *ctx,
                       int max_peers, size_t tcp_recv,
                       size_t tcp_send, size_t udp_buf,
                       size_t reassembly)
{
    size_t total;
    unsigned char *block;
    unsigned char *ptr;
    int i;

#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
    /* MaxApplZone/MoreMasters already called in PT_Init (R17).
     * Safe to call FreeMem() here — heap is fully extended. */

    /* Override caller's defaults with sizes based on FreeMem() */
    {
        long free_mem = FreeMem();
        long avail = (free_mem * 3) / 4; /* use 75% conservatively */
        CLOG_INFO("FreeMem() = %ld, using %ld for PeerTalk", free_mem, avail);
        mac_size_from_memory(avail, &max_peers, &tcp_recv,
                             &tcp_send, &udp_buf, &reassembly);
        CLOG_INFO("Mac sizing: %d peers, tcp_recv=%lu, tcp_send=%lu, "
                  "reassembly=%lu",
                  max_peers, (unsigned long)tcp_recv,
                  (unsigned long)tcp_send, (unsigned long)reassembly);
    }
#endif

    total = pt_memory_calculate_size(max_peers, tcp_recv, tcp_send,
                                     udp_buf, reassembly);

#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
    block = (unsigned char *)NewPtrClear((Size)total);
#else
    block = (unsigned char *)malloc(total);
    if (block) memset(block, 0, total);
#endif
    if (!block) {
        return -1;
    }

    ctx->memory_block = block;
    ctx->memory_size = total;
    ctx->max_peers = max_peers;

    /* Carve peers array from the front of the block (after global overhead) */
    ptr = block + PT_GLOBAL_OVERHEAD;
    ctx->peers = (PT_Peer_Internal *)ptr;
    ptr += (size_t)max_peers * sizeof(PT_Peer_Internal);

    /* Assign per-peer buffers from the rest of the contiguous block */
    for (i = 0; i < max_peers; i++) {
        ctx->peers[i].in_use = 0;
        ctx->peers[i].state = PT_PEER_DISCONNECTED;

        ctx->peers[i].tcp_recv_buf = ptr;
        ctx->peers[i].tcp_recv_size = tcp_recv;
        ctx->peers[i].tcp_recv_len = 0;
        ptr += tcp_recv;

        ctx->peers[i].tcp_send_buf = ptr;
        ctx->peers[i].tcp_send_size = tcp_send;
        ptr += tcp_send;

        ctx->peers[i].udp_buf = ptr;
        ctx->peers[i].udp_buf_size = udp_buf;
        ptr += udp_buf;

        ctx->peers[i].reassembly_buf = ptr;
        ctx->peers[i].reassembly_buf_size = reassembly;
        ctx->peers[i].reassembly_total = 0;
        ctx->peers[i].reassembly_received = 0;
        ptr += reassembly;

        /* Skip metadata padding */
        ptr += PT_PEER_METADATA;

#if defined(PT_PLATFORM_POSIX)
        ctx->peers[i].platform_peer.tcp_fd = -1;
#elif defined(PT_PLATFORM_MACTCP)
        ctx->peers[i].platform_peer.tcp_stream = NULL;
#elif defined(PT_PLATFORM_OT)
        ctx->peers[i].platform_peer.endpoint = NULL;
        ctx->peers[i].platform_peer.events = 0;
#endif
    }

    return 0;
}

void pt_memory_free(PT_Context_Internal *ctx)
{
    /* peers array is inside memory_block — just NULL the pointer */
    ctx->peers = NULL;

    if (ctx->memory_block) {
#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
        DisposePtr((Ptr)ctx->memory_block);
#else
        free(ctx->memory_block);
#endif
        ctx->memory_block = NULL;
    }
    ctx->memory_size = 0;
    ctx->max_peers = 0;
    ctx->peer_count = 0;
}
