/*
 * ISR Safety Compile-Time Test for Open Transport
 *
 * This file MUST NOT compile. It verifies that the PT_ISR_CONTEXT guard
 * macro correctly blocks PT_Log calls from notifier (deferred task) code.
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
 *   powerpc-apple-macos-gcc -c tests/test_isr_safety_ot.c -I include -I src/core
 *
 * CI INTEGRATION:
 *   Add to .github/workflows/ci.yml to verify guard works:
 *     - name: ISR safety check (Open Transport)
 *       run: |
 *         if powerpc-apple-macos-gcc -c tests/test_isr_safety_ot.c \
 *            -I include -I src/core 2>&1 | \
 *            grep -q "cannot be called at interrupt time"; then
 *           echo "✓ ISR safety guard working correctly"
 *         else
 *           echo "✗ ISR safety guard failed!"
 *           exit 1
 *         fi
 */

#define PT_ISR_CONTEXT  /* Mark as deferred task context - enables safety check */
#include "pt_log.h"

/* Open Transport headers (will be available when Phase 6 is implemented) */
#ifdef PT_PLATFORM_OPENTRANSPORT
#include <OpenTransport.h>
#include <OpenTptInternet.h>
#endif

/*
 * Example notifier callback that violates ISR safety
 *
 * This simulates a real Open Transport notifier attempting to call PT_Log,
 * which is forbidden at deferred task time.
 *
 * From Networking With Open Transport:
 *   "Notifiers run at deferred task time (not hardware interrupt time but
 *    still restricted). Cannot call File Manager, cannot reliably allocate
 *    memory, cannot call synchronous OT functions."
 *
 * PT_Log may:
 * - Call File Manager for file output (forbidden at deferred task time)
 * - Allocate memory via NewPtr (unreliable at deferred task time)
 * - Call TickCount() or other timing functions (availability varies)
 *
 * Open Transport provides three execution levels:
 *   1. Hardware interrupt time (Table C-1 functions only)
 *   2. Deferred task time (Table C-3 functions only) ← Notifiers run here
 *   3. System task time (full API available) ← Main thread runs here
 *
 * PT_ISR_CONTEXT blocks PT_Log for both level 1 and level 2, since both
 * have severe restrictions that PT_Log cannot satisfy.
 */
#ifdef PT_PLATFORM_OPENTRANSPORT
static pascal void test_notifier_violates_isr_safety(
    void *contextPtr,
    OTEventCode code,
    OTResult result,
    void *cookie)
{
    PT_Log *log = (PT_Log *)contextPtr;

    /* These should all cause compile errors if PT_ISR_CONTEXT guard works */
    PT_LOG_ERR(log, PT_LOG_CAT_PLATFORM, "Notifier event: %ld", code);
    PT_LOG_INFO(log, PT_LOG_CAT_NETWORK, "Result: %ld", result);
    PT_LOG_DEBUG(log, PT_LOG_CAT_PLATFORM, "Cookie: %p", cookie);
    PT_LOG_WARN(log, PT_LOG_CAT_NETWORK, "Connection event");

    /*
     * The guard should also block performance logging at deferred task time
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
 * (The error message says "interrupt time" but applies to deferred task
 * time as well, since both have similar restrictions.)
 *
 * If compilation succeeds, the PT_ISR_CONTEXT guard is broken and
 * developers may accidentally call PT_Log from notifiers, leading to
 * File Manager crashes or memory corruption.
 *
 * INTEGRATION WITH ACTUAL CODE:
 *
 * When implementing Open Transport notifiers in Phase 6, add the guard
 * at the function level:
 *
 *   static pascal void pt_tcp_notifier(...) {
 *       #define PT_ISR_CONTEXT
 *       // Notifier implementation - PT_Log calls fail to compile here
 *       PT_FLAG_SET(hot->flags, PT_FLAG_DATA_AVAILABLE);
 *       #undef PT_ISR_CONTEXT
 *   }
 *
 * This provides per-function compile-time verification of deferred task
 * safety, which is equivalent to ISR safety for our purposes.
 *
 * WHY DEFERRED TASK TIME IS LIKE INTERRUPT TIME:
 *
 * Although deferred tasks run at a lower priority than hardware interrupts,
 * they still cannot:
 * - Call File Manager (PT_Log writes to files)
 * - Reliably allocate memory (NewPtr may fail or fragment heap)
 * - Call Device Manager (synchronous I/O forbidden)
 * - Call most Toolbox routines
 *
 * The PT_ISR_CONTEXT guard treats both levels as equally unsafe for PT_Log.
 */
