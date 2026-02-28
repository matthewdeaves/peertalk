# Task Tracking

Rules for marking task completion in spec-kit tasks.md and legacy PHASE-*.md files.

## Spec-Kit Mode

### Marking Complete `[X]`

After a task passes verification (compiles, tests pass, acceptance criteria met):

```markdown
# Before
- [ ] T001 [P] [US1] Define pt_peer struct — `include/peertalk.h`

# After
- [X] T001 [P] [US1] Define pt_peer struct — `include/peertalk.h`
```

**Rules:**
- Use Edit tool to change `- [ ]` to `- [X]`
- Only mark after verification passes
- Never mark ahead of time or optimistically
- Mark immediately after verification, before moving to next task

### Marking Skipped `[~]`

When a task is intentionally not implemented:

```markdown
- [~] T005 [US3] Implement AppleTalk backend — deferred to v3
```

**Rules:**
- Add a reason after the em dash
- Use when: task is out of scope, superseded, or blocked indefinitely
- Requires user confirmation via AskUserQuestion before marking

### Never Remove Tasks

- Don't delete task lines from tasks.md
- Use `[~]` for tasks that won't be done
- This preserves the task ID sequence and traceability

### Phase Completion

A phase is complete when all its tasks are `[X]` or `[~]`:

```markdown
## Phase 1: Core Types ✓

- [X] T001 [P] [US1] Define pt_peer struct — `include/peertalk.h`
- [X] T002 [P] [US1] Define pt_context struct — `include/peertalk.h`
- [X] T003 [US1] Implement pt_init() — `src/core/pt_init.c`
- [~] T004 [US1] Write fuzz tests — deferred, not critical for v2
```

Add a `✓` to the phase header when complete.

## Legacy Mode

### Session Status Updates

In PHASE-*.md files, update the Session Scope Table:

```markdown
# Before
| N.1 | Title | [OPEN] | Phase N-1 |

# After
| N.1 | Title | [DONE] | Phase N-1 |
```

**Status progression:**
- `[OPEN]` → `[IN PROGRESS]` → `[DONE]`
- Set `[IN PROGRESS]` when starting a session
- Set `[DONE]` only after all verification passes

### Equivalent Markers

| Spec-Kit | Legacy | Meaning |
|----------|--------|---------|
| `- [ ]` | `[OPEN]` | Not started |
| (in progress) | `[IN PROGRESS]` | Currently being worked on |
| `- [X]` | `[DONE]` | Complete and verified |
| `- [~]` | `[SKIPPED]` | Intentionally not done |

## Progress Reporting

### Spec-Kit Mode

```
Progress: 12/20 tasks complete (60%)
  Phase 1: 4/4 ✓
  Phase 2: 8/10 (in progress)
  Phase 3: 0/6 (pending)

Skipped: 1 task (T004 — deferred)
```

### Legacy Mode

```
Progress: Session 6.2 complete
  Phase 6: 2/4 sessions done
  Next: Session 6.3

Blocked: None
```

## Verification Before Marking

Before marking any task/session complete, always verify:

1. **Compiles** — `make docker-test` or equivalent
2. **Tests pass** — all relevant tests green
3. **Acceptance criteria** — each criterion checked
4. **ISR safety** — `/check-isr` for Mac platform code
5. **Quality gates** — function length, file size, complexity

Only after ALL checks pass should the marker be updated.
