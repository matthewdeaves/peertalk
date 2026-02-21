---
name: perf-optimize
description: Autonomous performance optimization cycle for PeerTalk SDK. Runs tests on real hardware, analyzes results, identifies bottlenecks, implements SDK improvements, and verifies gains. Use when you want to iteratively improve network performance without manual intervention.
argument-hint: [machine] [--cycles N] [--focus latency|throughput|stream] [--dry-run]
---

# Autonomous Performance Optimization

Orchestrates complete optimization cycles: test → analyze → identify bottleneck → implement fix → verify improvement.

## Overview

This skill automates the performance tuning loop that would normally require manual intervention:

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│   Run Tests  │────>│   Analyze    │────>│   Identify   │
│  (/run-test) │     │   Results    │     │  Bottleneck  │
└──────────────┘     └──────────────┘     └──────┬───────┘
       ▲                                         │
       │                                         ▼
┌──────┴───────┐     ┌──────────────┐     ┌──────────────┐
│    Verify    │<────│ Implement    │<────│  Design Fix  │
│  Improvement │     │   Fix        │     │              │
└──────────────┘     └──────────────┘     └──────────────┘
```

## Usage

```bash
# Run optimization cycle on Performa 6200
/perf-optimize performa6200

# Run 3 optimization cycles focused on throughput
/perf-optimize performa6200 --cycles 3 --focus throughput

# Analyze and recommend without implementing (dry-run)
/perf-optimize performa6200 --dry-run

# Focus on latency optimization
/perf-optimize --focus latency
```

## What It Does

### Phase 1: Baseline Establishment

1. **Run complete test suite** via `/run-test all <machine>`
   - Captures latency, throughput, stream, stress metrics
   - Saves baseline to `plan/performance/baselines/<machine>/baseline_<timestamp>.json`

2. **Analyze current performance** against expectations:
   - Compare to theoretical limits (10Mbps Ethernet ≈ 1MB/s)
   - Compare to previous baselines
   - Identify metrics furthest from optimal

### Phase 2: Bottleneck Identification

Analyze test results to find the limiting factor:

| Symptom | Likely Bottleneck | Investigation |
|---------|-------------------|---------------|
| High latency, low throughput | Protocol overhead | Check message framing, ACK frequency |
| Good small, poor large msgs | Buffer management | Check fragmentation, window sizing |
| Throughput drops with size | Rate limiting | Check token bucket parameters |
| High RTT variance | Retransmits | Check ACK timing, timeout values |
| RECV << SEND | Backpressure | Check pressure thresholds, peer buffer |
| Memory growth in stress | Leak | Check alloc/free balance |

### Phase 3: Implementation

Based on bottleneck, implements one of these strategies:

| Bottleneck | Strategy | SDK Changes |
|------------|----------|-------------|
| Rate limiting too aggressive | Relax rate limits | Adjust `rate_limit_bytes_per_sec` |
| Fragmentation threshold wrong | Tune threshold | Adjust `PT_PRESSURE_FRAG_THRESHOLD` |
| Flow window too small | Increase window | Adjust adaptive window algorithm |
| Pressure thresholds mistuned | Recalibrate | Adjust `pressure_medium/high/critical` |
| ACK coalescing inefficient | Tune coalescing | Adjust `ack_delay_ms` |
| Chunk size suboptimal | Auto-tune chunk | Adjust `effective_chunk_size` |

**Safety:** All changes are:
- Logged with before/after values
- Covered by existing tests
- Validated with `/check-isr` for Mac code
- Tested with `/build test` before hardware verification

### Phase 4: Verification

1. **Run unit tests** via `/build test`
2. **Run ISR safety check** via `/check-isr` (if Mac code modified)
3. **Run same hardware tests** via `/run-test all <machine>`
4. **Compare to baseline**:
   - Green: ≥10% improvement in target metric
   - Yellow: <10% change (within noise)
   - Red: >10% regression (rollback change)

### Phase 5: Iteration Decision

After verification:
- **Improvement found:** Save new baseline, continue to next cycle
- **No improvement:** Try alternative strategy for same bottleneck
- **Regression:** Rollback change, try different approach
- **Cycles exhausted:** Generate summary report

## Metrics Tracked

### Latency Metrics
- `rtt_min_ms[size]` - Minimum RTT per message size
- `rtt_avg_ms[size]` - Average RTT per message size
- `rtt_max_ms[size]` - Maximum RTT per message size
- `packet_loss_pct[size]` - Loss percentage per size

### Throughput Metrics
- `send_kbps[size]` - Send throughput per buffer size
- `recv_kbps[size]` - Receive throughput per buffer size
- `peak_kbps` - Maximum sustained throughput
- `optimal_chunk` - Best performing chunk size

### Stream Metrics
- `send_kbps_unidirectional` - Mac→POSIX one-way throughput
- `recv_kbps_unidirectional` - POSIX→Mac one-way throughput
- `bidirectional_ratio` - Stream vs echo throughput ratio

### Stability Metrics
- `stress_success_rate` - % of connect/disconnect cycles passing
- `memory_delta_bytes` - Memory leaked during stress test
- `crash_count` - Number of crashes/hangs

## Baseline Format

Baselines are stored as JSON for easy comparison:

```json
{
  "version": "1.0",
  "timestamp": "2026-02-13T12:34:56Z",
  "machine": "performa6200",
  "platform": "mactcp",
  "sdk_version": "1.0.0",
  "git_commit": "abc123",
  "metrics": {
    "latency": {
      "rtt_avg_ms": {"16": 18, "256": 28, "1024": 42, "4096": 85},
      "packet_loss_pct": {"16": 0, "256": 1, "1024": 2, "4096": 3}
    },
    "throughput": {
      "send_kbps": {"256": 17, "512": 32, "1024": 61, "2048": 94, "4096": 9},
      "recv_kbps": {"256": 17, "512": 32, "1024": 61, "2048": 94, "4096": 8}
    },
    "stream": {
      "send_kbps_unidirectional": 456,
      "recv_kbps_unidirectional": 380
    },
    "stress": {
      "success_rate": 98.5,
      "memory_delta_bytes": 1024
    }
  }
}
```

## Example Session

```
$ /perf-optimize performa6200 --cycles 2 --focus throughput

Performance Optimization: Performa 6200
========================================

Cycle 1/2
---------

[1/4] Establishing baseline...
      Running: /run-test all performa6200
      Tests complete. Baseline saved.

      Current Performance:
      | Metric           | Value    | Target   | Gap      |
      |------------------|----------|----------|----------|
      | Throughput 2048B | 94 KB/s  | 150 KB/s | -37%     |
      | Throughput 4096B | 9 KB/s   | 100 KB/s | -91%     |
      | Latency 1024B    | 42 ms    | 30 ms    | +40%     |

[2/4] Identifying bottleneck...
      Analysis: 4096B throughput drops 90% vs 2048B
      Pattern: Large message throughput cliff
      Root cause: Pressure-triggered fragmentation threshold too aggressive

      Current: PT_PRESSURE_FRAG_THRESHOLD = 75
      At 4096B, peer hits 75% pressure → fragments to 2048B
      But fragmentation overhead > raw size benefit

[3/4] Implementing fix...
      Strategy: Raise fragmentation threshold to 85%

      Edit: src/core/queue.h
        - #define PT_PRESSURE_FRAG_THRESHOLD 75
        + #define PT_PRESSURE_FRAG_THRESHOLD 85

      Running: /build test
      All tests pass.

      Running: /check-isr
      No ISR violations.

[4/4] Verifying improvement...
      Running: /run-test throughput performa6200

      Results:
      | Size   | Before  | After   | Change  |
      |--------|---------|---------|---------|
      | 2048B  | 94 KB/s | 92 KB/s | -2%     |
      | 4096B  | 9 KB/s  | 45 KB/s | +400%   |

      Verdict: IMPROVEMENT (+400% at 4096B)
      New baseline saved.

Cycle 2/2
---------

[1/4] Using cycle 1 baseline...

[2/4] Identifying bottleneck...
      Analysis: 4096B still underperforming vs 2048B
      Pattern: Rate limiting kicks in at high pressure
      Root cause: Token bucket refill rate too conservative

[3/4] Implementing fix...
      Strategy: Increase token bucket refill rate under medium pressure

      Edit: src/core/peer.c (pt_peer_update_adaptive_params)
        - rate_limit_bytes_per_sec = 100 * 1024;  /* 100 KB/s */
        + rate_limit_bytes_per_sec = 150 * 1024;  /* 150 KB/s */

      Running: /build test
      All tests pass.

[4/4] Verifying improvement...
      Running: /run-test throughput performa6200

      Results:
      | Size   | Before  | After   | Change  |
      |--------|---------|---------|---------|
      | 4096B  | 45 KB/s | 62 KB/s | +38%    |

      Verdict: IMPROVEMENT (+38% at 4096B)

========================================
OPTIMIZATION COMPLETE
========================================

Summary:
  Cycles run: 2
  Improvements: 2
  Regressions: 0

Total gains from baseline:
  | Metric     | Original | Final   | Improvement |
  |------------|----------|---------|-------------|
  | 4096B      | 9 KB/s   | 62 KB/s | +589%       |
  | 2048B      | 94 KB/s  | 92 KB/s | -2%         |

Changes made:
  1. src/core/queue.h: PT_PRESSURE_FRAG_THRESHOLD 75→85
  2. src/core/peer.c: rate_limit_bytes_per_sec 100KB/s→150KB/s

Recommended next:
  - Run full test suite to verify no regressions
  - Test on Mac SE (lowmem) to verify changes work on constrained hardware
  - Consider adjusting adaptive window for latency improvements
```

## Skill Integration

This skill orchestrates other skills:

| Skill | Usage |
|-------|-------|
| `/run-test` | Run hardware tests, collect metrics |
| `/build test` | Verify unit tests pass after changes |
| `/check-isr` | Validate ISR safety for Mac code |
| `/mac-api` | Look up API docs when investigating |

## Safety Features

1. **No blind changes** - Always analyzes test data before modifying code
2. **Test verification** - Runs `/build test` and `/check-isr` after each change
3. **Rollback support** - Tracks changes for rollback on regression
4. **Conservative defaults** - Prefers small, incremental adjustments
5. **Human review** - Reports all changes made with rationale

## Output Files

| File | Purpose |
|------|---------|
| `plan/performance/baselines/<machine>/baseline_<ts>.json` | Baseline metrics |
| `plan/performance/optimization/<machine>/cycle_<n>_<ts>.md` | Cycle report |
| `plan/performance/optimization/<machine>/summary_<ts>.md` | Final summary |

## Related Skills

- `/run-test` - Execute individual tests
- `/build` - Build and test SDK
- `/check-isr` - Validate interrupt safety
- `/implement` - Manual implementation workflow
- `/review` - Review plans before implementation
