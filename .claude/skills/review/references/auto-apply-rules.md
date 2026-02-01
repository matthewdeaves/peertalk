# Auto-Apply Rules - Automated Plan Correction

When the user confirms "Apply all changes" after review, apply ALL recommended changes to the plan file automatically using these decision-making rules.

## Contents
- Priority Levels (1-6: Errors, Documentation, Struct/Type, Logging, DOD, Dependencies)
- Decision-Making Hierarchy
- Examples of Automatic Decisions
- Update Checklist
- Also Update CLAUDE.md If Needed
- Execution Flow
- Error Handling

## Priority Levels

Apply changes in this order:

### Priority 1 - Errors (Always fix)

**What qualifies:**
- Calculation errors (e.g., buffer size math incorrect)
- API signature mismatches (wrong parameter counts, types)
- Incorrect constants or magic numbers
- ISR-safety violations (calling forbidden functions in callbacks)
- Type errors (using wrong struct field types)

**How to apply:**
- Find the exact location in plan file (use Grep with line numbers)
- Use Edit tool to replace incorrect value with correct value
- Update any dependent calculations or comments
- Add citation in comment if fixing based on documentation

**Example:**
```
// Before:
TCPCreate(stream, 6, ...);  // Create TCP stream

// After:
TCPCreate(stream, 8, ...);  // Create TCP stream (MacTCP.h:142 - 8 params)
```

### Priority 2 - Documentation Issues (Always fix)

**What qualifies:**
- Update "Review Applied" header with current date and summary
- Add/update Fact-Check Summary table entries
- Fix incorrect page/section references
- Correct error code names and meanings
- Update struct size comments to match actual size

**How to apply:**
- Add or update review header at top of plan file
- Create or update Fact-Check Summary table
- Fix inline citations to match authoritative sources

**Example header:**
```markdown
**Review Applied:** 2026-01-31
**Reviewer:** Claude Code (review skill)
**Changes:** Fixed TCPCreate signature, corrected pt_peer alignment, added state transition logging
```

### Priority 3 - Struct/Type Improvements (Apply using best practice)

**What qualifies:**
- Reorder struct fields for proper alignment
- Add missing fields identified in review
- Update size comments to match actual struct size
- Change field types for better performance or correctness

**Decision rules for alignment:**
- **68k alignment:** uint16_t at even offsets, uint32_t at offsets divisible by 4
- **Order:** Largest to smallest (uint32_t, uint16_t, uint8_t, then pointers)
- **Padding:** Add explicit padding bytes if needed for documentation

**How to apply:**
- Find struct definition in plan file
- Reorder fields following alignment rules
- Update struct size calculation/comment
- Document alignment rationale in comment

**Example:**
```c
// Before:
typedef struct {
    uint8_t flags;          // Offset 0
    uint16_t port;          // Offset 1 - MISALIGNED on 68k!
    uint32_t ip_address;    // Offset 3 - MISALIGNED on 68k!
} pt_peer;                  // Size: 7 bytes

// After:
typedef struct {
    uint32_t ip_address;    // Offset 0 (aligned)
    uint16_t port;          // Offset 4 (aligned)
    uint8_t flags;          // Offset 6
    uint8_t _pad;           // Offset 7 (explicit padding)
} pt_peer;                  // Size: 8 bytes (68k-safe alignment)
```

### Priority 4 - Logging Requirements (Add all identified gaps)

**What qualifies:**
- Add missing logging requirement documentation
- Add log event flags for completion routines (ISR-safe pattern)
- Document state transition logging patterns
- Add log category recommendations
- Document what should be logged at each severity level

**How to apply:**
- Find relevant task or session section
- Add logging requirements to task description
- Use appropriate PT_Log macros (PT_ERR, PT_WARN, PT_INFO, PT_DEBUG)
- Use correct categories (PT_LOG_CAT_NETWORK, PT_LOG_CAT_PLATFORM, etc.)
- For ISR callbacks: use flag pattern, not direct logging

**Example addition:**
```markdown
### Task 5.2.3: Implement TCP ASR

**Logging requirements:**
- Set `state->log_events.tcp_data_received = 1` in ASR (ISR-safe)
- In main loop, check flag and log: `PT_INFO(PT_LOG_CAT_NETWORK, "TCP data received: %d bytes", len)`
- On ASR error, set `state->log_events.tcp_error = 1` and `state->log_error_code = result`
- In main loop: `PT_ERR(PT_LOG_CAT_PLATFORM, "MacTCP ASR error: %d", state->log_error_code)`
```

### Priority 5 - DOD Optimizations (Document recommendations)

**What qualifies:**
- Add "Fix Now" items as architectural notes in the plan
- Add "Fix Later" items to implementation guidance sections
- Document trade-offs with rationale
- Note cache-friendly patterns for future reference

**How to apply:**
- For "Fix Now" items: Add to session objectives or architectural notes
- For "Fix Later" items: Add to "Future Optimizations" or "Performance Notes" section
- Include rationale (why this matters on Classic Mac hardware)
- Don't change implementation code, just document the recommendation

**Example:**
```markdown
## Performance Notes

**Cache Efficiency (Fix Later):**
The current peer list uses a linked list for flexibility. On Classic Mac hardware (68030 with 256-byte cache),
array-based iteration would provide better cache locality. Consider migrating to fixed-size peer array in Phase 9
optimization pass. Trade-off: array limits max peers but improves iteration from O(n) cache misses to O(1) per cache line.

**Alignment Optimized (Fixed):**
pt_peer struct reordered for 68k alignment (uint32_t first, uint16_t at even offset, uint8_t last).
Prevents misaligned access exceptions on 68020/68030.
```

### Priority 6 - Dependency Updates

**What qualifies:**
- Update phase dependency status (verified/unverified)
- Add required function signatures for cross-phase dependencies
- Update prerequisite notes with verification steps
- Document what prior phases must deliver

**How to apply:**
- Find "Depends on:" section in phase header
- Add verification checklist for dependencies
- Document required exports from dependency phases
- Add function signatures that must exist

**Example:**
```markdown
**Depends on:** Phase 2 (POSIX Implementation)

**Required exports from Phase 2:**
- `pt_queue_t* pt_queue_create(size_t capacity)` - Queue allocation
- `void pt_queue_destroy(pt_queue_t* queue)` - Queue cleanup
- `int pt_queue_push(pt_queue_t* queue, const void* data, size_t len)` - Enqueue

**Verification:**
- [ ] Check include/peertalk.h has pt_queue_t typedef
- [ ] Check src/core/pt_queue.c has create/destroy/push implementations
- [ ] Verify POSIX tests in tests/test_queue.c pass
```

## Decision-Making Hierarchy

When multiple options exist for fixing an issue, choose based on this priority order:

1. **Safety first:** ISR-safety, memory safety, crash prevention
2. **Project goals:** Match PROJECT_GOALS.md requirements
3. **Simplicity:** Fewer moving parts, less indirection
4. **Performance:** Cache efficiency, memory bandwidth (especially for 68k)
5. **Debuggability:** Better logging, clearer state machines

## Examples of Automatic Decisions

| Situation | Option A | Option B | Decision | Rationale |
|-----------|----------|----------|----------|-----------|
| Hot struct field ordering | By declaration order | By size (largest first) | **Option B** | Performance (#4) - better alignment and padding |
| Pointer vs index in hot struct | Pointer (flexible) | Index (fixed array) | **Document both** | Trade-off requires context; note in performance section |
| Log category for discovery | PT_LOG_CAT_NETWORK | PT_LOG_CAT_PROTOCOL | **PT_LOG_CAT_PROTOCOL** | More specific category for protocol-level operations |
| Buffer size calculation | Plan's formula | MacTCP Guide formula | **MacTCP Guide** | Authoritative source (#1/#2) |
| ISR-safe timing | TickCount() | Flag + main loop timestamp | **Flag pattern** | Safety first (#1) - TickCount not in Table B-3 |

## Update Checklist

When applying changes, ensure these sections are updated:

- [ ] Header "Review Applied" line with date and summary
- [ ] All code blocks with corrected values
- [ ] Struct definitions with proper field ordering
- [ ] Comments with corrected calculations or citations
- [ ] Fact-Check Summary table with new verified facts
- [ ] Acceptance criteria/checklists if affected
- [ ] Cross-references to other phase documents if dependencies changed
- [ ] Performance notes or optimization sections if DOD recommendations added
- [ ] Logging requirements in task descriptions

## Also Update CLAUDE.md If Needed

If the review identifies issues that affect project-wide guidance:

**Add to CLAUDE.md when:**
- New ISR-safety pattern discovered (add to Common Pitfalls)
- Magic number or constant needs project-wide definition
- Alignment rule clarification needed
- Common mistake that will recur in other phases

**How to apply:**
- Use Edit tool to add to appropriate CLAUDE.md section
- Reference the phase where issue was discovered
- Include authoritative citation
- Add to Common Pitfalls table if applicable

**Example CLAUDE.md addition:**
```markdown
## Common Pitfalls

| Pitfall | Correct Approach |
|---------|------------------|
| ... | ... |
| Calling PT_Log from ASR/notifier | Set flag in callback, log from main loop (discovered in Phase 5 review) |
```

## Execution Flow

1. **Read the review synthesis** to get all recommended changes
2. **Group changes by priority** (1-6)
3. **For each priority level:**
   - Find all changes in that category
   - Apply changes using Edit tool
   - Verify edit succeeded before continuing
4. **After all edits:**
   - Use Grep to verify key changes were applied
   - Report summary of changes made
5. **If CLAUDE.md updates needed:**
   - Apply those separately
   - Note in summary

## Error Handling

If an Edit fails:
- Log the failure
- Continue with remaining changes
- Report failed edits at end
- Provide manual edit instructions for failed changes
