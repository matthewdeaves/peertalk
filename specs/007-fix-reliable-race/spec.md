# Feature Specification: Fix test_reliable Responder Race

**Feature Branch**: `007-fix-reliable-race`
**Created**: 2026-03-26
**Status**: Draft
**Input**: Fix test_reliable responder g_moves_done race causing spurious FAIL on slow hardware (Mac SE).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Responder Marks Completion After Final Send (Priority: P1)

A developer runs test_reliable between a POSIX peer and a Mac SE. The Mac SE is the responder (accepts incoming connection). After exchanging 10 moves each, the Mac SE reports FAIL even though all moves were sent, received, and validated. The root cause: the responder never sets its "moves done" flag, so when the initiator disconnects and re-discovery fires, the responder's role flag flips from "responder" to "initiator," breaking the broadcast pass/fail check.

After this fix, both the initiator and responder mark completion immediately after their 10th move, preventing re-discovery from corrupting the test verdict.

**Why this priority**: This is a timing-dependent bug that causes spurious test failures on slow hardware. It blocks hardware validation of the SDK.

**Independent Test**: Run test_reliable with Mac SE as responder against POSIX peer. Verify PASS with 10 sent, 10 received, broadcast received.

**Acceptance Scenarios**:

1. **Given** a responder that has sent its 10th move, **When** the responder's send count reaches the total, **Then** the completion flag is set immediately (not deferred to a future receive)
2. **Given** a responder with the completion flag set, **When** the initiator disconnects and re-discovery fires, **Then** the re-discovery guard prevents role flip (guard checks completion flag)
3. **Given** test_reliable running on Mac SE against POSIX peer, **When** the test completes, **Then** the verdict is PASS with 10 sent, 10 received, valid order, valid payload, broadcast received

---

### Edge Cases

- What if the responder's 10th send fails? (Completion flag should not be set; test will fail on send count, which is correct)
- What if both peers finish simultaneously and both try to broadcast? (Only the initiator broadcasts; responder checks broadcast_received — no change to this logic)

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The responder MUST set the completion flag after successfully sending its final move (when send count reaches the total)
- **FR-002**: The completion flag MUST be set in the same code path as the send, not deferred to a subsequent receive
- **FR-003**: The re-discovery guard MUST prevent reconnection after the completion flag is set
- **FR-004**: The fix MUST NOT change the initiator's behavior (initiator already sets completion correctly on its final receive)
- **FR-005**: The fix MUST be limited to the test app (no SDK changes)

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: test_reliable passes on Mac SE (68k MacTCP) against POSIX peer with 10/10 moves and broadcast received
- **SC-002**: test_reliable passes on Performa 6200 (PPC MacTCP) with no regressions
- **SC-003**: test_reliable passes on Performa 6400 (PPC OT) with no regressions

## Assumptions

- The fix is a single test file change (tests/test_reliable.c)
- C11 is allowed in test apps per Constitution principle X
- The initiator's completion path (set on final receive) remains unchanged
