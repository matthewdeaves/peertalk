---
name: session
description: Navigate and manage PeerTalk phase sessions. Use when checking project progress (status), finding the next session to implement (next), identifying blocking dependencies (blocked), marking sessions complete, or viewing session details. Proactively use this after completing a session to find the next task.
argument-hint: <command> [args]
---

# Session Navigator

Navigate the multi-phase PeerTalk implementation workflow. Track progress, find available work, and manage session completion.

## Commands

| Command | Description |
|---------|-------------|
| `status` | Show overall progress dashboard with all phases |
| `next` | Find the next available session to work on |
| `blocked` | Show sessions that are blocked and why |
| `complete <session>` | Mark a session as done and update phase file |
| `show <session>` | Show details for a specific session |

## Usage

```
/session              # Default: runs 'next'
/session status
/session next
/session blocked
/session complete 1.4
/session show 2.1
```

## Implementation

**You (Claude) implement this skill directly** by reading and parsing the plan files. No external tools needed.

### CRITICAL: Plan Files Are Too Large

**NEVER use Read on plan files** - they are 500+ lines each (45K+ tokens total) and will exceed the 25K token limit.

**ALWAYS use Grep** to extract information from plan files:
- Use `output_mode: "content"` with `-n: true` to get line numbers
- Use `-C: 3` or `-C: 5` for context lines if needed
- Make multiple targeted Grep calls instead of trying to Read entire files

**Example - CORRECT approach:**
```
Grep(pattern: "^# PHASE", path: "plan/PHASE-1-FOUNDATION.md", output_mode: "content", -n: true)
Grep(pattern: "\\*\\*Status:\\*\\*", path: "plan/PHASE-1-FOUNDATION.md", output_mode: "content", -n: true)
Grep(pattern: "^\\| Session \\|", path: "plan/PHASE-1-FOUNDATION.md", output_mode: "content", -n: true, -C: 50)
```

**Example - WRONG approach (will fail):**
```
Read("plan/PHASE-1-FOUNDATION.md")  ❌ ERROR: File too large!
```

### Step 1: Parse Arguments

- If `$ARGUMENTS` is empty or whitespace → use `next`
- Otherwise parse the command from `$ARGUMENTS`

### Step 2: Execute Command

#### For `status` command:

1. Use Glob to find all phase files: `plan/PHASE-*.md`
2. For each phase file, use Grep to extract:
   - Phase number and title from filename and first `# PHASE` header
   - Status from `**Status:** [OPEN|IN PROGRESS|DONE]`
   - Dependencies from `**Depends on:**` line
3. Find the Session Scope Table (starts with `| Session | Focus | Status |`)
4. Count sessions by status: `[OPEN]`, `[IN PROGRESS]`, `[DONE]`
5. Display a summary table showing all phases and their progress

**Output format:**
```
PeerTalk Progress
=================

Overall: X/Y sessions complete (Z%)

Phase | Title          | Status       | Sessions        | Depends
------|----------------|--------------|-----------------|--------
0     | Logging        | [DONE]       | 4/4 done        | -
1     | Foundation     | [IN PROGRESS]| 2/6 done        | Phase 0
2     | Protocol       | [OPEN]       | 0/3 done        | Phase 1
...
```

#### For `next` command:

1. Run the status logic to get all phases and sessions
2. For each phase in order (0, 1, 2, 3, 3.5, 4, 5, 6, 7, 8, 9, 10):
   - Check if dependencies are satisfied (dependent phases are `[DONE]`)
   - Find the first session that is NOT `[DONE]`
   - That's the next available session
3. Display the recommended session with `/implement` command

**Output format:**
```
Next Available Session
======================

→ Session 1.2: Portable Primitives
  Phase 1: Foundation
  Status: [OPEN]

Start with:
  /implement 1 1.2
```

#### For `blocked` command:

1. For each phase, check if dependencies are NOT satisfied
2. List sessions that can't be started and what's blocking them

**Output format:**
```
Blocked Sessions
================

Session 5.1 (MacTCP Driver)
  Blocked by: Phase 2 [OPEN], Phase 3 [OPEN]

Session 6.1 (OT Init)
  Blocked by: Phase 2 [OPEN]
```

#### For `complete <session>` command:

1. Parse the session number (e.g., "1.4", "5.1")
2. Find the phase file containing this session
3. **Use Grep** to find the session row (NOT Read - file too large!)
   - Example: `Grep(pattern: "^\\| 1\\.4 \\|", path: "plan/PHASE-1-FOUNDATION.md", output_mode: "content", -n: true)`
4. Find the exact line with the session in the Session Scope Table
5. Use Edit to change `[OPEN]` or `[IN PROGRESS]` to `[DONE]`
   - Pattern: `| 1.4 | ... | [OPEN] |` → `| 1.4 | ... | [DONE] |`
   - Handle bold session numbers: `| **5.9** | ... | [OPEN] |`
6. Check if all sessions in the phase are now `[DONE]` (use Grep to count remaining [OPEN] sessions)
   - If so, also update `**Status:** [OPEN]` to `**Status:** [DONE]` (use Edit)
7. Run `next` to show what's available

**Output format:**
```
Session 1.4 marked [DONE]

Phase 1 progress: 5/6 sessions complete

Next available:
  Session 1.5: Integration Test
  /implement 1 1.5
```

#### For `show <session>` command:

1. Find the phase file containing the session
2. **Use Grep** to find the session section (NOT Read - file too large!)
   - Example: `Grep(pattern: "^## Session 1\\.2", path: "plan/PHASE-1-FOUNDATION.md", output_mode: "content", -n: true, -C: 50)`
   - Get ~50 lines of context to capture the session details
3. Display the session details including tasks and acceptance criteria

### Parsing Plan Files

**For complete parsing details, see [parsing-guide.md](references/parsing-guide.md)**

**CRITICAL:** Plan files are too large to Read (500+ lines each). Always use Grep with targeted patterns.

Quick reference:
- **Session Scope Table:** Use `Grep(pattern: "^\\| Session \\|", -C: 50)` to extract table
- **Phase Status:** Use `Grep(pattern: "\\*\\*Status:\\*\\*")` to get phase status
- **Dependencies:** Use `Grep(pattern: "\\*\\*Depends on:\\*\\*")` to get dependencies
- **Parse table rows:** Split by `|`, strip `**` bold markers, extract status from `[OPEN|IN PROGRESS|DONE]`

Session availability rules:
1. All dependent phases have status `[DONE]`
2. All earlier sessions in the same phase are `[DONE]`

## Integration with Other Skills

After `/session next`:
```
→ Session 5.1 available
  /implement 5 5.1
```

After `/session complete X.Y`:
```
Session X.Y marked [DONE]
Next: /implement ... or /session for options
```

## Notes

- Sessions must be completed in order within each phase
- Phase dependencies must be satisfied before starting a phase
- The `complete` command edits the plan file directly
