# Feature Specification: Fix test_reliable Turn Deadlock

**Feature Branch**: `004-fix-test-reliable`
**Created**: 2026-03-07
**Status**: Draft
**Input**: Hardware testing revealed test_reliable deadlocks on all platforms — both sides wait for opponent's first move

## User Scenarios & Testing *(mandatory)*

### User Story 1 - test_reliable Completes Successfully (Priority: P1)

When test_reliable runs between a POSIX peer and a Classic Mac peer (or two POSIX peers), one side deterministically goes first and the other goes second. The 10-move exchange completes and both sides report PASS.

**Why this priority**: test_reliable is one of four test apps that prove the SDK. It currently fails on every platform, undermining confidence in the reliable messaging path.

**Independent Test**: Run test_reliable on POSIX vs any Classic Mac. Both sides should exchange 10 moves and report PASS.

**Acceptance Scenarios**:

1. **Given** two peers both named "Unnamed" (default), **When** they connect, **Then** one side deterministically goes first based on IP address comparison
2. **Given** a POSIX peer and a Classic Mac peer, **When** test_reliable runs, **Then** 10 moves are exchanged and both sides report PASS
3. **Given** two peers on the same machine (loopback), **When** test_reliable runs, **Then** one side still goes first (fallback to any deterministic tiebreaker)

### Edge Cases

- Both peers have the same IP (loopback) — should still pick a first mover
- Connection established before peer name is exchanged — IP is available immediately via PT_PeerAddress

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: test_reliable MUST determine first-mover using connection initiation, not peer name
- **FR-002**: The side that initiates the connection (calls PT_Connect from on_discovered) MUST go first — this is deterministic because spec 003's IP tiebreaker ensures only one side initiates
- **FR-003**: First-mover determination MUST happen at connect time (on_discovered sets initiator flag, on_connected reads it), not at init time
- **FR-004**: The old name-based tiebreaker (`name[0] <= 'M'`) MUST be removed
- **FR-005**: test_reliable MUST pass on all three platforms (POSIX, MacTCP, OT) when tested against a POSIX peer

### Assumptions

- PT_PeerAddress is always available in on_connected (confirmed working in spec 002)
- IP string comparison works as a tiebreaker since peers are always on different IPs on a LAN

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: test_reliable reports PASS on all three hardware platforms when run against a POSIX peer
- **SC-002**: 10 moves sent, 10 received, order valid, payload valid on both sides
- **SC-003**: Broadcast phase (GAME_OVER) completes successfully
