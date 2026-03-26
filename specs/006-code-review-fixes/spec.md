# Feature Specification: Code Review Fixes

**Feature Branch**: `006-code-review-fixes`
**Created**: 2026-03-26
**Status**: Draft
**Input**: Five issues confirmed by code review and fact-checked against Inside Macintosh, MacTCP Programmer's Guide, and Networking With Open Transport primary sources.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Atomic Flag Exchange in Platform Backends (Priority: P1)

A developer running two peers (e.g., Bomberman on Mac SE and Performa 6400) experiences a connection that hangs for 60 seconds instead of disconnecting cleanly. The root cause is a lost disconnect flag: the MacTCP ASR or OT notifier set the flag between the main loop's read and clear of the flags variable, and the clear overwrote the flag before it was processed. After this fix, all flags set by interrupt-level or deferred-task-level callbacks are reliably captured by the main loop's snapshot operation.

**Why this priority**: A lost disconnect flag causes a 60-second hang visible to users. A lost data-arrival flag causes received data to stall until the next unrelated network event. Both degrade the experience of all three target apps (Bomberman, Chess, Chat).

**Independent Test**: Run test_lifecycle between two Classic Mac peers. Verify clean disconnect completes within 5 seconds (not 60). Run test_reliable and verify all messages arrive without stalls.

**Acceptance Scenarios**:

1. **Given** a MacTCP peer with an active TCP connection, **When** the remote peer disconnects, **Then** the local peer detects the disconnect within the TCP timeout period (no missed flags causing additional 60-second delay)
2. **Given** an OT peer receiving TCP data, **When** the notifier sets EVT_DATA between the main loop's flag read and flag clear, **Then** the data-arrival flag is not lost and the data is processed on the current or next poll cycle
3. **Given** a MacTCP peer, **When** the ASR sets FLAG_DATA_AVAIL during the main loop's flag snapshot, **Then** the flag is captured in the snapshot (not cleared without being read)

---

### User Story 2 - Init Failure Cleanup (Priority: P2)

A developer calling PT_Init on a Classic Mac where a network resource is unavailable (e.g., MacTCP driver not loaded, OT endpoint creation fails) gets PT_ERR_INIT returned. After this fix, all resources allocated before the failure point are properly released, preventing memory leaks on memory-constrained machines (4MB Mac SE, 8MB Performa 6200).

**Why this priority**: On Classic Macs with 4-8MB RAM, leaked resources from a failed init can exhaust available memory, preventing a successful retry or other application work. The fix is straightforward (goto-based cleanup) and self-contained within each platform init function.

**Independent Test**: On POSIX, simulate init failure by exhausting a resource. Verify no memory leaks via code inspection. On Classic Mac hardware, verify PT_Init succeeds on retry after a transient failure.

**Acceptance Scenarios**:

1. **Given** MacTCP init where UDP stream creation fails after TCP streams were created, **When** PT_Init returns PT_ERR_INIT, **Then** all previously created TCP streams, buffers, and UPPs are released
2. **Given** OT init where message UDP endpoint creation fails after listener and discovery endpoints were created, **When** PT_Init returns PT_ERR_INIT, **Then** all previously created endpoints are closed and UPPs are disposed before CloseOpenTransport is called

---

### User Story 3 - Reassembly Admission Check Fix (Priority: P3)

A developer sending a reliable message near the reassembly buffer limit (e.g., a 65,000-byte game state snapshot) finds it rejected with an error callback even though the reassembly buffer (65,536 bytes) has room. The root cause is that the admission check overestimates total message size by assuming all chunks are the same size as the first chunk. After this fix, messages that fit in the reassembly buffer are always accepted.

**Why this priority**: The rejection window is narrow (~4KB at the top of the 64KB default buffer), but the fix is simple and removes an incorrect calculation. This matters for Chess (large board state) more than Bomberman (small position updates).

**Independent Test**: On POSIX, send a message of exactly reassembly_buf_size - 1 bytes between two peers. Verify it is received successfully (currently rejected due to overestimate).

**Acceptance Scenarios**:

1. **Given** a peer with a 65,536-byte reassembly buffer, **When** a 65,000-byte message is sent via PT_RELIABLE, **Then** the message is accepted for reassembly and delivered to the callback
2. **Given** a peer with a 65,536-byte reassembly buffer, **When** a 65,537-byte message is sent, **Then** the message is correctly rejected (buffer too small)

---

### User Story 4 - PT_Broadcast Semantics with No Peers (Priority: P4)

A developer calling PT_Broadcast before any peers have connected receives PT_ERR_SEND_FAILED. This is misleading because no send was attempted. After this fix, broadcasting to zero connected peers returns PT_OK (a successful no-op).

**Why this priority**: Minor API correctness issue. Callers currently cannot distinguish "no peers" from "send failed to a peer." The fix is a one-line change. Serves all three apps when broadcasting game state before opponents connect.

**Independent Test**: On POSIX, call PT_Broadcast immediately after PT_Init (before any peers connect). Verify return value is PT_OK.

**Acceptance Scenarios**:

1. **Given** a context with no connected peers, **When** PT_Broadcast is called, **Then** it returns PT_OK
2. **Given** a context with two connected peers where one send fails, **When** PT_Broadcast is called, **Then** it returns PT_ERR_SEND_FAILED

---

### User Story 5 - POSIX UDP Drain Loop (Priority: P5)

A developer running the Bomberman test (60Hz UDP) between two POSIX peers notices message lag under load. The POSIX backend reads only one UDP datagram per poll cycle, while the OT backend reads all available datagrams. After this fix, the POSIX backend drains the UDP socket on each poll, matching OT behavior.

**Why this priority**: Lowest priority because the current 16ms poll interval makes single-read-per-poll adequate for most scenarios. The fix improves consistency between platform backends and helps under sustained high-frequency messaging.

**Independent Test**: Run test_fast between two POSIX peers. Verify inter-arrival latency does not degrade over the 5-second test window.

**Acceptance Scenarios**:

1. **Given** a POSIX peer receiving UDP messages at 60Hz, **When** PT_Poll is called, **Then** all pending datagrams are read (not just one)
2. **Given** a POSIX peer with no pending UDP datagrams, **When** PT_Poll is called, **Then** the UDP drain loop exits immediately without blocking

---

### Edge Cases

- What happens if the 68k interrupt-disable approach adds measurable latency to the main loop? (The critical section is 2-3 instructions, sub-microsecond even at 8MHz)
- What happens if OTAtomicClearBit is not available on all OT versions? (It is in Table C-1, available since OT 1.0)
- What happens if init cleanup calls fail (e.g., stream release fails)? (Best-effort cleanup, log errors, still return PT_ERR_INIT)
- What happens if the reassembly buffer check is removed entirely and only per-chunk bounds checking is used? (A malformed chunk with a huge offset could write past the buffer; per-chunk offset+payload check prevents this)

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: MacTCP backend MUST disable interrupts around the flag snapshot-and-clear operation to prevent ASR flag loss
- **FR-002**: OT backend MUST use OTAtomic* functions for flag manipulation in notifiers and poll to prevent notifier flag loss
- **FR-003**: MacTCP init MUST release all previously created resources (streams, buffers, UPPs, driver) when a later init step fails
- **FR-004**: OT init MUST close all previously created endpoints and dispose all UPPs when a later init step fails, before calling CloseOpenTransport
- **FR-005**: Reassembly admission check MUST accept any chunked message whose total reassembled size fits within the reassembly buffer
- **FR-006**: Reassembly MUST reject any individual chunk whose offset + payload would exceed the reassembly buffer bounds
- **FR-007**: PT_Broadcast MUST return PT_OK when no peers are connected
- **FR-008**: PT_Broadcast MUST return PT_ERR_SEND_FAILED only when at least one send to a connected peer actually fails
- **FR-009**: POSIX backend MUST drain all pending UDP datagrams per poll cycle for both discovery and message sockets
- **FR-010**: POSIX UDP drain loop MUST exit when recvfrom returns -1 with EAGAIN/EWOULDBLOCK (no blocking)
- **FR-011**: All fixes MUST maintain C89 compatibility in SDK source files
- **FR-012**: All fixes MUST preserve existing test_lifecycle, test_fast, and test_reliable pass criteria on all platforms

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: test_lifecycle passes on Mac SE (68k MacTCP), Performa 6200 (PPC MacTCP), and Performa 6400 (PPC OT) with no regressions
- **SC-002**: test_reliable passes on all three hardware targets with no message delivery stalls
- **SC-003**: test_fast passes on POSIX with inter-arrival latency no worse than before the change
- **SC-004**: POSIX build compiles with zero warnings under -Wall -Wextra
- **SC-005**: Total SDK line count remains under 15,000 lines (Constitution principle IX)

## Assumptions

- The 68k interrupt-disable pattern uses inline assembly or a compiler intrinsic available in Retro68's GCC (move SR to save, ori #0x0700 to disable, move to restore)
- OTAtomicSetBit, OTAtomicClearBit, and OTAtomicTestBit are available in the OT headers provided by Retro68
- Init failure is rare in practice (network stack not loaded, out of memory); the cleanup path does not need to be highly optimized
- The reassembly per-chunk bounds check (offset + payload <= buffer_size) is sufficient to prevent buffer overflows without an aggregate admission check
