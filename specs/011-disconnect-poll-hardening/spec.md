# Feature Specification: PT_DisconnectAll and Poll Robustness Hardening

**Feature Branch**: `011-disconnect-poll-hardening`  
**Created**: 2026-04-12  
**Status**: Draft  
**Input**: User description: "PT_DisconnectAll convenience function and poll robustness hardening"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Disconnect All Peers for Lobby Transition (Priority: P1)

A Bomberman app transitions from an active game back to the lobby. The app needs to cleanly disconnect all connected peers so that when it restarts discovery for the lobby browser, there are no stale TCP connections that could cause hangs or crashes on Classic Mac platforms. Today the app must manually iterate peers and call PT_Disconnect on each one; the SDK should provide a single call to do this.

**Why this priority**: This is the primary motivating use case. Without this, every app using PeerTalk must implement its own disconnect-all loop, and getting the iteration wrong (e.g., index shifting during disconnect) leads to bugs. All three target apps (Bomberman lobby return, Chess rematch, Chat reconnect) need this pattern.

**Independent Test**: Call PT_DisconnectAll after connecting to multiple peers. Verify all peers transition to DISCONNECTED state and goodbye frames are sent. Verify discovery can be restarted cleanly afterward.

**Acceptance Scenarios**:

1. **Given** 3 peers are connected, **When** app calls PT_DisconnectAll, **Then** all 3 peers receive goodbye frames and transition to DISCONNECTED state
2. **Given** no peers are connected, **When** app calls PT_DisconnectAll, **Then** the call completes without error (no-op)
3. **Given** 2 peers connected and 1 peer in DISCOVERED state, **When** app calls PT_DisconnectAll, **Then** only the 2 connected peers are disconnected; the discovered peer remains unchanged
4. **Given** PT_DisconnectAll has been called, **When** app calls PT_StartDiscovery, **Then** discovery starts cleanly with no stale connection state

---

### User Story 2 - OT Poll Survives Unexpected Disconnection (Priority: P2)

An Open Transport peer disconnects unexpectedly (cable pull, app crash, or power loss) while the local app is actively polling. The SDK must handle the resulting OT endpoint errors gracefully rather than hanging or crashing. Today, error return values from OT receive operations are silently ignored, which can leave the poll loop processing invalid endpoints.

**Why this priority**: This is a correctness/robustness issue. A hang or crash in PT_Poll on Classic Mac hardware requires a restart of the machine. All three target apps are affected because any peer can disconnect unexpectedly.

**Independent Test**: Connect two peers on OT hardware. Kill the remote peer's app without clean shutdown. Verify the local peer's PT_Poll detects the disconnection and fires the on_disconnected callback without hanging.

**Acceptance Scenarios**:

1. **Given** a connected OT peer, **When** the remote peer disconnects abruptly, **Then** PT_Poll processes the disconnect event and fires on_disconnected callback within the next poll cycle
2. **Given** a connected OT peer, **When** OTRcv returns an error during disconnect drain, **Then** PT_Poll treats the error as a disconnection rather than silently ignoring it
3. **Given** an OT endpoint receiving an orderly release, **When** PT_Poll processes the release, **Then** the endpoint state is validated before any peer pointer is used

---

### User Story 3 - MacTCP Poll Survives Unexpected Disconnection (Priority: P3)

A MacTCP peer disconnects unexpectedly while the local app is polling. The SDK must handle TCPRcv errors on terminated or aborted streams gracefully. Today, error returns from TCPRcv on terminated streams are not checked, which could leave the poll processing data on invalid streams.

**Why this priority**: Same robustness concern as OT but on 68k hardware. MacTCP is single-threaded and cooperative, so the timing windows are narrower, but the consequences of a hang on a Mac SE (hard reboot required) make this important.

**Independent Test**: Connect two peers with one on MacTCP hardware. Kill the remote peer's app. Verify the local MacTCP peer detects disconnection cleanly via PT_Poll.

**Acceptance Scenarios**:

1. **Given** a connected MacTCP peer, **When** the remote peer disconnects and FLAG_TERMINATED is set, **Then** PT_Poll handles TCPRcv errors on the terminated stream without hanging
2. **Given** a MacTCP stream with FLAG_REMOTE_CLOSE set, **When** PT_Poll drains remaining data, **Then** stream state is validated before issuing TCPRcv
3. **Given** a MacTCP peer disconnects during active data transfer, **When** PT_Poll processes the termination, **Then** on_disconnected callback fires with appropriate reason

---

### Edge Cases

- What happens if PT_DisconnectAll is called during an active PT_Poll iteration? (Must be safe since both run in the same cooperative main loop — PT_DisconnectAll would only be called between poll calls)
- What happens if a peer's goodbye send fails because the TCP connection is already broken? (send_goodbye failure should not prevent the local disconnect cleanup from completing)
- What happens if PT_DisconnectAll is called when some peers are in CONNECTING state (not yet CONNECTED)? (Only CONNECTED peers should be disconnected; CONNECTING peers should be left as-is, matching PT_Disconnect behavior)
- What happens if the on_disconnected callback itself calls PT_DisconnectAll? (Re-entrancy: the function must be safe if called from within its own callback, since remaining peers may still be in the iteration)

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: SDK MUST provide a PT_DisconnectAll function that disconnects all currently connected peers with goodbye frames
- **FR-002**: PT_DisconnectAll MUST send a goodbye frame to each connected peer before closing the connection, matching PT_Disconnect behavior
- **FR-003**: PT_DisconnectAll MUST fire the on_disconnected callback for each disconnected peer with reason PT_QUIT
- **FR-004**: PT_DisconnectAll MUST be a no-op when no peers are connected (no error, no callbacks)
- **FR-005**: PT_DisconnectAll MUST NOT affect discovery state — it disconnects peers only, leaving discovery active or stopped as it was
- **FR-006**: PT_DisconnectAll MUST NOT affect peers in DISCOVERED or DISCONNECTED states — only CONNECTED peers are disconnected
- **FR-007**: PT_DisconnectAll MUST safely iterate the peer array even though disconnecting a peer changes its state during iteration
- **FR-008**: OT poll MUST check error return values from all OTRcv calls and treat negative returns as disconnection events
- **FR-009**: OT poll MUST validate endpoint slot state before using peer pointers after processing orderly release events
- **FR-010**: MacTCP poll MUST check error return values from TCPRcv on terminated or aborted streams and handle them as disconnection events
- **FR-011**: MacTCP poll MUST validate stream state before issuing TCPRcv in the remote-close and terminated drain paths
- **FR-012**: PT_Poll MUST never hang or crash regardless of the state of remote peers or network connections

### Key Entities

- **PT_Context**: Owns the peer array and platform operations; passed to PT_DisconnectAll
- **PT_Peer**: Individual peer with state (DISCOVERED, CONNECTED, DISCONNECTED) and platform-specific endpoint/stream handle
- **Endpoint/Stream**: Platform-specific connection handle (OT endpoint slot, MacTCP TCP stream) that may be in transitional states during disconnect

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Apps can transition from game to lobby with a single SDK call, replacing manual peer iteration loops
- **SC-002**: PT_Poll completes every poll cycle without hanging on all three platforms (POSIX, MacTCP, OT) even when remote peers disconnect unexpectedly. A "hang" means the machine becomes unresponsive and requires a hard reboot — the failure mode on Classic Mac hardware when poll loops block on invalid endpoints or streams.
- **SC-003**: All existing test apps (test_init_only, test_lifecycle, test_fast, test_reliable) continue to pass on all hardware targets after changes
- **SC-004**: PT_DisconnectAll correctly disconnects all connected peers as verified by peer state and on_disconnected callbacks in a test scenario with 2+ peers

## Assumptions

- PT_DisconnectAll is always called from the main event loop, never from within an interrupt or callback context (consistent with all other PT_ API calls)
- Classic Mac platforms are single-threaded and cooperative, so there are no true race conditions between PT_Poll and PT_DisconnectAll — they cannot run simultaneously
- The peer array uses in_use flags and does not compact, so iterating by physical index is safe even when peer state changes during iteration
- Goodbye frame send failure (e.g., broken TCP) does not block the disconnect — the local cleanup proceeds regardless
