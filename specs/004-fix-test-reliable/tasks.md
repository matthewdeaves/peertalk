# Tasks: Fix test_reliable Turn Deadlock

**Input**: Design documents from `/specs/004-fix-test-reliable/`

## Format: `[ID] [P?] [Story] Description`

---

## Phase 1: Fix First-Mover Logic

**Goal**: Replace name-based tiebreaker with initiator-based tiebreaker. The side that calls PT_Connect (from on_discovered) goes first. This is deterministic because spec 003's IP tiebreaker ensures only one side initiates.

- [x] T001 [US1] In tests/test_reliable.c: remove `g_is_first = (name[0] <= 'M')` from main() (line 204). Change the g_is_first global initializer (line 41) to 0. First-mover will now be determined at connect time, not init time.
- [x] T002 [US1] In tests/test_reliable.c: add a `static int g_initiated = 0;` global. In on_discovered (line 78), set `g_initiated = 1;` before calling PT_Connect. This tracks whether we initiated the connection or received it.
- [x] T003 [US1] In tests/test_reliable.c on_connected (line 85): replace the `if (g_is_first)` check with `if (g_initiated)`. The initiating side sends the first move. The receiving side waits for opponent. Remove the now-unused `g_is_first` global entirely.
- [x] T004 [US1] Build POSIX target and run test_reliable locally to verify both sides complete 10 moves in build/

**Checkpoint**: test_reliable passes on POSIX.

---

## Phase 2: Hardware Verification

- [ ] T005 Build 68k MacTCP target (build-68k/) and PPC OT target (build-ppc-ot/) and verify clean compilation
- [ ] T006 Run test_reliable on Performa 6400 (PPC OT) with POSIX peer — verify PASS with 10 moves exchanged
- [ ] T007 Run test_reliable on Performa 6200 (PPC MacTCP) with POSIX peer — verify PASS with 10 moves exchanged
- [ ] T008 Run test_reliable on Mac SE (68k MacTCP) with POSIX peer — verify PASS with 10 moves exchanged
- [ ] T009 Download and review clog files from all hardware tests — confirm 10 sent, 10 received, order valid, payload valid, broadcast complete

**Checkpoint**: test_reliable PASS on all platforms. Feature complete.

---

## Dependencies & Execution Order

- Phase 1 first, Phase 2 after
- T001-T003 are sequential (same file, building on each other)
- T004 validates Phase 1
- T005-T008 can be done sequentially (one LaunchAPPL test at a time)

## Notes

- Only tests/test_reliable.c is modified — no SDK changes
- The initiator-based approach is simpler than IP comparison and doesn't need PT_LocalAddress
- The connecting side (who called PT_Connect) is deterministic thanks to the IP tiebreaker in spec 003
