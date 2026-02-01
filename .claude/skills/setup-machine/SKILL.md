---
name: setup-machine
description: Register a new Classic Mac machine and verify FTP connectivity. Creates machine registry entry and directory structure.
argument-hint: ""
---

# Setup New Classic Mac Machine

Register a new Classic Mac in the machine registry and verify FTP connectivity.

## Prerequisites

1. **MCP server is configured:**
   ```bash
   # If not done yet:
   cp .mcp.json.example .mcp.json
   # Then restart Claude Code or run /mcp
   ```

2. **Mac has RumpusFTP server running** (or equivalent FTP server)

3. **You know the Mac's:**
   - IP address
   - FTP username and password
   - System version (e.g., System 7.5.3, System 7.6.1)
   - Platform: **mactcp** (68k) or **opentransport** (PPC/68040)

## Usage

```bash
/setup-machine
```

**The skill will interactively prompt for:**
1. Machine nickname (e.g., "performa6400", "se30")
2. IP address - simple text input (e.g., "10.188.1.102")
3. Platform: **mactcp** (68k) or **opentransport** (PPC/68040)
4. FTP username (e.g., "peertalk")
5. FTP password
6. System version (e.g., "7.6.1", "7.5.3")
7. Optional description (e.g., "Performa 6400/200 - PPC")

**Note:** All text fields (machine ID, IP, credentials, system version) use simple text input without numbered options to avoid input conflicts.

## What This Skill Does

### 1. Validate Input
- Check machine ID doesn't already exist
- Validate IP address format
- Verify platform is valid (mactcp or opentransport)

### 2. Add to machines.json
- Append new machine entry to `.claude/mcp-servers/classic-mac-hardware/machines.json`
- Format:
  ```json
  {
    "performa6400": {
      "name": "Performa 6400",
      "platform": "opentransport",
      "system": "System 7.6.1",
      "cpu": "PPC 603e",
      "ftp": {
        "host": "10.188.1.102",
        "port": 21,
        "username": "peertalk",
        "password": "hunter2",
        "paths": {
          "binaries": "Applications:PeerTalk",
          "logs": "Documents:PeerTalk-Logs",
          "temp": "Temp",
          "launchappl": "Applications:LaunchAPPLServer"
        }
      },
      "notes": "Performa 6400/200 - PPC"
    }
  }
  ```

### 3. Reload MCP Configuration
- Call `mcp__classic-mac-hardware__reload_config` to pick up new machine

### 4. Test FTP Connectivity
- Call `mcp__classic-mac-hardware__test_connection` with `test_launchappl: false`
- Verify FTP server is reachable and credentials work

### 5. Create Directory Structure
- Use MCP `create_directory` tool to create:
  - `Applications`, `Applications:PeerTalk`, `Applications:LaunchAPPLServer`
  - `Documents`, `Documents:PeerTalk-Logs`
  - `Temp`
- Empty FTP root is normal - just create the directories

### 6. Next Steps
Provide clear next steps:
```
✓ Machine registered: performa6400
✓ FTP connectivity verified
✓ Directory structure created

Next steps:
1. Deploy LaunchAPPLServer and demo apps:
   /setup-launcher performa6400

2. After deploying, run LaunchAPPLServer on your Mac and test:
   /test-machine performa6400

3. Once verified, deploy PeerTalk builds:
   /deploy performa6400 opentransport
```

## Example Session

```
User: /setup-machine

Claude: Let's register your new Classic Mac!

Checking MCP server... ✓ connected

What's the machine nickname (e.g., "performa6400", "se30")?
User: performa6200

Claude: What's the IP address?
User: 10.188.1.213

Claude: What platform?
  [mactcp] System 6.0.8 - 7.5.5 (68k)
  [opentransport] System 7.6.1+ (PPC/68040)
User: mactcp

Claude: What's the FTP username?
User: mac

Claude: What's the FTP password?
User: mac

Claude: What's the System version (e.g., "7.5.3")?
User: 7.5.3

Claude: Optional description (e.g., "Performa 6200 - 68040"):
User: Performa 6200/75 - 68040

Claude: Registering performa6200...

✓ Added to machines.json
✓ Reloaded MCP configuration
✓ FTP connectivity verified
✓ Directory structure created

Next steps:
1. Deploy LaunchAPPLServer and demo apps:
   /setup-launcher performa6200

2. After deploying, run LaunchAPPLServer on your Mac and test:
   /test-machine performa6200

3. Once verified, deploy PeerTalk builds:
   /deploy performa6200 mactcp
```

## Platform Selection Guide

| Platform | System Versions | Architecture | Use For |
|----------|----------------|--------------|---------|
| **mactcp** | 6.0.8 - 7.5.5 | 68k (SE/30, IIci, LC) | MacTCP networking |
| **opentransport** | 7.6.1+ / Mac OS 8-9 | PPC, late 68040 | Open Transport networking |

## Troubleshooting

### FTP Connection Failed
- **Check IP:** Ping the Mac from your network
- **Check FTP server:** Ensure RumpusFTP is running on the Mac
- **Check credentials:** Username/password correct?
- **Firewall:** Some Macs may need FTP port 21 enabled

### Directory Creation Failed
- **Disk space:** Check if Mac has enough free space
- **Permissions:** Ensure FTP user can write to the root directory
- **FTP server:** Some FTP servers may require specific path formats

## Implementation Notes

**CRITICAL RULES:**
1. **ALWAYS use MCP tools** - Never use raw Python/Bash for FTP operations
2. **Empty FTP root is normal** - Don't probe it, just create directories
3. **Check prerequisites first** - Verify MCP server is available

**Workflow:**
1. **Check MCP is available** - Call `mcp__classic-mac-hardware__list_machines`
2. **Use AskUserQuestion ONCE** to collect all machine information in a single round
3. **Read machines.json** to check for duplicates (create if doesn't exist)
4. **Validate inputs** - If anything is incomplete, ask follow-up questions
5. **Write to machines.json** with correct nested format
6. **Call MCP tools:**
   - `mcp__classic-mac-hardware__reload_config`
   - `mcp__classic-mac-hardware__test_connection` (with `test_launchappl: false`)
   - `mcp__classic-mac-hardware__create_directory` (multiple calls for folder structure)
7. **Provide clear next steps** - Suggest `/setup-launcher` to deploy LaunchAPPLServer

## Files Modified

- `.claude/mcp-servers/classic-mac-hardware/machines.json` - Machine registry

## Related Skills

- `/setup-launcher` - Build and deploy LaunchAPPLServer and demo apps
- `/test-machine` - Test FTP and LaunchAPPL connectivity
- `/deploy` - Deploy PeerTalk binaries after setup
- `/fetch-logs` - Retrieve PT_Log output from the Mac