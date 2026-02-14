# Structured Metrics Output

When `/run-test` completes, it should output structured metrics for automation.

## Metrics Summary Block

After analysis, output a structured metrics block that can be parsed:

```
========================================
METRICS (JSON)
========================================
{
  "test": "throughput",
  "machine": "performa6200",
  "platform": "mactcp",
  "timestamp": "2026-02-13T12:34:56Z",
  "duration_seconds": 180,
  "status": "pass",
  "metrics": {
    "send_kbps": {"256": 17, "512": 32, "1024": 61, "2048": 94, "4096": 9},
    "recv_kbps": {"256": 17, "512": 32, "1024": 61, "2048": 94, "4096": 8},
    "peak_kbps": 94,
    "optimal_chunk": 2048,
    "errors": 0
  },
  "logs": {
    "mac": "plan/performance/mactcp/performa6200/throughput_20260213_123456.log",
    "partner": "plan/performance/mactcp/performa6200/throughput_20260213_123456_partner.log"
  }
}
========================================
```

## Test-Specific Metrics

### Latency Test
```json
{
  "test": "latency",
  "metrics": {
    "rtt_min_ms": {"16": 12, "64": 14, "256": 18, "1024": 25, "4096": 45},
    "rtt_avg_ms": {"16": 18, "64": 21, "256": 28, "1024": 42, "4096": 85},
    "rtt_max_ms": {"16": 45, "64": 52, "256": 78, "1024": 125, "4096": 245},
    "packet_loss_pct": {"16": 0, "64": 0, "256": 1, "1024": 2, "4096": 3},
    "total_pings": 500,
    "successful_pings": 485
  }
}
```

### Throughput Test
```json
{
  "test": "throughput",
  "metrics": {
    "send_kbps": {"256": 17, "512": 32, "1024": 61, "2048": 94, "4096": 9},
    "recv_kbps": {"256": 17, "512": 32, "1024": 61, "2048": 94, "4096": 8},
    "peak_kbps": 94,
    "optimal_chunk": 2048,
    "message_counts": {"256": 2103, "512": 1980, "1024": 1837, "2048": 1420, "4096": 68},
    "errors": 0
  }
}
```

### Stream Test
```json
{
  "test": "stream",
  "metrics": {
    "send_kbps_unidirectional": 455.8,
    "recv_kbps_unidirectional": 382.4,
    "peak_kbps": 455.8,
    "bidirectional_ratio": 4.85,
    "total_bytes_sent": 13674000,
    "total_bytes_recv": 11472000
  }
}
```

### Stress Test
```json
{
  "test": "stress",
  "metrics": {
    "cycles_attempted": 50,
    "cycles_passed": 48,
    "success_rate": 96.0,
    "memory_start": 2784032,
    "memory_end": 2782984,
    "memory_delta_bytes": -1048,
    "avg_cycle_time_ms": 5200
  }
}
```

### Discovery Test
```json
{
  "test": "discovery",
  "metrics": {
    "total_discoveries": 24,
    "unique_peers": 1,
    "lost_events": 2,
    "discoveries_per_minute": 12.0,
    "duration_seconds": 120
  }
}
```

### All Tests Summary
```json
{
  "test": "all",
  "metrics": {
    "latency": { ... },
    "throughput": { ... },
    "stream": { ... },
    "stress": { ... },
    "discovery": { ... }
  },
  "overall_status": "pass",
  "tests_passed": 5,
  "tests_failed": 0
}
```

## Status Values

| Status | Meaning |
|--------|---------|
| `pass` | Test completed, metrics within acceptable range |
| `warn` | Test completed, some metrics concerning |
| `fail` | Test failed or metrics outside acceptable range |
| `error` | Test could not complete (crash, timeout, etc.) |

## Acceptable Ranges

For determining pass/warn/fail:

| Metric | Pass | Warn | Fail |
|--------|------|------|------|
| Packet loss | <5% | 5-15% | >15% |
| Success rate | >95% | 80-95% | <80% |
| Memory delta | <4KB | 4-16KB | >16KB |
| Throughput vs baseline | >80% | 50-80% | <50% |

## Integration with /perf-optimize

The `/perf-optimize` skill reads these metrics blocks to:
1. Establish baselines
2. Compare before/after
3. Determine improvement percentage

When outputting metrics, always include the JSON block even if also providing human-readable analysis.

## Parsing the Metrics Block

To extract JSON metrics programmatically:

```bash
# Extract from output
grep -A 100 "METRICS (JSON)" output.txt | \
  sed -n '/^{/,/^}/p' | head -1
```

Or in the skill, read between the markers:
```
Look for: "METRICS (JSON)"
Extract everything between { and } on the following lines
Parse as JSON
```
