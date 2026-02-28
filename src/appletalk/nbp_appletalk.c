/**
 * @file nbp_appletalk.c
 * @brief NBP Discovery for AppleTalk
 *
 * Uses Name Binding Protocol (NBP) for peer discovery.
 * - Register: Announce our presence as "name:PeerTalk@*"
 * - Lookup: Find all "=:PeerTalk@*" on the network
 * - Extract: Parse entity names from lookup results
 *
 * All large structures (lookup buffer, entity temps) are kept in
 * cold storage to avoid stack overflow on 68k.
 *
 * References:
 * - Inside Macintosh: Networking (NBP chapter)
 * - Programming With AppleTalk (1996), Chapter 3: NBP
 */

#include "at_defs.h"

#if defined(PT_PLATFORM_APPLETALK)

#include <MacMemory.h>
#include <string.h>

/* ========================================================================== */
/* Logging Macros                                                              */
/* ========================================================================== */

#define NBP_LOG_ERR(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_ERR((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define NBP_LOG_INFO(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_INFO((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define NBP_LOG_DEBUG(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_DEBUG((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)

/* ========================================================================== */
/* Name Registration                                                           */
/* ========================================================================== */

/**
 * Register our NBP name for AppleTalk discovery.
 *
 * Makes our service visible as "peer_name:PeerTalk@*".
 *
 * @param ctx        AppleTalk context
 * @param peer_name  Service name (C string, max 32 chars)
 * @param socket     Socket number to register on
 * @return           noErr on success, Mac OS error code on failure
 */
int pt_nbp_register(pt_at_context *ctx, const char *peer_name, short socket)
{
    OSErr err;
    MPPParamBlock pb;
    pt_nbp_state_cold *cold;

    if (!ctx || !peer_name || !ctx->cold) return -1;

    cold = PT_AT_NBP_COLD(ctx);

    NBP_LOG_DEBUG(ctx, "Registering NBP: %s on socket %d",
                  peer_name, (int)socket);

    /* Convert C string to Pascal string in cold storage */
    cold->local_name[0] = (unsigned char)strlen(peer_name);
    if (cold->local_name[0] > PT_NBP_OBJECT_MAX) {
        cold->local_name[0] = PT_NBP_OBJECT_MAX;
    }
    memcpy(&cold->local_name[1], peer_name, cold->local_name[0]);

    /* Build Names Table Entry (NTE) */
    NBPSetNTE(
        (Ptr)&cold->nte,
        cold->local_name,       /* Object: peer name */
        PT_NBP_TYPE,            /* Type: "PeerTalk" */
        "\p*",                  /* Zone: local zone */
        socket
    );

    /* Register the name */
    memset(&pb, 0, sizeof(pb));
    pb.NBPinterval = 8;         /* 8 ticks (~133ms) retry interval */
    pb.NBPcount = 3;            /* 3 retries */
    pb.NBPentityPtr = (Ptr)&cold->nte;
    pb.NBPverifyFlag = 1;       /* Verify name is unique */

    err = PRegisterName(&pb, false);  /* Synchronous */
    if (err != noErr) {
        NBP_LOG_ERR(ctx, "NBP registration failed for '%s': %d",
                    peer_name, (int)err);
        return err;
    }

    ctx->nbp.registered = true;
    NBP_LOG_INFO(ctx, "NBP registered: %s:PeerTalk@* socket=%d",
                 peer_name, (int)socket);
    return noErr;
}

/* ========================================================================== */
/* Name Unregistration                                                         */
/* ========================================================================== */

/**
 * Unregister our NBP name.
 *
 * @param ctx  AppleTalk context
 * @return     noErr on success
 */
int pt_nbp_unregister(pt_at_context *ctx)
{
    OSErr err;
    MPPParamBlock pb;
    pt_nbp_state_cold *cold;

    if (!ctx || !ctx->nbp.registered || !ctx->cold) return noErr;

    cold = PT_AT_NBP_COLD(ctx);

    NBP_LOG_DEBUG(ctx, "Unregistering NBP name");

    /* Build entity to remove using cold storage temp_entity */
    NBPSetEntity(
        (Ptr)&cold->temp_entity,
        cold->local_name,
        PT_NBP_TYPE,
        "\p*"
    );

    memset(&pb, 0, sizeof(pb));
    pb.NBPentityPtr = (Ptr)&cold->temp_entity;

    err = PRemoveName(&pb, false);  /* Synchronous */
    if (err != noErr) {
        NBP_LOG_ERR(ctx, "NBP unregister failed: %d", (int)err);
    } else {
        NBP_LOG_INFO(ctx, "NBP name unregistered");
    }

    ctx->nbp.registered = false;
    return err;
}

/* ========================================================================== */
/* Peer Lookup                                                                 */
/* ========================================================================== */

/**
 * Search for all PeerTalk peers on the network.
 *
 * Uses wildcard "=:PeerTalk@*" to find all services of our type.
 * Results stored in hot (address) and cold (name) arrays.
 *
 * @param ctx  AppleTalk context
 * @return     noErr on success, error code on failure
 */
int pt_nbp_lookup(pt_at_context *ctx)
{
    OSErr err;
    MPPParamBlock pb;
    pt_nbp_state_cold *cold;
    int i;

    if (!ctx || !ctx->cold) return -1;

    cold = PT_AT_NBP_COLD(ctx);

    NBP_LOG_DEBUG(ctx, "Starting NBP lookup");

    /* Build wildcard search entity */
    NBPSetEntity(
        (Ptr)&cold->temp_entity,
        "\p=",              /* Object: wildcard */
        PT_NBP_TYPE,        /* Type: "PeerTalk" */
        "\p*"               /* Zone: local zone */
    );

    /* Perform lookup */
    memset(&pb, 0, sizeof(pb));
    pb.NBPinterval = 4;                         /* 4 ticks retry */
    pb.NBPcount = 2;                            /* 2 retries */
    pb.NBPentityPtr = (Ptr)&cold->temp_entity;
    pb.NBPretBuffPtr = (Ptr)cold->lookup_buf;
    pb.NBPretBuffSize = PT_NBP_LOOKUP_BUF_SIZE;
    pb.NBPmaxToGet = PT_NBP_MAX_ENTRIES;

    err = PLookupName(&pb, false);  /* Synchronous */
    if (err != noErr && err != nbpNotFound) {
        NBP_LOG_ERR(ctx, "NBP lookup failed: %d", (int)err);
        ctx->nbp.entry_count = 0;
        return err;
    }

    /* Extract results */
    ctx->nbp.entry_count = 0;
    for (i = 1; i <= pb.NBPnumGotten && i <= PT_NBP_MAX_ENTRIES; i++) {
        AddrBlock addr;

        err = NBPExtract(
            (Ptr)cold->lookup_buf,
            pb.NBPnumGotten,
            i,
            &cold->temp_entity,  /* Reuse cold temp */
            &addr
        );

        if (err == noErr) {
            uint8_t slot = ctx->nbp.entry_count;

            /* Store hot data (address) */
            cold->entries[slot].slot_index = slot;
            cold->entries[slot].addr = addr;

            /* Store cold data (name) */
            pt_nbp_get_name(&cold->temp_entity,
                           cold->entry_names[slot].name,
                           PT_NBP_OBJECT_MAX + 1);

            ctx->nbp.entry_count++;
        }
    }

    NBP_LOG_DEBUG(ctx, "NBP lookup found %d peers",
                  (int)ctx->nbp.entry_count);
    return noErr;
}

/* ========================================================================== */
/* Peer Access Functions                                                        */
/* ========================================================================== */

/**
 * Get discovered peers (hot data only - addresses).
 *
 * @param ctx          AppleTalk context
 * @param entries      Output array for hot entries
 * @param max_entries  Max entries to copy
 * @return             Number of entries copied
 */
int pt_nbp_get_peers(pt_at_context *ctx, pt_nbp_entry_hot *entries,
                     int max_entries)
{
    int count;
    pt_nbp_state_cold *cold;

    if (!ctx || !entries || !ctx->cold) return 0;

    cold = PT_AT_NBP_COLD(ctx);
    count = ctx->nbp.entry_count;
    if (count > max_entries) count = max_entries;

    memcpy(entries, cold->entries, (size_t)count * sizeof(pt_nbp_entry_hot));
    return count;
}

/**
 * Get peer name from cold storage.
 *
 * @param ctx         AppleTalk context
 * @param slot_index  Slot index from hot entry
 * @param name_out    Output buffer for C string
 * @param max_len     Buffer size
 * @return            0 on success, -1 on error
 */
int pt_nbp_get_peer_name(pt_at_context *ctx, uint8_t slot_index,
                         char *name_out, int max_len)
{
    pt_nbp_state_cold *cold;

    if (!ctx || !ctx->cold || !name_out || max_len <= 0) return -1;
    if (slot_index >= ctx->nbp.entry_count) return -1;

    cold = PT_AT_NBP_COLD(ctx);
    strncpy(name_out, cold->entry_names[slot_index].name,
            (size_t)(max_len - 1));
    name_out[max_len - 1] = '\0';

    return 0;
}

/* ========================================================================== */
/* Entity Name Extraction                                                      */
/* ========================================================================== */

/**
 * Extract object name from EntityName to C string.
 *
 * EntityName.objStr is a Pascal string (length byte + data).
 *
 * @param entity    NBP entity
 * @param name_out  Output buffer for C string
 * @param max_len   Buffer size (including null terminator)
 */
void pt_nbp_get_name(const EntityName *entity, char *name_out, int max_len)
{
    int len;

    if (!entity || !name_out || max_len <= 0) return;

    /* EntityName.objStr is a Pascal string */
    len = entity->objStr[0];
    if (len >= max_len) len = max_len - 1;

    memcpy(name_out, &entity->objStr[1], (size_t)len);
    name_out[len] = '\0';
}

#endif /* PT_PLATFORM_APPLETALK */
