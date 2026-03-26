# Implementation Plan: Code Review Fixes

**Branch**: `006-code-review-fixes` | **Date**: 2026-03-26 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/006-code-review-fixes/spec.md`

## Summary

Five fact-checked code review findings: atomic flag exchange in MacTCP/OT backends, init failure resource cleanup, reassembly admission check fix, PT_Broadcast semantics, and POSIX UDP drain loop. All fixes are internal — no public API changes, no wire protocol changes.

## Technical Context

**Language/Version**: C89/C90 (SDK), C11 (POSIX test apps)
**Primary Dependencies**: clog (logging), MacTCP (68k), Open Transport (PPC), BSD sockets (POSIX)
**Storage**: N/A
**Testing**: Hardware testing on Mac SE (68k MacTCP), Performa 6200 (PPC MacTCP), Performa 6400 (PPC OT), POSIX
**Target Platform**: Classic Mac (68k, PPC) + POSIX (Linux)
**Project Type**: Library (static)
**Performance Goals**: No regression in test_lifecycle, test_fast, test_reliable pass rates
**Constraints**: C89, zero malloc after init, <15K LOC total, interrupt-safe callbacks
**Scale/Scope**: ~5,900 LOC across 8 source files; changes touch 4 files (pt_mactcp.c, pt_ot.c, pt_messaging.c, pt_posix.c)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Three Apps Are the Spec | PASS | All fixes serve Bomberman (flag race affects 60Hz UDP), Chess (reassembly affects large messages), Chat (broadcast semantics) |
| II. SDK Handles the Protocol | PASS | All fixes are internal SDK improvements, invisible to app developer |
| III. Honest About Platform Limits | PASS | No performance claims changed |
| IV. Simple Defaults, No Knobs | PASS | No new configuration added |
| V. Pre-Allocate Everything | PASS | No new allocations; init cleanup only releases on failure path |
| VI. Adapt at Init, Not Runtime | PASS | No runtime adaptation added |
| VII. Logging Is Separate | PASS | No logging changes |
| VIII. Test Apps Prove the SDK | PASS | Existing test apps validate all fixes via hardware testing |
| IX. Keep It Small | PASS | Net LOC change estimated at +50-80 lines (cleanup code + drain loops) |
| X. C89 for Portability | PASS | All changes in SDK files are C89; `__asm__ __volatile__` is a GCC extension that compiles under `-std=c89` |

**Post-design re-check**: All gates still pass. OTAtomic* functions are part of the existing OT dependency. Inline assembly is 68k-only, guarded by `#ifdef PT_PLATFORM_MACTCP`.

## Project Structure

### Documentation (this feature)

```text
specs/006-code-review-fixes/
├── plan.md              # This file
├── research.md          # Phase 0 output (R1-R5)
├── spec.md              # Feature specification
├── contracts/           # No public API changes
│   └── internal-changes.md  # Documents internal behavior changes
├── quickstart.md        # Implementation quickstart
├── checklists/
│   └── requirements.md  # Spec quality checklist
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
src/
├── core/
│   └── pt_messaging.c      # Fix #3 (reassembly), Fix #4 (broadcast)
└── platform/
    ├── posix/
    │   └── pt_posix.c       # Fix #5 (UDP drain loop)
    ├── mactcp/
    │   └── pt_mactcp.c      # Fix #1 (atomic flags), Fix #2 (init cleanup)
    └── opentransport/
        └── pt_ot.c          # Fix #1 (atomic flags), Fix #2 (init cleanup)
```

**Structure Decision**: No new files. All changes are modifications to existing platform and core source files.

## Complexity Tracking

No constitution violations. Table not needed.
