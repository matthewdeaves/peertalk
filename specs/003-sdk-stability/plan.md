# Implementation Plan: SDK Stability Improvements

**Branch**: `003-sdk-stability` | **Date**: 2026-03-07 | **Spec**: `specs/003-sdk-stability/spec.md`
**Input**: Feature specification from `/specs/003-sdk-stability/spec.md`

## Summary

Four stability improvements driven by real-world csend testing on Classic Mac hardware: increase TCP/discovery timeouts to prevent spurious disconnects, add IP-based tiebreaker to prevent duplicate connections, add peer context to error callbacks (breaking API change), and clear callbacks before PT_Shutdown teardown.

## Technical Context

**Language/Version**: C89/C90 (SDK), C11 (POSIX test apps)
**Primary Dependencies**: clog (logging), MacTCP (68k), Open Transport (PPC), BSD sockets (POSIX)
**Storage**: N/A
**Testing**: Hardware test apps (test_init_only, test_lifecycle, test_fast, test_reliable) on real Classic Mac hardware + POSIX
**Target Platform**: POSIX (Linux/macOS), MacTCP (68k), Open Transport (PPC/late 68k)
**Project Type**: Library (C networking SDK)
**Performance Goals**: Stable connections on slow hardware (68000 @ 8MHz), zero spurious disconnects over 5+ minutes
**Constraints**: Zero malloc after PT_Init, C89 in SDK code, pre-allocated buffers, poll-based I/O
**Scale/Scope**: ~3,900 LOC, 22-function API (23 after error callback change), 3 platform backends

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Three Apps Are the Spec | PASS | All four items serve Chat (csend). Timeouts/dedup also serve Bomberman and Chess. |
| II. SDK Handles the Protocol | PASS | Dedup tiebreaker and timeout tuning are invisible protocol details. |
| III. Honest About Platform Limits | PASS | Timeout values based on real hardware testing observations. |
| IV. Simple Defaults, No Knobs | PASS | Only changing defaults. No new config API. No knobs added. |
| V. Pre-Allocate Everything | PASS | No new allocations. Error callback change is signature-only. |
| VI. Adapt at Init, Not Runtime | PASS | Timeouts are compile-time constants, not runtime-adapted. |
| VII. Logging Is Separate | PASS | No changes to logging. |
| VIII. Test Apps Prove the SDK | PASS | All test apps updated for new error callback signature. |
| IX. Keep It Small | PASS | Estimated +50-100 lines for dedup logic. Well within 15K budget. |
| X. C89 for Portability | PASS | All changes are C89-compatible. |

No violations. All gates pass.

## Project Structure

### Documentation (this feature)

```text
specs/003-sdk-stability/
├── plan.md              # This file
├── spec.md              # Feature specification
├── research.md          # Phase 0 output
├── contracts/           # Phase 1 output (API contract changes)
├── checklists/          # Validation checklists
│   └── requirements.md
└── tasks.md             # Phase 2 output (/speckit.tasks)
```

### Source Code (repository root)

```text
include/peertalk.h              # Error callback signature change (FR-009)
src/core/pt_internal.h          # Timeout constant changes (FR-001 to FR-003)
src/core/pt_core.c              # Dedup logic, shutdown ordering, error dispatch
src/core/pt_discovery.c         # Discovery timeout uses new constant
src/platform/mactcp/pt_mactcp.c # MacTCP timeout values (FR-004, FR-005)
tests/test_lifecycle.c          # Updated error callback signature
tests/test_fast.c               # Updated error callback signature
tests/test_reliable.c           # Updated error callback signature
tests/test_init_only.c          # Updated error callback signature
tests/test_chat.c               # Updated error callback signature
```

**Structure Decision**: All changes are modifications to existing files. No new files needed in source tree.

## Complexity Tracking

No constitution violations to justify.
