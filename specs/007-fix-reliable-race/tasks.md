# Tasks: Fix test_reliable Responder Race

**Input**: Design documents from `/specs/007-fix-reliable-race/`

## Phase 1: Fix

- [x] T001 [US1] In on_move() in tests/test_reliable.c, after the responder sends its final move (g_moves_sent reaches TOTAL_TURNS), set g_moves_done = 1 and g_moves_done_time = test_time_sec()
- [x] T002 [US1] Verify POSIX, 68k, and PPC builds compile cleanly

## Phase 2: Hardware Validation

- [x] T003 [US1] Run test_reliable on Mac SE against POSIX peer, verify PASS
- [ ] T004 [US1] Run test_reliable on Performa 6200 against POSIX peer, verify PASS — machine offline
- [x] T005 [US1] Run test_reliable on Performa 6400 against POSIX peer, verify PASS
