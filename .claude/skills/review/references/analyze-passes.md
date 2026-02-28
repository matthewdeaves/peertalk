# Analyze Passes

Six detection passes for cross-artifact consistency analysis. Used in spec-kit mode to validate that constitution, spec, plan, and tasks are aligned.

## When to Run

- **Spec-kit mode:** Run as part of fact-gathering (additional subagents)
- **Legacy mode:** Run passes 1-2 only (duplication and ambiguity within PHASE-*.md)
- **Triggered by:** `/review analyze plan/` or automatically during spec-kit review

## Pass 1: Duplication Detection

Scan for repeated or redundant content across artifacts.

**Check for:**
- Same requirement stated differently in spec.md and plan.md
- Duplicate tasks in tasks.md (same work described twice)
- Redundant acceptance criteria (same criterion in multiple user stories)
- Copy-pasted sections that have diverged

**Report format:**
```
DUPLICATION: [artifact1:location] ↔ [artifact2:location]
  Content A: "..."
  Content B: "..."
  Recommendation: Consolidate into [preferred location]
```

## Pass 2: Ambiguity Detection

Scan for vague, undefined, or open-ended language.

**Flag words/phrases:**
- "appropriate", "suitable", "reasonable", "as needed"
- "etc.", "and so on", "similar"
- "should" (vs "must" — is it required or optional?)
- "may", "might", "could" (uncertainty)
- "TBD", "TODO", "to be determined"
- "fast", "efficient", "performant" (without metrics)

**Report format:**
```
AMBIGUITY in [artifact:line]: "[quoted text]"
  Category: [from taxonomy in clarify-protocol.md]
  Impact: [what goes wrong if misinterpreted]
  Suggestion: [concrete replacement text]
```

## Pass 3: Underspecification Detection

Identify things that are mentioned but not fully defined.

**Check for:**
- Functions referenced but not specified (signature, behavior, errors)
- Data structures mentioned but not defined
- Protocols described but not formatted (wire format, byte order)
- Error conditions mentioned but not handled
- State machines described but not all transitions listed
- Configuration options mentioned but no defaults given

**Report format:**
```
UNDERSPEC in [artifact:location]: [what's missing]
  Referenced in: [where it's used]
  Needed by: [what depends on it]
  Suggestion: [what to add]
```

## Pass 4: Constitution Alignment

Compare decisions in spec/plan/tasks against constitution principles.

**For each decision or design choice:**
1. Identify which constitution principle(s) apply
2. Check if the choice aligns with the principle
3. If principles conflict, check if the resolution matches the constitution's priority order

**Report format:**
```
CONSTITUTION ISSUE in [artifact:location]:
  Decision: "[what was decided]"
  Principle: "[relevant constitution principle]"
  Conflict: [how the decision contradicts the principle]
  Recommendation: [how to realign]
```

**Common violations:**
- Over-engineering when constitution values simplicity
- Missing error handling when constitution requires consistent error model
- Platform-specific code in core when constitution requires clean abstraction

## Pass 5: Coverage Analysis

Verify that every spec requirement traces to at least one task.

**Build a traceability matrix:**

```
| User Story | Acceptance Criteria | Tasks | Status |
|------------|-------------------|-------|--------|
| US1        | AC1.1             | T001  | Covered |
| US1        | AC1.2             | T003  | Covered |
| US2        | AC2.1             | —     | GAP     |
| US2        | AC2.2             | T007  | Covered |
```

**Check for:**
- User stories with no tasks (`[USn]` label not used)
- Acceptance criteria with no corresponding task verification
- Tasks with no user story label (orphan tasks — may be valid infrastructure)
- Plan phases with no tasks generated

**Report format:**
```
COVERAGE GAP: US2 AC2.1 has no implementing task
  Acceptance criterion: "[text]"
  Recommendation: Add task T0XX [US2] to Phase N
```

## Pass 6: Inconsistency Detection

Find contradictions between artifacts.

**Cross-check:**
- spec.md acceptance criteria vs tasks.md completion criteria
- plan.md architecture decisions vs tasks.md implementation approach
- plan.md phase ordering vs tasks.md phase ordering
- spec.md data model vs data-model.md definitions
- Task file paths vs actual project file structure
- Constant values (ports, magic numbers, limits) across all artifacts

**Report format:**
```
INCONSISTENCY: [artifact1:location] vs [artifact2:location]
  In spec: "[text from spec]"
  In plan: "[text from plan]"
  Resolution: [which is correct and why]
```

## Summary Report

After all passes, produce a summary:

```markdown
### Cross-Artifact Analysis Summary

| Pass | Issues Found | Critical | Important | Minor |
|------|-------------|----------|-----------|-------|
| Duplication | N | ... | ... | ... |
| Ambiguity | N | ... | ... | ... |
| Underspecification | N | ... | ... | ... |
| Constitution | N | ... | ... | ... |
| Coverage | N | ... | ... | ... |
| Inconsistency | N | ... | ... | ... |
| **Total** | **N** | **N** | **N** | **N** |

Overall assessment: CONSISTENT / NEEDS ATTENTION / SIGNIFICANT ISSUES
```
