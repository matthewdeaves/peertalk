# Implementation Plan: Test Coverage Gaps

**Branch**: `008-test-coverage` | **Date**: 2026-03-26 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/008-test-coverage/spec.md`

## Summary

Three work items: new test_multi app for multi-peer testing (P1), error path validation added to a new test_errors section in test_init_only (P2), and lifecycle additions for StopDiscovery/SetName/PeerLost (P3). All are test app changes — no SDK modifications.

## Technical Context

**Language/Version**: C11 (test apps), C89 (SDK unchanged)
**Primary Dependencies**: PeerTalk SDK, clog, test_common.h framework
**Testing**: Hardware on Mac SE, Performa 6200, Performa 6400, POSIX
**Target Platform**: Classic Mac (68k, PPC) + POSIX (Linux)
**Project Type**: Test app additions
**Constraints**: Each test app under 400 lines; test_common.h globals are single-peer oriented (test_multi needs its own tracking)

## Constitution Check

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Three Apps Are the Spec | PASS | Multi-peer serves Bomberman (2-6 players) and Chat (any number) |
| VIII. Test Apps Prove the SDK | PASS | Directly adding tests that prove untested SDK paths |
| IX. Keep It Small | PASS | ~200 lines for test_multi, ~30 for errors, ~40 for lifecycle additions |
| X. C89 for Portability | PASS | Test apps may use C11 |

## Project Structure

```text
tests/
├── test_multi.c        # NEW: multi-peer test (US1)
├── test_init_only.c    # MODIFIED: add error path checks (US2)
├── test_lifecycle.c    # MODIFIED: add stop/start/setname/peerlost (US3)
└── test_common.h       # No changes needed
```

**CMakeLists.txt**: Add test_multi target (same pattern as other test apps).

## Complexity Tracking

No violations.
