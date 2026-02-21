# Log Parsing Reference

Patterns for detecting test completion and extracting metrics from BOTH Mac and partner logs.

## Log Files

Each test produces two log files:

| Log | Location | Source |
|-----|----------|--------|
| Mac log | `plan/performance/mactcp/<machine>/<test>_YYYYMMDD_HHMMSS.log` | Streamed from Mac test app |
| Partner log | `plan/performance/mactcp/<machine>/<test>_YYYYMMDD_HHMMSS_partner.log` | Saved from `docker logs perf-partner` |

## Completion Markers

### Mac Log Markers

| Marker | Meaning | Pattern |
|--------|---------|---------|
| Test Start | App initialized | `"PeerTalk <Name> Test"` |
| Results Section | Test finished | `"<TEST> RESULTS"` or `"<TEST> TEST RESULTS"` |
| Log Streaming | Logs being sent | `"Streaming X bytes of logs to partner..."` |

### Partner Log Markers

| Marker | Meaning | Pattern |
|--------|---------|---------|
| Log received | Mac log auto-saved | `"[LOG] Saved X bytes to <path>"` |
| Stream phase | Stream test phases | `"[STREAM-TEST] SINK phase complete"` / `"STREAM phase complete"` |
| Metrics parsed | Auto-extracted metrics | `"========== <test> METRICS =========="` |

### Detection Commands

```bash
# Check if Mac test completed (results section present)
grep -q "RESULTS" "$MAC_LOG"

# Check if log was received by partner
grep -q "[LOG] Saved" "$PARTNER_LOG"

# Find latest Mac log for a test
ls -t plan/performance/mactcp/<machine>/<test>_*.log | grep -v _partner | head -1

# Find latest partner log
ls -t plan/performance/mactcp/<machine>/<test>_*_partner.log | head -1
```

## Mac Log Metrics Extraction

### Latency Test

**Structured metrics section:**
```
SIZE 16: min=0 max=50 avg=0 ms (sent=100 recv=100 lost=0)
SIZE 64: min=0 max=33 avg=0 ms (sent=100 recv=100 lost=0)
SIZE 256: min=0 max=183 avg=16 ms (sent=100 recv=100 lost=0)
SIZE 1024: min=0 max=316 avg=16 ms (sent=100 recv=100 lost=0)
SIZE 4096: min=16 max=100 avg=16 ms (sent=100 recv=100 lost=0)
```

Extraction:
```bash
grep "^SIZE " "$MAC_LOG"
```

Regex:
```
SIZE (\d+): min=(\d+) max=(\d+) avg=(\d+) ms \(sent=(\d+) recv=(\d+) lost=(\d+)\)
```

Fields: `$1`=size, `$2`=min_ms, `$3`=max_ms, `$4`=avg_ms, `$5`=sent, `$6`=recv, `$7`=lost

**Human-readable table (also in log):**
```
Size   Min  Avg  Max  Sent Recv Lost
----  ---- ---- ---- ---- ---- ----
  16B    0    0   50  100  100    0
```

### Throughput Test

**Per-phase completion lines:**
```
COMPLETE 1024 bytes: SEND=57 KB/s (1718 msgs) RECV=57 KB/s (1718 msgs)
```

Extraction:
```bash
grep "^COMPLETE " "$MAC_LOG"
```

Regex:
```
COMPLETE (\d+) bytes: SEND=(\d+) KB/s \((\d+) msgs\) RECV=(\d+) KB/s \((\d+) msgs\)
```

Fields: `$1`=size, `$2`=send_kbps, `$3`=send_msgs, `$4`=recv_kbps, `$5`=recv_msgs

**Results section summary:**
```
 256 bytes: SEND   16 KB/s  RECV   16 KB/s  (errs=0)
1024 bytes: SEND   57 KB/s  RECV   57 KB/s  (errs=0)
```

Extraction:
```bash
grep "bytes: SEND" "$MAC_LOG"
```

Regex:
```
\s*(\d+) bytes: SEND\s+(\d+) KB/s\s+RECV\s+(\d+) KB/s\s+\(errs=(\d+)\)
```

Fields: `$1`=size, `$2`=send_kbps, `$3`=recv_kbps, `$4`=errors

**Memory (end of log):**
```
Memory: FreeMem=2573632 MaxBlock=2572144
```

### Stream Test (One-Way)

**Per-phase completion lines:**
```
SEND COMPLETE 1024 bytes: 183 KB/s (5515 msgs, 0 errors)
RECV COMPLETE 1024 bytes: 44 KB/s (1327 msgs)
```

Extraction:
```bash
grep "COMPLETE.*bytes:" "$MAC_LOG"
```

SEND regex:
```
SEND COMPLETE (\d+) bytes: (\d+) KB/s \((\d+) msgs, (\d+) errors\)
```

RECV regex:
```
RECV COMPLETE (\d+) bytes: (\d+) KB/s \((\d+) msgs\)
```

**Results section summary:**
```
 256 bytes: SEND   34 KB/s  RECV   22 KB/s
1024 bytes: SEND  183 KB/s  RECV   44 KB/s
```

Extraction:
```bash
# In RESULTS section (after "STREAMING TEST RESULTS")
grep "bytes: SEND" "$MAC_LOG"
```

Regex:
```
\s*(\d+) bytes: SEND\s+(\d+) KB/s\s+RECV\s+(\d+) KB/s
```

Fields: `$1`=size, `$2`=send_kbps, `$3`=recv_kbps

**Memory (end of log):**
```
Memory: FreeMem=2571472 MaxBlock=2569984
```

### Stress Test

**Results section:**
```
Connection Cycles: 5
  Successes: 5
  Failures: 0
  Disconnects: 5
  Success rate: 100%
Messages: sent=5 recv=0
```

Extraction:
```bash
grep "Connection Cycles:\|Successes:\|Failures:\|Success rate:\|Messages:" "$MAC_LOG"
```

**Memory section:**
```
Memory Analysis:
  Initial MaxBlock: 2080112
  Final MaxBlock:   2018832 (+0)
  Min MaxBlock:     2018832
  Initial FreeMem:  2082400
  Final FreeMem:    2018832 (+0)
  Min FreeMem:      2018832
```

Extraction:
```bash
grep "Initial FreeMem:\|Final FreeMem:\|Initial MaxBlock:\|Final MaxBlock:" "$MAC_LOG"
```

**Verdict:**
```
VERDICT: PASS - No significant memory leaks detected
```

### Discovery Test

**Results section:**
```
Discovery Summary:
  Total discoveries: 12
  Unique peers found: 1
  Time to first discovery: 2966 ms
  Peer lost events: 0
  Peer recovered: 0
```

Extraction:
```bash
grep "Total discoveries:\|Unique peers:\|Time to first:\|Peer lost:\|Peer recovered:" "$MAC_LOG"
```

**Per-peer details:**
```
Per-Peer Details:
  PerfPartner (10.188.1.59:7354):
    Discovery count: 12 (avg 10.0 sec between)
    Lost count: 0, Currently: PRESENT
```

**Verdict:**
```
VERDICT: PASS - Stable discovery, no peer timeouts
```

## Partner Log Metrics Extraction

### Stream Test (POSIX-side measurements)

The partner log contains throughput measured from the POSIX side — essential for cross-validation.

**SINK phases (Mac→POSIX):**
```
[STREAM-TEST] SINK phase complete: 5647360 bytes in 30.03s = 183.6 KB/s
```

**STREAM phases (POSIX→Mac):**
```
[STREAM-TEST] STREAM phase complete: 1362944 bytes in 29.98s = 44.4 KB/s
```

Extraction:
```bash
grep "SINK phase complete\|STREAM phase complete" "$PARTNER_LOG"
```

SINK regex:
```
SINK phase complete: (\d+) bytes in ([\d.]+)s = ([\d.]+) KB/s
```

STREAM regex:
```
STREAM phase complete: (\d+) bytes in ([\d.]+)s = ([\d.]+) KB/s
```

Fields: `$1`=total_bytes, `$2`=duration_sec, `$3`=kbps

### Log Save Confirmation

```
[LOG] Saved 8743 bytes to plan/performance/mactcp/performa6200/stream_20260221_144111.log
```

Extraction:
```bash
grep "\[LOG\] Saved" "$PARTNER_LOG"
```

### General Message Activity

```bash
# Count messages processed during test
grep -c "\[MESSAGE\]" "$PARTNER_LOG"

# Check for errors
grep -i "error\|failed\|refused" "$PARTNER_LOG"
```

## Error Patterns

| Error | Pattern | Meaning |
|-------|---------|---------|
| Connection failure | `"Connect failed:"` | TCP connection error |
| Discovery timeout | `"No peer discovered"` | No POSIX partner found |
| Send failure | `"TCPSend.*error"` or `"errs=[1-9]"` | Network error during send |
| Memory issue | `"NewPtr failed"` or `"FAILED to allocate"` | Out of memory |
| Stream failure | `"STRM.*error"` | Stream control protocol error |
| Log stream failure | No `"[LOG] Saved"` in partner log | Mac disconnected before streaming |

### Critical Errors

```bash
# In Mac log
grep -iE "(FAILED|ERROR|crash|panic|VERDICT: FAIL)" "$MAC_LOG"

# In partner log
grep -iE "(error|failed|refused|timeout)" "$PARTNER_LOG"
```

## Log File Naming

| File | Pattern | Example |
|------|---------|---------|
| Mac log | `<test>_YYYYMMDD_HHMMSS.log` | `latency_20260221_142714.log` |
| Partner log | `<test>_YYYYMMDD_HHMMSS_partner.log` | `latency_20260221_142714_partner.log` |

Location: `plan/performance/mactcp/<machine>/`

## Detecting Test Type from Log Content

```bash
if grep -q "Latency Test" "$LOG_FILE"; then
    TEST_TYPE="latency"
elif grep -q "Throughput Test" "$LOG_FILE"; then
    TEST_TYPE="throughput"
elif grep -q "One-Way Stream Test\|Stream Test" "$LOG_FILE"; then
    TEST_TYPE="stream"
elif grep -q "Stress Test" "$LOG_FILE"; then
    TEST_TYPE="stress"
elif grep -q "Discovery Test" "$LOG_FILE"; then
    TEST_TYPE="discovery"
elif grep -q "MacTCP Test" "$LOG_FILE"; then
    TEST_TYPE="mactcp"
fi
```
