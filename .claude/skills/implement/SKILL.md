---
name: implement
description: Implements PeerTalk phase sessions or spec-kit tasks. Supports legacy PHASE-*.md sessions (e.g., /implement 1 1.2) and spec-kit task IDs (e.g., /implement T001-T010). Gathers context, implements tasks sequentially or in parallel batches for [P] tasks, marks completion, and verifies deliverables. Supports checkpoint/resume for long sessions. Use without arguments to auto-detect the next work item.
argument-hint: <phase session | task-id | task-range | next>
---

# Implement: $ARGUMENTS

Implement PeerTalk work items. This skill handles format detection, context gathering, task execution (sequential or parallel), verification, and completion marking.

## Step 0: Parse Arguments + Format Detection

**Detect format using [artifact-detection.md](../_shared/artifact-detection.md):**

| Argument Pattern | Mode | Action |
|-----------------|------|--------|
| Empty / whitespace | Auto-detect | Scan for artifacts, find next work item |
| `N N.Y` (e.g., `6 6.1`) | Legacy | Map to `plan/PHASE-{N}-*.md`, session N.Y |
| `T001` | Spec-kit | Single task from tasks.md |
| `T001-T010` | Spec-kit | Task range from tasks.md |
| `T001 T003 T007` | Spec-kit | Specific tasks (space-separated) |
| `next` | Auto-detect | Find first pending item in detected format |
| `next 5` | Auto-detect | Next 5 pending items |

### Auto-Detection (No Arguments)

1. Check for spec-kit artifacts (tasks.md with pending `- [ ]` items)
2. Check for legacy artifacts (PHASE-*.md with `[OPEN]` sessions)
3. If both exist, use AskUserQuestion to let user choose format
4. Find the first pending work item in the chosen format

### Spec-Kit Resume Detection

If tasks.md has some `[X]` markers (completed tasks), follow the resume protocol in [checkpoint-protocol.md](references/checkpoint-protocol.md):

1. Count completed vs pending tasks
2. Offer to resume from the first pending task
3. Use AskUserQuestion:
   ```
   Header: "Resume"
   Question: "Found N completed tasks. Resume from TXXX?"
   Options:
   - "Resume from TXXX" (Recommended) - Continue where left off
   - "Start over" - Re-implement from T001
   - "Show completed tasks" - Review what was done
   - "Jump to specific task" - Pick a different starting point
   ```

### Legacy Auto-Detection

If `$ARGUMENTS` is empty and only legacy artifacts exist:
1. Use Glob to find `plan/PHASE-*.md`
2. For each phase, check dependencies and find first non-DONE session
3. Use AskUserQuestion to confirm

### Load Constitution (Spec-Kit Mode)

In spec-kit mode, load the constitution following [constitution-loading.md](../_shared/constitution-loading.md).

## Step 1: Context Gathering (Parallel)

Spawn subagents in parallel using the Task tool to gather implementation context.

**For complete subagent prompts and details, see [context-gathering.md](references/context-gathering.md)**

### Legacy Mode (4 Subagents)

1. **Session Extraction** (Explore) - Extract complete session spec from plan file
2. **Platform Rules** (Explore) - Extract relevant CLAUDE.md rules for this phase
3. **Dependency Check** (Explore) - Verify phase and session dependencies are met
4. **Existing Code Inventory** (Explore) - Survey what files/functions already exist

### Spec-Kit Mode (4 Subagents)

1. **Task Extraction** (Explore) - Read tasks.md + spec.md + plan.md + constitution for the target tasks
2. **Platform Rules** (Explore) - Extract relevant CLAUDE.md rules based on task file paths
3. **Dependency Check** (Explore) - Verify earlier tasks and phases are complete
4. **Existing Code Inventory** (Explore) - Survey what files/functions already exist

## Step 2: Pre-Implementation Check

After gathering context, verify:

1. **Dependencies satisfied?** If not, report what's blocking and use AskUserQuestion:
   ```
   Header: "Blocked"
   Question: "Task TXXX is blocked by {dependency}. What would you like to do?"
   Options:
   - "Implement dependency first" - Switch to the blocking task/session
   - "Skip dependency check" - Proceed anyway (may cause issues)
   - "Show dependency status" - List all task/session statuses
   ```

2. **Work not already done?** If tasks are `[X]` or session is `[DONE]`, confirm re-implementation.

3. **Context fits?** If >15 tasks in scope, warn and offer batch ranges (see [checkpoint-protocol.md](references/checkpoint-protocol.md)).

4. **Ready to start?** Present summary and use AskUserQuestion:
   ```
   Header: "Ready"
   Question: "Implement [work items]?"
   Options:
   - "Start implementation" (Recommended) - Begin with first task
   - "Show task list first" - Display all tasks before starting
   - "Check a specific task" - Jump to a particular task
   ```

## Step 3: Implementation

### 3.0 Task Scheduling (Spec-Kit Mode)

Parse `[P]` markers and build an execution plan. See [parallel-execution.md](references/parallel-execution.md).

```
LEGACY MODE: sequential (unchanged — process tasks in session order)

SPEC-KIT MODE: parse [P] markers into execution plan
  Consecutive [P] tasks -> parallel batch (via Task tool)
  Non-[P] task -> sequential barrier

  Example:
    T001 [P], T002 [P] -> Batch 1 (parallel)
    T003 (no [P])       -> Barrier (sequential)
    T004 [P], T005 [P] -> Batch 2 (parallel)
```

**Max 4 parallel tasks per batch.** If more, split into sub-batches.

### 3.1 Announce Task
State which task you're implementing: "Implementing Task {X.Y.Z}: {task title}" or "Implementing T001: {task title}"

### 3.2 Check API Correctness (if Classic Mac)
For MacTCP/OT/AppleTalk code, verify function signatures against Retro68 headers.

**Build Environment:** The project uses Docker for Retro68 cross-compilation:
```bash
docker compose -f docker/docker-compose.yml run --rm peertalk-dev \
  grep -n "function_name" /Retro68/.../CIncludes/MacTCP.h
```

Key headers (in Docker at `/Retro68/.../CIncludes/`):
- MacTCP.h, OpenTransport.h, OpenTptInternet.h, ADSP.h, AppleTalk.h

### 3.3 Documentation Lookup (On-Demand)

When implementing Classic Mac code and you encounter uncertainty, use the `/mac-api` skill for documentation lookups. See [patterns.md](references/patterns.md) for when to use vs skip.

### 3.4 Write Code

Implement the task following:
- Code examples from the session specification or task description
- CLAUDE.md patterns and restrictions
- Existing code style in the codebase
- Constitution principles (spec-kit mode)

**Code Quality Gates (from CLAUDE.md):**
- Max function length: 100 lines (prefer 50)
- Max file size: 500 lines
- Cyclomatic complexity: 15 max per function
- Compiler warnings: Treat as errors

**ISR-Safety (if applicable):**
- NO memory allocation in ASR/notifier/completion
- NO synchronous network calls
- NO TickCount() - use pre-set timestamps or OT timing functions
- Set flags only; process in main loop
- Use pt_memcpy_isr() not pt_memcpy()

**For detailed implementation patterns and guidance, see [patterns.md](references/patterns.md)**

### 3.5 Verify Task

After implementing each task:
- Does it compile? (for POSIX: `make`)
- Does it match the specification/acceptance criteria?
- Are all CRITICAL/IMPORTANT notes addressed?

**If in "task by task" mode**, use AskUserQuestion after each task to confirm continuation.

### 3.6 Mark Task Complete (Spec-Kit Mode)

After verification passes, mark the task in tasks.md. See [task-tracking.md](references/task-tracking.md).

```markdown
# Before
- [ ] T001 [P] [US1] Define pt_peer struct — `include/peertalk.h`

# After
- [X] T001 [P] [US1] Define pt_peer struct — `include/peertalk.h`
```

### 3.7 Periodic Checkpoint

Every 5 tasks (sequential) or after each batch (parallel), run a checkpoint. See [checkpoint-protocol.md](references/checkpoint-protocol.md).

Report:
- Progress (N/total tasks)
- Files created/modified
- Quick compile check
- Use AskUserQuestion: Continue / Pause / Show status

If >15 tasks in phase, warn and offer batch ranges.

## Step 4: Session Verification

After all tasks complete, run the verification checklist.

**For complete verification steps, see [verification.md](references/verification.md)**

Quick checklist:
1. Build verification (POSIX: `make clean && make test`)
2. Run session-specific tests
3. Check acceptance criteria
4. Memory/leak check (valgrind for POSIX)
5. Quality gates (function length, file size, complexity, coverage)
6. ISR-safety check (for Mac code: `/check-isr`)
7. Code style verification

**Spec-kit mode additional checks:**
8. Spec compliance (match acceptance criteria from spec.md user stories)
9. Constitution compliance (implementation aligns with principles)
10. Cross-task consistency (after parallel batches — verify compatible implementations)

**If verification passes:** Proceed to Step 5
**If verification fails:** Report issues, do NOT mark complete, use AskUserQuestion to decide how to proceed

## Step 5: Status Update

If all verification passes:

### Legacy Mode

1. **Update session status** in the phase file:
   - Change `[OPEN]` or `[IN PROGRESS]` to `[DONE]`
   - Use Edit tool to modify the Session Scope Table row

2. **Report completion**

### Spec-Kit Mode

1. **Tasks already marked `[X]`** during implementation (Step 3.6)
2. **If all tasks in a phase are `[X]` or `[~]`**, add `✓` to the phase header in tasks.md
3. **Report completion** with task summary

## Step 6: Next Steps

### Legacy Mode

```
Session {X.Y} complete!

Files created/modified:
  - {list of files}

Recommended next steps:
  1. /build test          - Run automated tests (POSIX)
  2. /check-isr           - Validate ISR safety (if Mac code)
  3. /hw-test generate {X.Y}  - Create hardware test plan
  4. /build package       - Build Mac binaries for transfer
  5. /session complete {X.Y}  - Mark session done
  6. /clear && /session   - Find next session
```

### Spec-Kit Mode

```
Tasks TXXX-TYYY complete!

Files created/modified:
  - {list of files}

Recommended next steps:
  1. /build test           - Run automated tests (POSIX)
  2. /check-isr            - Validate ISR safety (if Mac code)
  3. /review analyze plan/ - Post-implementation consistency check
  4. /implement next       - Continue to next pending task
```

Then use AskUserQuestion:

```
Header: "Next"
Question: "What would you like to do next?"
Options:
- "Verify with /build test" (Recommended) - Run automated tests
- "Check ISR safety" - Run /check-isr on Mac code
- "Continue to next tasks" - /implement next
- "Done for now" - End implementation session
```
