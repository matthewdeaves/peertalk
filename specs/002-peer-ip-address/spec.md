# Feature Specification: Peer IP Address API

**Feature Branch**: `002-peer-ip-address`
**Created**: 2026-03-07
**Status**: Draft
**Input**: User description: "Add a method to get the IP address for a peer, for GUI display like 'username@ip'"

## User Scenarios & Testing

### User Story 1 - Get peer IP as formatted string (Priority: P1)

The csend-pt chat application (and any other PeerTalk app with a GUI) needs to display peers in a list showing "username@ip". The developer calls `PT_PeerAddress(peer)` and gets back a dotted-quad string like "10.188.1.213" that can be concatenated with the peer name for display.

**Why this priority**: This is the entire feature. The old csend GUI showed "username@ip" in the peer list. Without this, the developer has no way to get the IP from an opaque `PT_Peer*`.

**Independent Test**: Call `PT_PeerAddress()` on a discovered peer and verify the returned string is a valid dotted-quad IP address matching the peer's actual address.

**Acceptance Scenarios**:

1. **Given** a discovered peer at 10.188.1.213, **When** calling `PT_PeerAddress(peer)`, **Then** returns "10.188.1.213"
2. **Given** a NULL peer pointer, **When** calling `PT_PeerAddress(NULL)`, **Then** returns "" (empty string, not NULL)
3. **Given** a connected peer, **When** calling `PT_PeerAddress(peer)`, **Then** returns the same IP as when the peer was discovered

---

### Edge Cases

- NULL peer pointer: returns empty string (same pattern as PT_PeerName)
- Peer in any state (discovered, connected, disconnected): IP is always available once set
- IP 0.0.0.0: should not occur in practice (discovery filters own IP and requires valid source)

## Requirements

### Functional Requirements

- **FR-001**: SDK MUST provide `PT_PeerAddress(PT_Peer *peer)` returning `const char *` with dotted-quad IP
- **FR-002**: Returned string MUST be stored in the peer's internal struct (not a static buffer) so multiple peers can be queried without overwriting
- **FR-003**: Function MUST be C89-compatible with no platform-specific includes in the public header
- **FR-004**: IP string MUST be formatted when the IP is first assigned (not on each call) to avoid runtime formatting overhead
- **FR-005**: Function MUST return "" (empty string) for NULL input, matching PT_PeerName convention

## Success Criteria

### Measurable Outcomes

- **SC-001**: `PT_PeerAddress(peer)` returns correct dotted-quad for peers discovered on POSIX, MacTCP, and OT
- **SC-002**: Zero additional memory allocation — string stored in existing peer struct
- **SC-003**: API count goes from 21 to 22 functions
- **SC-004**: csend-pt can display "name@ip" using `PT_PeerName` + `PT_PeerAddress`
