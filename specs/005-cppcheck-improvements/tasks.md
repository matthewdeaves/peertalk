# Tasks: Cppcheck Code Quality Improvements

**Input**: Design documents from `/specs/005-cppcheck-improvements/`
**Prerequisites**: plan.md, spec.md

**Tests**: No test tasks needed - verification is via cppcheck re-run and existing test suite.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup

**Purpose**: Verify baseline and understand current state

- [x] T001 Run cppcheck baseline to document all 18 warnings in src/

---

## Phase 2: User Story 1 - Fix Unsigned Comparison Bug (Priority: P1) 🎯 MVP

**Goal**: Remove meaningless unsigned-less-than-zero comparison that produces dead code

**Independent Test**: Run `cppcheck --enable=all src/core/pt_core.c` and verify no `unsignedLessThanZero` warning

### Implementation for User Story 1

- [x] T002 [US1] Fix unsigned comparison bug at line 507 in src/core/pt_core.c
- [x] T003 [US1] Verify fix compiles on POSIX with `cmake --build build`

**Checkpoint**: Unsigned comparison bug is fixed. Run cppcheck on pt_core.c to verify.

---

## Phase 3: User Story 2 - Add Const Qualifiers to Variables (Priority: P2)

**Goal**: Add `const` qualifiers to 9 local variables that are never modified after initialization

**Independent Test**: Run cppcheck and verify zero `constVariablePointer` warnings

### Implementation for User Story 2

- [x] T004 [P] [US2] Add const to variable `b` at line 83 in src/core/pt_core.c
- [x] T005 [P] [US2] Add const to variable `ctx` at line 614 in src/core/pt_core.c
- [x] T006 [P] [US2] Add const to variable `peer` at lines 640, 647, 654 in src/core/pt_core.c
- [x] T007 [P] [US2] Add const to variable `us` at line 442 in src/platform/mactcp/pt_mactcp.c
- [x] T008 [P] [US2] Add const to variable `ts` at line 626 in src/platform/mactcp/pt_mactcp.c
- [x] T009 [P] [US2] Add const to variable `peer` at line 662 in src/platform/mactcp/pt_mactcp.c
- [x] T010 [P] [US2] Add const to variable `accepted` at line 421 in src/platform/posix/pt_posix.c
- [x] T011 [US2] Verify all const variable changes compile on POSIX with `cmake --build build`

**Checkpoint**: All 9 constVariablePointer warnings resolved. Run cppcheck to verify.

---

## Phase 4: User Story 3 - Add Const Qualifiers to Function Parameters (Priority: P2)

**Goal**: Add `const` qualifiers to 3 function parameters that are not modified within their function body

**Independent Test**: Run cppcheck and verify zero `constParameterPointer` warnings

### Implementation for User Story 3

- [ ] T012 [P] [US3] Add const to parameter `ppeer` at line 173 in src/core/pt_core.c
- [ ] T013 [P] [US3] Add const to parameter `ts` at line 270 in src/platform/mactcp/pt_mactcp.c
- [ ] T014 [P] [US3] Add const to parameter `slot` at line 170 in src/platform/opentransport/pt_ot.c
- [ ] T015 [US3] Verify all const parameter changes compile on POSIX with `cmake --build build`

**Checkpoint**: All 3 constParameterPointer warnings resolved. Run cppcheck to verify.

---

## Phase 5: User Story 4 - Reduce Variable Scope (Priority: P3)

**Goal**: Move 3 variable declarations closer to their first use

**Independent Test**: Run cppcheck and verify zero `variableScope` warnings

### Implementation for User Story 4

- [ ] T016 [P] [US4] Reduce scope of variable `has_listener` at line 648 in src/platform/mactcp/pt_mactcp.c
- [ ] T017 [P] [US4] Reduce scope of variable `sent` at line 308 in src/platform/posix/pt_posix.c
- [ ] T018 [P] [US4] Reduce scope of variable `n` at line 497 in src/platform/posix/pt_posix.c
- [ ] T019 [US4] Verify scope changes maintain C89 compliance (declarations at block start) and compile on POSIX

**Checkpoint**: All 3 variableScope warnings resolved while maintaining C89 compliance.

---

## Phase 6: User Story 5 - Evaluate Callback Const Parameters (Priority: P3)

**Goal**: Evaluate and potentially fix callback function parameters for const correctness

**Independent Test**: Run cppcheck and verify `constParameterCallback` warnings are either resolved or documented

### Implementation for User Story 5

- [ ] T020 [US5] Evaluate adding const to `peer` parameter in `mactcp_udp_send` at line 471 in src/platform/mactcp/pt_mactcp.c
- [ ] T021 [US5] Evaluate adding const to `peer` parameter in `posix_udp_send` at line 229 in src/platform/posix/pt_posix.c
- [ ] T022 [US5] If function pointer types need updating, modify typedef in include/peertalk.h or relevant header
- [ ] T023 [US5] Verify callback changes compile on all platforms or document as intentionally deferred

**Checkpoint**: Callback const parameters either fixed or documented with reasoning.

---

## Phase 7: Polish & Verification

**Purpose**: Final verification across all platforms

- [ ] T024 Run cppcheck with full options and verify zero warnings for addressed categories
- [ ] T025 Run existing test suite to verify no behavioral changes
- [ ] T026 Verify build succeeds on POSIX platform
- [ ] T027 Document any warnings intentionally not fixed (with reasoning) in spec.md Assumptions section

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - establishes baseline
- **User Stories (Phase 2-6)**: All independent, can run in any order after Setup
- **Polish (Phase 7)**: Depends on all user story phases complete

### User Story Dependencies

- **User Story 1 (P1)**: No dependencies - single file change
- **User Story 2 (P2)**: No dependencies - changes to 3 files, all parallelizable
- **User Story 3 (P2)**: No dependencies - changes to 3 files, all parallelizable
- **User Story 4 (P3)**: No dependencies - changes to 2 files, all parallelizable
- **User Story 5 (P3)**: May depend on understanding function pointer types in peertalk.h

### Parallel Opportunities

All tasks within User Stories 2, 3, and 4 are marked [P] and can run in parallel since they modify different lines in different files. User Story 5 tasks should be done sequentially due to potential cross-file dependencies.

---

## Parallel Example: User Story 2

```bash
# Launch all const variable fixes together (different files/lines):
Task: "Add const to variable `b` at line 83 in src/core/pt_core.c"
Task: "Add const to variable `us` at line 442 in src/platform/mactcp/pt_mactcp.c"
Task: "Add const to variable `accepted` at line 421 in src/platform/posix/pt_posix.c"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (baseline)
2. Complete Phase 2: User Story 1 (fix the actual bug)
3. **STOP and VALIDATE**: Verify bug is fixed, tests pass
4. Commit: "fix: Remove meaningless unsigned comparison in pt_core.c"

### Incremental Delivery

1. US1 (bug fix) → Commit
2. US2 (const variables) → Commit
3. US3 (const parameters) → Commit
4. US4 (variable scope) → Commit
5. US5 (callback evaluation) → Commit
6. Polish → Final verification

---

## Summary

| Phase | Tasks | Files Modified |
|-------|-------|----------------|
| Setup | 1 | - |
| US1 (Bug Fix) | 2 | pt_core.c |
| US2 (Const Vars) | 8 | pt_core.c, pt_mactcp.c, pt_posix.c |
| US3 (Const Params) | 4 | pt_core.c, pt_mactcp.c, pt_ot.c |
| US4 (Scope) | 4 | pt_mactcp.c, pt_posix.c |
| US5 (Callbacks) | 4 | pt_mactcp.c, pt_posix.c, possibly peertalk.h |
| Polish | 4 | - |
| **Total** | **27** | **4 source files** |

---

## Notes

- All changes must maintain C89 compliance (no mixed declarations in SDK code)
- Variable scope reduction (US4) requires careful placement at block start per C89
- Callback const changes (US5) may require updating function pointer typedefs
- Run cppcheck after each phase to verify progress
- Commit after each completed user story
