---
name: build
description: Build PeerTalk for all platforms with quality checks. Use when compiling code (quick syntax check, full compile, tests), packaging Mac binaries for hardware transfer, or running the complete release pipeline with quality gates.
argument-hint: <mode>
---

# Cross-Platform Build Orchestrator

Build PeerTalk for POSIX, 68k MacTCP, and PPC Open Transport platforms with integrated quality checks.

**All builds run inside Docker containers** per project requirements (see `.claude/rules/build-requirements.md`).

## Quick Reference

| Mode | What It Does |
|------|--------------|
| `test` | Build + run all tests (default) |
| `coverage` | Tests with HTML coverage report |
| `analyze` | Static analysis (cppcheck, complexity, duplicates) |
| `quick` | Fast syntax check (~5 sec) |
| `compile` | Full compile with `-Werror` |
| `valgrind` | Memory leak detection |
| `integration` | Multi-container network test |
| `all` | All platforms (POSIX + 68k + PPC) |
| `package` | Create Mac `.bin` files for transfer |
| `release` | Full pipeline with all quality gates |
| `launcher-mactcp` | Build LaunchAPPLServer for 68k |
| `launcher-ot` | Build LaunchAPPLServer for PPC |
| `mac-tests` | Build Mac test apps (test_throughput, etc.) |
| `mac-tests-perf` | Build only performance test apps |

## Docker Images

| Image | Size | Use For |
|-------|------|---------|
| `peertalk-dev` | ~2.5-3.7GB | All builds (tests, coverage, analysis, Mac) |
| `peertalk-posix` | ~580MB | Lightweight POSIX-only builds |
| `ghcr.io/matthewdeaves/peertalk-dev:develop` | ~2.5GB | CI/pre-built dev image |

The Makefile targets (`docker-test`, `docker-coverage`, `docker-analyze`) use `peertalk-dev`.

---

## Mode Details

### test (default)
Build and run all POSIX tests:
```bash
make docker-test
```
This runs `make clean && make all && make test-local` inside the container.

### coverage
Tests with HTML coverage report:
```bash
make docker-coverage
# Report: build/coverage/html/index.html
```

### analyze
Run static analysis suite:
```bash
make docker-analyze
# Runs: cppcheck, pmccabe (complexity), jscpd (duplicates), lizard
```

### quick
Fast syntax check without full compilation:
```bash
docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest \
    gcc -fsyntax-only -Wall -Wextra -I include -I src/core \
    src/core/*.c src/posix/*.c src/log/*.c
```

### compile
Full compilation with warnings as errors:
```bash
docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-dev make clean all
```

### valgrind
Memory leak detection on core tests:
```bash
make valgrind
# Runs test binaries through valgrind with --leak-check=full
```

### integration
Multi-container network test (3 peers):
```bash
make test-integration-docker
# Runs Alice, Bob, Charlie containers communicating over network
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
# Output: LaunchAPPL-build/LaunchAPPLServer-MacTCP.bin
```

### launcher-ot
Build LaunchAPPLServer for Open Transport (PPC):
```bash
./scripts/build-launcher.sh ot
# Output: LaunchAPPL-build/LaunchAPPLServer-OpenTransport.bin
```

### mac-tests
Build Mac test applications (requires MacTCP platform):
```bash
./scripts/build-mac-tests.sh mactcp
# Output: build/mac/test_mactcp.bin, test_latency.bin, test_throughput.bin, etc.
```

### mac-tests-perf
Build only performance test apps:
```bash
./scripts/build-mac-tests.sh mactcp perf
# Output: build/mac/test_latency.bin, test_throughput.bin, test_stress.bin, test_discovery.bin
```

---

## Makefile Targets Reference

The Makefile provides these Docker-wrapped targets:

| Target | Description |
|--------|-------------|
| `make docker-test` | Run all tests in container |
| `make docker-coverage` | Tests with coverage report |
| `make docker-analyze` | Static analysis suite |
| `make docker-build` | Just compile (no tests) |
| `make test-integration-docker` | Multi-peer network test |
| `make valgrind` | Memory leak detection |

Local targets (run inside container or with deps installed):

| Target | Description |
|--------|-------------|
| `make test-local` | Run all tests |
| `make coverage-local` | Coverage (requires lcov) |
| `make test-log` | Just logging tests |
| `make test-queue` | Just queue tests |
| `make test-fuzz` | Protocol fuzz tests |

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

Run checks:
```bash
./tools/build/quality_gates.sh         # Full check
./tools/build/quality_gates.sh quick   # Quick (no coverage)
```

---

## Build Artifacts

| Directory | Contents |
|-----------|----------|
| `build/lib/` | Static libraries (`libpeertalk.a`, `libptlog.a`) |
| `build/bin/` | Test executables |
| `build/coverage/html/` | Coverage report (open `index.html`) |
| `packages/` | MacBinary packages for transfer |
| `LaunchAPPL-build/` | Built LaunchAPPLServer binaries |

---

## Example Workflows

### Quick Development Cycle
```
/build quick          # Fast syntax check
# Fix any errors
/build test           # Full test suite
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
# Pull pre-built dev image (recommended)
docker pull ghcr.io/matthewdeaves/peertalk-dev:develop
docker tag ghcr.io/matthewdeaves/peertalk-dev:develop peertalk-dev

# Or build POSIX image locally (lighter, POSIX-only)
docker build -t peertalk-posix -f docker/Dockerfile.posix .

# Or build full dev image (includes Retro68)
docker compose -f docker/docker-compose.yml build
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

### Check prerequisites
```bash
./.claude/skills/build/scripts/check-build-prereqs.sh
```
