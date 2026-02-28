# Subagent Prompts for Plan Review

Spawn these subagents in parallel using the Task tool for comprehensive plan review. For Classic Mac API verification (subagents 2a-2d), use the `/mac-api` skill directly instead of spawning subagents.

**Important:** Only spawn domain subagents (1, 2a-2d, 3) when the artifact references those platforms. Always spawn subagents 4-6. In spec-kit mode, also spawn subagents 7-9.

## Contents
- Build Environment Note
- Subagent 1: MPW/Retro68 API Verification
- Subagent 2a: MacTCP Documentation Review
- Subagent 2b: Open Transport Documentation Review
- Subagent 2c: AppleTalk Documentation Review
- Subagent 2d: Inside Macintosh / ISR Safety Review
- Subagent 3: CSEND Lessons Audit
- Subagent 4: Phase Continuity Check
- Subagent 5: Logging & Debugging Review
- Subagent 6: Data-Oriented Design Review
- Subagent 7: Constitution Alignment Check (spec-kit only)
- Subagent 8: Cross-Artifact Consistency Analysis (spec-kit only)
- Subagent 9: Coverage Analysis (spec-kit only)

## Build Environment Note

The project uses Docker for Retro68 cross-compilation. MPW headers are available at:
- In Docker: `/Retro68/InterfacesAndLibraries/MPW_Interfaces/Interfaces&Libraries/Interfaces/CIncludes/`
- Locally (if set): `$RETRO68/InterfacesAndLibraries/...`
- In project: `resources/retro68/` (MPW_Interfaces.zip)

For header verification, extract and check locally or use Docker.

## Subagent 1: MPW/Retro68 API Verification

**Type:** `Explore`

**Prompt:**
```
Check resources/retro68/MPW_Interfaces.zip or use Docker container to verify MPW interface definitions.

If Docker is available, run:
  docker compose -f docker/docker-compose.yml run --rm peertalk-dev \
    find /Retro68 -name "*.h" | xargs grep -l "{function_name}"

For the plan at: $ARGUMENTS

Extract actual function signatures, struct definitions, and constants referenced in the plan.
Note any discrepancies between the plan's assumptions and the real APIs.

Return: List of API facts with file paths as citations.
```

## Subagent 2a: MacTCP Documentation Review

**Method:** Use `/mac-api` skill directly

**Queries to run:**
```
/mac-api [specific function or concept from plan]

Example:
/mac-api TCPPassiveOpen parameter block structure
/mac-api MacTCP ASR restrictions

**Verify against these sources:**
- ASR (Asynchronous Service Routine) rules and restrictions
- TCPCreate, TCPPassiveOpen, TCPActiveOpen, TCPSend, TCPRcv parameter blocks
- TCPNoCopyRcv and TCPBfrReturn buffer management
- UDP stream creation and datagram handling
- MacTCP error codes and their meanings
- Stream lifecycle and state transitions
- DNR (Domain Name Resolver) usage if referenced

**CRITICAL - Interrupt safety (search for "interrupt level", "ASR", "cannot"):**
- ASR is called at interrupt level - document what CANNOT be done
- No memory allocation/deallocation in ASR
- No synchronous MacTCP calls from ASR
- What CAN be done: async MacTCP calls, set flags, read pre-allocated buffers
- ioCompletion routine restrictions

Quote specific sections with page/chapter references.
Return: Verified MacTCP facts and any contradictions with the plan.
```

## Subagent 2b: Open Transport Documentation Review

**Method:** Use `/mac-api` skill directly

**Queries to run:**
```
/mac-api [specific function or concept from plan]

Example:
/mac-api OTListen tilisten pattern
/mac-api Open Transport notifier restrictions

**Verify against this source:**
- Notifier callback rules and restrictions
- OTOpenEndpoint, OTBind, OTConnect, OTListen, OTAccept patterns
- Endpoint states and state transitions
- T_DATA, T_LISTEN, T_DISCONNECT, T_ORDREL event handling
- OTSnd, OTRcv, OTSndUData, OTRcvUData usage
- tilisten pattern for accepting multiple connections
- OTAtomicSetBit, OTAtomicAdd and other atomic operations
- Flow control (T_GODATA) handling
- Error codes and kOTXXX constants

**CRITICAL - Interrupt safety (search for "Special Functions" appendix, ~page 793):**
- Three execution levels: hardware interrupt, deferred task, system task
- Functions callable at hardware interrupt time (Table C-1 or similar)
- Functions callable from deferred tasks
- Functions that allocate memory (avoid these in notifiers)
- Notifiers run at deferred task time - what restrictions apply
- OTScheduleDeferredTask for deferring work from HW interrupt
- OTGetTimeStamp/OTElapsedMilliseconds are interrupt-safe (unlike TickCount)
- kOTProviderWillClose is special - sync calls allowed

Quote specific sections with page/chapter references.
Return: Verified Open Transport facts and any contradictions with the plan.
```

## Subagent 2c: AppleTalk Documentation Review

**Method:** Use `/mac-api` skill directly

**Queries to run:**
```
/mac-api [specific function or concept from plan]

Example:
/mac-api ADSP userFlags clearing requirement
/mac-api NBP registration limits

**Topics to verify:**
- NBP (Name Binding Protocol) registration and lookup (Chapter 5)
- ADSP (AppleTalk Data Stream Protocol) connections (Chapter 8)
- ioCompletion and userRoutine callback rules
- CCB (Connection Control Block) structure and userFlags
- DSPParamBlock usage for async operations
- Zone and network number handling
- LocalTalk vs EtherTalk considerations
- .MPP and .DSP driver usage
- AppleTalk error codes

**CRITICAL - Interrupt safety (search Chapter 3 "Completion Routines"):**
- Completion routines run at interrupt level
- Cannot call any trap that moves memory
- Reference to Inside Macintosh III/IV/V for memory-moving traps list
- Must use register A0 to access parameter block (special technique needed)
- Parameter block integrity - cannot write to PB while in use
- ADSP userRoutine vs ioCompletion - both have interrupt restrictions
- userFlags must be cleared after reading in userRoutine

Quote specific sections with page/chapter references.
Return: Verified AppleTalk facts and any contradictions with the plan.
```

## Subagent 2d: Inside Macintosh / ISR Safety Review

**Method:** Use `/mac-api` skill directly

**Queries to run:**
```
/mac-api [specific function or concept from plan]

Example:
/mac-api is TickCount safe at interrupt time?
/mac-api Table B-3 interrupt-safe routines

**Topics to verify:**
- Memory Manager: NewPtr, NewHandle, DisposePtr, FreeMem, MaxBlock
- BlockMove vs BlockMoveData differences
- Resource Manager usage if referenced
- Gestalt selectors for capability detection
- 68k vs PPC differences
- System version requirements and compatibility

**CRITICAL - Interrupt safety (multiple volumes have relevant tables):**
- Volume VI Table B-3: Routines safe to call at interrupt time (lines 224396-224607)
- Volume III/IV/V: Traps that move memory (the INVERSE - what NOT to call)
- Verify TickCount() is NOT in Table B-3 (common mistake)
- Deferred Task Manager rules if referenced
- VBL task restrictions
- Time Manager task restrictions
- Which Memory Manager calls are interrupt-safe vs not

Cross-reference these tables when verifying any claim about interrupt safety.
Quote specific sections with volume/page/table references.
Return: Verified Inside Macintosh facts and any contradictions with the plan.
```

## Subagent 3: CSEND Lessons Audit

**Type:** `Explore`

**Prompt:**
```
Read plan/CSEND-LESSONS.md (if it exists) and examine ~/csend/ for reference.

For the plan at: $ARGUMENTS

Cross-reference relevant claims against POSIX/C standards.
Flag claims that seem outdated, incorrect, or unverifiable.
Note any logging, timing, or platform patterns that apply.

Return: List of relevant claims with status (verified/suspect/false) and reasoning.
```

## Subagent 4: Phase Continuity Check

**Type:** `Explore`

**Prompt:**
```
Read plan/PROJECT_GOALS.md and other PHASE-*.md documents in plan/.

For the plan at: $ARGUMENTS

Map this phase's outputs to PROJECT_GOALS requirements.
Identify what this phase must deliver for subsequent phases to proceed.
Check that dependencies are correctly declared.

Return: Dependency graph and gap analysis.
```

## Subagent 5: Logging & Debugging Review

**Type:** `Explore`

**Prompt:**
```
Review the plan at $ARGUMENTS for cross-platform logging and debugging provisions.

**Reference:** Read plan/PHASE-0-LOGGING.md for PT_Log API and patterns.

**Check for these logging requirements:**

1. **PT_Log Integration**
   - Does the plan use PT_Log for logging? (PT_ERR, PT_WARN, PT_INFO, PT_DEBUG macros)
   - Is a PT_Log context created/shared appropriately?
   - Are log files enabled for persistent debugging?

2. **Log Categories (should use appropriate categories from pt_log.h)**
   - PT_LOG_CAT_NETWORK for network operations (connect, send, receive, disconnect)
   - PT_LOG_CAT_PROTOCOL for protocol encoding/decoding
   - PT_LOG_CAT_MEMORY for allocation/deallocation tracking
   - PT_LOG_CAT_PLATFORM for platform-specific operations (MacTCP, OT, POSIX)
   - PT_LOG_CAT_PERF for performance-critical timing data

3. **Log Levels (appropriate severity for each message type)**
   - PT_LOG_ERR: Errors that affect functionality (failed operations, invalid state)
   - PT_LOG_WARN: Issues that may cause problems (timeouts, retries, resource low)
   - PT_LOG_INFO: Normal operational messages (connection established, data received)
   - PT_LOG_DEBUG: Verbose diagnostics (packet contents, state transitions, buffer fills)

4. **Critical Points That MUST Be Logged**
   - Initialization success/failure
   - Connection establishment and teardown
   - Error conditions with error codes
   - State machine transitions
   - Resource allocation failures
   - Protocol version mismatches
   - Timeout events

5. **ISR-Safety Rules (CRITICAL for Classic Mac)**
   - Does the plan avoid calling PT_Log from ASR/notifiers?
   - Does the plan use flag-based patterns for interrupt->main loop communication?
   - Is logging done from the main event loop, not interrupt context?

6. **Platform-Specific Debugging**
   - POSIX: stderr output, file logging, valgrind-friendly patterns
   - Classic Mac: File logging (since no console), callback for UI display
   - Are platform differences in debugging addressed?

7. **Runtime Control**
   - Can log level be changed at runtime for debugging?
   - Can categories be filtered to focus on specific subsystems?
   - Is auto-flush available for crash debugging?

Return: List of logging gaps and recommendations, categorized as:
- "CRITICAL" - Missing logging for error paths or ISR-safety violation
- "IMPORTANT" - Missing logging for key operations
- "NICE-TO-HAVE" - Additional debug logging that would help
```

## Subagent 6: Data-Oriented Design Review

**Type:** `general-purpose`

**Prompt:**
```
Evaluate the plan at $ARGUMENTS for CPU cache efficiency and performance.

Check for:

**Memory Layout**
- Struct field ordering (largest to smallest to minimise padding)
- Appropriate data type sizes (uint8_t vs int for small enums/flags)
- Pointer indirection that could cause cache misses during iteration
- Strings used where integer IDs would suffice

**Processing Patterns**
- Per-connection callbacks vs bulk processing opportunities
- Functions that take single items vs arrays/counts
- Loops with invariant expressions that should be hoisted
- Branching inside loops that could be eliminated by pre-sorting data

**Collection Design**
- List operations that shuffle data unnecessarily (consider swap-back removal)
- Whether ordering guarantees are actually needed
- Hot path data separated from cold path data

**Platform Considerations**
- Classic Mac has very limited CPU cache (68k has none; early PPC has small L1)
- Memory bandwidth is severely constrained on older hardware (2-10 MB/s)
- 68030 data cache is only 256 bytes
- These machines benefit even more from cache-friendly patterns than modern CPUs

Return: List of DOD concerns with specific struct/function references and recommendations.
Categorize as "Fix Now" (architectural) vs "Fix Later" (implementation).
```

## Subagent 7: Constitution Alignment Check (Spec-Kit Only)

**Type:** `general-purpose`

**When to spawn:** Only in spec-kit mode, when a constitution has been loaded.

**Prompt:**
```
Read the constitution at: [constitution path]
Read the spec at: [spec.md path]
Read the plan at: [plan.md path]

For each design decision or architectural choice in the spec and plan:
1. Identify which constitution principle(s) apply
2. Check if the choice aligns with the principle
3. If principles conflict, check if the resolution matches the constitution's priority order

Focus on:
- Architecture decisions (e.g., "use callbacks" vs "use polling")
- Scope decisions (what's in/out)
- Technology choices (libraries, protocols, patterns)
- Quality vs speed trade-offs
- Platform support decisions

Report format for each issue:
  CONSTITUTION ISSUE in [artifact:location]:
    Decision: "[what was decided]"
    Principle: "[relevant constitution principle]"
    Conflict: [how the decision contradicts the principle]
    Recommendation: [how to realign]

If no issues found, state: "All decisions align with constitution principles."

Return: List of constitution alignment issues with principle citations.
```

## Subagent 8: Cross-Artifact Consistency Analysis (Spec-Kit Only)

**Type:** `general-purpose`

**When to spawn:** Only in spec-kit mode, when multiple spec-kit artifacts exist.

**Prompt:**
```
Read all spec-kit artifacts:
- constitution.md (if exists)
- spec.md
- plan.md
- tasks.md (if exists)
- data-model.md (if exists)

Run these 6 consistency analysis passes:

Pass 1 - DUPLICATION: Find repeated/redundant content across artifacts.
Pass 2 - AMBIGUITY: Flag vague language ("appropriate", "suitable", "as needed", "etc.").
Pass 3 - UNDERSPECIFICATION: Find things mentioned but not fully defined.
Pass 4 - CONSTITUTION: Decisions that contradict constitution principles (if loaded).
Pass 5 - COVERAGE: User stories without tasks, tasks without stories.
Pass 6 - INCONSISTENCY: Contradictions between artifacts (constants, names, ordering).

For detailed pass descriptions, see .claude/skills/review/references/analyze-passes.md

Return: Summary table with issue counts per pass, plus details for each issue found.
Format:
| Pass | Issues Found | Critical | Important | Minor |
|------|-------------|----------|-----------|-------|
| ... | ... | ... | ... | ... |

Overall assessment: CONSISTENT / NEEDS ATTENTION / SIGNIFICANT ISSUES
```

## Subagent 9: Coverage Analysis (Spec-Kit Only)

**Type:** `general-purpose`

**When to spawn:** Only in spec-kit mode, when both spec.md and tasks.md exist.

**Prompt:**
```
Read spec.md and tasks.md.

Build a traceability matrix:

1. Extract all user stories (US1, US2, ...) from spec.md
2. Extract all acceptance criteria for each user story
3. Extract all tasks from tasks.md with their [USn] labels
4. Map tasks to user stories via [USn] labels

Produce this matrix:
| User Story | Acceptance Criteria | Tasks | Status |
|------------|-------------------|-------|--------|
| US1 | AC1.1 | T001, T002 | Covered |
| US1 | AC1.2 | — | GAP |
| ... | ... | ... | ... |

Also report:
- Coverage percentage: X/Y acceptance criteria have implementing tasks
- Orphan tasks: tasks with no [USn] label (may be valid infrastructure)
- Plan phases with no tasks generated

Return: The traceability matrix and coverage statistics.
```
