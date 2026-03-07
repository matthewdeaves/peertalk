# Feature Specification: SDK Stability Improvements

**Feature Branch**: `003-sdk-stability`
**Created**: 2026-03-07
**Status**: Draft
**Input**: Real-world testing feedback from csend chat app on Classic Mac hardware (Performa 6400 OT, Performa 6200 MacTCP, Mac SE MacTCP, POSIX Linux peer)

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Stable Long-Running Connections (Priority: P1)

A chat app developer runs their app on two Classic Macs on a LAN. After peers connect, the connection stays up indefinitely as long as both apps are running. No spurious disconnects or reconnect cycles.

**Why this priority**: Without stable connections, the SDK is unusable for any real application. The current 30-second TCP timeout causes constant disconnect/reconnect cycling on slow hardware.

**Independent Test**: Run test_lifecycle on two machines, verify connection holds for at least 2 minutes without timeout disconnects. Verify via clog that no `TCP timeout` messages appear during normal connected idle.

**Acceptance Scenarios**:

1. **Given** two peers connected on a LAN, **When** neither sends data for 45 seconds, **Then** the connection remains established (no timeout disconnect)
2. **Given** a peer on slow Classic Mac hardware (68000 CPU), **When** TCP keepalive probes are sent, **Then** the peer responds within the timeout window and stays connected
3. **Given** a peer that genuinely goes offline, **When** 60 seconds of inactivity pass, **Then** the SDK detects the loss and fires a disconnect callback with PT_TIMEOUT reason

---

### User Story 2 - No Duplicate Connections (Priority: P1)

When two peers discover each other simultaneously and both attempt to connect, only one TCP connection is established. The app receives exactly one on_connected callback per peer.

**Why this priority**: Duplicate connections cause confusion in apps (doubled messages, inconsistent state) and waste resources on memory-constrained Classic Macs.

**Independent Test**: Run two peers that both auto-connect on discovery. Verify only one TCP connection exists between them. Count on_connected callbacks — should be exactly 1 per peer pair.

**Acceptance Scenarios**:

1. **Given** two peers with auto-connect on discovery, **When** both discover each other at roughly the same time, **Then** only one TCP connection is established between them
2. **Given** a pending outgoing connection to a peer, **When** an incoming connection arrives from the same peer, **Then** the tiebreaker (lower IP initiates) resolves which connection survives
3. **Given** a peer already in CONNECTED state, **When** a second incoming connection arrives from the same IP, **Then** the duplicate is rejected

---

### User Story 3 - Meaningful Error Context (Priority: P2)

When a connection attempt fails or a send error occurs, the app's error callback receives the relevant peer pointer so it can display which peer had the problem.

**Why this priority**: Without peer context, error callbacks are nearly useless for multi-peer apps. The app cannot show the user which peer failed.

**Independent Test**: Trigger a connection failure (e.g., connect to a peer that goes offline during handshake). Verify the error callback receives a non-NULL peer pointer identifying the failing peer.

**Acceptance Scenarios**:

1. **Given** a registered error callback, **When** a connection attempt to a specific peer fails, **Then** the error callback receives a pointer to that peer
2. **Given** a registered error callback, **When** a non-peer error occurs (e.g., init failure), **Then** the error callback receives a NULL peer pointer
3. **Given** multiple connected peers, **When** a send to one peer fails, **Then** the error identifies which peer experienced the failure

---

### User Story 4 - Clean Shutdown Without Stale Callbacks (Priority: P2)

When an app calls PT_Shutdown(), no callbacks fire after the call begins. The app can safely tear down its UI and state before calling PT_Shutdown() without guarding against late callbacks.

**Why this priority**: Apps currently must add defensive guards in every callback to handle the shutdown race. This is error-prone and should be handled by the SDK.

**Independent Test**: Register all callbacks, connect to a peer, call PT_Shutdown(). Verify no disconnect callbacks fire during or after PT_Shutdown() execution.

**Acceptance Scenarios**:

1. **Given** connected peers with all callbacks registered, **When** PT_Shutdown() is called, **Then** no on_disconnected callbacks fire for the peers being torn down
2. **Given** an app that has torn down its UI, **When** PT_Shutdown() executes, **Then** no callback dereferences freed app state

---

### Edge Cases

- What happens when both peers have the same IP address? (localhost/loopback testing — tiebreaker should fall back to allowing the first connection attempt)
- What happens when a connection timeout fires at the exact moment a connection succeeds? (timeout check should verify state before disconnecting)
- What happens when PT_Shutdown is called from inside a callback? (must not deadlock or double-free)
- What happens when a peer reconnects during the discovery timeout window after a previous disconnect?

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: SDK MUST use a TCP inactivity timeout of 60 seconds (up from 30)
- **FR-002**: SDK MUST use a TCP connect timeout of 15 seconds (up from 10)
- **FR-003**: SDK MUST use a discovery timeout of 15 seconds (up from 10)
- **FR-004**: MacTCP backend MUST set ulpTimeoutValue to 60 seconds for active connections
- **FR-005**: MacTCP backend MUST set commandTimeoutValue to 15 seconds for outgoing connections
- **FR-006**: SDK MUST prevent duplicate TCP connections to the same peer using an IP-based tiebreaker (lower IP initiates)
- **FR-007**: SDK MUST reject incoming TCP connections from a peer that is already in CONNECTED state
- **FR-008**: SDK MUST close the outgoing connection attempt when an incoming connection from the same peer wins the tiebreaker
- **FR-009**: Error callback signature MUST include an optional PT_Peer pointer as the first parameter
- **FR-010**: Error callback MUST pass NULL for the peer parameter when the error is not peer-specific
- **FR-011**: Error callback MUST pass the relevant peer pointer for connection failures, send failures, and peer-specific errors
- **FR-012**: PT_Shutdown() MUST clear all callback pointers before closing any connections
- **FR-013**: No callbacks MUST fire after PT_Shutdown() begins executing
- **FR-014**: All existing test apps MUST be updated to match the new error callback signature

### Assumptions

- The IP-based tiebreaker works for all deployment scenarios (no NAT between peers on the same LAN)
- Loopback connections (same IP) are not a primary use case but should not crash
- The error callback signature change is an acceptable breaking API change since the SDK is pre-1.0

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Two peers on Classic Mac hardware maintain a connection for 5+ minutes without spurious disconnect/reconnect cycles
- **SC-002**: Simultaneous discovery between two peers results in exactly one TCP connection (zero duplicates across 10 test runs)
- **SC-003**: Connection failure errors identify the specific peer in 100% of cases where a peer is known
- **SC-004**: PT_Shutdown() completes without firing any callbacks on all three platforms (POSIX, MacTCP, OT)
- **SC-005**: All four test apps (test_init_only, test_lifecycle, test_fast, test_reliable) build and pass on all platforms after the changes
