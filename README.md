# PeerTalk

A C networking SDK for LAN peer-to-peer communication between modern POSIX systems and Classic Macintosh computers.

## Features

- **21-function C89 API** with a single public header (`peertalk.h`)
- **3 platform backends**: POSIX (BSD sockets), MacTCP (68k/PPC), Open Transport (68k/PPC)
- **Zero allocation after init** — all buffers pre-allocated in a single block
- **Automatic peer discovery** via UDP broadcast
- **Reliable (TCP) and fast (UDP)** message transports
- **~5,700 lines of C** across all platforms

## Supported Platforms

| Platform | Backend | Build Target | Tested Hardware |
|----------|---------|-------------|-----------------|
| Linux / macOS | POSIX (BSD sockets) | `build/` | Any modern system |
| Mac SE (68000) | MacTCP | `build-68k/` | Mac SE, 4 MB RAM |
| Performa 6200 (PPC 603) | MacTCP | `build-ppc-mactcp/` | Performa 6200, 40 MB RAM |
| Performa 6400 (PPC 603ev) | Open Transport | `build-ppc-ot/` | Performa 6400, 48 MB RAM |
| Performa 630 (68LC040) | Open Transport | `build-68k-ot/` | Performa 630 (pending) |

## Quick Start

```c
#include "peertalk.h"

PT_Context *ctx = PT_Init("MyApp", 0);

PT_SetOnPeerDiscovered(ctx, on_discovered);
PT_SetOnMessageReceived(ctx, on_message);
PT_StartDiscovery(ctx);

while (running) {
    PT_Poll(ctx);
    /* send messages with PT_Send() or PT_SendFast() */
}

PT_Shutdown(ctx);
```

See [Quickstart Guide](specs/001-peertalk-sdk/quickstart.md) and [API Reference](specs/001-peertalk-sdk/contracts/peertalk-api.md) for details.

## Building

Requires [clog](https://github.com/your-org/clog) built first at `~/Desktop/clog`.

```bash
# POSIX
mkdir -p build && cd build
cmake .. -DCLOG_DIR=$HOME/Desktop/clog && make

# 68k MacTCP (Retro68 cross-compiler)
mkdir -p build-68k && cd build-68k
cmake .. -DCMAKE_TOOLCHAIN_FILE=~/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake \
  -DPT_PLATFORM=MACTCP -DCLOG_DIR=~/Desktop/clog -DCLOG_LIB_DIR=~/Desktop/clog/build-m68k && make

# PPC Open Transport (Retro68 cross-compiler)
mkdir -p build-ppc-ot && cd build-ppc-ot
cmake .. -DCMAKE_TOOLCHAIN_FILE=~/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DPT_PLATFORM=OT -DCLOG_DIR=~/Desktop/clog -DCLOG_LIB_DIR=~/Desktop/clog/build-ppc && make
```

## Using as a Library

```cmake
set(PEERTALK_BUILD_TESTS OFF)
add_subdirectory(${PEERTALK_DIR} ${CMAKE_BINARY_DIR}/peertalk)
target_link_libraries(myapp PRIVATE peertalk)
```

## Project Structure

```
include/peertalk.h          # Single public header (C89)
src/core/                   # Platform-independent core
src/platform/posix/         # BSD sockets + select()
src/platform/mactcp/        # MacTCP parameter blocks
src/platform/opentransport/ # OT endpoints + notifiers
tests/                      # Four test apps
```

## Test Apps

| App | Pattern | What it tests |
|-----|---------|---------------|
| test_lifecycle | Connection | Discovery, connect, disconnect, reconnect |
| test_reliable | Chess (TCP) | Ordered reliable message exchange |
| test_fast | Bomberman (UDP) | High-frequency positional updates at 60 Hz |
| test_chat | Chat (TCP) | Variable-length bidirectional messages |

## Hardware Verification

All test apps pass on real Classic Mac hardware:

- **Mac SE** (68000, System 6.0.8, MacTCP): 4/4 PASS
- **Performa 6200** (PPC 603, System 7.5.5, MacTCP): 4/4 PASS
- **Performa 6400** (PPC 603ev, Mac OS 8.1, Open Transport): test_lifecycle PASS

## Design Principles

1. Every feature serves Bomberman, Chess, or Chat
2. Pre-allocate everything at init, zero malloc after
3. Poll-based I/O on all platforms (no threads)
4. C89 for maximum portability
5. Measure on real hardware, document honestly

## Documentation

- [Specification](specs/001-peertalk-sdk/spec.md)
- [API Contract](specs/001-peertalk-sdk/contracts/peertalk-api.md)
- [Quickstart](specs/001-peertalk-sdk/quickstart.md)
- [Research Decisions](specs/001-peertalk-sdk/research.md)
