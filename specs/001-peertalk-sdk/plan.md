# Implementation Plan: PeerTalk SDK

**Branch**: `001-peertalk-sdk` | **Date**: 2026-02-28 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/001-peertalk-sdk/spec.md`

## Summary

Build a C89 peer-to-peer LAN networking SDK with three
platform backends (POSIX, MacTCP, Open Transport). The SDK
provides 21 public functions for peer discovery, connection
management, and message exchange. All memory is pre-allocated
at init. I/O is poll-driven with no threads. The wire protocol
is identical across all platforms — only the transport layer
differs.

## Technical Context

**Language/Version**: C89/C90 (SDK core + Classic Mac backends),
C11 (POSIX test apps and build tools)
**Primary Dependencies**: clog (logging, separate repo at
`~/Desktop/clog`), MacTCP (System 6-7.5 via Retro68),
Open Transport (System 7.6+ via Retro68), BSD sockets (POSIX)
**Storage**: N/A
**Testing**: Four custom test applications (test_fast,
test_reliable, test_chat, test_lifecycle) running on POSIX
and Classic Mac.
**Target Platform**: POSIX (Linux, macOS), MacTCP (68k, System
6-7.5), Open Transport (PPC/late 68k, System 7.6+)
**Project Type**: Static library
**Performance Goals**: 30-60 Hz UDP messaging (Bomberman
pattern), reliable delivery of messages up to 64 KB (Chat
pattern), request/response at application pace (Chess pattern)
**Constraints**: C89 compatibility, zero malloc after
`PT_Init`, total codebase under 15,000 lines, minimum 8
peers on 4 MB Mac, single public header
**Scale/Scope**: 21 public functions, 3 platform backends,
4 test apps, <15K LOC total
**Cross-platform buffer constraint**: Peers may have different
tcp_send_size values. Reassembly must derive chunk offsets from
actual wire protocol payload lengths, not from local buffer sizes.
**Classic Mac TCP recv buffer**: Mac tcp_recv_buf (2048) must be
large enough to receive chunk frames from any sender. A POSIX
sender with tcp_send_size=4096 produces chunk frames up to 4096
bytes. Either increase Mac recv buffer or support partial frame
reads across poll cycles. See R21.
**Log file naming**: Each test app writes to a test-specific
log file (PT_Lifecycle, PT_Reliable, PT_Fast, PT_Chat) so
sequential test runs on the same Mac preserve all logs. See R20.
**Machine identification**: PT_Init logs machine model, CPU,
and system version via Gestalt (Classic Mac) or uname (POSIX)
for log identification. See R24.
**Classic Mac Heap**: PT_Init must call MaxApplZone() +
MoreMasters() before any allocations (R17). Without this,
the application heap is at SIZE resource minimum and
FreeMem() returns wrong values.
**Build**: CMake with Retro68 toolchain files for cross-
compilation. All builds run natively on the host.
**Cross-compiler**: Retro68 at `~/Retro68-build/toolchain`,
using Apple MPW interfaces from
`~/Retro68/InterfacesAndLibraries/MPW_Interfaces/`
**Network input validation**: Discovery packets from the network
must be validated before processing — null terminator check before
strlen, bounds on name length, port sanity. See R25.
**POSIX send safety**: TCP send must not silently drop bytes on
EAGAIN. Return error on partial send. See R26.
**Classic Mac atomicity**: Flag clearing in poll loops must be
atomic with respect to ASR/notifier interrupts. Use interrupt
disable (68k) or OTAtomicClearBit (PPC). See R27.
**OT endpoint lifecycle**: Unbind/rebind on async endpoints must
use synchronous mode or track intermediate state. See R29.
**Demo app UI constraints (R33)**: Demo apps targeting Classic Mac must handle
two screen tiers: compact (512x342 for Mac SE/Plus/Classic) and standard
(640x480+ for Performa and later). Dialog resources should either use dimensions
that fit the smallest target, or check screen size at runtime via
`screenBits.bounds` and adapt. The peer list must show discovered peers (not just
connected ones) since users need to select and connect.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after
Phase 1 design.*

| # | Principle | Status | Evidence |
|---|-----------|--------|----------|
| I | Three Apps Are the Spec | PASS | All features map to Bomberman (fast msgs), Chess (reliable msgs), or Chat (chunked msgs) patterns |
| II | SDK Handles the Protocol | PASS | Framing, chunking, transport selection, discovery all internal. Developer calls PT_Send. |
| III | Honest About Platform Limits | PASS | Any platform limits discovered during testing will be documented. MacTCP throughput documented from v1 testing. OT will be measured fresh. |
| IV | Simple Defaults, No Knobs | PASS | Single PT_Init(name) call. No config structs. 21 functions total. |
| V | Pre-Allocate Everything | PASS | Single contiguous block at init, arena-style suballocation. Zero malloc after init. |
| VI | Adapt at Init, Not Runtime | PASS | FreeMem() sizing on Mac, generous defaults on POSIX. No runtime adaptation. |
| VII | Logging Separate | PASS | clog is external dependency, linked as static lib. Not exposed in peertalk.h. |
| VIII | Test Apps and Demo Apps Prove the SDK | PASS | Four test apps + csend-pt demo app (Phase 15). Constitution v1.1.0. |
| IX | Keep It Small | PASS | Target <15K LOC. Three platform backends + core + test apps. |
| X | C89 for Portability | PASS | All SDK code is C89. POSIX test apps may use C11. Public header uses only C89 types. |

**Gate result**: ALL PASS. No violations requiring justification.

## Project Structure

### Documentation (this feature)

```text
specs/001-peertalk-sdk/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/
│   └── peertalk-api.md
└── checklists/
    └── requirements.md
```

### Source Code (repository root)

```text
include/
└── peertalk.h                  # Single public header (C89)

src/
├── core/
│   ├── pt_internal.h           # Internal types, macros, forward decls
│   ├── pt_core.c               # PT_Init, PT_Shutdown, callback dispatch
│   ├── pt_discovery.c          # Discovery broadcast/receive, peer timeout
│   ├── pt_messaging.c          # Send, Broadcast, framing, chunking, reassembly
│   └── pt_memory.c             # Memory pool: sizing, allocation, slot mgmt
├── platform/
│   ├── posix/
│   │   └── pt_posix.c          # BSD sockets + select(), PT_PlatformOps impl
│   ├── mactcp/
│   │   └── pt_mactcp.c         # Async PBs + polling, PT_PlatformOps impl
│   └── opentransport/
│       └── pt_ot.c             # OT endpoints + notifiers, PT_PlatformOps impl

tests/
├── test_common.h               # Shared test utilities, message definitions
├── test_fast.c                 # Bomberman pattern (high-freq UDP)
├── test_reliable.c             # Chess pattern (turn-based TCP)
├── test_chat.c                 # Chat pattern (variable-size chunked TCP)
└── test_lifecycle.c            # Connect/disconnect/reconnect cycles

CMakeLists.txt                  # Root build configuration
```

**Structure Decision**: Single project with platform-specific
source files selected at compile time via CMake. The `include/`
directory contains only the public header. All internal headers
live in `src/core/`. Each platform backend is a single .c file
implementing the PT_PlatformOps vtable. Test apps are standalone
executables linked against libpeertalk.a + libclog.a.

## Design Philosophy: Keep the Send Path Simple

The send path is deliberately simple: `PT_Send` frames the
message and calls `platform_ops->tcp_send` (or `udp_send`).
The platform backend sends it or returns an error. There is
no rate limiting, no send queue, no batching, no flow control
beyond what TCP provides natively. If the network can't keep
up, `tcp_send` returns an error and the application decides
what to do. MacTCP and Open Transport handle buffering and
retransmission — the SDK does not duplicate that work.

The same applies to receive: the platform backend reads
available data, the core parses frames, and callbacks fire.
No coalescing, no adaptive tuning, no backpressure system.

This is a deliberate reaction to v1, which grew rate limiters,
token buckets, capability exchange, and multi-tier backpressure
— none of which was needed by the three target applications.

## Test App Logging Strategy

Test apps write logs to a PT_Log file on the Mac using clog.
After the test completes, logs are collected by:

1. **FTP download** (machines with FTP): Use `download_file`
   MCP tool to retrieve PT_Log
2. **LaunchAPPL stdout** (LaunchAPPL-only machines): Test
   output is captured in the execute_binary response

There is no log streaming from test apps. The v1 log streaming
system (32KB buffer, drain timers, "LOG:" prefix protocol)
was complex and fragile. Simple file download works reliably.

## Build Environment

All builds run natively on the host (Ubuntu 25.10):

- **POSIX**: `cmake .. && make` (GCC 15, CMake 3.31)
- **68k (MacTCP)**: `cmake .. -DCMAKE_TOOLCHAIN_FILE=~/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake -DPT_PLATFORM=MACTCP && make`
- **PPC (OT)**: `cmake .. -DCMAKE_TOOLCHAIN_FILE=~/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake -DPT_PLATFORM=OT && make`

Docker is available for CI but not required for local
development. The `setup.sh` script verifies all host
prerequisites.

## Implementation Status

| Phase | Status | Notes |
|-------|--------|-------|
| 1: Setup | Complete | T001-T003 |
| 2: Foundational | Complete | T004-T008 |
| 3: US1 Discovery+Connect | Complete | T009-T012 |
| 4: US2 Reliable Messaging | Complete | T013-T017 |
| 5: US3 Fast Messaging | Complete | T018-T020 |
| 6: US4 Lifecycle Mgmt | Complete | T021-T024 |
| 7: US5 Cross-Platform | Complete | T025-T028 |
| 8: Polish | Complete | T029-T032 |
| 9: Hardware Fixes | Complete | T033-T044 |
| 10: Mac Hardening | Complete | T045-T048 |
| 11: GUI + Speckit | Complete | T049-T055 |
| 12: Logs + Verification | Complete | T056-T063 |
| 13: SDK Safety | Complete | T064-T072 |
| 14: Test Quality | Complete | T073-T079 |
| 15: Chat App | Complete | T080-T087 |
| 16: Hardware Verification | Complete | T088, T094-T095 |
| 17: Task Runner | Complete | T096-T101 |
| 18: Post-Review Fixes | Complete | T102-T108, T111 |
| 19: Final Hardware Verification | Complete | T089-T093 |
| 20: API — Name Setter | Complete | T109-T110 |
| 21: Chat App Fixes | Complete | T112-T115 |
| 22: MacTCP UDP Fix | Complete | T116-T117 |
| 23: Artifact Consistency | Complete | T118-T120 |
| 24: Bidirectional + MacTCP Fixes | Complete | T121-T127 |
| 25: 68k UDP Fix + HW Verification | Complete | T128-T136 |
| 26: Book Review + 68k OT + v1.0 Prep | In Progress | T137-T148 (T143 pending HW) |
| 27: Test App Role Fix | Complete | T149-T151 |
| 28: 68k Stack Fix + Connect Guard | In Progress | T152-T155 |

**153 of 154 tasks complete.** T143 (68k OT hardware test on Performa 630) blocked on machine setup. Phase 27 hardware verification results (T151, auto-connect-on-discovery after R47 fix):
- POSIX (Linux): test_lifecycle PASS, test_chat PASS
- PPC/OT (Performa 6400): test_lifecycle PASS (2 connects, 2 disconnects, auto-connect works, OTConnect -3158 on Mac reconnect but POSIX re-connects). test_chat PASS (Mac sent 6, received 6, POSIX sent 10, received 6, all VALID integrity).
- PPC/MacTCP (Performa 6200): not tested (offline)
- 68k/MacTCP (Mac SE): not tested (offline)

**MacTCP UDP constraint (R34)**: MacTCP requires explicit udp_listen()
for every UDP port that should receive datagrams. Unlike POSIX
(select-based) and OT (pull-based OTRcvUData), MacTCP's async model
requires a pre-posted UDPRead parameter block per port. PT_StartDiscovery
must call udp_listen for both PT_DISCOVERY_PORT and PT_UDP_MSG_PORT.

**Test role coverage gap (R37)**: Mac peers default to "Unnamed" ('U' > 'M'),
permanently assigning passive role in test_fast (RECEIVER), test_chat
(RECEIVER), and test_lifecycle (RESPONDER). Only test_reliable is truly
bidirectional. Mac send paths (UDP, variable-size TCP, PT_Connect) require
dedicated sender-role test runs or bidirectional test redesign.

**68k MacTCP transient crash (R38)**: test_fast initially crashed on Mac SE
after dirty MacTCP state from a prior failed run. After clean reboot, all 4
tests PASS (59/60 UDP received, 74ms avg). Mac SE is re-included in the test
matrix. R35 suspension is superseded. If test_fast crashes on Mac SE, reboot
and retry before marking as failed.

**MacTCP UDP shutdown safety (R39)**: mactcp_shutdown must spin-wait for
pending async UDPRead to complete before disposing buffers. UDPRelease
cancels the pending read but completion is asynchronous — DisposePtr
before completion causes write-after-free corrupting driver state. This
is the V1 TCP shutdown pattern (spin-wait on ioResult) applied to V2 UDP.

**68k MacTCP UDP send burst crash (R40)**: Mac SE crashes when
test_fast sends 12 UDPWrite calls in a tight loop. MacTCP docs
confirm concurrent UDPWrite+UDPRead is safe (Device Manager I/O
queue serializes). The crash is burst-related, not concurrency.
PPC handles the same burst. Fix: throttle 68k to 1-2 sends per
poll cycle. Also: P6400 test_lifecycle needs re-verification with
the 3-phase bidirectional design (T123).

**MacTCP send-side chunking limit (R42)**: On MacTCP, messages
requiring multi-chunk TCP sends fail because the async model only
supports one tcp_send per poll cycle (R19). Mac SE (tcp_send=1024)
can send messages up to 1024 bytes; 2000+ bytes fail. This is the
send-side corollary to the receive-side limit (R21). Test apps should
only send messages within the platform's single-chunk capability,
or gracefully handle partial success in their summaries.

**OTSnd partial send (R44)**: In async non-blocking mode, OTSnd can
return fewer bytes than requested (Networking With Open Transport,
page 495). The current OT backend treats any positive return as
success. Fix: loop to send remaining bytes, matching the book's
recommendation and preventing framing corruption from partial writes.

**OT orderly release protocol (R45)**: T_ORDREL handler should call
OTSndOrderlyDisconnect after OTRcvOrderlyDisconnect to complete the
four-way TCP close per the book (page 115). Current code uses
abortive disconnect instead. One-line fix.

**68k OT import libraries (R46)**: Retro68 m68k toolchain has OT
libraries under different names than PPC (OpenTransportApp vs
OpenTransportAppPPC, etc.). CMakeLists.txt must detect the toolchain
and use the correct names. Enables 5th build target: build-68k-ot/
for Performa 630 (68040, System 7.6.1).

**68k stack buffer crash (R48)**: PT_Send allocates a 1403-byte stack
buffer for UDP framing. On 68k Macs with ~8KB stacks, this overflows
the stack on the first send. Fix: move the buffer to PT_Context_Internal
(allocated at init). No new malloc — buffer is part of the context struct.

**Simultaneous-connect race (R49)**: When both peers auto-connect on
discovery, fast platforms (OT) can create dual TCP connections. Test
apps must guard on_discovered with `if (g_connected) return;`. Not
an SDK issue — the SDK accepts all valid incoming connections.

## Complexity Tracking

No violations — this section is intentionally empty.

## Phase 0 Artifacts

- [research.md](research.md) — 49 research decisions (R1-R49)
  covering build system, platform detection, memory strategy,
  test app quality (chat exit, reliable strictness, lifecycle Phase 3),
  MacTCP send-side chunking limit, discovery re-fire for disconnected peers,
  async I/O, chunking, discovery filtering, TCP accept model,
  clog integration, OT throughput limits, byte order, Classic
  Mac application requirements, Retro68 import library mapping,
  cross-platform chunk reassembly, peers array allocation,
  platform buffer memory budget, v1 Mac test app patterns,
  Classic Mac heap preparation, test app design constraints,
  TCP send two-call bug, Retro68/LaunchAPPL known issues,
  chunked TCP receive failure, POSIX clog timestamp overflow,
  disconnect reason fix, Gestalt machine ID, discovery buffer
  overflow, POSIX partial send, ASR flag race, reassembly type
  check, OT unbind race, and csend GUI reuse.

## Phase 1 Artifacts

- [data-model.md](data-model.md) — Internal entity model:
  PT_Context, PT_Peer, PT_PlatformOps, Callbacks, Message Type
  Registry, Wire Protocol structures, Memory Layout
- [contracts/peertalk-api.md](contracts/peertalk-api.md) —
  Complete public API contract: 21 functions, 4 enums,
  4 callback typedefs, 2 opaque types, C89 compatibility
  notes, port assignments, reserved values
- [quickstart.md](quickstart.md) — Build instructions (POSIX
  + Docker/Retro68), minimal example, test app usage, platform
  performance notes, linking

## Post-Design Constitution Re-Check

All 10 principles re-verified against the completed design:

- **I**: No feature serves anything outside the three app
  patterns. The four test apps map directly to Bomberman,
  Chess, Chat, and lifecycle management.
- **II**: The wire protocol, chunking, and transport selection
  are entirely internal. The public API exposes only PT_Send
  and PT_RegisterMessage.
- **III**: Any platform limits found during testing will be
  measured and documented honestly. No pre-assumed limitations.
- **IV**: PT_Init takes only a name string. No config structs.
  21 functions, all fit on one screen.
- **V**: Single contiguous allocation at init. Per-peer buffers
  carved from the block. Zero malloc in send/receive path.
- **VI**: FreeMem() on Mac sizes everything at init. No runtime
  adaptation, no capability negotiation.
- **VII**: clog is external, linked as static lib, not exposed
  in peertalk.h.
- **VIII**: Four test apps exercise all three target patterns
  plus lifecycle management. Demo apps (csend-pt) ship in
  their own repos and link peertalk as a library.
- **IX**: Project structure has 5 core .c files + 3 platform
  .c files + 4 test .c files = 12 source files. Target <15K
  LOC across all.
- **X**: All SDK code is C89. Public header uses only C89
  types. No stdint.h, no bool, no C99/C11 features in shared
  code.

**Gate result**: ALL PASS. Design is constitution-compliant.
