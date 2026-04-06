# Tasks: Poll Back-Pointer Optimisation

**Input**: Design documents from `/specs/010-poll-backpointer/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md

**Tests**: No new test code needed — existing test apps (test_init_only, test_lifecycle, test_fast, test_reliable) validate all affected paths.

**Organization**: Tasks grouped by user story. US1 (MacTCP backend) and US2 (OT backend) can proceed in parallel since they modify different files.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1, US2, US3)
- Exact file paths included in descriptions

---

## Phase 1: Setup

**Purpose**: No setup needed — no new files, no build changes, no dependency changes.

(Phase empty — proceed directly to Phase 2.)

---

## Phase 2: Foundational (Struct Modifications)

**Purpose**: Add the `owner` field to both platform structs. These are the data model changes that all subsequent tasks depend on.

**CRITICAL**: These two tasks modify different files and can run in parallel.

- [x] T001 [P] Add `PT_Peer_Internal *owner` field to `TCPStreamSlot` struct in src/platform/mactcp/pt_mactcp.c (after `send_pending` field, initialise to NULL)
- [x] T002 [P] Add `PT_Peer_Internal *owner` field to `OTEndpointSlot` struct in src/platform/opentransport/pt_ot.c (after `flow_off` field, initialise to NULL)
- [x] T003 [P] Initialise `owner = NULL` for all TCP stream slots in `mactcp_init()` loop in src/platform/mactcp/pt_mactcp.c (after `state = STREAM_FREE`)
- [x] T004 [P] Initialise `owner = NULL` for all endpoint slots in `ot_init()` loop in src/platform/opentransport/pt_ot.c (after `state = EP_FREE`)

**Checkpoint**: Both structs have the new field, initialised to NULL at init time. Builds succeed on all platforms. Existing tests pass unchanged (owner field is unused so far).

---

## Phase 3: User Story 1 — MacTCP Back-Pointer (Priority: P1)

**Goal**: Replace all `find_peer_for_stream()` calls with `ts->owner` dereference in MacTCP poll loop.

**Independent Test**: Build 68k MacTCP target and run test_lifecycle between Mac SE and a POSIX peer. All discovery, connection, messaging, and disconnect succeed.

### Implementation for User Story 1

- [x] T005 [US1] Set `ts->owner = peer` in `mactcp_tcp_connect()` in src/platform/mactcp/pt_mactcp.c, immediately after `peer->platform_peer.tcp_stream = ts` (line ~653)
- [x] T006 [US1] Set `ts->owner` in `mactcp_poll()` passive open completion path in src/platform/mactcp/pt_mactcp.c (~line 749-758). After `pt_handle_incoming_connection()` returns, scan `ctx->peers[]` to find which peer (if any) now has `platform_peer.tcp_stream == ts`. If found, set `ts->owner = peer`. If not found (connection rejected — no room), the existing `abort_stream(i)` path (which clears owner via T007) handles cleanup. Replace the current `find_peer_for_stream` call and `if (peer)` check with `if (ts->owner)`
- [x] T007 [US1] Clear `ts->owner = NULL` in `abort_stream()` in src/platform/mactcp/pt_mactcp.c (after `ts->state = STREAM_FREE`)
- [x] T008 [US1] Clear `ts->owner = NULL` in `mactcp_tcp_disconnect()` in src/platform/mactcp/pt_mactcp.c (before `peer->platform_peer.tcp_stream = NULL`)
- [x] T009 [US1] Replace `find_peer_for_stream(ctx, ts)` call in FLAG_DATA_AVAIL handler with `ts->owner` in src/platform/mactcp/pt_mactcp.c (poll connected stream events, ~line 818)
- [x] T010 [US1] Replace `find_peer_for_stream(ctx, ts)` call in FLAG_REMOTE_CLOSE/FLAG_TERMINATED handler with `ts->owner` in src/platform/mactcp/pt_mactcp.c (~line 851)
- [x] T011 [US1] Remove the `find_peer_for_stream()` static function from src/platform/mactcp/pt_mactcp.c (lines 303-315)
- [x] T012 [US1] Build POSIX target and run test_init_only and test_lifecycle to verify no regressions in src/platform/mactcp/pt_mactcp.c changes (POSIX doesn't use MacTCP, but confirms no compile errors in shared headers)

**Checkpoint**: MacTCP backend uses back-pointers exclusively. `find_peer_for_stream` is removed. 68k build compiles cleanly.

---

## Phase 4: User Story 2 — OT Back-Pointer (Priority: P2)

**Goal**: Replace all `find_peer_for_ep()` calls with `slot->owner` dereference in OT poll loop.

**Independent Test**: Build PPC OT target and run test_lifecycle between Performa 6400 and a POSIX peer. All discovery, connection, messaging, and disconnect succeed.

### Implementation for User Story 2

- [x] T013 [US2] Set `slot->owner = peer` in `ot_tcp_connect()` in src/platform/opentransport/pt_ot.c, immediately after `peer->platform_peer.endpoint = slot` (~line 590)
- [x] T014 [US2] Set `slot->owner` in `ot_poll()` listener T_LISTEN handling path in src/platform/opentransport/pt_ot.c (~line 769-793). After `pt_handle_incoming_connection()` returns, scan `ctx->peers[]` to find which peer (if any) now has `platform_peer.endpoint == slot`. If found, set `slot->owner = peer`. If not found (connection rejected — no room), the existing unbind/rebind reset path (which clears owner via T016) handles cleanup. Replace the current `find_peer_for_ep(ctx, slot)` call and `if (!find_peer_for_ep(...))` check with `if (!slot->owner)`
- [x] T015 [US2] Clear `slot->owner = NULL` in `ot_tcp_disconnect()` in src/platform/opentransport/pt_ot.c (after `slot->state = EP_FREE`)
- [x] T016 [US2] Clear `slot->owner = NULL` in the reject-no-room endpoint reset path in `ot_poll()` listener handling in src/platform/opentransport/pt_ot.c (after `slot->state = EP_FREE`, ~line 791)
- [x] T017 [US2] Replace `find_peer_for_ep(ctx, slot)` call in T_CONNECT active connect completion handler with `slot->owner` in src/platform/opentransport/pt_ot.c (~line 832)
- [x] T018 [US2] Replace `find_peer_for_ep(ctx, slot)` call in T_DISCONNECT handler with `slot->owner` in src/platform/opentransport/pt_ot.c (~line 882)
- [x] T019 [US2] Replace `find_peer_for_ep(ctx, slot)` call in T_ORDREL handler with `slot->owner` in src/platform/opentransport/pt_ot.c (~line 917)
- [x] T020 [US2] Replace `find_peer_for_ep(ctx, slot)` call in T_DATA handler with `slot->owner` in src/platform/opentransport/pt_ot.c (~line 953)
- [x] T021 [US2] Clear `slot->owner = NULL` in the failed active connect reset path in `ot_poll()` in src/platform/opentransport/pt_ot.c (~line 864, after `slot->state = EP_FREE`)
- [x] T022 [US2] Remove the `find_peer_for_ep()` static function from src/platform/opentransport/pt_ot.c (lines 170-181)
- [x] T023 [US2] Build POSIX target and run test_init_only and test_lifecycle to verify no regressions (confirms no compile errors in shared headers)

**Checkpoint**: OT backend uses back-pointers exclusively. `find_peer_for_ep` is removed. PPC build compiles cleanly.

---

## Phase 5: User Story 3 — Reconnect Resilience Verification (Priority: P3)

**Goal**: Verify back-pointers survive disconnect/reconnect cycles without stale pointer issues.

**Independent Test**: Run test_fast and test_reliable on POSIX (both exercise connect/disconnect/reconnect patterns). If Classic Mac hardware available, run on 68k MacTCP and PPC OT targets.

### Verification for User Story 3

- [x] T024 [US3] Build all three platform targets (POSIX, 68k MacTCP, PPC OT) and verify clean compilation with no warnings
- [ ] T025 [US3] Run test_init_only on POSIX to verify init/shutdown cycle
- [ ] T026 [US3] Run test_lifecycle on POSIX with two peers to verify discovery → connect → message → disconnect
- [ ] T027 [US3] Run test_fast on POSIX with two peers to verify UDP fast messages work through connection lifecycle
- [ ] T028 [US3] Run test_reliable on POSIX with two peers to verify TCP chunked messages and reconnect patterns

**Checkpoint**: All four test apps pass on POSIX. Ready for hardware testing.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Final cleanup and validation.

- [x] T029 Verify total SDK line count remains under 15,000 lines (expect net reduction)
- [x] T030 Run quickstart.md validation — confirm find_peer_for_stream and find_peer_for_ep no longer exist in codebase (grep for both function names)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: Empty — no setup needed
- **Foundational (Phase 2)**: No dependencies — can start immediately. T001+T003 (MacTCP) and T002+T004 (OT) can run in parallel.
- **User Story 1 (Phase 3)**: Depends on T001, T003 (MacTCP struct field exists)
- **User Story 2 (Phase 4)**: Depends on T002, T004 (OT struct field exists). **Independent of US1** — can run in parallel.
- **User Story 3 (Phase 5)**: Depends on US1 and US2 completion
- **Polish (Phase 6)**: Depends on US3 completion

### User Story Dependencies

- **User Story 1 (P1)**: MacTCP only — can start after T001, T003
- **User Story 2 (P2)**: OT only — can start after T002, T004. **No dependency on US1.**
- **User Story 3 (P3)**: Verification — depends on both US1 and US2

### Parallel Opportunities

- T001 + T002 (struct changes in different files)
- T003 + T004 (init changes in different files)
- All of US1 (Phase 3) + all of US2 (Phase 4) — completely independent files

---

## Parallel Example: Foundational + User Stories

```bash
# Foundational — both in parallel:
Task: "T001 Add owner field to TCPStreamSlot in pt_mactcp.c"
Task: "T002 Add owner field to OTEndpointSlot in pt_ot.c"

# Then User Stories 1 and 2 — fully parallel:
Task: "T005-T012 MacTCP back-pointer implementation"
Task: "T013-T023 OT back-pointer implementation"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 2: T001, T003 (MacTCP struct field)
2. Complete Phase 3: User Story 1 (MacTCP back-pointer)
3. **STOP and VALIDATE**: Build 68k target, run test_lifecycle on Mac SE
4. If passing, proceed to US2

### Incremental Delivery

1. T001-T004 → Struct fields added → builds pass
2. T005-T012 → MacTCP converted → 68k tests pass
3. T013-T023 → OT converted → PPC tests pass
4. T024-T028 → Full verification → all platforms pass
5. T029-T030 → Cleanup confirmed

---

## Notes

- [P] tasks = different files, no dependencies
- US1 and US2 are fully independent — they modify different platform backend files
- No new test code needed — existing test apps cover all affected paths
- Commit after each phase for easy bisection if issues arise
- Net line count change: approximately -20 lines (scan functions removed, ~4 lines of field declarations added)
