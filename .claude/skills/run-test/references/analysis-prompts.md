# Analysis Subagent Prompts

Prompts for the analysis subagent to parse test results and provide recommendations.

## Generic Analysis Prompt

```
Analyze the test results in {LOG_FILE}.

Context:
- Machine: {MACHINE_NAME} ({MACHINE_PLATFORM})
- Test: {TEST_TYPE}
- Duration: {DURATION}

Tasks:
1. Extract all metrics from the log
2. Identify any anomalies or concerns
3. Compare to previous runs (if available in same directory)
4. Provide actionable recommendations

Format your response as:

## Summary
[1-2 sentence overview]

## Metrics
[Table of extracted values]

## Observations
[Bullet points of notable findings]

## Recommendations
[Actionable next steps]
```

## Latency Test Prompt

```
Analyze latency test results in {LOG_FILE}.

Extract:
1. Per-size RTT statistics (min/avg/max for 16B, 64B, 256B, 1024B, 4096B)
2. Packet loss percentages per size
3. Any RTT spikes (max >> avg)
4. Total pings sent/received

Expected patterns for MacTCP on {MACHINE}:
- 16-64B: 10-30ms typical RTT
- 256-1024B: 20-50ms typical RTT
- 4096B: 50-150ms typical RTT
- Loss: <5% is acceptable, >10% indicates issues

Compare to previous runs in: plan/performance/mactcp/{MACHINE}/latency_*.log

Report:
- Performance vs expectations
- Trend analysis (improving/degrading)
- Specific code areas to investigate if issues found
```

## Throughput Test Prompt

```
Analyze throughput test results in {LOG_FILE}.

Extract:
1. KB/s per buffer size
2. Message counts
3. Send errors or drops
4. Duration of each test phase

Expected patterns for MacTCP on {MACHINE}:
- 256B buffers: 15-30 KB/s typical
- 1024B buffers: 30-60 KB/s typical
- 4096B buffers: 50-100 KB/s typical (may see more loss)

Factors affecting throughput:
- Network congestion (check peer logs)
- MacTCP buffer management (check for TCPRcvBfrReturn calls)
- CPU load (68k is slow for large buffers)

Compare to previous runs and note:
- Throughput trends
- Optimal buffer size for this machine
- Any regression from previous tests
```

## Stress Test Prompt

```
Analyze stress test results in {LOG_FILE}.

Extract:
1. Total cycles attempted
2. Success/failure count
3. Memory delta (start vs end FreeMem)
4. Any specific failure patterns

Pass criteria:
- >90% cycle success rate
- Memory delta < 4KB (no significant leak)
- No crashes or hangs

If failures detected:
- Note which cycle numbers failed
- Check for patterns (e.g., fails after N cycles)
- Correlate with memory readings

Memory leak detection:
- Delta > 4KB per 10 cycles = concerning
- Delta > 16KB total = likely leak
- Suggest checking: NewPtr/DisposePtr balance, handle cleanup
```

## Discovery Test Prompt

```
Analyze discovery test results in {LOG_FILE}.

Extract:
1. Total discovery events
2. Unique peers found
3. Lost events (dropped broadcasts)
4. Discovery rate (events/minute)

Expected patterns:
- POSIX partner broadcasts every 5 seconds
- Should see 12+ discoveries per minute
- Lost events should be <5%

If low discovery rate:
- Check UDP binding (port 7353)
- Check broadcast reach (same subnet?)
- Verify partner is actually broadcasting

If high lost events:
- Mac may be too slow processing
- UDP buffer overflow
- Check for CPU-intensive operations during discovery
```

## Comparison Analysis Prompt

```
Compare test results across multiple runs.

Current run: {CURRENT_LOG}
Previous runs: {PREVIOUS_LOGS}

Analysis:
1. Extract metrics from all runs
2. Compute averages and trends
3. Identify regressions (>10% worse than average)
4. Identify improvements (>10% better than average)

Report format:

## Trend Analysis

| Metric | Previous Avg | Current | Change |
|--------|--------------|---------|--------|
| ...    | ...          | ...     | +/-X%  |

## Notable Changes
[Any significant regressions or improvements]

## Confidence
[How many data points, statistical significance]

## Recommendations
[Based on trend direction]
```

## Error Investigation Prompt

```
Investigate errors found in {LOG_FILE}.

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
- mactcp: {MACTCP_LOG}
- discovery: {DISCOVERY_LOG}
- latency: {LATENCY_LOG}
- throughput: {THROUGHPUT_LOG}
- stress: {STRESS_LOG}

Create executive summary:

## Overall Status
[PASS/FAIL with brief explanation]

## Key Metrics
| Test | Status | Key Result |
|------|--------|------------|
| mactcp | PASS/FAIL | Basic connectivity |
| discovery | PASS/FAIL | X discoveries/min |
| latency | PASS/FAIL | Xms avg RTT |
| throughput | PASS/FAIL | X KB/s peak |
| stress | PASS/FAIL | X% success |

## Critical Issues
[Any failures or concerns requiring immediate attention]

## Recommendations
[Prioritized list of improvements]

## Next Steps
[Suggested follow-up testing or investigation]
```
