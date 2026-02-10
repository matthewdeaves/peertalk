# Test Log Management Rules

Rules for handling performance test logs from Classic Mac hardware.

## Canonical Log Location

**All performance logs should end up in:**
```
plan/performance/{platform}/{machine}/
```

Example structure:
```
plan/performance/
  mactcp/
    performa6200/
      latency_20260210_120000.log
      throughput_20260210_130000.log
    macse/
      latency_20260210_140000.log
```

These directories are committed to git for historical tracking.

## Log Naming Convention

Pattern: `{test}_{YYYYMMDD}_{HHMMSS}.log`

| Test App | Log Pattern |
|----------|-------------|
| test_latency | latency_YYYYMMDD_HHMMSS.log |
| test_throughput | throughput_YYYYMMDD_HHMMSS.log |
| test_stress | stress_YYYYMMDD_HHMMSS.log |
| test_discovery | discovery_YYYYMMDD_HHMMSS.log |
| test_mactcp | mactcp_YYYYMMDD_HHMMSS.log |

## Staging Directories (Gitignored)

| Directory | Purpose |
|-----------|---------|
| `downloads/` | Raw FTP downloads from Mac |
| `logs/` | Temporary log staging |

These are gitignored and cleaned regularly.

## Log Collection Methods

### Method 1: Automatic Streaming (Preferred)

Mac test apps stream logs to the POSIX perf_partner at test completion.

1. Start perf_partner with `--verbose`
2. Run test on Mac
3. Logs are automatically saved to `plan/performance/mactcp/{machine}/`

The test name is extracted from log content (e.g., "PeerTalk Latency Test" → "latency").

### Method 2: FTP Download (Fallback)

For machines with FTP, you can download logs directly:

```bash
# Via MCP
mcp__classic-mac-hardware__download_file(
    machine="performa6200",
    remote_path="PT_Latency",
    local_path="downloads/performa6200/PT_Latency"
)

# Then move to canonical location with proper name
mv downloads/performa6200/PT_Latency \
   plan/performance/mactcp/performa6200/latency_$(date +%Y%m%d_%H%M%S).log
```

### Method 3: perf_partner Container Logs

For LaunchAPPL-only machines, logs are streamed to perf_partner:

```bash
# Capture from container
docker logs perf-partner 2>&1 | tee plan/performance/mactcp/macse/latency_$(date +%Y%m%d_%H%M%S).log
```

## Machine Registry

The perf_partner uses environment variable `MACHINE_REGISTRY` to map IPs to machine names:

```bash
export MACHINE_REGISTRY="10.188.1.55:macse,10.188.1.213:performa6200"
```

If not set, defaults to known machines (macse, performa6200).

## Cleanup Workflow

### Regular Cleanup

```bash
# Clean staging directories
rm -rf downloads/* logs/*

# Or use cleanup script
./tools/cleanup-project.sh --all --force
```

### Mac Cleanup

```bash
# Remove old log files from Mac
mcp__classic-mac-hardware__cleanup_machine(machine="performa6200", scope="logs")
```

## Log Content Markers

All test apps log these markers for easy parsing:

| Marker | Meaning |
|--------|---------|
| `"======== PeerTalk <Name> Test ========"` | Test started |
| `"======== <TEST> RESULTS ========"` | Results section |
| `"TEST EXITING - cleaning up..."` | Test completed |

## Best Practices

1. **Always use proper naming** - Files should be `{test}_{YYYYMMDD}_{HHMMSS}.log`
2. **Commit performance logs** - They provide historical tracking
3. **Clean staging regularly** - `downloads/` and `logs/` are temporary
4. **One test at a time** - LaunchAPPL tests bind ports, can't run parallel
5. **Check log markers** - Verify test completed before moving logs
