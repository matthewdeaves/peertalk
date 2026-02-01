# PeerTalk

Cross-platform peer-to-peer networking library for Classic Macintosh and modern systems.

## Platforms

| Platform | System | Use Case |
|----------|--------|----------|
| POSIX | Linux/macOS | Reference implementation, automated testing |
| MacTCP | System 6.0.8 - 7.5.5 | 68k Macs (SE/30, IIci, LC) |
| Open Transport | System 7.6.1+ / Mac OS 8-9 | PPC Macs, late 68040 |
| AppleTalk | System 6+ | Any Mac with LocalTalk/EtherTalk |

**All Mac testing happens on real hardware**, not emulators.

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
LaunchAPPL/
  Server/             # LaunchAPPLServer source (Retro68 remote execution server)
  Common/             # Shared protocol code
  build-mactcp/       # MacTCP (68k) build artifacts
  build-ppc/          # Open Transport (PPC) build artifacts
scripts/
  build-launcher.sh   # Build LaunchAPPLServer for Mac platforms
```

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

## Platform-Specific Rules

Detailed rules for each platform are in `.claude/rules/`:

- **isr-safety.md** - Universal interrupt-time rules
- **mactcp.md** - MacTCP ASR, error codes, TCPPassiveOpen
- **opentransport.md** - OT notifier, endpoint states, tilisten
- **appletalk.md** - ADSP callbacks, NBP, userFlags clearing

These rules are automatically loaded when editing files in the corresponding `src/` directories.

## Custom Skills

| Skill | When to Use |
|-------|-------------|
| `/session` | Check progress, find next available session |
| `/implement X Y` | Implement a phase session (e.g., `/implement 1 1.2`) |
| `/build test` | Compile and run POSIX tests with coverage |
| `/build package` | Create Mac binaries for hardware transfer |
| `/build launcher-mactcp` | Build LaunchAPPLServer for MacTCP (68k) |
| `/build launcher-ot` | Build LaunchAPPLServer for Open Transport (PPC) |
| `/review plan/PHASE-X.md` | Review plan before starting (recommended for Mac phases) |
| `/check-isr` | Validate interrupt-time safety for Mac code |
| `/hw-test generate X.Y` | Create hardware test plan for Classic Mac |
| `/setup-machine` | Onboard new Classic Mac: add to registry, build & deploy LaunchAPPLServer, test connectivity |
| `/test-machine <id>` | Test FTP and LaunchAPPL connectivity to a Classic Mac |
| `/backport` | Identify commits to cherry-pick to starter-template |
| `/mac-api [query]` | Search Inside Macintosh books for API docs, interrupt safety, error codes |
| `/deploy [machine\|platform\|all]` | Deploy binaries to Classic Mac hardware via FTP |
| `/fetch-logs [machine\|platform\|all]` | Retrieve PT_Log output from Classic Mac hardware |

## MCP Servers

| Server | Purpose |
|--------|---------|
| `classic-mac-hardware` | FTP access to Classic Mac test machines for binary deployment and log retrieval |

**Setup:** See `.claude/mcp-servers/classic-mac-hardware/SETUP.md`

## Agents

| Agent | Auto-Triggers |
|-------|---------------|
| `cross-platform-debug` | "Works on Linux but crashes on SE/30", "Different behavior on Mac vs POSIX" |

**Purpose:** Compares implementations, fetches logs from real hardware via MCP, diagnoses platform differences
