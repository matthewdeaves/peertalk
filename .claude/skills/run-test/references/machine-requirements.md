# Machine Build Requirements

Reference for machine-specific build requirements based on RAM.

## Build Types

| Build Type | Heap Request | Target Machines | Binary Suffix |
|------------|--------------|-----------------|---------------|
| standard | 2-3MB | 8MB+ RAM | `.bin` |
| lowmem | 384-512KB | 4MB RAM | `_lowmem.bin` |

## Known Machines

| Machine ID | RAM | Build Type | Platform |
|------------|-----|------------|----------|
| performa6200 | 8MB | standard | mactcp |
| macse | 4MB | lowmem | mactcp |

## Determining Build Type

Check machine's `build` field in machines.json:

```json
{
  "macse": {
    "name": "Mac SE",
    "platform": "mactcp",
    "build": "lowmem",
    ...
  },
  "performa6200": {
    "name": "Performa 6200",
    "platform": "mactcp",
    ...  // no "build" field = standard
  }
}
```

Rule:
- If `build == "lowmem"` → use lowmem builds
- Otherwise (missing or "standard") → use standard builds

## Build Commands

### Standard Builds

```bash
# Build all perf test apps (standard heap)
./scripts/build-mac-tests.sh mactcp perf
```

Output:
```
build/mac/test_latency.bin
build/mac/test_throughput.bin
build/mac/test_stream.bin
build/mac/test_stress.bin
build/mac/test_discovery.bin
```

### Lowmem Builds

```bash
# Build all perf test apps (lowmem heap for 4MB Macs)
./scripts/build-mac-tests.sh mactcp lowmem
```

Output:
```
build/mac/test_latency_lowmem.bin
build/mac/test_throughput_lowmem.bin
build/mac/test_stream_lowmem.bin
build/mac/test_stress_lowmem.bin
build/mac/test_discovery_lowmem.bin
```

## Why This Matters

Mac SE (4MB RAM) breakdown:
- System: ~1.5MB
- Screen buffer: ~175KB
- Available: ~2.3MB
- Finder/DA: ~500KB
- **Free for apps: ~1.8MB**

Standard builds request 2-3MB heap → **WON'T LAUNCH**

Lowmem builds request 384-512KB → Works fine

## Binary Selection Logic

```python
def get_binary_path(test_name, machine_config):
    base = f"build/mac/test_{test_name}"

    if machine_config.get("build") == "lowmem":
        return f"{base}_lowmem.bin"
    else:
        return f"{base}.bin"
```

## Verification

Before deploying to Mac SE:
1. Check file exists: `ls build/mac/test_*_lowmem.bin`
2. Check file size: Should be similar to standard (not accidentally empty)
3. If missing: Run lowmem build command above

## Adding New Machines

When adding a machine to machines.json:

1. Check total RAM in "About This Macintosh"
2. If RAM <= 4MB, add `"build": "lowmem"`
3. If RAM > 4MB, omit build field (defaults to standard)

Example:
```json
{
  "classic_ii": {
    "name": "Classic II",
    "platform": "mactcp",
    "system": "7.1",
    "ram": "4MB",
    "build": "lowmem",  // 4MB RAM
    ...
  }
}
```
