# Tasks: Code Review Fixes

**Input**: Design documents from `/specs/006-code-review-fixes/`
**Prerequisites**: plan.md, spec.md, research.md, contracts/internal-changes.md, quickstart.md

**Tests**: No new test apps requested. Validation via existing test_lifecycle, test_fast, test_reliable on hardware.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each fix.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup

**Purpose**: Build verification before making changes

- [x] T001 Verify POSIX build compiles cleanly in build/ with cmake -DCLOG_DIR=~/clog && make
- [x] T002 [P] Verify 68k MacTCP cross-compile succeeds in build-68k/
- [x] T003 [P] Verify PPC OT cross-compile succeeds in build-ppc-ot/

**Checkpoint**: All three builds compile cleanly before any code changes begin.

---

## Phase 2: User Story 1 - Atomic Flag Exchange (Priority: P1)

**Goal**: Eliminate flag loss race between interrupt/notifier callbacks and main loop snapshot-and-clear.

**Independent Test**: Run test_lifecycle and test_reliable on Mac SE (68k MacTCP), Performa 6200 (PPC MacTCP), and Performa 6400 (PPC OT). Verify clean disconnect and no message stalls.

### MacTCP Atomic Flags

- [x] T004 [US1] Add pt_disable_interrupts() and pt_restore_interrupts() static helper functions using 68k inline assembly (__asm__ __volatile__) in src/platform/mactcp/pt_mactcp.c per R1 implementation pattern
- [x] T005 [US1] Wrap TCP stream flag snapshot-and-clear in mactcp_poll_tcp() with interrupt disable/restore bracket in src/platform/mactcp/pt_mactcp.c
- [x] T006 [US1] N/A — MacTCP has no UDP flag snapshot-and-clear (UDP_FLAG_DATA was removed per T127; poll checks read_pending/ioResult directly)
- [x] T007 [US1] Verify 68k MacTCP build compiles cleanly with new inline assembly in build-68k/

### OT Atomic Flags

- [x] T008 [US1] Define EVT_BIT_* bit-index constants (DATA=0, DISCONNECT=1, ORDREL=2, CONNECT=3, LISTEN=4, PASSCON=5, GODATA=6) in src/platform/opentransport/pt_ot.c
- [x] T009 [US1] Change volatile unsigned long flags to volatile UInt8 flags in OT endpoint slot struct and listener struct in src/platform/opentransport/pt_ot.c
- [x] T010 [US1] Replace all slot->flags |= EVT_xxx in TCP notifier callback with OTAtomicSetBit(&slot->flags, EVT_BIT_xxx) in src/platform/opentransport/pt_ot.c
- [x] T011 [US1] Replace all listener flags |= EVT_xxx in listener notifier callback with OTAtomicSetBit calls in src/platform/opentransport/pt_ot.c
- [x] T012 [US1] Replace all UDP notifier flag sets with OTAtomicSetBit calls in src/platform/opentransport/pt_ot.c
- [x] T013 [US1] Replace snapshot-and-clear in TCP poll loop with individual OTAtomicClearBit() calls (each returns previous state) in src/platform/opentransport/pt_ot.c
- [x] T014 [US1] Replace snapshot-and-clear in listener poll with individual OTAtomicClearBit() calls in src/platform/opentransport/pt_ot.c
- [x] T015 [US1] Replace snapshot-and-clear in UDP poll with individual OTAtomicClearBit() calls in src/platform/opentransport/pt_ot.c
- [x] T016 [US1] Verify PPC OT build compiles cleanly with OTAtomic* calls in build-ppc-ot/

### Hardware Validation

- [ ] T017 [US1] Build and deploy test_lifecycle to Mac SE via LaunchAPPL, run against POSIX peer, verify PASS — BLOCKED: Mac SE offline
- [x] T018 [US1] Build and deploy test_lifecycle to Performa 6200 via LaunchAPPL, run against POSIX peer, verify PASS
- [x] T019 [US1] Build and deploy test_lifecycle to Performa 6400 via LaunchAPPL, run against POSIX peer, verify PASS
- [ ] T020 [US1] Build and deploy test_reliable to Mac SE via LaunchAPPL, run against POSIX peer, verify PASS — BLOCKED: Mac SE offline
- [x] T021 [US1] Build and deploy test_reliable to Performa 6200 via LaunchAPPL, run against POSIX peer, verify PASS
- [x] T022 [US1] Build and deploy test_reliable to Performa 6400 via LaunchAPPL, run against POSIX peer, verify PASS

**Checkpoint**: Atomic flag exchange verified on all three hardware targets. No regressions.

---

## Phase 3: User Story 2 - Init Failure Cleanup (Priority: P2)

**Goal**: Release all previously created resources when a later init step fails.

**Independent Test**: Code inspection to verify every failure path releases resources. Hardware test: test_lifecycle still passes (init succeeds, normal path unaffected).

### MacTCP Init Cleanup

- [x] T023 [US2] Add goto-based cleanup labels (fail_msg_udp, fail_disc_udp, fail_tcp, fail_upp) at end of mactcp_init() in src/platform/mactcp/pt_mactcp.c
- [x] T024 [US2] Implement cleanup code at each label: TCPRelease streams, DisposePtr buffers, UDPRelease streams, DisposeTCPNotifyUPP/DisposeUDPNotifyUPP per R3 sequence in src/platform/mactcp/pt_mactcp.c
- [x] T025 [US2] Replace each bare return PT_ERR_INIT with goto to appropriate cleanup label in mactcp_init() in src/platform/mactcp/pt_mactcp.c
- [x] T026 [US2] Verify 68k MacTCP build compiles cleanly in build-68k/

### OT Init Cleanup

- [x] T027 [P] [US2] Add goto-based cleanup labels (fail_msg_udp, fail_disc_udp, fail_tcp, fail_listener, fail_upp) at end of ot_init() in src/platform/opentransport/pt_ot.c
- [x] T028 [US2] Implement cleanup code at each label: OTCloseProvider endpoints, DisposeOTNotifyUPP for each UPP, CloseOpenTransport last per R4 sequence in src/platform/opentransport/pt_ot.c
- [x] T029 [US2] Replace each bare return PT_ERR_INIT (after resource creation) with goto to appropriate cleanup label in ot_init() in src/platform/opentransport/pt_ot.c
- [x] T030 [US2] Verify PPC OT build compiles cleanly in build-ppc-ot/

### Hardware Validation

- [ ] T031 [US2] Run test_lifecycle on Mac SE against POSIX peer, verify PASS — BLOCKED: Mac SE offline
- [x] T032 [US2] Run test_lifecycle on Performa 6400 against POSIX peer, verify PASS

**Checkpoint**: Init failure cleanup verified by code inspection. No regressions on hardware.

---

## Phase 4: User Story 3 - Reassembly Admission Check Fix (Priority: P3)

**Goal**: Accept any chunked message whose total reassembled size fits within the reassembly buffer.

**Independent Test**: Run test_reliable on POSIX (exercises chunking). Send near-buffer-limit messages if test_reliable covers this range.

- [x] T033 [US3] Replace aggregate total_size admission check (total * chunk_payload) with per-chunk bounds check (offset + chunk_payload <= reassembly_buf_size) on first chunk arrival in src/core/pt_messaging.c
- [x] T034 [US3] Ensure per-chunk bounds check also validates offset + chunk_payload on subsequent chunk arrivals (not just first) in src/core/pt_messaging.c
- [x] T035 [US3] Verify POSIX build compiles cleanly in build/
- [x] T036 [US3] Run test_reliable on POSIX, verify PASS (verified via Performa 6200 + POSIX peer)

### Hardware Validation

- [ ] T037 [US3] Run test_reliable on Mac SE against POSIX peer, verify PASS — BLOCKED: Mac SE offline
- [x] T038 [US3] Run test_reliable on Performa 6400 against POSIX peer, verify PASS

**Checkpoint**: Reassembly correctly accepts messages up to buffer limit. No regressions.

---

## Phase 5: User Story 4 - PT_Broadcast Semantics (Priority: P4)

**Goal**: PT_Broadcast returns PT_OK when no peers are connected (no-op, not error).

**Independent Test**: Run test_reliable on POSIX (calls PT_Broadcast in its flow). Verify no regression.

- [x] T039 [US4] Change PT_Broadcast return logic: track sent_count and fail_count separately, return PT_OK when no sends attempted, PT_ERR_SEND_FAILED only when fail_count > 0 in src/core/pt_messaging.c
- [x] T040 [US4] Verify POSIX build compiles cleanly in build/
- [x] T041 [US4] Run test_reliable on POSIX, verify PASS (broadcast phase verified on both Performa 6200 and 6400)

**Checkpoint**: PT_Broadcast semantics corrected. No regressions.

---

## Phase 6: User Story 5 - POSIX UDP Drain Loop (Priority: P5)

**Goal**: POSIX backend reads all pending UDP datagrams per poll cycle, matching OT behavior.

**Independent Test**: Run test_fast on POSIX. Verify inter-arrival latency does not degrade.

- [x] T042 [US5] Confirmed: UDP sockets already call make_nonblocking(fd) via create_udp_socket() at line 68 of src/platform/posix/pt_posix.c — no changes needed, drain loop will work correctly
- [x] T043 [US5] Wrap discovery UDP recvfrom in for(;;) loop, break when recvfrom returns -1 with errno EAGAIN/EWOULDBLOCK in src/platform/posix/pt_posix.c
- [x] T044 [US5] Wrap message UDP recvfrom in for(;;) loop, break when recvfrom returns -1 with errno EAGAIN/EWOULDBLOCK in src/platform/posix/pt_posix.c
- [x] T045 [US5] Verify POSIX build compiles cleanly in build/
- [ ] T046 [US5] Run test_fast on POSIX, verify PASS — requires two POSIX machines (single machine can't bind same ports twice)

**Checkpoint**: POSIX UDP drain loop matches OT behavior. No regressions.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Final verification across all platforms

- [x] T047 Verify POSIX build compiles with zero warnings under -Wall -Wextra in build/
- [x] T048 [P] Verify 68k MacTCP build compiles cleanly in build-68k/ (pre-existing warnings only: pt_memcpy_isr unused, TickCount sign-compare, Dispose UPP empty-body)
- [x] T049 [P] Verify PPC OT build compiles cleanly in build-ppc-ot/ (pre-existing warning only: pt_memcpy_isr unused)
- [x] T050 Count total SDK LOC and verify under 15,000 lines — 4,398 lines (Constitution principle IX)
- [ ] T051 Run test_fast on POSIX to verify no latency regression — requires two POSIX machines
- [x] T052 Run test_lifecycle on Performa 6200 + Performa 6400 as final regression pass (Mac SE offline)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - verify builds first
- **US1 Atomic Flags (Phase 2)**: Depends on Setup. Most critical fix, do first.
- **US2 Init Cleanup (Phase 3)**: Depends on Setup. Can run in parallel with US1 since changes are in different functions (init vs poll), BUT both touch pt_mactcp.c and pt_ot.c — run sequentially to avoid merge conflicts.
- **US3 Reassembly (Phase 4)**: Depends on Setup only. Independent file (pt_messaging.c). Can run in parallel with US1/US2.
- **US4 Broadcast (Phase 5)**: Depends on Setup only. Same file as US3 (pt_messaging.c) — run after US3.
- **US5 POSIX UDP (Phase 6)**: Depends on Setup only. Independent file (pt_posix.c). Can run in parallel with US1-US4.
- **Polish (Phase 7)**: Depends on all user stories complete.

### User Story Dependencies

- **US1 (Atomic Flags)**: Independent — no dependency on other stories
- **US2 (Init Cleanup)**: Independent — but shares files with US1, do after US1
- **US3 (Reassembly)**: Independent — different file from US1/US2
- **US4 (Broadcast)**: Independent — shares file with US3, do after US3
- **US5 (POSIX UDP)**: Independent — different file from all others

### Parallel Opportunities

```
Phase 1: T001 | T002 + T003 (parallel)

Phase 2 (US1):
  MacTCP: T004 → T005 → T007 (sequential, same file; T006 is N/A)
  OT:     T008 → T009 → T010 → T011 → T012 → T013 → T014 → T015 → T016 (sequential, same file)
  MacTCP and OT tracks can run in parallel (different files)
  Hardware: T017 + T018 + T019 (one at a time per machine)

Phase 4 (US3) can run in parallel with Phase 2 (US1) — different files
Phase 6 (US5) can run in parallel with Phase 2 (US1) — different files
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (verify builds)
2. Complete Phase 2: US1 Atomic Flags
3. **STOP and VALIDATE**: Hardware test on all three targets
4. This alone fixes the most impactful bug (60-second hangs)

### Incremental Delivery

1. Setup → US1 (Atomic Flags) → Hardware test
2. US2 (Init Cleanup) → Build verify
3. US3 (Reassembly) + US4 (Broadcast) → POSIX test
4. US5 (POSIX UDP) → POSIX test
5. Final hardware regression pass

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story is independently testable
- Hardware tests run one at a time per machine (LaunchAPPL constraint)
- Commit after each completed user story phase
