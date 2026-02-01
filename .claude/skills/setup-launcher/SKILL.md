---
name: setup-launcher
description: Build and deploy LaunchAPPLServer and Retro68 demo applications to a registered Classic Mac machine via FTP.
argument-hint: "<machine-id>"
---

# Setup LaunchAPPL and Demo Apps

Build and deploy LaunchAPPLServer and Retro68 demo applications to a registered Classic Mac.

## Prerequisites

1. **Machine must be registered** via `/setup-machine`
2. **MCP server configured** and connected
3. **Docker running** (for Retro68 builds)
4. **FTP connectivity verified** (done during `/setup-machine`)

## Usage

```bash
# Deploy to a specific machine
/setup-launcher performa6400

# Or without arguments to select from registered machines
/setup-launcher
```

## What This Skill Does

### 1. Validate Machine
- Check that the machine exists in the registry
- Read machine configuration to determine platform (mactcp/opentransport)
- Verify MCP server is connected

### 2. Build LaunchAPPLServer
- Build for the detected platform using Retro68 toolchain:
  - **mactcp** → Build 68k version using m68k-apple-macos toolchain
  - **opentransport** → Build PPC version using powerpc-apple-macos (retroppc) toolchain
- Source location: `/opt/Retro68/LaunchAPPL/` in Docker container
- Output files: `.bin` (binary) and `.dsk` (disk image)
- Build artifacts copied to `LaunchAPPL-build/` in workspace

### 3. Build Retro68 Demo Apps
- Build Hello World application from Retro68 samples
- Uses same platform toolchain as LaunchAPPLServer
- Provides a simple test to verify LaunchAPPL remote execution works
- Output: `HelloWorld.bin` and `HelloWorld.dsk`

### 4. Deploy to Mac
- Upload LaunchAPPLServer to `Applications:LaunchAPPLServer:`
  - `LaunchAPPLServer.bin`
  - `LaunchAPPLServer.dsk`
- Upload demo apps to `Temp:`
  - `HelloWorld.bin`
  - `HelloWorld.dsk`

### 5. Provide Instructions
```
✓ LaunchAPPLServer built for Open Transport
✓ Demo apps built
✓ Deployed to performa6400

Next steps on your Classic Mac:
1. Navigate to Applications:LaunchAPPLServer
2. Mount LaunchAPPLServer.dsk (or extract .bin with StuffIt/BinUnpk)
3. Launch LaunchAPPLServer application
4. In Preferences, enable "TCP Server" on port 1984
5. Test connectivity: /test-machine performa6400

Try the demo app:
1. Navigate to Temp folder
2. Mount HelloWorld.dsk
3. Run HelloWorld application

Once LaunchAPPL is running:
  /deploy performa6400 opentransport  # Deploy PeerTalk builds
  /fetch-logs performa6400            # Retrieve PT_Log output
```

## Example Session

```
User: /setup-launcher performa6400

Claude: Building LaunchAPPLServer for performa6400 (opentransport platform)...

✓ Built LaunchAPPLServer for Open Transport (PPC)
  - Binary: 211 KB
  - Disk image: 800 KB

✓ Built demo applications
  - HelloWorld: 156 KB

✓ Deployed to Performa 6400
  - LaunchAPPLServer → Applications:LaunchAPPLServer
  - HelloWorld → Temp

Next steps on your Performa 6400:
1. Navigate to Applications:LaunchAPPLServer
2. Mount LaunchAPPLServer.dsk
3. Launch LaunchAPPLServer and enable TCP Server (port 1984)
4. Test: /test-machine performa6400
```

## Platform Build Details

| Platform | Toolchain | CMake Toolchain File |
|----------|-----------|---------------------|
| **mactcp** | m68k-apple-macos | `/opt/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake` |
| **opentransport** | powerpc-apple-macos | `/opt/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake` |

## Troubleshooting

### Build Failed
- **Check Docker:** Ensure Docker daemon is running
- **Check toolchain:** Verify Retro68 toolchain is built in container
  ```bash
  docker compose -f docker/docker-compose.yml run --rm peertalk-dev bash -c "ls /opt/Retro68-build/toolchain/"
  ```
- **Missing source:** LaunchAPPL source should be at `/opt/Retro68/LaunchAPPL/` in container

### Upload Failed
- **Check disk space:** Mac may be full
- **Check permissions:** FTP user needs write access
- **Check directories:** Run `/setup-machine` first to create directory structure

### Wrong Platform
- **Check registry:** Verify machine platform in `.claude/mcp-servers/classic-mac-hardware/machines.json`
- **Update manually:** Edit machines.json and change `"platform": "mactcp"` or `"opentransport"`

## Implementation Notes

**CRITICAL RULES:**
1. **Read machine config first** - Determine platform from machines.json
2. **Build in single Docker run** - Build and copy in one command to preserve artifacts
3. **Use correct toolchain** - mactcp uses retro68.toolchain.cmake, opentransport uses retroppc.toolchain.cmake
4. **Suppress build noise** - Redirect CMake/make output to /dev/null for cleaner output
5. **Always use MCP tools** - Never use raw FTP operations

**Workflow:**
1. **Check MCP is available** - Call `mcp__classic-mac-hardware__list_machines`
2. **Read machines.json** to get platform
3. **Build LaunchAPPLServer** in Docker:
   - `cd /opt/Retro68/LaunchAPPL`
   - `mkdir -p build-ppc` or `build-mactcp`
   - Run CMake with correct toolchain file
   - Run `make`
   - Copy artifacts to `/workspace/LaunchAPPL-build/`
4. **Build demo apps** (optional - Hello World from Retro68 samples)
5. **Deploy using MCP:**
   - `mcp__classic-mac-hardware__upload_file` for LaunchAPPLServer
   - `mcp__classic-mac-hardware__upload_file` for demo apps
6. **Provide clear instructions**

## Files Created

- `LaunchAPPL-build/LaunchAPPLServer.bin` - Binary format
- `LaunchAPPL-build/LaunchAPPLServer.dsk` - Disk image format
- `LaunchAPPL-build/HelloWorld.bin` - Demo app binary (optional)
- `LaunchAPPL-build/HelloWorld.dsk` - Demo app disk image (optional)

## Related Skills

- `/setup-machine` - Register machine first (prerequisite)
- `/test-machine` - Test LaunchAPPL connectivity after deployment
- `/deploy` - Deploy PeerTalk builds to the Mac
- `/fetch-logs` - Retrieve PT_Log output
