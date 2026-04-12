# Implementation Plan: PT_DisconnectAll and Poll Robustness Hardening

**Branch**: `011-disconnect-poll-hardening` | **Date**: 2026-04-12 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/011-disconnect-poll-hardening/spec.md`

## Summary

Add `PT_DisconnectAll(ctx)` as public API function #23 for clean lifecycle transitions (game→lobby). Harden OT and MacTCP poll paths to check OTRcv/TCPRcv error returns and validate endpoint/stream state before operating on transitional connections. Total change: ~35 lines across 4 files.

## Technical Context

**Language/Version**: C89/C90 (SDK), C11 (POSIX test apps)
**Primary Dependencies**: clog (logging), MacTCP (68k), Open Transport (PPC), BSD sockets (POSIX)
**Storage**: N/A
**Testing**: Hardware testing on Mac SE (68k MacTCP), Performa 6200 (PPC MacTCP), Performa 6400 (PPC OT), POSIX (Linux)
**Target Platform**: POSIX + Classic Mac (MacTCP 68k, OT PPC)
**Project Type**: C library (SDK)
**Performance Goals**: PT_Poll must complete every cycle without hanging; PT_DisconnectAll must complete in a single pass
**Constraints**: Zero malloc after PT_Init; C89 in SDK code; cooperative single-threaded on Classic Mac
**Scale/Scope**: ~3,900 LOC SDK, 4 test apps, 3 platform backends

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| # | Principle | Status | Evidence |
|---|-----------|--------|----------|
| I | Three Apps Are the Spec | PASS | Bomberman lobby→game→lobby, Chess rematch, Chat reconnect all need PT_DisconnectAll. Poll robustness serves all three. |
| II | SDK Handles the Protocol | PASS | PT_DisconnectAll moves lifecycle cleanup into the SDK so apps don't iterate peers manually. |
| III | Honest About Platform Limits | PASS | Research documents actual OTRcv/TCPRcv error behavior on each platform. |
| IV | Simple Defaults, No Knobs | PASS | PT_DisconnectAll takes only ctx — no options, no flags, no config. |
| V | Pre-Allocate Everything | PASS | Zero new allocations. Iterates existing peer array, uses existing send buffers for goodbye frames. |
| VI | Adapt at Init, Not Runtime | PASS | No runtime adaptation. Fixed behavior: iterate and disconnect. |
| VII | Logging Is Separate | PASS | Error logging uses clog (CLOG_DEBUG), not exposed in API. |
| VIII | Test Apps Prove the SDK | PASS | Existing test_lifecycle exercises connect/disconnect. All 4 test apps verify no regression. |
| IX | Keep It Small | PASS | ~25 lines added across 4 files. Well under 15K LOC limit. |
| X | C89 for Portability | PASS | PT_DisconnectAll and all poll fixes use C89 patterns (no mixed declarations, no // comments). |

**Gate result**: ALL PASS — no violations, no justifications needed.

## Project Structure

### Documentation (this feature)

```text
specs/011-disconnect-poll-hardening/
├── plan.md              # This file
├── spec.md              # Feature specification
├── research.md          # Phase 0: 6 research decisions
├── data-model.md        # Phase 1: entity/state documentation
├── quickstart.md        # Phase 1: build & usage guide
├── contracts/
│   └── peertalk-api-additions.md  # PT_DisconnectAll API contract
├── checklists/
│   └── requirements.md  # Spec quality validation
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
include/
└── peertalk.h              # Add PT_DisconnectAll declaration

src/core/
└── pt_core.c               # Add PT_DisconnectAll implementation

src/platform/opentransport/
└── pt_ot.c                 # OT poll: OTRcv error checks, slot state validation

src/platform/mactcp/
└── pt_mactcp.c             # MacTCP poll: stream state validation, error logging
```

**Structure Decision**: All changes are to existing files. No new files created in source tree.
