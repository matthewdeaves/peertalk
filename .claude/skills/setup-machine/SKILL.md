---
name: setup-machine
description: Interactive workflow to onboard a new Classic Mac machine for PeerTalk development. Adds machine to registry, builds LaunchAPPLServer, deploys via FTP, and tests connectivity.
argument-hint: ""
---

# Setup New Classic Mac Machine

Interactive workflow to onboard a new Classic Mac to the PeerTalk development environment.

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
2. IP address (e.g., "10.188.1.102")
3. Platform: **mactcp** (68k) or **opentransport** (PPC/68040)
4. FTP username (e.g., "peertalk")
5. FTP password
6. System version (e.g., "7.6.1", "7.5.3")
7. Optional description (e.g., "Performa 6400/200 - PPC")

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

### 5. Build LaunchAPPLServer
- Check if LaunchAPPL source exists (skip if not yet implemented)
- Determine platform-specific build:
  - **mactcp** → Build 68k MacTCP version
  - **opentransport** → Build PPC Open Transport version
- Run: `./scripts/build-launcher.sh <platform>`
- Binary outputs (both .bin and .dsk):
  - MacTCP: `LaunchAPPL/build-mactcp/Server/LaunchAPPLServer-MacTCP.{bin,dsk}`
  - Open Transport: `LaunchAPPL/build-ppc/Server/LaunchAPPLServer-OpenTransport.{bin,dsk}`

### 6. Create Directory Structure
- Use MCP `create_directory` tool to create:
  - `Applications`, `Applications:PeerTalk`, `Applications:LaunchAPPLServer`
  - `Documents`, `Documents:PeerTalk-Logs`
  - `Temp`
- Empty FTP root is normal - just create the directories

### 7. Deploy LaunchAPPLServer
- Skip if LaunchAPPL source doesn't exist
- Call `mcp__classic-mac-hardware__deploy_binary` twice:
  - Deploy .bin file (MacBinary format)
  - Deploy .dsk file (disk image with resource fork)
- Uploads to `Applications:LaunchAPPLServer` path on the Mac

### 8. User Instructions
Provide clear next steps:
```
✓ Machine added: performa6400
✓ FTP connectivity verified
✓ Directory structure created
✓ LaunchAPPLServer built and deployed

Next steps on your Classic Mac:
1. Navigate to Applications:LaunchAPPLServer folder
2. Use BinUnpk to extract LaunchAPPLServer from the .bin file
   (or mount the .dsk disk image)
3. Launch the LaunchAPPLServer application
4. Enable "TCP Server" on port 1984 in preferences
5. Run: /test-machine performa6400

Once LaunchAPPL connectivity is verified, you can:
  /deploy performa6400 <platform>  # Deploy PeerTalk builds
  /fetch-logs performa6400         # Retrieve PT_Log output
```

## Example Session

```
User: /setup-machine

Claude: Let's onboard your new Classic Mac!

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

Claude: Setting up performa6200...

✓ Added to machines.json
✓ Reloaded MCP configuration
✓ FTP connectivity verified
✓ Directory structure created
✓ LaunchAPPLServer built for MacTCP
✓ Deployed .bin and .dsk files

Next steps on your Performa 6200:
1. Navigate to Applications:LaunchAPPLServer folder
2. Use BinUnpk to extract LaunchAPPLServer from the .bin file
3. Launch the LaunchAPPLServer application
4. Enable "TCP Server" on port 1984 in preferences
5. Run: /test-machine performa6200

Once verified, you can:
  /deploy performa6200 mactcp     # Deploy PeerTalk builds
  /fetch-logs performa6200        # Retrieve PT_Log output
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

### LaunchAPPL Build Failed
- **Check Docker:** Ensure Docker is running
- **Check toolchain:** Retro68 toolchain must be built in container
- **Logs:** Check build output for missing headers or libraries

### Deploy Failed
- **Disk space:** Check if Mac has enough free space
- **Permissions:** Ensure FTP user can write to /Applications/
- **Path exists:** Create /Applications/LaunchAPPLServer/ manually if needed

## Implementation Notes

**CRITICAL RULES:**
1. **ALWAYS use MCP tools** - Never use raw Python/Bash for FTP operations
2. **Empty FTP root is normal** - Don't probe it, just create directories
3. **Check prerequisites first** - Verify MCP server is available

**Workflow:**
1. **Check MCP is available** - Call `mcp__classic-mac-hardware__list_machines`
2. **Use AskUserQuestion** to collect all machine information
3. **Read machines.json** to check for duplicates
4. **Validate inputs** before proceeding
5. **Write to machines.json** with correct nested format
6. **Call MCP tools:**
   - `mcp__classic-mac-hardware__reload_config`
   - `mcp__classic-mac-hardware__test_connection`
   - `mcp__classic-mac-hardware__create_directory` (multiple calls for folder structure)
7. **Check if LaunchAPPL source exists** - Skip build/deploy if not
8. **Run build script** via Bash: `./scripts/build-launcher.sh <platform>`
9. **Deploy both files** using `mcp__classic-mac-hardware__deploy_binary`:
   - Deploy .bin file
   - Deploy .dsk file
10. **Provide clear user instructions** for next steps

## Files Modified

- `.claude/mcp-servers/classic-mac-hardware/machines.json` - Machine registry
- `LaunchAPPL/build-mactcp/Server/LaunchAPPLServer-MacTCP.bin` - Built binary (if mactcp)
- `LaunchAPPL/build-ppc/Server/LaunchAPPLServer-OpenTransport.bin` - Built binary (if ot)

## Related Skills

- `/deploy` - Deploy PeerTalk binaries after setup
- `/fetch-logs` - Retrieve PT_Log output from the Mac
- `/build` - Build PeerTalk for different platforms