# Bottleneck Patterns Reference

Decision tree for identifying performance bottlenecks and selecting fixes.

## Pattern 1: Large Message Throughput Cliff

**Symptoms:**
- 4096B throughput << 2048B throughput (>50% drop)
- 4096B RECV = SEND (not a one-way issue)
- No errors reported

**Root Cause:** Pressure-triggered fragmentation threshold too aggressive

**Evidence:**
```
2048 bytes: SEND   94 KB/s  RECV   94 KB/s
4096 bytes: SEND    9 KB/s  RECV    8 KB/s  ← 90% drop!
```

**Analysis:**
At 4096B, peer buffer fills faster → hits pressure threshold → triggers fragmentation.
But fragmentation overhead (headers, reassembly) negates size benefit.

**Fix Strategy: Raise Fragmentation Threshold**

```c
// src/core/queue.h
// Current:
#define PT_PRESSURE_FRAG_THRESHOLD 75

// Options to try:
#define PT_PRESSURE_FRAG_THRESHOLD 80  // Conservative
#define PT_PRESSURE_FRAG_THRESHOLD 85  // Moderate
#define PT_PRESSURE_FRAG_THRESHOLD 90  // Aggressive
```

**Expected Impact:** +200-500% at 4096B, slight regression at 2048B possible

**Risk:** Higher threshold means more buffer pressure before fragmentation kicks in.
If too high, may cause buffer overflow → packet loss.

---

## Pattern 2: Rate Limiting Choking Throughput

**Symptoms:**
- Throughput plateaus at specific value (e.g., exactly 50 KB/s or 100 KB/s)
- Affects multiple buffer sizes equally
- Peer pressure reports are high (>50%)

**Root Cause:** Token bucket rate limiting too aggressive

**Evidence:**
```
Throughput consistently ~50 KB/s regardless of buffer size
Peer pressure: 60-70% during test
```

**Analysis:**
Rate limiter engaging due to peer pressure feedback.
Token bucket refill rate is the bottleneck, not network or CPU.

**Fix Strategy: Increase Rate Limit Under Pressure**

```c
// src/core/peer.c - pt_peer_update_adaptive_params()
// Current:
if (peer->cold.caps.buffer_pressure >= PT_PRESSURE_HIGH) {
    peer->cold.caps.rate_limit_bytes_per_sec = 50 * 1024;  /* 50 KB/s */
} else if (peer->cold.caps.buffer_pressure >= PT_PRESSURE_MEDIUM) {
    peer->cold.caps.rate_limit_bytes_per_sec = 100 * 1024; /* 100 KB/s */
}

// Options to try:
// For HIGH pressure:
peer->cold.caps.rate_limit_bytes_per_sec = 75 * 1024;   // +50%
peer->cold.caps.rate_limit_bytes_per_sec = 100 * 1024;  // +100%

// For MEDIUM pressure:
peer->cold.caps.rate_limit_bytes_per_sec = 150 * 1024;  // +50%
peer->cold.caps.rate_limit_bytes_per_sec = 200 * 1024;  // +100%
```

**Expected Impact:** +20-50% throughput at affected sizes

**Risk:** Higher rate can overwhelm slow peer. Monitor for increased packet loss.

---

## Pattern 3: High RTT Variance

**Symptoms:**
- Max RTT >> Avg RTT (3x or more)
- Affects larger message sizes more
- Packet loss correlates with RTT spikes

**Root Cause:** Retransmits due to aggressive timeout or lost ACKs

**Evidence:**
```
SIZE 1024B: min=25ms avg=42ms max=125ms  ← max is 3x avg
```

**Analysis:**
TCP retransmit timer too aggressive → premature retransmits → duplicate data.
Or: ACKs getting lost → sender times out → retransmit.

**Fix Strategy: Increase ACK Timeout or Coalesce ACKs**

```c
// src/core/protocol.h (if exists) or peer.c
// Increase base timeout:
#define PT_ACK_TIMEOUT_MS 200  // Current: 100

// Or adjust adaptive timeout multiplier:
timeout = base_rtt * 2.5;  // Current: * 2
```

**Expected Impact:** -20% RTT variance, slight +latency increase

**Risk:** Longer timeouts mean slower recovery from actual packet loss.

---

## Pattern 4: RECV << SEND Asymmetry

**Symptoms:**
- RECV significantly lower than SEND (>20% difference)
- Happens at specific buffer sizes
- No explicit errors in log

**Root Cause:** Flow control / backpressure mismatch

**Evidence:**
```
4096 bytes: SEND   45 KB/s  RECV   12 KB/s  ← 3.5x difference
```

**Analysis:**
Mac is sending faster than POSIX partner can echo back.
Or: Echo path has different bottleneck than send path.

**Fix Strategy: Improve Flow Window or Reduce Send Burst**

```c
// src/core/peer.c - adaptive window
// Current adaptive window caps at 4-6
// Reduce for high-latency links:
if (rtt > 100) {
    new_window = 2;  // More conservative
}

// Or add send pacing:
// After each send, add small delay
```

**Expected Impact:** Better SEND/RECV balance, possibly lower peak SEND

**Risk:** May reduce overall throughput in favor of balance.

---

## Pattern 5: Memory Growth in Stress Test

**Symptoms:**
- memory_delta > 4KB after stress test
- Grows proportionally with cycle count
- No crashes but concerning trend

**Root Cause:** Memory leak in connect/disconnect path

**Evidence:**
```
Cycles: 50/50 passed (100.0%)
Memory: start=2784032 end=2760000 delta=-24032  ← 24KB lost!
```

**Analysis:**
Each connect/disconnect cycle leaks memory.
Likely cause: forgetting to free a buffer or return a handle.

**Fix Strategy: Audit Allocation/Free Pairs**

1. Search for NewPtr/DisposePtr pairs
2. Search for malloc/free pairs
3. Check pt_peer_disconnect() cleanup
4. Check error paths for missing cleanup

```c
// Common fix locations:
// src/core/peer.c - pt_peer_disconnect()
// src/core/context.c - pt_context_free()
// src/mactcp/stream.c - stream cleanup
```

**Expected Impact:** Stable memory after stress test

**Risk:** None if properly tested.

---

## Pattern 6: Discovery Rate Too Low

**Symptoms:**
- <10 discoveries/minute (expected 12+)
- Lost events > 10%
- Works on POSIX but not Mac

**Root Cause:** UDP buffer overflow or slow processing

**Evidence:**
```
Rate: 8.5/min
Lost events: 15%
```

**Analysis:**
Mac can't process discovery packets fast enough.
UDP packets dropped before application can read them.

**Fix Strategy: Reduce Discovery Packet Size or Rate**

```c
// Discovery packet configuration
#define PT_DISCOVERY_INTERVAL_MS 5000  // Current: 5000
// Try: 7000 or 10000 to reduce load

// Or reduce discovery payload size
// Check if we're sending unnecessary data
```

**Expected Impact:** Lower lost events, more consistent discovery rate

**Risk:** Slower peer discovery, may affect UX.

---

## Pattern 7: Stream vs Throughput Gap Too Small

**Symptoms:**
- Stream unidirectional ~= Throughput bidirectional
- Expected: Stream should be 50-100% higher
- Echo overhead not visible

**Root Cause:** Measurement or protocol inefficiency

**Evidence:**
```
Throughput peak: 94 KB/s
Stream peak: 100 KB/s  ← only 6% higher
```

**Analysis:**
Stream should remove echo overhead → should be significantly faster.
If not, something else is bottleneck (rate limiting, fragmentation).

**Fix Strategy: Check Other Bottlenecks First**

This pattern usually indicates another bottleneck is masking the improvement.
Address rate limiting and fragmentation issues first, then re-test.

---

## Decision Tree

```
START
  │
  ▼
Is throughput cliff at large sizes?
  YES → Pattern 1: Fragmentation Threshold
  NO  ▼

Does throughput plateau at specific value?
  YES → Pattern 2: Rate Limiting
  NO  ▼

Is RTT max >> avg (>2x)?
  YES → Pattern 3: ACK Timeout
  NO  ▼

Is RECV << SEND (>20% difference)?
  YES → Pattern 4: Flow Control
  NO  ▼

Is memory growing in stress test?
  YES → Pattern 5: Memory Leak
  NO  ▼

Is discovery rate low (<10/min)?
  YES → Pattern 6: UDP Processing
  NO  ▼

Is stream ~= throughput?
  YES → Pattern 7: Check other patterns first
  NO  ▼

Performance is likely near optimal for this hardware.
Consider:
- Hardware limitations (68k CPU, 10Mbps Ethernet)
- MacTCP stack overhead
- Protocol design limits
```

## Priority Order

When multiple bottlenecks are present, fix in this order:

1. **Memory leaks** (Pattern 5) - Stability first
2. **Rate limiting** (Pattern 2) - Most common artificial limit
3. **Fragmentation threshold** (Pattern 1) - Big gains for large messages
4. **Flow control** (Pattern 4) - Balance send/receive
5. **ACK timeout** (Pattern 3) - Reduce variance
6. **Discovery** (Pattern 6) - Less critical for throughput focus

## Confidence Levels

Each fix has a confidence level based on evidence strength:

| Evidence | Confidence | Action |
|----------|------------|--------|
| Clear pattern match | High | Implement fix |
| Partial match | Medium | Try fix, watch closely |
| Weak indicators | Low | Investigate more first |
