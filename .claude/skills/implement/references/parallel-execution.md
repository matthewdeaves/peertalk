# Parallel Task Execution

Exploit `[P]` markers in spec-kit tasks.md to execute independent tasks concurrently via the Task tool.

## When to Use

- **Spec-kit mode only** — legacy mode always executes sequentially
- Only when tasks have `[P]` markers
- Only when the batch has 2+ consecutive `[P]` tasks

## Execution Plan Construction

Parse tasks.md and build an execution plan:

```
INPUT (from tasks.md):
  T001 [P] — parallel
  T002 [P] — parallel
  T003     — sequential barrier
  T004 [P] — parallel
  T005 [P] — parallel
  T006 [P] — parallel
  T007     — sequential barrier

EXECUTION PLAN:
  Batch 1: T001, T002 (parallel via Task tool)
  Barrier: T003 (sequential, wait for Batch 1)
  Batch 2: T004, T005, T006 (parallel via Task tool)
  Barrier: T007 (sequential, wait for Batch 2)
```

## Batch Limits

- **Max parallel tasks per batch:** 4 (Task tool practical limit)
- If more than 4 consecutive `[P]` tasks, split into sub-batches of 4
- Each sub-batch completes before the next starts

## Task Tool Invocation

For each task in a parallel batch, spawn a Task agent:

```
Task tool call:
  subagent_type: "general-purpose"
  description: "Implement T001"
  prompt: |
    Implement task T001 from the PeerTalk project.

    TASK: [full task description from tasks.md]
    SPEC CONTEXT: [relevant user story and acceptance criteria from spec.md]
    CONSTITUTION: [relevant principles]
    PLATFORM RULES: [relevant rules from context gathering]

    FILES TO CREATE/MODIFY: [file paths from task]

    IMPLEMENTATION RULES:
    - Follow existing code style in the codebase
    - Max function length: 100 lines (prefer 50)
    - Max file size: 500 lines
    - All builds happen in Docker
    - Check ISR safety for Mac platform code

    VERIFICATION:
    - Code compiles (describe how)
    - Matches acceptance criteria
    - No ISR safety violations (if Mac code)

    Return: List of files created/modified and verification result.
```

## Parallel Safety Checks

Before executing a batch in parallel, verify:

1. **No file conflicts** — tasks in the same batch don't modify the same file
2. **No data dependencies** — no task reads output from another task in the batch
3. **No shared state** — tasks don't both modify the same struct/header section

If any check fails, demote the conflicting task to sequential:

```
ORIGINAL: T001 [P], T002 [P], T003 [P]
CHECK: T001 and T002 both modify include/peertalk.h
ADJUSTED:
  Sequential: T001
  Sequential: T002  (file conflict with T001)
  Sequential: T003  (lost parallelism due to cascade)
```

## After Batch Completion

After all tasks in a batch complete:

1. **Merge results** — collect files modified by each agent
2. **Compile check** — verify everything still compiles together
3. **Mark tasks** — edit tasks.md to mark each as `[X]`
4. **Cross-task consistency** — check that parallel implementations are compatible
5. **Report** — summarize batch results before proceeding

## Failure Handling

If a task in a parallel batch fails:

1. **Don't fail the whole batch** — other tasks may be fine
2. **Report the failure** with details
3. **Mark failed task** as still `[ ]` (pending)
4. **Continue** to the barrier task
5. **At the barrier**, use AskUserQuestion:
   ```
   Header: "Batch incomplete"
   Question: "T002 failed in Batch 1. How to proceed?"
   Options:
   - "Fix and retry T002" - Attempt to fix the issue
   - "Skip T002 and continue" - Mark as [~] and proceed
   - "Stop here" - Pause implementation for review
   ```

## Legacy Mode Fallback

In legacy mode (PHASE-*.md), tasks are always sequential. The implementation loop is:

```
For each task in session:
  1. Announce task
  2. Implement task
  3. Verify task
  4. Continue to next
```

No parallelism, no batch construction.
