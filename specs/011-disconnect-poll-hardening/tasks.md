# Tasks: PT_DisconnectAll and Poll Robustness Hardening

**Input**: Design documents from `/specs/011-disconnect-poll-hardening/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/peertalk-api-additions.md

**Tests**: No test tasks — spec does not request new test apps. Existing test_lifecycle verifies connect/disconnect. Hardware validation is in the polish phase.

**Organization**: Tasks grouped by user story. US1 (PT_DisconnectAll) is MVP. US2 (OT hardening) and US3 (MacTCP hardening) are independent and parallelizable.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1, US2, US3)
- Exact file paths included in all descriptions

---

## Phase 1: Setup

**Purpose**: No new project structure needed. All changes are to existing files. This phase verifies the build works before making changes.

- [x] T001 Verify POSIX build succeeds: `mkdir -p build && cd build && cmake .. -DCLOG_DIR=~/clog && make`
- [x] T002 [P] Verify 68k MacTCP build succeeds: `cd build-68k && make`
- [x] T003 [P] Verify PPC OT build succeeds: `cd build-ppc-ot && make`

**Checkpoint**: All three platform builds pass — safe to begin implementation.

---

## Phase 2: User Story 1 - Disconnect All Peers for Lobby Transition (Priority: P1) MVP

**Goal**: Add `PT_DisconnectAll(ctx)` as public API function #23 that disconnects all connected peers with goodbye frames in a single call.

**Independent Test**: Call PT_DisconnectAll after connecting to multiple peers. Verify all peers transition to DISCONNECTED state and on_disconnected fires for each.

### Implementation for User Story 1

- [x] T004 [US1] Add PT_DisconnectAll declaration to include/peertalk.h in the Connections section. Update section comment from "Connections (2)" to "Connections (3)". Add: `void PT_DisconnectAll(PT_Context *ctx);` after PT_Disconnect. Per contract: contracts/peertalk-api-additions.md.
- [x] T005 [US1] Implement PT_DisconnectAll in src/core/pt_core.c. Place after PT_Disconnect (line 481). Pattern: iterate `ctx->peers[0..max_peers]` by physical index, for each slot where `in_use && state == PT_PEER_CONNECTED`, call `send_goodbye(ctx, &ctx->peers[i])` then `pt_handle_peer_disconnect(ctx, &ctx->peers[i], PT_QUIT)`. NULL ctx is a no-op. Add CLOG_INFO with count of disconnected peers. Physical index iteration ensures safe iteration when peer state changes mid-loop (FR-007) and re-entrancy safety if on_disconnected callback calls PT_DisconnectAll (remaining peers still iterated correctly). Per research R1, R2.
- [x] T006 [US1] Verify POSIX build succeeds with new function and run test_lifecycle to confirm no regression in connect/disconnect flow.

**Checkpoint**: PT_DisconnectAll is available in the public API. POSIX build passes. MVP complete.

---

## Phase 3: User Story 2 - OT Poll Survives Unexpected Disconnection (Priority: P2)

**Goal**: Harden OT poll error handling so PT_Poll never hangs or crashes when OT endpoints are in transitional states.

**Independent Test**: Build PPC OT target. Existing test apps should work without hangs when remote peer disconnects unexpectedly.

### Implementation for User Story 2

- [x] T007 [US2] Add CLOG_DEBUG error logging after OTRcv in the disconnect drain path at src/platform/opentransport/pt_ot.c around line 894. After `nread = OTRcv(...)`, add: `if (nread < 0) { CLOG_DEBUG("OTRcv error %ld in disconnect drain", (long)nread); }` before the existing `if (nread > 0)` check. The existing positive-check logic is unchanged. Per research R3.
- [x] T008 [P] [US2] Add CLOG_DEBUG error logging after OTRcv in the orderly release drain path at src/platform/opentransport/pt_ot.c around line 929. Same pattern as T007: log negative nread at CLOG_DEBUG level. Per research R3.
- [x] T009 [P] [US2] Add CLOG_DEBUG error logging after OTRcv in the data receive path at src/platform/opentransport/pt_ot.c around line 966. Same pattern as T007: log negative nread at CLOG_DEBUG level. Per research R3.
- [x] T010 [US2] Re-read slot->owner after the drain in the orderly release handler at src/platform/opentransport/pt_ot.c. After `OTSndOrderlyDisconnect(slot->ep)` (around line 943), re-assign `peer = slot->owner;` before the check at line 945. This ensures the peer pointer is valid if pt_messaging_process_tcp_data triggered a goodbye-driven disconnect during the drain. Per research R4.
- [x] T011 [US2] Verify PPC OT build succeeds: `cd build-ppc-ot && make`

**Checkpoint**: OT poll paths have error logging and state validation. PPC OT build passes.

---

## Phase 4: User Story 3 - MacTCP Poll Survives Unexpected Disconnection (Priority: P3)

**Goal**: Harden MacTCP poll error handling so PT_Poll never hangs on terminated or aborted streams.

**Independent Test**: Build 68k MacTCP target. Existing test apps should work without hangs when remote peer disconnects.

### Implementation for User Story 3

- [x] T012 [US3] Add stream state validation before TCPRcv in the FLAG_REMOTE_CLOSE/FLAG_TERMINATED drain path at src/platform/mactcp/pt_mactcp.c around line 954. Before the `if (space > 0)` block, add a guard: `if (ts->stream && ts->state != STREAM_FREE)`. Wrap the existing TCPRcv block inside this guard so that freed streams are skipped. Per research R6.
- [x] T013 [US3] Add CLOG_DEBUG error logging when PBControlSync returns non-noErr in the terminated drain path at src/platform/mactcp/pt_mactcp.c around line 962. After the existing `if (PBControlSync(...) == noErr && ...)` block, add an else clause: `else { OSErr err = g_mactcp.recv_pb.ioResult; if (err != noErr) CLOG_DEBUG("TCPRcv error %d in terminated drain", err); }`. Per research R5.
- [x] T014 [US3] Verify 68k MacTCP build succeeds: `cd build-68k && make`

**Checkpoint**: MacTCP poll has stream state validation and error logging. 68k build passes.

---

## Phase 5: Polish & Cross-Cutting Concerns

**Purpose**: Final build verification across all platforms and hardware testing.

- [x] T015 Verify all three platform builds succeed after all changes: POSIX, 68k MacTCP, PPC OT
- [x] T016 Run POSIX test_lifecycle to verify PT_DisconnectAll works in connect/disconnect flow
- [x] T017 Deploy 68k MacTCP build to Mac SE via LaunchAPPL and run test_lifecycle to verify no hangs on disconnect
- [x] T018 Deploy PPC OT build to Performa 6400 via LaunchAPPL and run test_lifecycle to verify no hangs on disconnect. T017 and T018 together validate FR-012 (PT_Poll must never hang or crash) across all Classic Mac platforms.
- [x] T019 Update specs/001-peertalk-sdk/contracts/peertalk-api.md to document PT_DisconnectAll as function #23 in the public API

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — verify builds first
- **Phase 2 (US1 - PT_DisconnectAll)**: Depends on Phase 1 — core API change
- **Phase 3 (US2 - OT hardening)**: Depends on Phase 1 only — independent of US1
- **Phase 4 (US3 - MacTCP hardening)**: Depends on Phase 1 only — independent of US1 and US2
- **Phase 5 (Polish)**: Depends on all previous phases

### User Story Dependencies

- **US1 (PT_DisconnectAll)**: No dependencies on US2 or US3. Modifies peertalk.h + pt_core.c.
- **US2 (OT hardening)**: No dependencies on US1 or US3. Modifies pt_ot.c only.
- **US3 (MacTCP hardening)**: No dependencies on US1 or US2. Modifies pt_mactcp.c only.

### Within Each User Story

- Header declaration before implementation (US1: T004 before T005)
- Implementation before build verification (T005 before T006)
- OT error logging tasks T007, T008, T009 are parallelizable (different code locations in same file, but non-overlapping edits)

### Parallel Opportunities

- After Phase 1 completes, US1, US2, and US3 can all proceed in parallel (different files)
- Within US2: T007, T008, T009 can run in parallel (different locations in pt_ot.c)
- T002 and T003 can run in parallel with T001 (different build directories)

---

## Parallel Example: All User Stories

```bash
# After Phase 1 (setup builds pass), launch all three stories in parallel:
# US1: T004 → T005 → T006 (peertalk.h, pt_core.c)
# US2: T007 + T008 + T009 → T010 → T011 (pt_ot.c)
# US3: T012 → T013 → T014 (pt_mactcp.c)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Verify builds
2. Complete Phase 2: PT_DisconnectAll (T004-T006)
3. **STOP and VALIDATE**: POSIX test_lifecycle passes
4. This alone fixes the BomberTalk lobby transition bug

### Incremental Delivery

1. Setup builds → verified
2. Add PT_DisconnectAll → POSIX test → MVP done
3. Add OT poll hardening → PPC OT build verified
4. Add MacTCP poll hardening → 68k build verified
5. Hardware test all platforms → complete

---

## Notes

- All changes are C89 compatible (no // comments, no mixed declarations, no VLAs)
- Zero new memory allocations — all changes operate on existing data structures
- Total estimated change: ~25 lines across 4 existing files
- No new files created in source tree
- 19 tasks total across 5 phases
