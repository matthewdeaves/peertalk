# Feature Specification: Test Coverage Gaps

**Feature Branch**: `008-test-coverage`
**Created**: 2026-03-26
**Status**: Draft
**Input**: Four test coverage gaps identified during code review: multi-peer, error paths, stop/start discovery, set-name, peer-lost validation.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Multi-Peer Test (Priority: P1)

A developer runs a new test app (test_multi) across 4 machines: POSIX (Linux), Mac SE (68k MacTCP), Performa 6200 (PPC MacTCP), and Performa 6400 (PPC OT). Each peer discovers the other 3, connects to all of them, broadcasts a message, receives broadcasts from all 3, then disconnects cleanly. This is the first test to exercise multiple simultaneous TCP connections, the peer table with >1 entry, and PT_Broadcast to multiple connected peers.

**Why this priority**: This is the only untested dimension of the SDK that could hide real bugs. The peer table, tiebreaker logic, and broadcast loop have never been exercised with more than 2 peers.

**Independent Test**: Run test_multi on all 4 machines simultaneously. Verify each peer reports PASS with 3 connections, 3 broadcasts received, 3 clean disconnects.

**Acceptance Scenarios**:

1. **Given** 4 peers on the LAN, **When** test_multi runs on each, **Then** each peer discovers 3 others, connects to all 3, and reports 3 connections
2. **Given** 4 connected peers, **When** each broadcasts a message, **Then** each peer receives broadcasts from the other 3
3. **Given** 4 connected peers, **When** each disconnects, **Then** all disconnects are clean (reason: QUIT, not ERROR or TIMEOUT)
4. **Given** the Mac SE (8MHz, 4MB RAM), **When** running test_multi, **Then** it completes within the solo timeout despite being the slowest peer

---

### User Story 2 - Error Path Tests (Priority: P2)

A developer runs existing test_init_only (or a new error-path section in an existing test) to verify that the SDK returns correct error codes for invalid API usage. This validates PT_Send on NULL/disconnected peers, PT_Connect before discovery, and PT_Broadcast with no connections.

**Why this priority**: Quick win — ~30 lines of code, runs on all platforms, no networking needed. Validates the SDK's defensive coding.

**Independent Test**: Run on POSIX. Verify all expected error codes are returned.

**Acceptance Scenarios**:

1. **Given** an initialized context with no peers, **When** PT_Broadcast is called, **Then** it returns PT_OK (no-op)
2. **Given** an initialized context, **When** PT_Send is called with NULL peer, **Then** it returns PT_ERR_INVALID_ARG
3. **Given** a discovered but not connected peer, **When** PT_Send is called, **Then** it returns PT_ERR_NOT_CONNECTED

---

### User Story 3 - Lifecycle Additions: StopDiscovery, SetName, PeerLost (Priority: P3)

A developer runs an enhanced test_lifecycle that exercises PT_StopDiscovery, PT_SetName, and validates that PT_OnPeerLost fires correctly. These are minor API coverage additions to the existing test.

**Why this priority**: Lowest risk — these are simple API functions unlikely to be broken, but testing them improves confidence and catches regressions.

**Independent Test**: Run enhanced test_lifecycle between POSIX and a Classic Mac. Verify stop/start discovery works, name change propagates, and peer-lost fires after discovery timeout.

**Acceptance Scenarios**:

1. **Given** a connected peer, **When** PT_StopDiscovery is called and 5 seconds pass, **Then** no new peers are discovered during that window
2. **Given** a stopped discovery, **When** PT_StartDiscovery is called again, **Then** the remote peer is re-discovered and can reconnect
3. **Given** an initialized context, **When** PT_SetName is called with a new name, **Then** remote peers see the new name via PT_PeerName after the next discovery broadcast
4. **Given** a disconnected peer, **When** the discovery timeout (15s) elapses without re-discovery, **Then** PT_OnPeerLost fires for that peer

---

### Edge Cases

- What if the Mac SE takes >30 seconds to establish all 3 connections? (Set a generous discovery/connect window of 45 seconds before starting message exchange)
- What if a tiebreaker cancels a connection that another peer has already accepted? (The tiebreaker logic is per-pair and handles this — one side aborts, the other re-listens)
- What if PT_Send is called with len=0? (Should succeed as a no-op or return an error — document whichever the SDK does)
- What if PT_SetName is called with a name longer than 31 characters? (Should truncate to PT_MAX_NAME_LEN)
- What if on_peer_lost fires while the peer is still connected? (Should not happen — only fires for discovered-but-not-connected peers after timeout)

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: test_multi MUST support 2-4 peers, auto-connecting to all discovered peers
- **FR-002**: test_multi MUST use PT_Broadcast to send a message to all connected peers
- **FR-003**: test_multi MUST verify receipt of broadcasts from all connected peers
- **FR-004**: test_multi MUST report PASS only when all expected connections, broadcasts, and disconnects complete
- **FR-005**: test_multi MUST work on all platforms (POSIX, 68k MacTCP, PPC MacTCP, PPC OT)
- **FR-006**: Error path tests MUST verify PT_Send with NULL peer returns PT_ERR_INVALID_ARG
- **FR-007**: Error path tests MUST verify PT_Send on unconnected peer returns PT_ERR_NOT_CONNECTED
- **FR-008**: Error path tests MUST verify PT_Broadcast with no connections returns PT_OK
- **FR-009**: Enhanced test_lifecycle MUST call PT_StopDiscovery and verify no discoveries during stop
- **FR-010**: Enhanced test_lifecycle MUST call PT_SetName and verify remote peer sees the new name
- **FR-011**: Enhanced test_lifecycle MUST validate that PT_OnPeerLost fires after discovery timeout
- **FR-012**: All new tests MUST compile as C11 on POSIX and Classic Mac (test apps, not SDK)
- **FR-013**: All new tests MUST use the existing test_common.h framework

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: test_multi passes on 4 machines simultaneously (POSIX + Mac SE + Performa 6200 + Performa 6400) with 3 connections each
- **SC-002**: Error path tests pass on POSIX and all Classic Mac targets
- **SC-003**: Enhanced test_lifecycle passes on Mac SE and Performa 6400 with no regressions to existing PASS criteria
- **SC-004**: Total test app LOC remains reasonable (no single test app exceeds 400 lines)

## Assumptions

- The Mac SE can handle 3 simultaneous TCP connections within its memory budget (confirmed: ~18KB per peer, 3 peers = ~54KB, well within 4MB)
- Discovery broadcasts from 4 peers (every 2 seconds each) produce trivial network load
- The tiebreaker resolves all 6 peer-pairs independently within a few seconds
- PT_OnPeerLost test requires waiting 15 seconds for the discovery timeout, making test_lifecycle ~30 seconds longer
