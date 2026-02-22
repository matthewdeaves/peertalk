# End-of-Test Summary Tables

After each test completes and logs are collected, display a formatted summary table.
These tables combine data from BOTH the Mac test app log and the POSIX partner log.

**CRITICAL: Always display these tables after every test run. This is NOT optional.**

## Data Sources

Each test produces two logs:

| Log | Source | Contains |
|-----|--------|----------|
| Mac log | `plan/performance/mactcp/<machine>/<test>_YYYYMMDD_HHMMSS.log` | Test results, RTT/KB/s, memory, verdict |
| Partner log | `plan/performance/mactcp/<machine>/<test>_YYYYMMDD_HHMMSS_partner.log` | Echo counts, stream phase KB/s, errors, connection events |

Extract metrics from BOTH logs to populate the tables below.

## Latency Test

```
LATENCY TEST -- <Machine> (<Platform>) -- <Date> <Time>
================================================================
 Size   |  Min   |  Avg   |  Max   | Sent | Recv | Loss
--------|--------|--------|--------|------|------|------
   16 B |   0 ms |   0 ms |  50 ms |  100 |  100 |  0.0%
   64 B |   0 ms |   0 ms |  33 ms |  100 |  100 |  0.0%
  256 B |   0 ms |  16 ms | 183 ms |  100 |  100 |  0.0%
 1024 B |   0 ms |  16 ms | 316 ms |  100 |  100 |  0.0%
 4096 B |  16 ms |  16 ms | 100 ms |  100 |  100 |  0.0%
--------|--------|--------|--------|------|------|------
 TOTAL  |        |        |        |  500 |  500 |  0.0%

Verdict: PASS
```

**Data extraction from Mac log:**
```
grep "^SIZE " <mac_log>
→ SIZE 16: min=0 max=50 avg=0 ms (sent=100 recv=100 lost=0)
```

**Partner log cross-reference:**
- Check for `[MESSAGE]` count — should match total sent
- Check for any error lines

## Throughput Test

```
THROUGHPUT TEST -- <Machine> (<Platform>) -- <Date> <Time>
================================================================
 Size   | SEND (KB/s) | RECV (KB/s) | Messages | Errors
--------|-------------|-------------|----------|-------
  256 B |          16 |          16 |    2,001 |      0
  512 B |          33 |          33 |    2,038 |      0
 1024 B |          57 |          57 |    1,718 |      0
 2048 B |          85 |          85 |    1,286 |      0
 4096 B |          23 |          23 |      177 |      0
--------|-------------|-------------|----------|-------
 Peak   |          85 |          85 |          |

Verdict: PASS
Memory: FreeMem=2,573,632  MaxBlock=2,572,144
```

**Data extraction from Mac log:**
```
grep "^COMPLETE " <mac_log>
→ COMPLETE 1024 bytes: SEND=57 KB/s (1718 msgs) RECV=57 KB/s (1718 msgs)
```
And from the results section:
```
grep "bytes: SEND" <mac_log> (in RESULTS section)
→ 1024 bytes: SEND   57 KB/s  RECV   57 KB/s  (errs=0)
```

**Partner log cross-reference:**
- Check `[MESSAGE]` lines for echo count per size
- Any `error` or `would_block` lines indicate backpressure

## Stream Test (One-Way)

```
STREAM TEST -- <Machine> (<Platform>) -- <Date> <Time>
================================================================
 Size   | Mac->POSIX (SEND) | POSIX->Mac (RECV) | SEND/RECV
--------|-------------------|-------------------|----------
  256 B |         34 KB/s   |         22 KB/s   |     1.5x
  512 B |         98 KB/s   |         22 KB/s   |     4.5x
 1024 B |        183 KB/s   |         44 KB/s   |     4.2x
 2048 B |        304 KB/s   |         59 KB/s   |     5.2x
 4096 B |        429 KB/s   |        117 KB/s   |     3.7x
--------|-------------------|-------------------|----------
 Peak   |        429 KB/s   |        117 KB/s   |

Partner Perspective (POSIX-side measurements):
 Size   | SINK (Mac->POSIX) | STREAM (POSIX->Mac) | Bytes
--------|-------------------|---------------------|------------
  256 B |       34.0 KB/s   |        22.2 KB/s    |   1,046 KB
  512 B |       98.0 KB/s   |        22.2 KB/s    |   3,014 KB
 1024 B |      183.6 KB/s   |        44.4 KB/s    |   5,647 KB
 2048 B |      304.4 KB/s   |        59.2 KB/s    |   9,355 KB
 4096 B |      429.0 KB/s   |       117.9 KB/s    |  13,181 KB

Verdict: PASS
Memory: FreeMem=2,571,472  MaxBlock=2,569,984
```

**Data extraction from Mac log:**
```
grep "^SEND COMPLETE\|^RECV COMPLETE" <mac_log>
→ SEND COMPLETE 1024 bytes: 183 KB/s (5515 msgs, 0 errors)
→ RECV COMPLETE 1024 bytes: 44 KB/s (1327 msgs)
```
And from the results section:
```
grep "bytes: SEND" <mac_log> (in RESULTS section)
→ 1024 bytes: SEND  183 KB/s  RECV   44 KB/s
```

**Partner log cross-reference (essential for stream test):**
```
grep "SINK phase complete\|STREAM phase complete" <partner_log>
→ [STREAM-TEST] SINK phase complete: 5647360 bytes in 30.03s = 183.6 KB/s
→ [STREAM-TEST] STREAM phase complete: 1362944 bytes in 29.98s = 44.4 KB/s
```
The partner log gives exact byte counts and POSIX-side throughput for comparison.

## Stress Test

```
STRESS TEST -- <Machine> (<Platform>) -- <Date> <Time>
================================================================
 Metric              | Value
---------------------|-------------------
 Cycles              | 5 / 5 (100%)
 Successes           | 5
 Failures            | 0
 Messages Sent       | 5
 Initial FreeMem     | 2,082,400
 Final FreeMem       | 2,018,832
 Memory Delta        | -63,568
 Leak Detected       | No

Verdict: PASS
```

**Data extraction from Mac log:**
```
grep "Connection Cycles:\|Successes:\|Failures:\|Success rate:\|Messages:" <mac_log>
grep "Initial FreeMem:\|Final FreeMem:" <mac_log>
```

**Partner log cross-reference:**
- Check connection/disconnection event counts match cycle count
- Any error events during rapid connect/disconnect

## Discovery Test

```
DISCOVERY TEST -- <Machine> (<Platform>) -- <Date> <Time>
================================================================
 Metric                | Value
-----------------------|-------------------
 Duration              | 120 sec
 Total Discoveries     | 12
 Unique Peers          | 1
 Time to First         | 2,966 ms
 Avg Interval          | 10.0 sec
 Rate                  | 0.10/sec (6.0/min)
 Peer Lost Events      | 0
 Peer Recovered        | 0

Peers:
 Name           | IP              | Seen | Lost | Status
----------------|-----------------|------|------|--------
 PerfPartner    | 10.188.1.59     |   12 |    0 | PRESENT

Verdict: PASS
```

**Data extraction from Mac log:**
```
grep "Total discoveries:\|Unique peers:\|Time to first:\|Peer lost:\|Peer recovered:" <mac_log>
grep "Discovery count:\|Lost count:" <mac_log>
```

**Partner log cross-reference:**
- Check discovery broadcast count from partner side
- Any "peer timeout" or "lost" events

## All Tests Summary

When running all tests, display individual tables for each test, then a combined summary:

```
ALL TESTS SUMMARY -- <Machine> (<Platform>) -- <Date>
================================================================
 Test       | Verdict | Key Metric           | Log Size
------------|---------|----------------------|---------
 Latency    | PASS    | 0-16ms avg, 0% loss  |   31 KB
 Throughput | PASS    | 85 KB/s peak (2048B) |    4 KB
 Stream     | PASS    | 429 KB/s SEND peak   |    9 KB
 Stress     | PASS    | 5/5 cycles, no leaks |    2 KB
 Discovery  | PASS    | 12 in 120s, 0 lost   |    2 KB
------------|---------|----------------------|---------
 Overall    | PASS    | 5/5 tests passed     |
```

## Comparison to Previous Run

When a previous log exists for the same test, append a comparison:

```
vs Previous Run (<previous_date>):
 Metric            | Previous | Current | Change
-------------------|----------|---------|--------
 Peak Throughput   |  80 KB/s |  85 KB/s| +6.3%
 Avg Latency 1024B |  18 ms   |  16 ms  | -11.1%
 Packet Loss       |  0.2%    |  0.0%   | improved

Trend: STABLE (within normal variance)
```

## Formatting Rules

1. **Always right-align numbers** in table columns
2. **Use commas** for numbers > 999 (e.g., 2,001 not 2001)
3. **Include units** in column headers, not in data cells
4. **Show the verdict** prominently at the end
5. **Include memory stats** when available (throughput, stream tests)
6. **Include the partner perspective** for stream tests (essential for cross-validation)
7. **Round KB/s to integers** — sub-KB/s precision is noise on these machines
8. **Show the log file paths** at the very end so the user can find them
