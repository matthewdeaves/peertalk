---
name: build
description: Build PeerTalk for all platforms with quality checks. Use when compiling code (quick syntax check, full compile, tests), packaging Mac binaries for hardware transfer, or running the complete release pipeline with quality gates.
argument-hint: <mode>
---

# Cross-Platform Build Orchestrator

Build PeerTalk for POSIX, 68k MacTCP, and PPC Open Transport platforms with integrated quality checks.

## Modes

### PeerTalk Builds

| Mode | Description |
|------|-------------|
| `quick` | Syntax check only (fastest) |
| `compile` | Full compile with warnings as errors |
| `test` | POSIX build + tests + coverage report |
| `all` | All platforms (POSIX + 68k + PPC) |
| `package` | Build + create .bin files for Mac transfer |
| `release` | Full pipeline with all quality gates |

### LaunchAPPLServer Builds

| Mode | Description |
|------|-------------|
| `launcher-mactcp` | Build LaunchAPPLServer for MacTCP (68k, System 6.0.8 - 7.5.5) |
| `launcher-ot` | Build LaunchAPPLServer for Open Transport (PPC, System 7.6.1+) |
| `launcher-all` | Build both LaunchAPPLServer versions |

## Usage

### PeerTalk Library
```
/build quick              # Fast syntax check
/build test               # Build + tests + coverage
/build all                # All platforms (POSIX + Mac)
/build package            # Create .bin files for Macs
/build release            # Full pipeline with quality gates
```

### LaunchAPPLServer (Remote Execution)
```
/build launcher-mactcp    # For MacTCP machines (68k)
/build launcher-ot        # For Open Transport machines (PPC)
/build launcher-all       # Build both versions
```

## Process

### quick
Fast syntax check without full compilation:
```bash
gcc -fsyntax-only -Wall -I include src/**/*.c
```

### compile
Full compilation with warnings as errors:
```bash
make clean && make CFLAGS="-Wall -Werror"
```

### test
POSIX build with tests and coverage:
```bash
make clean && make test
make coverage
./tools/build/quality_gates.sh
```

### all
Build for all platforms:
```bash
./tools/build/build_all.sh
```

### package
Build and create transferable Mac binaries:
```bash
./tools/build/build_all.sh
./tools/build/package.sh
```
Creates: `packages/PeerTalk-68k.bin`, `packages/PeerTalk-PPC.bin`

### release
Full pipeline with all quality gates:
```bash
./tools/build/build_all.sh
make test
./tools/build/quality_gates.sh
python tools/validators/isr_safety.py src/
./tools/build/package.sh
```

### launcher-mactcp
Build LaunchAPPLServer for MacTCP (68k):
```bash
./scripts/build-launcher.sh mactcp
```
Creates: `LaunchAPPL/build-mactcp/Server/LaunchAPPLServer-MacTCP.bin`

**Used by:** `/setup-launcher` when deploying to MacTCP machines

### launcher-ot
Build LaunchAPPLServer for Open Transport (PPC):
```bash
./scripts/build-launcher.sh ot
```
Creates: `LaunchAPPL/build-ppc/Server/LaunchAPPLServer-OpenTransport.bin`

**Used by:** `/setup-launcher` when deploying to Open Transport machines

### launcher-all
Build both LaunchAPPLServer versions:
```bash
./scripts/build-launcher.sh both
```
Creates both MacTCP and Open Transport binaries.

**Note:** LaunchAPPLServer is the remote execution server for Classic Macs, not the PeerTalk library itself. See `.claude/mcp-servers/classic-mac-hardware/SETUP.md` for deployment instructions.

## Quality Gates

The orchestrator enforces these quality gates from CLAUDE.md:

| Gate | Threshold | How Checked |
|------|-----------|-------------|
| File size | 500 lines max | wc -l on all .c/.h files |
| Function length | 100 lines max | ctags analysis |
| Coverage | 10% minimum | lcov summary |
| Compiler warnings | Treat as errors | -Werror flag |
| ISR safety | No violations | isr_safety.py |
| Formatting | clang-format | --dry-run --Werror |

## Requirements

### Prerequisite Check

Before running build commands, verify requirements are met:

```bash
.claude/skills/build/scripts/check-build-prereqs.sh
```

This script checks for:
- Required: gcc, make, Docker or $RETRO68
- Optional: lcov, clang-format, ctags, python3

### POSIX Build
- gcc or clang (required)
- make (required)

### Mac Builds (choose one)
- **Docker (recommended):** Run `docker compose -f docker/docker-compose.yml up -d` first
- **Native:** Set `$RETRO68` to local Retro68 installation path
- `Makefile.retro68` in project root (created in Phase 1)

### Quality Checks (optional but recommended)
- lcov (for coverage reports)
- ctags (for function length analysis)
- clang-format (for code formatting)
- Python 3 (for ISR safety validator)

## Output

Build artifacts go to:
- `build/` - Compiled binaries
- `packages/` - MacBinary packages for transfer
- `coverage/` - HTML coverage reports

---

## Example Workflows

### Quick Development Cycle
```bash
# Edit code...
/build quick          # Fast syntax check (~5 sec)
# Fix any errors
/build test           # Full test with coverage (~30 sec)
```

### Preparing for Mac Transfer
```bash
/build test           # Verify POSIX tests pass
/check-isr            # Validate ISR safety
/build package        # Create .bin files
# Transfer to Mac and test
```

### Release Build
```bash
/build release
# Runs:
#   - All platform builds (POSIX + 68k + PPC)
#   - Test suite
#   - Quality gates (coverage, ISR safety, formatting)
#   - Package creation
```

### Debugging Build Failures
```bash
# Check prerequisites first
which gcc && which make && echo $RETRO68
docker compose -f docker/docker-compose.yml ps

# Try modes incrementally
/build quick          # Syntax only
/build compile        # Full compile
/build test           # With tests
```
