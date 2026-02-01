# Execute Skill - Execution Instructions

Execute applications on Classic Mac via LaunchAPPL remote execution.

## Input

- `machine_id` (required): Machine ID from registry (e.g., "performa6400")
- `app_path` (optional): Path to application binary
  - If not provided and `--list` is given, list available apps
  - Can be relative (e.g., "LaunchAPPL-build/Dialog.bin") or absolute

## Execution Steps

### 1. Parse Arguments

```
Parse $ARGUMENTS:
- If contains "--list": List available apps and exit
- Otherwise: machine_id is first argument, app_path is second argument

Examples:
  $ARGUMENTS = "performa6400 LaunchAPPL-build/Dialog.bin"
    → machine_id = "performa6400"
    → app_path = "LaunchAPPL-build/Dialog.bin"

  $ARGUMENTS = "performa6400 --list"
    → machine_id = "performa6400"
    → list_mode = true
```

### 2. Check Prerequisites

```
Verify MCP server is available:
- Call mcp__classic-mac-hardware__list_machines
- Verify machine_id exists
- If not found, suggest /setup-machine

Read machines.json to get platform:
- Read .claude/mcp-servers/classic-mac-hardware/machines.json
- Extract platform value (mactcp or opentransport)
- Store for later use
```

### 3. Handle List Mode (if --list)

```
If user requested --list:

Check what's available locally:
- LaunchAPPL-build/*.bin (demo apps from /setup-launcher)
- build/mactcp/*.bin (PeerTalk for MacTCP, if exists)
- build/ppc/*.bin (PeerTalk for PPC, if exists)

Display available apps:

Available Apps on <machine-name>
================================

Demo Apps (from /setup-launcher):
  LaunchAPPL-build/Dialog.bin (7 KB)
  LaunchAPPL-build/HelloWorld.bin (if exists)

PeerTalk Apps (if built):
  build/<platform>/PeerTalk.bin (<size>)

Usage:
  /execute <machine-id> <local-path>

Example:
  /execute performa6400 LaunchAPPL-build/Dialog.bin

Exit after displaying list.
```

### 4. Verify Binary Exists

```
If app_path provided:

Check if file exists locally:
- Use relative path from current working directory
- Common locations:
  - LaunchAPPL-build/*.bin (demo apps)
  - build/mactcp/*.bin (MacTCP builds)
  - build/ppc/*.bin (PPC builds)

If not found:
  Show error with suggestions:

  ❌ Binary not found: <app_path>

  Check available apps:
    /execute <machine-id> --list

  Build demo apps:
    /setup-launcher <machine-id>

  Build PeerTalk (once implemented):
    /build package
```

### 5. Execute via MCP

```
Call MCP tool to execute:
- Tool: mcp__classic-mac-hardware__execute_binary
- Arguments:
  - machine: <machine_id>
  - platform: <platform> (from machines.json)
  - binary_path: <app_path> (relative or absolute)

The MCP server will:
1. Verify binary exists locally
2. Connect to LaunchAPPLServer on the Mac (port 1984)
3. Transfer the binary over TCP
4. Execute it on the Mac
5. Return output/result
```

### 6. Display Result

```
✅ Executing on Classic Mac
============================

Machine: <machine-name> (<platform-name>)
Binary: <app_path>
Size: <file-size>

<MCP tool output>

Next steps:
  - Test another demo app: /execute <machine-id> LaunchAPPL-build/HelloWorld.bin
  - Build PeerTalk: /build package
  - Deploy PeerTalk: /deploy <machine-id> <platform>
```

## Error Handling

### Machine Not Found

```
Tell user: "Machine '<machine_id>' not found in registry."
List available machines.
Suggest: "Run /setup-machine to register a new machine."
```

### LaunchAPPL Not Running

```
If MCP returns connection error:

❌ Connection failed to <machine-ip>:1984

LaunchAPPLServer is not running on the Mac.

Steps to fix:
1. Navigate to Applications → LaunchAPPLServer on your Mac
2. Mount LaunchAPPLServer.dsk (double-click)
3. Launch LaunchAPPLServer
4. Enable "TCP Server" on port 1984 in Preferences

Verify connection:
  /test-machine <machine-id>
```

### Binary Not Found

```
❌ Binary not found: <app_path>

The file doesn't exist locally in the Docker container.

Available options:
  - List available apps: /execute <machine-id> --list
  - Build demo apps: /setup-launcher <machine-id>
  - Build PeerTalk: /build package (once implemented)
```

### Platform Mismatch

```
If binary was built for wrong platform:

⚠️  Warning: <app_path> may not run on <machine-platform>

MacTCP binaries only run on 68k Macs.
Open Transport binaries only run on PPC Macs.

Check:
  - Machine platform: <platform> (from machines.json)
  - Build directory: build/<platform>/
```

## Important Notes

1. **Use MCP tool, not direct Docker** - Always use mcp__classic-mac-hardware__execute_binary
2. **Local paths required** - Binary must exist locally in container, not on remote Mac
3. **Relative paths supported** - Can use "LaunchAPPL-build/Dialog.bin" or "build/ppc/PeerTalk.bin"
4. **LaunchAPPLServer must be running** - Server app must be running on Mac (port 1984)

## Examples

### Execute Demo App

```
User: /execute performa6400 LaunchAPPL-build/Dialog.bin

Claude:
✅ Executing on Classic Mac
============================

Machine: performa6400 (Performa 6400/200 - Open Transport)
Binary: LaunchAPPL-build/Dialog.bin
Size: 7 KB

✅ Executed on Performa 6400:
Application launched successfully.

Next steps:
  - Test HelloWorld: /execute performa6400 LaunchAPPL-build/HelloWorld.bin
  - Build PeerTalk: /build package
```

### List Available Apps

```
User: /execute performa6400 --list

Claude:
Available Apps on performa6400
================================

Demo Apps (from /setup-launcher):
  LaunchAPPL-build/Dialog.bin (7 KB)
  LaunchAPPL-build/HelloWorld.bin (15 KB)

PeerTalk Apps:
  (Not built yet - run /build package)

Usage:
  /execute performa6400 <local-path>

Example:
  /execute performa6400 LaunchAPPL-build/Dialog.bin
```

### Execute PeerTalk (Once Implemented)

```
User: /execute se30 build/mactcp/PeerTalk.bin

Claude:
✅ Executing on Classic Mac
============================

Machine: se30 (SE/30 - MacTCP)
Binary: build/mactcp/PeerTalk.bin
Size: 45 KB

✅ Executed on SE/30:
PeerTalk test suite started...
[test output from Mac]

Fetch logs:
  /fetch-logs se30
```
