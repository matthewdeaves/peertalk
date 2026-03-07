# Classic Mac Hardware Rules

**CRITICAL: All file operations with Classic Mac hardware MUST use the classic-mac-hardware MCP server.**

## Required: MCP Tools

| Operation | MCP Tool |
|-----------|----------|
| Upload file | `mcp__classic-mac-hardware__upload_file` |
| Download file | `mcp__classic-mac-hardware__download_file` |
| List directory | `mcp__classic-mac-hardware__list_directory` |
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

## Why MCP Only?

1. **Rate limiting** - MCP handles RumpusFTP's timing requirements
2. **Path normalization** - MCP converts paths to Mac colon notation
3. **Error handling** - MCP provides retry logic and informative errors
4. **Consistency** - Same interface regardless of machine

## Machine Registry

All machines are configured in `.claude/mcp-servers/classic-mac-hardware/machines.json`.
Edit the file directly to add or modify machines.
Use `mcp__classic-mac-hardware__list_machines` to see available machines.

## Log Collection

Test apps write logs to a `PT_Log` file on the Mac. After the test completes,
download logs via FTP:

```python
mcp__classic-mac-hardware__download_file(
    machine="performa6400",
    remote_path="PT_Log",
    local_path="downloads/performa6400/PT_Log"
)
```

For LaunchAPPL-only machines (no FTP), test output is captured in the
`execute_binary` stdout.

## Deployment Methods

Machines may support FTP, LaunchAPPL, or both:

| Machine | FTP | LaunchAPPL | Preferred |
|---------|-----|------------|-----------|
| performa6400 | yes | yes | Either |
| performa630 | no | yes | LaunchAPPL only |
| macse | no | yes | LaunchAPPL only |

- **FTP machines**: Use `upload_file` to deploy, then run manually or via LaunchAPPL
- **LaunchAPPL-only**: Use `execute_binary` which transfers and runs in one step
- **LaunchAPPL port**: Always 1984

## LaunchAPPL Test Execution

**Run only ONE test at a time via LaunchAPPL.** Test apps bind to network
ports (7353 discovery, 7354 TCP). Running multiple tests causes port
conflicts and resource leaks.

### Execution Pattern

```python
# 1. Execute test
result = mcp__classic-mac-hardware__execute_binary(
    machine="performa630",
    platform="mactcp",
    binary_path="build/mac/test_latency.bin"
)

# 2. If timeout (120s), the test is still running on the Mac
# Download logs via FTP when it finishes (if FTP available)

# 3. Only start the next test after the previous one completes
```

### If a Test Gets Stuck

1. Check log file for errors (download via FTP if available)
2. Reboot the Mac (cleanest solution)
3. Wait for peer timeout (30 seconds) before retrying

## If MCP Doesn't Work

1. Check connectivity: `mcp__classic-mac-hardware__test_connection`
2. Restart the MCP server
3. Fix the MCP server - DO NOT fall back to raw FTP
