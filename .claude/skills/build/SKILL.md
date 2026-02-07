---
name: build
description: Build PeerTalk for all platforms with quality checks. Use when compiling code (quick syntax check, full compile, tests), packaging Mac binaries for hardware transfer, or running the complete release pipeline with quality gates.
argument-hint: <mode>
---

# Cross-Platform Build Orchestrator

Build PeerTalk for POSIX, 68k MacTCP, and PPC Open Transport platforms with integrated quality checks.

**All builds run inside Docker containers** per project requirements.

## Quick Reference

| Mode | What It Does |
|------|--------------|
| `quick` | Fast syntax check (~5 sec) |
| `compile` | Full compile with `-Werror` |
| `test` | Build + tests + coverage |
| `coverage` | Tests with HTML coverage report |
| `analyze` | Static analysis (complexity, cppcheck, duplicates) |
| `all` | All platforms (POSIX + 68k + PPC) |
| `package` | Create Mac `.bin` files for transfer |
| `release` | Full pipeline with all quality gates |
| `launcher-mactcp` | Build LaunchAPPLServer for 68k |
| `launcher-ot` | Build LaunchAPPLServer for PPC |
| `launcher-all` | Build both LaunchAPPLServer versions |

## Docker Images

| Image | Size | Use For |
|-------|------|---------|
| `peertalk-posix:latest` | ~580MB | POSIX builds, tests, coverage |
| `ghcr.io/matthewdeaves/peertalk-dev:develop` | ~2.5GB | Mac builds (Retro68 toolchain) |

Pull images if needed:
```bash
docker pull ghcr.io/matthewdeaves/peertalk-dev:develop
# Or build locally: docker build -t peertalk-posix -f docker/Dockerfile.posix .
```

---

## Mode Details

### quick
Fast syntax check without full compilation:
```bash
docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest \
    gcc -fsyntax-only -Wall -Wextra -I include -I src/core \
    src/core/*.c src/posix/*.c src/log/*.c
```

### compile
Full compilation with warnings as errors (uses Makefile defaults):
```bash
docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest \
    make clean all
```

### test
Build and run all POSIX tests:
```bash
make docker-test
# Equivalent to:
# docker run --rm -v "$(pwd)":/workspace -w /workspace \
#     -u $(id -u):$(id -g) peertalk-dev make clean all test-local
```

### coverage
Tests with HTML coverage report:
```bash
make docker-coverage
# Report: build/coverage/html/index.html
```

### analyze
Run static analysis suite (complexity, cppcheck, duplicates):
```bash
make docker-analyze
# Reports: build/analysis/*.json
```

### all
Build for all platforms (POSIX + 68k + PPC):
```bash
./tools/build/build_all.sh all
# Uses Docker automatically when $RETRO68 not set
```

### package
Build and create transferable Mac binaries:
```bash
./tools/build/build_all.sh all
./tools/build/package.sh
# Creates: packages/PeerTalk-68k.bin, packages/PeerTalk-PPC.bin
```

### release
Full pipeline with all quality gates:
```bash
make docker-test
make docker-analyze
./tools/build/quality_gates.sh
./tools/build/build_all.sh all
./tools/build/package.sh
```

### launcher-mactcp
Build LaunchAPPLServer for MacTCP (68k):
```bash
./scripts/build-launcher.sh mactcp
# Output: Built in container at /opt/Retro68/LaunchAPPL/build-mactcp/Server/
# Copy to workspace: LaunchAPPL-build/LaunchAPPLServer-MacTCP.bin
```

### launcher-ot
Build LaunchAPPLServer for Open Transport (PPC):
```bash
./scripts/build-launcher.sh ot
# Output: Built in container at /opt/Retro68/LaunchAPPL/build-ppc/Server/
# Copy to workspace: LaunchAPPL-build/LaunchAPPLServer-OpenTransport.bin
```

### launcher-all
Build both LaunchAPPLServer versions:
```bash
./scripts/build-launcher.sh both
```

---

## Quality Gates

Enforced by `./tools/build/quality_gates.sh`:

| Gate | Threshold | Tool |
|------|-----------|------|
| File size | 500 lines max | `wc -l` |
| Function length | 100 lines max (prefer 50) | `ctags` |
| Coverage | 10% minimum | `lcov` |
| Compiler warnings | Treat as errors | `-Werror` |
| Cyclomatic complexity | 15 max per function | `pmccabe`/`lizard` |
| ISR safety | No violations (Mac code) | `tools/validators/isr_safety.py` |

Run quick quality check:
```bash
./tools/build/quality_gates.sh quick
```

Run full quality gates:
```bash
./tools/build/quality_gates.sh
```

---

## Build Artifacts

| Directory | Contents |
|-----------|----------|
| `build/` | Compiled binaries and objects |
| `build/lib/` | Static libraries (`libpeertalk.a`, `libptlog.a`) |
| `build/bin/` | Test executables |
| `build/coverage/` | Coverage reports (HTML in `html/`) |
| `build/analysis/` | Static analysis JSON reports |
| `packages/` | MacBinary packages for transfer |
| `LaunchAPPL-build/` | Built LaunchAPPLServer binaries |

---

## Example Workflows

### Quick Development Cycle
```
/build quick          # Fast syntax check
# Fix any errors
/build test           # Full test with coverage
```

### Before Committing
```
/build test           # Verify tests pass
/build analyze        # Check for issues
```

### Preparing for Mac Hardware
```
/build test           # Verify POSIX tests pass
/check-isr            # Validate ISR safety
/build package        # Create .bin files
# Transfer packages/*.bin to Mac
```

### Full Release
```
/build release
# Creates tested, validated packages for all platforms
```

### Setting Up LaunchAPPL for Hardware Testing
```
/build launcher-mactcp    # For 68k Macs (SE/30, IIci)
/build launcher-ot        # For PPC Macs
# Then use /deploy to transfer to Mac
```

---

## Troubleshooting

### Docker image not found
```bash
# Build POSIX image locally
docker build -t peertalk-posix -f docker/Dockerfile.posix .

# Or pull pre-built dev image
docker pull ghcr.io/matthewdeaves/peertalk-dev:develop
```

### Permission denied on build artifacts
The Makefile uses `-u $(id -u):$(id -g)` to match host user. If issues persist:
```bash
sudo chown -R $(id -u):$(id -g) build/
```

### Mac builds fail with "Retro68 not available"
Ensure Docker is running and the dev image is available:
```bash
docker compose -f docker/docker-compose.yml run --rm peertalk-dev which m68k-apple-macos-gcc
```

### Coverage shows 0%
Coverage requires tests to actually run. Check for test failures:
```bash
make docker-test
```
