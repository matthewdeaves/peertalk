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

**NEVER run directly on host:**
- `gcc`, `g++`, `make`, `cmake` - always wrap with Docker
- `./build/bin/*` - run POSIX binaries inside containers (Mac binaries run on real hardware via MCP)
- `cppcheck`, `valgrind` - use Docker for all analysis tools

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
include/                    # Public API headers
  peertalk.h                  # Main PeerTalk API
  pt_log.h                    # Logging API
src/
  core/                       # Platform-independent code
  posix/                      # Linux/macOS implementation
  mactcp/                     # MacTCP (68k) implementation
  opentransport/              # Open Transport (PPC) implementation
  appletalk/                  # AppleTalk implementation
  log/                        # PT_Log (POSIX + Mac)
tests/
  test_*.c                    # POSIX unit tests
  posix/                      # POSIX test partners (perf_partner, test_partner)
  mac/                        # Mac hardware test apps (test_throughput, etc.)
  hw/                         # Hardware test plans
plan/                         # Implementation phase plans
  PHASE-*.md
books/                        # Reference documentation (Inside Macintosh, etc.)
docker/                       # Docker configurations
  Dockerfile                  # Full dev image (Retro68 + tools)
  Dockerfile.posix            # Lightweight POSIX-only image
  docker-compose.yml          # Development container
  docker-compose.test.yml     # 3-peer integration test
tools/
  build/                      # Build scripts and quality gates
  validators/                 # ISR safety checker, etc.
  metrics/                    # Code metrics extraction
scripts/                      # Build and utility scripts
  build-mac-tests.sh            # Build Mac test apps for hardware testing
  build-launcher.sh             # Build LaunchAPPLServer for remote execution
.claude/
  skills/                     # Custom Claude Code skills
  rules/                      # Platform-specific coding rules
  mcp-servers/                # MCP server for Classic Mac hardware
    classic-mac-hardware/
      machines.json           # Machine registry (gitignored - user config)
      machines.example.json   # Example configuration
```

**Generated directories (gitignored):**
- `build/` - Compiled libraries and test binaries
- `downloads/` - Temporary logs fetched from Classic Mac via FTP (use for debugging)
- `packages/` - Mac binaries packaged for transfer
- `LaunchAPPL-build/` - Built LaunchAPPLServer binaries

**Performance logs (committed to git):**
- `plan/performance/mactcp/performa6200/` - Performa 6200 (8MB RAM) test results
- `plan/performance/mactcp/macse/` - Mac SE (4MB RAM) test results
- Test apps auto-stream logs to POSIX partner; perf_partner saves them by machine IP

**LaunchAPPL Architecture:**
- **Client** at `/opt/Retro68-build/toolchain/bin/LaunchAPPL` (in Docker container)
- **Server** built from `/opt/Retro68/LaunchAPPL/Server/` and deployed to Classic Mac
- **Protocol:** Client reads local .bin file → transfers via TCP (port 1984) → Server executes on Mac
- **Used by:** `/execute` skill and MCP `execute_binary` tool

## Hardware Test Applications

### Mac Test Apps (built with Retro68)

| App | Log File | Purpose |
|-----|----------|---------|
| `test_latency` | `PT_Latency` | RTT measurement with various message sizes (16-4096 bytes) |
| `test_throughput` | `PT_Throughput` | Bidirectional throughput (echo-based) |
| `test_stream` | `PT_Stream` | One-way streaming (true unidirectional capacity) |
| `test_stress` | `PT_Stress` | Rapid connect/disconnect cycles |
| `test_discovery` | `PT_Discovery` | Extended discovery packet counting |

**Build:**
```bash
# Using the build script (recommended)
./scripts/build-mac-tests.sh mactcp

# Or manually with Docker
docker-compose -f docker/docker-compose.yml run --rm peertalk-dev \
    make -f Makefile.retro68 PLATFORM=mactcp test perf_tests
```

**Deploy to Mac:**
```bash
# Via MCP (preferred)
mcp__classic-mac-hardware__upload_file(machine="performa6200", local_path="build/mac/test_latency.bin", remote_path="test_latency.bin")

# Or execute directly via LaunchAPPL
mcp__classic-mac-hardware__execute_binary(machine="performa6200", platform="mactcp", binary_path="build/mac/test_latency.bin")
```

### POSIX Test Partners

| App | Purpose |
|-----|---------|
| `test_partner` | Basic discovery partner, sends canned responses |
| `perf_partner` | **Universal test partner** - auto-detects ALL test types |

**Build:**
```bash
docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest make build/bin/perf_partner
```

**Run (must use host networking for UDP broadcast):**
```bash
# Start once - handles ALL test types automatically
docker run -d --name perf-partner --network host \
    -v "$(pwd)":/workspace -w /workspace \
    -e MACHINE_REGISTRY="10.188.1.55:macse,10.188.1.213:performa6200" \
    peertalk-posix:latest ./build/bin/perf_partner --verbose
```

**Auto-detection:** The partner detects test type from Mac messages:
- Regular messages → echoed back (latency, throughput tests)
- STRM control messages → auto-switches to sink/stream mode (stream test)
- No manual `--mode` switching needed between tests!

### Testing Workflow

**Simplest approach - use the /run-test skill:**
```bash
/run-test throughput performa6200   # Run throughput test on Performa 6200
/run-test stream macse              # Run one-way stream test on Mac SE
/run-test all performa6200          # Run all tests sequentially
```

**Manual workflow:**

1. **Build and start partner once** (handles ALL tests):
   ```bash
   docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest make build/bin/perf_partner
   docker run -d --name perf-partner --network host \
       -v "$(pwd)":/workspace -w /workspace \
       peertalk-posix:latest ./build/bin/perf_partner --verbose
   ```

2. **Build Mac test apps:**
   ```bash
   ./scripts/build-mac-tests.sh mactcp
   ```

3. **Run any test via LaunchAPPL:**
   ```bash
   mcp__classic-mac-hardware__execute_binary(machine="performa6200",
       platform="mactcp", binary_path="build/mac/test_throughput.bin")
   ```

4. **Logs auto-saved** to `plan/performance/mactcp/<machine>/`

5. **Stop partner when done:**
   ```bash
   docker stop perf-partner && docker rm perf-partner
   ```

6. **Stop partner when done**:
   ```bash
   docker stop perf-partner && docker rm perf-partner
   ```

**IMPORTANT: Log Streaming Behavior**

Mac test apps automatically stream their logs to the POSIX perf_partner at the end of the test. This is critical for:
- Machines without FTP (e.g., Mac SE with LaunchAPPL only)
- Capturing test results before the Mac test app exits

**Always capture perf_partner logs before stopping the container**, especially for LaunchAPPL-only machines. Save performance logs to `plan/performance/mactcp/` with descriptive filenames.

**Skill shortcuts:**
```
/test-partner start          # Start partner (auto-handles all test types)
/test-partner status         # Check if running
/test-partner stop           # Stop when done
/run-test throughput         # Full automated test workflow
```

**Note:** Mac apps use ports 7353 (discovery) and 7354 (TCP). The POSIX partner must use the same ports.

## Common Pitfalls

1. **Allocating in ASR/notifier** - Crashes. Use pre-allocated buffers.
2. **Forgetting TCPRcvBfrReturn** - Leaks MacTCP buffers.
3. **Wrong byte order** - Use htonl/ntohl for network data.
4. **TCPPassiveOpen re-use** - It's one-shot. Need stream transfer pattern.
5. **Testing only in emulator** - Real hardware behaves differently.
6. **Fresh logs each run** - Mac test apps now clear their log files at startup using `PT_LogClearFile()`. Each log contains only data from that run - no stale data from previous runs.
7. **Mac SE memory limits** - CRITICAL: Mac SE (4MB RAM) REQUIRES `*_lowmem.bin` builds! Standard builds request 2-3MB heap and won't launch. Use `make -f Makefile.retro68 PLATFORM=mactcp lowmem_tests` for Mac SE.

## Build Scripts

| Script | Purpose | Output |
|--------|---------|--------|
| `./scripts/build-mac-tests.sh mactcp` | Build all Mac test apps | `build/mac/test_*.bin` |
| `./scripts/build-mac-tests.sh mactcp perf` | Build perf tests only | `build/mac/test_{latency,throughput,stream,stress,discovery}.bin` |
| `./scripts/build-launcher.sh mactcp` | Build LaunchAPPLServer (68k) | `LaunchAPPL-build/LaunchAPPLServer-MacTCP.bin` |
| `./scripts/build-launcher.sh ot` | Build LaunchAPPLServer (PPC) | `LaunchAPPL-build/LaunchAPPLServer-OpenTransport.bin` |
| `./tools/build/build_all.sh all` | Build PeerTalk SDK for all platforms | `build/`, `packages/` |
| `./tools/build/package.sh` | Package Mac binaries for transfer | `packages/PeerTalk-*.bin` |

**Note:** All build scripts automatically use Docker - no host toolchain required.

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
- **classic-mac-hardware.md** - MCP-only file operations (CRITICAL - never use raw FTP)
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
| `/run-test <test> [machine]` | Full hardware test workflow: build, execute, collect logs, analyze |
| `/perf-optimize [machine]` | Autonomous optimization cycle: test → analyze → implement → verify |
| `/hw-test generate X.Y` | Create hardware test plan for Classic Mac |
| `/test-partner start` | Start POSIX test partner (auto-handles all test types) |
| `/test-partner stop` | Stop test partner container |
| `/test-partner status` | Check if partner is running |

### Hardware Setup & Deployment
| Skill | When to Use |
|-------|-------------|
| `/setup-machine` | Register new Classic Mac in machine registry, verify FTP connectivity |
| `/setup-launcher <machine>` | Build & deploy LaunchAPPLServer and demo apps to registered Mac |
| `/test-machine <id>` | Test FTP and LaunchAPPL connectivity |
| `/deploy [machine\|platform\|all]` | Deploy PeerTalk binaries via FTP (requires PeerTalk implemented) |
| `/execute <machine> <app-path>` | Run apps remotely via LaunchAPPL (tests without PeerTalk) |

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

**CRITICAL: Always use MCP tools for Classic Mac file operations. NEVER use raw FTP scripts or bash commands.**

See `.claude/rules/classic-mac-hardware.md` for complete enforcement rules.

**Key Tools:**
- `upload_file` - Upload any file to Classic Mac (preserves filename)
- `download_file` - Download file from Classic Mac (use for PT_Log if needed)
- `execute_binary` - Run apps remotely via LaunchAPPL TCP protocol
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
    test-machine/              # Test FTP/LaunchAPPL connectivity
    build/                     # Build system with quality gates
    session/                   # Phase session navigation
    implement/                 # Automated phase implementation
    review/                    # Phase plan review & validation
    check-isr/                 # ISR safety validation
    hw-test/                   # Hardware test plan generation
    mac-api/                   # Inside Macintosh API search
    backport/                  # Cherry-pick tooling updates
    test-partner/              # Manage POSIX test partner containers
    run-test/                  # Full hardware test workflow (logs auto-streamed)
  rules/                       # Development and platform rules
    build-requirements.md      # Docker-only builds (CRITICAL)
    classic-mac-hardware.md    # MCP-only file operations (CRITICAL)
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
