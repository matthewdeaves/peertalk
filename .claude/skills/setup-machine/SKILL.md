---
name: setup-machine
description: Interactive workflow to onboard a new Classic Mac machine for PeerTalk development. Adds machine to registry, builds LaunchAPPLServer, deploys via FTP, and tests connectivity.
argument-hint: ""
---

# Setup New Classic Mac Machine

Interactive workflow to onboard a new Classic Mac to the PeerTalk development environment.

## Prerequisites

1. **Mac has RumpusFTP server running** (or equivalent FTP server)
2. **You know the Mac's:**
   - IP address
   - FTP username and password
   - System version (e.g., System 7.5.3, System 7.6.1)
   - Platform (mactcp, opentransport, or appletalk)

## Usage

```bash
/setup-machine
```

**The skill will interactively prompt for:**
1. Machine nickname (e.g., "performa6400", "se30")
2. IP address (e.g., "10.188.1.102")
3. Platform: mactcp, opentransport, or appletalk
4. FTP username (e.g., "peertalk")
5. FTP password (secure input)
6. System version (e.g., "7.6.1", "7.5.3")
7. Optional description (e.g., "Performa 6400/200 - PPC")

## What This Skill Does

### 1. Validate Input
- Check machine ID doesn't already exist
- Validate IP address format
- Verify platform is valid (mactcp, ot, appletalk)

### 2. Add to machines.json
- Append new machine entry to `.claude/mcp-servers/classic-mac-hardware/machines.json`
- Format:
  ```json
  {
    "id": "performa6400",
    "name": "Performa 6400",
    "ip": "10.188.1.102",
    "ftp_user": "peertalk",
    "ftp_password": "hunter2",
    "platform": "opentransport",
    "system_version": "7.6.1",
    "description": "Performa 6400/200 - PPC"
  }
  ```

### 3. Reload MCP Configuration
- Call `mcp__classic-mac-hardware__reload_config` to pick up new machine

### 4. Test FTP Connectivity
- Call `mcp__classic-mac-hardware__test_connection` with `test_launchappl: false`
- Verify FTP server is reachable and credentials work

### 5. Build LaunchAPPLServer
- Determine platform-specific build:
  - **mactcp** → Build 68k MacTCP version
  - **opentransport** → Build PPC Open Transport version
  - **appletalk** → Build 68k AppleTalk version (or MacTCP if ADSP unavailable)
- Run: `./scripts/build-launcher.sh <platform>`
- Binary outputs:
  - MacTCP: `LaunchAPPL/build-mactcp/Server/LaunchAPPLServer-MacTCP.bin`
  - Open Transport: `LaunchAPPL/build-ppc/Server/LaunchAPPLServer-OpenTransport.bin`

### 6. Deploy LaunchAPPLServer
- Call `mcp__classic-mac-hardware__deploy_binary`:
  - `machine`: new machine ID
  - `platform`: mactcp/opentransport/appletalk
  - `binary_path`: path to built .bin file
- Uploads to `/Applications/LaunchAPPLServer/` on the Mac

### 7. User Instructions
Provide clear next steps:
```
✓ Machine added: performa6400
✓ FTP connectivity verified
✓ LaunchAPPLServer built and deployed

Next steps:
1. On your Performa 6400, navigate to /Applications/LaunchAPPLServer/
2. Use BinUnpk to extract LaunchAPPLServer from the .bin file
3. Launch the LaunchAPPLServer application
4. In the app's preferences, enable "TCP Server" on port 1984
5. Run: /test-machine performa6400

Once LaunchAPPL connectivity is verified, you can:
  /deploy performa6400 <platform>  # Deploy PeerTalk builds
  /fetch-logs performa6400         # Retrieve PT_Log output
```

## Example Session

```
User: /setup-machine

Claude: Let's onboard your new Classic Mac! I'll need some information.

What's the machine nickname (e.g., "performa6400", "se30")?
User: performa6200

Claude: What's the IP address?
User: 10.188.1.213

Claude: What platform?
  [mactcp] System 6.0.8 - 7.5.5 (68k)
  [opentransport] System 7.6.1+ (PPC/68040)
  [appletalk] Any Mac with LocalTalk/EtherTalk
User: mactcp

Claude: What's the FTP username?
User: peertalk

Claude: What's the FTP password?
User: [secure input]

Claude: What's the System version (e.g., "7.5.3")?
User: 7.5.3

Claude: Optional description (e.g., "Performa 6200 - 68040"):
User: Performa 6200/75 - 68040

Claude: Setting up performa6200...

✓ Added to machines.json
✓ Reloaded MCP configuration
✓ Testing FTP connectivity... success!
✓ Building LaunchAPPLServer for MacTCP... done!
✓ Deploying to performa6200... uploaded 142 KB

Next steps:
1. On your Performa 6200, navigate to /Applications/LaunchAPPLServer/
2. Use BinUnpk to extract LaunchAPPLServer from LaunchAPPLServer-MacTCP.bin
3. Launch the LaunchAPPLServer application
4. In the app's preferences, enable "TCP Server" on port 1984
5. Run: /test-machine performa6200

Once LaunchAPPL connectivity is verified, you can:
  /deploy performa6200 mactcp     # Deploy PeerTalk builds
  /fetch-logs performa6200        # Retrieve PT_Log output
```

## Platform Selection Guide

| Platform | System Versions | Architecture | Networking API |
|----------|----------------|--------------|----------------|
| **mactcp** | 6.0.8 - 7.5.5 | 68k | MacTCP |
| **opentransport** | 7.6.1+ / Mac OS 8-9 | PPC, late 68040 | Open Transport |
| **appletalk** | System 6+ | Any Mac | AppleTalk/ADSP |

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

When implementing this skill, use the following workflow:

1. **Use AskUserQuestion** to collect all machine information interactively
2. **Read machines.json** to check for duplicates
3. **Validate inputs** before proceeding
4. **Write to machines.json** by reading, parsing, appending, and writing back
5. **Call MCP tools** in sequence:
   - `mcp__classic-mac-hardware__reload_config`
   - `mcp__classic-mac-hardware__test_connection`
6. **Run build script** via Bash tool: `./scripts/build-launcher.sh <platform>`
7. **Determine binary path** based on platform
8. **Deploy binary** using `mcp__classic-mac-hardware__deploy_binary`
9. **Provide clear user instructions** for next steps

**IMPORTANT:** Never bypass the MCP server - all FTP operations must go through MCP tools.

## Files Modified

- `.claude/mcp-servers/classic-mac-hardware/machines.json` - Machine registry
- `LaunchAPPL/build-mactcp/Server/LaunchAPPLServer-MacTCP.bin` - Built binary (if mactcp)
- `LaunchAPPL/build-ppc/Server/LaunchAPPLServer-OpenTransport.bin` - Built binary (if ot)

## Related Skills

- `/deploy` - Deploy PeerTalk binaries after setup
- `/fetch-logs` - Retrieve PT_Log output from the Mac
- `/build` - Build PeerTalk for different platforms