# Baseline Parser Reference

Patterns for parsing test logs into structured baseline JSON.

## Log File Locations

```
plan/performance/mactcp/<machine>/
  latency_YYYYMMDD_HHMMSS.log
  throughput_YYYYMMDD_HHMMSS.log
  stream_YYYYMMDD_HHMMSS.log
  stress_YYYYMMDD_HHMMSS.log
  discovery_YYYYMMDD_HHMMSS.log
```

## Latency Log Parsing

Look for the results section:
```
======== LATENCY METRICS ========
```

Extract per-size metrics:
```
Pattern: "SIZE (\d+)B: min=(\d+)ms avg=(\d+)ms max=(\d+)ms loss=(\d+)%"

Example:
SIZE 16B: min=12ms avg=18ms max=45ms loss=0%
SIZE 64B: min=14ms avg=21ms max=52ms loss=0%
SIZE 256B: min=18ms avg=28ms max=78ms loss=1%
SIZE 1024B: min=25ms avg=42ms max=125ms loss=2%
SIZE 4096B: min=45ms avg=85ms max=245ms loss=3%
```

Map to JSON:
```json
{
  "latency": {
    "rtt_min_ms": {"16": 12, "64": 14, "256": 18, "1024": 25, "4096": 45},
    "rtt_avg_ms": {"16": 18, "64": 21, "256": 28, "1024": 42, "4096": 85},
    "rtt_max_ms": {"16": 45, "64": 52, "256": 78, "1024": 125, "4096": 245},
    "packet_loss_pct": {"16": 0, "64": 0, "256": 1, "1024": 2, "4096": 3}
  }
}
```

## Throughput Log Parsing

Look for the results section:
```
======== THROUGHPUT TEST RESULTS ========
```

Extract per-size metrics:
```
Pattern: "(\d+) bytes: SEND\s+(\d+) KB/s\s+RECV\s+(\d+) KB/s"

Example:
 256 bytes: SEND   17 KB/s  RECV   17 KB/s  (errs=0)
 512 bytes: SEND   32 KB/s  RECV   32 KB/s  (errs=0)
1024 bytes: SEND   61 KB/s  RECV   61 KB/s  (errs=0)
2048 bytes: SEND   94 KB/s  RECV   94 KB/s  (errs=0)
4096 bytes: SEND    9 KB/s  RECV    8 KB/s  (errs=0)
```

Map to JSON:
```json
{
  "throughput": {
    "send_kbps": {"256": 17, "512": 32, "1024": 61, "2048": 94, "4096": 9},
    "recv_kbps": {"256": 17, "512": 32, "1024": 61, "2048": 94, "4096": 8},
    "errors": {"256": 0, "512": 0, "1024": 0, "2048": 0, "4096": 0}
  }
}
```

Also extract peak throughput:
```
peak_kbps = max(send_kbps.values())
optimal_chunk = key with max(send_kbps.values())
```

## Stream Log Parsing

Look for the results section:
```
======== STREAM TEST RESULTS ========
```

Extract unidirectional metrics:
```
Pattern: "Mac->POSIX: (\d+\.?\d*) KB/s"
Pattern: "POSIX->Mac: (\d+\.?\d*) KB/s"

Example:
Mac->POSIX: 455.8 KB/s (peak)
POSIX->Mac: 382.4 KB/s (peak)
```

Map to JSON:
```json
{
  "stream": {
    "send_kbps_unidirectional": 455.8,
    "recv_kbps_unidirectional": 382.4,
    "peak_kbps": 455.8
  }
}
```

## Stress Log Parsing

Look for the results section:
```
======== STRESS TEST RESULTS ========
```

Extract stability metrics:
```
Pattern: "Cycles: (\d+)/(\d+) passed \((\d+\.?\d*)%\)"
Pattern: "Memory: start=(\d+) end=(\d+) delta=([+-]?\d+)"

Example:
Cycles: 48/50 passed (96.0%)
Memory: start=2784032 end=2782984 delta=-1048
```

Map to JSON:
```json
{
  "stress": {
    "cycles_attempted": 50,
    "cycles_passed": 48,
    "success_rate": 96.0,
    "memory_start": 2784032,
    "memory_end": 2782984,
    "memory_delta_bytes": -1048
  }
}
```

## Discovery Log Parsing

Look for the results section:
```
======== DISCOVERY METRICS ========
```

Extract discovery metrics:
```
Pattern: "Total discoveries: (\d+)"
Pattern: "Unique peers: (\d+)"
Pattern: "Lost events: (\d+)"
Pattern: "Rate: (\d+\.?\d*)/min"

Example:
Total discoveries: 24
Unique peers: 1
Lost events: 2
Rate: 12.0/min
```

Map to JSON:
```json
{
  "discovery": {
    "total_discoveries": 24,
    "unique_peers": 1,
    "lost_events": 2,
    "discoveries_per_minute": 12.0
  }
}
```

## Complete Baseline JSON

```json
{
  "version": "1.0",
  "timestamp": "2026-02-13T12:34:56Z",
  "machine": "performa6200",
  "platform": "mactcp",
  "sdk_version": "1.0.0",
  "git_commit": "abc123",
  "test_logs": {
    "latency": "latency_20260213_123456.log",
    "throughput": "throughput_20260213_123500.log",
    "stream": "stream_20260213_123530.log",
    "stress": "stress_20260213_123600.log",
    "discovery": "discovery_20260213_123630.log"
  },
  "metrics": {
    "latency": {
      "rtt_min_ms": {"16": 12, "256": 18, "1024": 25, "4096": 45},
      "rtt_avg_ms": {"16": 18, "256": 28, "1024": 42, "4096": 85},
      "rtt_max_ms": {"16": 45, "256": 78, "1024": 125, "4096": 245},
      "packet_loss_pct": {"16": 0, "256": 1, "1024": 2, "4096": 3}
    },
    "throughput": {
      "send_kbps": {"256": 17, "512": 32, "1024": 61, "2048": 94, "4096": 9},
      "recv_kbps": {"256": 17, "512": 32, "1024": 61, "2048": 94, "4096": 8},
      "peak_kbps": 94,
      "optimal_chunk": 2048
    },
    "stream": {
      "send_kbps_unidirectional": 455.8,
      "recv_kbps_unidirectional": 382.4,
      "peak_kbps": 455.8
    },
    "stress": {
      "success_rate": 96.0,
      "memory_delta_bytes": -1048
    },
    "discovery": {
      "discoveries_per_minute": 12.0,
      "lost_events": 2
    }
  }
}
```

## Parsing Implementation

When parsing logs, Claude should:

1. Find most recent log file for each test type:
   ```bash
   ls -t plan/performance/mactcp/<machine>/<test>_*.log | head -1
   ```

2. Read the file using Read tool

3. Extract metrics using the patterns above

4. If a log is missing or metrics can't be parsed, note as `null` in JSON

5. Calculate derived metrics:
   - `peak_kbps` = max throughput
   - `optimal_chunk` = size with best throughput
   - `bidirectional_ratio` = stream peak / throughput peak
