# Test Partner Skill

Manage POSIX test partner containers for hardware testing. Uses named containers that won't be accidentally killed by cleanup commands.

## Usage

```
/test-partner start [mode]   # Start partner (echo|stream|stress)
/test-partner stop           # Stop partner
/test-partner status         # Check if running
/test-partner logs           # Show recent logs
```

## How It Works

### Starting a Partner

1. Check if the partner container is already running
2. If not, start with the specified mode (default: echo)
3. Use a named container `perf-partner` that persists

### Container Management

**CRITICAL:** The partner runs in a named container `perf-partner`.

When you need to stop other Docker containers:
```bash
# CORRECT - stop only anonymous containers, preserve named partner
docker ps -q --filter "name=perf-partner" -q | xargs -r docker stop

# Or to stop EVERYTHING except the partner:
docker ps -q | grep -v $(docker ps -q --filter "name=perf-partner") | xargs -r docker stop
```

**NEVER run `docker stop $(docker ps -q)` while the partner is needed for testing.**

### Commands

**Start partner:**
```bash
# Check if already running
docker ps --filter "name=perf-partner" --format "{{.Names}}" | grep -q perf-partner && echo "Already running"

# Start if not running
docker run -d --name perf-partner --rm --network host \
    -v "$(pwd)":/workspace -w /workspace \
    peertalk-posix:latest ./build/bin/perf_partner --mode echo --verbose
```

**Stop partner:**
```bash
docker stop perf-partner 2>/dev/null || echo "Not running"
```

**Check status:**
```bash
docker ps --filter "name=perf-partner" --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"
```

**View logs:**
```bash
docker logs perf-partner --tail 50
```

**Follow logs:**
```bash
docker logs -f perf-partner
```

## Modes

| Mode | Purpose | Use Case |
|------|---------|----------|
| echo | Echo back received data | Latency testing |
| stream | Continuous data streaming | Throughput testing |
| stress | Rapid connect/disconnect | Stress testing |

## Important Notes

1. **Partner uses host networking** - Required for UDP broadcast discovery
2. **Ports used:** 7353 (discovery), 7354 (TCP), 7355 (UDP)
3. **Named container** - Won't be killed by generic `docker stop $(docker ps -q)`
4. **Auto-remove on stop** - Container cleans up after `docker stop`

## Example Session

```bash
# Start partner for throughput testing
/test-partner start stream

# ... user runs Mac test app ...

# Check if still running
/test-partner status

# View what happened
/test-partner logs

# When done
/test-partner stop
```

## Integration with Other Commands

When running other Docker commands during a test session:

```bash
# SAFE - build in separate container
docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest make test

# DANGEROUS - kills partner!
docker stop $(docker ps -q)

# SAFE alternative - stop only build containers
docker stop $(docker ps -q --filter "name!=perf-partner")
```

ARGUMENTS: $1 = action (start|stop|status|logs), $2 = mode for start (echo|stream|stress)
