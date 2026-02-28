# Spec-Kit Format Reference

Reference for the spec-kit artifact-driven workflow format. Used by both /review and /implement skills.

## Task Format

```markdown
- [ ] T001 [P] [US1] Task title — `path/to/file.c`
```

### Task IDs

- Format: `T` + 3-digit zero-padded number
- Globally unique within a tasks.md file
- Sequential within each phase
- Examples: `T001`, `T042`, `T100`

### Parallel Marker `[P]`

- `[P]` = this task can run in parallel with adjacent `[P]` tasks
- No `[P]` = sequential barrier (must complete before next task starts)
- Parallelism is within a phase, not across phases

```markdown
## Phase 1: Core Types

- [ ] T001 [P] [US1] Define pt_peer struct — `include/peertalk.h`
- [ ] T002 [P] [US1] Define pt_context struct — `include/peertalk.h`
- [ ] T003 [US2] Implement pt_init() — `src/core/pt_init.c`
```

In this example: T001 and T002 can run in parallel. T003 is a barrier — it waits for T001+T002.

### User Story Labels `[USn]`

- `[US1]`, `[US2]`, etc. trace tasks to user stories in spec.md
- A task may have multiple labels: `[US1] [US3]`
- Labels enable coverage analysis (every US should have tasks)

### File Paths

- After the em dash (`—`), list the primary file(s) the task touches
- Multiple files: `path/a.c`, `path/b.h`
- Used for parallel safety analysis (tasks touching same files can't be parallel)

## Completion Markers

| Marker | Meaning |
|--------|---------|
| `- [ ]` | Pending — not started |
| `- [X]` | Complete — implemented and verified |
| `- [~]` | Skipped — intentionally not done (add reason) |

### Marking Rules

- Mark `[X]` only after the task passes verification (compiles, tests pass)
- Mark `[~]` with a reason: `- [~] T005 [US3] Skipped — deferred to v3`
- Never remove a task — mark it `[~]` instead
- Marking is done by editing tasks.md directly

## Phase Headers

```markdown
## Phase 1: Core Types

- [ ] T001 ...
- [ ] T002 ...

## Phase 2: POSIX Backend

- [ ] T003 ...
```

- Phases are `## Phase N: Title` headers
- Tasks within a phase are ordered
- Phase completion = all tasks `[X]` or `[~]`

## Spec.md Structure

```markdown
# Specification: Project Name

## User Stories

### US1: Story title
As a [role], I want [capability], so that [benefit].

**Acceptance Criteria:**
- [ ] Criterion one
- [ ] Criterion two

### US2: Story title
...

## Clarifications

### Session YYYY-MM-DD
Q: Question asked during review
A: Answer provided by user
```

## Plan.md Structure

```markdown
# Plan: Project Name

## Architecture Overview
...

## Phase 1: Title
### Objective
### Key Decisions
### Deliverables

## Phase 2: Title
...
```

## Constitution.md Structure

See [constitution-loading.md](constitution-loading.md) for details.

## Full Artifact Set

Spec-kit defines these artifacts (not all are required):

| Artifact | Purpose | Required? |
|----------|---------|-----------|
| `constitution.md` | Root decision authority (principles, priorities) | Recommended |
| `spec.md` | What to build (user stories, acceptance criteria) | Yes |
| `research.md` | Technical context, library comparisons, prior art | Optional |
| `plan.md` | How to build it (phases, architecture decisions) | Yes |
| `data-model.md` | Data structures, schemas, entity definitions | Optional |
| `contracts/` | API specifications, interface definitions | Optional |
| `quickstart.md` | Key validation scenarios | Optional |
| `tasks.md` | Work items (T001 format, [P] markers, [USn] labels) | Yes |

The `/review` and `/implement` skills primarily operate on `constitution.md`, `spec.md`, `plan.md`, and `tasks.md`. Optional artifacts are read as context when present.

**Default location:** `specs/[feature-number]-[name]/` per feature branch, or `plan/` (PeerTalk convention).

## Cross-Artifact Traceability

```
constitution.md
    ↓ informs
spec.md (user stories + acceptance criteria)
    ↓ traced by [USn]
tasks.md (work items)
    ↑ implements
plan.md (architecture + phasing)
```

Every user story in spec.md should be traceable to at least one task via `[USn]` labels. The /review skill's coverage analysis checks this.
