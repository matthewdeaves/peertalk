---
name: hw-test
description: Generate and track hardware test plans for manual testing on real Classic Mac hardware. Use after implementing a session to create structured test documents for MacTCP, Open Transport, or AppleTalk code. Generates platform-specific checklists with acceptance criteria for verifying code on real SE/30, IIci, or PPC Macs.
argument-hint: <command> <session>
---

# Hardware Test Coordinator

Generate and manage test plans for real hardware testing on Classic Macintosh systems.

## Commands

| Command | Description |
|---------|-------------|
| `generate <session>` | Create test plan for a session |
| `checklist <platform>` | Show quick verification checklist |
| `status` | Show test coverage by platform |

## Usage

```
/hw-test generate 3.2
/hw-test generate 5.1 --platform mactcp
/hw-test checklist mactcp
/hw-test status
```

## Implementation

**You (Claude) implement this skill directly** by reading plan files and generating test documents. No external tools needed.

### For `generate <session>` command:

1. **Parse the session number** from arguments (e.g., "5.1")

2. **Detect platform** from phase number:
   | Phase | Platform |
   |-------|----------|
   | 5 | MacTCP (68k) |
   | 6 | Open Transport (PPC) |
   | 7 | AppleTalk |
   | 9 | Cross-platform |
   | Others | POSIX |

3. **Find the session** in plan files:
   - Use Glob to find `plan/PHASE-{phase}-*.md`
   - Read the session section (from `## Session X.Y:` to next `## Session`)

4. **Extract test cases** from the session:
   - Look for "Acceptance Criteria" or "Verify" sections
   - Look for checklist items (`- [ ]` or `- `)
   - Extract each criterion as a test case

5. **Generate test plan document**:
   - Create `tests/hw/test_plan_{session}_{platform}.md`
   - Use the template from [test_plan_template.md](assets/test_plan_template.md)
   - Fill in session-specific details: date, platform, system, hardware
   - Convert acceptance criteria to test cases

6. **Report completion** with next steps

### Test Plan Template

See [test_plan_template.md](assets/test_plan_template.md) for the complete template format.

Key sections:
- **Pre-Test Setup** - Build, transfer, note starting MaxBlock
- **Test Cases** - One per acceptance criterion with objective, steps, expected result
- **Post-Test Verification** - No crashes, MaxBlock unchanged, all tests executed
- **Summary** - Table of results and overall pass/fail

### Platform-Specific Details

**MacTCP (Phase 5):**
- System: System 6.0.8 - 7.5.5 + MacTCP 2.1
- Hardware: 68k Mac (SE/30, IIci, LC)
- Key checks: ASR fires without crash, streams don't leak

**Open Transport (Phase 6):**
- System: System 7.6.1+ or Mac OS 8/9
- Hardware: PPC Mac or 68040
- Key checks: Notifier fires without crash, tilisten works

**AppleTalk (Phase 7):**
- System: System 6+ with AppleTalk
- Hardware: Any Mac with LocalTalk or EtherTalk
- Key checks: NBP registration, ADSP connections, userFlags cleared

**Cross-Platform (Phase 9):**
- Systems: POSIX + Mac (any)
- Key checks: Discovery works both directions, messages transmit

### For `checklist <platform>` command:

Display the quick verification checklist from CLAUDE.md for the specified platform.

**MacTCP checklist:**
```
MacTCP Quick Checklist
======================
- [ ] PBOpen succeeds, driver refnum valid
- [ ] Streams create/release without leaks
- [ ] ASR fires and sets flags (doesn't crash)
- [ ] Main loop processes flags correctly
- [ ] No crashes after 10+ operations
- [ ] MaxBlock same before/after (no leaks)
```

**Open Transport checklist:**
```
Open Transport Quick Checklist
==============================
- [ ] Gestalt detects OT, InitOpenTransport succeeds
- [ ] Endpoints open/close without leaks
- [ ] Notifier fires and sets flags (doesn't crash)
- [ ] tilisten accepts multiple connections
- [ ] No crashes after 10+ operations
- [ ] MaxBlock same before/after (no leaks)
```

**AppleTalk checklist:**
```
AppleTalk Quick Checklist
=========================
- [ ] .MPP and .DSP drivers open successfully
- [ ] NBP registration succeeds
- [ ] NBP lookup finds peers
- [ ] ADSP connection listener accepts connections
- [ ] ADSP connect (request mode) succeeds
- [ ] Data flows bidirectionally with EOM framing
- [ ] Completion routines don't crash (ISR-safe)
- [ ] Works on LocalTalk (not just EtherTalk)
- [ ] No crashes after 10+ operations
- [ ] MaxBlock same before/after (no leaks)
```

**Cross-Platform checklist:**
```
Cross-Platform Quick Checklist
==============================
- [ ] POSIX peer discovers Mac peer
- [ ] Mac peer discovers POSIX peer
- [ ] Connections work in both directions
- [ ] Messages transmit correctly
- [ ] Protocol packets valid (check with Wireshark)
```

### For `status` command:

1. Use Glob to find existing test plans: `tests/hw/test_plan_*.md`
2. Parse each file to extract:
   - Session number
   - Platform
   - Pass/Fail status (if completed)
3. Display a summary table

**Output format:**
```
Hardware Test Status
====================

Session | Platform | Plan Exists | Result
--------|----------|-------------|--------
5.1     | mactcp   | Yes         | PASS
5.2     | mactcp   | Yes         | Not run
5.3     | mactcp   | No          | -
6.1     | ot       | No          | -
...

Summary:
- Plans generated: 4
- Tests passed: 2
- Tests failed: 0
- Not yet run: 2
```

## Integration with Other Skills

Hardware testing is part of the complete session workflow:

```
1. Implement:      /implement X Y
2. Verify (POSIX): /build test
3. Validate (Mac): /check-isr
4. Generate plan:  /hw-test generate X.Y
5. Build for Mac:  ./scripts/build-mac-tests.sh mactcp
                   (or /build mac-tests)
6. Start partner:  /test-partner start echo
7. Deploy & Test:  Upload build/mac/test_*.bin to Mac via MCP
8. Fetch logs:     /fetch-logs <machine>
9. Mark complete:  /session complete X.Y
```

## Build Scripts

| Script | Purpose |
|--------|---------|
| `./scripts/build-mac-tests.sh mactcp` | Build all test apps for MacTCP |
| `./scripts/build-mac-tests.sh mactcp perf` | Build only perf tests |
| `./scripts/build-launcher.sh mactcp` | Build LaunchAPPLServer |

## Notes

- Test plans are saved to `tests/hw/` directory
- Plans are markdown files that can be printed or viewed on any system
- Fill in actual results manually during hardware testing
- MaxBlock check is critical for detecting memory leaks
