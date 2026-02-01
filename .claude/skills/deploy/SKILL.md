---
name: deploy
description: Deploy compiled binaries to Classic Mac test machines via FTP. Uses classic-mac-hardware MCP server. Supports deploying to single machine, platform (all machines of that platform), or all machines.
argument-hint: <machine-id|platform|all> [platform]
---

# Deploy Binaries to Classic Mac Hardware

Deploy compiled binaries to Classic Mac test machines via FTP.

## Usage

```bash
/deploy se30 mactcp                # Deploy MacTCP binary to SE/30
/deploy mactcp                     # Deploy to all MacTCP machines
/deploy all                        # Deploy to all machines (all platforms)
/deploy se30 mactcp --verify       # Deploy and verify with test run
```

## Prerequisites

**IMPORTANT:** This skill requires PeerTalk binaries to exist. If not yet implemented:
- Check project status: `/session status`
- Start implementing: `/session next` or `/implement`
- Build binaries will be created during implementation

1. **Binaries built:**
   ```bash
   /build package    # Creates build/mactcp/ and build/ppc/ binaries
   ```

   **If binaries don't exist yet:**
   - PeerTalk SDK not implemented → Start with `/session next`
   - Build failed → Check `/build test` output

2. **Machines configured:**
   - **First time?** Run `/setup-machine` then `/setup-launcher` to onboard new Classic Macs
   - Check `.claude/mcp-servers/classic-mac-hardware/machines.json`
   - Or use MCP server: `/mcp` → list resources

3. **MCP server running:**
   - Configured in `.claude/settings.json`
   - machines.json configured

**For testing remote execution without PeerTalk:** Use `/execute` with Retro68 demo apps

## Execution Notes

**Before deploying, check if binaries exist:**

```bash
# Check for binaries
ls build/mactcp/PeerTalk.bin build/ppc/PeerTalk.bin 2>/dev/null
```

**If binaries don't exist:**
```
❌ PeerTalk binaries not found

The PeerTalk SDK hasn't been built yet.

Next steps:
1. Check implementation status: /session status
2. Build PeerTalk: /build package
3. Or start implementing: /session next

To test deployment without PeerTalk, use /execute with demo apps.
```

## Commands

### Deploy to Single Machine

```bash
/deploy se30 mactcp
```

**Process:**
1. Check binary exists: `build/mactcp/PeerTalk.bin` and `PeerTalk.dsk`
2. Connect to SE/30 via FTP
3. Upload both files to `/Applications/PeerTalk/`:
   - `PeerTalk-mactcp.dsk` (disk image - mount and copy app)
   - `PeerTalk-mactcp.bin` (binary - use with BinUnpk or LaunchAPPL)
4. Create version file with metadata
5. Verify upload succeeded

**Output:**
```
Deploying to Classic Mac
=========================

Machine: se30 (SE/30)
Platform: mactcp

Connecting to 192.168.1.10... ✓

Uploading files:
  - PeerTalk-mactcp.dsk... ✓ (142 KB)
  - PeerTalk-mactcp.bin... ✓ (138 KB)

Creating version file... ✓

✓ Deployed successfully

Remote path: /Applications/PeerTalk/
Files:
  - PeerTalk-mactcp.dsk (mount with BinHex or double-click)
  - PeerTalk-mactcp.bin (for BinUnpk or remote execution)

Next steps:
  /fetch-logs se30           # Get logs after testing
  # Test FTP by deploying again or checking machines.json
```

### Deploy to All Machines of Platform

```bash
/deploy mactcp
```

**Finds all machines with `platform: mactcp` and deploys to each:**
```
Deploying MacTCP binary to all machines
========================================

Found 2 MacTCP machines:
  - se30 (SE/30)
  - classic (Classic II)

se30 (SE/30):
  Uploading... ✓ (2.1s)

classic (Classic II):
  Uploading... ✓ (3.2s)

✓ Deployed to 2/2 machines

Binary: build/mactcp/PeerTalk (45.2 KB)
```

### Deploy to All Machines (All Platforms)

```bash
/deploy all
```

**Deploys each platform's binary to corresponding machines:**
```
Deploying to all machines
==========================

MacTCP (build/mactcp/PeerTalk):
  se30 (SE/30):      ✓ (2.1s)
  classic (Classic): ✓ (3.2s)

Open Transport (build/opentransport/PeerTalk):
  iici (IIci):       ✓ (1.8s)

AppleTalk (build/appletalk/PeerTalk):
  quadra (Quadra):   ✓ (2.5s)

✓ Deployed to 4/4 machines
```

### Deploy with Verification

```bash
/deploy se30 mactcp --verify
```

**After deployment, runs a quick test:**
```
Deploying to SE/30... ✓
Verifying deployment...

Running verification test:
  Executing: PeerTalk-mactcp --test basic
  Result: ✓ Binary executes successfully

Verification: PASSED
```

## Process

### Step 1: Parse Arguments

```
$ARGUMENTS = "se30 mactcp"     → machine="se30", platform="mactcp"
$ARGUMENTS = "mactcp"          → platform="mactcp" (deploy to all)
$ARGUMENTS = "all"             → deploy all platforms
$ARGUMENTS = "se30 mactcp --verify" → verify=true
```

### Step 2: Check Binary Exists

```bash
# Check if binary was built
if [ ! -f "build/${platform}/PeerTalk" ]; then
    echo "Binary not found: build/${platform}/PeerTalk"
    echo "Run /build package first"
    exit 1
fi
```

### Step 3: Get Machine List

**Single machine:**
- Read machines.json
- Verify machine exists
- Verify platform matches

**By platform:**
- Read machines.json
- Filter by platform
- Get list of matching machines

**All:**
- Read machines.json
- Group by platform

### Step 4: Deploy via MCP Server

Use the classic-mac-hardware MCP server:

```python
import classic_mac_hardware as mac

# Deploy to single machine
result = mac.deploy_binary(
    machine="se30",
    platform="mactcp",
    binary_path="build/mactcp/PeerTalk"
)
```

**Or call MCP tool directly:**
```
Tool: deploy_binary
Arguments:
  machine: se30
  platform: mactcp
  binary_path: build/mactcp/PeerTalk
```

### Step 5: Verify Upload

**Check file exists on remote:**
```python
# List remote files
files = mac.get_resource('mac://se30/files/binaries')

# Verify PeerTalk-mactcp exists
if 'PeerTalk-mactcp' in files:
    print("✓ Upload verified")
```

### Step 6: Report Results

Show:
- Machine name and platform
- Binary size
- Upload time
- Remote path
- Next steps

## Binary Path Convention

```
Local:  build/{platform}/PeerTalk
Remote: /Applications/PeerTalk/PeerTalk-{platform}
```

**Example:**
```
Local:  build/mactcp/PeerTalk
Remote: /Applications/PeerTalk/PeerTalk-mactcp
```

## Version File

For each binary, create a `.version` file:

**PeerTalk-mactcp.version:**
```json
{
  "platform": "mactcp",
  "uploaded": "2026-02-01T14:30:52Z",
  "source": "build/mactcp/PeerTalk",
  "size": 46234,
  "git_commit": "35a5c8d",
  "git_branch": "main"
}
```

This allows tracking which version is deployed on each machine.

## Error Handling

### Binary Not Built
```
✗ Binary not found: build/mactcp/PeerTalk

Build first:
  /build package
  # or
  /build compile mactcp
```

### Machine Not Configured
```
✗ Machine 'se30' not found in configuration

Available machines:
  - iici (IIci - opentransport)
  - quadra (Quadra - appletalk)

Add machine:
  Edit .claude/mcp-servers/classic-mac-hardware/machines.json
```

### FTP Connection Failed
```
✗ Connection failed: se30 (192.168.1.10)
  Error: Connection refused

Troubleshooting:
  1. Check machine is powered on
  2. Verify RumpusFTP is running
  3. Verify machines.json has correct IP/credentials
  4. Check IP: ping 192.168.1.10
```

### Upload Failed
```
✗ Upload failed: Permission denied

Possible causes:
  1. Insufficient FTP permissions
  2. Directory doesn't exist: /Applications/PeerTalk/
  3. Disk full on Classic Mac

Fix:
  - Check FTP user has write permissions
  - Create directory on Classic Mac
  - Free up disk space
```

### Platform Mismatch
```
✗ Platform mismatch: se30 is configured for 'mactcp' but you specified 'opentransport'

Correct usage:
  /deploy se30 mactcp
```

## Integration

### With `/build` Skill

```bash
/build package              # Build all platforms
/deploy all                 # Deploy to all machines
```

### With `/fetch-logs` Skill

```bash
/deploy se30 mactcp         # Deploy binary
# Test on SE/30 manually
/fetch-logs se30            # Get test results
```

### With `/hw-test` Skill

```bash
/hw-test generate 5.3       # Create test plan
/deploy se30 mactcp         # Deploy for testing
# Follow test plan
/fetch-logs se30            # Get results
```

### With cross-platform-debug Agent

```bash
/deploy all                 # Deploy to all machines
# Run tests, observe failure
"Test passed on Linux but crashed on SE/30"
# → Agent auto-spawns, uses MCP to fetch logs, diagnoses issue
```

## Advanced Usage

### Deploy Specific Build

```bash
/deploy se30 mactcp build/archive/v1.2.3/PeerTalk
```

### Deploy with Custom Name

```bash
/deploy se30 mactcp --as PeerTalk-test
# Deploys as PeerTalk-test instead of PeerTalk-mactcp
```

### Deploy and Run

```bash
/deploy se30 mactcp --run "--test tcp-connect"
# Deploys, then executes with arguments
```

### Dry Run

```bash
/deploy se30 mactcp --dry-run
# Shows what would be deployed without actually uploading
```

## Status Tracking

After deployment, query deployment status:

```python
# Get binary info via MCP resource
info = mac.get_resource('mac://se30/binary/mactcp')

# Returns:
# {
#   "platform": "mactcp",
#   "uploaded": "2026-02-01T14:30:52Z",
#   "size": 46234,
#   "git_commit": "35a5c8d"
# }
```

## Workflow Examples

### Quick Test Cycle

```bash
# Make changes to code
/build compile mactcp       # Quick compile
/deploy se30 mactcp         # Deploy to SE/30
# Test on SE/30
/fetch-logs se30            # Get results
```

### Full Platform Test

```bash
/build package              # Build all platforms
/deploy all                 # Deploy to all machines
# Test on each machine
/fetch-logs                 # Get all logs
/compare-logs               # Compare results
```

### Single Platform Test

```bash
/build compile mactcp
/deploy mactcp              # Deploy to all MacTCP machines
# Test on all MacTCP machines
/fetch-logs mactcp          # Get logs from all MacTCP machines
```

## Notes

- **Both .dsk and .bin files are deployed by default:**
  - `.dsk` = HFS disk image (preserves resource fork, double-click to mount)
  - `.bin` = MacBinary format (use with BinUnpk to convert to app, or for LaunchAPPL remote execution)
- Deployment uses passive mode FTP (RumpusFTP compatible)
- Binaries are uploaded in binary mode (not ASCII)
- Large binaries may take time on slow networks (Serial/LocalTalk)
- Version files help track which build is on which machine
- Old binaries are renamed to `.old` before new upload

## See Also

- `/build` - Build binaries for Classic Mac
- `/fetch-logs` - Retrieve logs from machines
- `/hw-test` - Generate hardware test plans
- [MCP Server README](../../mcp-servers/classic-mac-hardware/README.md)
- [machines.json](../../mcp-servers/classic-mac-hardware/machines.json) - Machine configuration
