---
name: implement
description: Implement a PeerTalk phase session. Reads the session from the phase plan, gathers platform rules, implements each task in order, and verifies deliverables against acceptance criteria. Use when starting work on a specific session (e.g., /implement 1 1.2) or without arguments to auto-detect the next available session.
argument-hint: <phase> <session> (e.g., "1 1.2" or "3 3.1")
---

# Implement Session: $ARGUMENTS

Implement a PeerTalk session from the phase plans. This skill handles context gathering, task execution, and verification.

## Step 0: Parse Arguments

If `$ARGUMENTS` is empty or just whitespace:
1. **Auto-find next session** by reading plan files directly:
   - Use Glob to find `plan/PHASE-*.md`
   - For each phase, check dependencies and find first non-DONE session
   - (Same logic as `/session next`)
2. Use AskUserQuestion to confirm:
   ```
   Header: "Auto-detected"
   Question: "Next available session is {X.Y}: {title}. Implement this?"
   Options:
   - "Yes, implement {X.Y} (Recommended)" - Start implementation
   - "Choose different session" - Show all available sessions
   - "Show session details first" - Read the full session spec
   - "Cancel" - Don't implement anything
   ```

If `$ARGUMENTS` provided, parse to extract:
- **Phase number** (e.g., "1", "3", "5")
- **Session number** (e.g., "1.2", "3.1", "5.3")

Map to plan file: `plan/PHASE-{phase}-*.md`

If arguments are malformed, use AskUserQuestion to clarify.

## Step 1: Context Gathering (Parallel)

Spawn 4 subagents in parallel using the Task tool to gather implementation context.

**For complete subagent prompts and details, see [context-gathering.md](references/context-gathering.md)**

Quick summary:
1. **Session Extraction** (Explore) - Extract complete session spec from plan file
2. **Platform Rules** (Explore) - Extract relevant CLAUDE.md rules for this phase
3. **Dependency Check** (Explore) - Verify phase and session dependencies are met
4. **Existing Code Inventory** (Explore) - Survey what files/functions already exist

## Step 2: Pre-Implementation Check

After gathering context, verify:

1. **Dependencies satisfied?** If not, report what's blocking and use AskUserQuestion:
   ```
   Header: "Blocked"
   Question: "Session {X.Y} is blocked by {dependency}. What would you like to do?"
   Options:
   - "Implement dependency first" - Switch to the blocking session
   - "Skip dependency check" - Proceed anyway (may cause issues)
   - "Show dependency status" - List all phase/session statuses
   ```

2. **Session not already done?** If status is [DONE], use AskUserQuestion to confirm re-implementation.

3. **Context fits?** If the session has >10 tasks or >50KB of specification, use AskUserQuestion to decide whether to implement all at once or task-by-task.

4. **Ready to start?** Present session summary and use AskUserQuestion:
   ```
   Header: "Ready"
   Question: "Implement Session {X.Y}: {session title}?"
   Options:
   - "Start implementation" (Recommended) - Begin with Task {X.Y.1}
   - "Show task list first" - Display all tasks before starting
   - "Check a specific task" - Jump to a particular task number
   ```

## Step 3: Implementation

For each task in the session, in order:

### 3.1 Announce Task
State which task you're implementing: "Implementing Task {X.Y.Z}: {task title}"

### 3.2 Check API Correctness (if Classic Mac)
For MacTCP/OT/AppleTalk code, verify function signatures against Retro68 headers.

**Build Environment:** The project uses Docker for Retro68 cross-compilation:
```bash
# Check header in Docker container
docker compose -f docker/docker-compose.yml run --rm peertalk-dev \
  grep -n "function_name" /Retro68/.../CIncludes/MacTCP.h
```

Key headers (in Docker at `/Retro68/.../CIncludes/`):
- MacTCP.h, OpenTransport.h, OpenTptInternet.h, ADSP.h, AppleTalk.h

### 3.3 Documentation Lookup (On-Demand)

When implementing Classic Mac code and you encounter uncertainty about:
- Function signatures or parameter block layouts
- Error codes and their meanings
- Callback restrictions or edge cases
- Interrupt-safety of a specific call

**Use the /mac-api skill for documentation lookups:**

```
/mac-api Can I call OTAllocMem from an Open Transport notifier callback?
```

Or invoke it directly in your response when you need to verify Classic Mac API details.

**When to use:** Use when plan doesn't specify exact value/signature or you're unsure about ISR-safety.

**Don't use for:** Things already in session spec or CLAUDE.md.

### 3.4 Write Code

Implement the task following:
- Code examples from the session specification
- CLAUDE.md patterns and restrictions
- Existing code style in the codebase

**Code Quality Gates (from CLAUDE.md):**
- Max function length: 100 lines (prefer 50)
- Max file size: 500 lines
- Cyclomatic complexity: 15 max per function
- Compiler warnings: Treat as errors

**ISR-Safety (if applicable):**
- NO memory allocation in ASR/notifier/completion
- NO synchronous network calls
- NO TickCount() - use pre-set timestamps or OT timing functions
- Set flags only; process in main loop
- Use pt_memcpy_isr() not pt_memcpy()

**For detailed implementation patterns and guidance, see [patterns.md](references/patterns.md)**

### 3.5 Verify Task

After implementing each task:
- Does it compile? (for POSIX: `make`)
- Does it match the specification?
- Are all CRITICAL/IMPORTANT notes addressed?

**If in "task by task" mode**, use AskUserQuestion after each task to confirm continuation.

## Step 4: Session Verification

After all tasks complete, run the session's verification checklist.

**For complete verification steps, see [verification.md](references/verification.md)**

Quick checklist:
1. Build verification (POSIX: `make clean && make test`)
2. Run session-specific tests
3. Check acceptance criteria
4. Memory/leak check (valgrind for POSIX)
5. Quality gates (function length, file size, complexity, coverage)
6. ISR-safety check (for Mac code: `/check-isr`)
7. Code style verification

**If verification passes:** Proceed to Step 5
**If verification fails:** Report issues, do NOT mark session as [DONE], use AskUserQuestion to decide how to proceed

## Step 5: Status Update

If all verification passes:

1. **Update session status** in the phase file:
   - Change `[OPEN]` or `[IN PROGRESS]` to `[DONE]`
   - Use Edit tool to modify the Session Scope Table row

2. **Report completion:**
   ```
   Session {X.Y} complete.

   Files created/modified:
   - {list of files}

   Tests passing:
   - {test results}

   Ready for: Session {X.Y+1} or Phase {X+1} (if last session)
   ```

## Step 6: Next Steps

After completing a session successfully, present the recommended workflow:

```
Session {X.Y} complete!

Files created/modified:
  - {list of files}

Recommended next steps:
  1. /build test          - Run automated tests (POSIX)
  2. /check-isr           - Validate ISR safety (if Mac code)
  3. /hw-test generate {X.Y}  - Create hardware test plan
  4. /build package       - Build Mac binaries for transfer
  5. /session complete {X.Y}  - Mark session done
  6. /clear && /session   - Find next session
```

Then use AskUserQuestion:

```
Header: "Next"
Question: "What would you like to do next?"
Options:
- "Verify with /build test" (Recommended) - Run automated tests
- "Check ISR safety" - Run /check-isr on Mac code
- "Generate test plan" - Run /hw-test generate {X.Y}
- "Continue to next session" - /clear && /session
- "Done for now" - End implementation session
```
