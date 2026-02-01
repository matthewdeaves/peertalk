---
name: review
description: Evaluate a PeerTalk phase plan for implementability, architectural soundness, cross-platform debugging support, and CPU performance. Spawns parallel subagents to verify APIs against Retro68 headers, check MacTCP/OT/AppleTalk documentation, audit lessons learned, validate phase dependencies, review logging provisions, and evaluate data-oriented design. After review, offers to automatically apply ALL recommended changes based on project goals and best practices. Use before implementing a phase or after drafting a plan to catch issues early.
argument-hint: <plan-file>
---

# Plan Review: $ARGUMENTS

Evaluate the plan document at `$ARGUMENTS` for implementability, architectural soundness, cross-platform debugging support, and CPU performance on Classic Mac hardware.

## Step 1: Parallel Fact-Gathering

Spawn 9 subagents in parallel using the Task tool. Each subagent should be type `Explore` or `general-purpose` as appropriate. For Classic Mac API verification, use the `/mac-api` skill directly rather than spawning subagents.

**For complete subagent prompts, see [subagent-prompts.md](references/subagent-prompts.md)**

### Quick Summary of Subagents

1. **MPW/Retro68 API Verification** (Explore)
   - Verify function signatures, struct definitions, constants against Retro68 headers
   - Check using Docker or local MPW_Interfaces.zip
   - Report discrepancies with citations

2. **MacTCP Documentation Review** (use `/mac-api` skill)
   - Verify ASR rules, parameter blocks, error codes, stream lifecycle
   - Check interrupt-safety requirements (ASR restrictions)
   - Trust hierarchy: .claude/rules/mactcp.md first, then books/

3. **Open Transport Documentation Review** (use `/mac-api` skill)
   - Verify notifier rules, endpoint patterns, tilisten, atomic operations
   - Check interrupt-safety (Table C-1, deferred tasks, notifier restrictions)
   - Trust hierarchy: .claude/rules/opentransport.md first, then books/

4. **AppleTalk Documentation Review** (use `/mac-api` skill)
   - Verify NBP, ADSP, completion routines, CCB structure
   - Check interrupt-safety (completion routine restrictions, userFlags)
   - Trust hierarchy: .claude/rules/appletalk.md first, then books/

5. **Inside Macintosh / ISR Safety Review** (use `/mac-api` skill)
   - Verify Memory Manager usage, BlockMove, Gestalt, system requirements
   - Check Table B-3 (interrupt-safe routines), memory-moving traps
   - Trust hierarchy: .claude/rules/isr-safety.md first, then books/

6. **CSEND Lessons Audit** (Explore)
   - Cross-reference plan claims against plan/CSEND-LESSONS.md
   - Flag outdated, incorrect, or unverifiable claims
   - Note relevant logging/timing patterns

7. **Phase Continuity Check** (Explore)
   - Map phase outputs to PROJECT_GOALS.md requirements
   - Identify deliverables for subsequent phases
   - Verify dependencies are correctly declared

8. **Logging & Debugging Review** (Explore)
   - Check PT_Log integration, categories, severity levels
   - Verify ISR-safety (no logging from callbacks)
   - Check coverage of critical points (errors, state transitions)
   - Reference: plan/PHASE-0-LOGGING.md for PT_Log API

9. **Data-Oriented Design Review** (general-purpose)
   - Evaluate struct field ordering, memory layout, cache efficiency
   - Check processing patterns (per-item vs bulk, branching)
   - Consider Classic Mac constraints (68030 256-byte cache, 2-10 MB/s bandwidth)
   - Categorize as "Fix Now" (architectural) vs "Fix Later" (implementation)

### Build Environment Note

The project uses Docker for Retro68 cross-compilation:
```bash
docker compose -f docker/docker-compose.yml run --rm peertalk-dev \
  grep -n "function_name" /Retro68/.../CIncludes/MacTCP.h
```

MPW headers location:
- In Docker: `/Retro68/InterfacesAndLibraries/MPW_Interfaces/.../CIncludes/`
- In project: `resources/retro68/MPW_Interfaces.zip`

## Step 2: Synthesis

Once all subagents complete, synthesise their findings into a standardized format.

**For complete synthesis format and templates, see [synthesis-format.md](references/synthesis-format.md)**

### Output Structure

1. **Summary Verdict**
   - State: IMPLEMENTABLE / NEEDS REVISION / BLOCKED
   - Brief rationale

2. **API/Documentation Issues**
   - Group by category: MPW/Retro68, MacTCP, OT, AppleTalk, Inside Macintosh
   - Table format: Issue | Source | Impact
   - Include citations (file:line, page numbers)

3. **CSEND-LESSONS Corrections**
   - Flag incorrect claims
   - Provide corrected information

4. **Phase Continuity Gaps**
   - Missing outputs or broken dependencies
   - What's required by subsequent phases

5. **Logging & Debugging Gaps**
   - Critical (ISR-safety violations, missing error logs)
   - Important (missing operational logs)
   - Nice-to-Have (additional debug visibility)

6. **Performance Concerns**
   - Fix Now (architectural - hard to change later)
   - Fix Later (implementation - can optimize without API changes)

7. **Recommended Plan Changes**
   - Prioritised list (Priority 1-3)
   - Specific locations and actions

8. **Apply Changes Prompt**
   - Use AskUserQuestion to offer auto-apply

After presenting the full review, use AskUserQuestion:

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

If user selects "Apply all changes", proceed to Step 3.

## Step 3: Apply All Changes (On User Confirmation)

After presenting the synthesis, when the user confirms "Apply all changes", automatically apply ALL recommended changes to the plan file.

**For complete decision-making rules and priority levels, see [auto-apply-rules.md](references/auto-apply-rules.md)**

### Priority Levels

Apply changes in this order:

**Priority 1 - Errors (Always fix):**
- Calculation errors, API signature mismatches
- Incorrect constants or magic numbers
- ISR-safety violations
- Type errors

**Priority 2 - Documentation Issues (Always fix):**
- Update "Review Applied" header with date and summary
- Add/update Fact-Check Summary table entries
- Fix incorrect page/section references
- Correct error code names

**Priority 3 - Struct/Type Improvements (Apply using best practice):**
- Reorder fields for proper alignment (68k: uint16_t at even offsets)
- Add missing fields identified in review
- Update size comments

**Priority 4 - Logging Requirements (Add all identified gaps):**
- Add missing logging requirement documentation
- Add log event flags for ISR callbacks
- Document state transition logging
- Add log category recommendations

**Priority 5 - DOD Optimizations (Document recommendations):**
- Add "Fix Now" items as architectural notes
- Add "Fix Later" items to implementation guidance
- Document trade-offs with rationale

**Priority 6 - Dependency Updates:**
- Update phase dependency status
- Add required function signatures for cross-phase dependencies
- Update prerequisite notes with verification steps

### Decision-Making Hierarchy

When multiple options exist, choose based on:
1. **Safety first** - ISR-safety, memory safety, crash prevention
2. **Project goals** - Match PROJECT_GOALS.md requirements
3. **Simplicity** - Fewer moving parts, less indirection
4. **Performance** - Cache efficiency, memory bandwidth (especially 68k)
5. **Debuggability** - Better logging, clearer state machines

### Execution Flow

1. Read the review synthesis to get all recommended changes
2. Group changes by priority (1-6)
3. For each priority level:
   - Find all changes in that category
   - Apply changes using Edit tool
   - Verify edit succeeded before continuing
4. After all edits:
   - Use Grep to verify key changes were applied
   - Report summary of changes made
5. If CLAUDE.md updates needed:
   - Apply those separately
   - Note in summary

### Update Checklist

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

### Also Update CLAUDE.md If Needed

If the review identifies issues that affect project-wide guidance:
- Add new ISR-safety patterns to Common Pitfalls
- Add magic numbers or constants to project-wide definitions
- Clarify alignment rules
- Document common mistakes that will recur in other phases

---

## Guidance for Subagents

Include these instructions when spawning each subagent:
- Stay focused on your specific reference materials and evaluation criteria
- Quote file paths and line numbers where possible
- Don't speculate—if information isn't in your assigned sources, say so
- Keep responses concise; the main agent will synthesise
- For logging review: Reference plan/PHASE-0-LOGGING.md for PT_Log API. Flag any ISR-safety violations as CRITICAL.
