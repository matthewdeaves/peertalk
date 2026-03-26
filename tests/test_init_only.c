/*
 * test_init_only.c -- Minimal PT_Init / PT_Shutdown test
 *
 * Purpose: isolate whether the crash is in the SDK init or later code.
 * Uses full Toolbox init like v1 working apps, then calls PT_Init,
 * logs result to PT_Log, and exits. No networking, no event loop.
 *
 * If this crashes, the bug is in PT_Init or platform init.
 * If this works, the bug is in the event loop or later code.
 */

#include <stdio.h>
#include <string.h>

/* Classic Mac Toolbox */
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Memory.h>
#include <Events.h>

#include "peertalk.h"
#include "clog.h"

#define MSG_CHAT 3

int main(void)
{
    PT_Context *ctx;
    long free_before, free_after;
    EventRecord event;

    /* Step 1: Extend heap FIRST (before any other call) */
    MaxApplZone();
    MoreMasters();
    MoreMasters();
    MoreMasters();
    MoreMasters();

    free_before = FreeMem();

    /* Step 2: Full Toolbox init (v1 pattern) */
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();

    /* Step 3: clog to file */
    clog_set_file("PT_Log");
    clog_init("test_init", CLOG_LVL_INFO);

    CLOG_INFO("=== test_init_only starting ===");
    CLOG_INFO("FreeMem before toolbox: %ld", free_before);
    CLOG_INFO("FreeMem after toolbox: %ld", FreeMem());

    /* Step 4: PT_Init */
    CLOG_INFO("Calling PT_Init...");

    if (PT_Init(&ctx, "InitTest") != PT_OK) {
        CLOG_INFO("PT_Init FAILED");
        clog_shutdown();
        return 1;
    }

    free_after = FreeMem();
    CLOG_INFO("PT_Init OK! FreeMem after: %ld", free_after);

    /* Register MSG_CHAT for error path tests later */
    PT_RegisterMessage(ctx, 3, PT_RELIABLE);

    /* Step 5: Start discovery (triggers MacTCP network operations) */
    CLOG_INFO("Calling PT_StartDiscovery...");
    if (PT_StartDiscovery(ctx) != PT_OK) {
        CLOG_INFO("PT_StartDiscovery FAILED");
        PT_Shutdown(ctx);
        clog_shutdown();
        return 1;
    }
    CLOG_INFO("PT_StartDiscovery OK! FreeMem: %ld", FreeMem());

    /* Step 6: Poll loop with discovery active (20 iterations, ~10 sec) */
    {
        int i;
        CLOG_INFO("Starting poll loop with discovery (20 iterations)...");
        for (i = 0; i < 20; i++) {
            long ticks;
            PT_Poll(ctx);
            if (i % 5 == 0) {
                CLOG_INFO("Poll %d OK, FreeMem: %ld, peers: %d",
                          i, FreeMem(), PT_GetPeerCount(ctx));
            }
            Delay(30, &ticks);  /* ~0.5 sec */
        }
        CLOG_INFO("Poll loop complete! Final FreeMem: %ld", FreeMem());
    }

    /* Step 7: Error path tests */
    {
        int err_pass = 0;
        int err_total = 0;
        PT_Status st;

        CLOG_INFO("=== Error path tests ===");

        /* PT_Send with NULL peer */
        err_total++;
        st = PT_Send(ctx, NULL, MSG_CHAT, "test", 4);
        if (st == PT_ERR_INVALID_ARG) {
            CLOG_INFO("PT_Send(NULL peer) = INVALID_ARG: OK");
            err_pass++;
        } else {
            CLOG_INFO("PT_Send(NULL peer) = %d: FAIL (expected INVALID_ARG)", (int)st);
        }

        /* PT_Send with NULL data and len > 0 */
        err_total++;
        st = PT_Send(ctx, NULL, MSG_CHAT, NULL, 10);
        if (st == PT_ERR_INVALID_ARG) {
            CLOG_INFO("PT_Send(NULL data, len>0) = INVALID_ARG: OK");
            err_pass++;
        } else {
            CLOG_INFO("PT_Send(NULL data, len>0) = %d: FAIL", (int)st);
        }

        /* PT_Connect with NULL peer */
        err_total++;
        st = PT_Connect(ctx, NULL);
        if (st == PT_ERR_INVALID_ARG) {
            CLOG_INFO("PT_Connect(NULL peer) = INVALID_ARG: OK");
            err_pass++;
        } else {
            CLOG_INFO("PT_Connect(NULL peer) = %d: FAIL", (int)st);
        }

        /* PT_Broadcast with no connected peers = PT_OK (no-op) */
        err_total++;
        st = PT_Broadcast(ctx, MSG_CHAT, "test", 4);
        if (st == PT_OK) {
            CLOG_INFO("PT_Broadcast(no peers) = OK: OK");
            err_pass++;
        } else {
            CLOG_INFO("PT_Broadcast(no peers) = %d: FAIL (expected OK)", (int)st);
        }

        /* PT_Broadcast with NULL ctx */
        err_total++;
        st = PT_Broadcast(NULL, MSG_CHAT, "test", 4);
        if (st == PT_ERR_INVALID_ARG) {
            CLOG_INFO("PT_Broadcast(NULL ctx) = INVALID_ARG: OK");
            err_pass++;
        } else {
            CLOG_INFO("PT_Broadcast(NULL ctx) = %d: FAIL", (int)st);
        }

        CLOG_INFO("Error paths: %d/%d passed", err_pass, err_total);
        if (err_pass != err_total) {
            CLOG_INFO("=== test_init_only FAILED (error paths) ===");
            PT_Shutdown(ctx);
            clog_shutdown();
            return 1;
        }
    }

    /* Step 8: Shutdown */
    CLOG_INFO("Calling PT_Shutdown...");
    PT_Shutdown(ctx);

    CLOG_INFO("PT_Shutdown OK. FreeMem: %ld", FreeMem());
    CLOG_INFO("=== test_init_only PASSED ===");

    clog_shutdown();

    /* Brief pause so log flushes */
    {
        long final_ticks;
        Delay(60, &final_ticks);
    }

    return 0;
}
