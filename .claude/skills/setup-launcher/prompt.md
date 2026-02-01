# Setup Launcher Skill - Execution Instructions

Build and deploy LaunchAPPLServer and Retro68 demo applications to a registered Classic Mac.

## Input

- `machine_id` (optional): The machine ID from the registry (e.g., "performa6400")
- If not provided, list available machines and ask user to select one

## Execution Steps

### 1. Check Prerequisites

```
Check MCP server is available:
- Call mcp__classic-mac-hardware__list_machines
- If no machines registered, tell user to run /setup-machine first
```

### 2. Get Machine Information

```
If machine_id provided:
- Verify it exists in the list
- If not found, show available machines and ask user to select

If machine_id not provided:
- Show available machines
- Ask user which machine to deploy to

Read machines.json to get platform and IP:
- Read .claude/mcp-servers/classic-mac-hardware/machines.json
- Extract platform value (mactcp or opentransport)
- Extract host IP address
- Store for later use

Set paths based on platform:
- If platform == "mactcp":
  - LAUNCHAPPL_CLIENT_PATH=/opt/Retro68/LaunchAPPL/build-m68k/Client/LaunchAPPLClient
  - TOOLCHAIN_FILE=/opt/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake

- If platform == "opentransport":
  - LAUNCHAPPL_CLIENT_PATH=/opt/Retro68/LaunchAPPL/build-ppc/Client/LaunchAPPLClient
  - TOOLCHAIN_FILE=/opt/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake
```

### 2.5. Verify/Create Directory Structure

**CRITICAL: Check directories exist before deploying**

```
Check if required directories exist:
- Call mcp__classic-mac-hardware__list_directory with path "/"
- Check for: Applications, Temp

If directories missing, create them:
- mcp__classic-mac-hardware__create_directory path="Applications"
- mcp__classic-mac-hardware__create_directory path="Applications:LaunchAPPLServer"
- mcp__classic-mac-hardware__create_directory path="Temp"
- mcp__classic-mac-hardware__create_directory path="Documents"
- mcp__classic-mac-hardware__create_directory path="Documents:PeerTalk-Logs"

This prevents "Directory not found" errors during upload.
```

### 3. Build LaunchAPPLServer

**CRITICAL:** Build, copy from Server/ subdirectory, and list artifacts in a SINGLE docker run command.

**For MacTCP (68k):**
```bash
docker compose -f docker/docker-compose.yml run --rm peertalk-dev bash -c "
set -e
cd /opt/Retro68/LaunchAPPL
rm -rf build-mactcp
mkdir -p build-mactcp
cd build-mactcp

echo 'Building LaunchAPPLServer for MacTCP...'
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=/opt/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1

make -j\$(nproc) > /dev/null 2>&1

echo 'Copying artifacts from Server/ subdirectory...'
mkdir -p /workspace/LaunchAPPL-build
cp Server/LaunchAPPLServer.bin /workspace/LaunchAPPL-build/
cp Server/LaunchAPPLServer.dsk /workspace/LaunchAPPL-build/

echo 'LaunchAPPLServer build complete!'
ls -lh /workspace/LaunchAPPL-build/LaunchAPPLServer.*
"
```

**For Open Transport (PPC):**
```bash
docker compose -f docker/docker-compose.yml run --rm peertalk-dev bash -c "
set -e
cd /opt/Retro68/LaunchAPPL
rm -rf build-ppc
mkdir -p build-ppc
cd build-ppc

echo 'Building LaunchAPPLServer for Open Transport...'
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=/opt/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1

make -j\$(nproc) > /dev/null 2>&1

echo 'Copying artifacts from Server/ subdirectory...'
mkdir -p /workspace/LaunchAPPL-build
cp Server/LaunchAPPLServer.bin /workspace/LaunchAPPL-build/
cp Server/LaunchAPPLServer.dsk /workspace/LaunchAPPL-build/

echo 'LaunchAPPLServer build complete!'
ls -lh /workspace/LaunchAPPL-build/LaunchAPPLServer.*
"
```

### 4. Build Demo App

**Build a simple demo app to test LaunchAPPL remote execution.**

Build Dialog or HelloWorld from Retro68 Samples in the SAME Docker command:

```bash
docker compose -f docker/docker-compose.yml run --rm peertalk-dev bash -c "
set -e

echo 'Building demo app (Dialog)...'

# Use the appropriate toolchain based on platform
cd /opt/Retro68/Samples/Dialog
rm -rf build
mkdir -p build
cd build

# For MacTCP (68k):
# cmake .. -DCMAKE_TOOLCHAIN_FILE=/opt/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake

# For Open Transport (PPC):
cmake .. -DCMAKE_TOOLCHAIN_FILE=/opt/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake

make -j\$(nproc) > /dev/null 2>&1

echo 'Copying demo app artifacts...'
cp Dialog.bin /workspace/LaunchAPPL-build/
cp Dialog.dsk /workspace/LaunchAPPL-build/

echo 'Demo app build complete!'
ls -lh /workspace/LaunchAPPL-build/*.bin /workspace/LaunchAPPL-build/*.dsk
"
```

**Available demo apps:**
- **Dialog** - Shows a simple dialog window (quick test, ~7KB)
- **HelloWorld** - Classic "Hello, World!" window
- **Launcher** - File launcher interface

Choose Dialog as it's the simplest test of remote execution.

### 5. Deploy to Mac

Use MCP upload_file tool with **relative paths** (required by MCP):

```
**IMPORTANT:** MCP requires relative paths from current working directory (/workspace)

Upload LaunchAPPLServer.bin:
- local_path: LaunchAPPL-build/LaunchAPPLServer.bin (relative path)
- remote_path: Applications:LaunchAPPLServer:LaunchAPPLServer.bin

Upload LaunchAPPLServer.dsk:
- local_path: LaunchAPPL-build/LaunchAPPLServer.dsk (relative path)
- remote_path: Applications:LaunchAPPLServer:LaunchAPPLServer.dsk

Upload demo apps (Dialog.bin, Dialog.dsk):
- local_path: LaunchAPPL-build/Dialog.bin
- remote_path: Temp:Dialog.bin
```

### 6. Verify Deployment

```
List the directory to confirm files are there:
- Call mcp__classic-mac-hardware__list_directory
- path: Applications:LaunchAPPLServer
```

### 7. Ask User How to Test Demo App

**CRITICAL: Offer user choice for testing method before providing instructions.**

Use AskUserQuestion to present testing options:

```
Ask user: "How would you like to test the demo app?"

Options:
1. "Remote execution via LaunchAPPL" (Recommended)
   - Executes app on Mac from command line
   - Tests LaunchAPPLClient → LaunchAPPLServer communication
   - Automated, good for CI/CD

2. "Manual test on the Mac"
   - Mount .dsk file and run locally
   - Visual verification
   - Good for checking GUI behavior

3. "Both methods"
   - Test remote execution first, then show manual steps
```

### 8. Provide Instructions Based on Choice

**If user chose "Remote execution" or "Both":**

```
✓ LaunchAPPLServer deployed to <machine-name>
✓ Demo app deployed: <app-name>

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
STEP 1: Install and Run LaunchAPPLServer
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

On your Classic Mac:
1. Navigate to Applications → LaunchAPPLServer
2. Mount LaunchAPPLServer.dsk (double-click)
3. Copy LaunchAPPLServer to Applications
4. Launch LaunchAPPLServer
5. Preferences → Enable "TCP Server" on port 1984

Verify it's running:
  /test-machine <machine-id>

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
STEP 2: Test Remote Execution
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Execute the demo app remotely:

  /execute <machine-id> LaunchAPPL-build/Dialog.bin

Example for performa6400:
  /execute performa6400 LaunchAPPL-build/Dialog.bin

You should see the Dialog window appear on your Mac!

Demo Apps Available:
  - LaunchAPPL-build/Dialog.bin    - Simple dialog window (~7KB)
  - LaunchAPPL-build/HelloWorld.bin - Classic greeting (if built)
  - LaunchAPPL-build/Launcher.bin   - File launcher (if built)

List all available apps:
  /execute <machine-id> --list
```

**If user chose "Manual test":**

```
✓ Demo app deployed to <machine-name>:Temp

Manual Testing Steps:
1. On your Mac, navigate to Temp folder
2. Mount <demo-app>.dsk (double-click)
3. Run the application
4. You should see a window with the demo

Files available:
  - Dialog.dsk    - Simple dialog window
  - HelloWorld.dsk - Classic greeting (if built)
```

**Always end with:**

```
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Next Steps
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Test remote execution with demo apps:
  /execute <machine-id> LaunchAPPL-build/Dialog.bin
  /execute <machine-id> --list              # See all available apps

Once ready to build PeerTalk:
  /session next                             # Start implementing SDK
  /build package                            # Build for Mac platforms
  /deploy <machine-id> <platform>           # Deploy to Mac
  /execute <machine-id> build/<platform>/PeerTalk.bin

Your Classic Mac is ready for development!
```

## Error Handling

### Machine Not Found
```
Tell user: "Machine '{machine_id}' not found in registry."
List available machines.
Suggest: "Run /setup-machine to register a new machine."
```

### Build Failed
```
Check error output.
Common issues:
- Docker not running
- Toolchain not built
- Out of memory

Suggest:
1. Check Docker is running: docker ps
2. Rebuild toolchain: ./tools/setup.sh
```

### Upload Failed
```
Check error output.
Common issues:
- FTP credentials wrong
- Disk full on Mac
- Directory doesn't exist

Suggest:
1. Run /setup-machine again to recreate directories
2. Check FTP credentials in machines.json
3. Check disk space on Mac
```

## Platform Detection

Map platform to toolchain:

| Platform | Toolchain File |
|----------|---------------|
| mactcp | `/opt/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake` |
| opentransport | `/opt/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake` |

## Output Format

Keep output concise and clear:
- Use ✓ for completed steps
- Show file sizes for built artifacts
- Provide actionable next steps
- Don't show verbose build output (redirect to /dev/null)

## CRITICAL RULES

1. **Check/create directories BEFORE deploying** - Verify Applications:LaunchAPPLServer and Temp exist, create if missing
2. **ALWAYS build and copy in single docker run** - Artifacts don't persist between runs, copy from Server/ subdirectory
3. **Read platform from machines.json** - Don't ask user, read it from the registry
4. **Use correct toolchain for platform** - mactcp = retro68, opentransport = retroppc
5. **Always use MCP tools for FTP** - Never use raw bash/python FTP
6. **Verify deployment** - List directory to confirm files uploaded
7. **Prioritize testing remote execution** - Next steps should focus on LaunchAPPLClient test, NOT /deploy
