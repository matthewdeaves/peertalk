# Requirement Quality Checklist

Evaluate the quality of requirements in spec-kit artifacts. Used during review synthesis to provide a structured quality assessment.

## Checklist Dimensions

### 1. Completeness

- [ ] Every user story has acceptance criteria
- [ ] All acceptance criteria are testable (pass/fail, not subjective)
- [ ] Error cases are specified for every operation that can fail
- [ ] Resource limits are defined (max peers, buffer sizes, timeouts)
- [ ] Platform scope is explicit (which platforms for each feature)
- [ ] Out-of-scope items are listed

### 2. Testability

- [ ] Each acceptance criterion can be verified by a specific test
- [ ] Performance requirements include concrete metrics (not "fast")
- [ ] Edge cases are identified (empty input, max capacity, concurrent access)
- [ ] Test environment is specified (Docker, real hardware, both)
- [ ] Failure modes have observable indicators (error codes, log messages)

### 3. Consistency

- [ ] No contradictions between spec and plan
- [ ] No contradictions between plan and tasks
- [ ] Terminology is used consistently across all artifacts
- [ ] Constants and magic numbers match across all references
- [ ] File paths in tasks match the project file structure

### 4. Traceability

- [ ] Every task has a `[USn]` label linking to a user story
- [ ] Every user story has at least one implementing task
- [ ] Plan phases map to task phases
- [ ] Architecture decisions in plan are reflected in task approach
- [ ] Constitution principles are cited in key design decisions

### 5. Feasibility

- [ ] Tasks are small enough for one Claude Code session (< 500 lines of code)
- [ ] Dependencies between tasks are correctly ordered
- [ ] Platform-specific constraints are accounted for (ISR safety, memory limits)
- [ ] Build/test infrastructure exists or is created before code tasks
- [ ] No task requires knowledge that isn't in the spec, plan, or constitution

### 6. Clarity

- [ ] No use of "appropriate", "suitable", "as needed" without definition
- [ ] "Should" vs "must" distinction is clear and intentional
- [ ] Acronyms are defined on first use
- [ ] Code examples match the described API
- [ ] State machines have all transitions listed

## Scoring

Rate each dimension:

| Score | Meaning |
|-------|---------|
| Pass | All items checked, no significant issues |
| Partial | Most items checked, minor gaps identified |
| Fail | Multiple items unchecked, significant gaps |

## Output Format

```markdown
### Requirement Quality Checklist

| Dimension | Score | Issues |
|-----------|-------|--------|
| Completeness | Pass/Partial/Fail | Brief description of gaps |
| Testability | Pass/Partial/Fail | Brief description of gaps |
| Consistency | Pass/Partial/Fail | Brief description of gaps |
| Traceability | Pass/Partial/Fail | Brief description of gaps |
| Feasibility | Pass/Partial/Fail | Brief description of gaps |
| Clarity | Pass/Partial/Fail | Brief description of gaps |

**Overall:** READY FOR IMPLEMENTATION / NEEDS REFINEMENT / MAJOR GAPS
```

## When to Use

- After fact-gathering, during synthesis (spec-kit mode)
- As a final check before offering task generation
- When the user asks for a quality assessment of their spec
