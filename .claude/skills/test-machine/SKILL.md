---
name: test-machine
description: Test FTP and LaunchAPPL connectivity to a Classic Mac machine. Simple wrapper around mcp__classic-mac-hardware__test_connection.
argument-hint: <machine-id>
---

# Test Classic Mac Connectivity

Test FTP and LaunchAPPL connectivity to a Classic Mac machine.

## Usage

```bash
/test-machine performa6400
/test-machine se30
```

## What This Does

Calls the MCP server to test:
1. **FTP connectivity** - Can we connect and authenticate?
2. **LaunchAPPL connectivity** - Is the remote execution server running on port 1984?

This is a convenience wrapper around:
```bash
mcp__classic-mac-hardware__test_connection machine=<machine-id>
```

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

4. **Display clear output:**
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

   ✓ Machine is ready for deployment!

   Next steps:
     /deploy <machine-id> <platform>   # Deploy PeerTalk binaries
     /fetch-logs <machine-id>          # Retrieve test logs
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

- **After /setup-machine** - Verify new machine is ready
- **Before /deploy** - Ensure target Mac is reachable
- **Debugging** - Diagnose connectivity issues
- **After moving Macs** - Verify IP addresses still work

## Related Skills

- `/setup-machine` - Onboard new Classic Macs
- `/deploy` - Deploy binaries to machines
- `/fetch-logs` - Retrieve PT_Log output

## See Also

- MCP Server: `.claude/mcp-servers/classic-mac-hardware/`
- Setup Guide: `.claude/mcp-servers/classic-mac-hardware/SETUP.md`
