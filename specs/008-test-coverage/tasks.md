# Tasks: Test Coverage Gaps

**Input**: Design documents from `/specs/008-test-coverage/`
**Prerequisites**: plan.md, spec.md, research.md

**Organization**: Tasks grouped by user story. US1 (multi-peer) is the most complex; US2 and US3 can run in parallel since they touch different files.

## Format: `[ID] [P?] [Story] Description`

---

## Phase 1: Setup

- [x] T001 Verify all builds compile cleanly before changes (POSIX, 68k, PPC OT)

---

## Phase 2: User Story 1 - Multi-Peer Test (Priority: P1)

**Goal**: New test_multi app that exercises multi-peer discovery, connections, broadcast, and disconnect.

**Independent Test**: Run on 4 machines (POSIX + Mac SE + Performa 6200 + Performa 6400). Each reports PASS with 3 connections.

### Implementation

- [x] T002 [US1] Create tests/test_multi.c with includes, globals for tracking per-peer state: arrays for discovered[], connected[], broadcast_received[], counters for g_num_discovered, g_num_connected, g_num_broadcast_recv, g_num_disconnected
- [x] T003 [US1] Implement on_discovered callback: log peer name and IP, increment g_num_discovered, auto-connect via PT_Connect in tests/test_multi.c
- [x] T004 [US1] Implement on_connected callback: log peer name, increment g_num_connected, mark peer as connected in tests/test_multi.c
- [x] T005 [US1] Implement on_disconnected callback: log peer name and reason, increment g_num_disconnected in tests/test_multi.c
- [x] T006 [US1] Implement on_broadcast callback (MSG_CHAT): validate payload starts with "HELLO", increment g_num_broadcast_recv, log sender in tests/test_multi.c
- [x] T007 [US1] Implement on_error callback: log error details in tests/test_multi.c
- [x] T008 [US1] Implement main function: PT_Init, register callbacks, PT_StartDiscovery, PT_RegisterMessage(MSG_CHAT, PT_RELIABLE) in tests/test_multi.c
- [x] T009 [US1] Implement main loop phase 1 — discovery/connect settle: poll for 45 seconds or until g_num_connected >= g_num_discovered (with minimum 1 connected), then wait 5s extra settle time in tests/test_multi.c
- [x] T010 [US1] Implement main loop phase 2 — broadcast: each peer broadcasts "HELLO from <name>" via PT_Broadcast, then polls for 10 seconds waiting for broadcasts from all connected peers in tests/test_multi.c
- [x] T011 [US1] Implement main loop phase 3 — disconnect and verdict: disconnect all peers, poll for 5s grace, then print summary (discovered, connected, broadcast_sent, broadcast_received, disconnected) and PASS/FAIL in tests/test_multi.c
- [x] T012 [US1] PASS criteria: g_num_connected >= 1 AND g_num_broadcast_recv >= g_num_connected AND g_num_disconnected >= g_num_connected in tests/test_multi.c
- [x] T013 [US1] Add test_multi target to CMakeLists.txt (POSIX executable + Retro68 application, same pattern as test_chat)
- [x] T014 [US1] Verify POSIX build compiles cleanly with test_multi in build/
- [x] T015 [US1] Verify 68k MacTCP build compiles cleanly in build-68k/
- [x] T016 [US1] Verify PPC OT build compiles cleanly in build-ppc-ot/

### Hardware Validation

- [ ] T017 [US1] Run test_multi on POSIX + Mac SE (2-peer): start POSIX peer, deploy 68k binary to Mac SE via LaunchAPPL, verify both report PASS with 1 connection
- [x] T018 [US1] Run test_multi on POSIX + Performa 6400 (2-peer): start POSIX peer, deploy PPC OT binary, verify both report PASS with 1 connection
- [ ] T019 [US1] Run test_multi on all 4 machines simultaneously: start POSIX peer, deploy to Mac SE (68k MacTCP via build-68k), Performa 6200 (PPC MacTCP via build-ppc-mactcp), Performa 6400 (PPC OT via build-ppc-ot), verify each reports PASS with 3 connections and 3 broadcasts received

**Checkpoint**: Multi-peer test verified on all hardware.

---

## Phase 3: User Story 2 - Error Path Tests (Priority: P2)

**Goal**: Validate SDK returns correct error codes for invalid API usage.

**Independent Test**: Run on POSIX. Verify all expected error codes.

- [x] T020 [P] [US2] Add error path test section to tests/test_init_only.c after existing init/poll/shutdown cycle: call PT_Send with NULL peer, verify PT_ERR_INVALID_ARG returned
- [x] T021 [US2] Add PT_Send with NULL data and len > 0, verify PT_ERR_INVALID_ARG in tests/test_init_only.c
- [x] T022 [US2] Add PT_Broadcast with no connected peers, verify PT_OK in tests/test_init_only.c
- [x] T023 [US2] Add PT_Connect with NULL peer, verify PT_ERR_INVALID_ARG in tests/test_init_only.c
- [x] T024 [US2] Add PT_Broadcast with NULL ctx, verify PT_ERR_INVALID_ARG in tests/test_init_only.c
- [x] T024b [US2] Note: PT_Send on discovered-but-not-connected peer (PT_ERR_NOT_CONNECTED) is implicitly tested in test_multi when peers are discovered before connections complete. No standalone test needed.
- [x] T025 [US2] Wrap error checks in a pass/fail counter, add to test verdict in tests/test_init_only.c
- [x] T026 [US2] Verify POSIX build compiles cleanly in build/
- [x] T027 [US2] Verify 68k and PPC OT builds compile cleanly
- [x] T028 [US2] Run test_init_only on POSIX, verify PASS with all error checks passing

**Checkpoint**: Error path coverage verified.

---

## Phase 4: User Story 3 - Lifecycle Additions (Priority: P3)

**Goal**: Exercise PT_StopDiscovery, PT_SetName, and validate PT_OnPeerLost in test_lifecycle.

**Independent Test**: Run enhanced test_lifecycle between POSIX and Performa 6400.

- [x] T029 [P] [US3] Add g_peers_lost counter and on_peer_lost callback that increments it in tests/test_lifecycle.c
- [x] T030 [US3] Register on_peer_lost callback in main() of tests/test_lifecycle.c
- [x] T031 [US3] Add stop/start discovery phase between first and second disconnect: after first disconnect, call PT_StopDiscovery, record g_num_discovered, poll for 5s, verify g_num_discovered unchanged, call PT_StartDiscovery in tests/test_lifecycle.c
- [x] T032 [US3] Add PT_SetName("Renamed") call after PT_StartDiscovery in the stop/start phase in tests/test_lifecycle.c
- [x] T033 [US3] After final disconnect, call PT_StopDiscovery and poll for 18 seconds (15s discovery timeout + 3s margin) waiting for on_peer_lost to fire in tests/test_lifecycle.c
- [x] T034 [US3] Update PASS criteria: require g_peers_lost >= 1 in addition to existing connect/disconnect counts in tests/test_lifecycle.c
- [x] T035 [US3] Verify POSIX, 68k, and PPC OT builds compile cleanly
- [x] T036 [US3] Run enhanced test_lifecycle on POSIX + Performa 6400, verify PASS

### Hardware Validation

- [ ] T037 [US3] Run enhanced test_lifecycle on POSIX + Mac SE, verify PASS
- [ ] T038 [US3] Run enhanced test_lifecycle on POSIX + Performa 6200, verify PASS (if machine available)

**Checkpoint**: All lifecycle additions verified.

---

## Phase 5: Polish

- [ ] T039 Verify all builds compile with zero new warnings (POSIX, 68k, PPC OT)
- [ ] T040 Count total test app LOC, verify no single test exceeds 400 lines

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: Verify builds first
- **US1 Multi-peer (Phase 2)**: Depends on Setup only. Most complex, start first.
- **US2 Error paths (Phase 3)**: Depends on Setup only. Can run in parallel with US1 (different files).
- **US3 Lifecycle (Phase 4)**: Depends on Setup only. Can run in parallel with US1 (different files). Cannot parallel with US2 if test_init_only is shared, but they touch different files.
- **Polish (Phase 5)**: After all user stories.

### Parallel Opportunities

```
US1 (test_multi.c) and US2 (test_init_only.c) and US3 (test_lifecycle.c) — all different files, can run in parallel.
Hardware tests must be sequential (one test per machine at a time via LaunchAPPL).
```

---

## Implementation Strategy

### MVP First (US1 Multi-Peer Only)

1. Setup → US1 (test_multi) → 2-peer hardware test → 4-peer hardware test
2. This alone covers the highest-value gap

### Full Delivery

1. US1 + US2 + US3 in parallel (all different files)
2. Build verify
3. Hardware tests sequentially: test_multi 4-peer, test_init_only, test_lifecycle

---

## Notes

- test_multi uses dynamic peer counting (not hardcoded 3) so it works with 2, 3, or 4 peers
- Mac SE is slowest — allow generous discovery timeout (45s)
- PT_OnPeerLost adds ~18s to test_lifecycle runtime (15s discovery timeout + margin)
- Hardware tests run one at a time per machine (LaunchAPPL constraint)
