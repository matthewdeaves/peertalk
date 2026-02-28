---
name: review
description: Evaluates a PeerTalk phase plan or spec-kit artifacts for implementability, architectural soundness, and performance. Supports both legacy PHASE-*.md and spec-kit formats (constitution, spec, plan, tasks). Spawns parallel subagents for API verification, documentation checks, and design review. In spec-kit mode, adds constitution alignment, cross-artifact consistency, and coverage analysis. Offers to auto-apply recommended changes. Use before implementing or after drafting a plan/spec.
argument-hint: <plan-file-or-artifact>
---

# Review: $ARGUMENTS

Evaluate `$ARGUMENTS` for implementability, architectural soundness, cross-platform debugging support, and CPU performance.

## Step 0: Format Detection

**Detect whether this is a legacy PHASE-*.md review or spec-kit artifact review.**

Follow the detection algorithm in [artifact-detection.md](../_shared/artifact-detection.md):

1. Check `$ARGUMENTS` for format indicators (file pattern, task ID, etc.)
2. If ambiguous, scan the project for artifacts
3. Announce the detected format before proceeding

**Load constitution** following [constitution-loading.md](../_shared/constitution-loading.md):
- Search for constitution.md in standard locations
- Fall back to PROJECT_GOALS.md + CLAUDE.md synthesis
- Last resort: hardcoded Safety > Goals > Simplicity > Performance > Debuggability

**Branch all subsequent behavior based on detected format.**

## Step 1: Clarify Phase (Spec-Kit Mode) / Skip (Legacy Mode)

**Spec-kit mode:** Before fact-gathering, scan artifacts for ambiguities.

Follow the protocol in [clarify-protocol.md](references/clarify-protocol.md):

1. Scan spec.md, plan.md, and tasks.md using the 11-category ambiguity taxonomy
2. Select the top 5 most impactful ambiguities
3. Present via AskUserQuestion with multi-choice options
4. Record resolutions in spec.md under `## Clarifications / ### Session YYYY-MM-DD`

**Skip conditions:** Already reviewed (clarifications section exists for today), `--no-clarify` flag in arguments, no significant ambiguities detected.

**Legacy mode:** Skip this step unless `--clarify` flag is explicitly passed. If passed, run the clarify protocol but record resolutions in the synthesis output instead of a separate file.

## Step 2: Parallel Fact-Gathering

Spawn subagents in parallel using the Task tool.

**For complete subagent prompts, see [subagent-prompts.md](references/subagent-prompts.md)**

### Domain Subagents (Both Modes)

Only spawn domain subagents that are relevant to the content being reviewed. Check if the artifact references the platform before spawning.

1. **MPW/Retro68 API Verification** (Explore) — if plan references Classic Mac APIs
2. **MacTCP Documentation Review** (use `/mac-api`) — if content references MacTCP
3. **Open Transport Documentation Review** (use `/mac-api`) — if content references OT
4. **AppleTalk Documentation Review** (use `/mac-api`) — if content references AppleTalk
5. **Inside Macintosh / ISR Safety Review** (use `/mac-api`) — if content involves callbacks/interrupts
6. **CSEND Lessons Audit** (Explore) — if plan/CSEND-LESSONS.md exists and is relevant
7. **Phase Continuity Check** (Explore) — legacy mode always; spec-kit mode if plan.md has phases
8. **Logging & Debugging Review** (Explore) — both modes
9. **Data-Oriented Design Review** (general-purpose) — both modes

### Spec-Kit Subagents (Spec-Kit Mode Only)

Spawn these additional subagents alongside domain subagents:

10. **Constitution Alignment Check** (general-purpose)
    - Read the loaded constitution
    - Read spec.md and plan.md design decisions
    - Check each decision against constitution principles
    - Flag contradictions with principle citations
    - See [analyze-passes.md](references/analyze-passes.md) Pass 4

11. **Cross-Artifact Consistency Analysis** (general-purpose)
    - Run 6 detection passes from [analyze-passes.md](references/analyze-passes.md)
    - Check duplication, ambiguity, underspecification, constitution, coverage, inconsistency
    - Produce summary table with issue counts

12. **Coverage Analysis** (general-purpose)
    - Build traceability matrix: user stories → acceptance criteria → tasks
    - Identify gaps (stories with no tasks, tasks with no story)
    - Report coverage percentage
    - See [analyze-passes.md](references/analyze-passes.md) Pass 5

### Build Environment Note

The project uses Docker for Retro68 cross-compilation:
```bash
docker compose -f docker/docker-compose.yml run --rm peertalk-dev \
  grep -n "function_name" /Retro68/.../CIncludes/MacTCP.h
```

MPW headers location:
- In Docker: `/Retro68/InterfacesAndLibraries/MPW_Interfaces/.../CIncludes/`
- In project: `resources/retro68/MPW_Interfaces.zip`

## Step 3: Synthesis

Once all subagents complete, synthesise their findings into a standardized format.

**For complete synthesis format and templates, see [synthesis-format.md](references/synthesis-format.md)**

### Legacy Mode Output Structure

1. **Summary Verdict** (IMPLEMENTABLE / NEEDS REVISION / BLOCKED)
2. **API/Documentation Issues** (grouped by platform)
3. **CSEND-LESSONS Corrections**
4. **Phase Continuity Gaps**
5. **Logging & Debugging Gaps**
6. **Performance Concerns**
7. **Recommended Plan Changes**
8. **Apply Changes Prompt**

### Spec-Kit Mode Output Structure

All legacy sections above, PLUS:

9. **Constitution Alignment Issues** — decisions that contradict loaded constitution
10. **Cross-Artifact Consistency** — summary table from 6 analysis passes
11. **Coverage Map** — traceability matrix showing spec → task coverage
12. **Requirement Quality Checklist** — from [checklist-template.md](references/checklist-template.md)

After presenting the full review, use AskUserQuestion:

```
Ready to apply recommended changes?

All decisions will be made based on:
• Constitution principles (if loaded)
• PROJECT_GOALS.md requirements
• Best architecture/design patterns for Classic Mac
• Performance optimization for 68k/PPC hardware
• ISR-safety requirements from CLAUDE.md

Options:
- "Apply all changes" (Recommended) - Fix all issues now
- "Review only" - Keep the review as documentation, apply manually later
```

If user selects "Apply all changes", proceed to Step 4 (legacy) or Step 4+5 (spec-kit).

## Step 4: Task Generation (Spec-Kit Mode Only)

If spec.md and plan.md exist but tasks.md does NOT:

Use AskUserQuestion:

```
Header: "Generate tasks"
Question: "No tasks.md found. Generate tasks from the reviewed spec and plan?"
Options:
- "Generate tasks" (Recommended) - Create tasks.md with T001 format
- "Skip task generation" - I'll create tasks manually
```

If user selects "Generate tasks", follow [task-generation.md](references/task-generation.md):

1. Parse spec.md user stories and plan.md phases
2. Generate tasks with T001 IDs, `[P]` markers, `[USn]` labels
3. Assign `[P]` based on file independence analysis
4. Write tasks.md
5. Verify coverage (every user story has tasks)

## Step 5: Apply Changes (On User Confirmation)

After presenting the synthesis, when the user confirms "Apply all changes", automatically apply ALL recommended changes.

**For complete decision-making rules and priority levels, see [auto-apply-rules.md](references/auto-apply-rules.md)**

### Decision-Making Hierarchy

**If constitution is loaded:** Use constitution's priority order for decisions.

**If no constitution:** Fall back to:
1. **Safety first** - ISR-safety, memory safety, crash prevention
2. **Project goals** - Match PROJECT_GOALS.md requirements
3. **Simplicity** - Fewer moving parts, less indirection
4. **Performance** - Cache efficiency, memory bandwidth (especially 68k)
5. **Debuggability** - Better logging, clearer state machines

### What Can Be Modified

**Legacy mode:** The plan file at `$ARGUMENTS`, plus CLAUDE.md if needed.

**Spec-kit mode:** spec.md, plan.md, tasks.md, data-model.md. Add constitution citation to each auto-applied change.

**NEVER auto-modify:** constitution.md (only the user changes the constitution).

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

---

## Guidance for Subagents

Include these instructions when spawning each subagent:
- Stay focused on your specific reference materials and evaluation criteria
- Quote file paths and line numbers where possible
- Don't speculate—if information isn't in your assigned sources, say so
- Keep responses concise; the main agent will synthesise
- For logging review: Reference plan/PHASE-0-LOGGING.md for PT_Log API. Flag any ISR-safety violations as CRITICAL.
