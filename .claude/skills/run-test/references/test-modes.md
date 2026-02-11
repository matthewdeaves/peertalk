# Test-to-Partner Mode Mapping

Maps test app names to the required perf_partner mode and expected duration.

## Mode Reference

| Test App | Partner Mode | Duration | Description |
|----------|--------------|----------|-------------|
| test_latency | echo | 2-3 min | RTT measurement with various message sizes |
| test_throughput | echo | 2-3 min | Bidirectional throughput measurement |
| test_stress | stress | ~5 min | Rapid connect/disconnect cycles |
| test_discovery | echo | 2 min | Extended discovery packet counting |
| test_mactcp | echo | 60 sec | Basic connectivity test |

**IMPORTANT:** Throughput uses **echo mode** (not stream). The Mac test sends messages
and measures both SEND and RECV rates. Echo mode returns messages for proper RECV
measurement. Stream mode gives RECV=0 because the partner sends its own data instead
of echoing.

## Partner Mode Details

### Echo Mode (--mode echo)

Used for: **latency, throughput, discovery, mactcp**

Behavior:
- Echoes all received messages back unchanged
- Preserves timestamp for RTT calculation
- Uses retry queue for backpressure handling
- No sleep in fast mode for minimal latency

Command:
```bash
./build/bin/perf_partner --mode echo --verbose
```

### Stream Mode (--mode stream)

Used for: **NOT USED by current tests** (reserved for future one-way streaming tests)

Behavior:
- Streams data continuously to connected peer
- Does NOT echo received messages
- Configurable message size (--size)
- Reports KB/s when complete

Command:
```bash
./build/bin/perf_partner --mode stream --size 4096 --verbose
```

**WARNING:** Using stream mode with test_throughput gives RECV=0 because the partner
doesn't echo messages back.

### Stress Mode (--mode stress)

Used for: stress

Behavior:
- Accepts rapid connections
- ACKs messages immediately
- Tracks connect/disconnect cycles

Command:
```bash
./build/bin/perf_partner --mode stress --verbose
```

## Binary Paths

### Standard Builds (8MB+ machines)

```
build/mac/test_latency.bin
build/mac/test_throughput.bin
build/mac/test_stress.bin
build/mac/test_discovery.bin
build/mac/test_mactcp.bin
```

### Lowmem Builds (4MB machines)

```
build/mac/test_latency_lowmem.bin
build/mac/test_throughput_lowmem.bin
build/mac/test_stress_lowmem.bin
build/mac/test_discovery_lowmem.bin
build/mac/test_mactcp_lowmem.bin
```

## Test Sequence for "all"

Run in this order (by complexity):

1. mactcp (60s) - Validates basic connectivity first
2. discovery (2min) - Tests UDP broadcast
3. latency (3min) - Measures RTT
4. throughput (3min) - Bidirectional throughput
5. stress (5min) - Tests stability last

Switch partner mode between tests as needed:
- mactcp → echo
- discovery → echo (same mode, no restart)
- latency → echo (same mode, no restart)
- throughput → echo (same mode, no restart)
- stress → stress (restart partner)

## Mode Detection

To detect the current partner mode, check startup logs:

```bash
docker logs perf-partner 2>&1 | grep -E "^Mode:"
```

Output: `Mode: echo` or `Mode: stream` or `Mode: stress`

**Always verify mode before running a test.** Wrong mode causes:
- Echo mode for stress test → connection state issues
- Stream mode for throughput → RECV=0 (no echoes)
- Stress mode for latency → incorrect timing data

## Port Usage

All tests use:
- Discovery: UDP 7353
- TCP: 7354

Partner must bind to these ports. Use `--network host` in Docker.
