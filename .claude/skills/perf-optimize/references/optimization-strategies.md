# Optimization Strategies Reference

Specific code changes for each optimization strategy.

## Strategy 1: Adjust Fragmentation Threshold

**Target:** `src/core/queue.h`

**Current code:**
```c
#define PT_PRESSURE_FRAG_THRESHOLD 75   /* Fragment large msgs when peer under pressure */
#define PT_PRESSURE_REDUCED_MAX    2048 /* Reduced max when fragmenting under pressure */
```

**Adjustment options:**

| Value | Effect | Use When |
|-------|--------|----------|
| 70 | More aggressive fragmentation | High packet loss at large sizes |
| 75 | Default (conservative) | Baseline |
| 80 | Less fragmentation | Good network, need throughput |
| 85 | Moderate | Proven stable, want more throughput |
| 90 | Aggressive | Very reliable network |

**Example edit:**
```c
// Raise from 75 to 85
#define PT_PRESSURE_FRAG_THRESHOLD 85
```

**Verification:**
- Run throughput test at 4096B
- Compare to previous run
- Watch for increased packet loss (bad sign)

---

## Strategy 2: Adjust Rate Limits

**Target:** `src/core/peer.c` - `pt_peer_update_adaptive_params()`

**Current code (around line 880-890):**
```c
/* Adjust rate limit based on peer pressure */
if (peer->cold.caps.buffer_pressure >= PT_PRESSURE_HIGH) {
    peer->cold.caps.rate_limit_bytes_per_sec = 50 * 1024;  /* 50 KB/s */
} else if (peer->cold.caps.buffer_pressure >= PT_PRESSURE_MEDIUM) {
    peer->cold.caps.rate_limit_bytes_per_sec = 100 * 1024; /* 100 KB/s */
} else {
    peer->cold.caps.rate_limit_bytes_per_sec = 0;  /* Unlimited */
}
```

**Adjustment options:**

| Pressure Level | Conservative | Moderate | Aggressive |
|----------------|--------------|----------|------------|
| HIGH (85+)     | 50 KB/s      | 75 KB/s  | 100 KB/s   |
| MEDIUM (50+)   | 100 KB/s     | 150 KB/s | 200 KB/s   |
| LOW (<50)      | Unlimited    | Unlimited| Unlimited  |

**Example edit (moderate increase):**
```c
if (peer->cold.caps.buffer_pressure >= PT_PRESSURE_HIGH) {
    peer->cold.caps.rate_limit_bytes_per_sec = 75 * 1024;  /* 75 KB/s */
} else if (peer->cold.caps.buffer_pressure >= PT_PRESSURE_MEDIUM) {
    peer->cold.caps.rate_limit_bytes_per_sec = 150 * 1024; /* 150 KB/s */
}
```

**Verification:**
- Run throughput test
- Check if throughput improved
- Watch for increased RECV < SEND (peer overwhelmed)

---

## Strategy 3: Adjust Adaptive Window

**Target:** `src/core/peer.c` - `pt_peer_update_adaptive_params()`

**Current code (around line 865-875):**
```c
/* Adapt flow window based on RTT */
uint16_t new_window;
if (rtt < 50) {
    new_window = 6;   /* Low latency = larger window */
} else if (rtt < 100) {
    new_window = 4;   /* Normal */
} else if (rtt < 200) {
    new_window = 3;   /* Moderate latency */
} else {
    new_window = 2;   /* High latency */
}
```

**Adjustment options:**

For better utilization (increase window):
```c
if (rtt < 50) {
    new_window = 8;   /* More aggressive */
} else if (rtt < 100) {
    new_window = 6;   /* Larger normal */
}
```

For stability (decrease window):
```c
if (rtt < 50) {
    new_window = 4;   /* More conservative */
} else if (rtt < 100) {
    new_window = 3;   /* Smaller normal */
}
```

**Verification:**
- Run latency test
- Check RTT variance (should decrease with smaller window)
- Run throughput test (should increase with larger window)

---

## Strategy 4: Adjust Pressure Thresholds

**Target:** `include/peertalk.h` (config defaults) and `src/core/queue.h`

**Current constants in queue.h:**
```c
#define PT_PRESSURE_MEDIUM    50  /* Light throttle starts */
#define PT_PRESSURE_HIGH      85  /* Heavy throttle starts */
#define PT_PRESSURE_CRITICAL  95  /* Blocking starts */
```

**Current config fields in peertalk.h:**
```c
uint8_t         pressure_medium;        /* 0 = 50 */
uint8_t         pressure_high;          /* 0 = 85 */
uint8_t         pressure_critical;      /* 0 = 95 */
uint8_t         pressure_frag;          /* 0 = 75 */
```

**Adjustment options:**

For less throttling (higher thresholds):
```c
#define PT_PRESSURE_MEDIUM    60
#define PT_PRESSURE_HIGH      90
#define PT_PRESSURE_CRITICAL  98
```

For more throttling (lower thresholds):
```c
#define PT_PRESSURE_MEDIUM    40
#define PT_PRESSURE_HIGH      75
#define PT_PRESSURE_CRITICAL  90
```

**Verification:**
- Run throughput test
- Higher thresholds = more throughput but risk of overflow
- Lower thresholds = less throughput but more stable

---

## Strategy 5: Adjust Token Bucket Parameters

**Target:** `src/core/peer.c` - `pt_peer_check_rate_limit()`

**Current refill calculation:**
```c
tokens_to_add = (elapsed * peer->cold.caps.rate_limit_bytes_per_sec) / 1000;
```

**Adjustment: Bucket size (burst capacity)**

In pt_internal.h, rate_bucket_max controls burst size.
Currently set equal to rate_limit_bytes_per_sec (1 second of tokens).

For larger bursts:
```c
peer->cold.caps.rate_bucket_max = peer->cold.caps.rate_limit_bytes_per_sec * 2;
```

For smaller bursts (more even pacing):
```c
peer->cold.caps.rate_bucket_max = peer->cold.caps.rate_limit_bytes_per_sec / 2;
```

**Verification:**
- Larger bucket = burstier, potentially better throughput
- Smaller bucket = smoother, less likely to overwhelm peer

---

## Strategy 6: Reduce Fragmentation Overhead

**Target:** `src/core/send.c` - `pt_send_fragmented()`

If fragmentation is necessary but causing overhead:

**Option A: Increase fragment size**
```c
// Currently fragments to PT_PRESSURE_REDUCED_MAX (2048)
// Could increase to 3072 for less overhead:
#define PT_PRESSURE_REDUCED_MAX 3072
```

**Option B: Use larger initial fragments**
```c
// Start with larger fragments, reduce only if needed
uint16_t frag_size = min(frag_max, length / 2);
if (frag_size < 1024) frag_size = 1024;  // Minimum useful fragment
```

**Verification:**
- Check fragment count in logs
- Fewer fragments = less overhead
- But too large fragments may hit pressure again

---

## Strategy 7: Optimize ACK Handling

**Target:** Send acknowledgment configuration

If high RTT variance due to ACK delays:

**Reduce ACK delay:**
```c
// If there's an ACK_DELAY configuration
#define PT_ACK_DELAY_MS 50  // Faster ACKs

// Or in adaptive params:
peer->ack_delay_ms = rtt / 4;  // Quarter of RTT
```

**Coalesce fewer ACKs:**
```c
// ACK every N messages instead of every M
#define PT_ACK_COALESCE_COUNT 2  // ACK every 2 instead of 4
```

**Verification:**
- Run latency test
- Check RTT variance (max vs avg)
- Lower variance = better ACK tuning

---

## Change Documentation Template

For each change made, log:

```markdown
## Change: [Strategy Name]

**Date:** YYYY-MM-DD HH:MM:SS
**Cycle:** N
**File:** path/to/file.c

**Before:**
```c
[exact code before change]
```

**After:**
```c
[exact code after change]
```

**Rationale:**
[Why this change was made]

**Expected Impact:**
[What improvement is expected]

**Actual Result:**
- Metric X: before → after (±N%)
- Metric Y: before → after (±N%)

**Verdict:** IMPROVEMENT / NEUTRAL / REGRESSION
```

---

## Rollback Templates

For each strategy, the rollback is simply reversing the edit:

```
Use Edit tool with:
- old_string = new code
- new_string = original code
```

Always verify tests pass after rollback:
```bash
docker run --rm -u "$(id -u):$(id -g)" -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest make test
```
