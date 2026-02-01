# PeerTalk Development Workflow

> **How to implement PeerTalk using Claude Code sessions**

## Overview

PeerTalk is implemented in 9 phases, each split into multiple sessions. This document explains how to work through the implementation efficiently.

## Session Management

### When to Start Fresh (`/clear`)

Run `/clear` to reset context:
- After completing a session
- When context feels bloated (responses slow)
- Before starting a new phase
- After debugging a complex issue

### Session Scope

Each session should:
- Touch 3-7 files maximum
- Complete in one sitting
- Have clear verification criteria
- End with working, testable code

## How to Run Sessions

### Starting an Implementation Session

```
Read PHASE-{N}-{NAME}.md and implement Session {N.M}.
Follow the tasks in order. The session scope table shows
which files to create/modify and how to verify completion.
```

**Example:**
```
Read PHASE-3-POSIX.md and implement Session 3.1 (UDP Discovery).
Create the files listed and ensure the acceptance criteria pass.
```

### Starting a Research Session

```
Read PHASE-{N}-{NAME}.md and research Session {N.M}.
Investigate how the existing codebase handles [specific area].
Do not write code yet - just gather information.
```

### After Each Session

1. **Verify** - Run the tests or checks specified
2. **Update status** - Change session status to `[DONE]` in the phase file
3. **Commit** - If tests pass, commit with descriptive message
4. **Clear** - Run `/clear` to reset context
5. **Continue** - Start next session or take a break

## Phase Dependencies

```
Phase 1: Foundation (types, platform detection)
    |
    v
Phase 2: Protocol (wire format, encode/decode)
    |
    v
Phase 3: Queues (message queuing, ISR-safe operations)
    |
    +---> Phase 4: POSIX (reference implementation)
    |         |
    |         +--------+
    |                  |
    +---> Phase 5: MacTCP (68k Classic Mac)
    |         |        |
    |         +--------+
    |                  |
    +---> Phase 6: Open Transport (PPC Classic Mac)
    |                  |
    +---> Phase 7: AppleTalk (all Macs)
                       |
                       v
                  Phase 8: Example Chat Application
                       |
                       v
                  Phase 9: Integration & Testing
```

**Notes:**
- Phases 4-7 can be worked on in parallel after Phase 3
- Phase 8 requires all networking phases (4-7) to be complete
- Phase 9 is the final validation across all platforms

## Session Prompts by Phase

### Phase 1: Foundation

```
Read PHASE-1-FOUNDATION.md and implement Session 1.1.
Create the core type definitions and platform detection macros.
Verify with: Code compiles on target platforms.
```

```
Read PHASE-1-FOUNDATION.md and implement Session 1.2.
Create the logging system with category filtering.
Verify with: Log output appears correctly formatted.
```

### Phase 2: Protocol

```
Read PHASE-2-PROTOCOL.md and implement Session 2.1.
Create the discovery packet format and encode/decode functions.
Verify with: Round-trip encode/decode produces identical packets.
```

```
Read PHASE-2-PROTOCOL.md and implement Session 2.2.
Create the message frame format with CRC validation.
Verify with: CRC detects corrupted messages.
```

### Phase 3: Queues

```
Read PHASE-3-QUEUES.md and implement Session 3.1.
Create the core queue data structure with ISR-safe operations.
Verify with: test_queue passes all operations.
```

### Phase 4: POSIX

```
Read PHASE-4-POSIX.md and implement Session 4.1.
Create UDP discovery with broadcast send/receive.
Verify with: Two processes discover each other.
```

```
Read PHASE-4-POSIX.md and implement Session 4.4.
Create the main poll loop integrating all components.
Verify with: test_integration_posix passes.
```

### Phase 5: MacTCP

```
Read PHASE-5-MACTCP.md and implement Session 5.1.
Create MacTCP initialization and driver open.
Verify with: PBOpen succeeds, resolver loads.
```

```
Read PHASE-5-MACTCP.md and implement Session 5.3.
Create TCP stream management with ASR.
CRITICAL: ASR must only set flags, no allocations.
Verify with: Stream creates and ASR fires on events.
```

### Phase 6: Open Transport

```
Read PHASE-6-OPENTRANSPORT.md and implement Session 6.1.
Create OT initialization with Gestalt check.
Verify with: InitOpenTransport succeeds, local IP retrieved.
```

```
Read PHASE-6-OPENTRANSPORT.md and implement Session 6.4.
Implement the tilisten pattern for accepting connections.
Verify with: Multiple connections accepted concurrently.
```

### Phase 7: Example Chat Application

```
Read PHASE-7-UI.md and implement Session 7.1.
Create ncurses chat application for POSIX.
Verify with: Application shows peers and exchanges messages.
```

```
Read PHASE-7-UI.md and implement Session 7.2.
Create console chat application for Classic Mac.
Verify with: Application runs on real Mac hardware.
```

### Phase 8: Integration & Testing

```
Read PHASE-8-INTEGRATION.md and implement Session 8.1.
Run cross-platform tests.
Verify with: POSIX peer communicates with real Mac peer.
```

## Testing Guidelines

### Coverage Requirements

- **Minimum target:** 10% overall coverage
- **POSIX layer:** Full unit test coverage (automated)
- **Mac layers:** Mock-based unit tests + emulator integration
- **Measurement:** Use gcov/lcov for POSIX

### Running Tests

**POSIX:**
```bash
make test
# Or individually:
./tests/test_discovery_posix
./tests/test_integration_posix
```

**MacTCP/OT (in emulator):**
```bash
# Build with Retro68
make -f Makefile.mac

# Run in Mini vMac or Basilisk II
# Manual verification against acceptance criteria
```

### Test File Naming

```
tests/test_{feature}_{platform}.c   # Platform-specific
tests/test_{feature}.c              # Cross-platform
tests/test_mock_{layer}.c           # Mock-based unit tests
```

## Debugging Tips

### POSIX
- Use valgrind for memory leaks
- Use gdb/lldb for crashes
- Add PT_LOG_DEBUG calls liberally

### Classic Mac
- Use MacsBug or Macintosh Debugger
- Check MaxBlock before/after for leaks
- Verify ASR isn't doing forbidden operations
- Test with low memory conditions

### Common Issues

| Symptom | Likely Cause | Fix |
|---------|--------------|-----|
| Crash in ASR | Memory allocation | Move to main loop |
| Connection fails | Wrong byte order | Use htonl/ntohl |
| Discovery not working | Broadcast blocked | Check firewall/router |
| Queue overflow | Too many messages | Enable coalescing |

## State Tracking

### Phase Status Values

| State | Meaning |
|-------|---------|
| `[OPEN]` | Not started |
| `[IN PROGRESS]` | Currently being worked on |
| `[READY TO TEST]` | Implementation complete, needs verification |
| `[DONE]` | Verified and complete |

### Updating Status

When completing a session, edit the phase file:

```markdown
| 3.1 | UDP Discovery | [DONE] | ... |
```

When starting a session:

```markdown
| 3.2 | TCP Connections | [IN PROGRESS] | ... |
```

## Commit Messages

Use conventional commits:

```
feat(posix): implement UDP discovery broadcast

- Add net_posix.h with platform-specific context
- Implement discovery_start/stop/poll
- Add test_discovery_posix.c

Refs: PHASE-3-POSIX.md Session 3.1
```

```
fix(mactcp): correct ASR flag handling

ASR was calling TCPRcv directly - moved to main poll loop.
Only set flags in ASR now.

Refs: PHASE-4-MACTCP.md, CLAUDE.md ASR rules
```

## Quick Reference

| Task | Command/Action |
|------|----------------|
| Start session | Read PHASE-N file, implement session |
| Verify | Run tests or manual check per criteria |
| Complete | Update status, commit, `/clear` |
| Debug | Check CLAUDE.md rules, add logging |
| Test coverage | `make coverage` (POSIX) |
