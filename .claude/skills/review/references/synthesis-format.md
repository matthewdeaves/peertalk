# Review Synthesis Format

After all subagents complete, synthesise their findings into this standardized format.

## Contents
- Template sections: Summary Verdict, API/Documentation Issues, CSEND-LESSONS Corrections, Phase Continuity Gaps, Logging & Debugging Gaps, Performance Concerns, Recommended Plan Changes, Apply Changes Prompt
- Notes on Synthesis Quality

---

## Plan Review: {PLAN-FILE-NAME}

### 1. Summary Verdict

State one of: **IMPLEMENTABLE** / **NEEDS REVISION** / **BLOCKED**

Provide a brief (1-2 sentence) rationale for the verdict.

### 2. API/Documentation Issues

Discrepancies between plan and authoritative references (from Subagents 1, 2a-2d).

Group by category:
- **MPW/Retro68 API** (Subagent 1)
- **MacTCP** (Subagent 2a)
- **Open Transport** (Subagent 2b)
- **AppleTalk** (Subagent 2c)
- **Inside Macintosh / General** (Subagent 2d)

For each category with issues, use this table format:

| Issue | Source | Impact |
|-------|--------|--------|
| Function signature mismatch: TCPCreate takes 8 params, not 6 | MacTCP.h:142 | Critical - won't compile |
| Error code -23012 is connectionClosing, not connectionAborted | MacTCP Guide p.4-27 | Medium - incorrect error handling |
| ... | ... | ... |

**If no issues in a category, state:** "None found - plan matches documentation"

### 3. CSEND-LESSONS Corrections

Claims flagged as incorrect by Subagent 3, with corrected information.

| Claim in Plan | Status | Correction |
|---------------|--------|------------|
| "POSIX guarantees atomic writes under 512 bytes" | FALSE | POSIX guarantees atomicity only for PIPE_BUF (typically 4096 bytes), not 512 |
| ... | ... | ... |

**If no issues:** "None found - all claims verified"

### 4. Phase Continuity Gaps

Missing outputs or broken dependencies (Subagent 4).

| Gap | Required By | Recommendation |
|-----|-------------|----------------|
| Phase declares dependency on Phase 2 but Phase 2 doesn't export pt_queue_create() | Session 5.2 | Add pt_queue_create() to Phase 2 deliverables |
| ... | ... | ... |

**If no gaps:** "None - phase dependencies are correct"

### 5. Logging & Debugging Gaps

Issues found by Subagent 5 regarding cross-platform debugging support.

#### Critical (Must fix - ISR-safety or missing error logging)

| Issue | Location | Recommendation |
|-------|----------|----------------|
| PT_Log called from tcp_asr callback | Session 5.2, Task 5.2.3 | Use flag-based pattern: set flag in ASR, log from main loop |
| No logging for TCPPassiveOpen failure | Session 5.1 | Add PT_ERR log with error code when TCPPassiveOpen fails |
| ... | ... | ... |

#### Important (Should fix - missing key operational logging)

| Issue | Location | Recommendation |
|-------|----------|----------------|
| No state transition logging for connection states | Session 5.3 | Add PT_DEBUG logs for IDLE→LISTENING→CONNECTED transitions |
| ... | ... | ... |

#### Nice-to-Have (Optional - additional debug visibility)

| Issue | Location | Recommendation |
|-------|----------|----------------|
| Could log buffer fill levels for debugging flow control | Session 5.4 | Add PT_DEBUG log when buffer >75% full |
| ... | ... | ... |

**If no gaps:** "Logging provisions are adequate"

### 6. Performance Concerns

Data-oriented design issues from Subagent 6.

#### Fix Now (Architectural - hard to change later)

| Issue | Location | Recommendation |
|-------|----------|----------------|
| pt_peer struct has uint8_t flags at offset 13, causing misalignment on 68k | Session 3.1 | Move uint8_t fields to end, keep uint16_t at even offsets |
| Using strcmp() in hot path for peer lookup | Session 4.2 | Use integer peer_id instead of string name for lookups |
| ... | ... | ... |

#### Fix Later (Implementation - can optimize without API changes)

| Issue | Location | Recommendation |
|-------|----------|----------------|
| Connection iteration could be array-based instead of linked list | Session 5.3 | Document for optimization: array provides better cache locality |
| ... | ... | ... |

**If no concerns:** "No significant performance concerns identified"

### 7. Recommended Plan Changes

Prioritised list of modifications addressing all of the above.

**Priority 1 (Critical - must fix before implementation):**
1. Correct TCPCreate signature (8 params) in Session 5.1, Task 5.1.2
2. Fix ISR-safety violation: remove PT_Log from tcp_asr, use flag pattern instead
3. Realign pt_peer struct fields for 68k alignment

**Priority 2 (Important - should fix to avoid rework):**
4. Add state transition logging to all state machine code
5. Update Phase 2 deliverables to include pt_queue_create()
6. Correct error code documentation (connectionClosing vs connectionAborted)

**Priority 3 (Nice-to-have - improves quality):**
7. Add buffer fill level debugging logs
8. Document cache-friendly array iteration pattern for future optimization

### 8. Apply Changes Prompt

After presenting the full review, use AskUserQuestion to ask:

```
Ready to apply all recommended changes to the plan file?

All decisions will be made automatically based on:
• PROJECT_GOALS.md requirements
• Best architecture/design patterns for Classic Mac
• Performance optimization for 68k/PPC hardware
• ISR-safety requirements from CLAUDE.md

Options:
- "Apply all changes" (Recommended) - Fix all issues now
- "Review only" - Keep the review as documentation, apply manually later
```

If user selects "Apply all changes", proceed to auto-apply step (see [auto-apply-rules.md](auto-apply-rules.md)).

---

## Notes on Synthesis Quality

- **Be specific:** Include file names, line numbers, session/task references
- **Include citations:** Quote sources with page/chapter/line references
- **Categorize impact:** Use "Critical", "Important", "Nice-to-have" consistently
- **Provide actionable recommendations:** Tell what to do, not just what's wrong
- **Maintain professional tone:** Objective, constructive, focused on facts
