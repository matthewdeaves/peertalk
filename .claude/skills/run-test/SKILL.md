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
  test      latency | throughput | stream | stress | discovery | mactcp | all
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

# Run one-way stream test
/run-test stream performa6200

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

The perf_partner **auto-detects all test types**. Start it once — no mode switching needed.

```bash
docker run -d --name perf-partner --network host \
  -v "$(pwd)":/workspace -w /workspace \
  -e MACHINE_REGISTRY="10.188.1.55:macse,10.188.1.213:performa6200" \
  peertalk-posix:latest ./build/bin/perf_partner --verbose
```

Auto-detection:
- Echo mode (default) handles latency, throughput, stress, discovery, mactcp
- Stream control messages (STRM magic) auto-switch to sink/stream for one-way tests
- No `--mode` flag or restarts needed between tests

### Step 3: Execute Test

Uses LaunchAPPL via MCP to remotely execute the test app:

```bash
mcp__classic-mac-hardware__execute_binary(
    machine="performa6200",
    platform="mactcp",
    binary_path="build/mac/test_latency.bin"
)
```

A 60-second LaunchAPPL timeout is **normal** — tests run longer than the command timeout.

### Step 4: Monitor Completion

**Use short polling intervals, NOT a single long sleep.** After an initial wait, poll every 15s for new log files.

| Test | Initial Wait | Max Wait |
|------|-------------|----------|
| latency | 90s | 4 min |
| throughput | 90s | 4 min |
| stream | 180s | 8 min |
| stress | 60s | 2 min |
| discovery | 90s | 3 min |
| mactcp | 45s | 2 min |

Completion signal: new log file in `plan/performance/mactcp/<machine>/`

### Step 5: Collect BOTH Logs

Each test produces TWO logs. **Both MUST be saved and analyzed.**

1. **Mac test app log** (auto-saved by partner)
   - Contains test results, RTT/throughput data, memory stats, verdict
   - Auto-saved as: `plan/performance/mactcp/<machine>/<test>_YYYYMMDD_HHMMSS.log`

2. **Partner log** (save manually after each test)
   - Contains echo counts, stream phase KB/s, errors, connection events
   - Save with: `docker logs --since <start_time> perf-partner > <test>_YYYYMMDD_HHMMSS_partner.log`
   - Save to same directory as Mac log

### Step 6: Display Summary Table

**CRITICAL: Always display a formatted summary table after every test.**

See [summary-tables.md](references/summary-tables.md) for the exact table format per test type.

Tables combine data from BOTH logs:
- Mac log provides: test metrics, memory stats, verdict
- Partner log provides: POSIX-side measurements, byte counts, error verification

When previous runs exist, include a comparison section showing changes.

### Step 7: Output JSON Metrics

**Always output a structured JSON metrics block** for `/perf-optimize` consumption:

```
========================================
METRICS (JSON)
========================================
{
  "test": "throughput",
  "machine": "performa6200",
  "platform": "mactcp",
  "timestamp": "2026-02-21T14:32:10Z",
  "status": "pass",
  "metrics": { ... },
  "logs": {
    "mac": "plan/performance/mactcp/performa6200/throughput_20260221_143210.log",
    "partner": "plan/performance/mactcp/performa6200/throughput_20260221_143210_partner.log"
  }
}
========================================
```

See [metrics-output.md](references/metrics-output.md) for test-specific metric formats.

## Test Descriptions

| Test | Purpose | Measures |
|------|---------|----------|
| latency | RTT measurement | Min/avg/max RTT per message size (16-4096B) |
| throughput | Bidirectional echo | KB/s sustained throughput per buffer size |
| stream | One-way streaming | True unidirectional capacity (Mac→POSIX, POSIX→Mac) |
| stress | Connection stability | Connect/disconnect cycles, memory leaks |
| discovery | Peer discovery | Discovery rate, unique peers, lost events |
| mactcp | Basic connectivity | UDP broadcast, TCP connection |
| all | Run all tests | Complete test suite sequentially |

## Test Sequence for "all"

Run in this order (see [test-modes.md](references/test-modes.md)):

1. **latency** (~2 min) — RTT measurements
2. **throughput** (~2.5 min) — Bidirectional echo throughput
3. **stream** (~6 min) — One-way streaming capacity
4. **stress** (~1 min) — Connection stability
5. **discovery** (~2 min) — Discovery observation

Display individual summary tables after each test, then an overall summary at the end.

## Machine Requirements

Machines are auto-detected from `machines.json`:

| Machine | RAM | Build Type |
|---------|-----|------------|
| performa6200 | 8MB | standard |
| macse | 4MB | lowmem |

**Mac SE requires lowmem builds!** Standard builds request 2-3MB heap and won't launch.

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
- Try running the test again (logs stream at end of each run)

### "Build failed"

- Docker container issue
- Rebuild container: `docker build -t peertalk-posix -f docker/Dockerfile.posix .`

## Related Skills

- `/build test` - Build POSIX tests
- `/test-machine` - Verify LaunchAPPL connectivity
- `/execute` - Manual test execution
- `/test-partner` - Manage partner container
- `/perf-optimize` - Autonomous optimization using these tests

## See Also

- [Summary Tables](references/summary-tables.md) - Exact table formats per test type
- [Log Parsing](references/log-parsing.md) - Metric extraction patterns
- [Metrics Output](references/metrics-output.md) - JSON metrics format
- [Test Modes](references/test-modes.md) - Test durations and partner behavior
- [Test Log Management](../../../rules/test-logs.md)
- [Classic Mac Hardware Rules](../../../rules/classic-mac-hardware.md)
