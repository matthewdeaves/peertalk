# Artifact Format Detection

Detect whether the user is working with legacy PeerTalk PHASE-*.md plans or spec-kit artifacts, and branch behavior accordingly.

## Detection Algorithm

### Step 1: Check the Argument

If the user provided an argument (file path or identifier):

| Pattern | Detected Format |
|---------|----------------|
| `plan/PHASE-*.md` | Legacy |
| `spec.md`, `plan.md`, `tasks.md`, `data-model.md` | Spec-kit |
| `constitution.md` | Spec-kit (constitution review) |
| `N N.Y` (e.g., `6 6.1`) | Legacy (phase + session) |
| `T001` or `T001-T010` | Spec-kit (task ID or range) |
| `next` | Auto-detect (see Step 2) |
| Directory path (e.g., `plan/`) | Scan directory (see Step 2) |

### Step 2: Auto-Detection (No Argument or Ambiguous)

Scan the project for artifacts. Spec-kit stores artifacts in several possible locations:

```
SPEC-KIT INDICATORS (check in order):
1. .specify/memory/constitution.md OR plan/constitution.md OR ./constitution.md
2. specs/*/spec.md OR plan/spec.md OR ./spec.md
3. specs/*/tasks.md OR plan/tasks.md OR ./tasks.md
4. specs/*/plan.md OR plan/plan.md OR ./plan.md
5. specs/*/research.md (optional artifact)

LEGACY INDICATORS:
1. plan/PHASE-*.md files exist
2. plan/PROJECT_GOALS.md exists

RESULT:
- Only spec-kit artifacts found  -> spec-kit mode
- Only legacy artifacts found    -> legacy mode
- Both found                     -> hybrid mode (operate on whichever the user points at)
- Neither found                  -> report "no artifacts found"
```

**Note:** Spec-kit's default layout uses `specs/[feature-number]-[name]/` per feature branch. PeerTalk may also use `plan/` for spec-kit artifacts. Check both.

### Step 3: Report Detection

Always announce the detected format before proceeding:

```
**Format detected:** spec-kit (found constitution.md, spec.md, tasks.md)
```

or

```
**Format detected:** legacy (found PHASE-1 through PHASE-9 plans)
```

## Format Reference

### Legacy Format (PHASE-*.md)

```markdown
# Phase N: Title

## Session Scope Table
| Session | Title | Status | Dependencies |
|---------|-------|--------|--------------|
| N.1     | ...   | [OPEN] | Phase N-1    |
| N.2     | ...   | [DONE] | N.1          |

## Session N.1: Title
### Task N.1.1: Description
### Task N.1.2: Description
```

Key features:
- Sessions contain ordered tasks
- Status markers: `[OPEN]`, `[IN PROGRESS]`, `[DONE]`
- Dependencies reference other phases/sessions
- Monolithic: spec, plan, and tasks in one file

### Spec-Kit Format (Separated Artifacts)

```
constitution.md  - Root decision authority (principles, priorities)
spec.md          - What to build (user stories, acceptance criteria)
plan.md          - How to build it (phases, architecture decisions)
tasks.md         - Work items (T001 format, [P] markers, [USn] labels)
data-model.md    - Data structures and schemas (optional)
```

Key features:
- Separated concerns (spec vs plan vs tasks)
- Constitution as root decision authority
- Task IDs: `T001` zero-padded, globally unique
- Parallel markers: `[P]` = can run alongside neighbors
- Story tracing: `[US1]` links tasks to spec user stories
- Completion: `- [ ]` pending, `- [X]` complete, `- [~]` skipped

## Hybrid Mode

Both formats can coexist during transition. Rules:

1. Operate on whichever format the user explicitly references
2. Never auto-convert between formats
3. If reviewing a legacy plan, use legacy synthesis format
4. If reviewing spec-kit artifacts, use spec-kit synthesis format
5. Constitution (if present) applies to BOTH formats as decision authority
