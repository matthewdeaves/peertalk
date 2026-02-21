# Performance Optimization - Execution Instructions

Autonomous performance optimization cycle for PeerTalk SDK.

## Input

`$ARGUMENTS`: Optional machine, cycles, focus, and mode flags

Parse format:
```
[machine] [--cycles N] [--focus latency|throughput|stream] [--dry-run]

machine:  Machine ID from machines.json (default: performa6200)
--cycles: Number of optimization cycles (default: 1)
--focus:  Metric to prioritize (default: throughput)
--dry-run: Analyze and recommend without implementing
```

## Execution

### Step 0: Parse Arguments

```
Parse $ARGUMENTS:
  - machine: First non-flag argument, or "performa6200"
  - cycles: Integer after --cycles, or 1
  - focus: "latency" | "throughput" | "stream" after --focus, or "throughput"
  - dry_run: true if --dry-run present

Validate machine exists in machines.json
```

### Step 1: Setup

```
1. Create output directories:
   mkdir -p plan/performance/baselines/<machine>
   mkdir -p plan/performance/optimization/<machine>

2. Get current git commit:
   GIT_COMMIT=$(git rev-parse --short HEAD)

3. Get SDK version from include/peertalk.h:
   grep "PT_VERSION" include/peertalk.h

4. Initialize cycle counter:
   CYCLE=1
```

### Step 2: Establish Baseline (each cycle)

```
1. If CYCLE == 1 or no recent baseline exists:

   Run: /run-test all <machine>

   Wait for all tests to complete.

2. Parse test results into baseline JSON format:

   Read: plan/performance/mactcp/<machine>/latency_*.log (most recent)
   Read: plan/performance/mactcp/<machine>/throughput_*.log (most recent)
   Read: plan/performance/mactcp/<machine>/stream_*.log (most recent)
   Read: plan/performance/mactcp/<machine>/stress_*.log (most recent)

   See [baseline-parser.md](references/baseline-parser.md) for parsing details.

3. Save baseline:
   Write: plan/performance/baselines/<machine>/baseline_<timestamp>.json

4. Display current performance summary:
   Show metrics table with current values vs targets
```

### Step 3: Identify Bottleneck

Use the bottleneck identification logic from [bottleneck-patterns.md](references/bottleneck-patterns.md).

```
1. Compare metrics to targets:

   | Metric | Target | Source |
   |--------|--------|--------|
   | Throughput 256B | 20 KB/s | Historical baseline |
   | Throughput 1024B | 60 KB/s | Historical baseline |
   | Throughput 2048B | 100 KB/s | Historical baseline |
   | Throughput 4096B | 80 KB/s | Historical baseline |
   | Latency 1024B | 30 ms | Historical baseline |
   | Stream unidirectional | 200 KB/s | 10Mbps Ethernet limit |
   | Stress success | 95% | Reliability target |

2. Calculate gap for each metric:
   gap = (target - actual) / target * 100

3. Rank by gap (largest negative = worst performer)

4. Select bottleneck based on --focus and gaps:
   - If --focus specified: prioritize that metric category
   - Otherwise: select metric with largest gap

5. Pattern match bottleneck to root cause:
   See patterns table in SKILL.md
```

### Step 4: Design Fix

Based on bottleneck, select appropriate fix strategy:

```
BOTTLENECK: Large message throughput cliff (4096B << 2048B)
STRATEGY: Adjust fragmentation threshold
FILES: src/core/queue.h
CHANGE: PT_PRESSURE_FRAG_THRESHOLD value

BOTTLENECK: Rate limiting too aggressive
STRATEGY: Increase rate limit under medium pressure
FILES: src/core/peer.c (pt_peer_update_adaptive_params)
CHANGE: rate_limit_bytes_per_sec values

BOTTLENECK: Flow window too small for RTT
STRATEGY: Increase adaptive window max
FILES: src/core/peer.c (pt_peer_update_adaptive_params)
CHANGE: Window size calculation

BOTTLENECK: Pressure thresholds causing early throttle
STRATEGY: Raise pressure thresholds
FILES: src/core/queue.h or include/peertalk.h
CHANGE: PT_PRESSURE_* constants

BOTTLENECK: High latency due to ACK coalescing
STRATEGY: Reduce ACK delay
FILES: src/core/protocol.h or config defaults
CHANGE: ack_delay_ms default

For each change, document:
- Current value
- New value
- Rationale
- Expected impact
```

### Step 5: Implement Fix (unless --dry-run)

```
If --dry-run:
  Display recommended changes
  Skip to Step 8 (Summary)

Otherwise:

1. Read target file
2. Make edit using Edit tool
3. Log change in cycle report

4. Run unit tests:
   docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest make test

   If tests fail:
     Rollback change
     Log failure
     Try alternative strategy or skip to next bottleneck

5. If Mac code modified, run ISR check:
   Spawn subagent to check ISR safety of modified files

   If ISR violations found:
     Rollback change
     Log failure
     Try alternative strategy
```

### Step 6: Verify Improvement

```
1. Rebuild Mac test binaries:
   ./scripts/build-mac-tests.sh mactcp perf

2. Run focused test based on --focus:
   /run-test <focus> <machine>

   Or if analyzing all metrics:
   /run-test throughput <machine>

3. Parse results (same as Step 2)

4. Compare to baseline:
   For each metric:
     change_pct = (new - baseline) / baseline * 100

5. Evaluate:
   - improvement: target metric improved ≥10%
   - neutral: change <10% (within noise)
   - regression: any metric worsened >10%

6. Decision:
   If improvement:
     Save new baseline
     Log success
     Continue to next cycle

   If neutral:
     Keep change (no harm)
     Try different bottleneck next cycle

   If regression:
     Rollback change
     Log failure
     Try alternative strategy for same bottleneck
```

### Step 7: Cycle Loop

```
CYCLE += 1

If CYCLE <= max_cycles:
  Go to Step 2

Otherwise:
  Continue to Step 8
```

### Step 8: Generate Summary

```
Create summary report at:
  plan/performance/optimization/<machine>/summary_<timestamp>.md

Include:
- Number of cycles run
- Changes made (with before/after values)
- Improvements achieved (with percentages)
- Regressions avoided (rollbacks)
- Final vs original baseline comparison
- Recommendations for future optimization

Display summary to user.
```

## Rollback Procedure

If a change causes regression:

```
1. Read the file again
2. Use Edit tool to restore original value
3. Verify tests pass again
4. Log rollback in cycle report
```

## Safety Checks

Before any code change:
1. File must exist and be readable
2. Change must be within expected pattern (not arbitrary)
3. Tests must exist for affected functionality

After any code change:
1. Unit tests must pass
2. ISR safety must be verified (for Mac code)
3. Metric verification must show no critical regressions

## Output Format

Display progress in this format:

```
Performance Optimization: <machine>
========================================

Cycle N/M
---------

[1/4] Establishing baseline...
      Running: /run-test all <machine>
      <progress updates>

[2/4] Identifying bottleneck...
      Analysis: <pattern description>
      Root cause: <identified cause>

[3/4] Implementing fix...
      Strategy: <strategy name>
      Edit: <file>
        - <old line>
        + <new line>
      Running: /build test
      <test result>

[4/4] Verifying improvement...
      Running: /run-test <focus> <machine>
      Results:
      | Metric | Before | After | Change |
      ...
      Verdict: IMPROVEMENT/NEUTRAL/REGRESSION
```

## Dry-Run Mode

When `--dry-run` is specified:

```
Performance Optimization: <machine> (DRY RUN)
=============================================

[1/2] Analyzing current performance...
      Reading existing logs...
      <or running tests if no recent data>

[2/2] Recommendations...

      Bottleneck #1: <description>
      Recommended fix: <strategy>
      Expected impact: <prediction>
      Risk level: <low/medium/high>

      Bottleneck #2: <description>
      ...

No changes made. Run without --dry-run to implement.
```
