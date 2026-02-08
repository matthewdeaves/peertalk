# Classic Mac Hardware Rules

**CRITICAL: All file operations with Classic Mac hardware MUST use the classic-mac-hardware MCP server.**

## Required: MCP Tools

| Operation | MCP Tool |
|-----------|----------|
| Upload file | `mcp__classic-mac-hardware__upload_file` |
| Download file | `mcp__classic-mac-hardware__download_file` |
| Fetch logs | `mcp__classic-mac-hardware__fetch_logs` |
| List directory | `mcp__classic-mac-hardware__list_directory` |
| Create directory | `mcp__classic-mac-hardware__create_directory` |
| Delete files | `mcp__classic-mac-hardware__delete_files` |
| Test connection | `mcp__classic-mac-hardware__test_connection` |
| Execute binary | `mcp__classic-mac-hardware__execute_binary` |

## Prohibited: Direct FTP/Scripts

**NEVER use these for Classic Mac file operations:**

- Python ftplib scripts
- Bash `ftp` or `lftp` commands
- `curl ftp://` commands
- Manual TCP socket connections
- Any hand-written FTP implementation

## Correct Examples

```python
# CORRECT: Use MCP tool
mcp__classic-mac-hardware__upload_file(
    machine="se30",
    local_path="build/app.bin",
    remote_path="Applications:MyApp:app.bin"
)
```

```python
# CORRECT: Use MCP tool for logs
mcp__classic-mac-hardware__fetch_logs(machine="se30")
```

## Incorrect Examples

```python
# WRONG: Never write FTP scripts
import ftplib
ftp = ftplib.FTP("192.168.1.10")
ftp.login("mac", "mac")
ftp.storbinary("STOR app.bin", open("build/app.bin", "rb"))
```

```bash
# WRONG: Never use bash ftp
ftp -n 192.168.1.10 << EOF
user mac mac
put build/app.bin
EOF
```

## Why MCP Only?

1. **Rate limiting** - MCP handles RumpusFTP's timing requirements
2. **Path normalization** - MCP converts paths to Mac colon notation
3. **Error handling** - MCP provides retry logic and informative errors
4. **Consistency** - Same interface regardless of machine
5. **Hot-reload** - MCP picks up machines.json changes automatically

## Machine Registry

All machines are configured in `.claude/mcp-servers/classic-mac-hardware/machines.json`.
Use `mcp__classic-mac-hardware__list_machines` to see available machines.

### Machine-Specific Build Requirements

**CRITICAL: Check `machines.json` for the `build` field before deploying:**

| Build Type | Heap Size | Machines | Binary Pattern |
|------------|-----------|----------|----------------|
| `standard` | 2-3MB | Performa 6200 (8MB RAM) | `test_*.bin` |
| `lowmem` | 384-512KB | Mac SE (4MB RAM) | `test_*_lowmem.bin` |

**Mac SE WILL NOT LAUNCH standard builds!** The 2-3MB heap request exceeds available RAM.

Build commands:
```bash
# Standard builds for high-RAM machines
make -f Makefile.retro68 PLATFORM=mactcp perf_tests

# Low-memory builds for 4MB machines (Mac SE, Plus, Classic)
make -f Makefile.retro68 PLATFORM=mactcp lowmem_tests
```

### Deployment Methods

Machines may support FTP, LaunchAPPL, or both:

| Machine | FTP | LaunchAPPL | Preferred for Deployment |
|---------|-----|------------|--------------------------|
| Performa 6200 | ✓ | ✓ | Either (FTP for files, LaunchAPPL for execution) |
| Mac SE | ✗ | ✓ | LaunchAPPL only |

- **If no FTP configured**: Use `execute_binary` (LaunchAPPL) which transfers and runs in one step
- **LaunchAPPL port**: Always 1984 when configured
- **Check capability**: Use `test_connection` with `test_launchappl=true`

## If MCP Doesn't Work

If the MCP server has issues:
1. Check connectivity with `mcp__classic-mac-hardware__test_connection`
2. Reload config with `mcp__classic-mac-hardware__reload_config`
3. Fix the MCP server - DO NOT fall back to raw FTP
