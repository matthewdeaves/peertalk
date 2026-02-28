# Implementation Patterns and Guidance

Best practices and common patterns for implementing PeerTalk sessions.

## Contents
- For Large Sessions
- For Platform-Specific Code (Classic Mac, ISR-Safety Patterns)
- For Struct Modifications
- For Test Code
- Code Quality Guidelines
- Documentation-Driven Development
- Common Pitfalls
- Performance Considerations

## For Large Sessions

If a session has many tasks (>10), implement incrementally:
1. Implement 3-5 tasks
2. Verify they compile
3. Continue to next batch
4. Final verification at end

This approach:
- Catches errors early
- Reduces context switching
- Makes progress visible
- Easier to debug issues

## For Platform-Specific Code

### Classic Mac Code (MacTCP, Open Transport, AppleTalk)

**Always verify against Retro68 headers** before writing Classic Mac code:

```bash
# Use Docker container to access headers
docker compose -f docker/docker-compose.yml run --rm peertalk-dev \
  grep -n "TCPCreate" /Retro68/.../CIncludes/MacTCP.h
```

**Use /mac-api skill** for documentation lookups when:
- Plan doesn't specify exact value/signature
- Unsure if a call is interrupt-safe
- Error code appears that isn't documented
- Need to verify behavior plan assumes but doesn't cite

**Don't use /mac-api for:**
- Things already in session spec or CLAUDE.md
- Every line of code
- General programming questions

### ISR-Safety Patterns

For code that runs at interrupt time (ASR, notifier, completion):

**✓ DO:**
- Set volatile flags
- Increment counters
- Read pre-allocated buffers
- Use `pt_memcpy_isr()` (byte-copy, no Toolbox)
- Issue async network calls (MacTCP ASR only)

**✗ DON'T:**
- Allocate memory (`malloc`, `NewPtr`, `NewHandle`)
- Free memory (`free`, `DisposePtr`, `DisposeHandle`)
- Call `TickCount()` - NOT in Table B-3
- Use `BlockMove`/`BlockMoveData` - safety unclear
- Do file I/O
- Make synchronous network calls
- Call most Toolbox routines

**Pattern: Set Flag, Process Later**
```c
/* In callback - just set flags */
static pascal void my_callback(...) {
    state->flags.event_occurred = 1;  /* OK: atomic flag set */
}

/* In main loop - do the work */
void poll(void) {
    if (state->flags.event_occurred) {
        state->flags.event_occurred = 0;
        handle_event();  /* Safe to do anything here */
    }
}
```

## For Struct Modifications

If the session modifies shared structs (`pt_peer`, `pt_context`, `pt_queue`):

1. **Update the struct definition first**
2. **Update all code that uses the struct**
3. **Rebuild everything that includes the header**
4. **Verify field ordering** (largest to smallest for padding, int16_t at even offsets for 68k)
5. **Update size comments** if struct size changed

## For Test Code

Tests go in `tests/test_{module}.c`. Follow existing test patterns:

**Structure:**
- Use simple assert-based testing
- Print pass/fail status
- Return 0 on success, non-zero on failure
- Clean up resources on exit

**Example:**
```c
int test_feature(void) {
    /* Setup */
    int result = function_under_test();

    /* Verify */
    if (result != EXPECTED) {
        printf("FAIL: Expected %d, got %d\n", EXPECTED, result);
        return 1;
    }

    printf("PASS: test_feature\n");
    return 0;
}
```

## Code Quality Guidelines

### Function Length
- **Target:** 50 lines or less
- **Maximum:** 100 lines
- **If exceeding:** Extract helper functions or refactor logic

### File Size
- **Maximum:** 500 lines
- **If exceeding:** Split into multiple files by responsibility

### Complexity
- **Maximum cyclomatic complexity:** 15 per function
- **If exceeding:** Simplify branching, extract functions, use tables

### Error Handling
- Check return values
- Log errors with context
- Clean up resources on error paths
- Return meaningful error codes

## Documentation-Driven Development

When implementing Classic Mac code:

1. **Check session spec first** - Most details should be there
2. **Check CLAUDE.md** - Platform rules and patterns
3. **Check Retro68 headers** - API signatures and constants
4. **Use /mac-api skill** - Only when above sources don't have the answer
5. **Update plan if wrong** - Document discrepancies for future sessions

## Common Pitfalls

| Pitfall | Correct Approach |
|---------|------------------|
| Allocating in ASR/notifier | Use pre-allocated buffers from context struct |
| Forgetting TCPRcvBfrReturn | Always return MacTCP buffers after reading |
| Wrong byte order | Use `htonl`/`ntohl` for network data |
| TCPPassiveOpen re-use | It's one-shot, need stream transfer pattern |
| Testing only POSIX | Real Mac hardware behaves differently |
| Ignoring compiler warnings | Treat warnings as errors (`-Werror`) |

## Performance Considerations

Classic Mac hardware is severely constrained:
- **68030 data cache:** Only 256 bytes
- **Memory bandwidth:** 2-10 MB/s
- **No branch prediction:** Keep hot paths linear

**Cache-friendly patterns:**
- Group related data in structs
- Process arrays in order
- Minimize pointer indirection
- Keep hot code paths small

## Parallel Batch Implementation (Spec-Kit Mode)

When spec-kit tasks have `[P]` markers, they can be implemented in parallel batches via the Task tool. See [parallel-execution.md](parallel-execution.md) for the full protocol.

**Key rules:**
- Max 4 tasks per parallel batch
- Verify no file conflicts between batch members
- After each batch, compile-check the combined result
- Mark all batch tasks `[X]` only after the batch passes verification

**Batch task context:**
Each parallel task agent needs:
1. The specific task description and file paths
2. Relevant user story and acceptance criteria (from spec.md)
3. Constitution principles (if loaded)
4. Platform rules (ISR safety, etc.)
5. Existing code inventory (what already exists)

**After parallel batches:**
Run cross-task consistency check — verify that parallel implementations are compatible (no conflicting assumptions, consistent naming, compatible interfaces).

## Checkpoint and Resume (Spec-Kit Mode)

For long implementation sessions (>15 tasks), use periodic checkpoints. See [checkpoint-protocol.md](checkpoint-protocol.md).

**Key points:**
- Checkpoint every 5 tasks or after each parallel batch
- Report progress, files modified, compile status
- Offer the user a chance to pause
- On resume, scan tasks.md for `[X]` markers and pick up from first pending task
