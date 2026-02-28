/**
 * @file ot_nbp.c
 * @brief Open Transport NBP (Name Binding Protocol) Discovery
 *
 * Implements AppleTalk service discovery using OT's mapper API.
 * Provides registration (making our service visible) and lookup
 * (finding other PeerTalk services on the network).
 *
 * NBP uses a mapper provider (OTOpenMapper), not an endpoint:
 * - OTOpenMapper("nbp") - create mapper
 * - OTRegisterName() - announce our presence
 * - OTLookupName() - find other services
 * - OTDeleteNameByID() - unregister
 *
 * The lookup reply buffer is pre-allocated in pt_nbp_mapper (2KB)
 * rather than on the stack to prevent stack overflow on 68k Macs
 * with limited stack space (~32KB typical).
 *
 * References:
 * - Networking With Open Transport (1997), Chapter 9: AppleTalk Services
 * - OpenTransportProviders.h (NBPEntity, DDPNBPAddress, etc.)
 */

#include "ot_multi.h"
#include "pt_internal.h"
#include "pt_compat.h"

#if defined(PT_PLATFORM_OT)

/* ========================================================================== */
/* NBP Mapper Initialization                                                  */
/* ========================================================================== */

/**
 * Initialize NBP mapper.
 *
 * Opens an NBP mapper provider for name registration and lookup.
 * Only ONE mapper is created per context (not per-peer), so we pass
 * OTCreateConfiguration() directly instead of caching and cloning.
 *
 * @param ctx  PeerTalk context
 * @return     0 on success, -1 on failure
 */
int pt_ot_nbp_init(struct pt_context *ctx)
{
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    OSStatus err;

    md->nbp.ref = OTOpenMapper(
        OTCreateConfiguration("nbp"),
        0,
        &err);

    if (err != kOTNoError || md->nbp.ref == NULL) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_DISCOVERY,
            "OTOpenMapper(nbp) failed: %ld", (long)err);
        md->nbp.ref = NULL;
        return -1;
    }

    md->nbp.registered = false;
    md->nbp.lookup_pending = false;
    md->nbp.lookup_complete = false;
    md->nbp.lookup_count = 0;

    PT_CTX_INFO(ctx, PT_LOG_CAT_DISCOVERY, "NBP mapper initialized");
    return 0;
}

/* ========================================================================== */
/* NBP Name Registration                                                      */
/* ========================================================================== */

/**
 * Register our NBP name for AppleTalk discovery.
 *
 * Makes our service visible to OTLookupName from other Macs.
 * Format: "name:type@zone" e.g., "Alice's Mac:PeerTalk@*"
 *
 * Uses DDPNBPAddress which combines a DDP socket address with an
 * NBP name. The bound_addr parameter provides the DDP socket from
 * our ADSP listener endpoint.
 *
 * @param ctx         PeerTalk context
 * @param name        Service name (e.g., computer name)
 * @param type        NBP type (e.g., "PeerTalk")
 * @param zone        NBP zone ("*" for all zones)
 * @param bound_addr  DDP address from bound ADSP listener
 * @return            0 on success, -1 on failure
 */
int pt_ot_nbp_register(struct pt_context *ctx,
                          const char *name,
                          const char *type,
                          const char *zone,
                          DDPAddress *bound_addr)
{
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    TRegisterRequest req;
    TRegisterReply reply;
    DDPNBPAddress addr;
    char entity_str[128];
    OTByteCount name_len;
    OSStatus err;

    if (md->nbp.ref == NULL)
        return -1;

    if (md->nbp.registered)
        return 0;  /* Already registered */

    /* Build entity string: "name:type@zone" */
    pt_snprintf(entity_str, sizeof(entity_str), "%s:%s@%s",
                name, type, zone ? zone : "*");

    /* Setup combined DDP+NBP address.
     * DDPNBPAddress contains both the DDP socket address and
     * the NBP entity name, allowing OT to bind the name to
     * our ADSP listener socket. */
    addr.fAddressType = AF_ATALK_DDPNBP;
    addr.fNetwork = bound_addr->fNetwork;
    addr.fNodeID = bound_addr->fNodeID;
    addr.fSocket = bound_addr->fSocket;
    addr.fDDPType = 7;  /* ADSP */
    addr.fPad = 0;
    name_len = OTSetAddressFromNBPString(addr.fNBPNameBuffer,
                                           entity_str, -1);

    /* Setup registration request */
    req.name.buf = (UInt8 *)&addr;
    req.name.len = sizeof(DDPNBPAddress);
    req.addr.buf = (UInt8 *)bound_addr;
    req.addr.len = sizeof(DDPAddress);
    req.addr.maxlen = sizeof(DDPAddress);
    req.flags = 0;

    reply.nameid = 0;

    /* Synchronous registration (fast local operation) */
    err = OTRegisterName(md->nbp.ref, &req, &reply);

    if (err != kOTNoError) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_DISCOVERY,
            "OTRegisterName failed: %ld", (long)err);
        return -1;
    }

    /* Save for later deletion */
    md->nbp.name_id = reply.nameid;
    OTSetNBPEntityFromAddress(&md->nbp.our_entity,
                                addr.fNBPNameBuffer, name_len);
    md->nbp.registered = true;

    PT_CTX_INFO(ctx, PT_LOG_CAT_DISCOVERY,
        "NBP registered: %s", entity_str);

    return 0;
}

/* ========================================================================== */
/* NBP Name Lookup                                                            */
/* ========================================================================== */

/**
 * Lookup NBP names matching a pattern.
 *
 * Finds all services matching pattern (e.g., "=:PeerTalk@*" for all
 * PeerTalk services). Uses pre-allocated lookup_reply_buf (2KB) to
 * avoid stack overflow on 68k.
 *
 * Results are stored in md->nbp.lookup_addrs[] and lookup_names[].
 * Our own registration is filtered from results.
 *
 * @param ctx   PeerTalk context
 * @param type  NBP type to search for (e.g., "PeerTalk")
 * @param zone  NBP zone ("*" for all zones)
 * @return      Number of peers found (>= 0), -1 on error
 */
int pt_ot_nbp_lookup(struct pt_context *ctx,
                       const char *type,
                       const char *zone)
{
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    TLookupRequest req;
    TLookupReply reply;
    NBPAddress lookup_pattern;
    char entity_str[128];
    OTByteCount pattern_len;
    OSStatus err;
    TLookupBuffer *buf;
    UInt32 i;

    if (md->nbp.ref == NULL)
        return -1;

    /* Build wildcard pattern: "=:PeerTalk@*"
     * "=" matches any name, type is specific, zone is specified */
    pt_snprintf(entity_str, sizeof(entity_str), "=:%s@%s",
                type, zone ? zone : "*");

    lookup_pattern.fAddressType = AF_ATALK_NBP;
    pattern_len = OTSetAddressFromNBPString(lookup_pattern.fNBPNameBuffer,
                                              entity_str, -1);

    req.name.buf = (UInt8 *)&lookup_pattern;
    req.name.len = sizeof(OTAddressType) + pattern_len;
    req.name.maxlen = sizeof(NBPAddress);
    req.addr.buf = NULL;
    req.addr.len = 0;
    req.addr.maxlen = 0;
    req.maxcnt = PT_MAX_PEERS;
    req.timeout = 2000;  /* 2 seconds */
    req.flags = 0;

    /* Use pre-allocated buffer to avoid 2KB stack allocation */
    reply.names.buf = md->nbp.lookup_reply_buf;
    reply.names.maxlen = sizeof(md->nbp.lookup_reply_buf);
    reply.names.len = 0;
    reply.rspcount = 0;

    /* Synchronous lookup */
    err = OTLookupName(md->nbp.ref, &req, &reply);

    if (err != kOTNoError && err != kOTNoDataErr) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_DISCOVERY,
            "OTLookupName failed: %ld", (long)err);
        return -1;
    }

    /* Parse results using TLookupBuffer iteration */
    md->nbp.lookup_count = 0;

    buf = (TLookupBuffer *)md->nbp.lookup_reply_buf;
    for (i = 0; i < reply.rspcount && md->nbp.lookup_count < PT_MAX_PEERS; i++) {
        DDPAddress *addr = (DDPAddress *)buf->fAddressBuffer;

        /* Skip our own registration.
         * Compare DDP address components (network, node, socket). */
        if (md->nbp.registered) {
            pt_adsp_endpoint_cold *listener_cold = md->adsp_listener_cold;
            if (listener_cold != NULL &&
                addr->fNetwork == listener_cold->local_addr.fNetwork &&
                addr->fNodeID == listener_cold->local_addr.fNodeID &&
                addr->fSocket == listener_cold->local_addr.fSocket) {
                buf = OTNextLookupBuffer(buf);
                continue;
            }
        }

        /* Store result address */
        md->nbp.lookup_addrs[md->nbp.lookup_count] = *addr;

        /* Extract name (follows address in buffer) */
        {
            UInt8 *name_ptr = buf->fAddressBuffer + buf->fAddressLength;
            OTSetNBPEntityFromAddress(
                &md->nbp.lookup_names[md->nbp.lookup_count],
                name_ptr, buf->fNameLength);
        }

        md->nbp.lookup_count++;

        buf = OTNextLookupBuffer(buf);
    }

    if (md->nbp.lookup_count == 0) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_DISCOVERY,
            "NBP lookup found no peers (zone=%s)", zone ? zone : "*");
    } else {
        PT_CTX_INFO(ctx, PT_LOG_CAT_DISCOVERY,
            "NBP lookup found %d peers", md->nbp.lookup_count);
    }

    return md->nbp.lookup_count;
}

/* ========================================================================== */
/* NBP Unregister and Shutdown                                                */
/* ========================================================================== */

/**
 * Unregister our NBP name.
 *
 * Removes our service from the AppleTalk name table so we are no
 * longer visible to other Macs doing OTLookupName.
 *
 * @param ctx  PeerTalk context
 */
void pt_ot_nbp_unregister(struct pt_context *ctx)
{
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);

    if (md->nbp.ref != NULL && md->nbp.registered) {
        OTDeleteNameByID(md->nbp.ref, md->nbp.name_id);
        md->nbp.registered = false;
        PT_CTX_INFO(ctx, PT_LOG_CAT_DISCOVERY, "NBP unregistered");
    }
}

/**
 * Shut down NBP mapper.
 *
 * Unregisters name (if registered) and closes the mapper provider.
 *
 * @param ctx  PeerTalk context
 */
void pt_ot_nbp_shutdown(struct pt_context *ctx)
{
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);

    pt_ot_nbp_unregister(ctx);

    if (md->nbp.ref != NULL) {
        OTCloseProvider(md->nbp.ref);
        md->nbp.ref = NULL;
    }

    PT_CTX_INFO(ctx, PT_LOG_CAT_DISCOVERY, "NBP mapper shutdown");
}

#endif /* PT_PLATFORM_OT */
