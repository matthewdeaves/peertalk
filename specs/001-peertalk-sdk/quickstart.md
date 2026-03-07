# Quickstart: PeerTalk SDK

**Branch**: `001-peertalk-sdk` | **Date**: 2026-02-28

## Prerequisites

- GCC or Clang (POSIX builds)
- CMake 3.10+
- Retro68 cross-compiler at `~/Retro68-build/toolchain` (Classic Mac builds)
- MPW Interfaces at `~/Retro68/InterfacesAndLibraries/MPW_Interfaces/`
- clog library built for target platform (`~/Desktop/clog`)
- Run `./setup.sh` to verify everything is installed

## Build (POSIX)

```bash
# Build clog first
cd ~/Desktop/clog
mkdir -p build && cd build
cmake .. && make

# Build PeerTalk
cd ~/Desktop/peertalk
mkdir -p build && cd build
cmake .. -DCLOG_DIR=$HOME/Desktop/clog
make
```

## Build (Classic Mac — Retro68)

```bash
# Build clog for 68k first
cd ~/Desktop/clog
mkdir -p build-m68k && cd build-m68k
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=~/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
make

# Build PeerTalk for 68k (MacTCP)
cd ~/Desktop/peertalk
mkdir -p build-m68k && cd build-m68k
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=~/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake \
  -DPT_PLATFORM=MACTCP \
  -DCLOG_DIR=$HOME/Desktop/clog \
  -DCLOG_LIB_DIR=$HOME/Desktop/clog/build-m68k
make

# Build clog for PPC first
cd ~/Desktop/clog
mkdir -p build-ppc && cd build-ppc
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=~/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake
make

# Build PeerTalk for PPC (Open Transport)
cd ~/Desktop/peertalk
mkdir -p build-ppc && cd build-ppc
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=~/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DPT_PLATFORM=OT \
  -DCLOG_DIR=$HOME/Desktop/clog \
  -DCLOG_LIB_DIR=$HOME/Desktop/clog/build-ppc
make
```

## Minimal Example

```c
#include "peertalk.h"
#include <stdio.h>

#define MSG_CHAT 1

static void on_discovered(PT_Peer *peer, void *data) {
    printf("Found: %s\n", PT_PeerName(peer));
    /* Auto-connect to first peer found */
    PT_Connect((PT_Context *)data, peer);
}

static void on_connected(PT_Peer *peer, void *data) {
    const char *msg = "Hello!";
    printf("Connected to: %s\n", PT_PeerName(peer));
    PT_Send((PT_Context *)data, peer, MSG_CHAT,
            msg, 6);
}

static void on_message(PT_Peer *peer, const void *data,
                        size_t len, void *user) {
    printf("%s says: %.*s\n", PT_PeerName(peer),
           (int)len, (const char *)data);
}

static void on_disconnected(PT_Peer *peer,
                             PT_DisconnectReason reason,
                             void *data) {
    printf("%s left (%s)\n", PT_PeerName(peer),
           reason == PT_QUIT ? "quit" :
           reason == PT_TIMEOUT ? "timeout" : "error");
}

int main(void) {
    PT_Context *ctx;

    if (PT_Init(&ctx, "Player1") != PT_OK) {
        fprintf(stderr, "Init failed\n");
        return 1;
    }

    PT_RegisterMessage(ctx, MSG_CHAT, PT_RELIABLE);
    PT_OnPeerDiscovered(ctx, on_discovered, ctx);
    PT_OnConnected(ctx, on_connected, ctx);
    PT_OnMessage(ctx, MSG_CHAT, on_message, NULL);
    PT_OnDisconnected(ctx, on_disconnected, NULL);

    PT_StartDiscovery(ctx);

    /* Main loop — call PT_Poll to drive I/O */
    while (1) {
        PT_Poll(ctx);
        /* On POSIX: usleep(16000) for ~60 Hz */
        /* On Mac: call WaitNextEvent or similar */
    }

    PT_Shutdown(ctx);
    return 0;
}
```

## Running the Test Apps

```bash
# Terminal 1: Start first peer
./build/test_reliable --name "Alice"

# Terminal 2: Start second peer
./build/test_reliable --name "Bob"

# They discover each other, connect, and exchange messages
```

## Key Concepts

1. **Init once, poll forever**: `PT_Init` allocates all
   memory. `PT_Poll` drives everything. No threads needed.

2. **Register then send**: Call `PT_RegisterMessage` to
   declare whether a type is fast (UDP) or reliable (TCP).
   Then just call `PT_Send` — the SDK picks the transport.

3. **Callbacks from Poll**: All callbacks fire inside
   `PT_Poll`. Your callback code runs in the main thread,
   safe to do anything.

4. **Peer lifecycle**: Discovered → Connected → Disconnected.
   A disconnected peer can be reconnected if still on the
   network.

## Platform Performance

Performance is limited by the network and the platform's I/O model.
All measurements require two peers on separate machines on the same LAN.

| Platform | Discovery | TCP Throughput | UDP Rate | Notes |
|----------|-----------|----------------|----------|-------|
| POSIX (Linux) | < 2s | Network-limited | 60 Hz stable | Reference implementation |
| MacTCP (PPC, P6200) | < 3s | Messages up to 2 KB | 60 msgs sent | Chunked >2KB limited by recv buffer (R21) |
| Open Transport (PPC, P6400) | < 3s | Messages up to 2 KB | 60 msgs sent | Same recv buffer limitation (R21) |
| MacTCP (68k, Mac SE) | Untested | Untested | Untested | T055 pending |

**How to measure**: Run `test_fast` for UDP throughput (sends/receives
per second), `test_reliable` for TCP message latency, and `test_chat`
for chunked TCP throughput. Each test prints statistics on completion.

**Note**: Two peers cannot run on the same host — they bind to fixed
ports (7353, 7354, 7355). Run one peer per machine.

## Platform Notes

| Platform | Notes |
|----------|-------|
| POSIX | Reference implementation, select()-based polling |
| MacTCP (68k) | Async parameter blocks, rotating listener pool |
| Open Transport (PPC) | tilisten,tcp for concurrent accepts, notifier-driven |

## Linking

```
# POSIX
gcc -o myapp myapp.c -lpeertalk -lclog

# Retro68 (68k)
# Handled by CMake toolchain — link against libpeertalk.a
# and libclog.a in your CMakeLists.txt
```
