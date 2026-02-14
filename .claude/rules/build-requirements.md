# Build and Test Requirements

**CRITICAL: All building and testing MUST happen inside Docker containers, never on the host.**

## Why Docker-Only?

1. **Reproducibility** - Same environment as CI, eliminates "works on my machine"
2. **Toolchain consistency** - Retro68 and POSIX toolchains are containerized
3. **Clean isolation** - No pollution of host system with build artifacts
4. **CI parity** - Local builds match exactly what runs in GitHub Actions

## Prohibited: Direct Host Commands

**NEVER run these directly on the host:**

- `gcc`, `g++`, `clang`, `clang++` - any compiler invocation
- `make` (without Docker wrapper)
- `cmake`, `ninja`, `meson` - any build system
- `./build/bin/*` - running any POSIX compiled binary
- `mkdir -p build/` - creating build directories
- `cppcheck`, `valgrind`, `lcov` - any analysis tools
- `sudo` anything build-related

## Running POSIX Binaries

**POSIX binaries MUST run inside Docker** (they're compiled for the container environment):

```bash
# CORRECT - run POSIX test binary in Docker
docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest ./build/bin/test_partner

# WRONG - never run POSIX binaries directly on host
./build/bin/test_partner       # NO!
sudo ./build/bin/test_partner  # NO!
```

## Running Mac Binaries

**Mac binaries run on real Classic Mac hardware**, not in Docker. Use the MCP server:

```bash
# CORRECT - deploy and execute via MCP tools
mcp__classic-mac-hardware__deploy_binary(machine="se30", platform="mactcp", binary_path="build/mactcp/PeerTalk.bin")
mcp__classic-mac-hardware__execute_binary(machine="se30", platform="mactcp", binary_path="build/mactcp/PeerTalk.bin")

# Or use skills
/deploy se30 mactcp
/execute se30 build/mactcp/PeerTalk.bin
```

See `.claude/rules/classic-mac-hardware.md` for MCP enforcement rules.

## Compiling Individual Files

```bash
# CORRECT - compile in Docker
docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest \
    gcc -Wall -std=c99 -I./include -o build/bin/test_partner tests/posix/test_partner.c

# WRONG - never compile on host
gcc -Wall -std=c99 -I./include -o build/bin/test_partner tests/posix/test_partner.c  # NO!
```

## If Docker Has Issues

If Docker doesn't work:
1. Fix Docker - DO NOT fall back to host commands
2. Check `docker ps` to verify daemon is running
3. Rebuild image if needed: `docker build -t peertalk-posix -f docker/Dockerfile.posix .`

## Commands

### POSIX Builds and Tests

```bash
# Correct - use Docker
docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest make test
docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest make coverage
docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest make cppcheck

# WRONG - never do this
make test        # NO! Uses host toolchain
make coverage    # NO! Uses host toolchain
```

### Classic Mac Builds (Retro68)

```bash
# Correct - use Docker
docker-compose -f docker/docker-compose.yml run --rm retro68 make -C src/mactcp
docker-compose -f docker/docker-compose.yml run --rm retro68 make -C src/opentransport

# WRONG - Retro68 isn't even installed on host
make -C src/mactcp  # NO! Will fail or use wrong toolchain
```

### Static Analysis

```bash
# Correct - use Docker
docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest make cppcheck

# WRONG
cppcheck src/     # NO! Different version than CI
```

## Quick Reference

| Task | Command |
|------|---------|
| Run tests | `docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest make test` |
| Coverage | `docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest make coverage` |
| Cppcheck | `docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest make cppcheck` |
| Clean | `docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest make clean` |
| All checks | `docker run --rm -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest make ci` |

## The /build Skill

The `/build` skill automatically uses Docker. Prefer using it:

```
/build test      # Runs tests in Docker
/build coverage  # Runs coverage in Docker
/build quick     # Quick syntax check in Docker
```

## Exception

The only exception is when explicitly debugging Docker or container issues themselves.

## Docker Maintenance

Docker can accumulate significant disk usage over time. Check and clean periodically:

```bash
# Check disk usage
docker system df

# Clean unused images, containers, volumes (safe)
docker system prune -af --volumes

# Check for running containers (avoid killing perf-partner during tests)
docker ps
```

**Named containers to preserve:**
- `perf-partner` - POSIX test partner for Mac hardware testing
- `peertalk-dev` - Development container (optional, can be recreated)
