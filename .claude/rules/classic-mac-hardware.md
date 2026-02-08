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

## If MCP Doesn't Work

If the MCP server has issues:
1. Check connectivity with `mcp__classic-mac-hardware__test_connection`
2. Reload config with `mcp__classic-mac-hardware__reload_config`
3. Fix the MCP server - DO NOT fall back to raw FTP
