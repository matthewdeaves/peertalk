# Checkpoint Protocol

Periodic progress checkpoints during implementation to prevent context loss and enable resume across interrupted sessions.

## When to Checkpoint

- **Every 5 tasks** (sequential mode) or **after each batch** (parallel mode)
- **After any failure** that requires user decision
- **When context is getting large** (>15 tasks in a phase)
- **Before any risky operation** (large refactor, multi-file structural change)

## Checkpoint Content

At each checkpoint, report:

```markdown
### Checkpoint: Tasks T001-T005

**Progress:** 5/20 tasks complete (25%)
**Phase:** Phase 1: Core Types

**Files created:**
- include/peertalk.h (new)
- src/core/pt_init.c (new)

**Files modified:**
- Makefile (added new targets)

**Compile status:** Passing
**Tests:** 3/3 passing

**Next up:** T006-T010 (Phase 2: POSIX Backend)
```

## User Interaction

After presenting the checkpoint, use AskUserQuestion:

```
Header: "Checkpoint"
Question: "Completed 5/20 tasks. Continue?"
Options:
- "Continue" (Recommended) - Proceed to next batch
- "Pause" - Stop here, can resume later
- "Show status" - Detailed view of all task states
- "Run tests" - Full test suite before continuing
```

## Resume Protocol

When `/implement` is invoked and tasks.md has some `[X]` markers:

### Step 1: Detect Resume State

```
Scan tasks.md for completion state:
- Count [X] tasks (completed)
- Count [~] tasks (skipped)
- Count [ ] tasks (pending)
- Find first pending task
```

### Step 2: Offer Resume

```
Header: "Resume"
Question: "Found 5 completed tasks. Resume from T006?"
Options:
- "Resume from T006" (Recommended) - Continue where left off
- "Start over" - Re-implement from T001
- "Show completed tasks" - Review what was done
- "Jump to specific task" - Pick a different starting point
```

### Step 3: Context Recovery

When resuming:

1. Read all `[X]` task descriptions to understand what's done
2. Read the files those tasks created/modified (code inventory)
3. Load platform rules and constitution (same as fresh start)
4. Begin implementation from the first pending task

## Long Session Warning

If a phase has more than 15 tasks:

```
Header: "Large phase"
Question: "Phase 3 has 22 tasks. Implement all at once or in batches?"
Options:
- "Batch of 10" (Recommended) - Do T001-T010, then checkpoint
- "Batch of 5" - Smaller batches, more checkpoints
- "All at once" - Attempt all 22 (may hit context limits)
- "Custom range" - Specify task range (e.g., T001-T008)
```

## Task Range Arguments

The implement skill accepts task ranges for partial execution:

| Argument | Meaning |
|----------|---------|
| `T001` | Single task |
| `T001-T010` | Range of tasks |
| `T001 T003 T007` | Specific tasks (space-separated) |
| `next` | First pending task |
| `next 5` | Next 5 pending tasks |

## Checkpoint File (Optional)

For very long implementations, optionally write a checkpoint file:

```markdown
<!-- .claude/checkpoint.md - auto-generated, do not edit -->
# Implementation Checkpoint

**Timestamp:** 2026-02-27T14:30:00Z
**Tasks file:** plan/tasks.md
**Last completed:** T005
**Next pending:** T006
**Compile status:** passing
**Test status:** 3/3 passing
**Files modified this session:**
- include/peertalk.h
- src/core/pt_init.c
- Makefile
```

This file is for reference only — the source of truth is always the `[X]` markers in tasks.md.
