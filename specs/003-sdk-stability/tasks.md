# Tasks: SDK Stability Improvements

**Input**: Design documents from `/specs/003-sdk-stability/`
**Prerequisites**: plan.md, spec.md, research.md, contracts/

**Tests**: No separate test tasks — validation is via existing test apps on real hardware.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Foundational (Error Callback Signature Change)

**Purpose**: The error callback signature change (FR-009) touches all platforms and all test apps. It must be done first as it affects every other phase.

**⚠️ CRITICAL**: This is a breaking API change. All call sites must be updated atomically.

- [x] T001 [US3] Update PT_ErrorCallback typedef to add PT_Peer* as first parameter in include/peertalk.h
- [x] T002 [US3] Update pt_fire_error declaration to add PT_Peer_Internal* parameter in src/core/pt_internal.h
- [x] T003 [US3] Update pt_fire_error implementation and all call sites in src/core/pt_core.c — pass peer where available, NULL where not
- [x] T004 [P] [US3] Update pt_fire_error call site in src/core/pt_discovery.c — pass NULL (no peer context for discovery no-room error)
- [x] T005 [P] [US3] Update pt_fire_error call site in src/core/pt_messaging.c — pass peer pointer from reassembly context
- [x] T006 [P] [US3] Update pt_fire_error call site in src/platform/posix/pt_posix.c — pass peer pointer for send failure
- [x] T007 [P] [US3] Update pt_fire_error call site in src/platform/mactcp/pt_mactcp.c — pass peer pointer for send failure
- [x] T008 [P] [US3] Update pt_fire_error call sites in src/platform/opentransport/pt_ot.c — pass peer pointer for send failure and NULL for no-room
- [x] T009 [P] [US3] Update on_error callback in tests/test_lifecycle.c to match new signature (add PT_Peer* first param)
- [x] T010 [P] [US3] Update on_error callback in tests/test_fast.c to match new signature
- [x] T011 [P] [US3] Update on_error callback in tests/test_reliable.c to match new signature
- [x] T012 [P] [US3] Update on_error callback in tests/test_init_only.c to match new signature (no on_error callback — already compatible)
- [x] T013 [P] [US3] Update on_error callback in tests/test_chat.c to match new signature
- [x] T014 [US3] Build POSIX target and verify all test apps compile cleanly in build/

**Checkpoint**: Error callback API change complete. All test apps compile. pt_fire_error passes peer context where available.

---

## Phase 2: User Story 1 - Stable Long-Running Connections (Priority: P1) 🎯 MVP

**Goal**: Increase timeout constants so connections stay up on slow Classic Mac hardware.

**Independent Test**: Run test_lifecycle between POSIX and a Classic Mac. Connection should hold for 45+ seconds of idle without spurious timeout.

### Implementation for User Story 1

- [x] T015 [P] [US1] Change PT_TCP_TIMEOUT from 30 to 60 in src/core/pt_internal.h
- [x] T016 [P] [US1] Change PT_CONNECT_TIMEOUT from 10 to 15 in src/core/pt_internal.h
- [x] T017 [P] [US1] Change PT_DISCOVERY_TIMEOUT from 10 to 15 in src/core/pt_internal.h
- [x] T018 [US1] Update MacTCP ulpTimeoutValue from 30 to 60 and commandTimeoutValue from 10 to 15 for active connections in src/platform/mactcp/pt_mactcp.c
- [x] T019 [US1] Build all targets (POSIX, 68k MacTCP, PPC OT) and verify no regressions

**Checkpoint**: Timeout values updated. All platforms build. Ready for hardware test.

---

## Phase 3: User Story 2 - No Duplicate Connections (Priority: P1)

**Goal**: Add IP-based tiebreaker to prevent dual connections when both peers auto-connect on discovery.

**Independent Test**: Run two peers that both auto-connect on discovery. Verify exactly one TCP connection per peer pair (no duplicate on_connected callbacks for same peer).

### Implementation for User Story 2

- [x] T020 [US2] Add duplicate connection check at top of pt_handle_incoming_connection in src/core/pt_core.c — if peer already CONNECTED, reject incoming (log and close)
- [x] T021 [US2] Add IP tiebreaker logic in pt_handle_incoming_connection in src/core/pt_core.c — if peer has connect_start > 0 (outgoing pending), compare ctx->local_ip vs peer_ip; if local_ip > peer_ip (we should not initiate), cancel outgoing and accept incoming; if local_ip < peer_ip (we are the initiator), reject incoming
- [x] T022 [US2] Handle same-IP edge case in tiebreaker in src/core/pt_core.c — if local_ip == peer_ip, accept the incoming connection (loopback scenario)
- [x] T023 [US2] Build POSIX target and test with two local peers to verify dedup works in build/

**Checkpoint**: Duplicate connection prevention working. Tiebreaker deterministic.

---

## Phase 4: User Story 4 - Clean Shutdown (Priority: P2)

**Goal**: No callbacks fire during PT_Shutdown() teardown.

**Independent Test**: Connect peers, call PT_Shutdown(). Verify no on_disconnected callbacks fire.

### Implementation for User Story 4

- [x] T024 [US4] Clear all callback pointers at top of PT_Shutdown before the disconnect loop in src/core/pt_core.c — zero out ctx->callbacks struct (memset or individual NULLs) before sending goodbyes
- [x] T025 [US4] Build POSIX target and verify test_lifecycle still passes (shutdown is clean, no callbacks after shutdown begins)

**Checkpoint**: PT_Shutdown no longer fires stale callbacks on any platform.

---

## Phase 5: Hardware Verification

**Purpose**: Build all cross-compile targets and verify on real hardware.

- [ ] T026 Build 68k MacTCP target (build-68k/) and verify clean compilation
- [ ] T027 Build PPC OT target (build-ppc-ot/) and verify clean compilation
- [ ] T028 Run test_lifecycle on Mac SE (68k MacTCP) via LaunchAPPL with POSIX peer — verify PASS and no spurious timeouts in clog
- [ ] T029 Run test_lifecycle on Performa 6400 (PPC OT) via LaunchAPPL or FTP with POSIX peer — verify PASS
- [ ] T030 Download and review clog files from hardware tests — confirm no TCP timeout disconnects during normal operation

**Checkpoint**: All platforms verified on real hardware. Feature complete.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Error Callback)**: No dependencies — start immediately. BLOCKS all other phases (API change).
- **Phase 2 (Timeouts)**: Depends on Phase 1 completion (error callback signature must be settled first)
- **Phase 3 (Dedup)**: Depends on Phase 1 completion. Can run in parallel with Phase 2.
- **Phase 4 (Shutdown)**: Depends on Phase 1 completion. Can run in parallel with Phases 2 and 3.
- **Phase 5 (Hardware)**: Depends on all implementation phases (2, 3, 4) completion.

### User Story Dependencies

- **US3 (Error Callback)**: Must be first — breaking API change affects all files
- **US1 (Timeouts)**: Independent after US3. No dependency on US2 or US4.
- **US2 (Dedup)**: Independent after US3. No dependency on US1 or US4.
- **US4 (Shutdown)**: Independent after US3. No dependency on US1 or US2.

### Parallel Opportunities

After Phase 1 completes:
- T015, T016, T017 can all run in parallel (different constants in same file, but simple enough to merge)
- Phases 2, 3, and 4 can run in parallel (different functions in pt_core.c, different concerns)
- T004-T008 can all run in parallel (different platform files)
- T009-T013 can all run in parallel (different test files)

---

## Implementation Strategy

### MVP First (Timeouts Only)

1. Complete Phase 1: Error callback signature change
2. Complete Phase 2: Timeout tuning
3. **STOP and VALIDATE**: Build and test on hardware — connections should be stable
4. This alone fixes the most critical user-reported issue

### Full Delivery

1. Phase 1: Error callback change (API foundation)
2. Phase 2: Timeouts (most impactful fix)
3. Phase 3: Dedup (prevents confusing duplicate connections)
4. Phase 4: Shutdown ordering (quality of life)
5. Phase 5: Hardware verification (proves it all works)

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Error callback change (Phase 1) is the critical path — everything else depends on it
- No new files created — all changes modify existing source
- Total estimated diff: ~100-150 lines changed across ~15 files
