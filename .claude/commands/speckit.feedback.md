---
description: Close the spec-driven development loop by capturing findings from testing, debugging, or production and propagating them back into spec artifacts as research entries, requirements, and tasks.
handoffs:
  - label: Analyze Updated Artifacts
    agent: speckit.analyze
    prompt: Run a project analysis for consistency after feedback integration
    send: true
  - label: Implement Remediation Tasks
    agent: speckit.implement
    prompt: Implement the newly created remediation tasks
    send: true
---

## User Input

```text
$ARGUMENTS
```

You **MUST** consider the user input before proceeding (if not empty).

## Purpose

This command closes the feedback loop that `/speckit.implement` leaves open.

The Spec-Driven Development methodology describes bidirectional feedback as a core principle: production metrics and incidents don't just trigger hotfixes — they update specifications for the next regeneration. But no command automates this, and the existing generative commands (`specify`, `plan`, `tasks`) cannot perform incremental updates — they create artifacts from scratch, which would overwrite completed work. Without `/speckit.feedback`, the workflow is:

```
specify → plan → tasks → implement → (gap) → ad-hoc fixes → spec drift
```

With `/speckit.feedback`:

```
specify → plan → tasks → implement → feedback → analyze → implement
```

This command takes a finding — a bug, a performance discovery, a platform behavior, a wrong assumption — and systematically propagates it through research.md, spec.md, plan.md, and tasks.md so the spec artifacts stay truthful and new remediation work is tracked.

## Design Principle: Append-Only with Mandatory Verification

This command touches artifacts owned by other commands (`specify` owns spec.md, `plan` owns plan.md and research.md, `tasks` owns tasks.md). It earns that right through two strict constraints:

1. **Append-only**: Feedback NEVER modifies or deletes existing content in any artifact. It only adds new entries — new research findings, new edge cases, new requirements, new tasks. The content created by `specify`, `plan`, and `tasks` is treated as immutable.

2. **Mandatory analyze**: After applying changes, feedback ALWAYS runs a consistency check equivalent to `/speckit.analyze` (step 9 below). This catches any inconsistencies between the appended content and existing artifacts before the user proceeds.

If a finding requires **modifying** existing requirements, architecture, or task descriptions (not just adding new ones), feedback will flag this and recommend re-running the owning command (`/speckit.specify`, `/speckit.plan`, or `/speckit.tasks`) with the finding as input context.

## Outline

1. **Setup**: Run `.specify/scripts/bash/check-prerequisites.sh --json --include-tasks` from repo root and parse JSON for FEATURE_DIR and AVAILABLE_DOCS list. All paths must be absolute. For single quotes in args like "I'm Groot", use escape syntax: e.g 'I'\''m Groot' (or double-quote if possible: "I'm Groot").

2. **Load all spec artifacts**:
   - **REQUIRED**: spec.md, plan.md, tasks.md
   - **REQUIRED**: `.specify/memory/constitution.md`
   - **IF EXISTS**: research.md, data-model.md, contracts/, quickstart.md

3. **Parse the feedback** from user input. Extract four elements:

   | Element | Description |
   |---------|-------------|
   | **What happened** | The observed behavior or discovery |
   | **What was expected** | What the spec/plan assumed would be true |
   | **Root cause** | Why the divergence occurred (if known) |
   | **Impact** | Which parts of the system are affected |

   If the user provides a short description, infer what you can from the spec artifacts and ask **at most 2** clarifying questions to fill gaps. Do NOT proceed without understanding what happened and what was expected.

4. **Classify the feedback** into one or more categories:

   | Category | Trigger Signal | Target Artifact |
   |----------|---------------|-----------------|
   | Missing Knowledge | "We didn't know X" | research.md |
   | Missing Requirement | "The spec didn't account for X" | spec.md |
   | Wrong Assumption | "We assumed X but reality is Y" | research.md + plan.md |
   | Architecture Gap | "The design can't handle X" | plan.md + data-model.md |
   | New Work Needed | "We need to build/fix X" | tasks.md |
   | Constitution Tension | "Principle X conflicts with reality Y" | FLAG only |

   A single finding may hit multiple categories. Process all that apply.

5. **Determine sequential IDs** for new entries:
   - Scan research.md for highest `R{N}` entry → next is `R{N+1}`
   - Scan tasks.md for highest `T{NNN}` entry → next is `T{NNN+1}`
   - Never reuse or skip IDs

6. **Propose artifact updates** — generate precise **append-only** changes for each affected artifact. Feedback MUST NOT modify or delete any existing content — only append new entries.

   ### research.md (append new entry)

   Append a new entry at the end, using existing format:

   ```markdown
   ## R{N}. {Title}

   **Decision**: {What was discovered or decided}

   **Rationale**: {Why this matters, with evidence}

   **Alternatives considered**: {What else was evaluated, if applicable}

   **Source**: {How this was discovered: hardware test, production incident,
   implementation discovery, external documentation}
   ```

   ### spec.md (append new requirements or edge cases)

   Append one or more of:
   - New functional requirement (appended to existing FR list)
   - New non-functional requirement (appended to existing NFR list)
   - New edge case entry (appended to Edge Cases section)

   **NEVER modify existing requirements.** If an existing requirement is wrong, flag it for `/speckit.specify` to handle:
   > "Existing FR-{N} assumes {X}, but reality is {Y}. Recommend updating via `/speckit.specify`."

   Also append to a `## Feedback Log` section (create at end of file if not present):

   ```markdown
   - **{YYYY-MM-DD}**: {One-line summary} → R{N}, T{NNN}
   ```

   ### plan.md (append new constraints)

   - Append new constraints to Technical Context section
   - Append new notes to relevant architecture sections

   **NEVER modify existing architecture decisions.** If an existing decision is wrong, flag it for `/speckit.plan` to handle:
   > "Plan section {X} assumes {Y}, which contradicts finding. Recommend re-running `/speckit.plan` with this context."

   ### tasks.md (append new tasks)

   Append new tasks using strict checklist format:

   ```text
   - [ ] T{NNN} [P?] [Story?] Description with file path
   ```

   Place in the most appropriate existing phase, or append a new phase at the end:

   ```markdown
   ## Phase {N}: Feedback Remediation

   **Purpose**: Address findings from testing/production feedback

   - [ ] T{NNN} Description with file path
   ```

   **NEVER modify existing tasks** — completed `[x]` tasks are immutable, pending `[ ]` tasks belong to `/speckit.tasks`.

   ### data-model.md (append new fields or constraints)

   - Append new fields, constraints, or relationships
   - **NEVER modify existing entity definitions** — flag for `/speckit.plan`

   ### Constitution tension

   - **Do NOT modify constitution.md** — only flag the tension
   - Report: "Principle [{name}] states [{rule}], but reality requires [{what}]"
   - Recommend: run `/speckit.constitution` separately if a principle needs amending

7. **Present changes for approval**:

   Group by artifact. For each change show:
   - File path
   - Section being modified
   - Exact text to add or replace
   - Why (traced back to the feedback)

   Then ask: **"Apply these changes? (all / select / none)"**

   - **all**: Apply everything
   - **select**: Let user pick which changes to apply
   - **none**: Abort, output the proposals as a report only

8. **Apply approved changes**:
   - Write updates to each artifact
   - Maintain formatting, heading hierarchy, and section ordering
   - Preserve all existing content — append or modify in place, never delete unrelated content
   - For tasks.md: verify new task IDs don't conflict with existing ones
   - Save each file after modification (atomic writes)

9. **Post-update validation (MANDATORY)** — this step is NOT optional. After applying changes, immediately run a consistency check:

   - Re-read all modified artifacts
   - Count requirements vs tasks → compute coverage delta
   - Flag any new requirements without corresponding tasks
   - Flag any new tasks without requirement traceability
   - Check constitution alignment of all new content
   - Verify no existing content was modified (diff check: only additions, no deletions)
   - If any inconsistencies found: report them clearly and recommend `/speckit.analyze` for full audit
   - If validation passes: report clean status

10. **Report**:

    ```markdown
    ## Feedback Integration Summary

    **Finding**: {one-line summary}
    **Date**: {YYYY-MM-DD}

    ### Artifacts Updated
    | Artifact | Change |
    |----------|--------|
    | research.md | Added R{N}: {title} |
    | spec.md | Added edge case: {description} |
    | tasks.md | Added T{NNN}: {description} |

    ### Coverage
    - Requirements: {before} → {after}
    - Tasks: {before} → {after}
    - Coverage: {before}% → {after}%

    ### Next Steps
    - {Recommended action with specific command}
    ```

    Recommended next steps by situation:
    - New tasks created → "Run `/speckit.implement` to execute T{NNN}-T{NNN}"
    - Major spec changes → "Run `/speckit.analyze` to verify cross-artifact consistency"
    - Constitution tension → "Run `/speckit.constitution` to review principle [{name}]"
    - Multiple findings → "Run `/speckit.feedback` again for each additional finding"

## Feedback Examples

### Bug from hardware testing

**Input**: "test_lifecycle crashes on Performa 6400 — WaitNextEvent dereferences garbage GrafPort pointer because Retro68 console apps have no Toolbox init"

**Classification**: Missing Knowledge + Missing Requirement + New Work

**Proposed updates**:
- research.md: R11 documenting Retro68 console app lifecycle (no auto Toolbox init)
- spec.md: New edge case "Classic Mac builds require platform-appropriate sleep/yield without Toolbox dependency"
- tasks.md: T033 "Replace WaitNextEvent with Delay() in tests/test_common.h"

### Performance discovery

**Input**: "OT STREAMS allocates minimum 64-byte mblk_t buffers — each OTSnd copies into a 64-byte buffer regardless of send size, hard-limiting throughput to ~6 KB/s"

**Classification**: Missing Knowledge + Wrong Assumption

**Proposed updates**:
- research.md: R12 documenting OT STREAMS buffer architecture with evidence from OT book line 41254
- plan.md: Update OT architecture section noting send-path throughput ceiling
- spec.md: Update NFR for throughput with platform-specific caveat

### Constitution tension

**Input**: "Constitution says single-alloc arena but OT requires OTAllocMem at interrupt time for safe buffer allocation"

**Classification**: Constitution Tension

**Proposed updates**:
- FLAG: "Principle [Single Allocation] says [one malloc/NewPtr at init], but OT interrupt-time allocation requires OTAllocMem per Table C-1"
- Recommend: `/speckit.constitution` to add platform-specific exception

## Key Rules

### Append-Only Discipline
- **NEVER modify existing content** in any artifact — only append new entries
- **NEVER delete content** from any artifact
- **NEVER modify constitution.md** — only flag tensions for separate review
- If existing content is wrong, **flag it** and recommend re-running the owning command (`/speckit.specify`, `/speckit.plan`, or `/speckit.tasks`) with the finding as context
- This constraint is what allows feedback to safely touch artifacts owned by other commands

### Safety
- **ALWAYS show changes before applying** — no silent writes
- **ALWAYS run post-update validation** (step 9) — this is mandatory, not optional
- **ALWAYS maintain sequential IDs** — scan existing entries, never guess or reuse
- **ALWAYS trace** — every new task links to a requirement, every requirement links to research
- Ask at most 2 clarifying questions — if input is too vague to classify, ask; otherwise proceed
- One finding per invocation — for multiple findings, run the command multiple times
- Use absolute paths for all file operations
