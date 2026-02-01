---
name: execute
description: Execute applications on Classic Mac via LaunchAPPL remote execution. Tests remote execution capability and runs apps without manual interaction.
argument-hint: <machine-id> <app-path>
---

# Execute Apps via LaunchAPPL Remote Execution

Execute applications on Classic Mac hardware using LaunchAPPL remote execution protocol.

## Purpose

- **Automated testing** - Run apps without manual interaction
- **CI/CD integration** - Execute tests on real hardware
- **Remote development** - Test builds from command line
- **Validate LaunchAPPL** - Confirm remote execution works

## Usage

```bash
# Execute demo app
/execute performa6400 Temp:Dialog.bin

# Execute PeerTalk test (once implemented)
/execute se30 Applications:PeerTalk:PeerTalk.bin

# List available apps on machine
/execute performa6400 --list
```

## Prerequisites

1. **LaunchAPPLServer running** on target Mac
   - Install via `/setup-launcher`
   - Enable TCP Server on port 1984
   - Verify with `/test-machine`

2. **App deployed** to Classic Mac
   - Demo apps: Deployed via `/setup-launcher`
   - PeerTalk: Deployed via `/deploy` (once implemented)

3. **MCP server connected**
   - machines.json configured
   - FTP connectivity verified

## How It Works

```
┌─────────────┐     MCP Tool        ┌──────────────┐
│ Claude Code ├──────execute────────>│  MCP Server  │
│             │     binary           │  (Docker)    │
└─────────────┘                      └──────┬───────┘
                                            │
                                            │ LaunchAPPL
                                            │ TCP Transfer
                                            ▼
                                     ┌──────────────┐
                                     │  Classic Mac │
                                     │ LaunchAPPL   │
                                     │   Server     │
                                     │  Port 1984   │
                                     └──────┬───────┘
                                            │
                                            ▼
                                     ┌──────────────┐
                                     │ Execute App  │
                                     │ (Dialog.bin) │
                                     └──────────────┘
```

**Process:**
1. `/execute` skill calls MCP `execute_binary` tool
2. MCP reads local binary from container filesystem
3. Connects to LaunchAPPLServer on Mac (port 1984)
4. Transfers binary over TCP
5. LaunchAPPLServer executes app on Mac

## Commands

### Execute App

```bash
/execute performa6400 LaunchAPPL-build/Dialog.bin
```

**Process:**
1. Read machine config from machines.json
2. Verify binary exists locally in container
3. Call MCP `execute_binary` tool:
   - machine: performa6400
   - platform: opentransport (from machines.json)
   - binary_path: LaunchAPPL-build/Dialog.bin
4. MCP connects to LaunchAPPLServer, transfers binary, executes
5. Show output and result

**Note:** Binary path must be LOCAL to the Docker container, not a remote Mac path.
Use relative paths from /workspace (e.g., "LaunchAPPL-build/Dialog.bin").

**Output:**
```
Executing on Classic Mac
========================

Machine: performa6400 (Performa 6400)
Platform: opentransport
App: Temp:Dialog.bin

Connecting to 10.188.1.102:1984... ✓
Launching application... ✓

✓ Application started successfully

You should see the Dialog window on your Mac!

To test another app:
  /execute performa6400 Temp:HelloWorld.bin

To deploy PeerTalk:
  /build package && /deploy performa6400 opentransport
```

### List Available Apps

```bash
/execute performa6400 --list
```

**Shows apps available for execution:**
```
Available Apps on performa6400
===============================

Demo Apps (from /setup-launcher):
  - Temp:Dialog.bin (7 KB) - Simple dialog window
  - Temp:HelloWorld.bin (if deployed)
  - Temp:Launcher.bin (if deployed)

PeerTalk Apps (once implemented):
  - Applications:PeerTalk:PeerTalk.bin
  - Applications:PeerTalk:ChatDemo.bin

Use: /execute performa6400 <path>
```

## Available Apps

### Demo Apps (Always Available)

After `/setup-launcher`, these demo apps are available:

| App | Path | Description | Size |
|-----|------|-------------|------|
| Dialog | `Temp:Dialog.bin` | Simple dialog window | ~7 KB |
| HelloWorld | `Temp:HelloWorld.bin` | Classic greeting | ~15 KB |
| Launcher | `Temp:Launcher.bin` | File launcher UI | ~20 KB |

### PeerTalk Apps (After Implementation)

Once PeerTalk SDK is built:

| App | Path | Description |
|-----|------|-------------|
| PeerTalk | `Applications:PeerTalk:PeerTalk.bin` | Core library tests |
| ChatDemo | `Applications:PeerTalk:ChatDemo.bin` | Example chat app |

## Troubleshooting

### "Connection refused"
- LaunchAPPLServer not running on Mac
- Check port 1984 is enabled in preferences
- Verify with `/test-machine <machine-id>`

### "Application not found"
- App not deployed to the specified path
- Check available apps with `/execute <machine-id> --list`
- Deploy demos with `/setup-launcher`
- Deploy PeerTalk with `/deploy`

### "LaunchAPPL not found"
- Docker container not running
- Start container: `docker compose -f docker/docker-compose.yml up -d`
- LaunchAPPL client is at `/opt/Retro68-build/toolchain/bin/LaunchAPPL` in container

### "Platform mismatch"
- App compiled for wrong platform
- MacTCP apps only run on 68k Macs
- Open Transport apps only run on PPC Macs
- Check machine platform in machines.json

## Related Skills

- `/setup-launcher` - Deploy LaunchAPPLServer and demo apps (prerequisite)
- `/test-machine` - Verify LaunchAPPL connectivity
- `/deploy` - Deploy PeerTalk binaries via FTP
- `/fetch-logs` - Retrieve logs after execution

## Comparison: /execute vs /deploy

| Feature | `/execute` | `/deploy` |
|---------|-----------|-----------|
| **Method** | Remote execution | FTP file transfer |
| **User action** | Automated launch | Manual launch |
| **Use case** | Testing, CI/CD | Distribution, install |
| **Requires** | LaunchAPPL running | FTP server only |
| **Speed** | Instant execution | Manual extraction |

**Typical workflow:**
1. `/setup-launcher` - Set up LaunchAPPL and test with demos
2. `/execute` - Test demo apps remotely
3. `/build package` - Build PeerTalk (once implemented)
4. `/deploy` - Transfer PeerTalk to Mac
5. `/execute` - Run PeerTalk tests remotely
6. `/fetch-logs` - Get test results

## Example Session

```
User: /execute performa6400 LaunchAPPL-build/Dialog.bin

Claude: Executing Dialog demo app on Performa 6400...

✅ Executing on Classic Mac
============================

Machine: performa6400 (Performa 6400/200 - Open Transport)
Binary: LaunchAPPL-build/Dialog.bin
Size: 7 KB

✅ Executed on Performa 6400:
Application launched successfully.

Next steps:
  - Test other demos: /execute performa6400 --list
  - Build PeerTalk: /build package
  - Start implementing: /session next
```
