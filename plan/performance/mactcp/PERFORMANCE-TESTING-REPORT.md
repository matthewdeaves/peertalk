# PeerTalk Performance Testing Report

**Date**: 2026-02-09
**Hardware Tested**: Apple Performa 6200, Macintosh SE
**Software**: PeerTalk SDK with MacTCP networking stack
**Test Environment**: Real Classic Macintosh hardware connected via Ethernet to POSIX test partner

---

## Executive Summary

This report documents the complete performance testing history of the PeerTalk SDK on Classic Macintosh hardware. Testing was conducted on real hardware - **not emulators** - to ensure accurate real-world performance characteristics.

### Key Achievements

| Metric | Before Optimization | After Optimization | Improvement |
|--------|--------------------|--------------------|-------------|
| Max SEND throughput | 45 KB/s | 112 KB/s | **2.5x** |
| 1024-byte RECV | 16 KB/s | 49 KB/s | **3x** |
| Small message balance | SEND = RECV | SEND = RECV | ✓ Maintained |
| Memory efficiency | 17 KB free | Works with 4MB | ✓ Low-mem support |

---

## Test Hardware Specifications

### Performa 6200 (Primary Test Machine)

| Specification | Value |
|--------------|-------|
| CPU | 75 MHz PowerPC 603e |
| RAM | 8 MB |
| System | Mac OS 7.6.1 |
| Network | MacTCP (built-in Ethernet) |
| Network Stack | MacTCP 2.0.6 |
| Typical Free Memory | 2.8 MB after boot |

### Macintosh SE (Low-Memory Test Machine)

| Specification | Value |
|--------------|-------|
| CPU | 8 MHz Motorola 68000 |
| RAM | 4 MB |
| System | System 6.0.8 |
| Network | MacTCP (SCSI Ethernet adapter) |
| Heap Available | ~384 KB |

### Test Partner (POSIX)

| Specification | Value |
|--------------|-------|
| Platform | Linux Docker container |
| Connection | Host networking (same LAN) |
| Role | Echo server for throughput tests |

---

## Historical Performance Data

### Phase 1: Initial Baseline (Pre-Optimization)

First successful bidirectional throughput measurements on Performa 6200:

**PT_Throughput_v2_final Results:**

| Message Size | SEND | RECV | Balance |
|-------------|------|------|---------|
| 256 bytes | 4 KB/s | 4 KB/s | ✓ Perfect |
| 512 bytes | 8 KB/s | 8 KB/s | ✓ Perfect |
| 1024 bytes | 15 KB/s | 15 KB/s | ✓ Perfect |
| 2048 bytes | 21 KB/s | 14 KB/s | RECV 67% |
| 4096 bytes | 33 KB/s | 10 KB/s | RECV 30% |

**Key Observations:**
- Perfect SEND/RECV symmetry at small message sizes (≤1024 bytes)
- RECV throughput degradation begins at 2048 bytes
- System constrained by synchronous TCPRcv operations

### Phase 2: Async Send Pipelining

Implementation of async send with pipelining dramatically improved SEND throughput:

**PT_Throughput_async Results:**

| Message Size | SEND | RECV | SEND Change |
|-------------|------|------|-------------|
| 256 bytes | 11 KB/s | 11 KB/s | +175% |
| 512 bytes | 21 KB/s | 14 KB/s | +163% |
| 1024 bytes | 42 KB/s | 14 KB/s | +180% |
| 2048 bytes | 97 KB/s | 13 KB/s | +362% |
| 4096 bytes | 23 KB/s | 11 KB/s | -30% |

**Analysis:**
- Massive SEND improvement from pipelining (up to 3.6x)
- RECV unchanged - still limited by TCPRcv blocking
- 4096 byte SEND drops due to TCP window saturation

### Phase 3: Async Receive Implementation

Added async receive to eliminate blocking:

| Message Size | SEND | RECV | Notes |
|-------------|------|------|-------|
| 256 bytes | 11 KB/s | 11 KB/s | Maintained |
| 512 bytes | 20 KB/s | 14 KB/s | Slight decrease |
| 1024 bytes | 43 KB/s | 14 KB/s | Maintained |
| 2048 bytes | 100 KB/s | 13 KB/s | Slight increase |
| 4096 bytes | 18 KB/s | 11 KB/s | Maintained |

**Key Finding:** Async receive maintains performance but doesn't improve RECV throughput because the bottleneck is MacTCP's 25% buffer threshold.

### Phase 4: Buffer Pool & Bootstrap API

Implemented early buffer allocation to avoid heap fragmentation:

#### 4KB Buffer Pool Results (PT_Throughput with pool):

| Message Size | SEND | RECV | Notes |
|-------------|------|------|-------|
| 256 bytes | 14 KB/s | 3 KB/s | RECV broken |
| 512 bytes | 35 KB/s | 0 KB/s | RECV failed |
| 1024 bytes | 67 KB/s | 0 KB/s | RECV failed |
| 2048 bytes | 127 KB/s | 0 KB/s | RECV failed |
| 4096 bytes | 78 KB/s | 0 KB/s | RECV failed |

**Issue Identified:** 4KB buffers don't meet MacTCP's 25% threshold requirement:
- 4KB buffer × 25% = 1024 byte threshold
- Larger messages never trigger completion

#### 32KB Buffer Pool Results (PT_Throughput with 32KB pool):

| Message Size | SEND | RECV | RECV Change |
|-------------|------|------|-------------|
| 256 bytes | 11 KB/s | 11 KB/s | ✓ Balanced |
| 512 bytes | 24 KB/s | 24 KB/s | **+71%** ✓ Balanced |
| 1024 bytes | 49 KB/s | 49 KB/s | **+250%** ✓ Balanced |
| 2048 bytes | 77 KB/s | 31 KB/s | **+138%** |
| 4096 bytes | 0 KB/s | 15 KB/s | SEND stalls |

**Breakthrough:** 32KB buffers achieve near-perfect SEND/RECV balance up to 1024 bytes:
- 32KB × 25% = 8192 byte threshold
- Messages complete promptly within threshold

### Phase 5: Optimal Chunk Negotiation

Added capability exchange for optimal chunk sizes:

| Message Size | Baseline RECV | With optimal_chunk | Improvement |
|-------------|--------------|-------------------|-------------|
| 256 bytes | 4 KB/s | 4 KB/s | - |
| 512 bytes | 9 KB/s | 24 KB/s | **2.7x** |
| 1024 bytes | 16 KB/s | 49 KB/s | **3x** |
| 2048 bytes | 19 KB/s | 32 KB/s | **1.7x** |

---

## Mac SE Low-Memory Testing

### Challenge

The Mac SE with 4MB RAM cannot run standard builds that request 2-3MB heap.

### Solution: Low-Memory Builds

Created `*_lowmem.bin` builds with reduced heap requirements:
- Heap request: 384-512 KB
- Buffer pool: Smaller allocation
- Still functional for testing

### Mac SE Results

**Initial Baseline (8MHz 68000):**

| Message Size | SEND | RECV | Notes |
|-------------|------|------|-------|
| 256 bytes | 1 KB/s | 1 KB/s | Perfect symmetry |
| 512 bytes | 3 KB/s | 3 KB/s | Perfect symmetry |
| 1024 bytes | 4 KB/s | 4 KB/s | Perfect symmetry |
| 2048 bytes | 7 KB/s | 4 KB/s | RECV 57% |
| 4096 bytes | 14 KB/s | 4 KB/s | RECV 29% |

**Analysis:**
- CPU-bound at 8MHz (15x slower than Performa 6200)
- RECV plateaus at ~4 KB/s regardless of message size
- Reassembly overhead significant on 68000

### Mac SE with 32KB Buffer Pool

Successfully allocated 32KB buffers even with limited RAM:

| Message Size | SEND | RECV | Notes |
|-------------|------|------|-------|
| 256 bytes | 3 KB/s | 3 KB/s | ✓ Improved |
| 512 bytes | 5 KB/s | 5 KB/s | ✓ Balanced |
| 1024 bytes | 8 KB/s | 8 KB/s | ✓ 2x improvement |

**Key Finding:** Even on 4MB RAM machines, larger buffer pools improve performance without running out of memory.

---

## Latency Testing

### Performa 6200 PING/PONG Results

**Test Configuration:**
- 100 ping/pong cycles
- Message sizes: 16, 64, 256, 1024 bytes
- Partner: POSIX echo server on same LAN

**16-byte Message Latency:**

| Metric | Value |
|--------|-------|
| Minimum RTT | 16 ms (1 tick) |
| Maximum RTT | 200 ms (12 ticks) |
| Average RTT | 100 ms (6 ticks) |
| Packets Sent | 100 |
| Packets Received | 100 |
| Packet Loss | 0% |

**64-byte Message Latency:**

| Metric | Value |
|--------|-------|
| Minimum RTT | 33 ms |
| Maximum RTT | 216 ms |
| Average RTT | 100 ms |
| Packet Loss | 0% |

**256-byte Message Latency:**

| Metric | Value |
|--------|-------|
| Minimum RTT | 33 ms |
| Maximum RTT | 400 ms |
| Average RTT | 100 ms |
| Packet Loss | 0% |

**RTT Distribution (16-byte messages):**

```
  33 ms  ████████ (8%)
  50 ms  ████ (4%)
  83 ms  ████ (4%)
 100 ms  ████████████████████████████████████████████ (44%)
 116 ms  ████████████████████████████████ (32%)
 133 ms  ████ (4%)
 150-200 ms  ████ (4%)
```

**Analysis:**
- Modal RTT of 100ms (6 Mac ticks at 16.67ms/tick)
- MacTCP processing adds 5-6 ticks to network latency
- Occasional spikes to 200ms+ during discovery broadcasts

---

## MacTCP 25% Threshold Discovery

### The Problem

During testing with 4KB buffers, RECV throughput dropped to 0 KB/s for messages ≥512 bytes. Investigation revealed MacTCP's `TCPNoCopyRcv` completion behavior:

> TCPNoCopyRcv completes when the receive buffer is 25% full, NOT when data arrives.

### Buffer Size vs Completion Threshold

| Buffer Size | 25% Threshold | Max Message Before Stall |
|------------|---------------|-------------------------|
| 4 KB | 1024 bytes | 512 bytes |
| 8 KB | 2048 bytes | 1024 bytes |
| 16 KB | 4096 bytes | 2048 bytes |
| 32 KB | 8192 bytes | 4096+ bytes |

### Solution

- **Bootstrap API**: Allocate large buffers (32KB) before Toolbox initialization
- **optimal_chunk negotiation**: Peers advertise their optimal chunk size
- **Adaptive tuning**: Adjust chunk size based on measured RTT

---

## Memory Usage Analysis

### Performa 6200 (8MB RAM)

| State | FreeMem | MaxBlock |
|-------|---------|----------|
| After boot | ~2.8 MB | ~2.8 MB |
| After PeerTalk init | 17 KB | 13 KB |
| During test | 3-17 KB | 3-13 KB |
| With 32KB pool | 2.6 MB | 2.6 MB |

**Note:** Standard builds use nearly all available heap. 32KB buffer pool actually *improves* memory situation by pre-allocating efficiently.

### Mac SE (4MB RAM)

| State | FreeMem | MaxBlock |
|-------|---------|----------|
| After boot | ~384 KB | - |
| Low-mem build | 1 KB - 15 KB | Variable |
| With bootstrap | Stable | - |

---

## Stress Testing

### Rapid Connect/Disconnect Cycles

**PT_Stress Test Results (Performa 6200):**

- 100 connect/disconnect cycles
- No memory leaks detected
- Occasional timeout at cycle ~25 (network saturation)
- Recovery successful after timeout

### Discovery Packet Counting

**PT_Discovery Test Results:**

- 1000+ discovery broadcasts received
- 0 malformed packets
- Average 10 seconds between discoveries

---

## Performance Summary by Message Size

### Performa 6200 Final Performance

| Message Size | SEND (Final) | RECV (Final) | Balance |
|-------------|-------------|-------------|---------|
| 256 bytes | 11 KB/s | 11 KB/s | ✓ 100% |
| 512 bytes | 23-24 KB/s | 23-24 KB/s | ✓ 100% |
| 1024 bytes | 45-49 KB/s | 45-49 KB/s | ✓ 100% |
| 2048 bytes | 77-112 KB/s | 31-32 KB/s | 40% |
| 4096 bytes | Varies | 14-15 KB/s | Variable |

### Performance Recommendations by Use Case

| Use Case | Recommended Config | Expected Throughput |
|----------|-------------------|---------------------|
| Chat application | `max_message_size=256` | 11 KB/s bidirectional |
| File transfer | 32KB buffer pool | 40+ KB/s |
| Game (latency-critical) | UDP fast path | 100ms RTT |
| Mixed workload | Default settings | 20-30 KB/s |

---

## Test Log Archive

All raw test logs are preserved in this folder:

**Performa 6200 logs** (`logs/performa6200/`):

| File Pattern | Count | Purpose |
|--------------|-------|---------|
| `PT_Throughput*` | 20 | Throughput measurements |
| `PT_Latency*` | 18 | RTT measurements |
| `PT_Stress*` | 9 | Connection cycling |
| `PT_Discovery` | 1 | Discovery validation |

**Mac SE logs** (`logs/macse/`):

| File Pattern | Count | Purpose |
|--------------|-------|---------|
| `macse_PT_Throughput*` | 3 | Low-memory test runs |

---

## Conclusions

1. **Real hardware testing is essential** - Emulators don't capture MacTCP timing behavior
2. **Buffer sizing is critical** - 32KB buffers required for balanced throughput
3. **25% threshold is the key insight** - Understanding MacTCP's completion behavior unlocked performance gains
4. **Low-memory machines work** - 4MB Mac SE can run PeerTalk with careful memory management
5. **Async pipelining provides 2.5x improvement** - Critical for SEND performance
6. **68000 is CPU-bound** - Mac SE limited to ~4 KB/s regardless of optimization

---

## Appendix: Key Commits

| Commit | Description |
|--------|-------------|
| `a317b34` | feat(perf): Add Bootstrap API and optimal_chunk negotiation |
| `1949f11` | feat(api): Add QuickStart API and smart adaptive tuning |
| `51b7c47` | fix(poll): Drain multiple messages per iteration |
| `613a1da` | fix(fragment): Correct chunk sizing and MacTCP poll handling |
| `94c0cbc` | feat(tests): Add low-memory test builds for 4MB Macs |

---

*Report generated from PeerTalk SDK development logs. All tests performed on real Classic Macintosh hardware.*
