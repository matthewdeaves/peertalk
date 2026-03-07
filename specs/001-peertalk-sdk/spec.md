# Feature Specification: PeerTalk SDK

**Feature Branch**: `001-peertalk-sdk`
**Created**: 2026-02-28
**Status**: In Progress (153/154 tasks complete, Phases 1-28 nearly complete — T143 pending Performa 630 hardware setup)
**Input**: Technical specification from `spec.md`

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Peer Discovery and Connection (Priority: P1)

A developer initializes the SDK with a peer name and begins
discovering other peers on the local network. As peers appear,
the developer receives notifications and can see each peer's
name. The developer connects to a discovered peer, and the
remote side auto-accepts the connection. Both sides are
notified when the connection is established.

**Why this priority**: Discovery and connection are the
foundation for all communication. Without finding and
connecting to peers, no messages can be exchanged.

**Independent Test**: Can be fully tested by running two
instances on the same LAN, verifying they discover each other
by name, connect, and both receive connection confirmation.

**Acceptance Scenarios**:

1. **Given** two SDK instances on the same LAN, **When** both
   start discovery, **Then** each discovers the other within
   5 seconds and can read the other's peer name.
2. **Given** a discovered peer, **When** the developer
   initiates a connection, **Then** both sides receive a
   connection notification and peer state changes to connected.
3. **Given** a peer that was previously discovered, **When**
   that peer stops broadcasting for 10 seconds, **Then** the
   SDK fires a peer-lost notification.
4. **Given** discovery is active, **When** the developer stops
   discovery, **Then** broadcasting stops but listening
   continues so already-discovered peers can still refresh.

---

### User Story 2 - Reliable Message Exchange (Priority: P2)

A developer registers message types for guaranteed delivery,
then sends variable-size payloads to connected peers. Messages
arrive in order, complete, and exactly once. The developer can
send to a single peer or broadcast to all connected peers.
Messages of any size are accepted — the SDK handles
segmentation and reassembly transparently.

**Why this priority**: Reliable messaging is the most
broadly needed transport — the Chess and Chat target apps both
depend on it, and it serves as the default for any unregistered
message type.

**Independent Test**: Can be tested by having two connected
peers exchange messages of varying sizes (from a few bytes to
multi-kilobyte payloads) and verifying complete, ordered
delivery on both sides.

**Acceptance Scenarios**:

1. **Given** two connected peers, **When** one sends a
   reliable message, **Then** the other receives the complete
   message with correct type and payload.
2. **Given** two connected peers, **When** one sends a message
   larger than the platform buffer size, **Then** the SDK
   chunks and reassembles the message transparently and the
   receiver's callback delivers the complete message.
3. **Given** three or more connected peers, **When** one
   broadcasts a reliable message, **Then** all other connected
   peers receive the complete message.
4. **Given** a registered reliable message type, **When** a
   chunk does not arrive within 5 seconds, **Then** the
   partial message is discarded and the reassembly buffer
   freed.

---

### User Story 3 - Fast Message Exchange (Priority: P3)

A developer registers message types for low-latency delivery
and sends small, frequent payloads to connected peers. Messages
may be dropped but arrive with minimal delay. This supports
real-time patterns like position updates in action games.

**Why this priority**: Fast messaging enables the Bomberman
target app pattern (30-60 Hz position updates), but the SDK is
useful for Chess and Chat without it.

**Independent Test**: Can be tested by having one peer send
small payloads at high frequency and verifying the receiver
gets most messages with minimal delay, and that oversized
messages are properly rejected.

**Acceptance Scenarios**:

1. **Given** two connected peers, **When** one sends a fast
   message with a small payload, **Then** the other receives
   it with lower latency than a reliable message.
2. **Given** two connected peers, **When** one sends a fast
   message exceeding the datagram size limit (~1400 bytes),
   **Then** the send fails with an appropriate error status.
3. **Given** two connected peers, **When** one sends fast
   messages at 30-60 Hz, **Then** the receiver delivers
   messages via callback without requiring the sender to
   manage timing or batching.

---

### User Story 4 - Connection Lifecycle Management (Priority: P4)

A developer receives notifications for all peer lifecycle
events — discovery, connection, disconnection — with enough
context to react appropriately. Disconnections include a reason
(clean quit, timeout, or error) so the application can display
the right message or attempt reconnection. Clean shutdown
notifies all connected peers before closing.

**Why this priority**: Robust lifecycle handling is needed for
production-quality apps, but basic messaging works without
distinguished disconnect reasons.

**Independent Test**: Can be tested by connecting two peers
and exercising each disconnect path: clean shutdown, forced
kill (simulating crash), and network interruption.

**Acceptance Scenarios**:

1. **Given** a connected peer, **When** the remote peer shuts
   down cleanly, **Then** the local side receives a
   disconnect notification with reason "quit."
2. **Given** a connected peer, **When** the remote peer
   crashes or is killed, **Then** the local side receives a
   disconnect notification with reason "timeout" after 30
   seconds of TCP inactivity.
3. **Given** a connected peer, **When** the connection breaks
   unexpectedly, **Then** the local side receives a
   disconnect notification with reason "error."
4. **Given** a connected peer that has disconnected, **When**
   the peer is still broadcasting discovery, **Then** the
   peer remains in the peer list with disconnected state and
   can be reconnected.

---

### User Story 5 - Cross-Platform Communication (Priority: P5)

A developer's application on a modern computer (Linux or macOS)
discovers, connects to, and exchanges messages with an
application on a Classic Macintosh. The wire protocol is
identical across all platforms. The SDK adapts its internal
resource usage to the host hardware's available memory at
startup, requiring no configuration from the developer.

**Why this priority**: Cross-platform operation is the core
value proposition of PeerTalk, but implementing and validating
it depends on all prior stories being complete on at least one
platform first.

**Independent Test**: Can be tested by running a test app on
Linux and a test app on a Classic Mac (emulated or physical),
verifying bidirectional discovery, connection, and message
exchange.

**Acceptance Scenarios**:

1. **Given** a POSIX app and a Classic Mac app on the same LAN,
   **When** both start discovery, **Then** each discovers the
   other by name.
2. **Given** a POSIX app connected to a Classic Mac app,
   **When** one sends a reliable message, **Then** the other
   receives it identically regardless of platform.
3. **Given** a Classic Mac with limited RAM (4 MB), **When**
   the SDK initializes, **Then** it automatically sizes
   buffers and peer slots to fit available memory without
   developer configuration.
4. **Given** a machine with generous RAM (48+ MB), **When**
   the SDK initializes, **Then** it allocates larger buffers
   and more peer slots automatically.

---

### Edge Cases

- What happens when two peers have the same name on the
  network? (Discovery uses IP for identity, name is
  display-only.)
- What happens when a developer calls `PT_Send` on a peer that
  is in the discovered state but not connected? (Error status
  returned.)
- What happens when a developer sends a message with an
  unregistered type? (Defaults to reliable transport.)
- What happens when all peer slots are full and a new peer is
  discovered? (New peer is silently ignored. If `PT_OnError` is
  registered, error callback fires with `PT_ERR_NO_ROOM`.
  Existing connections unaffected.)
- What happens when the developer calls `PT_Shutdown` while
  messages are in flight? (Goodbye sent to all peers, pending
  sends may be lost.)
- What happens when the developer calls `PT_Poll` without
  starting discovery or connecting? (No-op, returns
  immediately.)
- What happens when `PT_Connect` is called on an
  already-connected peer? (Returns `PT_ERR_NOT_CONNECTED` —
  peer must be in DISCOVERED or DISCONNECTED state.)
- What happens when a TCP connection attempt times out?
  (After 10 seconds without completing, the connection attempt
  is abandoned. Error callback fires if registered. Peer
  remains in its previous state.)
- What happens when two peers have different buffer sizes (e.g.,
  POSIX with 4 KB send buffer, Mac SE with 1 KB send buffer) and
  exchange chunked messages? (The receiver derives chunk offsets
  from actual chunk payload sizes in the wire protocol, not from
  its own buffer configuration. Reassembly works correctly
  regardless of buffer size mismatch.)
- What happens when test apps use Retro68's console (printf) without
  Toolbox initialization? (On 68k, WaitNextEvent processes the event
  queue so console output reaches LaunchAPPL, but requires a valid
  GrafPort. On PPC, Delay() works without a GrafPort but doesn't
  process events. The v1 pattern of full Toolbox init + own windows
  avoids this conflict entirely.)
- What happens on Classic Mac when the application heap hasn't
  been extended? (PT_Init calls MaxApplZone() and MoreMasters()
  internally before any allocations. This extends the heap to
  its maximum and pre-allocates master pointers. The developer
  does not need to call these — the SDK handles it per
  Principle II.)
- What happens when a POSIX sender sends chunked messages to a
  Classic Mac with a smaller TCP receive buffer? (The receiver's
  tcp_recv_buf may be too small to hold a single chunk frame from
  the sender. Messages up to the receiver's tcp_recv_buf size work;
  larger messages time out. Fix: ensure tcp_recv_buf can hold the
  largest expected incoming frame, or support partial frame
  accumulation across poll cycles.)
- What happens when a malformed discovery packet arrives with no
  null-terminated name? (Prior to fix: strlen reads past the
  received buffer — memory safety bug. After fix: memchr validates
  null terminator within received bytes; packet is silently
  discarded if unterminated. See R25.)
- What happens when a POSIX TCP send gets EAGAIN after partial
  data is written? (Prior to fix: returns PT_OK with remaining
  bytes silently dropped, corrupting TCP framing for all subsequent
  messages on that stream. After fix: returns PT_ERR_SEND_FAILED
  so the caller knows the frame was not sent. See R26.)
- What happens when an ASR/notifier fires while the main loop is
  clearing a flag in the Classic Mac backends? (The non-atomic
  read-modify-write `flags &= ~FLAG` can lose flags set by the
  interrupt between the read and write. Fix: atomic flag clearing
  using interrupt disable (68k) or OTAtomicClearBit (PPC). See R27.)
- What happens when chunks from different message types arrive
  interleaved during reassembly? (Prior to fix: mismatched chunks
  are silently placed at wrong offsets, corrupting the reassembly
  buffer. After fix: chunk msg_type must match reassembly_type or
  the chunk is rejected. See R28.)
- What happens when FreeMem() returns less than the minimum
  viable allocation (not enough for even 1 peer)? (PT_Init
  returns PT_ERR_INIT. The minimum viable allocation is
  approximately 8 KB: 1 peer slot + minimal buffers.)
- What happens when discovery/TCP/UDP ports are already in use
  at init time? (The platform backend's init function returns an
  error, and PT_Init returns PT_ERR_INIT.)
- What happens when a chat message exceeds the TextEdit 32K
  limit or the display area in the demo app? (The messages
  TextEdit is trimmed from the top when it approaches 30K bytes,
  preserving recent history. Individual messages are capped at
  1024 bytes by chat.h.)
- **Chat app peer list must show discovered peers**: The demo chat app peer list must display both discovered and connected peers. Discovered peers should be visually distinguished from connected ones (e.g., state indicator). The user must be able to initiate a connection to a discovered peer from the list.
- **Demo apps must fit compact Mac screens**: Demo app dialogs must fit on 512x342 screens (Mac SE, Mac Plus, Mac Classic). Either adapt dialog dimensions to screen size at runtime, or use a fixed layout that fits all targets.
- What happens when PT_StartDiscovery is called but UDP fast messages
  don't arrive on MacTCP? (Prior to fix: udp_listen was only called
  for the discovery port, not the message port. MacTCP requires a
  pre-posted UDPRead per port. Without it, read_pending=0 and
  mactcp_poll never processes fast messages. After fix: udp_listen
  is called for both ports in PT_StartDiscovery. See R34.)
- What happens when test apps always run Mac in the passive/receiver role?
  (The role assignment `name[0] <= 'M'` combined with Mac default name
  "Unnamed" means Mac never exercises the send or connect-initiate paths.
  Mac UDP send, variable-size TCP send, and PT_Connect are untested on
  hardware. Fix: run Mac-as-sender tests by passing `--name Alice` via
  LaunchAPPL, or make tests bidirectional. See R37.)
- What happens when test_fast runs on 68k MacTCP (Mac SE)? (The R34
  fix posts two concurrent async UDPRead parameter blocks. This works
  on PPC MacTCP and on 68k MacTCP after a clean reboot. An initial
  crash was caused by residual corrupted MacTCP driver state from a
  previous failed run, not a fundamental 68k limitation. After clean
  reboot: 59/60 received, PASS. If test_fast crashes on Mac SE, reboot
  and retry. See R38.)
- What happens when the MacTCP backend shuts down while async UDPRead
  operations are pending? (Prior to fix: UDPRelease frees the stream
  but DisposePtr runs before the driver fully cancels the pending read,
  causing write-after-free that corrupts MacTCP driver state until
  reboot. After fix: spin-wait for read_pb.ioResult != inProgress
  before disposing buffer. See R39.)
- What happens when 68k MacTCP sends a burst of UDP datagrams? (Crash
  on Mac SE when test_fast sends 12 UDPWrite calls in a tight loop.
  MacTCP docs confirm concurrent UDPWrite+UDPRead is safe, so the crash
  is from the burst size, not the concurrency. PPC MacTCP handles the
  same burst. Fix: throttle to 1-2 sends per poll cycle on 68k. See R40.)
- What happens when PT_Send is called on a 68k Mac with a small stack?
  (Prior to fix: the 1403-byte stack buffer in PT_Send's UDP path
  overflows the 68k application stack (~8KB), crashing on the first
  send. After fix: the buffer lives in PT_Context_Internal, allocated
  at init time. No stack pressure from PT_Send. See R48.)
- What happens when both peers call PT_Connect simultaneously on
  discovery? (Both TCP connections may succeed, creating duplicate
  connections to the same peer. The SDK accepts both — it does not
  detect or reject duplicate connections from the same IP. Test apps
  must guard against this by checking connection state before calling
  PT_Connect in on_discovered. See R49.)

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: SDK MUST allow initialization with a peer name
  (max 31 characters) and produce an opaque context handle.
- **FR-002**: SDK MUST discover peers on the local network
  automatically when discovery is started, notifying the
  developer via callback as peers appear and disappear.
- **FR-003**: SDK MUST support connecting to a discovered peer,
  with the remote side auto-accepting the connection.
- **FR-004**: SDK MUST support two transport modes per message
  type: fast (low latency, may drop) and reliable (guaranteed,
  ordered).
- **FR-005**: SDK MUST deliver reliable messages in order and
  complete, handling segmentation and reassembly for payloads
  exceeding platform buffer sizes.
- **FR-006**: SDK MUST support sending messages to a single
  peer or broadcasting to all connected peers.
- **FR-007**: SDK MUST provide distinct disconnect reasons
  (quit, timeout, error) via callback.
- **FR-008**: SDK MUST send a goodbye notification on clean
  shutdown.
- **FR-009**: SDK MUST allocate all required memory at
  initialization and perform zero allocation during
  send/receive operations.
- **FR-010**: SDK MUST adapt buffer sizes and peer slot counts
  to available host memory at initialization without developer
  configuration.
- **FR-011**: SDK MUST expose an event-driven polling function
  that drives all I/O without requiring threads.
- **FR-012**: SDK MUST provide peer query functions: count,
  access by index, name, and state (discovered, connected,
  disconnected).
- **FR-013**: SDK MUST return a status code from every function
  whose failure the caller must handle. Setup functions
  (`PT_RegisterMessage`, callback registration) and cleanup
  functions (`PT_Shutdown`) are void — registration silently
  ignores type 255, and shutdown is best-effort.
- **FR-014**: SDK MUST reserve message type 255 for internal
  goodbye messages; application types use 0-254.
- **FR-015**: SDK MUST support three platform backends (POSIX,
  MacTCP, Open Transport) with identical behavior at the
  developer-facing level.
- **FR-016**: SDK MUST remove a peer from the discovered list
  after 10 seconds without a discovery broadcast.
- **FR-017**: SDK MUST discard incomplete chunked messages
  after a 5-second reassembly timeout.
- **FR-018**: SDK MUST reject fast messages that exceed the
  single-datagram size limit with an appropriate error.
- **FR-019**: SDK MUST use clog as its logging dependency.
- **FR-020**: SDK MUST compile as C89/C90 for all shared code;
  POSIX-only code may use C11.

### Key Entities

- **Context**: The top-level SDK handle. Owns all state
  including peer list, registered message types, callbacks,
  and platform-specific resources. One per application
  instance.
- **Peer**: Represents a discovered or connected network
  participant. Has a name (display string), a state
  (discovered, connected, or disconnected), and
  platform-specific addressing information.
- **Message Type**: A developer-defined identifier (0-254)
  paired with a transport mode (fast or reliable). Determines
  how the SDK delivers payloads of that type.
- **Platform Backend**: An abstraction layer that implements
  network operations for a specific platform. One active per
  context, selected at build time.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A developer can integrate the SDK and exchange
  messages between two peers on the same LAN within 30 minutes
  of reading the documentation.
- **SC-002**: The complete public interface fits on a single
  screen (20 functions or fewer).
- **SC-003**: Test apps on Linux can discover and exchange
  messages with test apps on a Mac SE (MacTCP) and a
  Performa 6400 (Open Transport).
- **SC-004**: The SDK operates with zero memory allocation
  after initialization on all platforms, verified by test.
- **SC-005**: Reliable messages up to 64 KB are delivered
  complete and in order on platforms with sufficient memory.
  The maximum message size is limited by the receiver's
  reassembly buffer, which scales with available RAM (4 KB on
  a 4 MB Mac, 64 KB on 8+ MB systems). Messages exceeding the
  receiver's buffer are discarded and the error callback fires.
- **SC-006**: Fast messages at 30-60 Hz are delivered with
  under 16ms average latency on a local LAN, suitable for
  real-time game position updates.
- **SC-007**: The total codebase remains under 15,000 lines
  across all platforms.
- **SC-008**: On a 4 MB Classic Mac, the SDK supports at least
  8 simultaneous peer connections.
- **SC-009**: All three test app patterns (frequent small
  sends, request/response, variable-size text) pass on every
  supported platform.
- **SC-010**: The codebase is readable enough that a C
  programmer can understand the core flow without external
  documentation.

### Assumptions

- The LAN supports UDP broadcast (255.255.255.255).
- All peers are on the same network segment (no routing/NAT).
- The developer calls `PT_Poll` frequently enough to drive I/O
  (at least once per frame or event loop iteration).
- clog is available as a separately built static library.
- Discovery port (7353), TCP port (7354), and UDP port (7355)
  are not in use by other applications.

## Feedback Log

- **2026-03-01**: Cross-platform chunk reassembly uses wrong buffer size for offsets → R13, T036
- **2026-03-01**: pt_memory.c makes 2 allocations instead of 1 contiguous block → R14, T037
- **2026-03-01**: OT flow_off field not volatile — notifier writes invisible to main loop → T038
- **2026-03-01**: Three OTNotifyUPP handles leaked — never stored or disposed → T039
- **2026-03-01**: MacTCP platform buffers not counted in memory budget — doubles actual consumption → R15, T040
- **2026-03-01**: Test apps crash on Classic Mac — v1 patterns are the working reference → R16, T041
- **2026-03-01**: Both Macs crash again — missing MaxApplZone()/MoreMasters() before allocations → R17, T042-T044
- **2026-03-03**: MaxApplZone was in pt_memory_allocate() but PT_Init calls NewPtrClear BEFORE that — moved to PT_Init. test_chat.c uses malloc (Principle V violation). Test apps invisible on Mac (printf only). → R18, T045-T048
- **2026-03-04**: Chunked TCP messages >2KB fail on Classic Mac — tcp_recv_buf (2048) too small for POSIX chunk frames (up to 4096) → R21, T054
- **2026-03-04**: All 4 tests PASS on P6400 (OT) and P6200 (MacTCP) with POSIX peer → T049-T052 complete
- **2026-03-07**: test_chat sender phase 2 exit + summary clarity → R41, T131-T132
- **2026-03-07**: test_reliable false tolerance + test_lifecycle Phase 3 cleanup → R41, T133-T134
- **2026-03-04**: POSIX clog timestamps overflow to near-UINT64_MAX after first few seconds → R22 (clog bug, not peertalk)
- **2026-03-04**: PT_Log filename collision — all tests overwrite same file → R20, T056
- **2026-03-04**: test_chat disconnect reason ERROR instead of QUIT on Mac → R23, T057
- **2026-03-04**: Machine identification via Gestalt for log identification → R24, T058
- **2026-03-05**: Discovery strlen buffer overflow on unterminated network data → R25, T064
- **2026-03-05**: POSIX TCP partial send returns PT_OK, corrupts framing → R26, T065
- **2026-03-05**: Non-atomic flag clearing race in Classic Mac backends → R27, T066
- **2026-03-05**: Reassembly accepts chunks without type check → R28, T067
- **2026-03-05**: OT async unbind/rebind race on endpoint reset → R29, T068
- **2026-03-05**: POSIX accepted fd leaks when no peer slot available → T069
- **2026-03-05**: UPP/notifier/configuration null checks missing in Mac backends → T070
- **2026-03-05**: test_chat integrity check only validates first 100 bytes → T073
- **2026-03-05**: test_fast pass criteria too weak (1/300 = PASS) → T074
- **2026-03-05**: test_reliable never validates payload content → T075
- **2026-03-05**: PT_Broadcast never tested (US2 scenario 3) → T076
- **2026-03-05**: Chat application — reuse csend GUI with PeerTalk SDK → R30, T080-T087
- **2026-03-06**: Chat app peer list empty on hardware — only shows connected peers, no connect mechanism → R33, T112-T114
- **2026-03-06**: Chat app dialog too large for Mac SE 512x342 screen → R33, T115
- **2026-03-06**: MacTCP UDP fast messages never received — udp_listen only called for discovery port, not message port → R34, T116-T117
- **2026-03-06**: Mac SE hard freeze running test_fast after R34 UDP fix — suspended 68k hardware testing → R35, T117 updated
- **2026-03-06**: Mac always passive in 3/4 tests — UDP send, variable-size TCP send, and PT_Connect untested on Mac hardware → R37, T121-T123
- **2026-03-06**: test_fast crash on Mac SE was transient (dirty MacTCP state) — all 4 tests PASS after clean reboot, Mac SE re-included → R38, T124
- **2026-03-06**: MacTCP UDP shutdown bug — DisposePtr before pending UDPRead completes, plus buffer oversizing and dead ASR flag → R39, T125-T127
- **2026-03-06**: 68k MacTCP crashes on UDP send burst (12 writes in tight loop) — not a concurrency issue per MacTCP docs → R40, T128-T130
- **2026-03-07**: MacTCP send-side chunking limit — Mac SE can't send >1024 byte messages due to async single-send-per-poll constraint → R42
- **2026-03-07**: Discovery doesn't re-fire on_discovered for disconnected peers — causes 10+ second reconnection delay on all platforms → R43, T136
- **2026-03-07**: Final hardware verification complete (T129) — P6200 all 4 PASS, Mac SE all 4 PASS, P6400 test_lifecycle PASS (OTConnect -3158 kOTLookErr on Phase 3 first attempt, succeeded on retry). 136/136 tasks complete.
- **2026-03-07**: Code review against Macintosh programming books — OTSnd partial send not handled (framing risk), missing OTSndOrderlyDisconnect in T_ORDREL handler, SIZE resource comment errors, vsprintf unbounded, SetPort not saved/restored → R44-R46, T137-T148
- **2026-03-07**: 68k OT build target for Performa 630 (68040) — Retro68 m68k toolchain has OT import libs under different names from PPC. Enables 5th build: build-68k-ot/ → R46, T142-T143
- **2026-03-07**: v1.0 release prep — remove apps/ (csend-pt moves to own repo), add README.md, gitignore books/ and stale build dirs → T144-T148
- **2026-03-07**: Name-based test role assignment broken on hardware — LaunchAPPL can't pass args, both sides default to RESPONDER, test solo-times-out → R47, T149-T151
- **2026-03-07**: PT_Send 1403-byte stack buffer crashes 68k Mac SE on first UDP send — stack overflow on 68000 with ~8KB stack → R48, T152-T154
- **2026-03-07**: test_fast simultaneous-connect race on P6400 OT — both peers PT_Connect on discovery, dual TCP connections, intermittent FAIL → R49, T155
