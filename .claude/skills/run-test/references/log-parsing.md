# Log Parsing Reference

Patterns for detecting test completion and extracting metrics.

## Completion Markers

All test apps log these markers:

| Marker | Meaning | Pattern |
|--------|---------|---------|
| Test Start | App initialized | `"======== PeerTalk <Name> Test ========"` |
| Results Section | Test finished | `"======== <TEST> RESULTS ========"` |
| Test Exit | Cleanup starting | `"TEST EXITING - cleaning up..."` |

### Detection Commands

```bash
# Check if test started
grep -q "PeerTalk.*Test" "$LOG_FILE"

# Check if results available
grep -q "RESULTS" "$LOG_FILE"

# Check if test fully complete
grep -q "TEST EXITING" "$LOG_FILE"
```

## Metrics Extraction Patterns

### Latency Test

Log format:
```
SIZE 256: min=12 max=45 avg=23 ms (sent=100 recv=98 lost=2)
```

Extraction pattern:
```bash
grep "SIZE [0-9]*:" "$LOG_FILE"
```

Regex:
```
SIZE (\d+): min=(\d+) max=(\d+) avg=(\d+) ms \(sent=(\d+) recv=(\d+) lost=(\d+)\)
```

Fields:
- `$1`: Message size (bytes)
- `$2`: Minimum RTT (ms)
- `$3`: Maximum RTT (ms)
- `$4`: Average RTT (ms)
- `$5`: Sent count
- `$6`: Received count
- `$7`: Lost count

### Throughput Test

Log format:
```
THROUGHPUT 1024: 125.3 KB/s (4521 messages in 30 sec)
```

Extraction:
```bash
grep "THROUGHPUT [0-9]*:" "$LOG_FILE"
```

Regex:
```
THROUGHPUT (\d+): ([\d.]+) KB/s \((\d+) messages in (\d+) sec\)
```

Fields:
- `$1`: Buffer size (bytes)
- `$2`: Throughput (KB/s)
- `$3`: Message count
- `$4`: Duration (seconds)

### Stress Test

Log format:
```
STRESS: 50 cycles, 48 success, 2 failed, memory delta: -2048
STRESS TEST: PASSED
```

Extraction:
```bash
grep "STRESS:" "$LOG_FILE"
grep "STRESS TEST:" "$LOG_FILE"
```

Regex:
```
STRESS: (\d+) cycles, (\d+) success, (\d+) failed, memory delta: (-?\d+)
STRESS TEST: (PASSED|FAILED)
```

### Discovery Test

Log format:
```
DISCOVERY: 156 total, 4 unique peers, 2 lost events
DISCOVERY: 78.0 discoveries/min
```

Extraction:
```bash
grep "DISCOVERY:" "$LOG_FILE"
```

Regex:
```
DISCOVERY: (\d+) total, (\d+) unique peers, (\d+) lost
DISCOVERY: ([\d.]+) discoveries/min
```

## Error Patterns

| Error | Pattern | Meaning |
|-------|---------|---------|
| Connection failure | `"Connect failed:"` | TCP connection error |
| Discovery timeout | `"No peer discovered"` | No POSIX partner found |
| Send failure | `"TCPSend.*error"` | Network error during send |
| Memory issue | `"NewPtr failed"` or `"FAILED to allocate"` | Out of memory |

### Critical Errors (Test Failed)

```bash
# Check for critical errors
grep -E "(FAILED|ERROR|crash|panic)" "$LOG_FILE"
```

### Warnings (Test May Have Issues)

```bash
# Check for warnings
grep -E "(WARN|timeout|retry|lost)" "$LOG_FILE"
```

## Log File Naming

Format: `<test>_<YYYYMMDD>_<HHMMSS>.log`

Examples:
```
latency_20260211_143215.log
throughput_20260211_145023.log
stress_20260211_150145.log
```

Location: `plan/performance/mactcp/<machine>/`

## Detecting Test Type from Log

Search for test header:

```bash
if grep -q "Latency Test" "$LOG_FILE"; then
    TEST_TYPE="latency"
elif grep -q "Throughput Test" "$LOG_FILE"; then
    TEST_TYPE="throughput"
elif grep -q "Stress Test" "$LOG_FILE"; then
    TEST_TYPE="stress"
elif grep -q "Discovery Test" "$LOG_FILE"; then
    TEST_TYPE="discovery"
elif grep -q "MacTCP Test" "$LOG_FILE"; then
    TEST_TYPE="mactcp"
fi
```

## Partner Log Markers

The POSIX perf_partner also logs useful markers:

| Marker | Meaning |
|--------|---------|
| `"[LOG] Started receiving logs"` | Mac began log stream |
| `"[LOG] Saved X bytes to"` | Log file written |
| `"[LOG] Received X/Y bytes"` | Streaming progress |
| `"TEST EXITING"` | Test complete (from Mac log) |
