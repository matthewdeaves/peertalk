# PeerTalk

Cross-platform peer-to-peer networking library for Classic Macintosh and modern systems.

## Project Status & Purpose

**Current State:** Starter template with world-class Claude Code configuration

This repository serves two purposes:

1. **Implementation Project** - Build the PeerTalk SDK using phase plans in `plan/`
2. **Learning Resource** - Real-world example of Claude Code customization (MCP, skills, hooks, tools)

**What's Implemented:**
- ✓ Phase plans ready in `plan/PHASE-*.md`
- ✓ Custom skills for development workflow
- ✓ MCP server for Classic Mac hardware access
- ✓ Pre-commit hooks and quality gates
- ✓ Docker environment with Retro68 toolchain

**Not Yet Implemented:**
- ⏳ PeerTalk SDK library (`src/`, `include/`) - networking/logging APIs for other apps to use
- ⏳ Example chat application - demo app using the SDK to show peer-to-peer messaging
- ⏳ Platform-specific implementations (MacTCP, Open Transport, AppleTalk)

**Getting Started:**
```bash
/session status   # Check project progress
/session next     # Find next implementation task
/implement        # Start implementing from plans
```

## Platforms

| Platform | System | Use Case |
|----------|--------|----------|
| POSIX | Linux/macOS | Reference implementation, automated testing |
| MacTCP | System 6.0.8 - 7.5.5 | 68k Macs (SE/30, IIci, LC) |
| Open Transport | System 7.6.1+ / Mac OS 8-9 | PPC Macs, late 68040 |
| AppleTalk | System 6+ | Mac-to-Mac (MacTCP/OT ↔ AppleTalk peers only) |

**All Mac testing happens on real hardware**, not emulators.

## Build Environment

**CRITICAL: All building and testing MUST happen inside Docker containers, never on the host.**

```bash
# Correct - always use Docker
docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest make test
docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest make coverage

# Or use the /build skill which handles Docker automatically
/build test
/build coverage
```

See `.claude/rules/build-requirements.md` for complete Docker command reference.

## Code Quality Gates

| Gate | Threshold |
|------|-----------|
| Max function length | 100 lines (prefer 50) |
| Max file size | 500 lines |
| Coverage target | 10% minimum (POSIX) |
| Compiler warnings | Treat as errors |
| Cyclomatic complexity | 15 max per function |

## Protocol Constants

| Constant | Value | Purpose |
|----------|-------|---------|
| Discovery magic | `"PTLK"` | UDP discovery packets |
| Message magic | `"PTMG"` | TCP message frames |
| Discovery port | 7353 | UDP broadcast |
| Default TCP port | 7354 | TCP connections |
| Default UDP port | 7355 | UDP messaging |

## Magic Numbers

| Constant | Value | Purpose |
|----------|-------|---------|
| PT_CONTEXT_MAGIC | 0x5054434E | "PTCN" - context validation |
| PT_PEER_MAGIC | 0x50545052 | "PTPR" - peer validation |
| PT_QUEUE_MAGIC | 0x50545155 | "PTQU" - queue validation |
| PT_CANARY | 0xDEADBEEF | Buffer overflow detection |

## File Structure

```
include/
  peertalk.h          # Public API
  pt_log.h            # Logging API
src/
  core/               # Platform-independent
  posix/              # Linux/macOS
  mactcp/             # MacTCP (68k)
  opentransport/      # Open Transport (PPC)
  appletalk/          # AppleTalk (all Macs)
  log/                # PT_Log implementation
tests/
  test_*.c            # POSIX tests
plan/
  PHASE-*.md          # Implementation plans
.claude/
  skills/             # Custom Claude Code skills
  rules/              # Platform-specific coding rules
  mcp-servers/        # MCP server configurations
    classic-mac-hardware/
      machines.json   # Classic Mac machine registry
      SETUP.md        # MCP server setup guide
scripts/
  build-launcher.sh   # Build LaunchAPPLServer for Mac platforms
docker/
  docker-compose.yml  # Retro68 development container
```

**LaunchAPPL Architecture:**
- **Client** at `/opt/Retro68-build/toolchain/bin/LaunchAPPL` (in Docker container)
- **Server** built from `/opt/Retro68/LaunchAPPL/Server/` and deployed to Classic Mac
- **Protocol:** Client reads local .bin file → transfers via TCP (port 1984) → Server executes on Mac
- **Used by:** `/execute` skill and MCP `execute_binary` tool

## Common Pitfalls

1. **Allocating in ASR/notifier** - Crashes. Use pre-allocated buffers.
2. **Forgetting TCPRcvBfrReturn** - Leaks MacTCP buffers.
3. **Wrong byte order** - Use htonl/ntohl for network data.
4. **TCPPassiveOpen re-use** - It's one-shot. Need stream transfer pattern.
5. **Testing only in emulator** - Real hardware behaves differently.

## Development Resources

### Retro68 (Cross-Compiler)

Classic Mac builds use the Retro68 cross-compiler (typically run in a Docker container).

Key headers in `InterfacesAndLibraries/MPW_Interfaces/.../CIncludes/`:
- `MacTCP.h`, `OpenTransport.h`, `OpenTptInternet.h`
- `AppleTalk.h`, `ADSP.h`
- `MacMemory.h`, `Gestalt.h`

### Reference Books

Path: `~/peertalk/books/`

| Book | Use For |
|------|---------|
| MacTCP Programmer's Guide | ASR rules, parameter blocks |
| Networking With Open Transport | Notifiers, endpoints, tilisten |
| Inside Macintosh Volume VI | Table B-3 (interrupt-safe routines) |
| Programming With AppleTalk | NBP discovery, ADSP connections |

## Development Rules

Detailed rules are in `.claude/rules/`:

- **build-requirements.md** - Docker-only builds (CRITICAL - always use Docker)
- **isr-safety.md** - Universal interrupt-time rules
- **mactcp.md** - MacTCP ASR, error codes, TCPPassiveOpen
- **opentransport.md** - OT notifier, endpoint states, tilisten
- **appletalk.md** - ADSP callbacks, NBP, userFlags clearing

Platform rules are automatically loaded when editing files in the corresponding `src/` directories.

## Custom Skills

### Development Workflow
| Skill | When to Use |
|-------|-------------|
| `/session` | Check progress, find next available session |
| `/implement X Y` | Implement a phase session (e.g., `/implement 1 1.2`) |
| `/review plan/PHASE-X.md` | Review plan before starting (recommended for Mac phases) |
| `/check-isr` | Validate interrupt-time safety for Mac code |

### Building & Testing
| Skill | When to Use |
|-------|-------------|
| `/build test` | Compile and run POSIX tests with coverage |
| `/build package` | Create Mac binaries for hardware transfer |
| `/hw-test generate X.Y` | Create hardware test plan for Classic Mac |

### Hardware Setup & Deployment
| Skill | When to Use |
|-------|-------------|
| `/setup-machine` | Register new Classic Mac in machine registry, verify FTP connectivity |
| `/setup-launcher <machine>` | Build & deploy LaunchAPPLServer and demo apps to registered Mac |
| `/test-machine <id>` | Test FTP and LaunchAPPL connectivity |
| `/deploy [machine\|platform\|all]` | Deploy PeerTalk binaries via FTP (requires PeerTalk implemented) |
| `/execute <machine> <app-path>` | Run apps remotely via LaunchAPPL (tests without PeerTalk) |
| `/fetch-logs [machine\|platform\|all]` | Retrieve PT_Log output from Classic Mac hardware |

### Reference & Documentation
| Skill | When to Use |
|-------|-------------|
| `/mac-api [query]` | Search Inside Macintosh books for API docs, interrupt safety, error codes |
| `/backport` | Identify commits to cherry-pick to starter-template |

**Setup Workflow:**
1. `/setup-machine` - Register your Mac and create directory structure
2. `/setup-launcher <machine>` - Build and deploy LaunchAPPLServer
3. Run LaunchAPPLServer on your Mac (enable TCP server on port 1984)
4. `/test-machine <machine>` - Verify LaunchAPPL connectivity
5. `/deploy <machine> <platform>` - Deploy PeerTalk builds

## MCP Servers

| Server | Purpose |
|--------|---------|
| `classic-mac-hardware` | FTP access to Classic Mac test machines for binary deployment, log retrieval, and file transfer |

**Key Tools:**
- `deploy_binary` - Deploy PeerTalk builds (.bin/.dsk) via FTP (supports relative paths)
- `execute_binary` - Run apps remotely via LaunchAPPL TCP protocol (supports relative paths)
- `fetch_logs` - Retrieve PT_Log output from Mac
- `upload_file` / `download_file` - Transfer any file to/from Classic Mac (relative paths)
- `list_directory` / `create_directory` / `delete_files` - File management
- `test_connection` - Verify FTP and LaunchAPPL connectivity
- `reload_config` - Hot-reload machine registry after changes

**Path Support:** All file operations support relative paths from `/workspace` (e.g., `LaunchAPPL-build/Dialog.bin`)

**Machine Registry:** `.claude/mcp-servers/classic-mac-hardware/machines.json`

Each machine entry includes:
- Platform (mactcp/opentransport)
- FTP credentials and paths
- System version and hardware details

**Setup:** Run `./tools/setup.sh` (sets up Docker + MCP configuration), then restart Claude Code.

**Detailed docs:** `.claude/mcp-servers/classic-mac-hardware/SETUP.md`

## .claude Folder Organization

```
.claude/
  skills/                      # Custom Claude Code skills
    setup-machine/             # Register Classic Mac in machine registry
    setup-launcher/            # Build & deploy LaunchAPPLServer
    deploy/                    # Deploy PeerTalk builds via FTP
    execute/                   # Remote execution via LaunchAPPL
    fetch-logs/                # Retrieve PT_Log output
    test-machine/              # Test FTP/LaunchAPPL connectivity
    build/                     # Build system with quality gates
    session/                   # Phase session navigation
    implement/                 # Automated phase implementation
    review/                    # Phase plan review & validation
    check-isr/                 # ISR safety validation
    hw-test/                   # Hardware test plan generation
    mac-api/                   # Inside Macintosh API search
    backport/                  # Cherry-pick tooling updates
  rules/                       # Development and platform rules
    build-requirements.md      # Docker-only builds (CRITICAL)
    isr-safety.md              # Universal interrupt-time rules
    mactcp.md                  # MacTCP ASR, TCPPassiveOpen, error codes
    opentransport.md           # OT notifier, endpoint states, tilisten
    appletalk.md               # ADSP callbacks, NBP, userFlags
  mcp-servers/                 # MCP server configurations
    classic-mac-hardware/
      machines.json            # Machine registry (FTP credentials, paths)
      SETUP.md                 # Setup guide
```

**Auto-loading rules:** When editing files in `src/mactcp/`, `src/opentransport/`, or `src/appletalk/`, the corresponding platform rules are automatically loaded.

## Agents

| Agent | Auto-Triggers |
|-------|---------------|
| `cross-platform-debug` | "Works on Linux but crashes on SE/30", "Different behavior on Mac vs POSIX" |

**Purpose:** Compares implementations, fetches logs from real hardware via MCP, diagnoses platform differences
