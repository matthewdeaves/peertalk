---
name: test-machine
description: Test FTP and LaunchAPPL connectivity to a Classic Mac machine. Offers to set up folder structure and deploy LaunchAPPL if needed.
argument-hint: <machine-id>
---

# Test Classic Mac Connectivity

Test FTP and LaunchAPPL connectivity to a Classic Mac machine, and optionally complete the setup if LaunchAPPL isn't running.

## Usage

```bash
/test-machine performa6400
/test-machine se30
```

## What This Does

1. **Tests connectivity:**
   - FTP (port 21): Connection, authentication, directory listing
   - LaunchAPPL (port 1984): Remote execution server availability

2. **Smart setup assistance:**
   - If LaunchAPPL isn't running, offers to:
     - Create folder structure (Applications, Documents, Temp)
     - Build LaunchAPPLServer for the correct platform
     - Deploy .bin/.dsk files to the Mac
     - Provide extraction instructions

3. **Provides next steps:**
   - Deployment commands if fully ready
   - Setup instructions if LaunchAPPL needs to be launched
   - Troubleshooting guidance if connectivity fails

## Implementation

When the user runs `/test-machine <machine-id>`:

1. **Validate input:**
   - Extract machine ID from arguments
   - If missing, list available machines and prompt for selection

2. **Call MCP tool:**
   ```bash
   mcp__classic-mac-hardware__test_connection \
     machine=<machine-id> \
     test_launchappl=true
   ```

3. **Parse results:**
   - FTP: Connected? Credentials work? Can list directories?
   - LaunchAPPL: Port 1984 responding? Server ready?

4. **Smart next steps based on results:**

   **If FTP works but LaunchAPPL fails:**
   - Offer to set up folder structure and deploy LaunchAPPL
   - If user accepts (responds "y", "yes", or similar):
     a. Read machines.json to get platform (mactcp or opentransport)
     b. Create directories using MCP tools:
        - Applications
        - Applications:LaunchAPPLServer
        - Applications:PeerTalk
        - Documents
        - Documents:PeerTalk-Logs
        - Temp
     c. Check if LaunchAPPL source exists (LaunchAPPL/Server/)
     d. If source exists:
        - Build: `./scripts/build-launcher.sh <platform>`
        - Deploy .bin file using mcp__classic-mac-hardware__deploy_binary
        - Deploy .dsk file using mcp__classic-mac-hardware__deploy_binary
     e. Provide user instructions for extracting and launching on the Mac
   - If user declines, just show manual troubleshooting steps

   **If both FTP and LaunchAPPL work:**
   - Show success message
   - Suggest deployment and development next steps

4. **Display clear output and smart next steps:**

   **If FTP works but LaunchAPPL is not responding:**
   ```
   Testing connectivity to <machine-name>
   ========================================

   FTP (port 21):
     ✓ Connected to 10.188.1.102
     ✓ Authentication successful
     ✓ Directory listing works

   LaunchAPPL (port 1984):
     ✗ Connection refused (server not running)

   Next steps to complete setup:
     1. Create folder structure and deploy LaunchAPPL:
        - I can create Applications:LaunchAPPLServer and other folders
        - Build LaunchAPPLServer for <platform>
        - Deploy the .bin/.dsk files to your Mac
        Would you like me to do this now? (y/n)

     2. Then on your Mac:
        - Navigate to Applications:LaunchAPPLServer
        - Use BinUnpk to extract LaunchAPPLServer.bin
        - Launch LaunchAPPLServer
        - Enable "TCP Server" on port 1984
        - Run: /test-machine <machine-id> again

   Or troubleshoot manually:
     - Check if LaunchAPPLServer is already installed
     - Verify the app is running and TCP server is enabled
   ```

   **If both FTP and LaunchAPPL work:**
   ```
   Testing connectivity to <machine-name>
   ========================================

   FTP (port 21):
     ✓ Connected to 10.188.1.102
     ✓ Authentication successful
     ✓ Directory listing works

   LaunchAPPL (port 1984):
     ✓ TCP connection successful
     ✓ Server is responding

   ✓ Machine is fully ready!

   Next steps:
     /deploy <machine-id> <platform>   # Deploy PeerTalk binaries
     /fetch-logs <machine-id>          # Retrieve test logs
     /implement                         # Start development
   ```

## Error Handling

### FTP Connection Failed
```
Testing connectivity to performa6400
========================================

FTP (port 21):
  ✗ Connection refused

Troubleshooting:
  1. Check if Mac is powered on
  2. Verify RumpusFTP is running
  3. Ping the Mac: ping 10.188.1.102
  4. Check firewall settings
```

### LaunchAPPL Connection Failed
```
FTP (port 21):
  ✓ Connected

LaunchAPPL (port 1984):
  ✗ Connection refused

Troubleshooting:
  1. Launch LaunchAPPLServer app on the Mac
  2. In preferences, enable "TCP Server"
  3. Verify port is set to 1984
  4. Check if app is running (should be visible in menu bar)
```

### Machine Not Configured
```
Error: Machine "se30" not found in registry

Available machines:
  - performa6400 (Open Transport)
  - performa6200 (MacTCP)

To add a new machine:
  /setup-machine
```

## When to Use

- **After /setup-machine and /setup-launcher** - Verify new machine is fully ready
- **Before /deploy** - Ensure target Mac is reachable
- **Debugging** - Diagnose connectivity issues
- **After moving Macs** - Verify IP addresses still work
- **If LaunchAPPL fails** - Automatically offers to set up folders and deploy server

## Related Skills

- `/setup-machine` - Register new Classic Mac in machine registry
- `/setup-launcher` - Build and deploy LaunchAPPLServer
- `/deploy` - Deploy PeerTalk binaries to machines
- `/fetch-logs` - Retrieve PT_Log output

## See Also

- MCP Server: `.claude/mcp-servers/classic-mac-hardware/`
- Setup Guide: `.claude/mcp-servers/classic-mac-hardware/SETUP.md`
