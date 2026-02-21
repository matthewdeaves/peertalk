# Test Partner Skill

Manage POSIX test partner containers for hardware testing. Uses named containers that won't be accidentally killed by cleanup commands.

## Usage

```
/test-partner start          # Start partner (auto-handles ALL test types)
/test-partner stop           # Stop partner
/test-partner status         # Check if running
/test-partner logs           # Show recent logs
```

## How It Works

### Starting a Partner

1. Check if the partner container is already running
2. If not, start in echo mode (handles ALL tests via auto-detection)
3. Use a named container `perf-partner` that persists

**Key Insight:** The partner now auto-detects test type from Mac control messages:
- Regular messages → echoed back (works for latency AND throughput tests)
- STRM control messages → auto-switch to one-way streaming mode
- No manual mode switching required!

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

**Start partner (recommended - no mode needed):**
```bash
# Check if already running
docker ps --filter "name=perf-partner" --format "{{.Names}}" | grep -q perf-partner && echo "Already running"

# Start if not running - echo mode auto-detects all test types
docker run -d --name perf-partner --network host \
    -u "$(id -u):$(id -g)" -v "$(pwd)":/workspace -w /workspace \
    -e MACHINE_REGISTRY="10.188.1.55:macse,10.188.1.213:performa6200" \
    peertalk-posix:latest ./build/bin/perf_partner --verbose
```

**Stop partner:**
```bash
docker stop perf-partner && docker rm perf-partner 2>/dev/null || echo "Not running"
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

## Modes (mostly automatic now)

| Mode | Purpose | When Used |
|------|---------|-----------|
| echo (default) | Universal partner | ALL tests (auto-detects stream test commands) |
| stress | Stress testing | Only if Mac sends stress-specific protocol |
| stream | Legacy streaming | Deprecated - use echo mode instead |

**IMPORTANT:** Just use the default (no `--mode` flag). The partner auto-detects:
- Latency tests → echoes messages back
- Throughput tests → echoes messages back
- Stream tests → detects STRM magic, switches to sink/stream mode
- Discovery tests → responds to discovery, counts packets

## Important Notes

1. **Partner uses host networking** - Required for UDP broadcast discovery
2. **Ports used:** 7353 (discovery), 7354 (TCP), 7355 (UDP)
3. **Named container** - Won't be killed by generic `docker stop $(docker ps -q)`
4. **Auto-remove on stop** - Container cleans up after `docker stop`

## Example Session

```bash
# Start partner (handles ALL test types automatically)
/test-partner start

# ... user runs ANY Mac test app (latency, throughput, stream, etc.) ...

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
docker run --rm -u "$(id -u):$(id -g)" -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest make test

# DANGEROUS - kills partner!
docker stop $(docker ps -q)

# SAFE alternative - stop only build containers
docker stop $(docker ps -q --filter "name!=perf-partner")
```

ARGUMENTS: $1 = action (start|stop|status|logs), $2 = mode for start (echo|stream|stress)

## Related Skills & Scripts

| Task | Command |
|------|---------|
| Build test apps | `./scripts/build-mac-tests.sh mactcp` |
| Deploy to Mac | MCP `upload_file` tool |
| Fetch logs | `/fetch-logs <machine>` |
| Start partner | `/test-partner start echo` |

## Complete Testing Workflow

```bash
# 1. Build test apps
./scripts/build-mac-tests.sh mactcp

# 2. Start POSIX partner
/test-partner start echo

# 3. Deploy to Mac (via MCP)
mcp__classic-mac-hardware__upload_file(machine="performa6200",
    local_path="build/mac/test_throughput.bin", remote_path="test_throughput.bin")

# 4. Run test on Mac (manual step)

# 5. Fetch logs
/fetch-logs performa6200

# 6. Stop partner
/test-partner stop
```
