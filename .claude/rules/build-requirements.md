# Build and Test Requirements

**CRITICAL: All building and testing MUST happen inside Docker containers, never on the host.**

## Why Docker-Only?

1. **Reproducibility** - Same environment as CI, eliminates "works on my machine"
2. **Toolchain consistency** - Retro68 and POSIX toolchains are containerized
3. **Clean isolation** - No pollution of host system with build artifacts
4. **CI parity** - Local builds match exactly what runs in GitHub Actions

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
