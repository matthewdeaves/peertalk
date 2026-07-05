# peertalk Development Guidelines

## Constitution (Binding)

These 11 principles govern ALL implementation decisions. Full text: `.specify/memory/constitution.md`

1. **Three Apps Are the Spec** — Every feature MUST serve Bomberman, Chess, or Chat. If none need it, don't build it.
2. **SDK Handles the Protocol** — Framing, chunking, transport selection, discovery, liveness are invisible to the app.
3. **Honest About Platform Limits** — Measure on real hardware, document honestly. Never assume.
4. **Simple Defaults, No Knobs** — One TCP + one UDP per peer. No config structs. Add a setter only if an app needs tuning.
5. **Pre-Allocate Everything** — Zero malloc after PT_Init. All buffers allocated at init.
6. **Adapt at Init, Not Runtime** — FreeMem() at startup sizes buffers. No runtime adaptation or capability negotiation. Fixed-interval protocol timers (discovery, keepalive) are not adaptation.
7. **Logging Is Separate** — clog is an external dependency, never exposed in peertalk.h.
8. **Test Apps and Demo Apps Prove the SDK** — Four test apps exercise all three app patterns. Demo apps (csend-pt) prove the SDK with real applications.
9. **Keep It Small** — Target under 15,000 lines total across all platforms.
10. **C89 for Portability** — All SDK code MUST be C89/C90. Test apps (POSIX only) may use C11.
11. **Use Standard Tools, Don't Reinvent** — Dev/test tooling reuses standard programs (socat, jq, cmake). If a tool is missing, ask the user to install it — don't hand-roll a bespoke replacement. Tooling only; SDK stays dependency-light (VII, IX).

## Before Every Change

- [ ] Does this serve Bomberman, Chess, or Chat? (I) — if no, don't build it
- [ ] Am I adding config knobs or options? (IV) — if yes, stop
- [ ] Does this allocate after init? (V) — if yes, redesign
- [ ] Is this C89-clean in SDK code? (X) — if not, fix it

## Project Structure

```
include/peertalk.h          # Single public header (C89, 29 functions)
src/core/                   # Platform-independent core
src/platform/posix/         # BSD sockets + select()
src/platform/mactcp/        # MacTCP async parameter blocks (68k)
src/platform/opentransport/ # OT endpoints + notifiers (PPC)
tests/                      # Four test apps (C11 on POSIX)
specs/001-peertalk-sdk/     # Spec, plan, tasks, contracts, research
```

## Build Commands

clog dependency: `$CLOG_DIR` (defaults to `~/clog`) — built from source via `add_subdirectory` (no pre-built library needed).

```bash
# POSIX (build/)
mkdir -p build && cd build && cmake .. -DCLOG_DIR=$CLOG_DIR && make

# 68k MacTCP (build-68k/) — for Mac SE
mkdir -p build-68k && cd build-68k && \
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/m68k-apple-macos/cmake/retro68.toolchain.cmake \
  -DPT_PLATFORM=MACTCP -DCLOG_DIR=$CLOG_DIR && make

# PPC Open Transport (build-ppc-ot/) — for Performa 6400
mkdir -p build-ppc-ot && cd build-ppc-ot && \
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DPT_PLATFORM=OT -DCLOG_DIR=$CLOG_DIR && make

# 68k Open Transport (build-68k-ot/) — for Performa 630
mkdir -p build-68k-ot && cd build-68k-ot && \
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/m68k-apple-macos/cmake/retro68.toolchain.cmake \
  -DPT_PLATFORM=OT -DCLOG_DIR=$CLOG_DIR && make

# PPC MacTCP (build-ppc-mactcp/) — for Performa 6200
mkdir -p build-ppc-mactcp && cd build-ppc-mactcp && \
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DPT_PLATFORM=MACTCP -DCLOG_DIR=$CLOG_DIR && make

# Carbon (build-carbon/) — carbonised OT backend, RetroConsole GUI app.
# Runtime target is Classic Mac OS 8.6-9 (has OT + CarbonLib), NOT OS X:
# Open Transport is absent from every OS X version (CarbonLib is present but
# there's no OT CFM lib), so a Carbon-OT app dies with cfragNoLibraryErr on
# OS X — verified on the G3 (10.3.9) and G5 (10.5). On OS X PPC use the native
# fat POSIX build above (BSD sockets). No 8.6-9 machine is in the fleet, so
# this target currently builds-clean but has no local runtime.
mkdir -p build-carbon && cd build-carbon && \
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retrocarbon.toolchain.cmake \
  -DPT_PLATFORM=OT -DCLOG_DIR=$CLOG_DIR && make

# Fat Mac OS X (PPC + Intel, 10.4-10.7) -- NOT CMake. Reuses pt_posix.c on
# a vintage Intel Lion host (Xcode 3.2.6 / 10.4u SDK). Driven over ssh:
#   rsync repo + clog to the host, then:
CLOG_DIR=~/clog tools/build-macosx-fat.sh   # run on the OS X build host
# See tools/build-macosx-fat.md for the full workflow + known warnings.
```

## Architecture Notes

### Core-Logic Unit Tests (`tests/test_seam.c`)

The home for white-box unit tests of the platform-independent core. The
event-driven seam moved connection lifecycle, TCP framing/reassembly,
discovery v2 parsing, peer ranking, and timeout sweeps out of the backends
into core, so all of it is testable with a mock backend + synthetic events
— no sockets, no hardware. A PASS covers all three backends. Build with the
POSIX build (`cd build && make test_seam && ./test_seam`, 112 checks).

Conventions when adding tests here: include `pt_internal.h` for white-box
access; drive core entry points directly (`pt_messaging_process_tcp_data`,
`pt_discovery_receive`, `pt_handle_incoming_connection`, the `PT_Poll` drain
loop, etc.); set `ctx->current_time` by hand to test timeouts; and make each
test **fail on broken/pre-seam logic** (verify by mutation) so it catches
regressions rather than merely exercising code. Wire new tests into `main()`.

### TCP Keepalive

PeerTalk sends automatic keepalive frames (type `PT_MSG_TYPE_KEEPALIVE` = 254) every `PT_KEEPALIVE_INTERVAL` (20s) to prevent TCP inactivity timeout. This is critical for apps like BomberTalk where position updates go via UDP (`PT_FAST`) and TCP can starve during normal gameplay if no game events fire for 60s.

- Zero-payload 4-byte TCP frame: `[0x00 0x00 0xFE 0x00]`
- `last_tcp_send` per peer tracks when TCP data was last sent; keepalive fires when idle
- Receiver silently consumes keepalive (no callback registered, `TCPRcv` updates `last_tcp_activity`)
- Type 254 is reserved — `PT_RegisterMessage` rejects it (like type 255/GOODBYE)

### Disconnect Semantics (QUIT vs ERROR)

`on_disconnected`'s reason reflects the *transport* event, and `PT_QUIT` requires a buffered **goodbye frame** (`PT_MSG_TYPE_GOODBYE` = 255) arriving before the close:

- **`PT_Disconnect` / `PT_DisconnectAll`** (mid-run) send a goodbye first → the partner buffers it → **`PT_QUIT`**.
- **`PT_Shutdown`** deliberately sends **no** goodbye (MacTCP's send is synchronous with a 60s ULP timeout — a goodbye to a dead peer would freeze the machine) and closes gracelessly on every backend (OT `OTSndDisconnect`/MacTCP `TCPAbort` → RST; POSIX `close()` → FIN). A peer still connected at our shutdown therefore sees **`PT_DISCONNECT_ERROR`**, *uniformly across all three platforms* — this is by design, not an OT wart.
- The **clean-quit signal at shutdown is the UDP leave broadcast** (`PT_DISCOVERY_FLAG_LEAVE`, v1.11.0), which fires the partner's **`on_peer_lost`**. On the partner, handling our RST marks its slot for us DISCONNECTED but leaves `in_use` set, so its leave handler still finds us regardless of TCP/UDP ordering. Apps that need "did the peer leave cleanly?" should watch `on_peer_lost`, not the `on_disconnected` reason. Hardware-confirmed on the OT Mac (test_chat, Mac quits mid-exchange): partner logs `ERROR`, both sides exit cleanly, `Integrity: ok`. Full analysis: memory `ot-shutdown-goodbye-abortive-close`.

### Debug Broadcast Channel

PeerTalk provides a general-purpose UDP debug broadcast channel (no clog coupling — Principle VII). Apps wire clog or any other output into it if desired.

- `PT_EnableDebugBroadcast(ctx, 0)` — enables on default port `PT_DEBUG_PORT` (7356). Pass non-zero to override.
- `PT_DebugSend(ctx, msg, len)` — auto-prefixes `[name@ip] `, appends newline, broadcasts via UDP. No-op if disabled.
- `PT_DisableDebugBroadcast(ctx)` — tears down cleanly, idempotent.
- Uses a separate static buffer (not `udp_send_buf`) to avoid conflicts with `PT_Send` during callbacks.
- `PT_SetName()` rebuilds the debug prefix automatically if broadcast is active.
- Monitor with: `socat -u UDP-RECV:7356,reuseaddr -`

### Remote Test-Log Capture (no FTP)

The test harness (`tests/test_common.h`) mirrors every `TEST_LOG`/`TEST_WARN` line to the debug broadcast (each test app calls `test_remote_log_enable(ctx)` after `PT_Init`). So a full run log streams over UDP port 7356, tagged `[name@ip]` — the way to get logs off a machine with **no FTP** (the Mac SE), since a GUI APPL's stdout can't reach the LaunchAPPL out-file (see the Classic Mac test-apps gotcha).

Procedure (run the sink + a POSIX peer on the host, launch the Mac binary, then read the capture):

```bash
# 1. capture (truncates per run -> always a clean log)
timeout 55 socat -u UDP-RECV:7356,reuseaddr - > run.log &
# 2. a POSIX peer for the Mac to talk to (also broadcasts; interleaved, tagged by IP)
timeout 55 ./build/test_lifecycle --name POSIXHOST &
# 3. run on the Mac (MCP execute_binary), then:
grep '10.188.1.55' run.log   # SE's own lines; verdict line is "*** PASS ***"
```

Both sides' logs land in one file; filter by IP. Verified on the Mac SE (68k/MacTCP) and a PPC/OT Mac.

### Peer Ranking

`PT_GetPeerRank(ctx, peer)` returns the 0-based rank of a peer among all connected peers + self, sorted by IP address (lowest IP = rank 0). Pass `NULL` for `peer` to get the local machine's rank. Returns -1 on error. Uses internal `ip_addr` fields directly — no string parsing needed.

### MacTCP Poll Optimizations

- **Listener counter**: `g_mactcp.listener_count` tracks active listeners via increment/decrement instead of scanning all 32 TCP streams at end of every `mactcp_poll()` call. O(1) vs O(32).
- **Pooled param blocks**: `g_mactcp.recv_pb` (TCPRcv) and `g_mactcp.bfr_ret_pb` (UDPBfrReturn) are pre-initialized at `mactcp_init()` with immutable fields (`ioCRefNum`, `csCode`, `ioCompletion`). Hot-path poll only sets per-call fields (`tcpStream`, `rcvBuff`, `rcvBuffLen`). Eliminates 4+ `memset()` calls per poll cycle.
- **Cached discovery packet**: `ctx->discovery_pkt[]` is pre-built by `pt_discovery_build_packet()` at init and on `PT_SetName()`. Broadcast sends the cached buffer directly — no `strlen()`, `memcpy()`, or stack allocation per broadcast.

## Code Style

- C89/C90: no `//` comments, no mixed declarations, no VLAs, no stdint.h in public header
- Zero malloc after PT_Init — all memory pre-allocated in single block
- ISR/ASR safety: set volatile flags only, process in main loop (see `.claude/rules/isr-safety.md`)
- Poll-based I/O on all platforms — no threads, no completion routines

## Known Platform Gotchas

**pt_memcpy_isr unused warning**: `pt_memcpy_isr()` in `pt_internal.h` is intentionally unused. It exists as an ISR-safe memcpy for future interrupt handlers that need to copy data (standard memcpy may call Toolbox on Classic Mac). All current backends use "set flag, process later" so no data copying happens at interrupt time. Do not remove — see `.claude/rules/isr-safety.md`.

**C89 + variadic macros**: clog uses variadic macros (C99). Do NOT use `-pedantic` — it rejects them. Use `-Wall -Wextra` only.

**POSIX C89 code**: `vsnprintf` requires `#define _POSIX_C_SOURCE 200112L` before includes when compiling with `-std=c89`.

**Classic Mac test apps** (R11, R17, R18): Retro68/LaunchAPPL console apps have no Toolbox init. Use `Delay()` for sleep (no Toolbox needed). Use `TickCount()/60` for timing (safe at main loop time, NOT at interrupt time). No stdio on Classic Mac — use clog with `clog_set_file("PT_Log")`. Do NOT call WaitNextEvent without Toolbox init (bus error in `_PortToMap`). Do NOT do Toolbox init before Retro68 console init (kills printf window). **MaxApplZone()/MoreMasters()** MUST be called before ANY Memory Manager or File Manager call — in test_init_toolbox() before clog_set_file, AND in PT_Init() before NewPtrClear. **No malloc after PT_Init** on Classic Mac — test apps must use static buffers. **Use CLOG_INFO** for test progress. **stdout does NOT reach the LaunchAPPL out-file for our apps** (verified 2026-07-05): the out-file is populated only by MPW *tools* (ToolLauncher's `'dosc'` + `> "out"` redirect); GUI APPLs launched via `LaunchApplication` forward nothing, and RetroConsole's `_consolewrite` writes only to an on-screen window. So `execute_binary` returns "(no stdout — check PT_Log)" for our test apps. To get a full run log off a machine WITH NO FTP (the Mac SE), use the **UDP debug-broadcast log capture** — see the Remote Test-Log Capture section below.

**OT linker** (R12, R46): PPC builds link `OpenTransportAppPPC` + `OpenTransportLib` + `OpenTptInternetLib`. 68k OT builds link `OpenTransportApp` + `OpenTransport` + `OpenTptInet` + `ot_slm_stubs` (provides SLM dispatch symbols missing from Retro68 import libs). OT headers `#define` non-InContext names as InContext macros — add `#undef OTOpenEndpoint`, `#undef InitOpenTransport`, `#undef CloseOpenTransport` after OT includes.

**PPC toolchain**: File is `retroppc.toolchain.cmake`, NOT `retro68.toolchain.cmake`. `CMAKE_SYSTEM_NAME` is `RetroPPC` (not `Retro68`).

## Spec Artifacts

All design docs live in `specs/001-peertalk-sdk/`:
- `spec.md` — requirements and user stories
- `tasks.md` — 148 tasks across 26 phases (147 complete)
- `contracts/peertalk-api.md` — 25-function public API contract
- `research.md` — platform research decisions (R1-R50)

<!-- MANUAL ADDITIONS START -->
<!-- MANUAL ADDITIONS END -->

## Active Technologies
- C89/C90 (SDK), C11 (POSIX test apps)
- clog (logging), MacTCP (68k), Open Transport (PPC), BSD sockets (POSIX)
- PeerTalk SDK, test_common.h framework

## Recent Changes
- v1.13.0: Automatic full-mesh topology. New public API `PT_EnableAutoMesh(ctx, enable)` (31-function API, was 30 after PT_ShouldInitiate). Additive — existing apps and the wire protocol are unchanged.
  - **What it does**: when enabled, PeerTalk keeps a TCP connection open to every discovered peer, dialing only the pairs this node is the designated initiator for (`PT_ShouldInitiate` — lower IP dials, higher listens) and re-dialing after any drop. Each pair is dialed from exactly one side, so the simultaneous-connect race cannot arise by construction (the tiebreaker stays only as a safety net); the mesh self-heals. Opt-in and composable: an app wanting a star/host topology leaves it off and calls `PT_Connect`.
  - **Implementation**: `pt_mesh_dial_sweep()` runs each `PT_Poll`, gated on a 2 s retry timer (`PT_MESH_RETRY_INTERVAL`) plus each peer's `connect_start`, so an in-flight dial is never repeated and an instant-fail peer is not re-dialed every poll. Extracted like the sibling sweeps for direct core-logic testing. Context gains `auto_mesh` + `mesh_dial_timer` (zero-init, off by default).
  - **Tests**: `test_seam` 112→**120 checks** — auto-mesh sweep: dial-direction (only lower-IP peers dialed), retry-interval throttle, in-flight/connected skip, self-heal re-dial after a drop. Added a mock `tcp_connect` to the seam harness.
  - **Motivation**: consolidates mesh formation that used to live in the BomberTalk lobby (rank stagger + full-mesh detection + retry + manual `PT_Connect`). Fixes a cross-era regression where, with only the initiator dialing, a pair whose designated dialer wasn't actively dialing never connected. Hardware-validated: a 4-way OT + MacTCP + G5 (PPC, big-endian) + Intel mini (i386, little-endian) game ran clean over the auto-formed mesh.
- v1.12.1: Architecture-review follow-up (post-v1.12.0 deep-module pass). Core/test-only refinements to the event seam — no public API or behaviour change; identical wire protocol.
  - **Dead code removed**: every backend drove the seam via `next_event()` with `.poll = NULL`, so `PT_Poll`'s legacy `poll()` fallback was unreachable and the vtable's `poll` slot supported a backend that never existed. Deleted the field, the branch, and the three `NULL` placeholders — platform interface narrowed 11→10 ops.
  - **Testability**: extracted `pt_check_peer_timeouts()` (connect-timeout / keepalive / TCP-inactivity) out of `PT_Poll`, so the mock backend can drive it directly — matching its already-extracted sibling sweeps. Made the mock `tcp_send` counting + configurably-failing to cover send-failure propagation and chunked-send abort. `test_seam` 92→**112 checks**, every new one mutation-verified.
  - **Investigated and declined** (recorded in `research.md` R50 so they are not re-proposed): routing inbound-accept and UDP through `PT_EVT_ACCEPT`/`PT_EVT_UDP` events (the three core helpers are already centralized + unit-tested; conversion would widen `PT_Event` and add backend drain cursors — relocation, not reduction); retiring the `udp_listen` vtable slot (it is the load-bearing arm/re-arm-UDP step on MacTCP discovery restart, symmetric with `tcp_listen`); and a dynamic event-handler registry (speculative generality against Constitution IV/IX). **Standing decision**: the platform seam stays a static vtable; non-lifecycle I/O calls shared core functions directly.
  - **Hardware-validated** (vtable layout shifted, so re-run on the fleet): `test_lifecycle` PASS on Mac SE (68k MacTCP), OT Mac (PPC OT), .213 (PPC MacTCP), Intel mini (i386 10.7), iMac G5 (ppc 10.5.8), G4 Quicksilver (ppc 10.4.11). cppcheck clean; builds clean on POSIX, 68k MacTCP, PPC OT.
- v1.12.0: Major release — event-driven platform seam, Mac OS X support (native POSIX + Carbon target), expanded testing, static analysis, and full cross-era hardware validation.
  - **Event-driven platform seam**: backends emit `PT_Event`s (CONNECTED/DATA/CLOSED); core applies every lifecycle transition in one place (`pt_complete_connect`/`pt_drain_disconnect`/the `PT_Poll` drain loop) instead of each backend inlining it. Makes core logic unit-testable without sockets. Hardware-validated on all three classic backends.
  - **Mac OS X 10.3+**: fat PPC+Intel universal build reusing `pt_posix.c` (`tools/build-macosx-fat.sh`) for 10.4–10.7; a PPC-only 10.3.9 build covers 10.3. Verified `test_lifecycle` PASS on Intel mini (10.7.5), iMac G5 (10.5.8), G4 Quicksilver (10.4.11), G3 (10.3.9). Makes BomberTalk-for-macOS feasible, interoperating with the classic Macs over the shared wire protocol. On-screen runs via `tools/osx-screen-run.sh` (Terminal window on the Mac's display).
  - **Carbon build target** (`build-carbon/`, retrocarbon toolchain): carbonises the OT backend under `TARGET_API_MAC_CARBON` (OTCARBONAPPLICATION, Carbon toolbox/heap guards). Builds clean; runtime is **Classic Mac OS 8.6–9** (OT + CarbonLib), NOT OS X — OT has no CFM library on any OS X version (verified `cfragNoLibraryErr` on the G3/G5).
  - **Testing**: core-logic unit tests expanded 41→92 checks (`tests/test_seam.c`: framing/reassembly, discovery v2 parse, peer ranking, timeouts; each mutation-verified). cppcheck static-analysis sweep (`tools/cppcheck.sh`) over all three backends — no dead code, no defects.
  - **Fixes**: simultaneous-connect tiebreaker kept the wrong connection (hardware race caught on the Mac SE); POSIX double-close on the incoming no-room path; documented `PT_Shutdown` QUIT-vs-ERROR disconnect semantics.
  - **Tooling**: UDP debug-broadcast test-log capture (no FTP needed) — spawned Constitution Principle XI (use standard tools, don't reinvent); local Docker two-peer test track.
- v1.11.2: Downgraded drain_endpoint_events() safety-limit log from CLOG_WARN to CLOG_DEBUG in OT backend. The limit fires during normal game-over teardown (stale events accumulate on endpoint reset) and is not actionable.
- v1.11.0: Discovery protocol v2 — adds flags byte to discovery header (6 bytes, was 5). New `PT_DISCOVERY_FLAG_LEAVE` (0x01) enables immediate peer removal on quit via UDP broadcast, instead of waiting for 15s timeout. `PT_Shutdown()` sends leave automatically. Fixed OT listener deadlock: stale T_DISCONNECT on listener endpoint blocked all future T_LISTEN delivery (XTI state machine). Three-layer defense: notifier tracking, accept-loop drain+retry, cleanup drain. Confirmed on real Performa 6400 across 4 consecutive games. Removed duplicate shutdown log from MacTCP.
- v1.9.0: Added `PT_GetPeerRank()` for deterministic IP-sort peer ranking (Bomberman player IDs, Chess turn order). Added debug broadcast channel (`PT_EnableDebugBroadcast`, `PT_DebugSend`, `PT_DisableDebugBroadcast`) — general-purpose UDP debug pipe with no clog coupling (Principle VII). Apps wire clog into it via a 3-line bridge. Port 7356 by default. 29 functions total, ~4,700 LOC.
- v1.7.0 (011-disconnect-poll-hardening): Added PT_DisconnectAll(ctx) for clean lifecycle transitions. Hardened OT poll (OTRcv error logging, slot->owner re-read after ordrel drain) and MacTCP poll (stream state guard before TCPRcv, error logging on terminated drain).
- TCP keepalive + poll optimizations: Automatic TCP keepalive frames (type 254, 20s interval) prevent inactivity timeout when apps use PT_FAST for frequent messages. MacTCP poll optimized: listener counter replaces 32-stream re-scan, pooled param blocks eliminate per-call memset, discovery packet cached at init.
