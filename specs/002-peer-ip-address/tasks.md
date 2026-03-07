# Tasks: Peer IP Address API

**Input**: Design documents from `/specs/002-peer-ip-address/`
**Prerequisites**: plan.md, spec.md, contracts/peertalk-api-addition.md

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to
- Include exact file paths in descriptions

---

## Phase 1: Internal Support

**Purpose**: Add IP formatting and storage to internal types

- [x] T001 [P] [US1] Add `char addr_str[16]` field to `PT_Peer_Internal` in `src/core/pt_internal.h`. Place it after the existing `ip_addr` field. Initialize to empty string in `pt_alloc_peer()` in `src/core/pt_core.c` (already clears fields individually — add `ctx->peers[i].addr_str[0] = '\0'`).

- [x] T002 [P] [US1] Add `pt_format_ip()` helper function in `src/core/pt_core.c`. Signature: `static void pt_format_ip(unsigned long ip, char *buf)`. Manually extracts bytes from network-order IP and writes dotted-quad string. No `inet_ntoa`, no sprintf — pure C89 byte arithmetic. Also declare in `src/core/pt_internal.h` as non-static: `void pt_format_ip(unsigned long ip, char *buf)` so discovery.c can call it. Max output 15 chars + null = 16 bytes.

---

## Phase 2: Wire Up and Expose

**Purpose**: Format IP at assignment sites and add public API function

- [x] T003 [US1] Update IP assignment sites to also format `addr_str`. Two locations: (1) `pt_discovery_receive()` in `src/core/pt_discovery.c` — after `peer->ip_addr = source_ip` call `pt_format_ip(source_ip, peer->addr_str)`. (2) `pt_handle_incoming_connection()` in `src/core/pt_core.c` — after `peer->ip_addr = peer_ip` call `pt_format_ip(peer_ip, peer->addr_str)`. Both are the only places where `ip_addr` is set on a peer.

- [x] T004 [US1] Add `PT_PeerAddress()` public API function. Declaration in `include/peertalk.h` in the "Peer info" section (update comment from 4 to 5). Implementation in `src/core/pt_core.c` alongside `PT_PeerName()`: cast to internal, return `peer->addr_str` (or "" if NULL).

---

## Phase 3: Documentation Update

**Purpose**: Update API contract and CLAUDE.md to reflect new function count

- [x] T005 [P] [US1] Update `specs/001-peertalk-sdk/contracts/peertalk-api.md`: add `PT_PeerAddress` to the "Peer Info" section (now 5 functions), update total from 21 to 22.

- [x] T006 [P] [US1] Update `CLAUDE.md`: change "21 functions" references to "22 functions" in Project Structure section and any other occurrences. Update `include/peertalk.h` description if it mentions function count.

---

## Phase 4: Test App Verification

**Purpose**: Update test apps to exercise PT_PeerAddress and verify on hardware

- [x] T007 [US1] Update test_lifecycle in `tests/test_lifecycle.c` to log `PT_PeerAddress(peer)` alongside `PT_PeerName(peer)` in the `on_discovered` and `on_connected` callbacks. Format as `"name@ip"` (e.g., `"Player@10.188.1.213"`). Verify the address string is non-empty and contains at least one dot. Log a FAIL line if `PT_PeerAddress` returns empty string for a discovered/connected peer. Build POSIX, 68k MacTCP, and PPC OT. Run test_lifecycle on POSIX first to confirm output. Then run on real hardware: Mac SE (68k MacTCP via LaunchAPPL, 10.188.1.55), Performa 6200 (PPC MacTCP via LaunchAPPL, 10.188.1.213), and Performa 6400 (PPC OT via LaunchAPPL, 10.188.1.102). Start a POSIX test_lifecycle as the peer partner for each hardware test. Verify logs from each machine show the POSIX peer's IP in "name@ip" format. Download clog files (PT_test_lifecycle) from FTP machines to confirm. **All PASS: Mac SE (68k MacTCP), Performa 6200 (PPC MacTCP, clog verified), Performa 6400 (PPC OT, clog verified).**

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1** (T001, T002): No dependencies, both tasks modify different parts of the codebase — can run in parallel
- **Phase 2** (T003, T004): Depends on Phase 1 (needs addr_str field and pt_format_ip). T003 before T004 preferred but not required.
- **Phase 3** (T005, T006): Depends on Phase 2 (need final function signature). Both can run in parallel.
- **Phase 4** (T007): Depends on Phase 2 (needs PT_PeerAddress in public header).

### Build Verification

After Phase 2: POSIX build should compile clean.
After Phase 4: Run test_lifecycle on POSIX to verify PT_PeerAddress returns correct IP addresses.
