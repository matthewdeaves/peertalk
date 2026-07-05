# PeerTalk

A C networking SDK for LAN peer-to-peer communication between modern POSIX systems and Classic Macintosh computers.

## Features

- **29-function C89 API** with a single public header (`peertalk.h`)
- **3 platform backends**: POSIX (BSD sockets), MacTCP (68k/PPC), Open Transport (PPC)
- **Zero allocation after init** — all buffers pre-allocated in a single block
- **Automatic peer discovery** via UDP broadcast with instant leave notification
- **Reliable (TCP) and fast (UDP)** message transports
- **~4,700 lines of SDK code** across all platforms

## Supported Platforms

| Platform | Backend | Build Target | Tested Hardware |
|----------|---------|-------------|-----------------|
| Linux / macOS | POSIX (BSD sockets) | `build/` | Any modern system |
| Mac SE (68000) | MacTCP | `build-68k/` | Mac SE, 4 MB RAM |
| Performa 6200 (PPC 603) | MacTCP | `build-ppc-mactcp/` | Performa 6200, 40 MB RAM |
| Performa 6400 (PPC 603e) | Open Transport | `build-ppc-ot/` | Performa 6400, 48 MB RAM |
| Performa 630 (68LC040) | Open Transport | `build-68k-ot/` | Performa 630 (pending) |

## Quick Start

```c
#include "peertalk.h"

PT_Context *ctx;
PT_Init(&ctx, "MyApp");

PT_OnPeerDiscovered(ctx, on_discovered, NULL);
PT_OnConnected(ctx, on_connected, NULL);
PT_OnMessage(ctx, MSG_CHAT, on_chat, NULL);

PT_RegisterMessage(ctx, MSG_CHAT, PT_RELIABLE);
PT_StartDiscovery(ctx);

while (running) {
    PT_Poll(ctx);
}

PT_Shutdown(ctx);
```

See [API Contract](specs/001-peertalk-sdk/contracts/peertalk-api.md) for the full 29-function reference.

## Prerequisites

- [Retro68](https://github.com/matthewdeaves/Retro68) fork for Classic Mac cross-compilation (`$RETRO68_TOOLCHAIN`)
- [clog](https://github.com/matthewdeaves/clog) — fetched automatically via FetchContent, or pass `-DCLOG_DIR=path` for a local checkout

## Building

```bash
# POSIX
mkdir -p build && cd build && cmake .. && make

# 68k MacTCP (Retro68 cross-compiler)
mkdir -p build-68k && cd build-68k
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/m68k-apple-macos/cmake/retro68.toolchain.cmake \
  -DPT_PLATFORM=MACTCP && make

# PPC Open Transport (Retro68 cross-compiler)
mkdir -p build-ppc-ot && cd build-ppc-ot
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DPT_PLATFORM=OT && make
```

## Using as a Library

```cmake
# Option 1: FetchContent (automatic download of peertalk + clog)
include(FetchContent)
FetchContent_Declare(clog
    GIT_REPOSITORY https://github.com/matthewdeaves/clog.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
set(CLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(clog)

FetchContent_Declare(peertalk
    GIT_REPOSITORY https://github.com/matthewdeaves/peertalk.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
set(PEERTALK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(peertalk)
target_link_libraries(myapp PRIVATE peertalk clog)

# Option 2: Local checkout
set(PEERTALK_BUILD_TESTS OFF)
add_subdirectory(${PEERTALK_DIR} ${CMAKE_BINARY_DIR}/peertalk)
target_link_libraries(myapp PRIVATE peertalk)
```

## Project Structure

```
include/peertalk.h          # Single public header (C89, 29 functions)
src/core/                   # Platform-independent core
src/platform/posix/         # BSD sockets + select()
src/platform/mactcp/        # MacTCP async parameter blocks
src/platform/opentransport/ # OT endpoints + notifiers
tests/                      # 7 test apps
```

## Test Apps

| App | Pattern | What it tests |
|-----|---------|---------------|
| test_lifecycle | Connection | Discovery, connect, disconnect, reconnect, StopDiscovery, SetName, PeerLost |
| test_reliable | Chess (TCP) | Ordered reliable message exchange, chunking/reassembly |
| test_fast | Bomberman (UDP) | High-frequency positional updates at 60 Hz |
| test_chat | Chat (TCP) | Variable-length bidirectional messages |
| test_multi | Multi-peer | N-way discovery, connect all, broadcast to all, verify receipt |
| test_init_only | Init/shutdown | Memory allocation, error path validation (10 checks) |
| test_clog_minimal | Logging | clog library verification |

## Hardware Testing

Deploy and run test binaries on real Classic Mac hardware using the [classic-mac-hardware-mcp](https://github.com/matthewdeaves/classic-mac-hardware-mcp) MCP server. See its README for setup.

### Getting a run log off a machine with no FTP

A Classic Mac GUI app's stdout does **not** reach the LaunchAPPL out-file, so
`execute_binary` can't return the log inline. Instead the test apps mirror
every log line to PeerTalk's UDP debug broadcast (port 7356); capture it on
the host with `socat` — works for any machine on the LAN, including the
FTP-less Mac SE:

```bash
timeout 55 socat -u UDP-RECV:7356,reuseaddr - > run.log &   # clean log each run
timeout 55 ./build/test_lifecycle --name POSIXHOST &        # a peer to talk to
# ...run the Mac binary via the MCP, then:
grep '<mac-ip>' run.log     # that machine's own lines; verdict = *** PASS ***
```

Both peers' logs land in one file, tagged `[name@ip]`. Machines with FTP can
still pull the `PT_<appname>` clog file directly.

## Hardware Verification

All test apps verified on real Classic Mac hardware:

- **Mac SE** (68000, System 6.0.8, MacTCP): test_lifecycle, test_reliable, test_multi PASS
- **Performa 6200** (PPC 603, System 7.5.3, MacTCP): test_lifecycle, test_reliable, test_multi, test_init_only PASS
- **Performa 6400** (PPC 603e, System 7.6.1, Open Transport): test_lifecycle, test_reliable, test_multi, test_init_only PASS

4-peer multi-peer test verified: all 4 machines (POSIX + Mac SE + 6200 + 6400) discovering, connecting, and exchanging broadcasts simultaneously.

## Design Principles

1. Every feature serves Bomberman, Chess, or Chat
2. Pre-allocate everything at init, zero malloc after
3. Poll-based I/O on all platforms (no threads)
4. C89 for maximum portability
5. Measure on real hardware, document honestly

## Documentation

- [Architecture Diagrams](ARCHITECTURE.md) — C4 Mermaid diagrams (Context, Container, Component, Deployment)
- [API Contract](specs/001-peertalk-sdk/contracts/peertalk-api.md)
- [Specification](specs/001-peertalk-sdk/spec.md)
- [Research Decisions](specs/001-peertalk-sdk/research.md)

## Dependencies

[Retro68](https://github.com/matthewdeaves/Retro68) (cross-compilation) + [clog](https://github.com/matthewdeaves/clog) (auto-fetched) -> peertalk -> [csend](https://github.com/matthewdeaves/csend), [BomberTalk](https://github.com/matthewdeaves/BomberTalk)
