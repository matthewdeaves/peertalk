# Run-Test Skill - Execution Instructions

Orchestrate complete hardware testing workflow on Classic Mac.

## Input

- `$ARGUMENTS`: Test name, optional machine, and options

Parse format:
```
<test> [machine] [--skip-build] [--skip-analysis] [--verbose]

test:     latency | throughput | stress | discovery | mactcp | all
machine:  Machine ID from machines.json (default: performa6200)
```

Examples:
```
$ARGUMENTS = "latency performa6200"
  → test="latency", machine="performa6200"

$ARGUMENTS = "all macse --skip-build"
  → test="all", machine="macse", skip_build=true

$ARGUMENTS = "throughput --verbose"
  → test="throughput", machine="performa6200", verbose=true
```

## Execution Steps

### Step 1: Parse Arguments and Load Machine Config

```
1. Parse test name (required):
   - latency, throughput, stress, discovery, mactcp, all

2. Parse machine (optional, default: performa6200):
   - Read .claude/mcp-servers/classic-mac-hardware/machines.json
   - Extract machine config: platform, ftp, launchappl_port, build

3. Parse options:
   - --skip-build: Don't rebuild test apps
   - --skip-analysis: Skip log analysis step
   - --verbose: Show detailed progress

4. Validate machine exists:
   - If not found: list available machines, exit

5. Determine build type from machine config:
   - If build == "lowmem": use lowmem builds
   - Otherwise: use standard builds
```

### Step 2: Build Test Apps (unless --skip-build)

```
Check if --skip-build flag is set. If not:

For standard builds (most machines):
  Run: ./scripts/build-mac-tests.sh mactcp perf

For lowmem builds (Mac SE, machines with build: "lowmem"):
  Run:
  docker compose -f docker/docker-compose.yml run --rm peertalk-dev \
    make -f Makefile.retro68 PLATFORM=mactcp lowmem_tests

Also build perf_partner:
  docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest \
    make build/bin/perf_partner

Expected outputs:
  Standard: build/mac/test_latency.bin, test_throughput.bin, etc.
  Lowmem: build/mac/test_latency_lowmem.bin, etc.

If build fails:
  Show error message
  Suggest: docker system prune -af && rebuild
  Exit
```

### Step 3: Start Test Partner

Determine partner mode from test type:

| Test | Partner Mode |
|------|--------------|
| latency | echo |
| throughput | echo |
| stress | stress |
| discovery | echo |
| mactcp | echo |
| all | (varies per test) |

**IMPORTANT:** Most tests use echo mode. Only stress uses stress mode.
Throughput uses echo mode (NOT stream) to measure bidirectional SEND/RECV.

```
1. Check if perf-partner container already running:
   docker ps --filter name=perf-partner --format "{{.Names}}"

2. If running, DETECT CURRENT MODE:
   docker logs perf-partner 2>&1 | grep -E "^Mode:" | head -1

   This shows: "Mode: echo" or "Mode: stream" or "Mode: stress"

3. COMPARE required mode vs running mode:
   - If mode matches: Partner is ready, skip restart
   - If mode differs: Must restart with correct mode
   - If can't detect mode: Restart to be safe

   Examples:
   - Running "Mode: echo", need latency → KEEP (echo matches)
   - Running "Mode: echo", need throughput → KEEP (throughput needs echo!)
   - Running "Mode: echo", need stress → RESTART (need stress mode)
   - Running "Mode: stream", need throughput → RESTART (need echo, not stream!)

4. If restart needed, stop old partner:
   docker stop perf-partner && docker rm perf-partner 2>/dev/null

5. Start partner with correct mode:
   docker run -d --name perf-partner --network host \
     -v "$(pwd)":/workspace -w /workspace \
     -e MACHINE_REGISTRY="10.188.1.55:macse,10.188.1.213:performa6200" \
     peertalk-posix:latest ./build/bin/perf_partner --mode <mode> --verbose

6. Wait 2 seconds for partner to start

7. Verify partner mode is correct:
   docker logs perf-partner 2>&1 | grep -E "^Mode:"

   Confirm output shows expected mode (e.g., "Mode: echo" for throughput)

If partner fails to start:
  Show logs
  Suggest: docker logs perf-partner
  Exit
```

### Step 4: Execute Test via LaunchAPPL

```
1. Determine binary path:
   - Standard: build/mac/test_<test>.bin
   - Lowmem: build/mac/test_<test>_lowmem.bin

2. Execute via MCP:
   mcp__classic-mac-hardware__execute_binary(
     machine: "<machine>",
     platform: "mactcp",
     binary_path: "<binary_path>"
   )

3. Handle response:
   - Success with output: Test started, continue
   - Timeout (60s): Expected for long tests, continue monitoring
   - Connection failed: LaunchAPPL not running, suggest /test-machine
   - Binary not found: Build failed, suggest /build

IMPORTANT: A 60-second timeout from execute_binary is NORMAL.
The test continues running on the Mac beyond the timeout.
```

### Step 5: Monitor for Completion

```
Test timeout values:
  latency: 5 minutes
  throughput: 5 minutes
  stress: 8 minutes
  discovery: 3 minutes
  mactcp: 2 minutes

BEFORE polling, note existing log files:
  BEFORE_COUNT=$(ls plan/performance/mactcp/<machine>/<test>_*.log 2>/dev/null | wc -l)

Polling loop (every 30 seconds):
  1. Check for NEW log file (best completion signal):
     AFTER_COUNT=$(ls plan/performance/mactcp/<machine>/<test>_*.log 2>/dev/null | wc -l)
     if [ "$AFTER_COUNT" -gt "$BEFORE_COUNT" ]; then
       → Test complete! Log was streamed and saved.
       → Continue to step 6
     fi

  2. Check partner logs for completion markers:
     docker logs perf-partner 2>&1 | tail -20 | grep -E "GOODBYE|Saving log to|TEST EXITING"

  3. Check partner logs for activity (test still running):
     docker logs perf-partner 2>&1 | tail -5
     - If seeing "Processing msg" or "Echo" lines → test in progress
     - If seeing only "Discovery ANNOUNCE" → test may have completed or not started

  4. If timeout exceeded:
     → Warn user, suggest checking Mac screen
     → Continue to step 6 anyway (may have partial results)
```

### Step 6: Collect BOTH Logs (Mac + Partner)

**CRITICAL: Save BOTH the Mac test app log AND the perf_partner log for complete test records.**

```
A test run produces TWO logs:
  1. Mac test app log - streamed from Mac to partner, auto-saved
  2. perf_partner log - partner's own output (echoes, metrics, errors)

TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG_DIR="plan/performance/mactcp/<machine>"

1. Find the Mac test app log (auto-saved by perf_partner):

   MAC_LOG=$(ls -t ${LOG_DIR}/<test>_*.log 2>/dev/null | head -1)

   If a log file exists with today's timestamp: this is the Mac log.

2. Save the perf_partner log:

   docker logs perf-partner 2>&1 > "${LOG_DIR}/<test>_${TIMESTAMP}_partner.log"

   This captures:
   - Echo/stream activity during test
   - Parsed metrics summary
   - Any errors or warnings
   - Connection events

3. Check perf_partner log for parsed metrics:

   grep -A 20 "METRICS =========" "${LOG_DIR}/<test>_${TIMESTAMP}_partner.log"

   Display these metrics in the final report.

4. ONLY if no Mac log found (streaming failed):

   a. Check if log data is embedded in partner output:
      grep -A 1000 "LOG:" "${LOG_DIR}/<test>_${TIMESTAMP}_partner.log"

   b. If still nothing, try FTP as last resort:
      mcp__classic-mac-hardware__download_file(...)

5. Read results from Mac log:
   tail -100 "$MAC_LOG"

   Look for:
   - "======== LATENCY METRICS ========" section
   - "TEST EXITING" marker

6. Report both log locations:
   Mac log:     ${LOG_DIR}/<test>_${TIMESTAMP}.log (XX KB)
   Partner log: ${LOG_DIR}/<test>_${TIMESTAMP}_partner.log (YY KB)
```

**Log naming convention:**
- `latency_20260211_192936.log` - Mac test app log (streamed)
- `latency_20260211_192936_partner.log` - POSIX partner log

### Step 7: Analyze Results (unless --skip-analysis)

```
Analyze BOTH log files together:
  - Mac log: Test results, RTT/throughput data, completion status
  - Partner log: Echo counts, errors, parsed metrics, connection events

Extract from Mac log:
1. For latency tests:
   - Min/avg/max RTT per message size (from LATENCY METRICS section)
   - Packet loss percentage
   - Any RTT spikes (individual PONG entries with high RTT)

2. For throughput tests:
   - KB/s achieved per buffer size
   - Message counts
   - Send errors or buffer issues

3. For stress tests:
   - Cycles completed vs failed
   - Memory delta (leak detection)
   - Pass/fail status

4. For discovery tests:
   - Discoveries per minute
   - Unique peers found
   - Lost events

Extract from Partner log:
  - Total echoes/messages processed
  - Any errors (would_block, buffer_full, connection_reset)
  - Timing anomalies from partner's perspective
  - Parsed metrics summary (if present)

Cross-reference:
  - Mac sent count should match partner echo count
  - Any discrepancies indicate network or protocol issues

Compare to previous runs if available:
  ls plan/performance/mactcp/<machine>/<test>_*.log | head -5

Present results in a summary table and provide recommendations.
```

### Step 8: Cleanup and Report

```
For single test:
  Report results summary
  Ask: "Keep partner running for more tests? (Y/n)"
  If no: docker stop perf-partner && docker rm perf-partner

For "all" tests:
  Keep partner running between tests
  Run each test sequentially: latency → throughput → stress → discovery → mactcp
  Stop partner after all complete

Final report:
  ========================================
  TEST COMPLETE: <test> on <machine>
  ========================================

  Logs saved:
    Mac:     plan/performance/mactcp/<machine>/<test>_YYYYMMDD_HHMMSS.log
    Partner: plan/performance/mactcp/<machine>/<test>_YYYYMMDD_HHMMSS_partner.log

  Duration: X minutes Y seconds

  [Summary table from analysis]
  [Metrics from partner log if available]

  Next steps:
    - Run another test: /run-test <other-test> <machine>
    - Review all results: cat plan/performance/mactcp/<machine>/*.log
    - Stop partner: docker stop perf-partner && docker rm perf-partner
```

## Error Handling

### Build Failure

```
Build failed: <error message>

Troubleshooting:
  1. Clean Docker: docker system prune -af
  2. Rebuild container: docker build -t peertalk-posix -f docker/Dockerfile.posix .
  3. Check disk space: df -h

Retry with: /run-test <test> <machine>
```

### LaunchAPPL Connection Failed

```
Failed to connect to <machine> via LaunchAPPL.

LaunchAPPLServer may not be running on the Mac.

Steps:
  1. On your Mac, launch LaunchAPPLServer
  2. Enable TCP Server on port 1984
  3. Verify: /test-machine <machine>

Then retry: /run-test <test> <machine>
```

### Test Timeout

```
Test appears to have timed out after <X> minutes.

Possible causes:
  1. Test app crashed on Mac
  2. Network disconnection
  3. Mac hung or froze

Check:
  - Mac screen for error dialogs
  - Partner logs: docker logs perf-partner

If Mac needs reboot, restart and retry.
Partial results may be available in logs.
```

### No Logs Received

```
Test completed but no logs were received.

The Mac may have disconnected before streaming logs.

Manual recovery:
  1. Check partner: docker logs perf-partner 2>&1 | tail -50
  2. If FTP available: /fetch-logs <machine>
  3. Check Mac: PT_<TestName> file should exist

Copy manually if found on Mac via FTP.
```

## Test Sequence for "all"

When test="all", run in this order (matching test complexity):

1. **mactcp** (60s) - Basic connectivity validation
2. **discovery** (2min) - Discovery packet counting
3. **latency** (3min) - RTT measurements
4. **throughput** (3min) - Streaming performance
5. **stress** (5min) - Connection stability

If any test fails critically, ask user whether to continue with remaining tests.

## Important Notes

1. **One test at a time** - Mac test apps bind network ports. Never run multiple tests concurrently on the same machine.

2. **Partner mode MUST match test type**:
   - Most tests need **echo mode**: latency, throughput, discovery, mactcp
   - Only stress test needs **stress mode**
   - **NEVER use stream mode for throughput** (gives RECV=0)
   - Always verify mode with: `docker logs perf-partner 2>&1 | grep "^Mode:"`

3. **Lowmem builds** - Mac SE REQUIRES lowmem builds. Check machine's `build` field in machines.json.

4. **60s LaunchAPPL timeout** - This is normal! Tests run longer than the LaunchAPPL command timeout.

5. **Log streaming** - Mac apps stream logs to partner at completion. Ensure partner stays running until logs are saved.

6. **Canonical log location** - All logs go to `plan/performance/mactcp/<machine>/` with timestamp naming.
