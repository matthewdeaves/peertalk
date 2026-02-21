# Analysis Prompts

Prompts for analyzing test results. Always analyze BOTH Mac log and partner log together.

## Generic Analysis Prompt

```
Analyze the test results from BOTH log files.

Mac log: {MAC_LOG}
Partner log: {PARTNER_LOG}

Context:
- Machine: {MACHINE_NAME} ({MACHINE_PLATFORM})
- Test: {TEST_TYPE}

Tasks:
1. Extract all metrics from the Mac log
2. Cross-reference with partner log data
3. Identify any anomalies or discrepancies between the two
4. Compare to previous runs (if available in same directory)
5. Provide actionable recommendations

Format your response as:

## Summary
[1-2 sentence overview]

## Metrics
[Table matching the format in summary-tables.md]

## Partner Verification
[Any discrepancies between Mac and partner measurements]

## Observations
[Bullet points of notable findings]

## Recommendations
[Actionable next steps]
```

## Latency Test Prompt

```
Analyze latency test results.

Mac log: {MAC_LOG}
Partner log: {PARTNER_LOG}

Extract from Mac log:
1. Per-size RTT statistics from "SIZE N:" lines (min/avg/max for 16B, 64B, 256B, 1024B, 4096B)
2. Packet loss per size (sent vs recv vs lost counts)
3. Total pings sent/received

Extract from partner log:
1. [MESSAGE] count — should match total pings sent by Mac
2. Any errors or connection issues

Expected patterns for MacTCP on Performa 6200:
- 16-64B: 0-16ms avg RTT (TickCount ~1ms resolution)
- 256-1024B: 0-33ms avg RTT
- 4096B: 16-50ms avg RTT
- Loss: 0% is normal, >5% indicates issues

Display results using the latency table format from summary-tables.md.

Compare to previous runs in: plan/performance/mactcp/{MACHINE}/latency_*.log
Report trends (improving/degrading) and highlight any regressions.
```

## Throughput Test Prompt

```
Analyze throughput test results.

Mac log: {MAC_LOG}
Partner log: {PARTNER_LOG}

Extract from Mac log (use "COMPLETE" lines or results section):
1. SEND and RECV KB/s per buffer size (256, 512, 1024, 2048, 4096)
2. Message counts per size
3. Error counts per size
4. Memory stats (FreeMem, MaxBlock)

Extract from partner log:
1. Echo/message counts — should match Mac message counts
2. Any errors or would_block events

Expected patterns for MacTCP on Performa 6200 (echo-based bidirectional):
- 256B: ~16 KB/s (SEND ≈ RECV since echo-based)
- 512B: ~33 KB/s
- 1024B: ~57 KB/s
- 2048B: ~85 KB/s (often optimal)
- 4096B: variable (may drop due to backpressure)

Note: SEND ≈ RECV is expected for echo-based throughput (partner echoes all messages).
If RECV << SEND, check for partner errors or backpressure.

Display results using the throughput table format from summary-tables.md.
```

## Stream Test Prompt

```
Analyze one-way stream test results.

Mac log: {MAC_LOG}
Partner log: {PARTNER_LOG}

Extract from Mac log:
1. SEND KB/s per size from "SEND COMPLETE" lines (Mac→POSIX unidirectional)
2. RECV KB/s per size from "RECV COMPLETE" lines (POSIX→Mac unidirectional)
3. Message counts per size
4. Memory stats (FreeMem, MaxBlock)

Extract from partner log (ESSENTIAL for stream test):
1. SINK phase throughput from "[STREAM-TEST] SINK phase complete" lines
   - This is the POSIX-side measurement of Mac→POSIX throughput
   - Includes exact byte counts and duration
2. STREAM phase throughput from "[STREAM-TEST] STREAM phase complete" lines
   - This is the POSIX-side measurement of POSIX→Mac throughput
   - Includes exact byte counts and duration

Cross-reference:
- Mac SEND KB/s should be close to partner SINK KB/s
- Mac RECV KB/s should be close to partner STREAM KB/s
- Large discrepancies suggest measurement issues

Expected patterns for MacTCP on Performa 6200:
- SEND >> RECV is normal (Mac sending is faster than receiving)
- SEND 4096B: 400-500 KB/s (approaching 10Mbps Ethernet theoretical)
- RECV 4096B: 100-120 KB/s (limited by Mac TCP receive processing)
- SEND/RECV ratio typically 3-5x

Display results using the stream table format from summary-tables.md.
Include the "Partner Perspective" sub-table with POSIX-side measurements.
```

## Stress Test Prompt

```
Analyze stress test results.

Mac log: {MAC_LOG}
Partner log: {PARTNER_LOG}

Extract from Mac log:
1. Connection cycles (attempted, successes, failures)
2. Success rate percentage
3. Messages sent
4. Memory analysis (initial/final FreeMem and MaxBlock)

Extract from partner log:
1. Connection/disconnection event counts (should match cycle count)
2. Any error events during rapid connect/disconnect

Pass criteria:
- 100% cycle success rate
- No crashes or hangs
- Memory delta explained by normal allocation patterns

Memory leak detection:
- Some delta is normal (caching, first-time allocations)
- Growing delta across repeated test runs = concerning
- Suggest checking: NewPtr/DisposePtr balance, handle cleanup

Display results using the stress table format from summary-tables.md.
```

## Discovery Test Prompt

```
Analyze discovery test results.

Mac log: {MAC_LOG}
Partner log: {PARTNER_LOG}

Extract from Mac log:
1. Total discovery events
2. Unique peers found
3. Time to first discovery (ms)
4. Average interval between discoveries
5. Peer lost/recovered events
6. Per-peer details (name, IP, seen count, lost count, status)

Extract from partner log:
1. Discovery broadcast count from partner side
2. Any "peer timeout" or connection events

Expected patterns:
- POSIX partner broadcasts every ~10 seconds
- Should see ~12 discoveries in 120 seconds
- Time to first discovery: 1-10 seconds typical
- Lost events should be 0

If high time to first discovery:
- Mac UDP listener may be slow to start
- Network convergence delay

Display results using the discovery table format from summary-tables.md.
Include the per-peer table.
```

## Comparison Analysis Prompt

```
Compare test results across multiple runs.

Current run: {CURRENT_MAC_LOG}
Previous runs: {PREVIOUS_MAC_LOGS}

Analysis:
1. Extract key metrics from all runs
2. Compute averages and trends
3. Identify regressions (>10% worse than average)
4. Identify improvements (>10% better than average)

Report format:

## Trend Analysis

| Metric | Previous | Current | Change |
|--------|----------|---------|--------|
| ...    | ...      | ...     | +/-X%  |

## Notable Changes
[Any significant regressions or improvements]

## Confidence
[How many data points for comparison]

## Recommendations
[Based on trend direction]
```

## Error Investigation Prompt

```
Investigate errors found in test logs.

Mac log: {MAC_LOG}
Partner log: {PARTNER_LOG}

Errors detected:
{ERROR_LINES}

For each error:
1. Identify the error code/type
2. Look up meaning (MacTCP errors, POSIX errors)
3. Determine likely cause
4. Suggest fix or workaround

Common MacTCP errors:
- -23000 to -23049: TCP errors (connection issues)
- -23044: connectionClosing (peer disconnected)
- -23006: connectionTerminated (abrupt close)
- -23012: connectionDoesntExist (stale reference)

Provide:
- Root cause analysis
- Code locations to check (search patterns)
- Similar issues in project history (if known)
```

## Summary Report Prompt (for "all" tests)

```
Generate summary report for complete test suite.

Tests completed:
- latency: {LATENCY_MAC_LOG} + {LATENCY_PARTNER_LOG}
- throughput: {THROUGHPUT_MAC_LOG} + {THROUGHPUT_PARTNER_LOG}
- stream: {STREAM_MAC_LOG} + {STREAM_PARTNER_LOG}
- stress: {STRESS_MAC_LOG} + {STRESS_PARTNER_LOG}
- discovery: {DISCOVERY_MAC_LOG} + {DISCOVERY_PARTNER_LOG}

Create summary using the "All Tests Summary" table format from summary-tables.md:

## Overall Status
[PASS/FAIL with brief explanation]

## Key Metrics
| Test       | Verdict | Key Metric           | Log Size |
|------------|---------|----------------------|----------|
| Latency    | PASS    | Xms avg, Y% loss     |   XX KB  |
| Throughput | PASS    | X KB/s peak (YB)     |   XX KB  |
| Stream     | PASS    | X KB/s SEND peak     |   XX KB  |
| Stress     | PASS    | X/Y cycles, no leaks |   XX KB  |
| Discovery  | PASS    | X in Ys, Z lost      |   XX KB  |
| Overall    | PASS    | N/N tests passed     |          |

## Critical Issues
[Any failures or concerns requiring immediate attention]

## Recommendations
[Prioritized list of improvements]
```
