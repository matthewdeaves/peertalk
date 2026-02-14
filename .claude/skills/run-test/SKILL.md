---
name: run-test
description: Run hardware tests on Classic Mac with full workflow automation. Builds apps, starts partner, executes via LaunchAPPL, collects logs, analyzes results.
argument-hint: <test> [machine] [--skip-build] [--skip-analysis] [--verbose]
---

# Run Hardware Tests on Classic Mac

Orchestrates complete hardware testing workflow: build, deploy, execute, collect, analyze.

## Usage

```bash
/run-test <test> [machine] [options]

Arguments:
  test      latency | throughput | stress | discovery | mactcp | all
  machine   Machine ID (default: performa6200)

Options:
  --skip-build      Use existing binaries (don't rebuild)
  --skip-analysis   Skip log analysis
  --verbose         Detailed progress output
```

## Examples

```bash
# Run latency test on Performa 6200
/run-test latency performa6200

# Run all tests on Mac SE (uses lowmem builds automatically)
/run-test all macse

# Quick re-run with existing binaries
/run-test throughput --skip-build

# Full test suite with verbose output
/run-test all performa6200 --verbose
```

## What It Does

### Step 1: Build Test Apps

Automatically builds the correct variant based on machine memory:
- **Standard builds** (2-3MB heap) for 8MB+ machines (Performa 6200)
- **Lowmem builds** (384-512KB heap) for 4MB machines (Mac SE)

```bash
# Standard
./scripts/build-mac-tests.sh mactcp perf

# Lowmem (Mac SE)
make -f Makefile.retro68 PLATFORM=mactcp lowmem_tests
```

### Step 2: Check/Start Test Partner

Verifies the POSIX perf_partner is running in the correct mode:

| Test | Partner Mode | Duration |
|------|--------------|----------|
| latency | echo | 2-3 min |
| throughput | echo | 2-3 min |
| stress | stress | ~5 min |
| discovery | echo | 2 min |
| mactcp | echo | 60 sec |

**Mode detection:** If partner is already running, checks its mode with:
```bash
docker logs perf-partner 2>&1 | grep "^Mode:"
```

If mode doesn't match the required mode, restarts partner automatically.

**Common mistake:** Throughput uses **echo mode** (not stream). Stream mode
gives RECV=0 because the partner doesn't echo messages back.

### Step 3: Execute Test

Uses LaunchAPPL via MCP to remotely execute the test app:

```bash
mcp__classic-mac-hardware__execute_binary(
    machine="performa6200",
    platform="mactcp",
    binary_path="build/mac/test_latency.bin"
)
```

### Step 4: Monitor Completion

Polls for test completion by watching for:
- `"TEST EXITING - cleaning up..."` in partner logs
- Log file appearing in `plan/performance/mactcp/<machine>/`
- Timeout based on test type

### Step 5: Collect BOTH Logs

Each test produces TWO logs that should be analyzed together:

1. **Mac test app log** (streamed from Mac to partner, auto-saved)
   - Contains test results, RTT/throughput data, completion status
   - Saved as: `plan/performance/mactcp/<machine>/<test>_YYYYMMDD_HHMMSS.log`
   - Mac apps clear their logs at startup (fresh each run)

2. **Partner log** (POSIX perf_partner output)
   - Contains echo counts, errors, connection events
   - Save with: `docker logs perf-partner > <test>_<timestamp>_partner.log`

### Step 6: Analyze BOTH Logs Together

Analysis should cross-reference both logs:
1. **From Mac log**: Extract statistics (RTT, KB/s, success rates)
2. **From Partner log**: Verify message counts, check for errors
3. **Cross-reference**: Mac sent count should match partner echo count
4. Compare to previous runs and suggest improvements

## Test Descriptions

| Test | Purpose | Measures |
|------|---------|----------|
| latency | RTT measurement | Min/avg/max RTT per message size |
| throughput | Streaming performance | KB/s sustained throughput |
| stress | Connection stability | Connect/disconnect cycles, memory leaks |
| discovery | Peer discovery | Discovery packets, unique peers |
| mactcp | Basic connectivity | UDP broadcast, TCP connection |
| all | Run all tests | Complete test suite |

## Machine Requirements

Machines are auto-detected from `machines.json`:

| Machine | RAM | Build Type |
|---------|-----|------------|
| performa6200 | 8MB | standard |
| macse | 4MB | lowmem |

**Mac SE requires lowmem builds!** Standard builds request 2-3MB heap and won't launch.

## Output Example

```
Running Latency Test on Performa 6200
=====================================

[1/6] Building test apps...
      ./scripts/build-mac-tests.sh mactcp perf
      Built: test_latency.bin (45KB)

[2/6] Starting test partner (echo mode)...
      docker run -d --name perf-partner ...
      Partner running on ports 7353/7354

[3/6] Executing test via LaunchAPPL...
      Connecting to 10.188.1.213:1984...
      Binary transferred, executing...
      (60s timeout - test runs longer, this is normal)

[4/6] Waiting for test completion...
      Polling partner logs every 30s...
      Test running: 0/100 pings complete
      Test running: 50/100 pings complete
      Test complete! (2m 15s)

[5/6] Collecting logs...
      Mac log:     plan/performance/mactcp/performa6200/latency_20260211_143215.log
      Partner log: plan/performance/mactcp/performa6200/latency_20260211_143215_partner.log

[6/6] Analyzing results...

      LATENCY RESULTS (Performa 6200)
      ===============================
      Size    Min    Avg    Max    Loss
       16B    12ms   18ms   45ms   0%
       64B    14ms   21ms   52ms   0%
      256B    18ms   28ms   78ms   1%
      1024B   25ms   42ms  125ms   2%
      4096B   45ms   85ms  245ms   3%

      OBSERVATIONS:
      - RTT scales ~linearly with message size (good)
      - Loss increases at larger sizes (expected for 68k)
      - Max RTT spikes suggest occasional retransmits

      RECOMMENDATIONS:
      - Consider reducing default message size for reliability
      - Buffer pressure at 4KB - check TCP_NODELAY

Test complete. Partner still running for additional tests.
Stop with: docker stop perf-partner && docker rm perf-partner
```

## Troubleshooting

### "LaunchAPPL connection failed"

- LaunchAPPLServer not running on Mac
- Check port 1984 is enabled
- Verify with `/test-machine <machine>`

### "Test timeout"

- Mac app may have crashed
- Check Mac screen for errors
- Reboot Mac and retry

### "No logs received"

- Mac disconnected before log stream completed
- Check partner logs: `docker logs perf-partner`
- Fetch manually: `/fetch-logs <machine>`

### "Build failed"

- Docker container issue
- Rebuild container: `docker build -t peertalk-posix -f docker/Dockerfile.posix .`

### "RECV=0 in throughput test"

- Partner is in wrong mode (stream instead of echo)
- Fix: Restart partner in echo mode
  ```bash
  docker stop perf-partner && docker rm perf-partner
  docker run -d --name perf-partner --network host \
    -v "$(pwd)":/workspace -w /workspace \
    peertalk-posix:latest ./build/bin/perf_partner --mode echo --verbose
  ```
- Verify mode: `docker logs perf-partner 2>&1 | grep "^Mode:"`

## Structured Metrics Output

After analysis, outputs structured JSON metrics for automation:

```
========================================
METRICS (JSON)
========================================
{
  "test": "throughput",
  "machine": "performa6200",
  "status": "pass",
  "metrics": {
    "send_kbps": {"256": 17, "1024": 61, "2048": 94, "4096": 9},
    "peak_kbps": 94,
    "optimal_chunk": 2048
  }
}
========================================
```

This enables `/perf-optimize` to:
- Establish performance baselines
- Track improvements across optimization cycles
- Detect regressions automatically

See [metrics-output.md](references/metrics-output.md) for complete format.

## Related Skills

- `/build test` - Build POSIX tests
- `/test-machine` - Verify LaunchAPPL connectivity
- `/fetch-logs` - Manually fetch logs
- `/execute` - Manual test execution
- `/test-partner` - Manage partner container
- `/perf-optimize` - Autonomous optimization using these tests

## See Also

- [Test Log Management](../../../rules/test-logs.md)
- [Classic Mac Hardware Rules](../../../rules/classic-mac-hardware.md)
- [machines.json](../../mcp-servers/classic-mac-hardware/machines.json)
