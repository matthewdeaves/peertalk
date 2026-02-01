# Classic Mac Hardware MCP Server - Summary

Production-quality MCP server following [Anthropic's best practices](https://www.anthropic.com/engineering/code-execution-with-mcp) for Classic Macintosh hardware access.

## What It Does

Provides FTP-based access to Classic Mac test machines (SE/30, IIci, Quadra) for:
- Deploying compiled binaries
- Fetching PT_Log output
- Comparing cross-platform behavior
- Debugging crashes on real hardware

## Architecture

**Design Pattern:** Code Execution Pattern (98%+ token savings)

Instead of:
```
❌ Load all tool definitions → 150K tokens
❌ Call deploy_binary for each machine
❌ Call fetch_logs for each machine
❌ Process all logs in model context
```

Use this:
```python
✅ Write code that uses MCP server as API
✅ Filter data in execution environment
✅ Return only summaries → 2K tokens
```

## MCP Primitives

Following [MCP Specification](https://modelcontextprotocol.io/specification/2025-11-25):

**Resources** (read-only, no side effects):
- `mac://{machine}/logs/latest` - PT_Log output
- `mac://{machine}/binary/{platform}` - Binary version info
- `mac://{machine}/files/{path}` - File listings

**Tools** (actions, require consent):
- `list_machines` - Show configured hardware
- `test_connection` - Test FTP and LaunchAPPL connectivity
- `list_directory` - Browse files and directories
- `create_directory` - Create directories via FTP
- `delete_files` - Delete files/directories recursively
- `deploy_binary` - Upload compiled binaries (.bin and .dsk)
- `fetch_logs` - Download PT_Log output
- `execute_binary` - Run binaries via LaunchAPPL TCP (port 1984)
- `cleanup_machine` - Remove files (old_files/binaries/logs/all/specific_path)
- `reload_config` - Hot-reload machines.json (auto-reloads on file change)

**Prompts** (workflow templates):
- `deploy-and-test` - Full test cycle
- `compare-platforms` - Multi-platform testing
- `debug-crash` - Crash log analysis

## Security

Following [MCP Security Best Practices](https://modelcontextprotocol.io/specification/2025-11-25):

1. **User Consent**: All deployments require explicit approval
2. **Data Privacy**: Logs fetched only on request, never auto-uploaded
3. **Tool Safety**: Only runs binaries deployed through this MCP server
4. **Access Controls**: Separate FTP credentials per machine, environment variables for passwords

## Configuration

**Hot-Reload:** The server automatically detects changes to `machines.json` and reloads on every operation - no restart needed!

**User-specific** (not committed to git):
```bash
.claude/mcp-servers/classic-mac-hardware/machines.json
```

**Shared template** (committed):
```bash
.claude/mcp-servers/classic-mac-hardware/machines.example.json
```

Each developer configures their own hardware. See `SETUP.md` for details.

## RumpusFTP Compatibility

- **Plain FTP** (not SFTP) - Classic Macs don't support SFTP
- **Passive mode (PASV)** - Required for NAT traversal
- **Mac path conventions** - Handles Classic Mac filesystem

## Integration

### With cross-platform-debug Agent

Auto-triggered when user says "Works on Linux but crashes on SE/30":

```
1. Agent spawned
2. Uses MCP to fetch logs from SE/30
3. Compares with local POSIX logs
4. Identifies divergence point
5. Checks source code differences
6. Reports fix with line numbers
```

### With Skills

Optional manual control:
```bash
/deploy se30 mactcp     # Deploy to hardware
/fetch-logs se30        # Get logs
/compare-logs           # Side-by-side comparison
```

### With Code

Direct usage in execution environment:
```python
import classic_mac_hardware as mac

# Get logs
logs = mac.get_resource('mac://se30/logs/latest')

# Filter in code (not model context)
errors = [l for l in logs.split('\n') if '[ERROR]' in l]

# Return summary only
print(f"Found {len(errors)} errors")
```

## Files

```
.claude/mcp-servers/classic-mac-hardware/
├── README.md              # Full documentation
├── SETUP.md               # Configuration guide
├── SUMMARY.md             # This file
├── server.py              # MCP server implementation
├── requirements.txt       # Python dependencies
├── setup.sh               # Interactive setup script
├── machines.example.json  # Configuration template
└── machines.json          # User-specific (gitignored)
```

## Token Efficiency

**Traditional approach:**
- Load all tool definitions: ~50K tokens
- Pass logs through model: ~100K tokens per test
- Total: ~150K tokens

**Code execution approach:**
- Load only needed tools on-demand: ~2K tokens
- Filter logs in execution environment: 0 tokens in model
- Return summaries only: ~500 tokens
- Total: ~2.5K tokens

**Savings: 98.3%**

## References

Design based on:
- [Code Execution with MCP](https://www.anthropic.com/engineering/code-execution-with-mcp)
- [MCP Specification](https://modelcontextprotocol.io/specification/2025-11-25)
- [MCP Best Practices](https://modelcontextprotocol.info/docs/best-practices/)
- [Claude Code MCP Integration](https://docs.anthropic.com/en/docs/claude-code/mcp)

## Quick Start

```bash
# Setup
.claude/mcp-servers/classic-mac-hardware/setup.sh

# Configure in Claude Code settings
# Add to .claude/settings.json:
{
  "mcpServers": {
    "classic-mac-hardware": {
      "command": "python",
      "args": [".claude/mcp-servers/classic-mac-hardware/server.py"]
    }
  }
}

# Test
python .claude/mcp-servers/classic-mac-hardware/server.py
```

## Status

✅ **Production-ready and battle-tested**
- Full MCP spec compliance
- Security best practices
- Token-efficient code execution pattern
- Multi-developer configuration support
- Hot-reload configuration (no restarts)
- Comprehensive file operations (list/create/delete directories)
- Remote execution via LaunchAPPL TCP
- Connection testing (FTP + LaunchAPPL)
- Tested with RumpusFTP on System 7.5.3 (MacTCP) and 7.6.1 (Open Transport)
- Passive mode FTP working
- Dual-format deployment (.bin + .dsk)
