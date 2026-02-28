/**
 * @file poll_appletalk.c
 * @brief AppleTalk Integration with PeerTalk
 *
 * Connects NBP discovery and ADSP connections to the main PeerTalk API.
 * Implements the main poll loop, periodic discovery, and startup/shutdown.
 *
 * References:
 * - Programming With AppleTalk (1996), Chapter 5: ADSP
 */

#include "at_defs.h"

#if defined(PT_PLATFORM_APPLETALK)

#include <string.h>

/* ========================================================================== */
/* Logging Macros                                                              */
/* ========================================================================== */

#define INT_LOG_ERR(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_ERR((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define INT_LOG_WARN(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_WARN((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define INT_LOG_INFO(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_INFO((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define INT_LOG_DEBUG(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_DEBUG((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)

/* ========================================================================== */
/* Poll Loop - Call Regularly from Main Application                            */
/* ========================================================================== */

/**
 * Poll all active connections and the listener.
 *
 * Uses bitmask iteration for O(active) polling on 68k.
 * Only touches hot data during iteration - cold accessed only on events.
 *
 * @param ctx  AppleTalk context
 */
void pt_at_poll(pt_at_context *ctx)
{
    pt_adsp_state old_state;
    pt_adsp_connection_hot *conn;
#if PT_MAX_PEERS <= 32
    uint32_t mask;
    int slot;
#else
    int i;
    int slot;
#endif

    if (!ctx || !ctx->drivers_open) return;

    /* ------------------------------------------------------------------ */
    /* Check listener for incoming connections                             */
    /* ------------------------------------------------------------------ */

    if (ctx->listener.state == PT_ADSP_LISTENING) {
        if (ctx->listener.flags & PT_AT_FLAG_ASYNC_COMPLETE) {
            ctx->listener.flags &= ~PT_AT_FLAG_ASYNC_COMPLETE;

            if (ctx->listener.async_result == noErr &&
                ctx->listener.connection_pending) {
                INT_LOG_INFO(ctx, "Incoming from %d.%d:%d",
                             (int)ctx->listener.remote_addr.aNet,
                             (int)ctx->listener.remote_addr.aNode,
                             (int)ctx->listener.remote_addr.aSocket);

                /* Accept the connection */
                conn = pt_adsp_alloc(ctx);
                if (conn) {
                    if (pt_adsp_listener_accept(ctx, conn) != noErr) {
                        pt_adsp_release(ctx, conn);
                    }
                } else {
                    /* No room - deny */
#if PT_MAX_PEERS <= 32
                    INT_LOG_WARN(ctx, "Pool full (%d/%d), denying",
                                 pt_popcount(ctx->active_mask),
                                 PT_MAX_PEERS);
#else
                    INT_LOG_WARN(ctx, "Pool full (%d/%d), denying",
                                 (int)ctx->active_count, PT_MAX_PEERS);
#endif
                    pt_adsp_listener_deny(ctx);
                }

                /* Re-arm listener for next connection */
                INT_LOG_DEBUG(ctx, "Re-arming listener");
                pt_adsp_listener_listen(ctx);
            } else if (ctx->listener.async_result != noErr) {
                INT_LOG_ERR(ctx, "Listener failed: %d",
                            (int)ctx->listener.async_result);
                ctx->listener.state = PT_ADSP_IDLE;
                pt_adsp_listener_listen(ctx);
            }
        }
    }

    /* ------------------------------------------------------------------ */
    /* Check all ACTIVE connections - O(active) not O(capacity)           */
    /* ------------------------------------------------------------------ */

#if PT_MAX_PEERS <= 32
    mask = ctx->active_mask;
    while (mask) {
        slot = pt_ffs(mask) - 1;
        mask &= ~(1UL << slot);
        conn = &ctx->connections[slot];
#else
    for (i = 0; i < ctx->active_count; i++) {
        slot = ctx->active_connections[i];
        conn = &ctx->connections[slot];
#endif

        old_state = conn->state;

        /* Check connecting state */
        if (conn->state == PT_ADSP_CONNECTING) {
            if (conn->flags & PT_AT_FLAG_ASYNC_COMPLETE) {
                conn->flags &= ~PT_AT_FLAG_ASYNC_COMPLETE;

                if (conn->async_result == noErr) {
                    conn->state = PT_ADSP_CONNECTED;
                    INT_LOG_INFO(ctx, "Connected to %d.%d:%d",
                                 (int)conn->remote_addr.aNet,
                                 (int)conn->remote_addr.aNode,
                                 (int)conn->remote_addr.aSocket);
                } else {
                    conn->state = PT_ADSP_ERROR;
                    INT_LOG_ERR(ctx, "Connect failed: %d.%d:%d err=%d",
                                (int)conn->remote_addr.aNet,
                                (int)conn->remote_addr.aNode,
                                (int)conn->remote_addr.aSocket,
                                (int)conn->async_result);
                }
            }
        }

        /* Check connected state for events */
        if (conn->state == PT_ADSP_CONNECTED) {
            /* Connection closed (from userRoutine via userFlags) */
            if (conn->flags & PT_AT_FLAG_CONNECTION_CLOSED) {
                conn->flags &= ~PT_AT_FLAG_CONNECTION_CLOSED;
                conn->state = PT_ADSP_CLOSING;
                INT_LOG_INFO(ctx, "Remote close from %d.%d:%d",
                             (int)conn->remote_addr.aNet,
                             (int)conn->remote_addr.aNode,
                             (int)conn->remote_addr.aSocket);
            }

            /* Attention message received */
            if (conn->flags & PT_AT_FLAG_ATTENTION) {
                conn->flags &= ~PT_AT_FLAG_ATTENTION;
                INT_LOG_DEBUG(ctx, "Attention from %d.%d:%d",
                              (int)conn->remote_addr.aNet,
                              (int)conn->remote_addr.aNode,
                              (int)conn->remote_addr.aSocket);
            }

            /* Forward reset */
            if (conn->flags & PT_AT_FLAG_FWD_RESET) {
                conn->flags &= ~PT_AT_FLAG_FWD_RESET;
                INT_LOG_WARN(ctx, "Forward reset from %d.%d:%d",
                             (int)conn->remote_addr.aNet,
                             (int)conn->remote_addr.aNode,
                             (int)conn->remote_addr.aSocket);
            }
        }

        /* Check closing state */
        if (conn->state == PT_ADSP_CLOSING) {
            if (conn->flags & PT_AT_FLAG_ASYNC_COMPLETE) {
                conn->flags &= ~PT_AT_FLAG_ASYNC_COMPLETE;

                INT_LOG_DEBUG(ctx, "Close complete for %d.%d:%d",
                              (int)conn->remote_addr.aNet,
                              (int)conn->remote_addr.aNode,
                              (int)conn->remote_addr.aSocket);

                pt_adsp_remove_ccb(ctx, conn);
                conn->state = PT_ADSP_UNUSED;
            }
        }

        /* Log state transitions */
        if (conn->state != old_state && conn->state != PT_ADSP_UNUSED) {
            INT_LOG_DEBUG(ctx, "Conn %d: %d -> %d",
                          slot, (int)old_state, (int)conn->state);
        }
    }
}

/* ========================================================================== */
/* Periodic NBP Discovery                                                      */
/*                                                                             */
/* Call periodically (e.g., every 5 seconds) to refresh peer list.             */
/* ========================================================================== */

/**
 * Perform NBP lookup and log changes.
 *
 * @param ctx  AppleTalk context
 */
void pt_at_discover(pt_at_context *ctx)
{
    int old_count;

    if (!ctx || !ctx->drivers_open) return;

    old_count = ctx->nbp.entry_count;

    pt_nbp_lookup(ctx);

    if (ctx->nbp.entry_count != old_count) {
        INT_LOG_INFO(ctx, "Discovery: %d peers (was %d)",
                     (int)ctx->nbp.entry_count, old_count);
    }
}

/* ========================================================================== */
/* Startup                                                                     */
/* ========================================================================== */

/**
 * Start AppleTalk networking.
 *
 * Initializes drivers, registers NBP name, starts listener.
 *
 * @param ctx         AppleTalk context
 * @param log         Logger instance
 * @param local_name  Our name for NBP registration
 * @param socket      Socket to listen on (0 = auto-assign)
 * @return            noErr on success, error code on failure
 */
int pt_at_start(pt_at_context *ctx, PT_Log *log,
                const char *local_name, short socket)
{
    int err;

    if (!ctx) return -1;

    INT_LOG_INFO(ctx, "Starting AppleTalk as '%s' socket=%d",
                 local_name, (int)socket);

    /* Initialize drivers */
    err = pt_at_init(ctx, log);
    if (err != noErr) {
        return err;
    }

    /* Register with NBP */
    err = pt_nbp_register(ctx, local_name, socket);
    if (err != noErr) {
        INT_LOG_ERR(ctx, "NBP registration failed, shutting down");
        pt_at_shutdown(ctx);
        return err;
    }

    /* Start connection listener */
    err = pt_adsp_listener_init(ctx, socket);
    if (err != noErr) {
        INT_LOG_ERR(ctx, "Listener init failed, shutting down");
        pt_nbp_unregister(ctx);
        pt_at_shutdown(ctx);
        return err;
    }

    err = pt_adsp_listener_listen(ctx);
    if (err != noErr) {
        INT_LOG_ERR(ctx, "Listener start failed, shutting down");
        pt_adsp_listener_remove(ctx);
        pt_nbp_unregister(ctx);
        pt_at_shutdown(ctx);
        return err;
    }

    INT_LOG_INFO(ctx, "AppleTalk networking started");
    return noErr;
}

/* ========================================================================== */
/* Shutdown                                                                    */
/* ========================================================================== */

/**
 * Stop AppleTalk networking.
 *
 * Closes all connections, removes listener, unregisters NBP, shuts down.
 *
 * @param ctx  AppleTalk context
 */
void pt_at_stop(pt_at_context *ctx)
{
    int conn_count = 0;
#if PT_MAX_PEERS <= 32
    uint32_t mask;
    int slot;
#else
    int i;
    int slot;
#endif

    if (!ctx) return;

    INT_LOG_INFO(ctx, "Stopping AppleTalk networking");

    /* Close all active connections - O(active) using bitmask */
#if PT_MAX_PEERS <= 32
    mask = ctx->active_mask;
    while (mask) {
        slot = pt_ffs(mask) - 1;
        mask &= ~(1UL << slot);

        pt_adsp_abort(ctx, &ctx->connections[slot]);
        pt_adsp_remove_ccb(ctx, &ctx->connections[slot]);
        conn_count++;
    }
    ctx->active_mask = 0;
#else
    for (i = 0; i < ctx->active_count; i++) {
        slot = ctx->active_connections[i];
        pt_adsp_abort(ctx, &ctx->connections[slot]);
        pt_adsp_remove_ccb(ctx, &ctx->connections[slot]);
        conn_count++;
    }
    ctx->active_count = 0;
#endif

    if (conn_count > 0) {
        INT_LOG_DEBUG(ctx, "Closed %d connections", conn_count);
    }

    /* Remove listener */
    pt_adsp_listener_remove(ctx);

    /* Unregister from NBP */
    pt_nbp_unregister(ctx);

    /* Shutdown drivers */
    pt_at_shutdown(ctx);

    INT_LOG_INFO(ctx, "AppleTalk networking stopped");
}

#endif /* PT_PLATFORM_APPLETALK */
