# Clarify Protocol

Systematic ambiguity detection and resolution for plan and spec artifacts. Used before fact-gathering to surface decisions that need user input.

## When to Run

- **Spec-kit mode:** Always run before fact-gathering (Step 1 in SKILL.md)
- **Legacy mode:** Run if `--clarify` flag is passed, otherwise skip
- **Skip conditions:** `--no-clarify` flag, artifact already has a Clarifications section from this session, no significant ambiguities detected

## Ambiguity Taxonomy (11 Categories)

Scan artifacts for these categories of ambiguity:

### 1. Undefined Scope Boundaries
Something is mentioned but not clearly in or out of scope.
> Example: "Support peer discovery" — LAN only? WAN? mDNS?

### 2. Underspecified Behavior
A feature is described but edge cases aren't covered.
> Example: "Reconnect on failure" — How many retries? Backoff? Give up?

### 3. Conflicting Requirements
Two parts of the spec/plan contradict each other.
> Example: Spec says "max 8 peers" but plan allocates for 16

### 4. Missing Error Handling
An operation can fail but no failure mode is specified.
> Example: "Send message to peer" — What if peer disconnected mid-send?

### 5. Implicit Platform Assumptions
Code assumes a platform behavior without stating it.
> Example: "Use UDP broadcast" — Not available on all Classic Mac network configs

### 6. Unspecified Data Formats
Data structures or protocols lack concrete format definitions.
> Example: "Exchange capabilities" — What's the wire format?

### 7. Missing Acceptance Criteria
A user story or task has no way to verify completion.
> Example: "Improve performance" — By how much? Measured how?

### 8. Ambiguous Priority
Multiple features with no clear ordering or must-have vs nice-to-have.
> Example: "Support MacTCP and Open Transport" — Both required for v1?

### 9. Undefined Integration Points
How components connect isn't specified.
> Example: "The UI calls the SDK" — Via callback? Polling? Events?

### 10. Missing Resource Constraints
No limits specified for bounded resources.
> Example: "Buffer incoming messages" — Max buffer size? What when full?

### 11. Temporal Ambiguity
Order of operations or timing requirements unclear.
> Example: "Initialize then connect" — Must init complete before connect starts?

## Question Batching

- Scan all artifacts, collect all ambiguities
- Group by category
- Select the **top 5 most impactful** ambiguities (those that would cause the most rework if guessed wrong)
- Present via AskUserQuestion with multi-choice options where possible

### Question Format

```
Header: "Clarify"
Question: "[Category]: [Specific question]?"
Options:
- "Option A" - Description of approach A
- "Option B" - Description of approach B
- "Option C" - Description of approach C
multiSelect: false
```

For questions with factual answers (not choices), use 2 options:
```
Options:
- "I'll specify" - Let me type the answer
- "Use your best judgment" - Apply the most reasonable default
```

## Recording Resolutions

### Spec-Kit Mode

Append resolutions to `spec.md` under a Clarifications section:

```markdown
## Clarifications

### Session YYYY-MM-DD

**Q1 [Undefined Scope]:** Does peer discovery include WAN or LAN only?
**A1:** LAN only. No WAN/NAT traversal in scope.

**Q2 [Missing Resource Constraints]:** Max number of simultaneous peers?
**A2:** 8 peers. Hard limit, fail connection attempts beyond this.
```

### Legacy Mode

Include resolutions in the review synthesis under "Clarifications Applied":

```markdown
### Clarifications Applied

The following ambiguities were resolved with the user before review:

1. **Scope:** Peer discovery is LAN-only (no WAN/NAT traversal)
2. **Limits:** Max 8 simultaneous peers, hard limit
```

## Handling "Use Your Best Judgment"

When the user defers a decision:

1. State the chosen approach clearly
2. Cite the reasoning (constitution principle, simplicity, safety)
3. Record both the question and the auto-resolved answer
4. Mark with `[auto-resolved]` so the user can revisit later

```markdown
**Q3 [Underspecified Behavior]:** Reconnection strategy on failure?
**A3:** [auto-resolved] 3 retries with 1s/2s/4s exponential backoff, then give up.
Reasoning: Simplicity principle — predictable behavior, no infinite loops.
```
