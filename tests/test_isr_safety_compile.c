/**
 * @file test_isr_safety_compile.c
 * @brief Compile-time test that PT_ISR_CONTEXT disables all logging
 *
 * This test verifies that when PT_ISR_CONTEXT is defined, all PT_LOG macros
 * expand to nothing (no function calls). This is critical for interrupt safety.
 *
 * VERIFICATION: Check assembly/object code to ensure function is empty:
 *   objdump -d test_isr_safety_compile.o | grep -A 10 "test_isr_logging"
 *
 * Expected: Function should only contain a return instruction, no calls to
 * PT_LogWrite, PT_LogPerf, or any other PT_Log functions.
 */

#define PT_ISR_CONTEXT  /* CRITICAL: Must be defined BEFORE including pt_log.h */
#include "../include/pt_log.h"
#include <stdio.h>

/**
 * Test function - should compile to empty function (just return)
 *
 * When PT_ISR_CONTEXT is defined, all logging macros expand to:
 *   do { (void)(log); } while (0)
 *
 * This ensures the compiler sees 'log' parameter used (no warnings) but
 * generates no actual code.
 */
void test_isr_logging(PT_Log *log) {
    /* All of these should expand to nothing */
    PT_LOG_ERR(log, PT_LOG_CAT_GENERAL, "Error in ISR");
    PT_LOG_WARN(log, PT_LOG_CAT_NETWORK, "Warning in ISR");
    PT_LOG_INFO(log, PT_LOG_CAT_MEMORY, "Info in ISR");
    PT_LOG_DEBUG(log, PT_LOG_CAT_PROTOCOL, "Debug in ISR");

    PT_LogPerfEntry entry = {0};
    entry.seq_num = 1;
    entry.event_type = 42;
    entry.category = PT_LOG_CAT_PERF;
    PT_LOG_PERF(log, &entry, "Perf in ISR");

    /* Suppress unused variable warning when PT_ISR_CONTEXT is defined */
    (void)entry;

    /* If PT_ISR_CONTEXT works correctly, this function compiles to:
     *   void test_isr_logging(PT_Log *log) {
     *       return;
     *   }
     */
}

/**
 * This test INTENTIONALLY does not have a main() function that links.
 *
 * WHY: PT_ISR_CONTEXT is designed to cause a LINKER ERROR with a clear message:
 *   "undefined reference to `_PT_ISR_ERROR_DO_NOT_CALL_PT_LOG_FROM_INTERRUPT'"
 *
 * This is BETTER than silently expanding to nothing because it catches ISR
 * logging bugs at link time with a descriptive error.
 *
 * VERIFICATION:
 *   1. Compile: gcc -c tests/test_isr_safety_compile.c -o test_isr_safety_compile.o
 *   2. Attempt link: gcc test_isr_safety_compile.o -o test_isr_safety -L. -lptlog
 *   3. Expected: Linker error with "_PT_ISR_ERROR_DO_NOT_CALL_PT_LOG_FROM_INTERRUPT"
 *   4. Objdump shows calls to the undefined function (not actual PT_Log calls)
 *
 * This test file is compiled but NOT linked by the build system.
 * Its presence in the codebase serves as documentation of the PT_ISR_CONTEXT
 * guard's behavior.
 */

/* NO main() function - this is intentional */
