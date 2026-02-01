# Classic Mac Hardware MCP Server

FTP-based access to Classic Macintosh test machines for PeerTalk development.

## Purpose

**Single Responsibility**: Provide standardized access to Classic Mac hardware over FTP for binary deployment, log retrieval, and file management.

## Design Principles

Following [Anthropic MCP Best Practices](https://www.anthropic.com/engineering/code-execution-with-mcp):

1. **Code Execution Pattern** - Designed as code API for 98%+ token savings
2. **Progressive Disclosure** - Load tools on-demand, not upfront
3. **Resources for reads** - Logs and file lists (no side effects)
4. **Tools for writes** - Deploy binaries, execute commands (explicit consent)
5. **Security First** - User consent for all deployments

## MCP Primitives

### Resources (Read-Only)

Resources provide data without side effects, like GET endpoints.

| Resource | URI | Description |
|----------|-----|-------------|
| Logs | `mac://{machine}/logs/{session_id}` | PT_Log output from test session |
| Binary | `mac://{machine}/binaries/{platform}` | Deployed binary version info |
| Files | `mac://{machine}/files/{path}` | List files in directory |

### Tools (Actions)

Tools perform actions with side effects, require explicit consent.

| Tool | Parameters | Purpose |
|------|------------|---------|
| `list_machines` | - | List all configured Classic Mac machines |
| `test_connection` | machine, test_launchappl? | Test FTP and LaunchAPPL connectivity |
| `list_directory` | machine, path? | List files and directories via FTP |
| `create_directory` | machine, path | Create directory via FTP |
| `delete_files` | machine, path, recursive? | Delete files/directories via FTP |
| `deploy_binary` | machine, platform, binary_path | Upload compiled binary via FTP (.bin and .dsk) |
| `upload_file` | machine, local_path, remote_path | Upload any file to Mac via FTP |
| `download_file` | machine, remote_path, local_path? | Download any file from Mac via FTP |
| `fetch_logs` | machine, session_id?, destination? | Download PT_Log output via FTP |
| `execute_binary` | machine, platform, binary_path?, args? | Execute binary via LaunchAPPL TCP |
| `cleanup_machine` | machine, scope?, specific_path?, keep_latest? | Clean files (old_files/binaries/logs/all/specific_path) |
| `reload_config` | - | Reload machines.json without restarting |

**Security:** All destructive operations (delete_files, cleanup_machine with scope=all) require explicit user consent.

### Prompts

Reusable templates for common workflows.

| Prompt | Description |
|--------|-------------|
| `deploy-and-test` | Full workflow: deploy binary, run tests, fetch logs |
| `compare-platforms` | Deploy to all machines, run same test, compare logs |
| `debug-crash` | Fetch crash logs and correlate with source code |

## Installation

**Zero Host Dependencies!** The MCP server runs entirely inside the Docker container.

```bash
# 1. Build Docker container (includes MCP dependencies)
./scripts/docker-build.sh

# 2. Configure your machines
cp .claude/mcp-servers/classic-mac-hardware/machines.example.json \
   .claude/mcp-servers/classic-mac-hardware/machines.json

# 3. Edit machines.json with your Classic Mac FTP details

# 4. Configure Claude Code to use MCP server
cp .mcp.json.example .mcp.json

# Done! The MCP server runs in Docker via run-in-container.sh wrapper
```

**What You Need:**
- Docker (for Retro68 + MCP server)
- Classic Mac(s) with RumpusFTP server
- That's it! No Python, no pip, no host dependencies.

## Configuration

**Hot-Reload:** The MCP server automatically detects changes to `machines.json` and reloads the configuration on every operation. No need to restart!

**machines.json** format:

```json
{
  "se30": {
    "name": "SE/30",
    "platform": "mactcp",
    "system": "System 6.0.8",
    "cpu": "68030",
    "ftp": {
      "host": "192.168.1.10",
      "port": 21,
      "username": "peertalk",
      "password": "${PEERTALK_SE30_FTP_PASSWORD}",
      "paths": {
        "binaries": "/Applications/PeerTalk/",
        "logs": "/Documents/PeerTalk-Logs/",
        "temp": "/Temp/"
      }
    }
  },
  "iici": {
    "name": "IIci",
    "platform": "opentransport",
    "system": "Mac OS 9.2",
    "cpu": "68040",
    "ftp": {
      "host": "192.168.1.11",
      "port": 21,
      "username": "peertalk",
      "password": "${PEERTALK_IICI_FTP_PASSWORD}",
      "paths": {
        "binaries": "/Applications/PeerTalk/",
        "logs": "/Documents/PeerTalk-Logs/",
        "temp": "/Temp/"
      }
    }
  }
}
```

**Environment variables:**

```bash
export PEERTALK_SE30_FTP_PASSWORD="your-password"
export PEERTALK_IICI_FTP_PASSWORD="your-password"
```

## Claude Code Integration

Add to `.claude/settings.json`:

```json
{
  "mcpServers": {
    "classic-mac-hardware": {
      "command": "python",
      "args": [
        ".claude/mcp-servers/classic-mac-hardware/server.py"
      ],
      "env": {
        "MACHINES_CONFIG": ".claude/mcp-servers/classic-mac-hardware/machines.json"
      }
    }
  }
}
```

## Code Execution Pattern

The MCP server is designed to be used via code, not just direct tool calls.

**Traditional approach (high token cost):**
```
1. Call list_machines tool → Get machine list
2. Call deploy_binary tool for each machine
3. Call execute_binary tool for each machine
4. Call fetch_logs tool for each machine
5. Process logs in model context
```

**Code execution approach (98% token savings):**
```python
import classic_mac_hardware as mac

# Deploy to all MacTCP machines
mactcp_machines = [m for m in mac.list_machines() if m['platform'] == 'mactcp']

for machine in mactcp_machines:
    # Deploy binary
    mac.deploy_binary(
        machine=machine['id'],
        platform='mactcp',
        binary_path='build/mactcp/PeerTalk'
    )

    # Run test
    result = mac.execute_binary(
        machine=machine['id'],
        platform='mactcp',
        args=['--test', 'tcp-connect']
    )

    # Download logs
    logs = mac.fetch_logs(machine=machine['id'], session_id=result['session_id'])

    # Filter for errors (in execution environment, not model context)
    errors = [line for line in logs.split('\n') if '[ERROR]' in line]

    # Only return summary
    print(f"{machine['name']}: {len(errors)} errors")
    if errors:
        print("First error:", errors[0])
```

This keeps large log files in the execution environment and only passes summaries to the model.

## Security

Following [MCP Security Best Practices](https://modelcontextprotocol.io/specification/2025-11-25):

1. **User Consent**:
   - All `deploy_binary` calls require explicit user approval
   - All `execute_binary` calls show command before execution
   - User sees machine name, platform, and file being deployed

2. **Data Privacy**:
   - Logs are fetched only when explicitly requested
   - No automatic log uploads to external services
   - FTP credentials stored in environment variables, not in code

3. **Tool Safety**:
   - `execute_binary` runs only binaries deployed via this MCP server
   - No arbitrary command execution on Classic Macs
   - `cleanup_machine` shows what will be deleted before proceeding

4. **Access Controls**:
   - machines.json is gitignored (contains sensitive FTP info)
   - Separate FTP credentials per machine
   - Read-only FTP access for log retrieval

## Usage Examples

### Via Agent (Auto-Triggered)

```
User: "Test passed on POSIX but crashed on SE/30"

cross-platform-debug agent (spawned):
1. Uses mac://se30/logs/latest to get crash log
2. Compares with local POSIX logs
3. Identifies divergence point
4. Suggests fix with line numbers
```

### Via Skill

```bash
# Deploy and test
/deploy se30 mactcp

# Fetch logs
/fetch-logs se30

# Compare logs
/compare-logs
```

### Via Code (Direct)

```python
import classic_mac_hardware as mac

# Get latest logs
logs = mac.get_resource('mac://se30/logs/latest')

# Parse PT_Log format
for line in logs.split('\n'):
    if '[ERROR]' in line:
        # Extract timestamp, category, message
        parts = line.split(']')
        timestamp = parts[0][1:]
        category = parts[1][1:]
        message = parts[2].strip()
        print(f"{category}: {message}")
```

## Testing Without Hardware

For development without Classic Mac hardware:

```bash
# Use mock FTP server
python .claude/mcp-servers/classic-mac-hardware/mock_server.py

# Simulates FTP responses for testing
```

## References

- [Model Context Protocol Specification](https://modelcontextprotocol.io/specification/2025-11-25)
- [Code Execution with MCP](https://www.anthropic.com/engineering/code-execution-with-mcp)
- [MCP Best Practices](https://modelcontextprotocol.info/docs/best-practices/)
- [Claude Code MCP Integration](https://docs.anthropic.com/en/docs/claude-code/mcp)

## Sources

- [Model Context Protocol Specification](https://modelcontextprotocol.io/specification/2025-11-25)
- [Introducing the Model Context Protocol](https://www.anthropic.com/news/model-context-protocol)
- [Code Execution with MCP](https://www.anthropic.com/engineering/code-execution-with-mcp)
- [MCP Best Practices Guide](https://modelcontextprotocol.info/docs/best-practices/)
- [Claude Code MCP Docs](https://docs.anthropic.com/en/docs/claude-code/mcp)
