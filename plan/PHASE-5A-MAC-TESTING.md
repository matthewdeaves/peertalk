# PHASE 5A: MacTCP Hardware Testing Strategy

> **Status:** OPEN
> **Depends on:** Phase 0 (PT_Log for structured test output), Phase 5 (MacTCP code to test)
> **Companion to:** Phase 5 MacTCP Networking
> **Produces:** Automated testing workflow for MacTCP on real Classic Mac hardware
> **Reusable for:** Phase 6 (Open Transport), Phase 7 (AppleTalk)
> **Review Applied:** 2026-02-07 (PT_LOG_CAT_TEST fix, CSEND lessons, argument passing, ISR safety)

## Summary

Create automated testing workflow for MacTCP implementation using:
1. **Combined test runner** - Single `test_mactcp.c` with selectable tests
2. **POSIX test partner** - Cross-platform validation from Session 5.3+
3. **`/mac-test` skill** - One-command build→deploy→execute→parse workflow

---

## Architecture

### Test Runner: `tests/mac/test_mactcp.c`

Single executable that runs all MacTCP tests with command-line selection:

```
test_mactcp              # Run all tests
test_mactcp 5.1          # Run Session 5.1 tests only
test_mactcp driver       # Run driver tests by name
test_mactcp --list       # Show available tests
```

**Structure:**
```c
/* Test registration */
typedef struct {
    const char *name;        /* "driver_open" */
    const char *session;     /* "5.1" */
    TestFunc    func;        /* test_driver_open */
    bool        needs_posix; /* Requires POSIX peer? */
} MacTest;

static MacTest tests[] = {
    {"driver_open",    "5.1", test_driver_open,    false},
    {"udp_create",     "5.2", test_udp_create,     false},
    {"udp_discovery",  "5.3", test_udp_discovery,  true},  /* Needs POSIX */
    {"tcp_create",     "5.4", test_tcp_create,     false},
    {"tcp_listen",     "5.5", test_tcp_listen,     true},  /* Needs POSIX */
    {"tcp_connect",    "5.6", test_tcp_connect,    true},  /* Needs POSIX */
    {"tcp_io",         "5.7", test_tcp_io,         true},  /* Needs POSIX */
    {"integration",    "5.8", test_integration,    true},  /* Needs POSIX */
    {NULL, NULL, NULL, false}
};
```

**Output Format (PT_Log markers for parsing):**
```
[TEST] ========================================
[TEST] PeerTalk MacTCP Test Suite
[TEST] Session: 5.3
[TEST] ========================================
[TEST] >>> driver_open
[PASS] driver_open
[TEST] >>> udp_create
[PASS] udp_create
[TEST] >>> udp_discovery
[PLATFORM] Broadcasting discovery...
[PLATFORM] Received discovery from 192.168.1.100
[PASS] udp_discovery
[TEST] ========================================
[TEST] SUMMARY: 3 passed, 0 failed, 0 skipped
[TEST] RESULT: PASS
[TEST] ========================================
```

### Test Selection on Classic Mac

Classic Mac applications don't receive command-line arguments like POSIX. Test selection uses a **settings resource**:

```c
/* In test_mactcp.c - read test settings from resource */
typedef struct {
    char session[8];    /* e.g., "5.3" or "all" */
    char test_name[32]; /* e.g., "discovery" or empty */
} TestSettings;

void get_test_settings(char *session, char *test_name) {
    Handle h = GetResource('PTst', 128);  /* Test settings resource */
    if (h && *h) {
        TestSettings *settings = (TestSettings*)*h;
        strncpy(session, settings->session, 7);
        strncpy(test_name, settings->test_name, 31);
        ReleaseResource(h);
    } else {
        strcpy(session, "all");  /* Default: run all tests */
        test_name[0] = '\0';
    }
}
```

The `/mac-test` skill modifies the resource before deployment using Rez or a build-time script.

**Alternative approach:** Build separate binaries per session (`test_mactcp_5_1.bin`, `test_mactcp_5_3.bin`). Simpler but requires more disk space on Mac.

### Test Framework: `tests/mac/mac_test.h`

Mac-compatible test macros using PT_Log:

```c
#ifndef MAC_TEST_H
#define MAC_TEST_H

#include "pt_log.h"

/*
 * Use PT_LOG_CAT_APP1 for test output.
 * PT_LOG_CAT_TEST does not exist in pt_log.h - APP1-APP5 are for applications.
 */
#define PT_LOG_CAT_TEST PT_LOG_CAT_APP1

/*
 * ISR SAFETY WARNING:
 * These macros use PT_Log which is NOT interrupt-safe.
 * NEVER call MAC_TEST_*, MAC_PASS, MAC_FAIL, etc. from:
 *   - MacTCP ASR callbacks
 *   - Open Transport notifiers
 *   - Any interrupt context
 * Tests run from main loop only.
 */

/* Test output via PT_Log - parseable format */
#define MAC_TEST_START(name) \
    PT_LOG_INFO(log, PT_LOG_CAT_TEST, ">>> %s", name)

#define MAC_PASS(name) \
    do { \
        PT_LOG_INFO(log, PT_LOG_CAT_TEST, "[PASS] %s", name); \
        _tests_passed++; \
    } while(0)

#define MAC_FAIL(name, msg) \
    do { \
        PT_LOG_ERR(log, PT_LOG_CAT_TEST, "[FAIL] %s: %s", name, msg); \
        _tests_failed++; \
    } while(0)

#define MAC_SKIP(name, reason) \
    do { \
        PT_LOG_WARN(log, PT_LOG_CAT_TEST, "[SKIP] %s: %s", name, reason); \
        _tests_skipped++; \
    } while(0)

#define MAC_ASSERT(cond, name, msg) \
    if (!(cond)) { MAC_FAIL(name, msg); return; }

/* Summary at end */
#define MAC_TEST_SUMMARY() \
    PT_LOG_INFO(log, PT_LOG_CAT_TEST, \
        "SUMMARY: %d passed, %d failed, %d skipped", \
        _tests_passed, _tests_failed, _tests_skipped); \
    PT_LOG_INFO(log, PT_LOG_CAT_TEST, \
        "RESULT: %s", _tests_failed == 0 ? "PASS" : "FAIL")

#endif
```

---

## POSIX Test Partner

### Purpose

Provides a known-good implementation to validate Mac behavior:
- Broadcasts discovery → Mac should receive
- Listens for discovery → Mac should appear
- Accepts TCP connections → Mac should connect
- Sends/receives messages → Validates protocol encoding

### Implementation: `tests/posix/test_partner.c`

Already have the building blocks in existing POSIX tests. Create a "partner mode":

```c
/* Run as test partner for Mac testing */
int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--partner") == 0) {
        /* Partner mode: cooperate with Mac test */
        const char *test = argv[2];  /* e.g., "discovery", "tcp" */
        return run_as_partner(test);
    }
    /* Normal test mode */
    return run_tests();
}
```

**Partner behaviors:**

| Test | Partner Action |
|------|----------------|
| `discovery` | Broadcast every 2s, log received announcements |
| `tcp-listen` | Accept connection, echo received data |
| `tcp-connect` | Connect to Mac, send test message |
| `messaging` | Bidirectional message exchange |

### Orchestration

The `/mac-test` skill handles coordination:

1. Start POSIX partner in Docker (background)
2. Deploy Mac test app
3. Execute Mac test via LaunchAPPL
4. Wait for completion
5. Fetch logs from both sides
6. Compare results, report combined pass/fail
7. Stop POSIX partner

---

## New Skill: `/mac-test`

### Usage

```bash
/mac-test 5.3              # Run Session 5.3 tests
/mac-test discovery        # Run discovery tests by name
/mac-test all              # Run entire test suite
/mac-test --machine iici   # Run on specific machine
/mac-test --no-partner     # Skip POSIX partner (offline tests only)
```

### Workflow

```
1. Parse arguments (session, test name, machine)

2. Check if test needs POSIX partner
   - Read test registry from tests/mac/tests.json
   - If needs_posix=true, start partner

3. If partner needed:
   - docker run -d peertalk-dev ./test_partner --partner <test>
   - Wait for "Partner ready" in stdout
   - Note: Partner listens on discovery port, TCP port

4. Build Mac test app (if needed):
   - make test-mac
   - Produces build/mac/test_mactcp.bin

5. Deploy to Mac:
   - Use MCP deploy_binary tool
   - Target: configured machine or --machine arg

6. Execute test:
   - Use MCP execute_binary tool
   - Pass session/test as argument: "test_mactcp 5.3"
   - LaunchAPPL runs app, waits for completion

7. Fetch logs:
   - Use MCP fetch_logs tool
   - Download PT_Log from Mac

8. Parse results:
   - Look for "[TEST] RESULT: PASS" or "[TEST] RESULT: FAIL"
   - Extract pass/fail counts from SUMMARY line
   - If partner running, also check partner logs

9. Report:
   - Display combined results
   - Show any failures with context
   - Suggest next steps if failed

10. Cleanup:
    - Stop POSIX partner container
    - Optionally clean up Mac logs
```

### Skill File: `.claude/skills/mac-test/SKILL.md`

```markdown
---
name: mac-test
description: Run MacTCP tests on real Mac hardware with optional POSIX test partner
argument-hint: <session|test-name|all> [--machine <id>] [--no-partner]
---

# Mac Test Runner

Automated testing for MacTCP implementation on real Classic Mac hardware.

## Commands

| Command | Description |
|---------|-------------|
| `<session>` | Run tests for a session (e.g., 5.3) |
| `<name>` | Run tests by name (e.g., discovery) |
| `all` | Run entire test suite |
| `--list` | Show available tests |

## Options

| Option | Description |
|--------|-------------|
| `--machine <id>` | Target specific machine |
| `--no-partner` | Skip POSIX partner (offline tests) |
| `--rebuild` | Force rebuild of test app |

## Implementation

### Step 1: Parse Arguments
Parse session number, test name, or "all". Check --machine and --no-partner flags.

### Step 2: Check Partner Requirement
Read test metadata. If test has `needs_posix=true` and `--no-partner` not set, start partner.

### Step 3: Start POSIX Partner (if needed)
```bash
docker compose -f docker/docker-compose.yml run -d --name peertalk-partner \
  peertalk-dev ./test_partner --partner <test>
# Wait for /workspace/partner_ready.flag to exist (partner writes on ready)
```

### Step 4: Build Mac Test App
```bash
make test-mac  # Produces build/mac/test_mactcp.bin
```

### Step 5: Deploy to Mac
Use MCP `deploy_binary` tool with machine from `--machine` or default.

### Step 6: Execute Test
Use MCP `execute_binary` tool. Session passed via resource modification or separate binary.

### Step 7: Fetch Logs
Use MCP `fetch_logs` tool. Download PT_Log output from Mac.

### Step 8: Parse Results
- Look for `[TEST] RESULT: PASS` or `[TEST] RESULT: FAIL`
- Check for completion marker (closing `========` line)
- If incomplete, report "Test still running" and retry

### Step 9: Stop Partner
```bash
docker stop peertalk-partner && docker rm peertalk-partner
```

### Step 10: Report
Display combined pass/fail. Show any failures with context from both Mac and POSIX logs.
```

---

## Files to Create

### During Phase 5 Implementation

| File | Purpose | When |
|------|---------|------|
| `tests/mac/mac_test.h` | Test framework macros | Session 5.1 |
| `tests/mac/test_mactcp.c` | Combined test runner | Session 5.1, extend each session |
| `tests/mac/tests.json` | Test registry (name, session, needs_posix) | Session 5.1 |
| `tests/posix/test_partner.c` | POSIX test partner | Session 5.3 |
| `.claude/skills/mac-test/SKILL.md` | Skill definition | After first few tests |

**Note on Test Registry:** The `tests.json` file duplicates metadata in the C struct. To avoid maintenance burden, either:

1. **Generate from C code** (recommended):
```makefile
tests/mac/tests.json: tests/mac/test_mactcp.c
	python tools/extract_tests.py $< -o $@
```

2. **Have binary dump its registry:**
```bash
# test_mactcp built with --dump-json support
./test_mactcp --dump-json > tests.json
```

The C struct is the single source of truth; JSON is derived for skill consumption.

### Makefile Additions

```makefile
# Mac test targets
test-mac: $(BUILD_DIR)/mac/test_mactcp.bin

$(BUILD_DIR)/mac/test_mactcp.bin: tests/mac/test_mactcp.c $(MAC_SOURCES)
	$(M68K_CC) $(M68K_CFLAGS) -o $@ $^ $(M68K_LDFLAGS)

# POSIX test partner
test-partner: $(BUILD_DIR)/bin/test_partner

$(BUILD_DIR)/bin/test_partner: tests/posix/test_partner.c $(POSIX_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
```

---

## Test Matrix

### Session → Test Mapping

| Session | Tests | Needs POSIX | Notes |
|---------|-------|-------------|-------|
| 5.1 | driver_open, driver_refnum | No | Verify refNum < 0 |
| 5.2 | udp_create, udp_release, udp_lifecycle | No | |
| 5.3 | discovery_send, discovery_recv, discovery_peer | Yes | |
| 5.4 | tcp_create, tcp_release, tcp_lifecycle, **tcp_listen_timing** | No | CSEND A.3: TCPAbort→re-listen <1ms |
| 5.5 | tcp_listen, tcp_accept | Yes | |
| 5.6 | tcp_connect, tcp_connect_timeout | Yes | |
| 5.7 | tcp_send, tcp_recv, tcp_echo, **tcp_recv_stress** | Yes | CSEND A.4, A.8: order + no -102 |
| 5.8 | integration_full, maxblock_check, **async_pool_stress** | Yes | CSEND A.1: 12+ concurrent, no -108 |

### Cross-Platform Validation Points

| Validation | POSIX sends → Mac receives | Mac sends → POSIX receives |
|------------|---------------------------|---------------------------|
| Discovery magic | Check PTLK header | Check PTLK header |
| Peer name | Name parsed correctly | Name in announcement |
| Message magic | Check PTMG header | Check PTMG header |
| Byte order | ntohl/ntohs correct | htonl/htons correct |
| Connection | Accept works | Connect works |
| Buffer mgmt | RDS returned immediately | RDS returned immediately |
| Reset timing | N/A | TCPAbort→re-listen <1ms (CSEND A.3) |

### CSEND Lessons Verification

Tests should validate these critical MacTCP behaviors from `plan/CSEND-LESSONS.md`:

| Session | CSEND Ref | Test | Validation |
|---------|-----------|------|------------|
| 5.1 | - | driver_open | PBOpen succeeds, refNum < 0 |
| 5.4 | A.3 | tcp_listen_timing | TCPAbort→TCPPassiveOpen < 1ms |
| 5.7 | A.4 | tcp_io | Listen restarts BEFORE data processing |
| 5.7 | A.8 | tcp_recv_stress | No -102 (noMemErr) after 50+ receives |
| 5.8 | A.1 | async_pool_stress | 12+ concurrent ops, no -108 (memFullErr) |
| 5.8 | A.6 | tcp_close_async | Async close completes without 30s block |

**Session 5.8 should verify:**
- [ ] Async pool sized at 16+ handles (no -108 errors)
- [ ] No reset delays after TCPAbort (<1ms)
- [ ] Listen restarts before data processing (<5ms)
- [ ] RDS buffers returned via TCPBfrReturn (no -102 errors)

---

## Verification

### How to verify this works

1. **Skill works end-to-end:**
   - `/mac-test 5.1` builds, deploys, executes, fetches logs, reports PASS/FAIL

2. **Cross-platform tests pass:**
   - `/mac-test 5.3` starts POSIX partner, Mac discovers it, both log same peer

3. **Failures are detected:**
   - Introduce bug → `/mac-test` reports FAIL with context

4. **Logs are useful:**
   - PT_Log output shows state transitions, ASR events, errors

5. **CSEND Lessons Applied:**
   - [ ] MacTCP pool sized at 16+ handles (no -108 errors under load)
   - [ ] No reset delays after TCPAbort (<1ms between abort and re-open)
   - [ ] Listen restarts before data processing (<5ms gap)
   - [ ] RDS buffers returned via TCPBfrReturn (no -102 errors over 50 ops)
   - [ ] Async close used under load (no 30s blocking)

---

## Implementation Order

1. **Session 5.1**: Create `mac_test.h` and initial `test_mactcp.c` with driver tests
2. **Session 5.2**: Add UDP tests to runner
3. **Session 5.3**: Create `test_partner.c`, add discovery tests, create `/mac-test` skill
4. **Sessions 5.4-5.7**: Extend test runner with TCP tests
5. **Session 5.8**: Full integration test

---

## Relationship to Phase 5

This phase is a **companion** to Phase 5 MacTCP:

- **Not blocking:** Phase 5 can proceed without this infrastructure
- **Incremental:** Build test framework alongside Phase 5 sessions
- **Reusable:** Same patterns apply to Phase 6 (OT) and Phase 7 (AppleTalk)

**Recommended workflow:**
1. Implement Phase 5 Session 5.1 (driver init)
2. Implement Phase 5A test framework with driver tests
3. Continue alternating: implement feature → add tests
