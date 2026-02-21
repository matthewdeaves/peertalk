# Structured Metrics Output

When `/run-test` completes, **ALWAYS** output structured metrics for automation.

## Metrics Summary Block

After the human-readable summary table, output a structured metrics block:

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
  "metrics": {
    "send_kbps": {"256": 16, "512": 33, "1024": 57, "2048": 85, "4096": 23},
    "recv_kbps": {"256": 16, "512": 33, "1024": 57, "2048": 85, "4096": 23},
    "peak_send_kbps": 85,
    "peak_recv_kbps": 85,
    "optimal_chunk": 2048,
    "errors": 0
  },
  "memory": {
    "free_mem": 2573632,
    "max_block": 2572144
  },
  "logs": {
    "mac": "plan/performance/mactcp/performa6200/throughput_20260221_143210.log",
    "partner": "plan/performance/mactcp/performa6200/throughput_20260221_143210_partner.log"
  }
}
========================================
```

## Test-Specific Metrics

### Latency Test

Values extracted from Mac log `SIZE` lines.

```json
{
  "test": "latency",
  "metrics": {
    "rtt_min_ms": {"16": 0, "64": 0, "256": 0, "1024": 0, "4096": 16},
    "rtt_avg_ms": {"16": 0, "64": 0, "256": 16, "1024": 16, "4096": 16},
    "rtt_max_ms": {"16": 50, "64": 33, "256": 183, "1024": 316, "4096": 100},
    "sent": {"16": 100, "64": 100, "256": 100, "1024": 100, "4096": 100},
    "recv": {"16": 100, "64": 100, "256": 100, "1024": 100, "4096": 100},
    "lost": {"16": 0, "64": 0, "256": 0, "1024": 0, "4096": 0},
    "total_sent": 500,
    "total_recv": 500,
    "total_lost": 0,
    "loss_pct": 0.0
  }
}
```

### Throughput Test

Values extracted from Mac log `COMPLETE` lines and results section.

```json
{
  "test": "throughput",
  "metrics": {
    "send_kbps": {"256": 16, "512": 33, "1024": 57, "2048": 85, "4096": 23},
    "recv_kbps": {"256": 16, "512": 33, "1024": 57, "2048": 85, "4096": 23},
    "messages": {"256": 2001, "512": 2038, "1024": 1718, "2048": 1286, "4096": 177},
    "errors": {"256": 0, "512": 0, "1024": 0, "2048": 0, "4096": 0},
    "peak_send_kbps": 85,
    "peak_recv_kbps": 85,
    "optimal_chunk": 2048
  },
  "memory": {
    "free_mem": 2573632,
    "max_block": 2572144
  }
}
```

### Stream Test (One-Way)

Values from BOTH Mac log (`SEND COMPLETE`/`RECV COMPLETE`) and partner log (`SINK`/`STREAM phase complete`).

```json
{
  "test": "stream",
  "metrics": {
    "send_kbps": {"256": 34, "512": 98, "1024": 183, "2048": 304, "4096": 429},
    "recv_kbps": {"256": 22, "512": 22, "1024": 44, "2048": 59, "4096": 117},
    "send_msgs": {"256": 4090, "512": 5886, "1024": 5515, "2048": 4568, "4096": 3218},
    "recv_msgs": {"256": 2661, "512": 1330, "1024": 1327, "2048": 886, "4096": 884},
    "peak_send_kbps": 429,
    "peak_recv_kbps": 117
  },
  "partner": {
    "sink_kbps": {"256": 34.0, "512": 98.0, "1024": 183.6, "2048": 304.4, "4096": 429.0},
    "stream_kbps": {"256": 22.2, "512": 22.2, "1024": 44.4, "2048": 59.2, "4096": 117.9},
    "sink_bytes": {"256": 1046272, "512": 3013632, "1024": 5647360, "2048": 9355264, "4096": 13180928},
    "stream_bytes": {"256": 681984, "512": 682496, "1024": 1362944, "2048": 1818624, "4096": 3620864}
  },
  "memory": {
    "free_mem": 2571472,
    "max_block": 2569984
  }
}
```

**Note:** The `partner` section contains POSIX-side measurements from `[STREAM-TEST] SINK phase complete` and `[STREAM-TEST] STREAM phase complete` lines. These are essential for cross-validating Mac-reported throughput.

### Stress Test

Values from Mac log results section.

```json
{
  "test": "stress",
  "metrics": {
    "cycles": 5,
    "successes": 5,
    "failures": 0,
    "success_rate": 100.0,
    "messages_sent": 5
  },
  "memory": {
    "initial_free_mem": 2082400,
    "final_free_mem": 2018832,
    "initial_max_block": 2080112,
    "final_max_block": 2018832,
    "delta_free_mem": -63568,
    "leak_detected": false
  }
}
```

### Discovery Test

Values from Mac log discovery summary section.

```json
{
  "test": "discovery",
  "metrics": {
    "duration_seconds": 120,
    "total_discoveries": 12,
    "unique_peers": 1,
    "time_to_first_ms": 2966,
    "avg_interval_sec": 10.0,
    "rate_per_sec": 0.10,
    "rate_per_min": 6.0,
    "peer_lost_events": 0,
    "peer_recovered": 0
  },
  "peers": [
    {
      "name": "PerfPartner",
      "ip": "10.188.1.59",
      "port": 7354,
      "discovery_count": 12,
      "lost_count": 0,
      "status": "PRESENT"
    }
  ]
}
```

### All Tests Summary

When running all tests, combine individual metrics:

```json
{
  "test": "all",
  "machine": "performa6200",
  "platform": "mactcp",
  "timestamp": "2026-02-21T14:45:18Z",
  "overall_status": "pass",
  "tests_passed": 5,
  "tests_failed": 0,
  "metrics": {
    "latency": { "total_sent": 500, "total_recv": 500, "loss_pct": 0.0 },
    "throughput": { "peak_send_kbps": 85, "optimal_chunk": 2048, "errors": 0 },
    "stream": { "peak_send_kbps": 429, "peak_recv_kbps": 117 },
    "stress": { "success_rate": 100.0, "leak_detected": false },
    "discovery": { "total_discoveries": 12, "peer_lost_events": 0 }
  },
  "logs": {
    "latency_mac": "...", "latency_partner": "...",
    "throughput_mac": "...", "throughput_partner": "...",
    "stream_mac": "...", "stream_partner": "...",
    "stress_mac": "...", "stress_partner": "...",
    "discovery_mac": "...", "discovery_partner": "..."
  }
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
| Memory delta (FreeMem) | <100KB | 100-500KB | >500KB |
| Throughput vs baseline | >80% | 50-80% | <50% |

## Integration with /perf-optimize

The `/perf-optimize` skill reads these metrics blocks to:
1. Establish baselines
2. Compare before/after
3. Determine improvement percentage
4. Track trends across optimization cycles

**Always include the JSON block even when providing human-readable analysis.**

## Parsing the Metrics Block

To find the JSON metrics in output:

```
Look for: "METRICS (JSON)"
Extract everything between the opening { and closing } on the following lines
Parse as JSON
```
