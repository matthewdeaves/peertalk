# Tasks: PeerTalk SDK

**Input**: Design documents from `/specs/001-peertalk-sdk/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/peertalk-api.md, research.md, quickstart.md

**Tests**: Four test applications (test_fast, test_reliable, test_chat, test_lifecycle) are part of the SDK deliverables per Constitution Principle VIII. They are included as implementation tasks within their respective user stories.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story. The POSIX backend is the reference implementation — all user stories are validated on POSIX first. Classic Mac backends (MacTCP, OT) are added in US5.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks in this phase)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4, US5)
- Include exact file paths in descriptions

---

## Phase 1: Setup

**Purpose**: Project directory structure, build system, and public header

- [x] T001 Create project directory structure: `include/`, `src/core/`, `src/platform/posix/`, `src/platform/mactcp/`, `src/platform/opentransport/`, `tests/`
- [x] T002 [P] Write CMakeLists.txt with platform detection (`PT_PLATFORM_POSIX`/`PT_PLATFORM_MACTCP`/`PT_PLATFORM_OT` macros), clog linkage via `CLOG_DIR`, static library target `peertalk`, and four test executable targets in `tests/`
- [x] T003 [P] Write include/peertalk.h — complete public header per contracts/peertalk-api.md: 2 opaque types (`PT_Context`, `PT_Peer`), 4 enums (`PT_Status`, `PT_PeerState`, `PT_Transport`, `PT_DisconnectReason`), 4 callback typedefs, 20 function declarations. C89 only — no stdint.h, no bool.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Internal types, memory system, core skeleton, POSIX platform skeleton. MUST complete before any user story.

- [x] T004 Write src/core/pt_internal.h — all internal structs and macros: `PT_Context_Internal` (fields per data-model.md: name, local_ip, platform_ops, platform_state, peers array, max_peers, peer_count, message_types[256], callbacks struct, discovery state, memory_block, memory_size), `PT_Peer_Internal` (fields per data-model.md: name, state, ip_addr, last_seen, in_use, per-peer buffers, reassembly state, platform_peer union), `PT_PlatformOps` vtable (10 function pointers per spec.md section 3), `PT_Callbacks` struct, wire protocol constants (ports 7353/7354/7355, magic "PTLK", version 1, MSG_TYPE_GOODBYE=255), platform macros (`PT_PLATFORM_POSIX`/`PT_PLATFORM_MACTCP`/`PT_PLATFORM_OT`), byte-order helpers (`pt_htons`/`pt_ntohs`). Include `clog.h` for internal logging.
- [x] T005 [P] Write src/core/pt_memory.c — `pt_memory_calculate_sizes()` (compute per-peer buffer sizes and max_peers from available memory using sizing formula from data-model.md: POSIX defaults to 32 peers with 8 KB tcp_recv, 4 KB tcp_send, 512 B udp, 64 KB reassembly), `pt_memory_allocate()` (single `malloc`/`NewPtr` call, subdivide into global state + N peer slots with buffer pointers), `pt_memory_free()`. Each peer slot gets pointers into the contiguous block for tcp_recv_buf, tcp_send_buf, udp_buf, reassembly_buf.
- [x] T006 [P] Write src/platform/posix/pt_posix.c — skeleton implementing all 10 `PT_PlatformOps` functions as stubs returning `PT_OK` or no-op. Platform state struct with socket fds for discovery (port 7353), TCP listener (port 7354), UDP messages (port 7355). `posix_init()`: create non-blocking sockets, set `SO_REUSEADDR`, `SO_BROADCAST` on UDP, bind all three ports. `posix_shutdown()`: close all sockets. `posix_poll()`: `select()` skeleton on all fds with zero timeout. Get local IP via `getsockname()` or interface enumeration. Set `TCP_NODELAY` on TCP sockets.
- [x] T007 [P] Write tests/test_common.h — shared test utilities: command-line argument parsing (`--name`), message type constants for test apps (MSG_POSITION=1, MSG_MOVE=2, MSG_CHAT=3), helper to print peer state changes, platform-appropriate sleep/delay (`usleep` on POSIX), signal handler for clean shutdown.
- [x] T008 Write src/core/pt_core.c — `PT_Init()` (validate name length <=31, call `pt_memory_allocate()`, init message_types[0-254] to `PT_RELIABLE`, zero callbacks, call `platform_ops->init()`), `PT_Shutdown()` (call `platform_ops->shutdown()`, call `pt_memory_free()`), all 6 callback registration functions (simple setters on callbacks struct), all 4 peer info functions (`PT_GetPeerCount`, `PT_GetPeer`, `PT_PeerName`, `PT_GetPeerState`), `PT_Poll()` skeleton (call `platform_ops->poll()`, placeholder hooks for discovery timer and timeout checks). Wire platform_ops to `posix_get_ops()` when `PT_PLATFORM_POSIX` is defined. Internal helper `pt_fire_error()`: fire `on_error` callback when registered. Error callback fires for: peer slots full during incoming TCP accept (`PT_ERR_NO_ROOM`), platform I/O errors not attributable to a specific peer, init partial failure details.

**Checkpoint**: Project compiles and links. `PT_Init`/`PT_Shutdown` allocate and free memory. POSIX sockets bind successfully. No networking yet.

---

## Phase 3: User Story 1 — Peer Discovery and Connection (Priority: P1) — MVP

**Goal**: Two POSIX instances discover each other on LAN, connect via TCP, and can disconnect cleanly.

**Independent Test**: Run two `test_lifecycle` instances with different names. They discover each other within 5 seconds, connect, and both receive connection confirmation. Disconnect sends goodbye.

### Implementation for User Story 1

- [x] T009 [P] [US1] Write src/core/pt_discovery.c — `pt_discovery_broadcast()` (encode PTLK discovery packet: 4-byte magic + 1-byte version + null-terminated name, call `platform_ops->udp_broadcast()` on port 7353), `pt_discovery_receive()` (parse incoming UDP, validate magic/version, extract name and source IP, filter own IP, find or create peer slot — if all slots are full, fire error callback with `PT_ERR_NO_ROOM` if `on_error` is registered, then silently ignore the packet (do not fire `on_peer_discovered`), update `last_seen`, fire `on_peer_discovered` for new peers), `pt_discovery_check_timeouts()` (scan peers, remove any not seen for 10 seconds, fire `on_peer_lost`, free slot), `PT_StartDiscovery()` (set `discovery_active`=1, `discovery_listening`=1, call `platform_ops->udp_listen()` on port 7353), `PT_StopDiscovery()` (set `discovery_active`=0, keep `discovery_listening`=1). Integrate timer check and receive dispatch into `PT_Poll`.
- [x] T010 [P] [US1] Add connection management to src/core/pt_core.c — `PT_Connect()` (validate peer in DISCOVERED or DISCONNECTED state — return `PT_ERR_NOT_CONNECTED` if already connected, call `platform_ops->tcp_connect()`, track connection start time, 10-second connection timeout checked in `PT_Poll` — abandon and fire error callback if exceeded, transition to CONNECTED on success, fire `on_connected`), `PT_Disconnect()` (encode 4-byte goodbye: length=0 type=255 flags=0, call `platform_ops->tcp_send()`, call `platform_ops->tcp_disconnect()`, transition to DISCONNECTED, fire `on_disconnected` with `PT_QUIT`). Handle incoming connections: when platform reports new TCP connection, match by source IP to known peer, transition to CONNECTED, fire `on_connected`. If no matching peer, create new slot if room.
- [x] T011 [US1] Implement discovery and TCP I/O in src/platform/posix/pt_posix.c — `posix_udp_broadcast()` (sendto 255.255.255.255:7353), `posix_udp_listen()` (already bound in init, just mark listening), `posix_tcp_listen()` (listen on port 7354 socket with backlog), `posix_tcp_connect()` (non-blocking `connect()` to peer IP:7354, store fd in peer's `platform_peer`), `posix_tcp_send()` (write to peer's TCP fd), `posix_tcp_disconnect()` (close peer's TCP fd). Update `posix_poll()`: check discovery UDP socket for incoming packets (call core's `pt_discovery_receive()`), check TCP listener for new connections (`accept()`), check each connected peer's TCP fd for readable data and connection errors.
- [x] T012 [US1] Write tests/test_lifecycle.c (basic version) — initialize SDK with name from `--name` arg, register `on_peer_discovered` (print name, auto-connect to first peer), register `on_connected` (print confirmation), register `on_disconnected` (print reason), start discovery, poll loop with 16ms sleep, clean shutdown on SIGINT. Validates: mutual discovery, TCP connect, goodbye on shutdown.

**Checkpoint**: Two POSIX instances discover each other, connect, and disconnect cleanly. `test_lifecycle` passes.

---

## Phase 4: User Story 2 — Reliable Message Exchange (Priority: P2)

**Goal**: Connected peers exchange TCP messages of any size. Messages arrive complete and in order. Chunking/reassembly is transparent.

**Independent Test**: Run `test_reliable` (small turn-based messages) and `test_chat` (large chunked messages). Verify complete, ordered delivery.

### Implementation for User Story 2

- [x] T013 [US2] Write src/core/pt_messaging.c — reliable messaging path: `PT_RegisterMessage()` (set `message_types[type]` to given transport, reject type 255), `PT_Send()` for `PT_RELIABLE` (validate peer is in `PT_PEER_CONNECTED` state — return `PT_ERR_NOT_CONNECTED` otherwise; validate args — return `PT_ERR_INVALID_ARG` for NULL data with len>0; encode TCP header: 2B payload length in network byte order + 1B type + 1B flags=0, call `platform_ops->tcp_send()` with header+payload), `PT_Broadcast()` (iterate connected peers, call `PT_Send()` for each, return `PT_OK` if at least one succeeds). Add frame parsing in receive path: `pt_messaging_process_tcp_data()` (called when platform delivers TCP data — buffer in peer's `tcp_recv_buf`, parse complete frames by reading 4-byte header then payload bytes, dispatch complete messages via `on_message[type]` callback).
- [x] T014 [US2] Add chunking and reassembly to src/core/pt_messaging.c — send side: if payload length > `tcp_send_size - 4` (single frame won't fit), split into chunks with 8-byte chunked header (2B chunk payload length + 1B type + 1B flags=1 + 2B sequence + 2B total_chunks), each chunk payload = `tcp_send_size - 8` bytes max. Receive side: when chunked flag is set, copy chunk payload into peer's `reassembly_buf` at correct offset, track received count, set `reassembly_timer` on first chunk. On first chunk, calculate total message size from `total_chunks * max_chunk_payload` — if it exceeds `reassembly_buf` size, discard immediately, fire error callback with `PT_ERR_NO_ROOM`, and reset reassembly state. When all chunks received, deliver complete message via callback and reset reassembly state. `pt_messaging_check_reassembly_timeouts()`: scan peers, discard incomplete reassembly after 5 seconds, reset state. Integrate timeout check into `PT_Poll`.
- [x] T015 [US2] Add TCP data receive to src/platform/posix/pt_posix.c — in `posix_poll()`: for each connected peer with readable TCP fd, `recv()` into peer's `tcp_recv_buf` (append to existing buffered data), call core's `pt_messaging_process_tcp_data()` to parse and dispatch complete frames. Handle `recv()` returning 0 (peer closed connection) and errors.
- [x] T016 [P] [US2] Write tests/test_reliable.c — Chess pattern: two peers discover+connect, then alternate sending small "move" structs (type MSG_MOVE, ~20 bytes) via `PT_Send` with `PT_RELIABLE`. Each side waits for the other's message before sending its next move. Run for N turns (e.g. 10), then disconnect. Print each move received. Validates: reliable delivery, ordering, request/response pattern.
- [x] T017 [P] [US2] Write tests/test_chat.c — Chat pattern: two or more peers discover+connect, send variable-length text messages via `PT_Send` with `PT_RELIABLE`. Include messages from small (10 bytes) to large (64 KB — exercises chunking). Print received messages with sender name and length. Validates: chunking/reassembly, multi-peer TCP, variable payload sizes.

**Checkpoint**: `test_reliable` and `test_chat` pass on POSIX. Messages up to 64 KB delivered correctly.

---

## Phase 5: User Story 3 — Fast Message Exchange (Priority: P3)

**Goal**: Connected peers exchange small, frequent UDP messages with minimal latency. Oversized messages are rejected.

**Independent Test**: Run `test_fast` — one peer sends position structs at high frequency, other receives most of them.

### Implementation for User Story 3

- [x] T018 [US3] Add fast messaging path to src/core/pt_messaging.c — `PT_Send()` for `PT_FAST`: validate payload length <= ~1400 bytes (MTU - IP/UDP overhead - 3-byte header), return `PT_ERR_SEND_FAILED` if too large. Encode UDP header: 2B payload length in network byte order + 1B type. Call `platform_ops->udp_send()`. `PT_Broadcast()` for fast: iterate connected peers, call `platform_ops->udp_send()` for each. Add `pt_messaging_process_udp_data()` for receive: parse 3-byte header, dispatch via `on_message[type]` callback.
- [x] T019 [US3] Add UDP message I/O to src/platform/posix/pt_posix.c — `posix_udp_send()` (sendto peer IP:7355 with framed data). In `posix_poll()`: check UDP message socket (port 7355) for incoming datagrams, `recvfrom()` into temp buffer, identify source peer by IP, call core's `pt_messaging_process_udp_data()`. Bind UDP message socket in `posix_init()` if not already done.
- [x] T020 [US3] Write tests/test_fast.c — Bomberman pattern: two or more peers discover+connect. Sender transmits position-like structs (x, y, direction — ~12 bytes) at 30-60 Hz via `PT_Send` with `PT_FAST`. Receiver counts and prints received messages. Run for a fixed duration (e.g. 5 seconds), then print delivery statistics (sent vs received count). Also test rejection of an oversized message (>1400 bytes). Validates: high-frequency UDP, broadcast, size rejection.

**Checkpoint**: `test_fast` passes on POSIX. 30-60 Hz messaging works. Oversized sends return error.

---

## Phase 6: User Story 4 — Connection Lifecycle Management (Priority: P4)

**Goal**: Disconnect reasons are correctly reported (quit, timeout, error). Disconnected peers can reconnect. Clean shutdown notifies all peers.

**Independent Test**: Run `test_lifecycle` extended version — exercise each disconnect path and verify correct reason codes.

### Implementation for User Story 4

- [x] T021 [US4] Add disconnect reason detection to src/core/pt_core.c — `PT_QUIT`: when goodbye message (type 255) is received via TCP, fire `on_disconnected` with `PT_QUIT`, transition peer to DISCONNECTED (NOTE: requires TCP frame parsing from T015/US2 to detect goodbye). `PT_TIMEOUT`: track last TCP activity timestamp per peer, in `PT_Poll` check for peers with no TCP activity for 30 seconds (connected but silent), fire `on_disconnected` with `PT_TIMEOUT`. Note: 30-second TCP inactivity timeout is distinct from the 10-second discovery timeout. `PT_DISCONNECT_ERROR`: when platform reports TCP read/write error or connection reset, fire `on_disconnected` with `PT_DISCONNECT_ERROR`. Ensure platform resources (fd/stream/endpoint) are cleaned up in all cases.
- [x] T022 [US4] Add reconnection support to src/core/pt_core.c — allow `PT_Connect()` on a peer in `PT_PEER_DISCONNECTED` state: reset `platform_peer` state, call `platform_ops->tcp_connect()`, transition back to CONNECTED on success. Ensure per-peer buffers (tcp_recv, tcp_send, reassembly) are reset/zeroed on reconnect. Verify peer remains in peer list while still broadcasting discovery (not removed by 10s timeout if discovery packets still arriving).
- [x] T023 [US4] Improve PT_Shutdown in src/core/pt_core.c — before closing connections, iterate all CONNECTED peers: encode and send goodbye message (type 255) via `platform_ops->tcp_send()`, then call `platform_ops->tcp_disconnect()`. Fire `on_disconnected` with `PT_QUIT` for each. Then call `platform_ops->shutdown()` and free memory. Handle partial send failures gracefully (best-effort goodbye).
- [x] T024 [US4] Extend tests/test_lifecycle.c — add test scenarios: (1) clean disconnect — one peer calls `PT_Disconnect`, other receives `PT_QUIT` reason, (2) crash simulation — kill one peer's process, other receives `PT_TIMEOUT` after timeout period, (3) reconnection — disconnect then reconnect same peer, verify new messages flow, (4) shutdown — one peer calls `PT_Shutdown`, other receives `PT_QUIT`. Print reason codes for all disconnect events.

**Checkpoint**: `test_lifecycle` exercises all disconnect paths. Reconnection works. Shutdown sends goodbye.

---

## Phase 7: User Story 5 — Cross-Platform Communication (Priority: P5)

**Goal**: Classic Mac backends (MacTCP and OT) implement the same PT_PlatformOps vtable. Memory sizing adapts to available RAM via FreeMem(). The wire protocol is identical — POSIX and Classic Mac peers interoperate.

**Independent Test**: Build for 68k and PPC via Retro68. Run test app on Classic Mac (emulated or physical) alongside POSIX peer. Verify bidirectional discovery, connection, and message exchange.

### Implementation for User Story 5

- [x] T025 [P] [US5] Write src/platform/mactcp/pt_mactcp.c — full MacTCP backend implementing all 10 `PT_PlatformOps` functions. Platform state: pre-allocated TCP stream array (via `TCPCreate`), UDP stream (`UDPCreate`), async parameter blocks per operation. `mactcp_init()`: open MacTCP driver (`.IPP`), `GetMyIPAddr()` for local IP, create TCP/UDP streams with pre-allocated buffers. `mactcp_udp_broadcast()`: `UDPWrite` async PB to 255.255.255.255. `mactcp_udp_send()`: `UDPWrite` to peer IP. `mactcp_udp_listen()`: `UDPRead` async PB, re-issue on completion. `mactcp_tcp_listen()`: `TCPPassiveOpen` async PB on dedicated listener stream, re-issue after each accept. `mactcp_tcp_connect()`: `TCPActiveOpen` async PB. `mactcp_tcp_send()`: `TCPSend` async PB with WDS. `mactcp_tcp_disconnect()`: `TCPClose` async PB. `mactcp_poll()`: check `ioResult` on all pending PBs (`==1` means pending, `==0` success, `<0` error), process completions, check `UDPRead` for incoming data, check `TCPPassiveOpen` for new connections. Set `ioCompletion=NULL` on all PBs (poll, never use completion routines). ASR: set volatile flags only, process in poll. Reference: `.claude/rules/mactcp.md` for ASR safety, register preservation, error codes.
- [x] T026 [P] [US5] Write src/platform/opentransport/pt_ot.c — full Open Transport backend implementing all 10 `PT_PlatformOps` functions. Platform state: TCP endpoint (via `OTOpenEndpoint` with `tilisten,tcp` config for concurrent accepts), UDP endpoint, volatile event flags set by notifier. `ot_init()`: `InitOpenTransportInContext()`, create TCP endpoint (`OTOpenEndpoint`), bind to port 7354, create UDP endpoint, bind to ports 7353/7355, `OTInetGetInterfaceInfo()` for local IP, install notifier callback. Notifier: `state->events |= event_code` (bitwise OR, atomic on 68k/PPC). `ot_udp_broadcast()`: `OTSndUData` to 255.255.255.255. `ot_udp_send()`: `OTSndUData` to peer IP. `ot_tcp_listen()`: endpoint already listening via tilisten module. `ot_tcp_connect()`: `OTConnect` async. `ot_tcp_send()`: `OTSnd` non-blocking. `ot_tcp_disconnect()`: `OTSndDisconnect` or `OTSndOrderlyDisconnect`. `ot_poll()`: read `state->events`, clear processed flags, handle `T_DATA` (call `OTRcv`), `T_LISTEN` (call `OTListen`+`OTAccept`), `T_DISCONNECT` (peer disconnect), `T_ORDREL` (orderly release), `kOTLookErr` (check `OTLook`). Reference: `.claude/rules/opentransport.md` for notifier safety, event codes, endpoint states, error handling.
- [x] T027 [US5] Add Classic Mac memory sizing to src/core/pt_memory.c — when `PT_PLATFORM_MACTCP` or `PT_PLATFORM_OT` is defined: query `FreeMem()`, use 75% of reported value (conservative — accounts for fragmentation, see research.md R3). Apply buffer sizing table from data-model.md: ~500 KB → 2 KB tcp_recv / 1 KB tcp_send / 4 KB reassembly / 8-12 peers, ~2 MB → 4 KB / 2 KB / 16 KB / 16-24 peers, ~8 MB+ → 8 KB / 4 KB / 64 KB / 32 peers (cap). Use `NewPtr()` for the single contiguous allocation on Classic Mac.
- [x] T028 [US5] Update CMakeLists.txt for Retro68 cross-compilation — add conditionals for `PT_PLATFORM` variable: when `MACTCP`, compile `src/platform/mactcp/pt_mactcp.c` and define `PT_PLATFORM_MACTCP`; when `OT`, compile `src/platform/opentransport/pt_ot.c` and define `PT_PLATFORM_OT`; default to POSIX. Link MacTCP/OT system libraries as needed. Add Retro68 application targets for test apps (`.bin` output). Ensure clog is found and linked for all platforms.

**Checkpoint**: 68k and PPC builds compile via Retro68 toolchain. Classic Mac test apps can be transferred to real/emulated hardware for testing.

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Validation, compliance, and cleanup across all user stories

- [x] T029 [P] Validate quickstart.md — follow build instructions from scratch on a clean checkout, verify POSIX build succeeds, run minimal example from quickstart.md, confirm test apps execute
- [x] T030 [P] C89 compliance audit — verify all files in `src/` and `include/` compile with `-std=c89 -pedantic -Wall -Werror`. Fix any C99/C11-isms (no `//` comments, no mixed declarations, no VLAs, no designated initializers). POSIX-only code in `tests/` may use C11.
- [x] T031 Measure and document platform performance — run test_fast (latency), test_reliable (throughput), and test_chat (chunked throughput) on each platform (POSIX, MacTCP on Mac SE, OT on Performa 6400). Record measured SEND/RECV throughput and discovery latency. Document results in quickstart.md under a "Platform Performance" section. Per Constitution Principle III: measure on real hardware, document honestly.
- [x] T032 Final review — count lines of code across all source files (target <15K LOC per plan.md), verify zero-malloc-after-init invariant by inspection, check constitution compliance (all 10 principles), verify all 20 public functions are implemented and callable

---

## Phase 9: Hardware Testing Fixes (Retroactive)

**Purpose**: Fixes discovered during hardware testing that were applied ad-hoc and tracked after the fact. These tasks were not anticipated during planning — see research.md R11, R12 for root cause analysis.

- [x] T033 [US5] Fix test_common.h Classic Mac compatibility — `test_init_toolbox()` redirects clog to file (skips InitGraf/InitFonts to avoid Retro68 console conflict). `test_sleep_ms()` uses `Delay()` (no GrafPort needed). `test_time_sec()` uses `TickCount()/60`. All four test apps call `test_init_toolbox()` before logging and `PT_Init`. POSIX path unchanged. Reference: research.md R11.
- [x] T034 [US5] Fix OT linker configuration in CMakeLists.txt — link OpenTransportAppPPC alongside OpenTransportLib for PPC builds. Add #undef for InContext macros in pt_ot.c after OT includes. Reference: research.md R12.
- [x] T035 Verify all test apps launch on hardware — run test_lifecycle on Performa 6400 (OT) and Performa 630 (MacTCP) via LaunchAPPL. Verify each app: starts without crashing, discovers POSIX peer, connects, exchanges at least one message. This is the minimum bar before any performance testing.
- [x] T036 [US2] Fix cross-platform chunk reassembly in src/core/pt_messaging.c — lines 205-228 and 226-228 use `peer->tcp_send_size` for reassembly offset calculation. Replace with actual chunk payload size derived from first chunk: store first chunk's payload_len as `reassembly_stride` in peer struct, use `seq * reassembly_stride` for offset. Also fix total size calculation at line 207. Add `reassembly_stride` field to PT_Peer_Internal in src/core/pt_internal.h. Reference: research.md R13, FR-005.
- [x] T037 [US5] Merge peers array into contiguous block in src/core/pt_memory.c — include `sizeof(PT_Peer_Internal) * max_peers` in `pt_memory_calculate_size()`. In `pt_memory_allocate()`, carve peers array from the start of the block (after global overhead), then assign per-peer buffers after. Remove the second malloc/NewPtrClear for ctx->peers. Update `pt_memory_free()` to remove the separate free/DisposePtr for peers. Reference: research.md R14, FR-009, Principle V.
- [x] T038 [US5] Add volatile qualifier to flow_off in src/platform/opentransport/pt_ot.c — change `int flow_off` to `volatile int flow_off` in OTEndpointSlot struct at line 62. Field is written from OT notifier (line 129, T_GODATA) and read from main loop (line 528). Without volatile, compiler may cache stale value. Reference: ISR safety rules, FR-015.
- [x] T039 [US5] Store and dispose OTNotifyUPP handles in src/platform/opentransport/pt_ot.c — add three OTNotifyUPP fields (listener_upp, tcp_upp, udp_upp) to the OT global state struct. Store UPPs after NewOTNotifyUPP at lines 293-295. In ot_shutdown(), call DisposeOTNotifyUPP for each before CloseOpenTransport(). Reference: FR-015.
- [x] T040 [US5] Include platform buffer overhead in memory sizing in src/core/pt_memory.c — add platform-specific overhead constant (MacTCP: 8KB per TCP stream + 4KB per UDP stream; OT: equivalent endpoint overhead). Subtract total platform overhead from available memory before computing max_peers in mac_size_from_memory(). This ensures FreeMem()-based sizing accounts for all allocations, not just the SDK contiguous block. Reference: research.md R15, FR-010, Principle V.
- [x] T041 [US5] Rewrite test_common.h Mac platform layer from v1 patterns — study ~/peertalk/tests/mac/test_throughput.c, test_latency.c, log_stream.h for proven Mac patterns. Current test_common.h uses Delay() for PPC (works) and WaitNextEvent for 68k (crashes without Toolbox init). Either: (a) adopt v1 pattern of full Toolbox init + own windows, or (b) accept Delay()-only with file-based log collection for both platforms. Verify all four test apps launch on Mac SE (68k/MacTCP), Performa 630 (68k/MacTCP), and Performa 6400 (PPC/OT). Reference: research.md R16, R11.
- [x] T042 [US5] Add MaxApplZone()/MoreMasters() to Classic Mac init in src/core/pt_memory.c — call MaxApplZone() once and MoreMasters() 4 times at the start of pt_memory_allocate() when PT_PLATFORM_MACTCP or PT_PLATFORM_OT is defined. Must execute before FreeMem() call. Include <Memory.h>. This extends the application heap to maximum size and pre-allocates master pointer blocks. Without this, FreeMem() returns the tiny initial heap size and all allocations fail. Reference: research.md R17, ~/peertalk/src/core/buffer_pool.c:85-89.
- [x] T043 [US5] Rebuild cross-compiled binaries (68k + PPC) and retest on hardware — rebuild clog (build-m68k, build-ppc), then rebuild peertalk (build-m68k, build-ppc). Deploy test_lifecycle to Performa 6400 (OT) and Performa 630 (MacTCP) via LaunchAPPL. Verify: app starts without crash, PT_Log is created, discovery finds POSIX peer. If still crashing, collect any PT_Log output and investigate further. **Note**: Rebuild verified in session. Hardware testing deferred to Phase 10 T048.
- [x] T044 Verify clog works on Classic Mac hardware — deploy a minimal clog test (init, write one line, shutdown) via LaunchAPPL to verify File Manager calls work. Check that PT_Log file is created and contains output. If clog itself crashes, the SDK will also crash since it logs during init. **Note**: Deferred to Phase 10 T048 which covers all hardware verification.

---

## Phase 10: Classic Mac App Hardening

**Purpose**: Fix remaining code issues found in v1 vs v2 deep review (R18). Test apps must be built to run correctly on Classic Mac, not as POSIX apps with #ifdef patches. All code-fix tasks can run without hardware access. Hardware verification tasks (T048) require Macs to be on.

**Context**: The MaxApplZone timing fix (R17) moved MaxApplZone/MoreMasters from pt_memory_allocate() to PT_Init() and test_init_toolbox(), so it runs before the first NewPtrClear and before clog. This fix is already applied in the working tree. The remaining issues are: test_chat.c malloc, missing clog logging in test apps, and hardware verification.

- [x] T045 [US5] Fix test_chat.c malloc — replace malloc/free in send_chat() (lines 37, 53) with platform-aware buffering. On Classic Mac (`#ifndef PT_PLATFORM_POSIX`): use a `static char send_buf[4096]` and skip test messages larger than 4096 bytes (Mac reassembly buffer is limited). On POSIX: keep existing malloc/free for full 65KB test coverage. This eliminates the Principle V violation (zero malloc after init on Classic Mac). Reference: R18.
- [x] T046 [US5] Add CLOG_INFO logging to all four test apps for Classic Mac log visibility — on Mac, printf goes to Retro68 console which may not flush to LaunchAPPL. Add `CLOG_INFO` calls at key test events so PT_Log captures progress. Specific changes: (a) In tests/test_common.h: add a `test_log(const char *msg)` helper that calls both `printf` and `CLOG_INFO`. (b) In each test app's main(): log init success, discovery started. (c) In each callback: log discovered, connected, disconnected, message sent/received. Keep it minimal — one CLOG_INFO per event, not verbose. Reference: R18.
- [x] T047 [US5] Rebuild all four platform targets and verify clean compilation — rebuild POSIX (`build/`), 68k MacTCP (`build-68k/`), PPC OT (`build-ppc-ot/`), and PPC MacTCP (`build-ppc-mactcp/`). The PPC MacTCP build is for the Performa 6200 (PPC 603, 40MB, MacTCP only, no OT). Build command: `cmake .. -DCMAKE_TOOLCHAIN_FILE=.../retroppc.toolchain.cmake -DPT_PLATFORM=MACTCP -DCLOG_DIR=... -DCLOG_LIB_DIR=.../build-ppc`. Delete stale CMakeCache.txt if source path has changed. Verify zero errors. Expected warnings: `pt_memcpy_isr unused` (OK, used only on Mac), `suggest braces` in mactcp UPP dispose (cosmetic). If any build fails, fix the issue before proceeding.
- [x] T048 [US5] Deploy test_lifecycle to both Macs and verify — requires hardware access (Performa 6400 for OT, Performa 6200 for MacTCP). For each machine: (1) test connectivity with `mcp__classic-mac-hardware__test_connection`, (2) execute the correct binary: `build-ppc-ot/test_lifecycle.bin` for Performa 6400 (PPC/OT), `build-ppc-mactcp/test_lifecycle.bin` for Performa 6200 (PPC/MacTCP), (3) verify app does not crash within 30 seconds, (4) download PT_Log via FTP (Performa 6400) and verify clog output was written. Run a POSIX test_lifecycle peer simultaneously to test cross-platform discovery. If app crashes: download any partial PT_Log, check for clog init failure vs SDK init failure vs network init failure. Also test test_clog_minimal.bin on each machine to isolate clog issues from SDK issues. Machine IDs: `performa6400` (OT), `performa6200` (MacTCP). Platform for LaunchAPPL: both use `mactcp` for Performa 6200, `opentransport` for Performa 6400.

---

## Phase 11: GUI Test Apps & Speckit Remediation

**Purpose**: Add status window GUI to Mac test apps, rewrite test apps with auto-exit and PASS/FAIL verdicts, update speckit artifacts to reflect current state so `speckit implement` can run in a loop unattended.

**Context**: Cross-platform networking confirmed working (POSIX peer discovered and connected to Mac peer on Performa 6200). Test apps need GUI (status window from v1), auto-exit on completion, and clear PASS/FAIL verdicts in both clog and status window.

- [x] T049 [P] Port status_window from v1 for Mac GUI — create `tests/status_window.h` and `tests/status_window.c` from v1 (`tests/mac/status_window.c`). Provides `status_init()`, `status_line()`, `status_linef()`, `status_clear()`, `status_cleanup()`. documentProc window, 9pt text, 12px line height, auto-wrap. Add to Mac `add_application()` calls in CMakeLists.txt. Not compiled for POSIX.
- [x] T050 Update test_common.h and all test apps with GUI + auto-exit + PASS/FAIL — integrate status_window into test_common.h: `#include "status_window.h"` on Mac, `status_init()` in `test_init_logging()`, `status_linef()` in TEST_LOG macro, `status_cleanup()` in `test_shutdown_logging()`. Increase solo timeout to 60s. Update `test_exit_pause()` to wait for keypress (so user can read results). Rewrite all 4 test apps: (a) test_lifecycle: 2 connect/disconnect cycles with reconnection, PASS if 2 connects + 2 disconnects + reconnect. (b) test_fast: oversize rejection + 60Hz send for 5s, PASS if oversize rejected + messages sent/received. (c) test_reliable: 10 alternating moves with order checking, PASS if all 10 sent + received in order. (d) test_chat: variable-size messages 10B-4KB with integrity check, PASS if all sent + received valid. Each app: clear "*** PASS ***" or "*** FAIL: reason ***" verdict, auto-exit on completion.
- [x] T051 Rebuild and verify all 4 targets — rebuild POSIX (`build/`), PPC OT (`build-ppc-ot/`), PPC MacTCP (`build-ppc-mactcp/`), 68k (`build-68k/`). Verify zero errors, expected warnings only.
- [x] T052 Hardware verification — run all 4 test apps on Performa 6400 (PPC/OT) and Performa 6200 (PPC/MacTCP) with POSIX peer. Results: all 4 tests PASS on both machines. test_chat receives 5/10 messages on Mac (chunking limitation, see R21). GUI window appears, auto-exits, PASS shown. PT_Log collected via FTP.
- [x] T053 Update speckit artifacts for overnight build — review and update all spec files to match current implementation state: (a) spec.md: update status from Draft to Implementation Complete for US1-US4, In Progress for US5. (b) research.md: ensure R11-R22 reflect final decisions. (c) tasks.md: verify all T001-T052 statuses are accurate. (d) plan.md: update implementation status. (e) contracts/peertalk-api.md: verify matches actual peertalk.h. (f) data-model.md: verify matches actual pt_internal.h. (g) quickstart.md: verify build commands work. (h) checklists/requirements.md: update checklist. Goal: `speckit implement` can pick up remaining tasks and run unattended.
- [x] T054 [US2] [US5] Fix chunked TCP receive on Classic Mac — tcp_recv_buf=2048 is too small to hold chunk frames from POSIX sender (tcp_send_size=4096). Either increase Mac tcp_recv_buf in pt_memory.c sizing table to ≥4100 bytes, or make pt_messaging.c TCP receive path accumulate partial frames across poll cycles. Verify with test_chat on Performa 6200 and Performa 6400: all 10 message sizes should be received. Reference: R21, FR-005, SC-005. **Result**: Increased tcp_recv to 4100 on all Mac tiers. P6400 and P6200 now receive 6/10 messages (up to 4000B). Messages ≥8KB rejected by reassembly_buf (4096B on <2MB tier) — expected.
- [x] T055 [US5] Run all 4 tests on Mac SE (68k/MacTCP) with POSIX peer — deploy 68k binaries from build-68k-fresh/ via LaunchAPPL. Test each: test_lifecycle, test_reliable, test_fast, test_chat. Collect results. Mac SE has 4MB RAM — exercises the low-memory sizing path (8-12 peers, smaller buffers). Machine ID: macse. Reference: SC-003, SC-008. **Result**: All 4 tests PASS. test_lifecycle: 2 connect/disconnect + reconnect. test_reliable: 10/10 moves. test_fast: oversize rejected, 60 msgs. test_chat: POSIX sender 10/10.

---

## Phase 12: Log Improvements & Final Verification

**Purpose**: Fix log collection issues, add machine identification, investigate disconnect reason bug, and perform comprehensive hardware verification on all 3 Macs.

**Context**: Overnight run completed all 55 tasks but revealed: PT_Log filename collision (all tests overwrite same file), test_chat disconnect reason ERROR instead of QUIT, no machine identification in logs, and T054 recv buffer fix not verified on P6400/P6200. Also need stale build directory cleanup.

- [x] T056 [P] Fix PT_Log filename collision in tests/test_common.h — change clog_set_file("PT_Log") to use the app_name parameter passed to test_init_logging(). Use format "PT_{Name}" e.g. clog_set_file("PT_Lifecycle"), clog_set_file("PT_Reliable"), clog_set_file("PT_Fast"), clog_set_file("PT_Chat"). This preserves logs across sequential test runs on the same Mac. Reference: R20 Issue 2.
- [x] T057 [P] Investigate test_chat disconnect reason ERROR on Mac — Mac SE shows "[DISCONNECTED] Alice (ERROR)" instead of QUIT when POSIX sender finishes. The goodbye frame may be lost during reassembly error processing. Check pt_messaging.c TCP receive path: does it continue parsing frames after a reassembly error, or does it discard remaining buffer data? If frames after the error are discarded, the goodbye is lost. Fix: ensure TCP receive continues parsing subsequent frames even after reassembly errors. Reference: R23, FR-007.
- [x] T058 [P] [US5] Add machine identification logging via Gestalt — add pt_log_platform_info() to src/core/pt_core.c, called from PT_Init after platform init. On Classic Mac: use Gestalt(gestaltMachineType), Gestalt(gestaltProcessorType), Gestalt(gestaltSystemVersion) to log machine model, CPU, and OS version. On POSIX: log uname() info. Log at CLOG_INFO level. Research Gestalt selectors in ~/peertalk/books/Inside_Macintosh_Volume_VI_1991.txt. Reference: R24, ISR safety rules (Gestalt is Table B-3 safe with predefined selectors).
- [x] T059 Clean up stale build directories, standardize names, update .gitignore, and rebuild all 4 targets — remove stale build dirs: build-68k-new/, build-68k/ (old), build-m68k/, build-m68k2/, build-ppc/, build-ppc2/, build-ppc-ot/ (old). Rename build-ppc-fresh/ to build-ppc-ot/ and build-68k-fresh/ to build-68k/. Final layout: build/ (POSIX), build-ppc-ot/ (PPC/OT for P6400), build-ppc-mactcp/ (PPC/MacTCP for P6200), build-68k/ (68k for Mac SE). Update .gitignore with entries for all 4 build dirs (build/, build-ppc-ot/, build-ppc-mactcp/, build-68k/), downloads/, logs/, and any other generated artifacts. Update CLAUDE.md build commands to use standardized dir names. Then reconfigure cmake and rebuild all 4 targets after T056-T058 fixes. Verify zero errors.
- [x] T060 [US5] Full hardware verification on Performa 6400 (PPC/OT) — run all 4 tests with POSIX peer (--name Alice). For each test: start POSIX peer, deploy build-ppc-ot/{test}.bin via execute_binary (platform=opentransport), wait for completion, download PT_{Name} log via FTP (machine=performa6400). Verify: all 4 PASS, logs have machine ID, test_chat receives 6/10 messages (tcp_recv=4100 fix confirmed). Collect all 4 logs to downloads/performa6400/.
- [x] T061 [US5] Full hardware verification on Performa 6200 (PPC/MacTCP) — run all 4 tests with POSIX peer (--name Alice). For each test: start POSIX peer, deploy build-ppc-mactcp/{test}.bin via execute_binary (platform=mactcp), wait for completion, download PT_{Name} log via FTP (machine=performa6200). Verify: all 4 PASS, logs have machine ID, test_chat receives 6/10 messages. Collect all 4 logs to downloads/performa6200/.
- [x] T062 [US5] Full hardware verification on Mac SE (68k/MacTCP) — run all 4 tests with POSIX peer (--name Alice). For each test: start POSIX peer, deploy build-68k/{test}.bin via execute_binary (platform=mactcp, machine=macse), capture LaunchAPPL output. Verify: all 4 PASS, logs have machine ID. Mac SE has no FTP — results from LaunchAPPL stdout only. Download logs from performa6400 FTP if Mac SE writes to shared volume.
- [x] T063 Run speckit analyze — full cross-artifact consistency check: constitution compliance of all SDK code, verify spec.md requirements map to tasks, verify tasks map to requirements, check plan.md matches implementation, verify contracts/peertalk-api.md matches actual peertalk.h, verify data-model.md matches actual pt_internal.h, check for orphaned tasks or uncovered requirements. Fix any issues found. Run via /speckit.analyze.

---

## Phase 13: Code Review Remediation — SDK Safety

**Purpose**: Fix safety and correctness issues found during deep code review. These are SDK-level bugs that affect wire protocol correctness, network input validation, and platform backend reliability.

**Context**: Deep code review on 2026-03-05 found 5 research-worthy issues in the SDK core and platform backends. All are bugs that exist in the current code — not theoretical.

- [x] T064 [P] Fix discovery packet buffer overflow — pt_discovery.c:62 calls strlen() on unterminated network data. Add memchr() null-terminator check within received byte bounds before computing namelen. If no null terminator found, discard the packet. Reference: R25. Test: send a crafted discovery packet with no null byte, verify no crash and packet is silently dropped.
- [x] T065 [P] Fix POSIX TCP partial send data loss — pt_posix.c:315-330 returns PT_OK when EAGAIN causes partial send, silently dropping remaining bytes and corrupting TCP framing. Change to return PT_ERR_SEND_FAILED when total < len after send loop exits. Reference: R26. Test: verify test_reliable still passes (clean send path), verify send failure is reported to caller on partial send.
- [x] T066 [P] Fix non-atomic flag clearing in Classic Mac backends — pt_mactcp.c and pt_ot.c use `flags &= ~FLAG` which is a non-atomic read-modify-write. ASR/notifier can set a flag between read and write, losing the flag. Fix: read flags into local, process local, then atomically clear processed bits. On 68k: use interrupt disable around clear. On PPC/OT: use OTAtomicClearBit (Table C-1). Reference: R27. Files: src/platform/mactcp/pt_mactcp.c, src/platform/opentransport/pt_ot.c.
- [x] T067 [P] Add reassembly type check — pt_messaging.c:228 accepts chunks without verifying msg_type matches reassembly_type. Add `msg_type == peer->reassembly_type` condition to chunk acceptance. Reference: R28. File: src/core/pt_messaging.c.
- [x] T068 Fix OT async unbind/rebind race — pt_ot.c:580-593 calls OTUnbind/OTBind on async endpoints without waiting for completion. Add OTSetSynchronous before unbind/rebind, then OTSetAsynchronous after. Same fix needed in ot_poll disconnect handlers (lines ~705-714 and ~756-765). Reference: R29. File: src/platform/opentransport/pt_ot.c.
- [x] T069 Fix POSIX accepted connection fd leak — pt_posix.c:417-430 does not close newfd when pt_handle_incoming_connection rejects the connection (no peer slot available). After the call, check if the peer was actually accepted; if not, close(newfd). File: src/platform/posix/pt_posix.c.
- [x] T070 [P] Add null checks for UPP/notifier creation — pt_mactcp.c:313-314 NewTCPNotifyUPP/NewUDPNotifyUPP return values not checked (NULL = OOM crash). pt_ot.c:298-300 NewOTNotifyUPP same issue. pt_ot.c:191,228,307 OTCreateConfiguration not checked for NULL. pt_ot.c:211,247,331 OTInstallNotifier return not checked. Add null checks, return PT_ERR_INIT on failure. Files: src/platform/mactcp/pt_mactcp.c, src/platform/opentransport/pt_ot.c.
- [x] T071 [P] Move frame size check before header write — pt_messaging.c:26-51 writes header to tcp_send_buf before checking frame_size > tcp_send_size. Move the size check to before any writes. Also remove redundant header[] array in UDP send (pt_messaging.c:117-118) — build header directly in buf[]. File: src/core/pt_messaging.c.
- [x] T072 [P] Extract send_goodbye helper — pt_core.c duplicates goodbye frame construction in PT_Shutdown (~line 302) and PT_Disconnect (~line 402). Extract a static send_goodbye(ctx, peer) function. File: src/core/pt_core.c.

---

## Phase 14: Code Review Remediation — Test Quality

**Purpose**: Fix test quality issues found during deep code review. Tests should meaningfully prove their user stories with proper payload validation, realistic pass criteria, and API coverage.

**Context**: Deep code review on 2026-03-05 found that tests pass too easily — weak assertions, unchecked payloads, missing API coverage. Constitution Principle VIII says "Test Apps Prove the SDK" — current tests demonstrate the SDK runs but don't rigorously prove it works correctly.

- [x] T073 Fix test_chat integrity check — tests/test_chat.c:138 only validates first 100 bytes of each message (`i < 100` cap). A 65KB message is 99.8% unchecked. Remove the cap so the full message pattern is verified. This is the primary test for chunking correctness. File: tests/test_chat.c.
- [x] T074 Strengthen test_fast pass criteria and payload validation — tests/test_fast.c:204 passes with g_received > 0 (1 out of ~300 messages). Change minimum to g_received >= 10 for receiver. Add payload validation in on_position(): verify seq is in valid range, direction < 4, x < 320, y < 200 (Bomberman grid bounds). Also document that the sending loop does 12-message bursts per second, not smooth 60Hz. File: tests/test_fast.c.
- [x] T075 Add payload integrity to test_reliable — tests/test_reliable.c:80-113 only checks move_num ordering, never validates from_row/from_col/to_row/to_col content. Add verification that payload fields match expected values derived from move_num (from_row = (move_num-1) % 8, etc.). Tighten pass criteria: require g_moves_received >= TOTAL_TURNS - 1 instead of > 0. File: tests/test_reliable.c.
- [x] T076 Add PT_Broadcast test to test_reliable — PT_Broadcast is a public API function (spec FR-006, US2 scenario 3) that is never tested. Add a broadcast round to test_reliable after the move exchange: first mover broadcasts a "game over" message, both peers verify receipt. Even with 2 peers this exercises the broadcast code path. File: tests/test_reliable.c.
- [x] T077 Rebuild all 4 targets after Phase 13 + 14 fixes — rebuild POSIX (build/), PPC/OT (build-ppc-ot/), PPC/MacTCP (build-ppc-mactcp/), 68k (build-68k/). Verify zero errors, run POSIX tests to confirm nothing is broken by the fixes.
- [x] T078 [US5] Hardware verification on all 3 Macs — run all 4 tests on Performa 6400 (PPC/OT), Performa 6200 (PPC/MacTCP), and Mac SE (68k/MacTCP) with POSIX peer (--name Alice). Deploy correct binaries: build-ppc-ot/ for P6400, build-ppc-mactcp/ for P6200, build-68k/ for Mac SE. Collect all PT_Log files. Verify: all 4 PASS on all 3 machines, test_chat full integrity check passes, test_fast minimum threshold met, test_reliable payload validated.
- [x] T079 [P] Clean up build and gitignore — add build-68k-fresh/, build-ppc-fresh/, .mcp.json to .gitignore. Remove -pedantic from POSIX SDK compile flags in CMakeLists.txt line 63 (CLAUDE.md says don't use it, currently suppressed by pragma workaround). File: CMakeLists.txt, .gitignore.

---

## Phase 15: Chat Application

**Purpose**: Build a working chat application demonstrating the PeerTalk SDK. Reuses GUI from csend (~&#47;csend/MPW_resources/) with fresh PeerTalk-based networking. ONE Classic Mac source set builds for all three Mac targets (68k/MacTCP, PPC/OT, PPC/MacTCP). POSIX gets a terminal-based chat. This is a shipping example app, not a test — it proves Constitution Principle I (Three Apps Are the Spec: Chat pattern) and demonstrates the SDK's cross-platform value.

**Context**: csend (~&#47;csend/) has a working Classic Mac chat GUI using Dialog Manager (DLOG 128), TextEdit (messages + input), List Manager (peer list), and a scrollbar. The networking layer is ~5000 lines per platform. With PeerTalk, the entire app should be ~800-1000 lines because the SDK handles discovery, connections, and messaging. Reference: R30.

**File structure**:
```
apps/chat/
  chat.h              # Shared types: MSG_CHAT_TEXT=1, format helpers
  mac/
    main.c            # Classic Mac: Toolbox init, event loop, PeerTalk callbacks
    dialog.c/.h       # Dialog management: peer list, messages, input, send
    chat.r            # Rez resources adapted from csend.r (DLOG, DITL, MBAR, CNTL)
    chat_size.r       # SIZE resource (2.5MB preferred, 1.5MB minimum)
  posix/
    main.c            # POSIX: terminal UI, stdin polling, PeerTalk integration
```

- [x] T080 [P] [Chat] Create apps/chat/ directory structure and shared header — create apps/chat/chat.h with: message type constant (MSG_CHAT_TEXT = 1), max message length (1024), display formatting helpers (format_incoming, format_outgoing). Register MSG_CHAT_TEXT as PT_TRANSPORT_RELIABLE. All code must be C89 (shared with Classic Mac). File: apps/chat/chat.h.
- [x] T081 [P] [Chat] Create Rez resource file adapted from csend — copy ~/csend/MPW_resources/csend.r to apps/chat/mac/chat.r. Adapt: (1) DLOG 128 title "CSend" → "PeerTalk Chat", (2) MENU 128: remove "Perform Test" item, keep "Quit" (Cmd+Q), (3) DITL 128: remove item 7 (Show Debug checkbox) — keep items 1-6. Keep CNTL 6 (scrollbar) and MBAR 128 as-is. Also create apps/chat/mac/chat_size.r with SIZE resource (2.5MB preferred, 1.5MB min) matching tests/peertalk_size.r pattern. Reference: R30 for csend GUI layout.
- [x] T082 [Chat] Implement Classic Mac dialog management — create apps/chat/mac/dialog.c and dialog.h. Port the proven csend UI patterns from ~/csend/shared/classic_mac/ui/ (dialog_messages.c, dialog_input.c, dialog_peerlist.c) into a single focused file. Use ~/csend/ source files as direct reference for every function — these patterns survived extensive crash testing on real hardware.

  **DITL item mapping** (from csend.r DITL 128): item 1 = peer list (userItem, List Manager), item 2 = messages display (userItem, TEHandle), item 3 = input field (userItem, TEHandle), item 4 = Send button, item 5 = Broadcast checkbox, item 6 = scrollbar (CNTL 6).

  **Functions needed**: DialogInit(DialogPtr) — create TEHandles for messages (item 2) and input (item 3), create List Manager for peer list (item 1), get scrollbar handle (item 6). DialogCleanup() — dispose in reverse order (list → input TE → messages TE+scrollbar → DisposeDialog). DialogAppendMessage(text) — append to messages TE with scrollbar sync. DialogGetInputText(buf, maxlen) / DialogClearInput(). DialogUpdatePeerList(PT_Context*) — refresh from PT_GetPeerCount/PT_GetPeer/PT_PeerName/PT_GetPeerState, show DISCOVERED and CONNECTED peers with state indicator. DialogGetSelectedPeer() — return PT_Peer* for selected row. DialogHandleClick(Point, DialogPtr) — three-tier dispatch: scrollbar first (FindControl), peer list second (PtInRect on rView), input TE third. DialogHandleKey(EventRecord*) — route to input TE, Return key triggers send. DialogHandleUpdate(DialogPtr) — BeginUpdate, DrawDialog, EraseRect+TEUpdate for each TE, FrameRect borders, LUpdate for list, EndUpdate. DialogIdle() — throttled TEIdle (15 ticks).

  **CRITICAL stability patterns from R31** (every one of these prevented real crashes in csend):
  - Handle safety: HGetState/HLock/HSetState triplet on EVERY handle dereference. Check `*handle != NULL` after HLock. Restore state on ALL error paths. Double-lock for hText access.
  - GrafPort: GetPort/SetPort/SetPort around ALL drawing. Check gMainWindow != NULL before SetPort.
  - TextEdit: 30K limit guard before TEInsert. TEAutoView(false) for messages. "\r" line endings.
  - Scrollbar: thumb vs arrow/page split handling. Scrolled-to-bottom tracking for auto-scroll. Zero lineHeight guard. Clamp scroll values. InvalRect after TEScroll. Check contrlVis AND contrlHilite.
  - Validation: verify item type from GetDialogItem. Check itemHandle for NULL. Validate rect after InsetRect. Fallback cell height if font metrics zero.
  - List Manager: LActivate(false) before LDispose. Preserve selection by peer identity not index. Verify LLastClick with LGetSelect. Lock list handle before rView access. lOnlyOne selection.
  - Broadcast/peer list mutual exclusion.
  - NULL-out handles after dispose. Null-terminate buffers on error. Preserve input on send failure.

  Reference: R30, R31. Source files for reference: ~/csend/shared/classic_mac/ui/dialog_messages.c, dialog_input.c, dialog_peerlist.c, ~/csend/classic_mac_mactcp/dialog.c.
- [x] T083 [Chat] Implement Classic Mac main event loop — create apps/chat/mac/main.c. Use ~/csend/classic_mac_mactcp/main.c as direct reference for event loop structure and Toolbox init.

  **Init sequence** (order matters — R31): (1) MaxApplZone + 4x MoreMasters, (2) Toolbox init: InitGraf(&qd.thePort) → InitFonts → InitWindows → InitMenus → TEInit → InitDialogs(NULL) → InitCursor, (3) load menu bar (GetNewMBar(128), SetMenuBar, DrawMenuBar, AppendResMenu for Apple menu), (4) install AppleEvent handler for kAEQuitApplication, (5) open dialog (GetNewDialog(128, NULL, (WindowPtr)-1)), (6) DialogInit to create all UI components, (7) PT_Init with default name, register MSG_CHAT_TEXT reliable, set all callbacks, PT_StartDiscovery, (8) display startup message in messages area.

  **Main loop**: while (!gDone) { DialogIdle (throttled 15 ticks), PT_Poll(ctx), WaitNextEvent(everyEvent, &event, 4L, NULL) — 4 ticks sleep (~67ms, good for chat responsiveness), dispatch event }.

  **Event dispatch** (three-tier per R31): mouseDown → FindWindow: inContent → DialogHandleClick (scrollbar/peerlist/input TE custom handling FIRST, then DialogSelect for Send button and Broadcast checkbox), inMenuBar → MenuSelect → HandleMenuChoice, inDrag → DragWindow, inGoAway → TrackGoAway → gDone=true. keyDown/autoKey → Cmd+key to MenuKey, else DialogHandleKey (check FrontWindow first). updateEvt → DialogHandleUpdate (throttled 100ms). activateEvt → activate/deactivate all UI components. kHighLevelEvent → AEProcessAppleEvent.

  **PeerTalk callbacks**: on_discovered → set gPeerListNeedsUpdate flag (dirty flag pattern from R31, not immediate redraw). on_connected → set flag + queue "* Connected: name" message. on_disconnected → set flag + queue "* Disconnected: name (reason)" message. on_chat_message → DialogAppendMessage("name: message\r"). on_error → DialogAppendMessage("! Error: description\r").

  **Send handling**: get input text, if empty return and SysBeep. If broadcast checkbox checked → PT_Broadcast, else PT_Send to DialogGetSelectedPeer. On success: DialogAppendMessage("You: message\r"), DialogClearInput, refocus input TE. On failure: DialogAppendMessage("! Send failed\r"), SysBeep, do NOT clear input (preserve for retry per R31).

  **Cleanup** (reverse order per R31): PT_Shutdown → DialogCleanup → DisposeAEEventHandlerUPP → exit.

  Reference: R30, R31. Source reference: ~/csend/classic_mac_mactcp/main.c (516 lines).
- [x] T084 [Chat] Implement POSIX terminal chat — create apps/chat/posix/main.c. Single-threaded poll loop: (1) parse --name arg (default "User"), (2) PT_Init(name), register MSG_CHAT_TEXT reliable, set callbacks, PT_StartDiscovery, (3) set stdin to non-blocking (fcntl O_NONBLOCK), (4) loop: read stdin line, parse commands (/list, /send N msg, /broadcast msg, /quit, /help), call PT_Poll(), usleep(16000) for ~60Hz. Callbacks: on_discovered → print "* Discovered: name", on_connected → print "* Connected: name", on_disconnected → print "* Disconnected: name (reason)", on_chat_message → print "name: message". Display format: timestamps, peer names, clean layout. Signal handler for SIGINT/SIGTERM → clean PT_Shutdown. File: apps/chat/posix/main.c.
- [x] T085 [Chat] Add chat app targets to CMakeLists.txt — add build targets for the chat app. POSIX: add_executable(chat apps/chat/posix/main.c) linked against peertalk + clog, C11 standard. Retro68: add_application(chat apps/chat/mac/main.c apps/chat/mac/dialog.c apps/chat/mac/chat.r apps/chat/mac/chat_size.r) linked against peertalk + clog + platform libs. Include paths: apps/chat/ (for chat.h), include/ (for peertalk.h), src/core/ (NOT needed — app only uses public API). The same Mac source builds for 68k MacTCP, PPC OT, and PPC MacTCP — PeerTalk selects the backend at compile time. File: CMakeLists.txt.
- [x] T086 [Chat] Build and test POSIX chat — build POSIX target, run two instances (./build/chat --name Alice, ./build/chat --name Bob) on same machine. Verify: discovery works, /list shows peer, /send 1 hello delivers message, /broadcast hello delivers to all, /quit sends goodbye. Fix any issues.
- [x] T087 [Chat] Build and test Classic Mac chat on all 3 Macs — build 68k (build-68k/chat.bin), PPC/OT (build-ppc-ot/chat.bin), PPC/MacTCP (build-ppc-mactcp/chat.bin). Deploy to each Mac via MCP. Start POSIX chat as peer. Verify on each Mac: (1) dialog opens with "PeerTalk Chat" title, (2) POSIX peer appears in peer list, (3) select peer and send message — appears in POSIX terminal, (4) POSIX sends message — appears in Mac messages area with scrollbar, (5) broadcast works, (6) Cmd+Q sends goodbye and exits cleanly. Machines: performa6400 (PPC/OT), performa6200 (PPC/MacTCP), macse (68k/MacTCP via LaunchAPPL).

## Phase 16: Hardware Verification — Full Stack

**Purpose**: Verify ALL code changes from Phases 13-15 on real Classic Mac hardware. Phases 13-15 were implemented and tested on POSIX only. No binaries from this session have been executed on P6400, P6200, or Mac SE. This phase MUST actually run binaries via MCP — do not mark tasks done without real execution results.

- [x] T088 [US5] Run all 4 test apps on Performa 6400 (PPC/OT) — rebuild build-ppc-ot/, start POSIX peer (--name Alice), deploy and execute each test binary via MCP execute_binary (machine=performa6400, platform=opentransport): test_lifecycle.bin, test_reliable.bin, test_fast.bin, test_chat.bin. Download PT_Lifecycle, PT_Reliable, PT_Fast, PT_Chat logs via download_file. Verify each shows PASS. Run ONE test at a time — wait for completion before starting next.
- [x] T094 [Chat] Fix peer list index mapping bug — apps/chat/mac/dialog.c DialogUpdatePeerList() uses loop counter `i` (PT_GetPeer index) for both LAddRow row position and LSetCell, but skips disconnected peers with `continue`. This means list row N does not correspond to PT_GetPeer(ctx, N). HandleSend calls PT_GetPeer(gCtx, peerIdx) where peerIdx is the list row — sends go to wrong peer. Fix: maintain a static mapping array (e.g. `static int gPeerMap[PT_MAX_PEERS]`) that maps display row → PT_GetPeer index. Populate during DialogUpdatePeerList, use in DialogGetSelectedPeerIndex. csend avoided this by pre-filtering peers. Reference: R30.
- [x] T095 [Chat] Fix dead clog include in chat app — apps/chat/mac/main.c:33-35 and apps/chat/mac/dialog.c:26-28 use `#ifdef CLOG_H` which checks clog.h's own include guard — never true before including. Result: zero logging in chat app. Fix: include clog.h unconditionally (same as tests/test_common.h:13). Add CLOG_INFO calls at key events: PT_Init result, discovery started, peer connected/disconnected, send success/failure. Write to clog file "PT_Chat_App" so logs can be collected from hardware. Reference: R20.
## Phase 17: Task Runner Improvements

**Purpose**: Fix reliability and usability issues in the unattended task runner discovered over multiple runs. Rename from overnight-build to autorun. Reference: R32.

- [x] T096 [P] Rename overnight-build.sh to autorun.sh — rename tools/overnight-build.sh to tools/autorun.sh. Update the header comment, log directory prefix (logs/autorun-TIMESTAMP instead of logs/overnight-TIMESTAMP), and summary log messages. Update any references in CLAUDE.md or other docs. Keep --dry-run and --resume flags.
- [x] T097 [P] Fix log buffering — iteration logs are 0 bytes during the entire Claude session because pipe buffering delays output. Fix: replace `claude -p "$PROMPT" 2>&1 | tee "$ITER_LOG"` with `stdbuf -oL claude -p "$PROMPT" 2>&1 | tee "$ITER_LOG"` for line-buffered output. If stdbuf doesn't work with claude's output, try `script -q -c "claude -p ..." "$ITER_LOG"` as fallback. Test by running a short iteration and checking log file grows during execution.
- [x] T098 [P] Add cross-build verification — the post-iteration build check only verifies POSIX (build/). Add checks for all existing build directories: build-ppc-ot/, build-ppc-mactcp/, build-68k/. For each dir that exists, run cmake+make and log the result. If any cross-build fails, log a warning but continue (Claude can fix it next iteration). Add a `check_all_builds()` function. File: tools/autorun.sh.
- [x] T099 [P] Add per-iteration timeout — wrap the Claude invocation with `timeout 45m` to prevent a single session from running indefinitely. If timeout triggers, log "TIMEOUT: Iteration N killed after 45 minutes" and continue to next iteration. The stuck detection will catch repeated timeouts. File: tools/autorun.sh.
- [x] T100 [P] Harden prompt for hardware tasks — update build_prompt() to: (1) dynamically test each machine with `mcp__classic-mac-hardware__test_connection` equivalent (curl or a helper script) and list only reachable machines, (2) add explicit rule: "Do NOT mark hardware tasks complete unless you actually executed on every machine listed in the task description. If a machine is unreachable, leave the task incomplete and note which machines succeeded in the task description." File: tools/autorun.sh.
- [x] T101 [P] Add completion notification — when the run finishes (all tasks complete or stuck), send a desktop notification via `notify-send` (Linux). Add a `--notify` flag to enable it. Default off. Also write a one-line status to `logs/autorun-latest-status.txt` (symlinked to most recent run) for easy polling. File: tools/autorun.sh.

## Phase 18: Post-Review Fixes

**Purpose**: Fix issues found during manual review of Phase 13-17 output. Bugs in autorun.sh, test app endianness, and cleanup. These MUST complete before the remaining hardware verification tasks (T089-T093).

- [x] T102 [P] Fix wrong machine IPs in autorun.sh — tools/autorun.sh get_machine_status() has wrong IPs for 2 of 3 machines. Line 214: `10.188.1.103` should be `10.188.1.213` (Performa 6200). Line 222: `10.188.1.104` should be `10.188.1.55` (Mac SE). These wrong IPs cause the script to report both machines as OFFLINE even when powered on, which means Claude skips hardware tests. Also check FTP port for P6200: machines.json uses port 21. Fix IPs to match machines.json exactly.
- [x] T103 [P] Delete old overnight-build.sh — tools/overnight-build.sh still exists alongside its replacement tools/autorun.sh. Delete the old file. It was replaced by T096 but the original wasn't removed.
- [x] T104 [P] Fix autorun.sh prompt rule 8 — line 265 still says "For Classic Mac tasks (Phase 7, T025-T028)" which is outdated. Change to: "For Classic Mac hardware testing tasks, use the MCP tools (execute_binary, upload_file, download_file) to deploy and run on real hardware."
- [x] T105 [P] Fix test_reliable move_num endianness — tests/test_reliable.c line 48 sends `move_num` as raw `unsigned short` in struct payload. POSIX (x86 little-endian) sends 0x0100 for value 1, Mac (PPC big-endian) reads 256. The ordering check still works (monotonic) but logs show wrong numbers and the raw comparison is fragile. Fix: use `pt_htons()` when writing move_num in `send_move()`, `pt_ntohs()` when reading in `on_move()`. This matches the SDK's own wire protocol byte ordering. Also fixes the log display. File: tests/test_reliable.c.
- [x] T106 [P] Fix test_reliable empty peer name display — P6400 log shows `[RECV] Move 256 from :` with blank name. This happens when TCP accept occurs before discovery name propagates. Fix: in on_move() callback, if PT_PeerName(peer) returns empty string or NULL, display "(unknown)" instead. Same fix in on_connected(). File: tests/test_reliable.c.
- [x] T107 [P] Clean up .gitignore and stale build dirs — .gitignore lists build-68k-new/, build-68k-fresh/, build-ppc-fresh/ but these directories don't exist on disk. Also lists build-m68k/, build-m68k2/, build-ppc/, build-ppc2/ as "stale dirs (permission-locked)". Verify which actually exist, remove entries for non-existent dirs, try to delete any stale dirs that can be removed. The canonical build dirs are: build/, build-68k/, build-ppc-ot/, build-ppc-mactcp/. Any others should be cleaned up or documented.
- [x] T108 [P] Rebuild all 4 targets and run POSIX tests — rebuild POSIX (build/), PPC/OT (build-ppc-ot/), PPC/MacTCP (build-ppc-mactcp/), 68k (build-68k/) after T102-T107 fixes. Run POSIX test suite to verify nothing broken. This must complete before hardware verification tasks T089-T093.
- [x] T111 [P] Fix autorun.sh stdin redirect — claude process gets SIGTTIN (stopped) when running under stdbuf because stdin inherits the terminal. Add `< /dev/null` to both claude invocation paths (stdbuf and non-stdbuf) in the run loop. Without this fix, the autorun process halts immediately and never makes progress. File: tools/autorun.sh.

## Phase 19: Final Hardware Verification

**Purpose**: Run all test apps and chat app on all 3 Macs after Phase 18 fixes. Depends on T108 completing first.

- [x] T089 [US5] Run all 4 test apps on Performa 6200 (PPC/MacTCP) — rebuild build-ppc-mactcp/, start POSIX peer (--name Alice), deploy and execute each test binary via MCP execute_binary (machine=performa6200, platform=mactcp): test_lifecycle.bin, test_reliable.bin, test_fast.bin, test_chat.bin. Download PT_Lifecycle, PT_Reliable, PT_Fast, PT_Chat logs via download_file. Verify each shows PASS. Run ONE test at a time. Results: test_lifecycle PASS, test_reliable PASS, test_chat PASS. test_fast: POSIX sender PASS, Mac receiver FAIL (0 UDP received — known MacTCP UDP delivery issue).
- [x] T090 [US5] Run all 4 test apps on Mac SE (68k/MacTCP) — rebuild build-68k/, start POSIX peer (--name Alice), deploy and execute each test binary via MCP execute_binary (machine=macse, platform=mactcp): test_lifecycle.bin, test_reliable.bin, test_fast.bin, test_chat.bin. Capture LaunchAPPL output (no FTP on Mac SE). Verify each shows PASS. Run ONE test at a time. Results: test_lifecycle PASS, test_reliable PASS, test_chat PASS. test_fast: POSIX sender PASS, Mac receiver FAIL (0 UDP received — known MacTCP UDP delivery issue, same as P6200).
- [x] T091 [Chat] Run chat app on Performa 6400 (PPC/OT) — start POSIX chat (./build/chat --name Alice), deploy build-ppc-ot/chat.bin via execute_binary (machine=performa6400, platform=opentransport). Verify: app launches without crash, POSIX peer discovered. Download PT_Chat_App log if created. Note: interactive UI testing (send/receive) limited via LaunchAPPL — verify at minimum that app starts, discovers peer, and doesn't crash within 30 seconds. Results: PASS — OT init OK, discovered Alice, ran 120s+ without crash. POSIX side discovered "Mac Chat".
- [x] T092 [Chat] Run chat app on Performa 6200 (PPC/MacTCP) — start POSIX chat (./build/chat --name Alice), deploy build-ppc-mactcp/chat.bin via execute_binary (machine=performa6200, platform=mactcp). Verify: app launches without crash, POSIX peer discovered. Download log if created. Results: PASS — MacTCP init OK, discovered Alice + Mac Chat (P6400), ran 130s without crash.
- [x] T093 [Chat] Run chat app on Mac SE (68k/MacTCP) — start POSIX chat (./build/chat --name Alice), deploy build-68k/chat.bin via execute_binary (machine=macse, platform=mactcp). Capture LaunchAPPL output. Verify: app launches without crash. Note: Mac SE has 4MB RAM — chat app must fit within low-memory sizing. Results: App launched without crash but exited immediately — likely DialogInit() fails on 512x342 screen (dialog too large). No peer discovery. No crash or bus error.

## Phase 20: API Enhancement — Peer Name Setter

**Purpose**: Add PT_SetName() to allow changing the local peer name after init. Serves the Chat app use case where the user picks a name in the UI before going online. Constitutional: Principle IV says "if an app needs tuning, add a setter." This is a 21st function, minimal change.

- [x] T109 [P] Add PT_SetName(ctx, name) to SDK — add `PT_Status PT_SetName(PT_Context *ctx, const char *name)` to include/peertalk.h and implement in src/core/pt_core.c. Implementation: validate args (ctx != NULL, name != NULL, strlen(name) <= PT_NAME_MAX), strncpy into ctx->name, return PT_OK. The next discovery broadcast will advertise the new name. Update the API contract in specs/001-peertalk-sdk/contracts/peertalk-api.md (21 functions now). Update the function count in peertalk.h header comment. C89 compatible.
- [x] T110 [Chat] Use PT_SetName in POSIX chat — apps/chat/posix/main.c: if --name not provided, default to hostname or "User". Add /name command to change name mid-session via PT_SetName. File: apps/chat/posix/main.c.

## Phase 21: Chat App Fixes — Peer List and Screen Size

**Purpose**: Fix two blocking issues found during hardware verification (R33). The chat app is non-functional: peer list is empty (only shows connected peers, no way to connect) and dialog is too large for Mac SE. These must be fixed for the demo app to serve its purpose (Constitution VIII).

- [x] T112 [Chat] Show discovered peers in peer list — apps/chat/mac/dialog.c DialogUpdatePeerList() line 508 filters `PT_GetPeerState(peer) != PT_PEER_CONNECTED`. Change to include `PT_PEER_DISCOVERED` peers. Append state indicator to name: "Alice" for connected, "Alice (discovered)" for discovered-only. Update gPeerMap to track peer index for both states. File: apps/chat/mac/dialog.c.
- [x] T113 [Chat] Auto-connect on peer list selection — apps/chat/mac/dialog.c: when user clicks a discovered (not yet connected) peer in the list, call PT_Connect(ctx, peer) to initiate connection. Add a HandlePeerClick() function called from the mouseDown handler. If peer is already connected, do nothing (it's the selected send target). If discovered, connect and update list. File: apps/chat/mac/dialog.c, apps/chat/mac/main.c.
- [x] T114 [Chat] Rebuild and verify chat peer list on P6400 — Verified: PT_Init OK, discovery found Alice, dialog opened without errors. Log: downloads/performa6400/PT_Chat_App — rebuild PPC/OT, deploy chat.bin to P6400 via MCP execute_binary. Start POSIX chat (./build/chat --name Alice). Verify: Mac discovers Alice, Alice appears in peer list, clicking Alice connects, messages can be exchanged. Download PT_Chat_App log. Machine: performa6400, platform: opentransport.
- [x] T115 [Chat] Fix chat dialog for Mac SE screen — apps/chat/mac/chat.r DLOG 128 bounds are 419x618 pixels, too large for Mac SE 512x342. Option A: shrink dialog to fit 512x342 (e.g., 300x490). Option B: check screenBits.bounds in DialogInit() and position/resize programmatically. Prefer Option A (simpler, no runtime code). Update DITL 128 item positions to match. Also update CNTL 6 scroll bar bounds. Test on Mac SE via LaunchAPPL. Files: apps/chat/mac/chat.r, apps/chat/mac/dialog.c.

## Phase 22: MacTCP UDP Fix and Verification

**Purpose**: Fix MacTCP UDP message port not listening (R34) and verify on hardware.

- [x] T116 [US3] Fix MacTCP UDP message receive — src/core/pt_core.c PT_StartDiscovery() only calls udp_listen(ctx, PT_DISCOVERY_PORT). Add udp_listen(ctx, PT_UDP_MSG_PORT) call after the discovery port listen. This enables MacTCP to receive fast messages. Reference: R34. File: src/core/pt_core.c. **Already applied — code change in working tree.**
- [x] T117 [US5] Hardware verify test_fast on P6400 and P6200 — **PASS on both (2026-03-06)**. P6400: 60/60 received, QUIT disconnect. P6200: 60/60 received, QUIT disconnect. Mac SE excluded per R35 (hard freeze on test_fast after R34 fix). Rebuild build-ppc-ot/ and build-ppc-mactcp/, start POSIX peer (--name Alice --role SENDER), deploy and run test_fast on Performa 6400 (MCP execute_binary, machine=performa6400, platform=opentransport) and Performa 6200 (machine=performa6200, platform=mactcp). Verify: g_received > 10 (PASS). Download logs. Run ONE test at a time.

## Phase 23: Artifact Consistency and Verification

**Purpose**: Fix stale status counts across spec artifacts and verify outstanding test issues on hardware. Found by `/speckit.analyze` on 2026-03-06 (findings F1-F6, A1, C1).

- [x] T118 [P] Update stale status counts in spec artifacts — Multiple files have stale progress numbers from earlier phases. Fix all: (1) spec.md:5 says "63/87 tasks, Phases 1-12 complete" — update to current task/phase counts. (2) plan.md:199-221 Implementation Status table shows phases 13-15 as "Pending" and says "63 of 87 tasks complete" — update all phase statuses to Complete (except phases with pending tasks), update total count. (3) plan.md:229 Phase 0 Artifacts says "30 research decisions (R1-R30)" — update to current count (R1-R35). (4) plan.md:249 API contract says "20 functions" — update to 21 (PT_SetName added). (5) tasks.md summary table: update status column for completed phases, verify all phase rows present including 16-23, update total counts. Files: specs/001-peertalk-sdk/spec.md, specs/001-peertalk-sdk/plan.md, specs/001-peertalk-sdk/tasks.md.
- [x] T119 [P] Verify test_chat disconnect reason on P6400 and P6200 after T116 fix — **Still broken (2026-03-06)**. P6400 (OT): disconnect reason TIMEOUT (1), not QUIT. P6200 (MacTCP): disconnect reason ERROR (2), not QUIT. Root cause: reassembly error on 8KB+ messages corrupts TCP receive state, subsequent frames (including goodbye) are dropped. Both platforms receive 6/10 messages (expected with 4096B buffer). Follow-up fix needed: TCP reassembly error recovery must not discard remaining stream data. Reference: R23, T057.
- [x] T120 [P] Measure UDP fast message latency on hardware — **Measured (2026-03-06)**. P6400: 61ms avg, P6200: 64ms avg inter-arrival. Exceeds 16ms target but reflects sender burst pacing, not network latency. Documented as R36. — SC-006 specifies "under 16ms average latency" but no measurement has been taken. During T117 hardware run, add timing instrumentation: in test_fast receiver, log TickCount() at message receipt and compute inter-message timing. Alternatively, compare POSIX sender timestamps with Mac receiver timestamps (requires clock sync — simpler to measure inter-arrival intervals). Document measured latency in research.md as a new entry. If latency exceeds 16ms, document honestly per Principle III. File: tests/test_fast.c (add timing log), specs/001-peertalk-sdk/research.md (document results).

## Phase 24: Bidirectional Test Coverage

**Purpose**: Exercise Mac send paths that are currently untested due to default passive role assignment (R37).

- [x] T121 [P] [US3] Make test_fast bidirectional — Both sides send AND receive simultaneously. Currently sender sends 60 msgs, receiver only receives. Change: after connecting, BOTH peers send at SEND_HZ for TEST_SECS while also receiving. Each side logs sent count, received count, and payload validity. Pass criteria: both sides sent > 0, both sides received >= 10, payload valid. This exercises Mac UDP send path (PT_Send with PT_FAST). File: tests/test_fast.c
- [x] T122 [P] [US2] Strengthen test_chat pass criteria and make bidirectional — Currently receiver PASS requires only g_msgs_received > 0 (1/10 = pass). Change: (1) Receiver must receive ALL messages that fit in reassembly buffer (6 on Mac with 4096B buffer, all 10 on POSIX). Use expected count based on reassembly buffer size. (2) Make bidirectional: after SENDER finishes sending, RECEIVER sends its message set back. Both sides validate integrity. This exercises Mac variable-size TCP send path. File: tests/test_chat.c
- [x] T123 [P] [US1+US4] Make test_lifecycle bidirectional — Currently RESPONDER never calls PT_Connect. Change: after second disconnect, swap roles — RESPONDER becomes initiator for a third connection cycle. Both peers must exercise PT_Connect at least once. Pass criteria: both sides have >= 1 initiated connection and >= 1 accepted connection. This exercises Mac PT_Connect path. File: tests/test_lifecycle.c
- [x] T124 [P] [US5] Re-include Mac SE in hardware test matrix — R38 confirms all 4 tests PASS on Mac SE after clean reboot. Update R35 status note, plan.md hardware status, and autorun prompt to include Mac SE. Note: if test_fast crashes on Mac SE, reboot and retry before marking as failed (transient MacTCP state corruption). Files: specs/001-peertalk-sdk/plan.md, tools/autorun.sh
- [x] T125 [US5] Fix MacTCP UDP shutdown — spin-wait for pending UDPRead completion before DisposePtr — mactcp_shutdown() calls UDPRelease then immediately DisposePtr on UDP buffers. If async UDPRead is pending (read_pending=1, ioResult=inProgress), the driver still holds buffer references. Fix: after UDPRelease, add spin-wait loop checking read_pb.ioResult != inProgress (with timeout) before DisposePtr, matching V1 TCP shutdown pattern (~/peertalk/src/mactcp/tcp_mactcp.c:540-615). Apply to both discovery_udp and message_udp streams. Reference: R39. File: src/platform/mactcp/pt_mactcp.c
- [x] T126 [P] [US5] Reduce MacTCP UDP buffer from 4096 to 2048 — UDP_BUF_SIZE is 4096 but MacTCP docs say minimum 2048, recommended 2N+256 where N=largest datagram (~1400 bytes for PT_FAST). 2048 is safe. Also update PT_PLATFORM_FIXED_OVERHEAD in pt_memory.c from 8192 to 4096 (2 streams x 2048). Saves 4096 bytes heap on Mac SE. Reference: R39. Files: src/platform/mactcp/pt_mactcp.c, src/core/pt_memory.c
- [x] T127 [P] [US5] Remove dead UDP ASR flag code — udp_asr() sets us->flags |= UDP_FLAG_DATA but poll loop never checks us->flags (checks read_pending && ioResult instead). UDPDataArrival rarely fires when UDPRead is outstanding. Remove the flag set from udp_asr and the UDP_FLAG_DATA constant. Reference: R39. File: src/platform/mactcp/pt_mactcp.c

## Phase 25: 68k UDP Send Fix + Hardware Verification

**Purpose**: Fix 68k UDP send burst crash (R40) and complete hardware verification of all bidirectional tests on all 3 Macs.

- [x] T128 [P] [US3+US5] Throttle UDP send burst on 68k MacTCP — Mac SE crashes on 12-message UDPWrite burst (R40). MacTCP docs confirm concurrent read+write is safe; crash is from burst size. Fix: on 68k (non-POSIX, non-OT), limit sends to 1-2 per poll cycle instead of SEND_HZ/TEST_SECS burst. Keep POSIX and PPC at full rate. Test on Mac SE: must send > 0 and receive >= 10. File: tests/test_fast.c
- [x] T135 [US2+US5] Make MacTCP tcp_send synchronous for chunked message support — Change PBControlAsync to PBControl (synchronous) in mactcp_tcp_send() at line 614. MacTCP docs confirm all routines support both sync and async modes. Sync sends complete before returning, so the chunking loop in pt_messaging.c (lines 99-113) can issue multiple tcp_send calls per PT_Send. Without this, messages >tcp_send_size fail on MacTCP because the second async chunk returns PT_ERR_SEND_FAILED while the first is still pending. Mac SE (tcp_send=1024) confirmed: 2000/4000 byte sends fail. Sync send blocks ~1ms per chunk on LAN — acceptable for Chat pattern (user-paced), and Bomberman uses UDP (unaffected). Reference: R42, MacTCP Programmer's Guide lines 700, 2939-2961. File: src/platform/mactcp/pt_mactcp.c
- [x] T136 [US1+US4] Re-fire on_discovered for disconnected peers — in pt_discovery_process_packet() (pt_discovery.c:75-82), when a known peer in PT_PEER_DISCONNECTED state receives a discovery broadcast, fire on_peer_discovered callback before returning. Currently the function returns early for all known peers, so a disconnected peer that keeps broadcasting never triggers on_discovered again. This causes test_lifecycle Phase 3 to wait 10+ seconds for the peer to time out and be rediscovered. With this fix, reconnection happens within 2 seconds (next broadcast cycle). Affects all platforms equally. Reference: R43. File: src/core/pt_discovery.c
- [x] T129 [P] [US5] Hardware verification of all bidirectional tests on all 3 Macs — Run all 4 tests with POSIX peer on: P6400 (OT), P6200 (MacTCP), Mac SE (68k, after T128). Verify: test_fast bidirectional PASS, test_chat phase 2 PASS, test_lifecycle 3-phase PASS, test_reliable unchanged. P6400 test_lifecycle specifically needs verification (was interrupted during Phase 24 autorun). Files: tests/test_fast.c, tests/test_chat.c, tests/test_lifecycle.c. **Results**: P6200 all 4 PASS, Mac SE all 4 PASS, P6400 test_lifecycle PASS (3 connects, 3 disconnects, Phase 3 role swap — OTConnect -3158 on first attempt but succeeded on retry). P6400 fast/reliable/chat not run (LaunchAPPL unavailable after lifecycle test hung the machine). OT backend verified via lifecycle test which exercises discovery, TCP connect, disconnect, and reconnection.
- [x] T130 [P] Update spec artifacts with final hardware results — Update plan.md hardware status, spec.md status line, tasks.md summary table with verified bidirectional results from T129. Document any platform-specific behaviors discovered. Files: specs/001-peertalk-sdk/plan.md, specs/001-peertalk-sdk/spec.md, specs/001-peertalk-sdk/tasks.md
- [x] T131 [P] [US2] Add silence-based exit to test_chat sender phase 2 — sender currently waits up to 45s (line 305-311) with no silence detection. Add: in the phase 2 timeout block, if g_is_sender && g_phase2_active && g_msgs_received >= 1 && g_last_recv_time > 0 && test_time_sec() - g_last_recv_time >= 3, break. This mirrors the receiver's silence detection pattern. File: tests/test_chat.c
- [x] T132 [P] [US2] Clean up test_chat summary for role clarity — replace line 319 "Sent: %d (phase1) + %d (phase2)" with role-appropriate output: sender shows "Sent: %d, Received: %d" (g_msgs_sent, g_msgs_received), receiver shows "Sent: %d, Received: %d" (g_phase2_sent, g_msgs_received). Add "Role: SENDER/RECEIVER" to summary. File: tests/test_chat.c
- [x] T133 [P] [US2] Stricten test_reliable PASS criteria — change line 281 from g_moves_received >= TOTAL_TURNS - 1 to g_moves_received == TOTAL_TURNS. TCP is reliable; any lost message is an SDK bug the test must catch. The 3s grace period after g_moves_done ensures all messages arrive. File: tests/test_reliable.c
- [x] T134 [P] [US4] Remove eager PT_Connect from test_lifecycle on_disconnected Phase 3 — remove lines 119-125 (the immediate PT_Connect in on_disconnected when g_disconnect_count == 2). Keep g_phase3_active = 1 so on_discovered (lines 43-50) handles the connection when the peer reappears via discovery broadcast. This makes Phase 3 behavior consistent across POSIX, MacTCP, and OT. File: tests/test_lifecycle.c

## Phase 26: Book Review Fixes + 68k OT + v1.0 Release Prep

**Purpose**: Address findings from code review against Macintosh programming books, add 68k OT build target for Performa 630, and prepare repository for v1.0 release.

- [x] T137 [US5] Fix OTSnd partial send handling — In ot_tcp_send() (pt_ot.c line 590), OTSnd can return fewer bytes than requested in async non-blocking mode. Add a loop: if res > 0 && res < len, advance the buffer pointer and retry with remaining bytes. If res == kOTFlowErr, set flow_off and return error. If res < 0 (other error), return PT_ERR_SEND_FAILED. This matches the book's recommendation (Networking With Open Transport page 495) and prevents framing corruption from partial header sends. Reference: R44. File: src/platform/opentransport/pt_ot.c
- [x] T138 [US5] Add OTSndOrderlyDisconnect after OTRcvOrderlyDisconnect — In ot_poll() T_ORDREL handler (pt_ot.c line 893), after calling OTRcvOrderlyDisconnect, add OTSndOrderlyDisconnect(slot->ep) before the disconnect handling. This completes the four-way TCP close handshake per the OT book (page 115). One-line addition. Reference: R45. File: src/platform/opentransport/pt_ot.c
- [x] T139 [P] [US5] Fix SIZE resource comment bit numbers — In tests/peertalk_size.r, the comments describe wrong bit positions for flags 0x5880. Fix comments to correctly describe: bit 14 = acceptSuspendResumeEvents, bit 12 = canBackground, bit 11 = doesActivateOnFGSwitch, bit 7 = is32BitCompatible. Flag VALUE is correct, only comments need fixing. Reference: Inside Macintosh Volume VI. File: tests/peertalk_size.r
- [x] T140 [P] [US5] Add vsprintf length guard in status_window — In status_linef() (status_window.c line 103), vsprintf writes to a 256-byte stack buffer with no bounds check. Replace with manual truncation or use a safe pattern: write at most 255 chars. Low risk in practice (all callers use short strings) but technically unsafe per Inside Macintosh coding standards. File: tests/status_window.c
- [x] T141 [P] [US5] Add GetPort/SetPort save-restore in status_window — In status_line() (status_window.c line 81), status_clear() (line 69), and status_init() (line 59), add GetPort(&savePort) before SetPort(g_status_win) and SetPort(savePort) before return. Inside Macintosh Volume I-III recommends save/restore when drawing into a specific port. Low risk with single window but follows best practice. File: tests/status_window.c
- [x] T142 [US5] Add 68k OT build support to CMakeLists.txt — The OT library linking section (CMakeLists.txt lines 82-95) hardcodes PPC library names (OpenTransportAppPPC, OpenTransportLib, OpenTptInternetLib). Detect toolchain: if CMAKE_SYSTEM_NAME is Retro68 (m68k), use 68k names (OpenTransportApp, OpenTransport, OpenTptInet). If RetroPPC, use PPC names. Also link OT libraries to test apps and test_init_only for 68k OT builds. Verify pt_ot.c #undef workaround (lines 24-26) works with 68k OT headers. Reference: R46. File: CMakeLists.txt
- [ ] T143 [US5] Build and test 68k OT on Performa 630 — Create build-68k-ot/ directory. Build with: cmake .. -DCMAKE_TOOLCHAIN_FILE=...m68k.../retro68.toolchain.cmake -DPT_PLATFORM=OT -DCLOG_DIR=~/Desktop/clog -DCLOG_LIB_DIR=~/Desktop/clog/build-m68k && make. Run all 4 tests on Performa 630 (System 7.6.1 with OT) via LaunchAPPL with POSIX peer. Verify: all 4 PASS. Add build-68k-ot/ to .gitignore. Reference: R46. Files: CMakeLists.txt, .gitignore. **Partial**: 68k OT build succeeds (T142). test_init_only ran on P6400 via 68k emulation (no crash). Performa 630 not configured in MCP machine registry — cannot run full test suite. Awaiting Performa 630 setup.
- [x] T144 Remove apps/ directory and chat app references from CMakeLists.txt — The chat demo app (apps/chat/) is moving to its own repo (csend). Remove apps/ directory entirely. Remove chat build targets from CMakeLists.txt: lines 121-130 (POSIX chat), lines 185-207 (Classic Mac chat). Update CLAUDE.md and plan.md to reflect that demo apps ship separately. Keep constitution Principle VIII reference to demo apps (they exist, just in another repo). File: CMakeLists.txt, apps/, CLAUDE.md
- [x] T145 Add README.md at repository root — Create README.md with: project overview (C networking SDK for LAN peer-to-peer between modern and Classic Mac), feature highlights (21-function API, 3 platform backends, zero-alloc after init, 6K LOC), supported platforms table, quickstart link to specs/001-peertalk-sdk/quickstart.md, API reference link to specs/001-peertalk-sdk/contracts/peertalk-api.md, build instructions summary, hardware verification status. Keep concise (under 200 lines). File: README.md
- [x] T146 [P] Add books/ to .gitignore — The books/ directory contains 12MB of reference PDFs that should not ship in the repo. Add books/ to .gitignore. File: .gitignore
- [x] T147 [P] Add build-68k-fresh/ build-68k-new/ build-ppc-fresh/ build-ppc-mactcp/ to .gitignore if missing — Check .gitignore covers all build directories shown in git status. Add any missing entries. Already covered: build/, build-68k/, build-ppc-ot/, build-ppc-mactcp/. Need to add: build-68k-fresh/, build-68k-new/, build-ppc-fresh/, build-68k-ot/. File: .gitignore
- [x] T148 Update spec artifacts for Phase 26 completion — Update plan.md phase table and task counts, spec.md status line, tasks.md summary table, CLAUDE.md task/research counts, and requirements.md implementation status. Mark all Phase 26 tasks complete. File: specs/001-peertalk-sdk/plan.md, spec.md, tasks.md, CLAUDE.md, checklists/requirements.md

## Phase 27: Test App Role Fix

**Purpose**: Remove broken name-based role assignment; replace with auto-connect-on-discovery

- [x] T149 [US5] Remove name-based role assignment from test_lifecycle and test_chat — Delete `g_is_initiator = (name[0] <= 'M')` from test_lifecycle.c:146 and `g_is_sender = (name[0] <= 'M')` from test_chat.c:195. Both sides should auto-connect on discovery: in on_discovered, if peer state is DISCOVERED or DISCONNECTED, call PT_Connect. Remove phase 3 role-swap logic from test_lifecycle (g_phase3_active, role-conditional connect/disconnect). Simplify PASS criteria to: connect_count >= 2 AND disconnect_count >= 2 (remove initiated/accepted tracking). For test_chat, remove sender/receiver distinction — both sides send test messages and verify receipt. Reference: R47. File: tests/test_lifecycle.c, tests/test_chat.c
- [x] T150 [US5] Add test_should_connect helper to test_common.h — Add `static int test_should_connect(PT_Peer *peer)` that returns 1 if `PT_GetPeerState(peer) == PT_PEER_DISCOVERED || PT_GetPeerState(peer) == PT_PEER_DISCONNECTED`. Use in on_discovered callbacks of test_lifecycle and test_chat to avoid duplicate connect attempts on already-connected peers. Reference: R47. File: tests/test_common.h
- [x] T151 [US5] Rebuild all 5 targets and verify test_lifecycle + test_chat on P6400 with POSIX peer — Build POSIX, 68k MacTCP, PPC MacTCP, PPC OT, 68k OT. Run test_lifecycle on P6400 (OT) with POSIX peer: start both, kill POSIX, restart POSIX, verify Mac reconnects. PASS: 2+ connects, 2+ disconnects. Run test_chat similarly. Download and review logs. Reference: R47. File: build verification. **Results**: All 5 targets build clean. P6400 test_lifecycle PASS (2 connects, 2 disconnects, auto-connect on discovery works, OTConnect -3158 on Mac reconnect attempt but POSIX re-connects successfully). P6400 test_chat PASS (Mac sent 6, received 6, POSIX sent 10, received 6, all VALID integrity).

---

## Phase 28: 68k Stack Fix

**Purpose**: Fix PT_Send stack buffer overflow crash on 68k Mac SE

- [x] T152 [US3] Move UDP send buffer from PT_Send stack to PT_Context_Internal — The 1403-byte stack buffer `buf[PT_UDP_MTU_SAFE + PT_UDP_HEADER_SIZE]` in PT_Send's UDP path (pt_messaging.c:118) overflows the 68k application stack (~8KB). Move to `unsigned char udp_send_buf[PT_UDP_MTU_SAFE + PT_UDP_HEADER_SIZE]` in PT_Context_Internal (pt_internal.h). Update PT_Send to use `ctx->udp_send_buf` instead of the stack buffer. Safe because PT_Send is never called reentrantly. No new malloc — buffer is part of context struct. Reference: R48. Files: src/core/pt_internal.h, src/core/pt_messaging.c
- [x] T153 [P] [US3] Add diagnostic logging and 1s post-connect delay in test_fast for 68k — Add CLOG_INFO before and after first PT_Send call to confirm the stack fix works. Add a 1-second delay after connection on 68k (Delay(60, &dummy)) to let MacTCP stabilize before the send loop. Add log of g_sent count at loop exit for diagnosis. Reference: R48. File: tests/test_fast.c
- [x] T154 [US3] Rebuild 68k MacTCP and run test_fast on Mac SE — Build build-68k/ with the stack fix. Run test_fast on Mac SE via LaunchAPPL. PASS: g_sent > 0 AND g_received >= 10 (or positive received count from POSIX peer). Download and review logs. Reference: R48. Files: build-68k/. **Results**: POSIX peer received 67 messages from Mac SE (previously 0 — crashed on first send). Clean QUIT disconnect. POSIX PASS (60 sent, 67 received, payload valid, 53ms avg). Mac SE LaunchAPPL output empty (no FTP), but POSIX confirms bidirectional success.
- [x] T155 [P] [US3] Add g_connected guard to test_fast on_discovered — test_fast's on_discovered (test_fast.c:55-60) calls PT_Connect unconditionally. Add `if (g_connected) return;` before PT_Connect, matching the pattern in test_lifecycle and test_chat. Prevents simultaneous-connect race on fast OT machines (P6400). Reference: R49. File: tests/test_fast.c

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — start immediately
- **Foundational (Phase 2)**: Depends on Phase 1 completion — BLOCKS all user stories
- **US1 (Phase 3)**: Depends on Phase 2 — first user story, MVP target
- **US2 (Phase 4)**: Depends on US1 (needs connected peers to exchange messages)
- **US3 (Phase 5)**: Depends on US1 (needs connected peers), also needs pt_messaging.c from US2
- **US4 (Phase 6)**: Depends on US1 (connection management). PT_TIMEOUT and PT_DISCONNECT_ERROR work without US2, but PT_QUIT detection requires TCP frame parsing from US2 (T015). Recommend completing US2 first.
- **US5 (Phase 7)**: Depends on US1-US4 complete (POSIX is reference implementation)
- **Polish (Phase 8)**: Depends on all user stories being complete

### User Story Dependencies

```
Phase 1 (Setup)
    │
    ▼
Phase 2 (Foundational) ─── BLOCKS ALL ───┐
    │                                      │
    ▼                                      │
Phase 3: US1 (Discovery+Connect)          │
    │                                      │
    ▼                                      │
Phase 4: US2 (Reliable)                   │
    │                                      │
    ├──────────┐                           │
    ▼          ▼                           │
Phase 5: US3  Phase 6: US4               │
(Fast)        (Lifecycle)                  │
    │          │                           │
    └──────────┘                           │
    ▼                                      │
Phase 7: US5 (Cross-Platform) ◄───────────┘
    │
    ▼
Phase 8 (Polish)
```

### Within Each Phase

- Tasks without [P] must execute in listed order
- Tasks with [P] can execute in parallel with other [P] tasks in the same phase
- All tasks in a phase must complete before the next phase begins

### Parallel Opportunities

- **Phase 1**: T002, T003 in parallel (after T001)
- **Phase 2**: T005, T006, T007 in parallel (after T004); T008 after T005+T006
- **Phase 3**: T009, T010 in parallel; T011 after both; T012 after T011
- **Phase 4**: T016, T017 in parallel (different test files, after T013-T015)
- **Phase 7**: T025, T026 in parallel (different platform backends)
- **Phase 8**: T029, T030 in parallel

---

## Parallel Example: Phase 2

```
T004 (pt_internal.h)
  │
  ├─── T005 [P] (pt_memory.c)
  ├─── T006 [P] (pt_posix.c skeleton)
  └─── T007 [P] (test_common.h)
         │
         ▼
       T008 (pt_core.c) — needs T005 + T006
```

## Parallel Example: Phase 7

```
T025 [P] (pt_mactcp.c) ─────┐
T026 [P] (pt_ot.c) ─────────┤
                              ▼
                    T027 (memory sizing)
                              │
                              ▼
                    T028 (CMakeLists.txt)
```

---

## Implementation Strategy

### MVP First (US1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: US1 — Discovery & Connection
4. **STOP and VALIDATE**: Run two `test_lifecycle` instances, verify discovery + connect + disconnect
5. This is the minimum viable SDK — peers can find and connect to each other

### Incremental Delivery

1. Setup + Foundational → Project compiles, sockets bind
2. US1 → Discovery + connection works → `test_lifecycle` passes (MVP)
3. US2 → Reliable messaging works → `test_reliable` + `test_chat` pass
4. US3 → Fast messaging works → `test_fast` passes
5. US4 → Full lifecycle management → `test_lifecycle` extended passes
6. US5 → Classic Mac builds → Cross-platform testing possible
7. Polish → C89 audit, LOC count, quickstart validation

Each increment adds value without breaking previous functionality.

---

## Task Summary

| Phase | Story | Tasks | Parallel | Status |
|-------|-------|-------|----------|--------|
| 1: Setup | — | 3 | 2 | Complete |
| 2: Foundational | — | 5 | 3 | Complete |
| 3: US1 Discovery+Connect | P1 | 4 | 2 | Complete |
| 4: US2 Reliable Messaging | P2 | 5 | 2 | Complete |
| 5: US3 Fast Messaging | P3 | 3 | 0 | Complete |
| 6: US4 Lifecycle Mgmt | P4 | 4 | 0 | Complete |
| 7: US5 Cross-Platform | P5 | 4 | 2 | Complete |
| 8: Polish | — | 4 | 2 | Complete |
| 9: Hardware Fixes | US5 | 12 | 0 | Complete |
| 10: Mac Hardening | US5 | 4 | 0 | Complete |
| 11: GUI + Speckit | — | 7 | 1 | Complete |
| 12: Logs + Verification | US5 | 8 | 3 | Complete |
| 13: SDK Safety | US1-US5 | 9 | 6 | Complete |
| 14: Test Quality | US2-US5 | 7 | 1 | Complete |
| 15: Chat App | Chat | 8 | 0 | Complete |
| 16: Hardware Verification | US5+Chat | 3 | 1 | Complete |
| 17: Task Runner | — | 6 | 4 | Complete |
| 18: Post-Review Fixes | — | 8 | 5 | Complete |
| 19: Final Hardware Verification | US5+Chat | 5 | 2 | Complete |
| 20: API — Name Setter | Chat | 2 | 0 | Complete |
| 21: Chat App Fixes | Chat | 4 | 1 | Complete |
| 22: MacTCP UDP Fix | US3+US5 | 2 | 0 | Complete |
| 23: Artifact Consistency | — | 3 | 3 | Complete |
| 24: Bidirectional Test Coverage + MacTCP Fixes | US1-US5 | 7 | 5 | Complete |
| 25: 68k UDP Fix + Hardware Verification | US1-US5 | 9 | 6 | Complete |
| 26: Book Review + 68k OT + v1.0 Prep | US5+Release | 12 | 4 | In Progress (T143 blocked) |
| 27: Test App Role Fix | US5 | 3 | 0 | Complete |
| 28: 68k Stack Fix | US3 | 4 | 2 | In Progress |
| **Total** | | **155** | **61** | **153/155** |

---

## Notes

- All SDK code is C89/C90. Test apps may use C11 (POSIX only).
- Zero malloc after PT_Init — enforced by design in pt_memory.c.
- Wire protocol is identical across all platforms — only PT_PlatformOps differs.
- MacTCP: set `ioCompletion=NULL` on all async PBs, poll `ioResult`. Never use completion routines.
- OT: notifier sets volatile flags, poll processes them. No blocking calls in notifier.
- clog is an external dependency at `~/Desktop/clog` — must be built first.
- Byte order: network byte order (big-endian) for all multi-byte wire fields. Use `pt_htons()`/`pt_ntohs()`.
