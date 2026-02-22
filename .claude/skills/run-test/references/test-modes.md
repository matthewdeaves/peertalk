# Test Apps and Partner Behavior

Reference for test apps, expected durations, and partner auto-detection.

## Partner Auto-Detection

**The perf_partner auto-detects ALL test types.** Start it once — no mode switching needed.

```bash
docker run -d --name perf-partner --network host \
  -u "$(id -u):$(id -g)" -v "$(pwd)":/workspace -w /workspace \
  -e MACHINE_REGISTRY="10.188.1.55:macse,10.188.1.213:performa6200" \
  peertalk-posix:latest ./build/bin/perf_partner --verbose
```

How auto-detection works:
- **Echo mode** is the default — handles latency, throughput, stress, discovery
- **Stream mode** activates automatically when Mac sends STRM control messages
  - `START_SEND` command → partner switches to sink (receives one-way data)
  - `START_RECV` command → partner switches to stream (sends one-way data)
  - `DONE` command → partner switches back to echo mode
- No `--mode` flag needed. No restarts between tests.

## Test Reference

| Test App | Duration | Initial Wait | Description | Partner Behavior |
|----------|----------|-------------|-------------|------------------|
| test_latency | ~2 min | 90s | RTT per message size (16-4096B) | Echoes pings back |
| test_throughput | ~2.5 min | 90s | Bidirectional echo throughput | Echoes data back |
| test_stream | ~6 min | 180s | One-way streaming capacity | Auto-detects STRM: sinks/streams |
| test_stress | ~1 min | 60s | Rapid connect/disconnect cycles | Echoes, handles reconnects |
| test_discovery | ~2 min | 90s | Discovery packet counting | Responds to UDP broadcasts |

## Test Sequence for "all"

Run in this order (connectivity first, then performance, then stability):

1. **latency** (3 min) — RTT measurements across message sizes
2. **throughput** (3 min) — Bidirectional echo throughput
3. **stream** (5-8 min) — One-way streaming (true unidirectional capacity)
4. **stress** (1-2 min) — Connection stability and memory leaks
5. **discovery** (2 min) — Extended discovery observation

No partner restarts needed between tests.

## Binary Paths

### Standard Builds (8MB+ machines)

```
build/mac/test_latency.bin
build/mac/test_throughput.bin
build/mac/test_stream.bin
build/mac/test_stress.bin
build/mac/test_discovery.bin
```

### Lowmem Builds (4MB machines)

```
build/mac/test_latency_lowmem.bin
build/mac/test_throughput_lowmem.bin
build/mac/test_stream_lowmem.bin
build/mac/test_stress_lowmem.bin
build/mac/test_discovery_lowmem.bin
```

## LaunchAPPL Timeout Expectations

| Test | LaunchAPPL Timeout? | Reason |
|------|---------------------|--------|
| test_latency | Yes (60s timeout) | Runs 2-3 min total |
| test_throughput | Yes (60s timeout) | Runs 2-3 min total |
| test_stream | Yes (60s timeout) | Runs 5-8 min total |
| test_stress | Sometimes | 5 cycles ~30-60s, may finish in time |
| test_discovery | Yes (60s timeout) | Fixed 120s duration |

A 60-second LaunchAPPL timeout is **normal and expected** for long-running tests.
The test continues running on the Mac beyond the timeout.

## Port Usage

All tests use:
- Discovery: UDP 7353
- TCP: 7354

Partner must bind to these ports. Use `--network host` in Docker.
**Run only ONE test at a time** — they share the same ports.

## Verifying Partner Status

```bash
# Check if running
docker ps --filter name=perf-partner --format "{{.Names}} {{.Status}}"

# Check startup output
docker logs perf-partner 2>&1 | head -10

# Check recent activity
docker logs --tail 20 perf-partner 2>&1
```
