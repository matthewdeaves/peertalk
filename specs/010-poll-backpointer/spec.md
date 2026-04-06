# Feature Specification: Poll Back-Pointer Optimisation

**Feature Branch**: `010-poll-backpointer`  
**Created**: 2026-04-06  
**Status**: Draft  
**Input**: User description: "Add back-pointers from platform stream/endpoint slots to their owning PT_Peer_Internal, eliminating linear peer scans in the poll hot path."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Chat App Maintains Responsive Polling Under Load (Priority: P1)

A Chat application connects to multiple peers over TCP. Each poll cycle must process incoming data, detect disconnections, and handle connection completions for every connected peer. Currently, each of these events requires a linear scan of the peer array to find the owning peer. With back-pointers, the poll loop resolves the owning peer in constant time, reducing per-poll CPU cost proportionally to the number of connected peers.

**Why this priority**: Chat is the most TCP-intensive target app — every message received and every connection event triggers peer lookups. On a 68000 at 8 MHz, eliminating repeated linear scans in the hot path directly improves message throughput and responsiveness.

**Independent Test**: Connect two POSIX peers and one Classic Mac peer running the Chat test app. Send a sustained stream of reliable messages. Verify all messages are received correctly and no regressions occur. Optionally measure poll cycle time before/after on real hardware.

**Acceptance Scenarios**:

1. **Given** a MacTCP peer with 2 active TCP connections, **When** both peers send data simultaneously, **Then** the poll loop processes both data events without calling find_peer_for_stream, using back-pointers instead.
2. **Given** an OT peer with 3 active TCP connections, **When** one peer disconnects while others send data, **Then** the disconnect and data events resolve their owning peers via back-pointers and all callbacks fire correctly.

---

### User Story 2 - Chess App Handles Connection Lifecycle Correctly (Priority: P2)

A Chess application discovers a peer, connects, exchanges moves over TCP, and eventually disconnects. The back-pointer must be set when the connection is established (both incoming and outgoing paths), remain valid for the connection lifetime, and be cleared on disconnect so the stream/endpoint slot can be reused.

**Why this priority**: Chess exercises the full connection lifecycle including the tiebreaker logic for simultaneous connections. The back-pointer must survive connection establishment, active use, and clean teardown without stale pointer bugs.

**Independent Test**: Run the existing test_lifecycle app on each Classic Mac platform. Verify discovery, connection, message exchange, and graceful disconnect all succeed with no regressions.

**Acceptance Scenarios**:

1. **Given** a free stream slot with a NULL back-pointer, **When** an outgoing TCP connection succeeds, **Then** the back-pointer is set to the connecting peer before the on_connected callback fires.
2. **Given** a connected stream with a valid back-pointer, **When** the peer disconnects gracefully (goodbye frame), **Then** the back-pointer is cleared to NULL and the stream slot returns to the free pool.
3. **Given** two peers attempting simultaneous connections (tiebreaker scenario), **When** one connection is cancelled in favour of the other, **Then** the cancelled stream's back-pointer is cleared and the accepted stream's back-pointer is set correctly.

---

### User Story 3 - Bomberman App Maintains Correct State During Fast Reconnects (Priority: P3)

A Bomberman game uses UDP for fast game-state updates but relies on TCP for reliable messages (game start, game over). Peers may disconnect and reconnect between rounds. The back-pointer must be correctly managed across disconnect/reconnect cycles without leaking stale pointers.

**Why this priority**: Bomberman exercises rapid disconnect/reconnect patterns. A stale back-pointer pointing to a freed or reassigned peer slot would cause data corruption or crashes.

**Independent Test**: Run test_fast on POSIX and Classic Mac. Verify UDP fast messages and TCP reliable messages both work correctly through connect/disconnect cycles.

**Acceptance Scenarios**:

1. **Given** a peer that disconnects and then reconnects using a different stream slot, **When** the new connection is established, **Then** the old stream's back-pointer is NULL and the new stream's back-pointer points to the correct peer.

---

### Edge Cases

- What happens when an incoming connection is rejected because the peer table is full? The stream/endpoint back-pointer must remain NULL (never set) or be cleared after rejection.
- What happens when a MacTCP listener stream is aborted to reclaim it for an outgoing connection? The back-pointer (if any) must be cleared during abort.
- What happens when the tiebreaker logic cancels an outgoing connection in favour of an incoming one on the same peer? The old stream's back-pointer must be cleared and the new stream's back-pointer set, all within the same pt_handle_incoming_connection call.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: MacTCP TCPStreamSlot MUST contain a pointer field that references the owning PT_Peer_Internal, or NULL if the stream is not assigned to any peer.
- **FR-002**: OT OTEndpointSlot MUST contain a pointer field that references the owning PT_Peer_Internal, or NULL if the endpoint is not assigned to any peer.
- **FR-003**: The back-pointer MUST be set to the owning peer when a stream/endpoint is assigned during connection establishment (both incoming and outgoing paths).
- **FR-004**: The back-pointer MUST be cleared to NULL when a stream/endpoint is released during disconnect, abort, or connection rejection.
- **FR-005**: The poll loop MUST use the back-pointer for peer resolution instead of linear scan functions (find_peer_for_stream, find_peer_for_ep).
- **FR-006**: The existing find_peer_for_stream and find_peer_for_ep functions MUST be removed after all call sites are converted to use back-pointers.
- **FR-007**: All existing tests (test_init_only, test_lifecycle, test_fast, test_reliable) MUST continue to pass on all platforms (POSIX, MacTCP 68k, OT PPC) with no behavioural changes.
- **FR-008**: The POSIX backend MUST NOT be modified — it does not use reverse lookup functions.

### Key Entities

- **TCPStreamSlot** (MacTCP): Per-stream state struct, gains an owner pointer field referencing the peer that currently uses this stream.
- **OTEndpointSlot** (OT): Per-endpoint state struct, gains an owner pointer field referencing the peer that currently uses this endpoint.
- **PT_Peer_Internal**: Existing peer struct. Referenced by back-pointers but not modified.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All four test apps pass on POSIX with identical behaviour to the current codebase.
- **SC-002**: All four test apps pass on at least one Classic Mac platform (MacTCP or OT) with identical behaviour.
- **SC-003**: The find_peer_for_stream function is removed from pt_mactcp.c and the find_peer_for_ep function is removed from pt_ot.c.
- **SC-004**: No new memory allocations are introduced — the back-pointer field is part of the existing struct, allocated at init time.
- **SC-005**: Total SDK line count remains under the 15,000-line project limit (currently ~4,400 lines; expect net reduction from removing scan functions).

## Assumptions

- The back-pointer field adds sizeof(void*) (4 bytes on 68k/PPC) per stream/endpoint slot. With max 32 slots, this is 128 bytes total — negligible impact on memory budget.
- The POSIX backend's poll loop iterates peers (not sockets) and checks fd membership directly, so it does not benefit from and does not need back-pointers.
- Stream/endpoint slots are never shared between peers — each slot has at most one owning peer at any time.
