/**
 * @file platform_posix.c
 * @brief PeerTalk POSIX Platform Implementation
 *
 * Platform abstraction for Linux and macOS systems.
 */

#include "pt_internal.h"

#if defined(PT_PLATFORM_POSIX)

#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>

/* ========================================================================== */
/* Platform Operations                                                        */
/* ========================================================================== */

static int posix_init(struct pt_context *ctx) {
    PT_LOG_INFO(ctx->log, PT_LOG_CAT_GENERAL, "POSIX platform initialized");
    return 0;
}

static void posix_shutdown(struct pt_context *ctx) {
    PT_LOG_INFO(ctx->log, PT_LOG_CAT_GENERAL, "POSIX platform shutdown");
}

static int posix_poll(struct pt_context *ctx) {
    /* Stub - implemented in Phase 4 (POSIX Networking) */
    (void)ctx;
    return 0;
}

static pt_tick_t posix_get_ticks(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);

    /* Return milliseconds - wraps every ~49 days, which is acceptable */
    return (pt_tick_t)((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
}

static unsigned long posix_get_free_mem(void) {
    /* POSIX: return effectively unlimited */
    return 1024UL * 1024UL * 1024UL; /* 1GB */
}

static unsigned long posix_get_max_block(void) {
    /* POSIX: return effectively unlimited */
    return 1024UL * 1024UL * 1024UL; /* 1GB */
}

/* Platform operations structure */
pt_platform_ops pt_posix_ops = {
    posix_init,
    posix_shutdown,
    posix_poll,
    posix_get_ticks,
    posix_get_free_mem,
    posix_get_max_block,
    NULL  /* send_udp - set by Phase 4 to pt_posix_send_udp */
};

/* ========================================================================== */
/* Platform-Specific Allocation                                               */
/* ========================================================================== */

void *pt_plat_alloc(size_t size) {
    return malloc(size);
}

void pt_plat_free(void *ptr) {
    free(ptr);
}

size_t pt_plat_extra_size(void) {
    /* No extra platform-specific data needed for POSIX */
    return 0;
}

#endif /* PT_PLATFORM_POSIX */
