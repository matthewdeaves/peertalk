# Research: PeerTalk SDK

**Branch**: `001-peertalk-sdk` | **Date**: 2026-02-28

## R1. Build System

**Decision**: CMake with Retro68 toolchain files

**Rationale**: Retro68 provides CMake toolchain files for
cross-compilation to 68k and PPC. CMake handles both native
POSIX builds and cross-compilation with a single build system.
The Docker environment already has CMake installed.

**Alternatives considered**:
- Plain Makefiles: Would require separate Makefiles per
  platform and manual toolchain configuration. More work for
  no benefit since Retro68 is CMake-native.
- Autotools: Overkill for a project this small and poor Retro68
  integration.

**Implementation notes**:
- Root CMakeLists.txt with platform detection
- Each Retro68 toolchain sets a different `CMAKE_SYSTEM_NAME`:
  - 68k: `Retro68`
  - PPC: `RetroPPC`
- Platform detection must match both:
  `CMAKE_SYSTEM_NAME MATCHES "Retro68|RetroPPC"`
- Retro68 toolchain files at:
  - `~/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake`
  - `~/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake`
- POSIX builds use system default compiler (GCC)
- All builds run natively on the host; `setup.sh` verifies
  prerequisites

## R2. Platform Detection and Backend Selection

**Decision**: Compile-time preprocessor macros with one
PT_PlatformOps implementation compiled per target

**Rationale**: Each build targets exactly one platform. There
is no runtime platform switching (Constitution Principle VI).
The Retro68 toolchain defines `__m68k__` and `__ppc__`. MacTCP
vs OT selection uses explicit macros set in CMakeLists.txt.

**Alternatives considered**:
- Runtime detection via Gestalt: Adds complexity for no
  benefit. The SDK is statically linked for a specific target.
- Build-time function pointer table selection: Unnecessary
  indirection when the compiler can resolve the platform at
  compile time.

**Implementation notes**:
- Preprocessor macros: `PT_PLATFORM_POSIX`,
  `PT_PLATFORM_MACTCP`, `PT_PLATFORM_OT`
- Core code uses `PT_PlatformOps` function pointer table
  regardless — the table is populated at init by the
  platform-specific module
- Only one platform .c file is compiled into each build

## R3. Memory Allocation Strategy

**Decision**: Single contiguous block allocated at PT_Init,
subdivided into fixed-size slots via arena-style indexing

**Rationale**: Constitution Principle V requires zero malloc
after init. A single allocation minimizes heap fragmentation on
Classic Mac (where the Memory Manager is cooperative and
fragile). Arena-style indexing means each peer slot, buffer,
and internal structure is at a known offset.

**Alternatives considered**:
- Multiple NewPtr calls at init: More allocations means more
  fragmentation risk. A single block is simpler and guaranteed
  contiguous.
- Handle-based allocation (Mac handles): Handles can move,
  requiring locking. Pointers from a single locked block are
  stable.

**Implementation notes**:
- Query available memory: `FreeMem()` on Mac, hardcoded
  generous default on POSIX. Note: `FreeMem()` returns total
  free space including fragments, not the largest contiguous
  block (Inside Macintosh Vol I line 62055). Allocate
  conservatively — use ~75% of the value to leave headroom
  for the system and account for fragmentation.
- Calculate peer slots from: `(available - global_overhead) /
  per_peer_cost`
- Per-peer cost: TCP recv buffer (2-8 KB) + TCP send buffer
  (1-4 KB) + UDP buffer (512 B) + reassembly buffer (4-64 KB)
  + metadata (~100 B)
- Global overhead: discovery buffer (~256 B), peer array,
  callback table, platform state
- On POSIX: default to 32 peer slots
- On 4 MB Mac SE (~500 KB free): 8-12 peer slots
- On 48 MB Performa (~8 MB free): cap at 32 slots (diminishing
  returns beyond that)

## R4. Async I/O Model

**Decision**: Polling-based on all three platforms, no threads,
no completion routines

**Rationale**: All three platforms support a poll-driven model:
- POSIX: `select()` with non-blocking sockets
- MacTCP: `PBControlAsync()` + poll `ioResult`
- OT: Notifier sets volatile flags, `PT_Poll` reads flags

Avoiding completion routines on MacTCP is practical wisdom
from v1 hardware testing (not documented in Apple books).
The mechanism: TCPAbort during shutdown fires pending
completions at interrupt time into already-freed memory.
Threads are unavailable on System 6 and add complexity on
all platforms. Polling is the lowest common denominator.

**Alternatives considered**:
- MacTCP completion routines: Crash risk during shutdown
  discovered in v1 testing. The safe pattern is
  `ioCompletion = NULL` and poll.
- Threads (POSIX pthreads): Would require mutex/lock
  primitives, not available on Classic Mac, and violates the
  single-event-loop design.

**Implementation notes**:
- MacTCP: Set `pb->ioCompletion = NULL`, poll `pb->ioResult`
  (`== 1` means inProgress/pending, `0` = success, `< 0` =
  error). The pending constant is specifically `1`
  (inProgress), per MacTCP Programming line 7527.
- OT: Notifier sets volatile flags with bitwise OR (e.g.,
  `state->events |= code`). This is already atomic on
  68k/PPC. Real OT projects (GUSI, ssheven) use this pattern
  rather than `OTAtomicSetBit` (which operates on `UInt8*`
  only). Use notifier callbacks as the primary event
  mechanism — the OT book (lines 8987-8998) warns against
  OTLook polling for performance-critical code, and OTLook
  cannot detect completion events.
- POSIX: `select()` on all fds, process ready sockets
- All platforms: `PT_Poll` drives everything, called from
  application event loop

## R5. Chunking and Reassembly

**Decision**: Send-side splits messages exceeding platform
buffer size into chunks with sequence headers. Receive-side
reassembles using a single reassembly buffer per peer.

**Rationale**: The wire protocol specifies chunked messages
with 8-byte overhead (vs 4 for unchunked). Chunk size is
determined by the send buffer size, which varies per platform.
A single reassembly buffer per peer limits memory usage and
avoids the complexity of multiple concurrent reassembly
contexts.

**Alternatives considered**:
- Application-level chunking: Violates Principle II (SDK
  handles the protocol). The developer should not see size
  limits.
- Multiple reassembly contexts per peer: Unnecessary — TCP is
  ordered, so only one message can be in-flight from a given
  peer at a time.

**Implementation notes**:
- Chunk header: 2B length + 1B type + 1B flags (chunked=1) +
  2B sequence + 2B total_chunks = 8 bytes
- Max chunk payload = send_buffer_size - 8
- Reassembly buffer: largest expected message (64 KB per spec)
- 5-second timeout on incomplete reassembly — timer checked in
  `PT_Poll`
- Reassembly keyed by peer + message type (only one in-flight
  per peer due to TCP ordering)

## R6. Discovery Self-Filtering

**Decision**: Filter own discovery broadcasts by comparing
source IP to local IP obtained at init

**Rationale**: The spec says "SDK filters out own broadcasts by
IP." The local IP is obtained once at init (MacTCP:
`GetMyIPAddr`, OT: `OTInetGetInterfaceInfo`, POSIX:
`getsockname` or similar) and stored in the context. Discovery
receive compares the source IP of each packet against this
stored value.

**Alternatives considered**:
- Filter by name: Names are not guaranteed unique per the edge
  cases in the spec. IP is the true identity.
- Don't filter (let app handle): Violates Principle II.

## R7. TCP Accept Model

**Decision**: Single listener per context that auto-accepts
connections into pre-allocated peer slots

**Rationale**: The spec says "Remote side auto-accepts incoming
TCP connections." A single `listen()` (POSIX) or
`TCPPassiveOpen` (MacTCP) or `OTListen` (OT) per context
handles all incoming connections. When a connection arrives, the
SDK finds the matching peer (by source IP from a previous
discovery) or creates a new peer entry, allocates from the
pre-allocated slot pool, and fires `on_connected`.

**Implementation notes**:
- MacTCP: `TCPPassiveOpen` is one-shot — must re-issue after
  each accepted connection. Use a dedicated listener stream.
- OT: Use `tilisten,tcp` module to avoid `kOTLookErr` when
  multiple connections arrive simultaneously. Requires OT
  1.1.1+ (OT book line 9527). No fallback for older OT
  versions — they are easy to upgrade and not worth
  supporting.
- POSIX: Standard `listen()` + `accept()` on non-blocking
  socket
- When peer slots are full: reject connection (close
  immediately), return `PT_ERR_NO_ROOM` via error callback

## R8. clog Integration

**Decision**: Link clog as a static library, include clog.h
only in internal .c files (never in peertalk.h)

**Rationale**: Constitution Principle VII requires logging as a
separate library. The public header must not depend on clog —
applications should not need to have clog headers to compile
against peertalk.h.

**Implementation notes**:
- clog repo at `~/Desktop/clog`, built separately
- CMake: `find_library(clog)` or direct path
- Internal files: `#include "clog.h"`
- Public header: no clog reference
- clog must be built for the same target platform (POSIX, 68k,
  PPC) before peertalk
- For CI: build clog first, then peertalk, linking against
  libclog.a

## R9. Open Transport SEND Throughput

**Decision**: Implement OT cleanly and measure. Do not
pre-assume any limitation.

**Rationale**: The v1 implementation measured 4-7 KB/s SEND
on OT, but the root cause was never confirmed. The claim that
64-byte STREAMS mblk_t allocation causes this was based on an
OT book reference (line 41254) that actually discusses serial
drivers, not TCP. No TCP-specific segment sizing is documented
in the OT book. Other possible causes include MSS=536 from
subnet mask misconfiguration, or suboptimal v1 code (which was
built on incomplete csend OT code). RECV worked fine (36-131
KB/s) on the same hardware and network.

Per Constitution Principle III (Honest About Platform Limits),
if slow SEND is observed again with clean code, document the
measured numbers. But do not carry forward an assumed limitation
from v1 — measure fresh.

**Implementation notes**:
- Implement OT backend with straightforward OTSnd, same
  pattern as GUSI and ssheven (the two most mature real-world
  OT codebases)
- Measure SEND and RECV throughput on real hardware
- If SEND is slow, investigate subnet mask first (should be
  255.255.255.0 explicitly in TCP/IP control panel)
- No workarounds, no batching, no coalescing — just clean
  OTSnd and honest measurement

## R10. Wire Protocol Byte Order

**Decision**: Network byte order (big-endian) for all
multi-byte wire protocol fields

**Rationale**: Standard network convention. 68k Macs are
natively big-endian, so no conversion needed on the primary
Classic Mac target. POSIX and PPC use `htons()`/`ntohs()`.

**Implementation notes**:
- Length fields (2 bytes): `htons()` on send, `ntohs()` on
  receive
- Chunk sequence and total_chunks (2 bytes each): same
- Magic "PTLK" (4 bytes): compared as byte array, no
  endianness concern
- Version field (1 byte): no conversion needed
- Message type (1 byte): no conversion needed

## R11. Classic Mac Application Requirements

**Decision**: Test apps must use WaitNextEvent, TickCount,
and Mac toolbox initialization

**Rationale**: Classic Mac uses cooperative multitasking. A
tight poll loop without WaitNextEvent starves the system and
causes hard crashes. `time()` and `clock()` are unavailable
or broken on Classic Mac — use `TickCount()/60` for seconds.
Discovered during hardware testing: Retro68/LaunchAPPL console
apps have no Toolbox init, so calling WaitNextEvent without
prior InitGraf causes a bus error (`_PortToMap` dereferences
a garbage GrafPort pointer). However, performing full Toolbox
init (InitGraf etc.) before Retro68's console init prevents
its printf window from appearing. The working solution is to
skip Toolbox init entirely and use `Delay()` for sleep, which
does not require a valid GrafPort.

**Alternatives considered**:
- Tight poll loop with no yield: Hard crashes on real hardware
  — the system never gets time to process interrupts and
  housekeeping.
- Full Toolbox init + WaitNextEvent: Conflicts with Retro68's
  console window initialization, causing lost printf output.
- Delay() without Toolbox init: Works. System processes
  interrupts during Delay so OT/MacTCP notifiers still fire.

**Implementation notes**:
- Sleep: `Delay(ticks, &final_ticks)` — no Toolbox needed,
  `ms / 16` approximates ticks (1 tick = 1/60 sec)
- Timing: `TickCount()/60` for seconds (safe at main loop
  time, NOT at interrupt time per ISR safety rules)
- No stdio on Classic Mac — use clog with
  `clog_set_file("PT_Log")` for output
- test_common.h needs `#ifdef PT_PLATFORM_MACTCP` /
  `PT_PLATFORM_OT` sections for platform-appropriate sleep
  and timing
- Reference: v1 test apps at `~/peertalk/tests/mac/` for
  patterns that worked on real hardware

## R12. Retro68 Import Library Mapping (OT)

**Decision**: Link OpenTransportAppPPC (public API) alongside
OpenTransportLib (core)

**Rationale**: OT headers define non-InContext function names
as macros that expand to InContext variants (e.g.,
`#define OTOpenEndpoint(...) OTOpenEndpointInContext(..., NULL)`),
but Retro68 import libraries only export the non-InContext
symbols. Calling the InContext variants produces linker errors
for undefined symbols like `.OTOpenEndpointInContext`. The
distinction between `OpenTransportLib` (kernel/internal
symbols) and `OpenTransportAppPPC` (application-facing public
API symbols like `OTOpenEndpoint`, `OTCloseProvider`) is not
documented in the OT book and was discovered by running `nm`
on the import libraries.

**Alternatives considered**:
- Link only OpenTransportLib: Missing public API symbols
  (`OTOpenEndpoint`, `OTCloseProvider`, etc.)
- Link only OpenTransportAppPPC: Missing core symbols needed
  by the public API wrappers
- Use InContext variants directly: Not exported by Retro68
  import libraries

**Implementation notes**:
- `#undef OTOpenEndpoint`, `#undef InitOpenTransport`,
  `#undef CloseOpenTransport` after OT includes in pt_ot.c
- Use non-InContext function names in source code
- CMakeLists.txt: `find_library(OT_APP_LIB OpenTransportAppPPC)`
  and link both `${OT_APP_LIB}` and `${OT_LIB}`
- This applies to PPC builds only — 68k OT builds would need
  equivalent 68k import libraries (not yet tested)

## R13. Cross-Platform Chunk Reassembly

**Decision**: Receiver must derive chunk offsets from actual chunk
payload sizes, not from its own tcp_send_size

**Rationale**: Peers on different platforms have different buffer sizes
(POSIX: 4 KB send, Mac SE: 1 KB send). The sender chunks based on its
own tcp_send_size, but the receiver used its own tcp_send_size to
calculate reassembly offsets. When these differ, chunks are placed at
wrong offsets, corrupting the reassembled message. The fix is to record
the first chunk's payload length as the stride for reassembly offsets.
The last chunk may be shorter — its actual length is in the chunk
header's payload_len field.

**Alternatives considered**:
- Exchange buffer sizes during connection handshake: Adds wire protocol
  complexity, violates Principle IV (no knobs). Not needed — chunk
  payload lengths are already in the header.
- Use total_chunks and total message size to infer chunk size: Fragile,
  requires knowing total size before first chunk.

**Source**: Code review of pt_messaging.c:205-228 during cross-platform
testing (POSIX ↔ Classic Mac)

## R14. Peers Array Must Be Inside Contiguous Block

**Decision**: The PT_Peer_Internal array must be allocated inside the
single contiguous memory block, not as a separate allocation

**Rationale**: The initial implementation made two allocations — one for
per-peer buffers (memory_block) and one for the peers array
(ctx->peers). This violates R3 (single contiguous block) and FR-009
(zero malloc after init implies only one malloc at init). On Classic
Mac, two NewPtrClear calls increase heap fragmentation. The fix is to
include sizeof(PT_Peer_Internal) * max_peers in the
pt_memory_calculate_size() total, and carve the peers array from the
front of the contiguous block.

**Alternatives considered**:
- Keep separate allocation but count it in memory budget: Still
  violates the single-allocation principle and increases fragmentation.
- Embed peer metadata inline with buffers: Would change the memory
  layout significantly and complicate pointer arithmetic.

**Source**: Code review of pt_memory.c:92-119 during analyze audit

## R15. Platform Buffer Allocations in Memory Budget

**Decision**: Platform-specific buffer allocations (MacTCP stream
buffers, OT endpoint overhead) must be included in the FreeMem()
sizing calculation

**Rationale**: pt_memory_calculate_size() only accounts for SDK-level
per-peer buffers (tcp_recv, tcp_send, udp, reassembly). MacTCP
allocates 8 KB per TCP stream buffer and 4 KB per UDP stream buffer
via NewPtrClear() — these are required by the MacTCP driver and cannot
be avoided. On a 4 MB Mac SE with ~500 KB free, the SDK budgets ~84 KB
for its contiguous block but the actual heap consumption is roughly
double when platform buffers are included. The sizing calculation must
subtract the expected platform overhead before computing max_peers,
or include platform buffer sizes in per_peer cost.

**Alternatives considered**:
- Reduce SDK buffer sizes to compensate: Would degrade performance
  without addressing the root cause — the budget is simply wrong.
- Use smaller MacTCP buffers: MacTCP requires minimum buffer sizes
  for reliable operation. 8 KB is already conservative for TCP.

**Source**: Code review of pt_mactcp.c:320-342 and pt_memory.c during
analyze audit. MacTCP Programming Guide specifies minimum buffer
requirements for TCPCreate.

## R16. V1 Mac Test App Patterns

**Decision**: Rewrite test_common.h Mac platform layer using proven v1
patterns from ~/peertalk/tests/mac/ as reference implementation

**Rationale**: The v2 test apps were built POSIX-first with Mac stubs
added incrementally during debugging. Three fix attempts failed:
(1) Full Toolbox init (InitGraf, InitFonts, InitWindows) before
Retro68's console init prevents printf window from appearing.
(2) WaitNextEvent without Toolbox init crashes in _PortToMap (Bus
Error) because GrafPort pointer is invalid. (3) Delay() works on PPC
but doesn't process the event queue on 68k, so Retro68 console output
never reaches LaunchAPPL. The v1 project has working test apps proven
on real hardware (Performa 6200, Mac SE, Performa 630) that use:
full Toolbox init, WaitNextEvent with sleep=0 for event polling,
TickCount()/60 for timing, and a 32KB log buffer with network streaming
to perf_partner via log_stream.h. The key difference: v1 apps use
Toolbox init because they create their own windows (status_window.h)
rather than relying on Retro68's console. The v2 approach should
either adopt the same pattern (own windows, full Toolbox init) or
accept Delay()-only sleep with file-based log collection.

**Alternatives considered**:
- Continue incremental debugging: Three attempts failed, diminishing
  returns on the current approach.
- Port v1 test apps directly: Different API (PeerTalk v1 vs v2), but
  the Mac platform patterns are transferable.
- Use Retro68 console with Delay only: Works for PPC (proven with
  OT backend), but 68k output doesn't reach LaunchAPPL without event
  processing.

**Source**: Hardware testing on Performa 6400, Performa 630, Mac SE.
V1 reference code at ~/peertalk/tests/mac/test_throughput.c,
~/peertalk/tests/mac/test_latency.c, ~/peertalk/tests/mac/log_stream.h

## R17. Classic Mac Heap Preparation

**Decision**: Call MaxApplZone() and MoreMasters() at the start
of PT_Init on Classic Mac platforms, before any NewPtrClear
allocations.

**Rationale**: Without MaxApplZone(), the application heap
starts at its SIZE resource minimum (often 32-64 KB for
Retro68/LaunchAPPL apps). FreeMem() returns free space in
this tiny initial heap, not available system memory. All
subsequent allocations fail or return undersized blocks.

The v1 SDK handled this via PeerTalk_Bootstrap(), called
explicitly by the test app before Toolbox init. The v2 SDK
hides this inside PT_Init per Principle II (SDK Handles the
Protocol) and Principle IV (Simple Defaults, No Knobs).

MoreMasters() pre-allocates master pointer blocks, preventing
heap fragmentation from incremental master pointer allocation.
Call 4 times per v1 pattern (Inside Macintosh Vol I).

**Alternatives considered**:
- Expose a Bootstrap function like v1: Violates Principle IV.
  The developer should not need to know about MaxApplZone.
- Call from test_common.h only: Only fixes test apps, not the
  SDK itself. Any developer using PT_Init on Classic Mac would
  hit the same problem.

**Source**: Hardware testing crash on Performa 6400 and
Performa 630 (2026-03-01). Root cause traced to v1 SDK at
~/peertalk/src/core/buffer_pool.c:85-89 which calls
MaxApplZone + 4x MoreMasters before any allocations.

**Implementation notes**:
- Called in PT_Init() (pt_core.c) BEFORE the NewPtrClear
  for the context struct — not in pt_memory_allocate()
- Also called in test_init_toolbox() (test_common.h) before
  clog_set_file, since clog runs before PT_Init
- Must be called before ANY NewPtrClear or File Manager call
- MaxApplZone is idempotent — safe to call even if already
  called
- Requires <Memory.h> on Classic Mac

## R18. Classic Mac Test App Design Constraints

**Decision**: Test apps for Classic Mac must not use malloc
after PT_Init, must log to clog (not just printf), and must
account for limited Retro68 console output.

**Rationale**: Three issues discovered from v1 vs v2 deep
review:

1. test_chat.c uses malloc(msg_len) for send buffers up to
   65000 bytes. On Classic Mac, the heap is mostly consumed by
   PT_Init's contiguous block. malloc fails, test crashes.
   Fix: use static buffer on Mac, limit test sizes to fit
   reassembly buffer.

2. All test apps use printf for progress output. On Classic
   Mac with Retro68 console and no WaitNextEvent, printf
   output may never reach LaunchAPPL stdout. The only reliable
   output channel is the PT_Log file via clog. Test apps must
   use CLOG_INFO for key events.

3. v1 test apps were full Toolbox apps with windows and
   WaitNextEvent. v2 uses Retro68 console + Delay(). This is
   a valid simpler design (fewer LOC per Principle IX) but
   means test output must go through clog, not printf.

**Alternatives considered**:
- Full Toolbox init like v1: Adds ~200 lines per test app
  for InitGraf/InitFonts/window creation. Not needed for
  basic verification. Would conflict with Retro68 console.
- WaitNextEvent in main loop: Crashes on PPC without
  Toolbox init (_PortToMap bus error). Delay() fires
  interrupts and deferred tasks, which is sufficient for
  MacTCP ASRs and OT notifiers.

**Source**: v1 vs v2 deep comparison (2026-03-03). v1 at
~/peertalk/ uses PeerTalk_Bootstrap + full Toolbox + status
windows. v2 at ~/Desktop/peertalk/ uses Retro68 console +
Delay(). Both are valid but v2 test apps need Classic Mac
awareness.

## R19. TCP Send Two-Call Bug (MacTCP)

**Decision**: Build complete TCP frame (header + payload)
in peer's tcp_send_buf, one tcp_send call per frame.

**Root cause**: `send_tcp_frame()` made two separate
`tcp_send()` calls — header then payload. On POSIX (synchronous
write), both complete immediately. On MacTCP (async PBControlAsync),
the second call fails because the first is still in progress
(`ioResult == inProgress`). This caused test_reliable to fail:
Mac received Move 1 but couldn't send its response.

**Fix**: Assemble complete frame in `peer->tcp_send_buf` before
calling `tcp_send` once. Added self-copy guard in mactcp_tcp_send
and ot_tcp_send to skip memcpy when data is already in tcp_send_buf.

**Verified**: test_reliable PASS on Performa 6200 (PPC/MacTCP)
with POSIX peer. All 10 moves exchanged bidirectionally.

## R20. Known Issues — Retro68/LaunchAPPL

**Issue 1**: All Retro68 binaries transferred via LaunchAPPL
appear as "Retro68App" on the Mac, making it hard to distinguish
which test is running. This is a Retro68/LaunchAPPL limitation.
**Workaround**: Status window title shows the test name.

**Issue 2**: All test apps write to the same `PT_Log` file via
clog. Running multiple tests overwrites the previous log.
**Future**: Use test-specific log names (e.g., `PT_Lifecycle`,
`PT_Reliable`) to preserve logs across test runs.

**Issue 3**: Peer name shows empty when a Mac peer connects via
TCP before POSIX discovers it. The incoming connection creates a
peer entry with no name. Discovery packets may arrive after
TCP connect on slower networks. Cosmetic — doesn't affect test
functionality.

**Issue 4**: `move_num` in test_reliable shows byte-swapped values
(256 instead of 1) on cross-platform runs. Expected: SDK delivers
raw bytes without byte-swapping application payloads. Apps doing
cross-platform struct exchange must byte-swap themselves.

## R21. Chunked TCP Receive Failure on Classic Mac

**Decision**: Messages requiring chunking (>2KB payload) fail to
be received on Classic Mac. The tcp_recv_buf (2048 bytes) is too
small to receive chunk frames from a POSIX sender.

**Rationale**: When POSIX sends a 4KB message, it gets split into
chunks with 8-byte chunked headers. Each chunk frame is up to
tcp_send_size (4096 on POSIX). The Mac receiver's tcp_recv_buf
is only 2048 bytes — it cannot hold a single chunk frame from
a POSIX sender. The receive loop gets partial frames and
eventually times out. This explains why messages up to 2000 bytes
(which fit in a single unchunked frame with 4-byte header = 2004
bytes, under 2048) work fine, but 4000-byte messages fail.

**Fix applied (T054)**: Increased Mac tcp_recv_buf from 2048 to 4100
in all memory tiers (pt_memory.c mac_size_from_memory). This allows
the recv buffer to hold a complete chunk frame from a POSIX sender
(up to 4096 bytes) plus a partial next frame header.

**Result**: test_chat now receives 6/10 messages on both P6400 (OT)
and P6200 (MacTCP): 10B, 100B, 500B, 1000B, 2000B, 4000B all VALID.
Messages ≥8000B correctly fail with "Reassembly buffer too small"
(reassembly_buf=4096 on the <2MB memory tier). This is expected —
the reassembly buffer limits max message size, not the recv buffer.

**Alternatives considered**:
- Reduce POSIX chunk size to fit Mac recv buffer: Would limit
  POSIX-to-POSIX performance unnecessarily.
- Dynamic recv buffer sizing based on sender's buffer: Violates
  Principle VI (no runtime adaptation).

**Source**: Hardware testing on Performa 6400 (OT) and
Performa 6200 (MacTCP), 2026-03-04. Before fix: 5/10 received,
timed out on 4000B (tcp_recv=2048). After fix: 6/10 received,
4000B works (tcp_recv=4100), 8KB+ correctly rejected by reassembly.

## R22. POSIX Clog Timestamp Overflow

**Decision**: Cosmetic issue — clog timestamps on POSIX show
unsigned overflow values (e.g., 18446744073717502) after the
first few seconds of operation.

**Rationale**: The clog library's POSIX time function produces
values that wrap to near-UINT64_MAX when cast to unsigned.
Initial timestamps (0, 1, 2) are correct, then values jump to
~1.8x10^19. This does not affect test logic (test_time_sec()
uses clock_gettime separately and works correctly) but makes
POSIX clog output hard to read. Mac clog timestamps are fine
(uses TickCount).

**Fix**: This is a clog bug, not a peertalk bug. Fix in the
clog repository (~/Desktop/clog).

**Source**: All POSIX test runs on 2026-03-04.

## R23. test_chat Disconnect Reason ERROR on Mac

**Decision**: When POSIX sender finishes test_chat and shuts down,
the Mac receiver reports disconnect reason ERROR (2) instead of
QUIT (0).

**Rationale**: The POSIX sender sends a goodbye frame (type 255)
before closing the TCP connection. However, the Mac receiver is
busy processing "Reassembly buffer too small" errors for the
8KB+ messages that follow the 4KB message. The goodbye frame
arrives while the Mac is in the error callback path. The TCP
connection is then closed by the OS before the Mac processes the
goodbye, so the Mac sees a broken connection (ERROR) rather than
a clean goodbye (QUIT).

**Fix options**: (a) Add a small delay on POSIX after sending
goodbye before closing socket. (b) Accept ERROR as valid when
the receiver has outstanding reassembly errors. (c) Investigate
whether the TCP receive path drops frames during error processing.

**Source**: Mac SE PT_Log from test_chat, 2026-03-04. Line 28:
"[DISCONNECTED] Alice (ERROR)" after 4 consecutive reassembly
errors.

## R24. Machine Identification via Gestalt

**Decision**: Use Gestalt() to identify the Mac model, CPU,
and system version at PT_Init time. Log this information for
easier debugging and log identification.

**Rationale**: When collecting PT_Log files from multiple Macs,
there is no way to tell which machine produced which log. The
Inside Macintosh Volume VI Gestalt Manager (Chapter 1) provides
selectors for machine type (gestaltMachineType), CPU
(gestaltProcessorType), system version (gestaltSystemVersion),
and FPU (gestaltFPUType). These are safe to call at any time
with predefined selectors (Table B-3, interrupt-safe). On POSIX,
equivalent info can come from uname().

**Implementation**: Add a pt_log_platform_info() helper called
from PT_Init after platform init. Log: machine model, CPU type,
system version, available memory. This makes every PT_Log
self-identifying.

**Source**: User request, 2026-03-04. Reference: Inside Macintosh
Volume VI, Gestalt Manager; ~/peertalk/books/
Inside_Macintosh_Volume_VI_1991.txt.

## R25. Discovery Packet Buffer Overflow via Unterminated Name

**Decision**: Discovery receive must validate null terminator exists
within received bytes before calling strlen().

**Rationale**: pt_discovery.c:62 calls `strlen(name)` on the name
field of an incoming discovery packet without verifying it is
null-terminated within the `len` bytes received. A malformed packet
(malicious or corrupt) with no null byte causes strlen to read past
the buffer. The fix is to use memchr() to find the null terminator
within bounds before computing namelen.

**Alternatives considered**:
- Rely on sender always sending null-terminated names: Unsafe —
  cannot trust network input.
- Cap name at PT_NAME_MAX without null check: strlen still reads
  past buffer; the cap only limits what is copied, not what is read.

**Source**: Code review of pt_discovery.c, 2026-03-05.

## R26. POSIX TCP Partial Send Data Loss

**Decision**: posix_tcp_send must not return PT_OK when EAGAIN
causes a partial send. Must return PT_ERR_SEND_FAILED.

**Rationale**: When send() returns EAGAIN mid-stream (some bytes
sent, rest would block), posix_tcp_send breaks out of the loop and
returns PT_OK. The caller (send_tcp_frame) believes the entire frame
was transmitted. The receiving peer gets a truncated frame, corrupting
TCP framing for all subsequent data on that stream. The fix is to
return PT_ERR_SEND_FAILED when total < len after the send loop.
This is consistent with the SDK philosophy of simple send path
(Plan: "tcp_send returns an error and the application decides").

**Alternatives considered**:
- Implement a send queue with drain-on-poll: Violates the design
  philosophy of keeping the send path simple. Adds complexity that
  TCP already handles (backpressure via EAGAIN).
- Retry with select() until writable: Blocks the poll loop, violates
  the non-blocking I/O model.

**Source**: Code review of pt_posix.c:315-330, 2026-03-05.

## R27. Non-Atomic Flag Clearing in Classic Mac Backends

**Decision**: The `flags &= ~FLAG` pattern in MacTCP and OT poll
loops is a read-modify-write that can lose flag bits set by ASR/
notifier between the read and write.

**Rationale**: When main-loop code does `ts->flags &= ~FLAG_DATA`,
if the ASR fires between the read (flags=0x01) and write
(flags=0x00) and sets FLAG_CLOSE (0x02), the write overwrites to
0x00 — FLAG_CLOSE is lost. On MacTCP, this could cause missed
close events. On OT, notifiers run at deferred task time (lower
risk but still possible). The fix is to read flags once into a
local variable, then atomically clear only the processed bits.
On 68k: disable interrupts around the clear. On PPC/OT: use
OTAtomicClearBit (Table C-1 safe at hardware interrupt time).

**Alternatives considered**:
- Individual flag checks without clearing: Flags would fire
  repeatedly every poll cycle until the condition is resolved.
- Process all flags before clearing any: Same race — the clear
  step still has the window.

**Source**: Code review of pt_mactcp.c:703-704 and pt_ot.c:604-605,
2026-03-05. ISR safety rules (.claude/rules/isr-safety.md).

## R28. Reassembly Type Check Missing

**Decision**: Chunk acceptance in pt_messaging.c must verify
msg_type matches the in-progress reassembly type.

**Rationale**: When a chunked message is in progress
(reassembly_total > 0), subsequent chunks with seq > 0 are placed
into the reassembly buffer using reassembly_stride offsets. But the
code does not check that the chunk's msg_type matches
reassembly_type. If a different message type arrives with seq > 0
and matching total, it would be placed at the wrong buffer offset,
corrupting the reassembly. Over TCP (in-order delivery) this is
unlikely with a single sender, but could occur if the sender
interleaves messages of different types.

**Alternatives considered**:
- Reset reassembly on type mismatch: Would lose the in-progress
  message. Better to reject the mismatched chunk.
- Multiple reassembly buffers per peer: Violates Principle V
  (pre-allocate) and IX (keep it small).

**Source**: Code review of pt_messaging.c:228, 2026-03-05.

## R29. OT Async Unbind/Rebind Race

**Decision**: ot_tcp_disconnect calls OTUnbind and OTBind on
asynchronous endpoints without waiting for completion.

**Rationale**: OT endpoints are set to async mode at creation
(OTSetAsynchronous). When ot_tcp_disconnect calls OTUnbind, it
returns immediately — the unbind completes asynchronously. The
code then immediately calls OTBind and sets state to EP_FREE.
If another operation finds this slot via find_free_ep before
the unbind/rebind completes, the endpoint is in an invalid state.
The fix is to switch the endpoint to synchronous mode before
unbind/rebind (OTSetSynchronous/OTSetAsynchronous), or track an
intermediate "resetting" state.

**Alternatives considered**:
- Always use synchronous endpoints: Would block the poll loop
  during connect/disconnect — violates the non-blocking model.
- Close and reopen endpoints: Expensive (OTOpenEndpoint is slow)
  and wastes endpoint resources on Classic Mac.

**Source**: Code review of pt_ot.c:580-593, 2026-03-05. OT
documentation: Networking With Open Transport, Appendix C.

## R30. Chat Application — Reuse csend GUI with PeerTalk SDK

**Decision**: Build a chat application that reuses the csend GUI
resources (DLOG 128, DITL 128, MBAR, CNTL 6) but replaces all
networking with PeerTalk SDK calls. Single Classic Mac source set
builds for all three Mac targets (68k MacTCP, PPC OT, PPC MacTCP).
POSIX gets a terminal-based version.

**Rationale**: csend (~/csend/) is a working Classic Mac chat app
with separate MacTCP and OT backends (~5000 lines of networking
code per platform). PeerTalk abstracts the transport, so the chat
app only needs UI + SDK calls. This demonstrates the SDK's value
proposition: one codebase, three platforms.

**csend GUI architecture** (from ~/csend/MPW_resources/csend.r):
- DLOG 128 "mainwindow": 618x419 noGrowDocProc with close box
- DITL 128: 7 items — peer list (1, user item + List Manager),
  messages area (2, user item + TEHandle), input field (3, user
  item + TEHandle), Send button (4), Broadcast checkbox (5),
  scrollbar placeholder (6, CNTL 6), Show Debug checkbox (7)
- MBAR 128: Apple menu (1) + File menu (128, Quit only)

**Key csend patterns to reuse**:
- TextEdit for messages display with manual scrollbar sync
  (TEAutoView false, AdjustMessagesScrollbar on append)
- TextEdit for input with InsetRect(1,1) border
- List Manager for peer list (LNew with single selection)
- GrafPort save/restore around all drawing
- TEHandle lock pattern (HGetState/HLock/HSetState)
- Mac line endings ("\r" not "\n")
- 30K text limit guard (TextEdit 32K max)
- Peer list / Broadcast checkbox mutual exclusion

**Key differences from csend**:
- No separate MacTCP/OT source dirs — PeerTalk handles transport
- No custom wire protocol — use PT_RegisterMessage + PT_Send
- No TCP connection pool or state machine — PeerTalk manages
  persistent connections
- No UDP discovery code — PT_StartDiscovery + callbacks
- No DNR, no stream buffers, no async PBs in app code
- Remove "Perform Test" menu item (not needed)
- Remove "Show Debug" checkbox (clog handles logging)

**POSIX version**: Terminal-based, single-threaded poll loop.
Read stdin (non-blocking), call PT_Poll(), display received
messages to stdout. Commands: /list, /send N msg, /broadcast msg,
/quit. No threads (PeerTalk is poll-driven).

**Alternatives considered**:
- Port csend GUI to a new framework: Unnecessary — the Dialog
  Manager UI works well and is proven on real hardware.
- ncurses/GTK for POSIX GUI: Over-engineering for an SDK example.
  Terminal chat is simple and effective.
- Separate app per platform: Defeats the purpose of demonstrating
  PeerTalk's cross-platform abstraction.

**Source**: Deep analysis of ~/csend/ codebase, 2026-03-05.
csend.r resource definitions, dialog_messages.c, dialog_input.c,
dialog_peerlist.c, dialog.c, main.c.

## R31. Classic Mac Dialog GUI Stability Patterns (from csend)

**Decision**: The PeerTalk chat app (Phase 15) MUST follow all
stability patterns proven in csend's GUI code. These were learned
through crashes on real Classic Mac hardware.

**Handle memory safety (crash source #1)**:
- ALWAYS use HGetState/HLock/HSetState triplet when dereferencing
  any Handle (TEHandle, ListHandle, ControlHandle).
- ALWAYS check `*handle != NULL` AFTER HLock before dereferencing.
  A disposed or corrupted handle can have NULL master pointer.
- ALWAYS restore handle state on error paths (HSetState before
  every early return after HLock).
- When accessing TextEdit hText, lock BOTH the outer TEHandle AND
  the inner hText handle (double-lock pattern for GetInputText).
- NULL-out handles immediately after TEDispose/LDispose to prevent
  use-after-free.

**GrafPort management (crash source #2)**:
- EVERY drawing function must GetPort/SetPort/SetPort (save,
  set to dialog window, restore).
- Check gMainWindow != NULL before SetPort — NULL causes bus error
  on 68k.
- Check window port for NULL before any drawing operations.
- Set port BEFORE GlobalToLocal — it uses current port coordinates.

**TextEdit 32K limit**:
- Guard against 32K limit: `if (currentLength + textLen < 30000)`
  with 30K safety margin. Exceeding corrupts TE internal arrays.
- Call TEAutoView(false, ...) for manually-scrolled TE fields.

**Scrollbar synchronization**:
- Handle thumb drag separately from arrow/page clicks (thumb uses
  nil action proc + post-track sync, arrows use MyScrollAction).
- Track scrolled-to-bottom state before append, auto-scroll to
  bottom only if user was already at bottom.
- Guard against zero/negative lineHeight in scroll calculations
  (division by zero crash).
- Clamp scroll values to valid range (negative or oversized values
  corrupt destRect, making text invisible).
- InvalRect after TEScroll to force clean redraw (prevents
  artifacts).
- Check contrlVis AND contrlHilite before processing scrollbar
  clicks.

**Dialog item validation**:
- Always verify item type from GetDialogItem matches expectation
  (userItem, ctrlItem+chkCtrl, etc.) before casting handle.
- Always check itemHandle for NULL from GetDialogItem.
- Validate rect dimensions after InsetRect (degenerate rect crashes
  TENew).
- Use fallback cell height if font metrics return zero.

**Event loop architecture**:
- Three-tier dispatch: (1) custom click handling for scrollbar/
  peerlist/input TE, (2) DialogSelect for buttons/checkboxes,
  (3) HandleEvent for system events. Never let DialogSelect handle
  custom TE clicks — it doesn't know about user-item TEHandles.
- Track eventHandledByApp flag to prevent double-handling.
- Throttle update events with TickCount (~100ms minimum interval).
- Throttle TEIdle to cursor blink rate (~250ms / 15 ticks).
- Throttle peer list updates to 5-second intervals.
- Guard against TickCount wrap-around (~27 hours on 68k):
  check `current < last` before computing elapsed.

**Dirty flag update system**:
- Use invalidation flags (gMessagesTENeedsUpdate, etc.) instead of
  immediate redraws. Batch updates to the next natural draw cycle.
  Immediate redraws from callbacks cause flicker and re-entrant
  drawing bugs.

**Initialization and cleanup order**:
- Toolbox init: InitGraf → InitFonts → InitWindows → InitMenus →
  TEInit → InitDialogs. Each depends on previous.
- MaxApplZone() BEFORE any Toolbox init.
- Clean up in reverse order: UI components → network → system
  handlers → logging. Within dialog: list → input TE → messages
  TE → DisposeDialog.
- On partial init failure, clean up only what was successfully
  created (track per-component success flags).
- If scrollbar init fails, also dispose the already-created TE
  (they are a unit).

**List Manager patterns**:
- LActivate(false, ...) before LDispose (prevents dangling refs).
- Preserve selection across rebuilds by matching peer identity
  (IP+name), not list index.
- Verify LLastClick result with LGetSelect (click may have
  deselected the cell).
- Lock List handle before checking rView for hit-testing.
- lOnlyOne selection flag to prevent multi-select complexity.

**Miscellaneous stability**:
- BeginUpdate/EndUpdate brackets for ALL update event handling
  (missing EndUpdate causes infinite update events).
- DrawDialog BEFORE custom component updates in update handler.
- EraseRect before TEUpdate (TE doesn't erase its background).
- FrameRect around user-item TE fields for visual borders.
- Use `\r` not `\n` for Classic Mac line endings in TextEdit.
- Null-terminate output buffers even on error paths.
- Preserve user input on send failure (only clear on success).
- Deselect broadcast checkbox when peer selected, and vice versa.
- Check FrontWindow() == gMainWindow before accepting keyboard
  input.
- Restore focus to input TE after every send operation.
- Define missing Control Manager constants (#ifndef inUpButton)
  and HiWord/LoWord for older headers.
- YieldTimeToSystem() with minimal WaitNextEvent(0, &event, 1L,
  NULL) during long operations.

**Source**: Exhaustive analysis of ~/csend/shared/classic_mac/ui/
dialog_messages.c (333 lines), dialog_input.c (562 lines),
dialog_peerlist.c (298 lines), classic_mac_mactcp/dialog.c (560
lines), classic_mac_mactcp/main.c (516 lines), classic_mac_ot/
main.c, classic_mac_ot/dialog.c. 61 individual crash-prevention
patterns identified.

## R32. Task Runner Improvements

**Decision**: Rename `tools/overnight-build.sh` to `tools/autorun.sh`
and address reliability issues discovered during multi-day use.

**Rationale**: The runner was used for 3 overnight and daytime runs
(2026-03-04 through 2026-03-05). Key findings:

1. **Name misleading**: Called "overnight-build" but used during the
   day. Rename to "autorun" — short, accurate, no time-of-day
   assumption.

2. **Log buffering**: `claude -p ... | tee` buffers all output until
   session ends. The iteration log is 0 bytes during the entire run,
   making it impossible to monitor progress. Fix: use `stdbuf -oL` or
   `unbuffer` for line-buffered output, or write progress to a
   separate status file.

3. **Hardware tasks marked done without execution**: Claude marked
   T078 "all 3 Macs verified" but only tested P6400, writing
   "P6200/SE deferred, not online" in the commit. The prompt must
   explicitly forbid this. Fix: add rule "Do NOT mark hardware tasks
   complete unless you ran on every machine listed in the task. If a
   machine is unreachable, leave the task incomplete and note which
   machines succeeded."

4. **Only POSIX build checked**: Post-iteration build check only
   verifies `build/` (POSIX). Doesn't verify 68k or PPC cross-
   compilation. Fix: check all build dirs that exist.

5. **No per-iteration timeout**: A Claude session can run for hours
   if stuck in spec artifact updates. Fix: wrap with `timeout 30m`.

6. **Prompt hardcodes machine availability**: Says "A Performa 6400
   is powered on" regardless of actual state. Fix: run
   `test_connection` on each machine before building the prompt,
   include only reachable machines.

7. **All tasks in one iteration**: Claude completed 24 tasks in one
   session, making stuck detection ineffective. Consider limiting
   to one phase per iteration for better granularity.

**Alternatives considered**: Moving to a CI system (GitHub Actions,
Jenkins) — rejected as premature. The shell script is sufficient
for the current single-developer workflow. The improvements keep
it simple while fixing the reliability gaps.

**Source**: Observed during overnight runs on 2026-03-04 and
2026-03-05. Hardware test gaps found by manual log review.

## R33. Chat App Hardware Verification — Peer List and Screen Size Issues

**Decision**: The chat app has two blocking issues discovered during
hardware verification on P6400, P6200, and Mac SE:

1. **Peer list shows only connected peers**: dialog.c:508 filters
   `PT_GetPeerState(peer) != PT_PEER_CONNECTED`, hiding discovered-
   but-not-connected peers. The app sets `gPeerListDirty = true` in
   `OnDiscovered()` but `DialogUpdatePeerList()` skips non-connected
   peers. Since the app has no auto-connect and no Connect button,
   the peer list is permanently empty. The original csend showed
   discovered peers and let the user initiate connections.

2. **Dialog too large for Mac SE**: DLOG 128 specifies bounds
   top=49, left=10, bottom=468, right=628 (419x618 pixels). Mac SE
   screen is 342x512. GetNewDialog returns a window that doesn't fit,
   likely causing immediate failure. The original csend had the same
   dialog size and also wouldn't fit on Mac SE.

**Rationale**: The chat app (T080-T087) was built to demonstrate the
SDK but never tested with actual user interaction on hardware. The
test apps auto-connect on discovery, masking the fact that the chat
app provides no connection mechanism. Screen size was not tested on
the compact Mac because csend also used a large dialog.

**Alternatives considered**:
- Auto-connect on discovery (like test apps) — rejected because the
  Chat app is meant to show the user-initiated connection pattern
  (Constitution I: serves the Chat app use case, which involves
  choosing which peer to talk to)
- Separate "discovered" and "connected" lists — overcomplicated for
  a demo app

**Source**: Hardware testing on Performa 6400 (OT), Performa 6200
(MacTCP), and Mac SE (68k) on 2026-03-06. PT_Chat_App logs from
P6400 and P6200 show "Discovered peer: Alice" but no connection
event. Mac SE app exits immediately with no output.

## R34. MacTCP UDP Message Port Not Listening

**Decision**: PT_StartDiscovery() must call udp_listen() for both
PT_DISCOVERY_PORT (7353) and PT_UDP_MSG_PORT (7355).

**Rationale**: MacTCP requires a pre-posted async UDPRead parameter
block before datagrams can be received on any port. The ASR (async
service routine) only fires for ports with read_pending=1. Without
udp_listen being called for port 7355, mactcp_poll() never processes
incoming fast messages — read_pending stays 0 forever.

POSIX and OT are unaffected because they poll both UDP sockets
unconditionally: POSIX always adds udp_msg_fd to select(), OT always
calls poll_udp for both slots.

The fix is a single additional udp_listen() call in PT_StartDiscovery()
(pt_core.c) after the existing discovery port listen.

**Alternatives considered**:
- Move udp_listen calls to PT_Init: Discovery and messaging should
  only start when the developer requests it. Listening at init
  violates the explicit start model.
- Post UDPRead in platform init: Same issue — would listen before
  the developer calls PT_StartDiscovery.

**Source**: Hardware testing on Performa 6200 (PPC/MacTCP) and Mac SE
(68k/MacTCP) on 2026-03-06. test_fast logs show 0 messages received.
Root cause traced to pt_core.c:355 (only discovery port) and
pt_mactcp.c:485 (udp_listen posts UDPRead), :258 (issue_udp_read
sets read_pending=1), :826 (poll only processes if read_pending).

## R35. Mac SE Hardware Testing (Reinstated)

**Decision**: ~~Suspend hardware testing on Mac SE~~ **Reinstated** —
Mac SE is back in the hardware test matrix. All 4 tests PASS after
clean reboot (R38). Original crash was transient MacTCP state
corruption, not systematic. T125 adds UDP shutdown spin-wait to
prevent the root cause.

**Rationale**: After applying the R34 UDP fix (adding udp_listen for
PT_UDP_MSG_PORT), the test_fast binary caused a hard freeze on the
Mac SE — system hung, only mouse tracking (interrupt-driven) still
worked. The additional udp_listen call posts a second async UDPRead
parameter block, doubling MacTCP UDP memory pressure. On a 4MB Mac SE
with tight heap, this may exceed available memory or trigger a MacTCP
driver crash. The Mac SE has no FTP for log collection, making crash
diagnosis difficult (LaunchAPPL-only). P6400 and P6200 provide full
coverage of both OT and MacTCP backends on PPC with better
diagnostics.

**Alternatives considered**:
- Debug the 68k crash immediately: No crash log available (LaunchAPPL
  timeout, no FTP). Would require iterative blind debugging via
  LaunchAPPL, very slow cycle time.
- Reduce Mac SE peer count or buffer sizes: May help, but root cause
  is unknown without diagnostic data.

**Source**: Hardware testing on Mac SE, 2026-03-06. test_fast.bin
via LaunchAPPL caused system freeze requiring hard reboot.

## R36. UDP Fast Message Latency Measurement

**Decision**: Measured UDP inter-arrival timing on hardware. Results
exceed the SC-006 target of "under 16ms average latency", but this
reflects sender-side batching, not network transport latency.

**Measured results** (60 messages over ~5 seconds, POSIX sender):
- Performa 6400 (OT): 61ms avg inter-arrival (3633ms / 59 intervals)
- Performa 6200 (MacTCP): 64ms avg inter-arrival (3834ms / 59 intervals)

**Analysis**: The test_fast sender uses `test_time_sec()` (1-second
resolution) to pace sends: 12 messages per tick, 5 ticks = 60 total.
This creates burst patterns (12 msgs in rapid succession, then ~1s
gap), not true 60Hz pacing. The 61-64ms average reflects the burst
distribution, not per-message network latency.

True network latency (time from UDP send to receive) cannot be
measured without clock synchronization between sender and receiver.
The inter-arrival measurement captures sender pacing + network + Mac
poll interval combined.

**Conclusion per Principle III**: The SC-006 "under 16ms average
latency" requirement is not validated by current instrumentation.
Achieving true 16ms inter-arrival would require millisecond-resolution
send pacing (e.g., `usleep(16000)` on POSIX or Time Manager on Mac).
The current burst pattern delivers all 60 messages within the 5-second
window with zero loss on both platforms, which satisfies the
throughput requirement even if pacing granularity is coarse.

**Source**: T120 hardware measurement, 2026-03-06. Instrumentation
added to tests/test_fast.c (test_time_ms_now, inter-arrival logging).

## R37. Unidirectional Test Coverage Gap — Mac Always Passive

**Decision**: Three of four test apps only exercise the passive/receiver
role on Classic Mac hardware. The Mac UDP send, variable-size TCP send,
and connection initiation paths are untested on hardware.

**Rationale**: Role assignment in all test apps uses `name[0] <= 'M'`
for the active role (SENDER/INITIATOR/FIRST) and `name[0] > 'M'` for
the passive role (RECEIVER/RESPONDER/SECOND). Mac apps run via
LaunchAPPL without a `--name` argument, defaulting to "Unnamed"
('U' > 'M'), which permanently assigns the passive role. POSIX peers
run with `--name Alice` ('A' <= 'M'), permanently assigning the active
role.

Affected tests:
- **test_fast**: Mac never calls PT_Send with PT_FAST. The Bomberman
  pattern (Constitution I) requires both peers to send position updates.
  Mac UDP send path completely untested.
- **test_chat**: Mac never calls PT_Send with variable-size payloads.
  Mac SENDER code path exists (static 4KB send_buf, 6 test sizes) but
  is never exercised on hardware. Receiver pass criteria trivially weak:
  `g_msgs_received > 0` (1 of 10 messages = PASS).
- **test_lifecycle**: Mac never calls PT_Connect. RESPONDER role is
  purely passive — only observes callbacks. Mac-initiated TCP connection
  path untested.
- **test_reliable**: Only truly bidirectional test — both sides send AND
  receive alternating moves. This is the only test exercising Mac send.

**Impact on constitution**:
- Principle III (Honest About Platform Limits): Cannot claim Mac send
  paths work without testing them.
- Principle VIII (Test Apps Prove the SDK): Tests must exercise all three
  app patterns. Bomberman needs bidirectional UDP, Chat needs
  bidirectional variable-size TCP.

**Alternatives considered**:
- Run Mac with `--name Alice` via LaunchAPPL: LaunchAPPL does support
  passing arguments, but test_parse_name would need verification on
  Classic Mac. Simplest fix.
- Make tests bidirectional (both sides send and receive): More thorough
  but larger code change. test_reliable already does this well.
- Add separate Mac-sender test runs: Run each test twice (once as sender,
  once as receiver) — doubles hardware test time.

**Source**: Test coverage review of test_fast.c, test_chat.c,
test_lifecycle.c, test_reliable.c and hardware logs from P6400 and P6200,
2026-03-06. All Mac logs show Role: RECEIVER or RESPONDER.

## R38. Mac SE 68k — test_fast Intermittent Crash, Then PASS

**Decision**: test_fast on Mac SE (68k/MacTCP) initially crashed
(log cut off at "Receiving for 5s...", zero messages received).
After clean reboot, test_fast PASSES: 59/60 received, payload
valid, 74ms avg inter-arrival, clean QUIT disconnect and shutdown.

**Rationale**: The R34 fix (dual UDPRead for discovery + message
ports) works correctly on 68k MacTCP when the system is in a clean
state. The initial crash was likely caused by residual corrupted
MacTCP driver state from a previous failed test run — not a
fundamental 68k limitation.

Evidence:
- First run (after earlier test_fast crash from R35): Log cuts off
  at line 20 "Receiving for 5s..." — zero messages, no shutdown.
- test_chat also crashed (no log) when run immediately after.
- After clean reboot: test_chat PASS (6/6, clean), then test_fast
  PASS (59/60, 74ms avg, clean QUIT, clean shutdown).

All 4 tests now PASS on Mac SE:
- test_lifecycle: PASS (2 connect, 2 disconnect, QUIT)
- test_reliable: PASS (10/10 bidirectional, payload valid)
- test_chat: PASS (6/6 received, integrity ok)
- test_fast: PASS (59/60 received, 74ms avg, payload valid)

Platform: machine=5 (Mac SE), cpu=1 (68000), system=607
(System 6.0.7), FreeMem=2.47MB, 32 peers, MacTCP backend.

**R35 superseded**: Mac SE is no longer excluded from hardware
testing. The suspension was based on a crash that is now
understood to be transient (dirty MacTCP state), not systematic.

**Alternatives considered**:
- Keep Mac SE excluded: Overly cautious — all 4 tests now pass.
- Add MacTCP state cleanup before each test: Not possible from
  application level — MacTCP driver state persists until reboot.
- Recommend reboot between test runs on Mac SE: Pragmatic but
  not necessary if tests run sequentially with clean shutdowns.

**Source**: Hardware testing on Mac SE, 2026-03-06. Logs:
downloads/macse/PT_test_fast (crash), downloads/macse/PT_test_fast_2
(PASS after reboot).

## R39. MacTCP UDP Shutdown Bug and Buffer Oversizing

**Decision**: Three issues found in MacTCP UDP implementation by
code review comparing V2 with V1 and MacTCP Programmer's Guide.

**Issue 1 — Shutdown frees buffer with pending UDPRead**:
mactcp_shutdown() (pt_mactcp.c:366-420) calls UDPRelease then
immediately DisposePtr on the UDP buffer. If a UDPRead is in
progress (infinite timeout, read_pending=1), the MacTCP driver
may still hold a reference to the buffer. DisposePtr frees the
memory while the driver retains the pointer, causing write-after-
free on next datagram arrival. This corrupts MacTCP driver state
that persists until reboot — the likely root cause of R35/R38
crashes.

V1 solved the equivalent problem for TCP (~/peertalk/src/mactcp/
tcp_mactcp.c:540-615) by spin-waiting for pending async operations
to complete before releasing streams. V1 never had outstanding
async UDPRead operations (used synchronous UDPRead after ASR flag),
so the problem didn't exist for UDP in V1.

Fix: After UDPRelease, spin-wait for read_pb.ioResult != inProgress
before calling DisposePtr. Same pattern as V1 TCP shutdown.

**Issue 2 — UDP buffer oversized for messages**:
UDP_BUF_SIZE is 4096 bytes per stream (8192 total for two streams).
MacTCP Programmer's Guide (line 1057): minimum receive buffer is
2048 bytes, recommended "at least 2N+256 bytes where N is largest
datagram". Fast messages are max ~1400 bytes (PT_UDP_MTU_SAFE),
so 2*576+256=1408 minimum, 2048 is safe with headroom. Reducing
UDP_BUF_SIZE from 4096 to 2048 saves 4096 bytes of heap —
significant on a 4MB Mac SE where FreeMem=2.47MB.

Also note: PT_PLATFORM_FIXED_OVERHEAD in pt_memory.c (line 43)
is set to 8192 (2 streams x 4096). Must update to 4096 (2 x 2048)
when buffer size changes, otherwise the memory budget overestimates
platform overhead and allocates fewer peers than possible.

**Issue 3 — ASR flag is dead code**:
udp_asr() (pt_mactcp.c:107-116) sets us->flags |= UDP_FLAG_DATA
when UDPDataArrival fires. But the poll loop (lines 795-854) never
checks us->flags — it only checks read_pending && read_pb.ioResult.
Per MacTCP docs (Programmer's Guide line 1553): UDPDataArrival fires
only when "no UDPRead commands are outstanding." Since V2 always
keeps a UDPRead outstanding, UDPDataArrival rarely fires (only in
the brief window between UDPBfrReturn and issue_udp_read). The flag
is effectively dead code. Fix: remove the flag set from udp_asr,
or check it in poll as a fallback for the no-read-pending case.

**Alternatives considered**:
- Issue 1: Cancel UDPRead with UDPAbort before UDPRelease: No
  UDPAbort exists in MacTCP API. UDPRelease is the cancellation
  mechanism, but it's async — need to wait for completion.
- Issue 2: Keep 4096 for safety margin: Wastes memory with no
  benefit — MacTCP docs are clear on the 2N+256 formula.
- Issue 3: Check flags as primary poll mechanism (like V1): Would
  require switching to synchronous UDPRead-after-flag model.
  Current async model is simpler and works correctly.

**Source**: Code review comparing V2 (src/platform/mactcp/
pt_mactcp.c) with V1 (~/peertalk/src/mactcp/tcp_mactcp.c shutdown
pattern, udp_mactcp.c synchronous read model). MacTCP Programmer's
Guide 1989 lines 1057-1060 (buffer sizing), 1169-1176 (internal
buffering), 1506-1510 (ASR restrictions), 1553-1556 (UDPDataArrival
event semantics). 2026-03-06.

## R40. 68k MacTCP Crash on Bidirectional UDP Send Burst

**Decision**: 68k MacTCP (Mac SE) crashes when test_fast T121
sends a burst of 12 UDP datagrams in a tight loop. PPC MacTCP
(P6200) handles the same workload without issue. The crash is
NOT caused by concurrent UDPWrite+UDPRead — MacTCP docs confirm
this is safe (Device Manager I/O queue serializes all operations).

**Rationale**: The T121 bidirectional test_fast has both sides
sending AND receiving UDP simultaneously. The send loop fires
SEND_HZ/TEST_SECS = 12 UDPWrite calls per second-tick via
PBControlSync on the message_udp stream. Log cuts off at
"Sending+receiving at 60Hz for 5s..." with zero messages sent.

MacTCP Programmer's Guide (1989, line 1541): "Asynchronous
notification will be used with the UDPRead command only; all
other commands are completed in a finite amount of time and can
be called synchronously." This confirms concurrent sync UDPWrite
and async UDPRead on the same stream is the intended usage
pattern — not the crash cause.

MacTCP does document `insufficientResources` ("too many datagrams
outstanding in transmit queue") as a possible UDPWrite error
(line 1369). A burst of 12 writes may overflow the internal
transmit queue on a memory-constrained 68k Mac (FreeMem=2.47MB,
~600KB free after PT_Init). However, this should return an error
code, not crash. Possible 68k MacTCP driver bug.

Evidence:
- Pre-T121 (unidirectional, Mac receive-only): 59/60 PASS
- Post-T121 (bidirectional, 12-message burst): crash on Mac SE
- Same T121 code: PASS on P6200 (PPC MacTCP)
- V1 reference: never sent UDP on Mac (discovery only)
- Book research: no documented 68k-specific UDP limitation

**Alternatives considered**:
- Cancel UDPRead before UDPWrite: Not necessary per MacTCP docs,
  and would lose incoming data during the cancel/repost window.
- Separate send/receive streams: Adds complexity for no documented
  benefit. Both operations are safe on the same stream.
- Throttle: limit 68k Mac to 1-2 UDP sends per poll cycle. Most
  likely fix — avoids the burst that triggers the crash without
  changing the architectural pattern.

**Source**: Hardware testing on Mac SE, 2026-03-06. Log:
downloads/macse/PT_test_fast_3. Book: MacTCP Programmers Guide
1989 lines 700 (I/O queue), 1369 (insufficientResources), 1426
(concurrent UDPWrite), 1541 (async notification for UDPRead only).

## R41. Test App Quality — Chat Exit, Reliable Strictness, Lifecycle Phase 3

**Decision**: Three test app quality issues found during code review:
(1) test_chat sender waits up to 45s in phase 2 with no silence
detection — should mirror the receiver's 3s silence pattern.
(2) test_reliable allows `g_moves_received >= TOTAL_TURNS - 1`,
tolerating 1 lost message. TCP is reliable — any loss is an SDK
bug that the test should catch, not tolerate. Stricten to
`== TOTAL_TURNS`. (3) test_lifecycle Phase 3 calls PT_Connect
on a just-disconnected peer in on_disconnected (line 122-124).
On OT, the endpoint is still in async unbind/rebind and the
connect silently fails. The on_discovered callback (line 43-50)
handles Phase 3 correctly as a fallback. Remove the eager
PT_Connect from on_disconnected and rely on on_discovered for
consistent behavior across all platforms.

**Rationale**: (1) The T122 bidirectional redesign added phase 2
state but didn't add sender-side silence detection. The receiver
detects 3s silence to trigger phase 2; the sender should use the
same pattern to detect phase 2 completion. (2) The `-1` tolerance
was likely defensive but undermines the test's purpose — proving
reliable delivery. With a 3s grace period after completion, TCP
messages will arrive. (3) The eager PT_Connect is an optimization
that works on POSIX/MacTCP but not OT. Removing it makes Phase 3
behavior predictable on all platforms without functional loss —
on_discovered fires within 2-3s when the next discovery broadcast
arrives.

**Alternatives considered**: (1) Shorter fixed timeout — fragile.
(2) Keep reliable tolerance — hides real bugs. (3) Add OT-specific
delay before PT_Connect — platform-specific code in test apps adds
complexity for no benefit when on_discovered already works.

**Source**: Code review of all 4 test apps during Phase 25, 2026-03-07.
Observed test_chat summary confusion during Mac SE test runs.
test_lifecycle Phase 3 delay observed on P6400 (OT) during hardware
verification.

## R42. MacTCP Send-Side Chunking Limit

**Decision**: On MacTCP with small tcp_send buffers (e.g., Mac SE
tcp_send=1024), PT_Send fails for messages requiring multi-chunk
sends. Messages of 2000 and 4000 bytes return PT_ERR_SEND_FAILED
because MacTCP's async model only supports one tcp_send call per
poll cycle (R19). The second chunk send fails immediately because
the first is still pending. Only messages fitting in a single
chunk (≤tcp_send_size) succeed.

**Rationale**: R21 documents the receive-side buffer limit
(reassembly buffer bounds incoming chunk frames). This is the
send-side corollary: the sender's tcp_send_size determines the
maximum message that can be sent in a single poll cycle. On POSIX
(blocking sends) and OT (synchronous OTSnd), multi-chunk sends
work because each chunk completes before the next begins. On
MacTCP, the async PBWrite returns immediately, and a second
PBWrite on the same stream while the first is pending fails.
Mac SE test_chat confirmed: sends of 10, 100, 500, 1000 bytes
succeed; 2000 and 4000 bytes fail. test_chat PASS criteria is
unaffected (checks received count), but T132 summary fix should
report attempted vs succeeded sends.

**Alternatives considered**: Multi-poll-cycle chunked sends
(queue chunks and send one per PT_Poll call) — violates "keep
the send path simple" design philosophy and adds send queue
complexity. Increasing Mac tcp_send_size — limited by available
heap memory. Accept and document — chosen approach.

**Source**: Mac SE hardware test logs (test_chat, 2026-03-07).
PT_test_chat log from macse folder on Performa 6400 FTP.

## R43. Discovery Does Not Re-Fire on_discovered for Disconnected Peers

**Decision**: pt_discovery_process_packet (pt_discovery.c:75-82)
returns early for known peers without firing on_discovered. When
a peer disconnects and remains "known" (state=DISCONNECTED,
in_use=1), subsequent discovery broadcasts update last_seen but
never re-trigger the callback. This means test_lifecycle Phase 3
(T134) relies on on_discovered to reconnect, but the callback
never fires because the peer is already known. The peer must
go through the full lost→rediscovered cycle (10s timeout + next
broadcast = 12s minimum) before on_discovered fires again.

Fix: in pt_discovery_process_packet, when a known peer is in
PT_PEER_DISCONNECTED state and a discovery broadcast arrives,
re-fire on_discovered. This lets applications reconnect
immediately (within 2s of next broadcast) instead of waiting
for the 10-second discovery timeout to expire the peer first.
The change is ~5 lines in pt_discovery.c and affects all
platforms equally.

**Rationale**: P6400 (OT) showed ~30s delay in test_lifecycle
Phase 3. Initially suspected OT endpoint recycling (R29), but
the sync unbind/bind completes in milliseconds. The real delay
is the discovery cycle: the remote peer keeps broadcasting, so
last_seen refreshes continuously, the peer never times out, and
on_discovered never re-fires. Mac SE Phase 3 worked faster (~4s)
likely due to timing coincidence in broadcast cycles. The fix
makes reconnection responsive on all platforms.

**Alternatives considered**: (1) Have test_lifecycle poll
PT_GetPeerState and call PT_Connect directly when peer is
DISCONNECTED — puts connection logic in the test instead of
using the callback pattern, inconsistent with other tests.
(2) Reduce PT_DISCOVERY_TIMEOUT — affects all peers, not just
disconnected ones. (3) Fire on_discovered for all known peers
on every broadcast — too noisy, callbacks would fire every 2s.
The targeted fix (only DISCONNECTED peers) is the right scope.

**Source**: Analysis of pt_discovery.c, test_lifecycle Phase 3
behavior on P6400 (OT) and Mac SE (MacTCP), 2026-03-07. OT
endpoint recycling (R29) ruled out as cause — sync unbind/bind
is fast. V1 code (~/peertalk/src/opentransport/) used
OTCloseProvider (no endpoint reuse) so this issue didn't exist.

## R44. OTSnd Partial Send in Async Non-Blocking Mode

**Decision**: OTSnd in async non-blocking mode can return fewer
bytes than requested per Networking With Open Transport (page 495):
"it is also possible that only part of the data is actually
accepted by the transport provider. In this case, the OTSnd
function returns a value that is less than the value of the nbytes
parameter." The current code in pt_ot.c ot_tcp_send() treats any
positive return as success, which could cause framing corruption
if only part of a TCP frame header is sent. Fix: loop until all
bytes are sent or an error occurs, matching the POSIX send pattern.

**Rationale**: Networking With Open Transport explicitly documents
this as expected behavior for async non-blocking endpoints. While
the SDK's frame sizes (max ~8KB) rarely trigger partial sends in
practice, the framing protocol requires complete frames — a partial
header write corrupts the stream for all subsequent messages. The
POSIX backend already handles this correctly (pt_posix.c rejects
partial sends).

**Alternatives considered**: (1) Return PT_ERR_SEND_FAILED on
partial send — safe but loses data unnecessarily when the provider
would accept the rest. (2) Switch OT TCP sends to synchronous like
MacTCP (R42) — would work but OT's async model is faster and
blocking OT sends would hurt Bomberman-pattern apps. (3) Loop to
send remaining bytes — matches the book's recommendation and is the
standard pattern.

**Source**: Networking With Open Transport, page 495. Code review
of pt_ot.c against reference book, 2026-03-07.

## R45. OT Orderly Release Protocol (OTSndOrderlyDisconnect)

**Decision**: When T_ORDREL fires, the book prescribes calling
OTRcvOrderlyDisconnect followed by OTSndOrderlyDisconnect to
complete the four-way TCP close handshake. The current code calls
OTRcvOrderlyDisconnect but then uses OTSndDisconnect (abortive)
instead. Per Networking With Open Transport (page 115): "When
the passive peer is finished sending any additional data, it calls
the OTSndOrderlyDisconnect function to complete its part of the
disconnection." Fix: add OTSndOrderlyDisconnect after
OTRcvOrderlyDisconnect before the abortive disconnect path.

**Rationale**: The abortive approach works — the connection tears
down either way — but the remote peer may see T_DISCONNECT
instead of a clean orderly release completion. For the SDK's use
case this is cosmetic, but following the documented protocol
prevents edge cases where the remote endpoint's state machine
gets confused. One-line fix.

**Alternatives considered**: (1) Keep abortive only — works but
doesn't follow the book. (2) Full orderly close with data drain
— over-engineered for this SDK.

**Source**: Networking With Open Transport, page 115 and sample
code page 46. Code review 2026-03-07.

## R46. 68k Open Transport Import Libraries (Performa 630)

**Decision**: The Retro68 m68k toolchain includes OT import
libraries with different names from the PPC toolchain:

| Purpose | PPC library | 68k library |
|---------|-------------|-------------|
| App API | OpenTransportAppPPC | OpenTransportApp |
| Core | OpenTransportLib | OpenTransport |
| Internet | OpenTptInternetLib | OpenTptInet |

The CMakeLists.txt must detect the toolchain (Retro68 vs
RetroPPC) and link the correct library names when PT_PLATFORM=OT.
The SDK C code in pt_ot.c is already platform-agnostic C89 and
should compile for both 68k and PPC without changes. The #undef
workaround for InContext macros (lines 24-26) needs verification
against the 68k OT headers.

This enables a 5th build target: build-68k-ot/ for the
Performa 630 (68040, System 7.6.1, OT). Build command:
cmake .. -DCMAKE_TOOLCHAIN_FILE=...m68k.../retro68.toolchain.cmake
  -DPT_PLATFORM=OT -DCLOG_DIR=... -DCLOG_LIB_DIR=.../build-m68k

**Rationale**: The Performa 630 (68LC040) runs System 7.5.3 with
MacTCP or System 7.6.1 with OT. Supporting both configurations
validates the SDK on a 4th Classic Mac and proves the OT backend
works on 68k. Constitution Principle III (Honest About Platform
Limits) requires testing on real hardware.

**Alternatives considered**: (1) Skip 68k OT — leaves a supported
platform combination untested. (2) Runtime detect 68k vs PPC in
OT code — unnecessary, the compile-time platform selection already
handles this.

**Source**: ls ~/Retro68-build/toolchain/m68k-apple-macos/lib/
showing libOpenTransportApp.a, libOpenTransport.a, libOpenTptInet.a.
2026-03-07.

## R47. Name-Based Test Role Assignment Broken on Hardware

**Decision**: Remove name-based role assignment (`name[0] <= 'M'`) from
test_lifecycle and test_chat. Replace with auto-connect-on-discovery —
both sides call PT_Connect when they discover a peer, and the first TCP
handshake to complete wins. No roles, no configuration.

**Rationale**: Classic Mac apps executed via LaunchAPPL cannot receive
command-line arguments, so the default name "Unnamed" (U > M = RESPONDER)
applies to both sides. Both peers wait for the other to connect, resulting
in a 60-second solo timeout with zero connections. Even when the POSIX
side is manually given `--name Linux`, phase 3 (role swap) fails because
timing-dependent re-discovery doesn't reliably trigger the Mac's
PT_Connect in the right window. The test_lifecycle PASS criteria (both
initiated AND accepted >= 1) cannot be met on hardware.

**Alternatives considered**: (1) IP-based role (lower IP initiates) —
deterministic but still a hidden mechanism, and doesn't solve the "both
sides must initiate" requirement without phase 3 complexity. (2) Platform-
based default names (POSIX="Linux", Mac="Unnamed") — works but perpetuates
the name-as-role antipattern. (3) Auto-connect with no roles — simplest,
most realistic (mirrors real app usage), and eliminates simultaneous-
connect risk because POSIX is always faster than Mac on LAN.

**Source**: Hardware testing on Performa 6400 (PPC/OT) with POSIX peer,
2026-03-07. Both peers defaulted to RESPONDER; test solo-timed-out with
0 connections. Second attempt with `--name Linux` got through phases 1-2
but stalled in phase 3.

## R48. 68k PT_Send Stack Buffer Crash on Mac SE

**Decision**: PT_Send allocates a 1403-byte stack buffer
(`buf[PT_UDP_MTU_SAFE + PT_UDP_HEADER_SIZE]` = 1400 + 3) for UDP
fast message framing (pt_messaging.c:118). On the Mac SE (68000/8MHz,
System 6.0.7), this buffer plus the deep call chain
(main → PT_Poll-context → on_connected → PT_Send → udp_send →
PBControlSync) exceeds the 68k application stack limit and crashes
immediately on the first UDP send — before any message reaches the wire.

The crash manifests as a hang/freeze at "Sending+receiving at 60Hz
for 5s..." with zero messages sent. The T128 throttle (R40) is
irrelevant because the crash occurs on the very first send, not from
burst volume. PPC MacTCP (P6200) and POSIX are unaffected because
they have larger default stacks (32KB+ vs ~8KB on 68k).

**Fix**: Move the 1403-byte buffer from PT_Send's stack to
`PT_Context_Internal` as `udp_send_buf[PT_UDP_MTU_SAFE + PT_UDP_HEADER_SIZE]`.
This is safe because PT_Send is never called reentrantly (poll-driven,
single-threaded). The buffer is part of the context struct which is
allocated at init time from the contiguous memory block — no new
malloc, consistent with Principle V.

**Alternatives considered**:
- Increase 68k stack size via SIZE resource: Classic Mac stack is
  set by the SIZE resource (or system default ~8KB). Increasing it
  reduces heap available for PT_Init's memory pool. The SDK should
  not require callers to adjust stack size.
- Reduce PT_UDP_MTU_SAFE: Would limit maximum fast message payload
  across all platforms, penalizing POSIX and PPC for a 68k constraint.
- Use peer's udp_buf for framing: The udp_buf is only 512 bytes,
  too small for max-size fast messages.

**Source**: Hardware testing on Mac SE, 2026-03-07. test_fast crash
at first PT_Send call with 68k MacTCP. Log stops at line 12
"Sending+receiving at 60Hz for 5s..." with g_sent=0. Same binary
works on PPC MacTCP (P6200).

## R49. test_fast Simultaneous-Connect Race on P6400 (OT)

**Decision**: test_fast's on_discovered callback calls PT_Connect
unconditionally, without checking g_connected or peer state. On P6400
(PPC/OT), both peers discover each other simultaneously and both call
PT_Connect, creating dual TCP connections. The second on_connected
overwrites g_peer and the duplicate connection disconnects with ERROR.
FAIL: 12 sent, 1 received. Passed on retry (timing-dependent).

MacTCP machines (P6200, Mac SE) never trigger this because MacTCP's
async TCP handshake is slower than POSIX's blocking connect — POSIX
always wins the race. OT's async non-blocking connect is fast enough
to complete before the POSIX-initiated connection is fully established.

R47 stated "eliminates simultaneous-connect risk because POSIX is
always faster than Mac on LAN" — this is true for MacTCP but not OT.

This is a test app bug, not an SDK bug. The SDK correctly accepts
both incoming connections. test_lifecycle and test_chat already use
test_should_connect() (T150) plus g_connected guards to prevent
duplicate connect attempts. test_fast was not updated in T149.

**Fix**: Add `if (g_connected) return;` guard in test_fast's
on_discovered callback, matching test_lifecycle and test_chat.

**Alternatives considered**:
- SDK-level duplicate connection rejection (detect same IP): Would
  add complexity to pt_core.c for a scenario that only matters when
  both sides auto-connect. Real apps (Bomberman, Chess, Chat) would
  have user-initiated connections, not auto-connect-on-discovery.
- IP-based tie-breaking (lower IP initiates): Deterministic but adds
  hidden logic. Auto-connect with a guard is simpler.

**Source**: Hardware testing on Performa 6400 (PPC/OT), 2026-03-07.
POSIX log shows dual [CONNECTED] events at timestamps 3487 and 3535,
followed by ERROR disconnect at 3632. P6400 log shows PASS (12/12)
because it used the first connection. Retry passed clean (60/60).

## R50. Post-v1.12.0 Architecture Review — Seam Refinements + What Was Declined

**Context**: A deep-module review (Ousterhout, *A Philosophy of Software
Design*) was run over core + all three backends after v1.12.0, using the
deletion test as the decision heuristic: if removing a module concentrates
complexity it was load-bearing; if it merely relocates, the module was
shallow. Six candidates surfaced. Three were implemented (they passed the
test cleanly); three were investigated at code level and **declined** —
recorded here so they are not re-proposed.

**Implemented (v1.12.x, "PR1")**:

- **A — deleted the dead `poll` vtable slot.** Every backend set
  `.poll = NULL` and drives the seam via `next_event()`, so `PT_Poll`'s
  `else { poll(ctx); }` fallback was unreachable and the slot supported a
  backend that does not exist. Removing it (field + branch + three `NULL`
  placeholders) makes the complexity vanish, not relocate. Platform
  interface narrowed 11→10 ops.
- **B — extracted `pt_check_peer_timeouts()`.** The per-peer
  connect/keepalive/inactivity sweep was inlined in `PT_Poll`, unreachable
  by the mock backend — unlike its already-extracted siblings
  `pt_discovery_check_timeouts` and
  `pt_messaging_check_reassembly_timeouts`. Extraction added
  mutation-verified unit coverage for three transitions (incl. the
  BomberTalk-critical keepalive) that previously only real hardware caught.
- **E — made the mock `tcp_send` counting + configurably-failing.** Added
  unit tests for send-failure propagation and chunked-send abort. test_seam
  92→112 checks.

**Declined after code-level investigation**:

- **C — converting inbound-accept and UDP reception into
  `PT_EVT_ACCEPT` / `PT_EVT_UDP` events.** The premise (that the seam is
  "partial" because backends call `pt_handle_incoming_connection`,
  `pt_discovery_receive`, `pt_messaging_process_udp_data` directly rather
  than via events) does not survive inspection. Those three helpers are
  **core** functions and are **already unit-tested directly** in
  `test_seam.c` (tiebreak, no-room, UDP-message, discovery-parse suites).
  The per-backend code around them is irreducibly platform-specific I/O
  (recvfrom / UDPBfrReturn / OTRcvUData; `accept` / passive-open), not
  duplicated logic. Converting to events would *widen* `PT_Event` (data
  pointer + len + port + source_ip), add a stateful UDP drain cursor to
  every backend, couple the received-buffer lifetime to core, and require a
  net-new "reject a platform peer" vtable op for the accept cleanup path
  (POSIX `close`, MacTCP `abort_stream`, OT disconnect are all
  platform-specific). That relocates and *grows* complexity to centralize a
  trivial port→function dispatch that is already effectively centralized —
  a reverse deletion-test result, and against Constitution IX (Keep It
  Small) and II (SDK Handles the Protocol; the split is already invisible
  to apps). Deferred UDP features (filtering, rate-limiting) that might
  justify a single dispatch point are speculative (Constitution I, IV) —
  YAGNI.

- **D — retiring the `udp_listen` vtable slot as a shallow module.** It
  *looks* shallow (POSIX and OT implementations are no-ops), but it is the
  named "arm/re-arm UDP reception on discovery (re)start" seam step,
  symmetric with `tcp_listen`, and does real correctly-timed work on
  MacTCP: `PT_StartDiscovery`'s re-entry path recreates the UDP streams via
  `cleanup_streams` (`read_pending = 0`, no read armed), and `udp_listen`
  is what re-arms them afterward. The POSIX/OT bodies are honest "nothing to
  arm on this platform" (the socket/endpoint is already listening from
  init), not dead code. Deleting the slot would scatter MacTCP's arming
  into `init` + `cleanup_streams` and break the `tcp_listen`/`udp_listen`
  symmetry — relocation, not reduction. Kept.

- **F — a dynamic event-handler registry**
  (`register_event_handler(type, callback)`) so future event types could be
  added without editing the vtable. Rejected as speculative generality
  against Constitution IV (Simple Defaults, No Knobs) and IX (Keep It
  Small). The static `PT_PlatformOps` vtable is the correct shape for a
  fixed-scope, zero-allocation, C89 SDK whose event set is bounded by
  "Three Apps Are the Spec" (I). If a genuinely new event category ever
  arrives, widening the `PT_EventType` enum is a good, compile-checked,
  reviewable diff — not a design flaw to pre-empt with runtime indirection.

**Standing decision**: the platform seam stays a **static vtable**;
non-lifecycle I/O (accept, UDP) stays as direct calls from each backend
into shared, already-tested core functions rather than being routed through
`PT_Event`. Only the per-peer TCP lifecycle (CONNECTED / DATA / CLOSED) is
event-driven, because that is where core genuinely owns cross-backend state
transitions.

**Source**: Architecture review, 2026-07-05. Report archived at
`architecture-review-2026-07-05.html`. All findings verified against the
working tree (line numbers checked); A/B/E landed with test_seam at 112
checks (all mutation-verified) and cppcheck clean, building on POSIX, 68k
MacTCP, and PPC Open Transport.
