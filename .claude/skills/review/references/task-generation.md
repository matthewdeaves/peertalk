# Task Generation

Generate spec-kit format tasks from a reviewed spec and plan. Used when spec/plan exist but tasks.md doesn't, or when the user requests task generation after review.

## When to Offer

- Spec-kit mode only
- After review synthesis is complete
- When `spec.md` and `plan.md` exist but `tasks.md` does not
- Or when user explicitly requests: `/review generate-tasks plan/`

## Task Generation Rules

### Task ID Assignment

- Start at `T001`, increment sequentially
- Zero-pad to 3 digits: `T001`, `T042`, `T100`
- IDs are globally unique within tasks.md (not per-phase)
- Never reuse an ID, even if a task is deleted

### Phase Grouping

Map plan.md phases to task phases:

```markdown
## Phase 1: Core Types

- [ ] T001 [P] [US1] Define pt_peer struct — `include/peertalk.h`
- [ ] T002 [P] [US1] Define pt_context struct — `include/peertalk.h`
- [ ] T003 [US1] Implement pt_init() with validation — `src/core/pt_init.c`

## Phase 2: POSIX Backend

- [ ] T004 [P] [US2] Implement TCP connect — `src/posix/platform_posix.c`
- [ ] T005 [P] [US2] Implement TCP listen/accept — `src/posix/platform_posix.c`
- [ ] T006 [US2] Implement UDP broadcast discovery — `src/posix/platform_posix.c`
```

### Parallel Marker Assignment `[P]`

A task gets `[P]` when ALL of these are true:

1. **No data dependency** on the previous task (doesn't read its output)
2. **No file conflict** with adjacent `[P]` tasks (different files, or different sections of same file)
3. **No ordering requirement** (task B doesn't need task A complete first)

**File independence analysis:**
- Tasks touching different files → can be parallel
- Tasks touching same header (adding different structs) → can be parallel if non-overlapping
- Tasks touching same .c file → generally NOT parallel (merge conflicts)
- Tasks where one defines a type and another uses it → NOT parallel

**Conservative default:** When unsure, omit `[P]`. Sequential is always safe.

### User Story Labels `[US1]`

- Read user stories from spec.md
- For each task, identify which user story it implements
- A task may implement multiple stories: `[US1] [US3]`
- Infrastructure tasks (build setup, test harness) may have no story label — that's OK
- Every user story should have at least one task

### Task Granularity

Each task should be:

- **Completable in one focused session** (< 500 lines of new code)
- **Independently verifiable** (compiles, test passes, or has observable output)
- **Clearly scoped** (specific files, specific functions)

**Too big:** "Implement the POSIX backend" → split into connect, listen, send, receive, discovery
**Too small:** "Add #include <string.h>" → merge into the task that needs it
**Just right:** "Implement pt_tcp_connect() for POSIX with error handling"

### File Path Assignment

After the em dash (`—`), list primary files:

```markdown
- [ ] T001 [P] [US1] Define pt_peer struct — `include/peertalk.h`
- [ ] T004 [US2] Implement TCP module — `src/posix/tcp_posix.c`, `src/posix/tcp_posix.h`
```

- Use backtick-quoted relative paths
- List 1-3 primary files (the ones created or significantly modified)
- Don't list every file touched (e.g., don't list Makefile for every task)

### Task Description

Keep task titles actionable and specific:

- Start with a verb: "Define", "Implement", "Add", "Create", "Write"
- Include the specific function/struct/module name
- Include the platform if relevant: "Implement pt_tcp_connect() for MacTCP"

## Output Format

Write to `tasks.md` (or the location specified by the user):

```markdown
# Tasks: Project Name

Generated from spec.md and plan.md review on YYYY-MM-DD.

## Phase 1: Core Types

- [ ] T001 [P] [US1] Define pt_peer struct — `include/peertalk.h`
- [ ] T002 [P] [US1] Define pt_context struct — `include/peertalk.h`
- [ ] T003 [US1] Implement pt_init() — `src/core/pt_init.c`
- [ ] T004 [US1] Write unit tests for core types — `tests/test_core.c`

## Phase 2: POSIX Backend

- [ ] T005 [P] [US2] Implement TCP connect — `src/posix/tcp_posix.c`
- [ ] T006 [P] [US2] Implement TCP listen/accept — `src/posix/tcp_posix.c`
- [ ] T007 [US2] Implement send/receive — `src/posix/tcp_posix.c`
- [ ] T008 [US3] Implement UDP discovery — `src/posix/discovery_posix.c`
- [ ] T009 [US2] [US3] Write integration tests — `tests/test_posix.c`

...
```

## Coverage Verification

After generating tasks, verify coverage:

1. Build the traceability matrix (see [analyze-passes.md](analyze-passes.md) Pass 5)
2. Report any user stories without tasks
3. Report any orphan tasks (no `[USn]` label)
4. Offer to fill gaps

## Interaction

After generating, use AskUserQuestion:

```
Header: "Tasks"
Question: "Generated N tasks across M phases. Review the tasks.md file?"
Options:
- "Looks good" (Recommended) - Accept the generated tasks
- "Adjust granularity" - Make tasks bigger or smaller
- "Add missing tasks" - I see gaps to fill
- "Regenerate" - Start over with different approach
```
