# Run-Test Skill - Execution Instructions

Orchestrate complete hardware testing workflow on Classic Mac.

**KEY INSIGHT: Logs are AUTO-STREAMED and saved locally. NEVER use FTP to download logs.**
- Mac test apps stream logs to perf_partner at completion
- perf_partner auto-saves to `plan/performance/mactcp/<machine>/<test>_<timestamp>.log`
- Just read the local file - it's already there!

## Input

- `$ARGUMENTS`: Test name, optional machine, and options

Parse format:
```
<test> [machine] [--skip-build] [--skip-analysis] [--verbose]

test:     latency | throughput | stream | stress | discovery | all
machine:  Machine ID from machines.json (default: performa6200)
```

Examples:
```
$ARGUMENTS = "latency performa6200"
  → test="latency", machine="performa6200"

$ARGUMENTS = "stream macse --skip-build"
  → test="stream", machine="macse", skip_build=true

$ARGUMENTS = "throughput --verbose"
  → test="throughput", machine="performa6200", verbose=true
```

## Execution Steps

### Step 1: Parse Arguments and Load Machine Config

```
1. Parse test name (required):
   - latency, throughput, stream, stress, discovery, all

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
  Run: ./scripts/build-mac-tests.sh mactcp lowmem

Also build perf_partner:
  docker run --rm -u "$(id -u):$(id -g)" -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest \
    make build/bin/perf_partner

Expected outputs:
  Standard: build/mac/test_latency.bin, test_throughput.bin, test_stream.bin, etc.
  Lowmem: build/mac/test_latency_lowmem.bin, etc.

If build fails:
  Show error message
  Suggest: docker system prune -af && rebuild
  Exit
```

### Step 3: Start Test Partner

The perf_partner auto-detects ALL test types. Start once, no restarts needed.

```
1. Check if perf-partner container already running:
   docker ps --filter name=perf-partner --format "{{.Names}}"

2. If running: Partner is ready for ALL test types. No restart needed.

3. If NOT running, start partner:
   docker run -d --name perf-partner --network host \
     -u "$(id -u):$(id -g)" -v "$(pwd)":/workspace -w /workspace \
     -e MACHINE_REGISTRY="10.188.1.55:macse,10.188.1.213:performa6200" \
     peertalk-posix:latest ./build/bin/perf_partner --verbose

4. Wait 2 seconds for partner to start

5. Verify partner is running:
   docker logs perf-partner 2>&1 | head -10

If partner fails to start:
  Show logs
  Suggest: docker logs perf-partner
  Exit
```

### Step 4: Execute Test via LaunchAPPL

```
1. Record start timestamp for partner log filtering:
   START_TIME=$(date +%Y%m%d_%H%M%S)

2. Determine binary path:
   - Standard: build/mac/test_<test>.bin
   - Lowmem: build/mac/test_<test>_lowmem.bin

3. Execute via MCP:
   mcp__classic-mac-hardware__execute_binary(
     machine: "<machine>",
     platform: "mactcp",
     binary_path: "<binary_path>"
   )

4. Handle response:
   - Success with output: Test completed within 60s
   - Timeout (60s): Expected for long tests, continue monitoring
   - Connection failed: LaunchAPPL not running, suggest /test-machine
   - Binary not found: Build failed, suggest /build

IMPORTANT: A 60-second timeout from execute_binary is NORMAL.
The test continues running on the Mac beyond the timeout.
```

### Step 5: Monitor for Completion

**CRITICAL: Use short polling intervals (sleep 15-30s), NOT a single long sleep.**

```
Initial wait before first poll (just enough for test to get going):
  latency: sleep 90
  throughput: sleep 90
  stream: sleep 180
  stress: sleep 60
  discovery: sleep 90

Max wait (give up after this):
  latency: 4 minutes
  throughput: 4 minutes
  stream: 8 minutes
  stress: 2 minutes
  discovery: 3 minutes

BEFORE polling, note existing log files:
  BEFORE_COUNT=$(ls plan/performance/mactcp/<machine>/<test>_*.log 2>/dev/null | wc -l)

After initial wait, poll every 15 seconds:
  1. Check for NEW log file (best completion signal):
     AFTER_COUNT=$(ls plan/performance/mactcp/<machine>/<test>_*.log 2>/dev/null | wc -l)
     if [ "$AFTER_COUNT" -gt "$BEFORE_COUNT" ]; then
       → Test complete! Log was streamed and saved.
       → Continue to step 6
     fi

  2. If no new log yet, sleep 15 more seconds and re-check.

  3. If max wait exceeded:
     → Warn user, suggest checking Mac screen
     → Continue to step 6 anyway (may have partial results)
```

### Step 6: Collect BOTH Logs (Mac + Partner)

**CRITICAL: ALWAYS save BOTH logs. Partner logs contain valuable POSIX-side data.**

```
LOG_DIR="plan/performance/mactcp/<machine>"

1. Find the Mac test app log (ALREADY auto-saved by perf_partner):

   MAC_LOG=$(ls -t ${LOG_DIR}/<test>_*.log | grep -v _partner | head -1)

   This file ALREADY EXISTS locally. Do NOT download via FTP.

2. Extract the timestamp from the Mac log filename:

   # e.g., latency_20260221_142714.log → TIMESTAMP=20260221_142714
   TIMESTAMP=$(basename "$MAC_LOG" .log | sed "s/^<test>_//")

3. Save the perf_partner log WITH MATCHING TIMESTAMP:

   docker logs perf-partner 2>&1 > "${LOG_DIR}/<test>_${TIMESTAMP}_partner.log"

   This ensures Mac log and partner log are paired:
     latency_20260221_142714.log          ← Mac log
     latency_20260221_142714_partner.log  ← Partner log

4. If no Mac log found (streaming failed):

   Check if log data is embedded in partner output:
     docker logs perf-partner 2>&1 | tail -200 | grep -A 1000 "LOG:"

   If still no logs, report streaming failure.

5. Report both log locations:
   Mac log:     ${LOG_DIR}/<test>_${TIMESTAMP}.log (XX KB)
   Partner log: ${LOG_DIR}/<test>_${TIMESTAMP}_partner.log (YY KB)
```

### Step 7: Display Summary Table

**CRITICAL: ALWAYS display a formatted summary table after EVERY test. This is NOT optional.**

Follow the exact table formats in [summary-tables.md](references/summary-tables.md).

```
1. Read Mac test app log — extract primary metrics:
   See [log-parsing.md](references/log-parsing.md) for extraction patterns.

   - latency: SIZE lines → min/avg/max/sent/recv/lost per size
   - throughput: COMPLETE lines → send_kbps/recv_kbps/msgs per size
   - stream: SEND COMPLETE/RECV COMPLETE lines → send_kbps/recv_kbps per size
   - stress: Results section → cycles/successes/failures/memory
   - discovery: Summary section → discoveries/unique/first_time/lost

2. Read Partner log — extract POSIX-side data:
   See [log-parsing.md](references/log-parsing.md) for partner extraction patterns.

   - stream: SINK/STREAM phase complete lines → kbps and byte counts from POSIX side
   - all tests: error/warning count, message count

3. Display formatted summary table:
   Use the EXACT format from [summary-tables.md](references/summary-tables.md).
   Include partner-side data where relevant (especially stream test).

4. Compare to previous run (if one exists):
   Find previous Mac log: ls -t ${LOG_DIR}/<test>_*.log | grep -v _partner | head -2
   If a previous run exists, show a comparison table with % change.

5. Show verdict prominently:
   VERDICT: PASS/WARN/FAIL
```

### Step 8: Output JSON Metrics

**ALWAYS output structured JSON metrics for /perf-optimize consumption.**

```
After the summary table, output:

========================================
METRICS (JSON)
========================================
{
  "test": "<test>",
  "machine": "<machine>",
  "platform": "mactcp",
  "timestamp": "<ISO8601>",
  "status": "pass|warn|fail",
  "metrics": {
    <test-specific — see references/metrics-output.md>
  },
  "partner": {
    <partner-side data where available>
  },
  "logs": {
    "mac": "<path to mac log>",
    "partner": "<path to partner log>"
  }
}
========================================

See [metrics-output.md](references/metrics-output.md) for test-specific formats.

Include partner-side metrics:
  - stream: sink_kbps, stream_kbps, total_bytes per phase
  - all tests: message_count, error_count from partner log
```

### Step 9: Cleanup and Report

```
For single test:
  Leave partner running (user may want more tests)
  Show log file paths

For "all" tests:
  Keep partner running between tests
  Run each test sequentially: latency → throughput → stream → stress → discovery
  Display individual summary table after EACH test
  After all tests, display combined "All Tests Summary" table
  (see summary-tables.md for format)

Final output:
  ========================================
  TEST COMPLETE: <test> on <machine>
  ========================================

  Logs saved:
    Mac:     plan/performance/mactcp/<machine>/<test>_YYYYMMDD_HHMMSS.log
    Partner: plan/performance/mactcp/<machine>/<test>_YYYYMMDD_HHMMSS_partner.log

  [Summary Table — see summary-tables.md]
  [JSON Metrics — see metrics-output.md]

  Partner still running for additional tests.
  Stop with: docker stop perf-partner && docker rm perf-partner
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

Check partner output:
  docker logs perf-partner 2>&1 | tail -100

If the test completed on the Mac but logs weren't streamed:
  - Rerun the test (logs stream at end of each run)
```

## Test Sequence for "all"

When test="all", run in this order:

1. **latency** (3 min) — RTT measurements
2. **throughput** (3 min) — Bidirectional echo throughput
3. **stream** (5-8 min) — One-way streaming (true unidirectional capacity)
4. **stress** (1-2 min) — Connection stability
5. **discovery** (2 min) — Discovery observation

After EACH test: display summary table + save partner log.
After ALL tests: display combined "All Tests Summary" table.

No partner restarts needed between tests.

If any test fails critically, ask user whether to continue with remaining tests.

## Important Notes

1. **One test at a time** — Mac test apps bind network ports. Never run concurrent tests.

2. **Partner auto-detects all test types** — Start once, no mode switching needed.

3. **Lowmem builds** — Mac SE REQUIRES lowmem builds. Check `build` field in machines.json.

4. **60s LaunchAPPL timeout** — Normal! Tests run longer than the command timeout.

5. **ALWAYS save partner logs** — They contain POSIX-side measurements essential for stream test analysis and cross-validation of all test types.

6. **ALWAYS display summary tables** — Use the exact formats in [summary-tables.md](references/summary-tables.md). This is the primary output the user sees.

7. **ALWAYS output JSON metrics** — Enables `/perf-optimize` to track baselines and detect regressions.

8. **Stream test vs throughput test**:
   - `throughput` — bidirectional echo (Mac sends, partner echoes back)
   - `stream` — one-way streaming (Mac→Partner then Partner→Mac, no echo)
   - Stream test shows true unidirectional capacity (typically higher than echo)
