/*
 * ISR Safety Compile-Time Test for MacTCP
 *
 * This file MUST NOT compile. It verifies that the PT_ISR_CONTEXT guard
 * macro correctly blocks PT_Log calls from interrupt-time code.
 *
 * DO NOT add this to the Makefile test target - it's intentionally designed
 * to fail compilation as a safety check.
 *
 * VERIFICATION:
 *   This test should FAIL to compile with an error about PT_Log being
 *   unsafe at interrupt time. If it compiles, the guard is broken.
 *
 * USAGE:
 *   # This should fail with explicit error message
 *   m68k-apple-macos-gcc -c tests/test_isr_safety_mactcp.c -I include -I src/core
 *
 * CI INTEGRATION:
 *   Add to .github/workflows/ci.yml to verify guard works:
 *     - name: ISR safety check (MacTCP)
 *       run: |
 *         if m68k-apple-macos-gcc -c tests/test_isr_safety_mactcp.c \
 *            -I include -I src/core 2>&1 | \
 *            grep -q "cannot be called at interrupt time"; then
 *           echo "✓ ISR safety guard working correctly"
 *         else
 *           echo "✗ ISR safety guard failed!"
 *           exit 1
 *         fi
 */

#define PT_ISR_CONTEXT  /* Mark as interrupt context - enables safety check */
#include "pt_log.h"

/* MacTCP headers (will be available when Phase 5 is implemented) */
#ifdef PT_PLATFORM_MACTCP
#include <MacTCP.h>
#endif

/*
 * Example ASR callback that violates ISR safety
 *
 * This simulates a real MacTCP ASR (Asynchronous Notification Routine)
 * attempting to call PT_Log, which is forbidden at interrupt time.
 *
 * From MacTCP Programmer's Guide:
 *   "ASRs run at interrupt level and cannot allocate or release memory,
 *    cannot make synchronous MacTCP calls, and must preserve registers."
 *
 * PT_Log may allocate memory, call File Manager (for file output), or
 * call TickCount() - all forbidden at interrupt level.
 */
#ifdef PT_PLATFORM_MACTCP
static pascal void test_asr_violates_isr_safety(
    StreamPtr stream,
    unsigned short event,
    Ptr userDataPtr,
    struct ICMPReport *icmpMsg)
{
    PT_Log *log = (PT_Log *)userDataPtr;

    /* These should all cause compile errors if PT_ISR_CONTEXT guard works */
    PT_LOG_ERR(log, PT_LOG_CAT_PLATFORM, "ASR event: %d", event);
    PT_LOG_INFO(log, PT_LOG_CAT_NETWORK, "Data arrived");
    PT_LOG_DEBUG(log, PT_LOG_CAT_PLATFORM, "Stream=%p", stream);
    PT_LOG_WARN(log, PT_LOG_CAT_NETWORK, "ICMP error");

    /*
     * The guard should also block performance logging, which may
     * allocate memory for structured entries
     */
    PT_LogPerf(log, PT_LOG_CAT_PERF, 1, 100, 0, 0);
}
#endif

/*
 * Main function - should never execute
 *
 * This file is designed to fail at compile time, not runtime.
 * If execution reaches here, something is wrong with the build system.
 */
int main(void) {
    /* This should never be reached */
    return 1;
}

/*
 * EXPECTED BEHAVIOR:
 *
 * When compiled, this file should produce an error like:
 *
 *   error: PT_Log functions cannot be called at interrupt time
 *   note: #define PT_ISR_CONTEXT before including pt_log.h
 *
 * If compilation succeeds, the PT_ISR_CONTEXT guard is broken and
 * developers may accidentally call PT_Log from interrupt handlers,
 * leading to memory corruption or crashes.
 *
 * INTEGRATION WITH ACTUAL CODE:
 *
 * When implementing MacTCP ASR callbacks in Phase 5, add the guard
 * at the function level:
 *
 *   static pascal void pt_tcp_asr(...) {
 *       #define PT_ISR_CONTEXT
 *       // ASR implementation - PT_Log calls fail to compile here
 *       hot->asr_flags |= PT_ASR_DATA_ARRIVED;
 *       #undef PT_ISR_CONTEXT
 *   }
 *
 * This provides per-function compile-time verification of ISR safety.
 */
