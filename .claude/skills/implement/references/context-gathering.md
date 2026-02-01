# Context Gathering - Parallel Subagent Execution

When implementing a session, spawn these 4 subagents in parallel using the Task tool to gather implementation context.

## Contents
- Subagent 1: Session Extraction (Explore)
- Subagent 2: Platform Rules (Explore)
- Subagent 3: Dependency Check (Explore)
- Subagent 4: Existing Code Inventory (Explore)
- Build Environment Note

## Subagent 1: Session Extraction

**Type:** `Explore`

**Prompt:**
```
Read plan/PHASE-{phase}-*.md and extract the COMPLETE session section for Session {session}.

Include:
- Session objective
- All tasks (Task X.Y.Z sections)
- Code examples and struct definitions
- Acceptance criteria / verification checklist
- Any "IMPORTANT" or "CRITICAL" notes in the session

Also extract from the phase file header:
- Phase dependencies (what must be complete first)
- Session status (should be [OPEN] or [IN PROGRESS])

Return: The complete session specification with all code blocks preserved.
```

## Subagent 2: Platform Rules

**Type:** `Explore`

**Prompt:**
```
Read CLAUDE.md and extract rules relevant to implementing Phase {phase}.

Focus on:
- ISR/ASR safety rules (if phase involves MacTCP, OT, or AppleTalk)
- Callback patterns (TCP ASR, OT notifier, ADSP completion signatures)
- Memory allocation restrictions
- Struct field ordering requirements
- Code quality gates (function length, complexity limits)
- Magic numbers and protocol constants

For Phase {phase}, identify which platform(s) are involved:
- Phase 0-3: Core (POSIX-testable)
- Phase 4: POSIX
- Phase 5: MacTCP (68k)
- Phase 6: Open Transport (PPC)
- Phase 7: AppleTalk (all Macs)
- Phase 8: UI (all platforms)
- Phase 9: Integration (all platforms)

Return: Relevant CLAUDE.md rules as a condensed reference.
```

## Subagent 3: Dependency Check

**Type:** `Explore`

**Prompt:**
```
Check if Phase {phase} dependencies are satisfied.

Read plan/PHASE-*.md files for phases that this phase depends on.
Check their status headers for [DONE] markers.

Also check if previous sessions in this phase are complete:
- Session {session} requires all earlier sessions in Phase {phase} to be [DONE]

Return: Dependency status (all satisfied / blocked on X).
```

## Subagent 4: Existing Code Inventory

**Type:** `Explore`

**Prompt:**
```
Survey the current codebase to understand what already exists.

Check:
- src/core/ - What core files exist?
- src/posix/, src/mactcp/, src/opentransport/, src/appletalk/ - Platform implementations
- include/ - Public headers (peertalk.h, pt_log.h)
- tests/ - Existing test files

For the session's target files:
- Do they already exist? (modification vs creation)
- What functions/structs are already defined?

Return: File inventory with existing vs needed files for this session.
```

## Build Environment Note

The project uses Docker for Retro68 cross-compilation. MPW headers are available at:
- In Docker: `/Retro68/InterfacesAndLibraries/MPW_Interfaces/Interfaces&Libraries/Interfaces/CIncludes/`

To check headers:
```bash
docker compose -f docker/docker-compose.yml run --rm peertalk-dev \
  grep -n "function_name" /Retro68/.../CIncludes/MacTCP.h
```

Key headers:
- MacTCP.h - TCPCreate, ASR signatures, parameter blocks
- OpenTransport.h - OTOpenEndpoint, notifier signatures
- OpenTptInternet.h - TCP/UDP configuration
- ADSP.h - ADSP completion routines, CCB structure
- AppleTalk.h - NBP registration/lookup
